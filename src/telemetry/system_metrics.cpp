#include "kf2/telemetry/system_metrics.hpp"
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <algorithm>
#include <bit>
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

std::optional<std::unordered_map<std::uint32_t, std::uint64_t>>
query_process_thread_ticks(std::uint32_t pid) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;

    std::unordered_map<std::uint32_t, std::uint64_t> ticks;
    THREADENTRY32 entry{sizeof(entry)};
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != pid) continue;
            const HANDLE thread = OpenThread(
                THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
            if (!thread) continue;
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (GetThreadTimes(thread, &creation, &exit, &kernel, &user)) {
                ticks.emplace(entry.th32ThreadID, value(kernel) + value(user));
            }
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return ticks;
}
}

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
    : identity_{std::move(identity)} {}

Result<ProcessMetrics> ProcessMetricSampler::sample() {
    auto rebound = game::bind_game_process(identity_.pid, identity_.executable);
    if (!rebound.has_value() ||
        rebound.value().process_start_id != identity_.process_start_id) {
        return Result<ProcessMetrics>::failure(
            {ErrorCode::stale_data, L"Metric process identity changed", 0});
    }
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
    const auto cpu_capacity = ok
        ? query_process_cpu_capacity(process) : std::nullopt;
    const DWORD native = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process);
    if (!ok) return Result<ProcessMetrics>::failure(
        {ErrorCode::platform_failure, L"Process metric query failed", native});
    CpuTimes current{value(system_kernel) + value(system_user),
                     value(process_kernel) + value(process_user)};
    ProcessMetrics result;
    if (previous_) result.cpu_percent = calculate_cpu_percent(*previous_, current);
    previous_ = current;

    // Thread enumeration is deliberately throttled. It supplies the missing
    // critical-thread signal without adding work to every overlay refresh.
    constexpr std::uint64_t kThreadSampleIntervalMs = 500;
    const std::uint64_t thread_now_ms = GetTickCount64();
    if (!previous_thread_sample_ms_ ||
        (thread_now_ms >= *previous_thread_sample_ms_ &&
         thread_now_ms - *previous_thread_sample_ms_ >=
             kThreadSampleIntervalMs)) {
        if (auto current_thread_ticks =
                query_process_thread_ticks(identity_.pid)) {
            if (previous_thread_sample_ms_ &&
                thread_now_ms > *previous_thread_sample_ms_) {
                std::optional<double> busiest;
                double summed_thread_percent = 0.0;
                std::uint32_t active_threads = 0;
                const auto elapsed_ms =
                    thread_now_ms - *previous_thread_sample_ms_;
                for (const auto& [thread_id, current_ticks] :
                     *current_thread_ticks) {
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
            previous_thread_ticks_ = std::move(*current_thread_ticks);
            previous_thread_sample_ms_ = thread_now_ms;
        }
    }
    result.critical_core_percent = cached_critical_core_percent_;
    result.effective_core_usage = cached_effective_core_usage_;
    result.dominant_thread_share_percent =
        cached_dominant_thread_share_percent_;
    result.active_cpu_threads = cached_active_cpu_threads_;
    if (cpu_capacity) {
        result.affinity_logical_processors =
            cpu_capacity->affinity_logical_processors;
        result.affinity_physical_cores =
            cpu_capacity->affinity_physical_cores;
        result.system_logical_processors =
            cpu_capacity->system_logical_processors;
    }
    result.working_set_bytes = memory.WorkingSetSize;
    result.private_bytes = memory.PrivateUsage;
    return Result<ProcessMetrics>::success(result);
}
}  // namespace kf2::telemetry
