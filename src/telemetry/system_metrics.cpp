#include "kf2/telemetry/system_metrics.hpp"
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <algorithm>
#include <bit>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace kf2::telemetry {
namespace {
std::uint64_t value(FILETIME time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) |
           time.dwLowDateTime;
}

struct ProcessorCoreMask {
    WORD group{0};
    KAFFINITY mask{0};
};

const std::vector<ProcessorCoreMask>& processor_core_masks() {
    static const auto masks = [] {
        DWORD length = 0;
        if (GetLogicalProcessorInformationEx(
                RelationProcessorCore, nullptr, &length) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0) {
            return std::vector<ProcessorCoreMask>{};
        }
        std::vector<std::byte> storage(length);
        auto* information = reinterpret_cast<
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data());
        if (!GetLogicalProcessorInformationEx(
                RelationProcessorCore, information, &length)) {
            return std::vector<ProcessorCoreMask>{};
        }
        std::vector<ProcessorCoreMask> result;
        DWORD offset = 0;
        while (offset < length) {
            auto* current = reinterpret_cast<
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                    storage.data() + offset);
            if (current->Size == 0 || offset + current->Size > length) break;
            if (current->Relationship == RelationProcessorCore &&
                current->Processor.GroupCount > 0) {
                const auto& affinity = current->Processor.GroupMask[0];
                result.push_back({affinity.Group, affinity.Mask});
            }
            offset += current->Size;
        }
        return result;
    }();
    return masks;
}

struct ProcessCpuCapacity {
    std::uint32_t affinity_logical_processors{0};
    std::optional<std::uint32_t> affinity_physical_cores;
    std::uint32_t system_logical_processors{0};
};

std::optional<ProcessCpuCapacity> query_process_cpu_capacity(HANDLE process) {
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (!GetProcessAffinityMask(process, &process_mask, &system_mask) ||
        process_mask == 0) {
        return std::nullopt;
    }
    ProcessCpuCapacity result;
    result.affinity_logical_processors = static_cast<std::uint32_t>(
        std::popcount(static_cast<std::uintptr_t>(process_mask)));
    result.system_logical_processors =
        GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    // GetProcessAffinityMask describes one processor group. Report physical
    // affinity capacity only when the machine itself has one group; this
    // avoids presenting an incomplete count on >64-logical-processor hosts.
    if (GetActiveProcessorGroupCount() == 1) {
        std::uint32_t physical = 0;
        for (const auto& core : processor_core_masks()) {
            if (core.group == 0 && (core.mask & process_mask) != 0) ++physical;
        }
        if (physical > 0) result.affinity_physical_cores = physical;
    }
    return result;
}

}

class ProcessMetricSampler::ThreadTracker final {
public:
    ~ThreadTracker() {
        for (const auto& [thread_id, handle] : handles_) {
            static_cast<void>(thread_id);
            CloseHandle(handle);
        }
    }

    bool refresh(std::uint32_t pid) {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        std::unordered_set<std::uint32_t> current;
        THREADENTRY32 entry{sizeof(entry)};
        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID == pid) {
                    current.insert(entry.th32ThreadID);
                }
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);

        for (auto iterator = handles_.begin(); iterator != handles_.end();) {
            if (current.contains(iterator->first)) {
                ++iterator;
            } else {
                CloseHandle(iterator->second);
                iterator = handles_.erase(iterator);
            }
        }
        for (const auto thread_id : current) {
            if (handles_.contains(thread_id)) continue;
            const HANDLE thread = OpenThread(
                THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread_id);
            if (thread) handles_.emplace(thread_id, thread);
        }
        return true;
    }

    std::unordered_map<std::uint32_t, std::uint64_t> sample() const {
        std::unordered_map<std::uint32_t, std::uint64_t> ticks;
        ticks.reserve(handles_.size());
        for (const auto& [thread_id, thread] : handles_) {
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (GetThreadTimes(thread, &creation, &exit, &kernel, &user)) {
                ticks.emplace(thread_id, value(kernel) + value(user));
            }
        }
        return ticks;
    }

    [[nodiscard]] bool empty() const { return handles_.empty(); }

private:
    std::unordered_map<std::uint32_t, HANDLE> handles_;
};

Result<HardwareInventory> query_hardware_inventory() {
    HardwareInventory result;
    result.physical_cores = static_cast<std::uint32_t>(
        processor_core_masks().size());
    result.processor_groups = GetActiveProcessorGroupCount();
    result.logical_processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    ULONGLONG installed_kb = 0;
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (!result.physical_cores || !result.processor_groups ||
        !result.logical_processors ||
        !GetPhysicallyInstalledSystemMemory(&installed_kb) ||
        !GlobalMemoryStatusEx(&memory)) {
        return Result<HardwareInventory>::failure(
            {ErrorCode::platform_failure, L"Hardware inventory is unavailable",
             GetLastError()});
    }
    result.installed_memory_bytes = installed_kb * 1024ULL;
    result.available_memory_bytes = memory.ullAvailPhys;
    return Result<HardwareInventory>::success(result);
}

Result<SystemMemoryMetrics> query_system_memory_metrics() {
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (!GlobalMemoryStatusEx(&memory) || memory.ullTotalPhys == 0 ||
        memory.ullAvailPhys > memory.ullTotalPhys) {
        return Result<SystemMemoryMetrics>::failure(
            {ErrorCode::platform_failure, L"System memory telemetry is unavailable",
             GetLastError()});
    }
    const auto used = memory.ullTotalPhys - memory.ullAvailPhys;
    return Result<SystemMemoryMetrics>::success({
        memory.ullTotalPhys, memory.ullAvailPhys,
        memory.ullTotalPageFile, memory.ullAvailPageFile,
        static_cast<double>(used) * 100.0 /
            static_cast<double>(memory.ullTotalPhys)});
}

std::optional<double> calculate_cpu_percent(CpuTimes previous, CpuTimes current) {
    if (current.system_ticks <= previous.system_ticks ||
        current.process_ticks < previous.process_ticks) return std::nullopt;
    const double system = static_cast<double>(current.system_ticks - previous.system_ticks);
    const double process = static_cast<double>(current.process_ticks - previous.process_ticks);
    return std::clamp(process * 100.0 / system, 0.0, 100.0);
}

std::optional<double> calculate_system_cpu_percent(
    CpuTimes previous, CpuTimes current) {
    if (current.system_ticks <= previous.system_ticks ||
        current.idle_ticks < previous.idle_ticks) {
        return std::nullopt;
    }
    const auto total = current.system_ticks - previous.system_ticks;
    const auto idle = current.idle_ticks - previous.idle_ticks;
    if (idle > total) return std::nullopt;
    return std::clamp(
        static_cast<double>(total - idle) * 100.0 /
            static_cast<double>(total),
        0.0, 100.0);
}

std::optional<double> calculate_thread_cpu_percent(
    std::uint64_t previous_thread_ticks,
    std::uint64_t current_thread_ticks,
    std::uint64_t elapsed_ms) {
    if (elapsed_ms == 0 || current_thread_ticks < previous_thread_ticks) {
        return std::nullopt;
    }
    constexpr double kHundredNanosecondTicksPerMillisecond = 10'000.0;
    const double elapsed_ticks = static_cast<double>(elapsed_ms) *
                                 kHundredNanosecondTicksPerMillisecond;
    const double thread_ticks = static_cast<double>(
        current_thread_ticks - previous_thread_ticks);
    return std::clamp(thread_ticks * 100.0 / elapsed_ticks, 0.0, 100.0);
}

ProcessMetricSampler::ProcessMetricSampler(game::GameProcessIdentity identity)
    : identity_{std::move(identity)},
      thread_tracker_{std::make_unique<ThreadTracker>()} {}

ProcessMetricSampler::~ProcessMetricSampler() = default;
ProcessMetricSampler::ProcessMetricSampler(ProcessMetricSampler&&) noexcept =
    default;
ProcessMetricSampler& ProcessMetricSampler::operator=(
    ProcessMetricSampler&&) noexcept = default;

Result<ProcessMetrics> ProcessMetricSampler::sample() {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                 PROCESS_VM_READ, FALSE, identity_.pid);
    if (!process) return Result<ProcessMetrics>::failure(
        {ErrorCode::access_denied, L"Process metrics cannot be read", GetLastError()});
    FILETIME creation{}, exit{}, process_kernel{}, process_user{};
    FILETIME idle{}, system_kernel{}, system_user{};
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    const bool ok = GetProcessTimes(process, &creation, &exit,
                                    &process_kernel, &process_user) &&
                    GetSystemTimes(&idle, &system_kernel, &system_user) &&
                    GetProcessMemoryInfo(process,
                        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                        sizeof(memory));
    if (ok && value(creation) != identity_.process_start_id) {
        CloseHandle(process);
        return Result<ProcessMetrics>::failure(
            {ErrorCode::stale_data, L"Metric process identity changed", 0});
    }
    if (ok && !cpu_capacity_sampled_) {
        if (const auto capacity = query_process_cpu_capacity(process)) {
            cached_affinity_logical_processors_ =
                capacity->affinity_logical_processors;
            cached_affinity_physical_cores_ = capacity->affinity_physical_cores;
            cached_system_logical_processors_ =
                capacity->system_logical_processors;
        }
        cpu_capacity_sampled_ = true;
    }
    const DWORD native = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process);
    if (!ok) return Result<ProcessMetrics>::failure(
        {ErrorCode::platform_failure, L"Process metric query failed", native});
    CpuTimes current{value(system_kernel) + value(system_user),
                     value(process_kernel) + value(process_user),
                     value(idle)};
    ProcessMetrics result;
    if (previous_) {
        result.cpu_percent = calculate_cpu_percent(*previous_, current);
        result.system_cpu_percent =
            calculate_system_cpu_percent(*previous_, current);
    }
    previous_ = current;

    // Cached handles keep CPU-time sampling responsive at 500 ms. Refresh the
    // membership separately because Toolhelp enumerates every thread on the
    // machine; KF2's long-lived game/render threads stay continuously sampled,
    // while a newly created thread becomes visible within five seconds.
    constexpr std::uint64_t kThreadSampleIntervalMs = 500;
    constexpr std::uint64_t kThreadRefreshIntervalMs = 5'000;
    const std::uint64_t thread_now_ms = GetTickCount64();
    if (!previous_thread_sample_ms_ ||
        (thread_now_ms >= *previous_thread_sample_ms_ &&
         thread_now_ms - *previous_thread_sample_ms_ >=
             kThreadSampleIntervalMs)) {
        const bool refresh_due = thread_tracker_->empty() ||
            !previous_thread_refresh_ms_ ||
            thread_now_ms < *previous_thread_refresh_ms_ ||
            thread_now_ms - *previous_thread_refresh_ms_ >=
                kThreadRefreshIntervalMs;
        if (refresh_due && thread_tracker_->refresh(identity_.pid)) {
            previous_thread_refresh_ms_ = thread_now_ms;
        }
        // A transient Toolhelp failure must not discard the valid handles
        // from the previous refresh. Continue sampling them and retry the
        // membership refresh on the next telemetry tick.
        if (!thread_tracker_->empty()) {
            auto current_thread_ticks = thread_tracker_->sample();
            if (previous_thread_sample_ms_ &&
                thread_now_ms > *previous_thread_sample_ms_) {
                std::optional<double> busiest;
                double summed_thread_percent = 0.0;
                std::uint32_t active_threads = 0;
                const auto elapsed_ms =
                    thread_now_ms - *previous_thread_sample_ms_;
                for (const auto& [thread_id, current_ticks] :
                     current_thread_ticks) {
                    const auto previous = previous_thread_ticks_.find(thread_id);
                    if (previous == previous_thread_ticks_.end()) continue;
                    const auto percent = calculate_thread_cpu_percent(
                        previous->second, current_ticks, elapsed_ms);
                    if (percent) {
                        summed_thread_percent += *percent;
                        if (*percent >= 1.0) ++active_threads;
                        if (!busiest || *percent > *busiest) {
                            busiest = percent;
                        }
                    }
                }
                if (busiest) {
                    cached_critical_core_percent_ = busiest;
                    cached_effective_core_usage_ =
                        summed_thread_percent / 100.0;
                    cached_dominant_thread_share_percent_ =
                        summed_thread_percent > 0.0
                            ? std::clamp(*busiest * 100.0 /
                                             summed_thread_percent,
                                         0.0, 100.0)
                            : 0.0;
                    cached_active_cpu_threads_ = active_threads;
                }
            }
            previous_thread_ticks_ = std::move(current_thread_ticks);
            previous_thread_sample_ms_ = thread_now_ms;
        }
    }
    result.critical_core_percent = cached_critical_core_percent_;
    result.effective_core_usage = cached_effective_core_usage_;
    result.dominant_thread_share_percent =
        cached_dominant_thread_share_percent_;
    result.active_cpu_threads = cached_active_cpu_threads_;
    result.affinity_logical_processors =
        cached_affinity_logical_processors_;
    result.affinity_physical_cores = cached_affinity_physical_cores_;
    result.system_logical_processors =
        cached_system_logical_processors_;
    result.working_set_bytes = memory.WorkingSetSize;
    result.private_bytes = memory.PrivateUsage;
    return Result<ProcessMetrics>::success(result);
}
}  // namespace kf2::telemetry
