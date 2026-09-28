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

const std::vector<detail::ProcessorGroupMask>& processor_group_masks() {
    static const auto masks = [] {
        DWORD length = 0;
        if (GetLogicalProcessorInformationEx(RelationGroup, nullptr, &length) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0) {
            return std::vector<detail::ProcessorGroupMask>{};
        }
        std::vector<std::byte> storage(length);
        auto* information = reinterpret_cast<
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data());
        if (!GetLogicalProcessorInformationEx(
                RelationGroup, information, &length) ||
            information->Relationship != RelationGroup ||
            information->Group.ActiveGroupCount == 0 ||
            information->Group.ActiveGroupCount >
                information->Group.MaximumGroupCount) {
            return std::vector<detail::ProcessorGroupMask>{};
        }
        std::vector<detail::ProcessorGroupMask> result;
        result.reserve(information->Group.ActiveGroupCount);
        for (WORD group = 0;
             group < information->Group.ActiveGroupCount; ++group) {
            const auto mask = information->Group.GroupInfo[group]
                                  .ActiveProcessorMask;
            if (mask == 0) return std::vector<detail::ProcessorGroupMask>{};
            result.push_back({group, static_cast<std::uintptr_t>(mask)});
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

std::optional<std::vector<std::uint16_t>> query_process_groups(
    HANDLE process) {
    const WORD active_groups = GetActiveProcessorGroupCount();
    if (active_groups == 0) return std::nullopt;
    std::vector<USHORT> groups(active_groups);
    USHORT count = active_groups;
    if (!GetProcessGroupAffinity(process, &count, groups.data()) ||
        count == 0 || count > groups.size()) {
        return std::nullopt;
    }
    groups.resize(count);
    return groups;
}

struct CpuSetMaskQuery {
    detail::CpuSetQueryState state{detail::CpuSetQueryState::unavailable};
    std::vector<detail::ProcessorGroupMask> masks;
    bool default_affinity_spans_groups{false};
};

CpuSetMaskQuery query_process_default_cpu_set_masks(HANDLE process) {
    using QueryMasks = BOOL(WINAPI*)(
        HANDLE, PGROUP_AFFINITY, USHORT, PUSHORT);
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const auto query_masks = kernel ? reinterpret_cast<QueryMasks>(
        GetProcAddress(kernel, "GetProcessDefaultCpuSetMasks")) : nullptr;
    if (query_masks) {
        USHORT required = 0;
        if (query_masks(process, nullptr, 0, &required)) {
            return required == 0
                ? CpuSetMaskQuery{
                      detail::CpuSetQueryState::succeeded, {}, true}
                : CpuSetMaskQuery{
                      detail::CpuSetQueryState::failed, {}, true};
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0) {
            return {detail::CpuSetQueryState::failed, {}, true};
        }
        std::vector<GROUP_AFFINITY> affinities(required);
        USHORT written = required;
        if (!query_masks(
                process, affinities.data(), required, &written) ||
            written == 0 || written > required) {
            return {detail::CpuSetQueryState::failed, {}, true};
        }
        CpuSetMaskQuery result{
            detail::CpuSetQueryState::succeeded, {}, true};
        result.masks.reserve(written);
        for (USHORT index = 0; index < written; ++index) {
            result.masks.push_back({
                affinities[index].Group,
                static_cast<std::uintptr_t>(affinities[index].Mask)});
        }
        return result;
    }

    // Windows 10 exposes CPU Set IDs but not their group masks through the
    // process query. An empty list proves that the group affinity is exact;
    // a non-empty list is deliberately left unavailable instead of guessed.
    ULONG required_ids = 0;
    if (GetProcessDefaultCpuSets(process, nullptr, 0, &required_ids)) {
        return required_ids == 0
            ? CpuSetMaskQuery{detail::CpuSetQueryState::succeeded, {}, false}
            : CpuSetMaskQuery{detail::CpuSetQueryState::failed, {}, false};
    }
    return {detail::CpuSetQueryState::failed, {}, false};
}

std::optional<ProcessCpuCapacity> query_process_cpu_capacity(HANDLE process) {
    const auto& system_masks = processor_group_masks();
    const auto process_groups = query_process_groups(process);
    if (system_masks.empty() || !process_groups) return std::nullopt;

    detail::CpuCapacityObservation observation;
    observation.system_group_masks = system_masks;
    observation.process_groups = process_groups;
    const auto cpu_sets = query_process_default_cpu_set_masks(process);
    observation.cpu_set_query = cpu_sets.state;
    observation.default_cpu_set_masks = cpu_sets.masks;
    observation.default_affinity_spans_groups =
        cpu_sets.default_affinity_spans_groups;

    DWORD_PTR process_mask = 0;
    DWORD_PTR primary_system_mask = 0;
    if (GetProcessAffinityMask(
            process, &process_mask, &primary_system_mask) &&
        process_mask != 0 && primary_system_mask != 0) {
        observation.primary_group_affinity = detail::ProcessorGroupMask{
            process_groups->front(),
            static_cast<std::uintptr_t>(process_mask)};
        observation.primary_group_affinity_is_full =
            process_mask == primary_system_mask;
    }

    const auto allowed = detail::resolve_process_capacity_masks(observation);
    if (!allowed) return std::nullopt;

    ProcessCpuCapacity result;
    for (const auto& group : *allowed) {
        result.affinity_logical_processors += static_cast<std::uint32_t>(
            std::popcount(group.mask));
    }
    for (const auto& group : system_masks) {
        result.system_logical_processors += static_cast<std::uint32_t>(
            std::popcount(group.mask));
    }
    if (result.affinity_logical_processors == 0 ||
        result.system_logical_processors == 0 ||
        result.affinity_logical_processors >
            result.system_logical_processors) {
        return std::nullopt;
    }

    const auto& cores = processor_core_masks();
    if (!cores.empty()) {
        std::uint32_t physical = 0;
        for (const auto& core : cores) {
            const auto group = std::find_if(
                allowed->begin(), allowed->end(), [&](const auto& candidate) {
                    return candidate.group == core.group;
                });
            if (group != allowed->end() && (group->mask & core.mask) != 0) {
                ++physical;
            }
        }
        if (physical > 0) result.affinity_physical_cores = physical;
    }
    return result;
}

}

std::optional<std::vector<detail::ProcessorGroupMask>>
detail::resolve_process_capacity_masks(
    const CpuCapacityObservation& observation) {
    if (!observation.process_groups || observation.process_groups->empty() ||
        observation.system_group_masks.empty() ||
        observation.cpu_set_query == CpuSetQueryState::failed) {
        return std::nullopt;
    }

    auto system_masks = observation.system_group_masks;
    std::sort(system_masks.begin(), system_masks.end(),
              [](const auto& left, const auto& right) {
                  return left.group < right.group;
              });
    for (std::size_t index = 0; index < system_masks.size(); ++index) {
        if (system_masks[index].mask == 0 ||
            (index > 0 && system_masks[index - 1].group ==
                              system_masks[index].group)) {
            return std::nullopt;
        }
    }

    auto process_groups = *observation.process_groups;
    std::sort(process_groups.begin(), process_groups.end());
    if (std::adjacent_find(process_groups.begin(), process_groups.end()) !=
        process_groups.end()) {
        return std::nullopt;
    }
    for (const auto group : process_groups) {
        if (std::ranges::none_of(system_masks, [&](const auto& system) {
                return system.group == group;
            })) {
            return std::nullopt;
        }
    }

    const auto normalize = [&](std::vector<ProcessorGroupMask> masks,
                               bool require_process_group)
        -> std::optional<std::vector<ProcessorGroupMask>> {
        if (masks.empty()) return std::nullopt;
        std::sort(masks.begin(), masks.end(),
                  [](const auto& left, const auto& right) {
                      return left.group < right.group;
                  });
        std::vector<ProcessorGroupMask> normalized;
        for (const auto& mask : masks) {
            const auto system = std::find_if(
                system_masks.begin(), system_masks.end(),
                [&](const auto& candidate) {
                    return candidate.group == mask.group;
                });
            if (mask.mask == 0 || system == system_masks.end() ||
                (mask.mask & ~system->mask) != 0 ||
                (require_process_group &&
                 !std::binary_search(process_groups.begin(),
                                     process_groups.end(), mask.group))) {
                return std::nullopt;
            }
            if (!normalized.empty() &&
                normalized.back().group == mask.group) {
                normalized.back().mask |= mask.mask;
            } else {
                normalized.push_back(mask);
            }
        }
        return normalized;
    };

    if (observation.cpu_set_query == CpuSetQueryState::succeeded &&
        !observation.default_cpu_set_masks.empty()) {
        return normalize(observation.default_cpu_set_masks, true);
    }
    if (observation.cpu_set_query == CpuSetQueryState::succeeded &&
        observation.default_cpu_set_masks.empty() &&
        observation.primary_group_affinity &&
        observation.primary_group_affinity_is_full &&
        observation.default_affinity_spans_groups &&
        system_masks.size() > 1) {
        std::vector<ProcessorGroupMask> process_system_masks;
        for (const auto& mask : system_masks) {
            if (std::binary_search(process_groups.begin(),
                                   process_groups.end(), mask.group)) {
                process_system_masks.push_back(mask);
            }
        }
        return process_system_masks.empty()
            ? std::nullopt
            : std::optional{std::move(process_system_masks)};
    }
    if (observation.primary_group_affinity) {
        return normalize({*observation.primary_group_affinity}, true);
    }
    return std::nullopt;
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
