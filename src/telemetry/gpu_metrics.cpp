#include "kf2/telemetry/gpu_metrics.hpp"
#include <winternl.h>
#include <d3dkmthk.h>
#include <pdhmsg.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <map>
#include <regex>
#include <set>
#include <tuple>
#include <type_traits>

namespace kf2::telemetry {
namespace {
LUID unpack_luid(std::uint64_t packed) {
    LUID luid{};
    luid.LowPart = static_cast<DWORD>(packed);
    luid.HighPart = static_cast<LONG>(packed >> 32U);
    return luid;
}

std::optional<std::wstring> query_physical_pnp_key(
    D3DKMT_HANDLE handle, UINT physical_index) {
    UINT characters = 0;
    D3DKMT_QUERY_PHYSICAL_ADAPTER_PNP_KEY key{};
    key.PhysicalAdapterIndex = physical_index;
    key.PnPKeyType = D3DKMT_PNP_KEY_HARDWARE;
    key.pCchDest = &characters;
    D3DKMT_QUERYADAPTERINFO query{};
    query.hAdapter = handle;
    query.Type = KMTQAITYPE_PHYSICALADAPTERPNPKEY;
    query.pPrivateDriverData = &key;
    query.PrivateDriverDataSize = sizeof(key);
    static_cast<void>(D3DKMTQueryAdapterInfo(&query));
    if (characters == 0 || characters > 32'768) return std::nullopt;

    std::vector<wchar_t> buffer(static_cast<std::size_t>(characters) + 1U);
    key.pDest = buffer.data();
    key.pCchDest = &characters;
    if (D3DKMTQueryAdapterInfo(&query) != 0 || buffer.front() == L'\0') {
        return std::nullopt;
    }
    return std::wstring{buffer.data()};
}

std::wstring physical_device_key(std::uint64_t packed_luid) {
    D3DKMT_OPENADAPTERFROMLUID opened{};
    opened.AdapterLuid = unpack_luid(packed_luid);
    if (D3DKMTOpenAdapterFromLuid(&opened) != 0) return {};

    D3DKMT_PHYSICAL_ADAPTER_COUNT count{};
    D3DKMT_QUERYADAPTERINFO query{};
    query.hAdapter = opened.hAdapter;
    query.Type = KMTQAITYPE_PHYSICALADAPTERCOUNT;
    query.pPrivateDriverData = &count;
    query.PrivateDriverDataSize = sizeof(count);
    if (D3DKMTQueryAdapterInfo(&query) != 0 || count.Count == 0 ||
        count.Count > 64) {
        count.Count = 1;
    }

    std::wstring identity;
    for (UINT index = 0; index < count.Count; ++index) {
        const auto key = query_physical_pnp_key(opened.hAdapter, index);
        if (!key || key->empty()) {
            identity.clear();
            break;
        }
        if (!identity.empty()) identity.push_back(L'\x1f');
        identity += *key;
    }
    D3DKMT_CLOSEADAPTER close{};
    close.hAdapter = opened.hAdapter;
    static_cast<void>(D3DKMTCloseAdapter(&close));
    return identity;
}

std::wstring normalized_gpu_name(std::wstring_view value) {
    std::wstring result;
    bool pending_space = false;
    for (const wchar_t character : value) {
        if (std::iswspace(character)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) result.push_back(L' ');
        pending_space = false;
        result.push_back(static_cast<wchar_t>(std::towlower(character)));
    }
    return result;
}

std::wstring widen_ascii(std::string_view value) {
    std::wstring result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<wchar_t>(character));
    }
    return result;
}

using NvApiStatus = int;
using NvPhysicalGpuHandle = void*;
constexpr NvApiStatus kNvApiOk = 0;
constexpr unsigned int kNvApiMaxPhysicalGpus = 64;
constexpr unsigned int kNvApiMaxGpuUtilizations = 8;
constexpr unsigned int kNvApiInitializeId = 0x0150e828U;
constexpr unsigned int kNvApiUnloadId = 0xd22bdd7eU;
constexpr unsigned int kNvApiEnumPhysicalGpusId = 0xe5ac921fU;
constexpr unsigned int kNvApiGpuGetFullNameId = 0xceee8e9fU;
constexpr unsigned int kNvApiGpuGetDynamicPstatesInfoExId = 0x60ded2edU;

struct NvApiUtilizationDomain {
    unsigned int present{0};
    unsigned int percentage{0};
};
struct NvApiDynamicPstatesInfo {
    unsigned int version{0};
    unsigned int flags{0};
    NvApiUtilizationDomain utilization[kNvApiMaxGpuUtilizations]{};
};
static_assert(sizeof(NvApiDynamicPstatesInfo) == 72);
constexpr unsigned int kNvApiDynamicPstatesInfoVersion =
    static_cast<unsigned int>(sizeof(NvApiDynamicPstatesInfo)) | (1U << 16U);

using NvApiQueryInterface = void* (__cdecl*)(unsigned int);
using NvApiInitialize = NvApiStatus (__cdecl*)();
using NvApiUnload = NvApiStatus (__cdecl*)();
using NvApiEnumPhysicalGpus = NvApiStatus (__cdecl*)(
    NvPhysicalGpuHandle*, unsigned int*);
using NvApiGpuGetFullName = NvApiStatus (__cdecl*)(NvPhysicalGpuHandle, char*);
using NvApiGpuGetDynamicPstatesInfoEx = NvApiStatus (__cdecl*)(
    NvPhysicalGpuHandle, NvApiDynamicPstatesInfo*);

template <typename Function>
Function nvapi_interface(NvApiQueryInterface query, unsigned int id) {
    return reinterpret_cast<Function>(query ? query(id) : nullptr);
}

using NvmlReturn = int;
struct NvmlDevice_st;
using NvmlDevice = NvmlDevice_st*;
struct NvmlUtilization {
    unsigned int gpu{0};
    unsigned int memory{0};
};
constexpr NvmlReturn kNvmlSuccess = 0;
using NvmlInit = NvmlReturn (*)();
using NvmlShutdown = NvmlReturn (*)();
using NvmlDeviceGetCount = NvmlReturn (*)(unsigned int*);
using NvmlDeviceGetHandleByIndex = NvmlReturn (*)(unsigned int, NvmlDevice*);
using NvmlDeviceGetName = NvmlReturn (*)(NvmlDevice, char*, unsigned int);
using NvmlDeviceGetUtilizationRates = NvmlReturn (*)(NvmlDevice,
                                                       NvmlUtilization*);

template <typename Function>
Function nvml_export(HMODULE library, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(library, name));
}
}  // namespace

Result<GpuMemoryBudget> query_gpu_memory_budget(
    std::uint64_t adapter_luid) {
    if (adapter_luid == 0) {
        return Result<GpuMemoryBudget>::failure(
            {ErrorCode::invalid_argument, L"GPU adapter identity is missing", 0});
    }
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    const HRESULT created = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(created)) {
        return Result<GpuMemoryBudget>::failure(
            {ErrorCode::platform_failure,
             L"DXGI factory could not be created",
             static_cast<std::uint32_t>(created)});
    }
    const LUID expected = unpack_luid(adapter_luid);
    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT found = factory->EnumAdapters1(index, &adapter);
        if (found == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(found)) continue;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            description.AdapterLuid.LowPart != expected.LowPart ||
            description.AdapterLuid.HighPart != expected.HighPart) {
            continue;
        }
        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
        const HRESULT upgraded = adapter.As(&adapter3);
        if (FAILED(upgraded)) {
            return Result<GpuMemoryBudget>::failure(
                {ErrorCode::platform_failure,
                 L"DXGI video-memory budgets are not supported",
                 static_cast<std::uint32_t>(upgraded)});
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        const HRESULT queried = adapter3->QueryVideoMemoryInfo(
            0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
        if (FAILED(queried) || info.Budget == 0) {
            return Result<GpuMemoryBudget>::failure(
                {ErrorCode::platform_failure,
                 L"DXGI video-memory budget is unavailable",
                 static_cast<std::uint32_t>(queried)});
        }
        return Result<GpuMemoryBudget>::success({
            info.CurrentUsage, info.Budget,
            info.AvailableForReservation});
    }
    return Result<GpuMemoryBudget>::failure(
        {ErrorCode::not_found, L"GPU adapter identity is no longer present", 0});
}

struct NvidiaGpuSampler::Impl {
    NvidiaGpuSource source{NvidiaGpuSource::nvml_utilization};
    HMODULE nvapi_library{};
    NvApiUnload nvapi_unload{};
    NvApiGpuGetDynamicPstatesInfoEx nvapi_utilization{};
    NvPhysicalGpuHandle nvapi_device{};
    bool nvapi_initialized{false};
    HMODULE library{};
    NvmlShutdown shutdown{};
    NvmlDeviceGetUtilizationRates utilization{};
    NvmlDevice device{};
    bool initialized{false};

    ~Impl() {
        if (nvapi_initialized && nvapi_unload) {
            static_cast<void>(nvapi_unload());
        }
        if (initialized && shutdown) static_cast<void>(shutdown());
        if (nvapi_library) FreeLibrary(nvapi_library);
        if (library) FreeLibrary(library);
    }
};

Result<std::vector<GpuAdapter>> enumerate_gpu_adapters() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    const HRESULT created = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(created)) return Result<std::vector<GpuAdapter>>::failure(
        {ErrorCode::platform_failure, L"DXGI adapter inventory is unavailable",
         static_cast<std::uint32_t>(created)});
    std::vector<GpuAdapter> adapters;
    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT found = factory->EnumAdapters1(index, &adapter);
        if (found == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(found)) return Result<std::vector<GpuAdapter>>::failure(
            {ErrorCode::platform_failure, L"DXGI adapter enumeration failed",
             static_cast<std::uint32_t>(found)});
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description))) continue;
        LARGE_INTEGER driver_version{};
        const bool has_driver_version = SUCCEEDED(adapter->CheckInterfaceSupport(
            __uuidof(IDXGIDevice), &driver_version));
        const auto luid =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                 description.AdapterLuid.HighPart)) << 32U) |
                description.AdapterLuid.LowPart;
        adapters.push_back({
            luid,
            description.Description,
            static_cast<std::uint64_t>(description.DedicatedVideoMemory),
            description.VendorId,
            description.DeviceId,
            has_driver_version
                ? std::optional<std::uint64_t>{
                      static_cast<std::uint64_t>(driver_version.QuadPart)}
                : std::nullopt,
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0,
            physical_device_key(luid)});
    }
    if (adapters.empty()) return Result<std::vector<GpuAdapter>>::failure(
        {ErrorCode::not_found, L"No DXGI adapter was found", 0});
    return Result<std::vector<GpuAdapter>>::success(std::move(adapters));
}

std::vector<GpuAdapter> unique_physical_gpu_adapters(
    const std::vector<GpuAdapter>& adapters) {
    std::vector<GpuAdapter> result;
    std::set<std::wstring> physical_keys;
    std::set<std::uint64_t> fallback_luids;
    for (const auto& adapter : adapters) {
        if (adapter.software) continue;
        if (!adapter.physical_device_key.empty()) {
            if (!physical_keys.insert(adapter.physical_device_key).second) continue;
        } else if (!fallback_luids.insert(adapter.luid).second) {
            continue;
        }
        result.push_back(adapter);
    }
    return result;
}

NvidiaGpuSampler::NvidiaGpuSampler(std::unique_ptr<Impl> implementation)
    : implementation_{std::move(implementation)} {}
NvidiaGpuSampler::NvidiaGpuSampler(NvidiaGpuSampler&&) noexcept = default;
NvidiaGpuSampler& NvidiaGpuSampler::operator=(NvidiaGpuSampler&&) noexcept = default;
NvidiaGpuSampler::~NvidiaGpuSampler() = default;

Result<NvidiaGpuSampler> NvidiaGpuSampler::create(
    std::wstring_view adapter_name) {
    // MSI Afterburner uses NVIDIA's dynamic P-state utilization domain. Prefer
    // that documented one-second driver metric so both monitors share the same
    // semantic source without requiring Afterburner to be installed or running.
    {
        auto implementation = std::make_unique<Impl>();
        implementation->nvapi_library = LoadLibraryExW(
            L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (implementation->nvapi_library) {
            const auto query = reinterpret_cast<NvApiQueryInterface>(
                GetProcAddress(implementation->nvapi_library,
                               "nvapi_QueryInterface"));
            const auto initialize = nvapi_interface<NvApiInitialize>(
                query, kNvApiInitializeId);
            implementation->nvapi_unload = nvapi_interface<NvApiUnload>(
                query, kNvApiUnloadId);
            const auto enumerate = nvapi_interface<NvApiEnumPhysicalGpus>(
                query, kNvApiEnumPhysicalGpusId);
            const auto device_name = nvapi_interface<NvApiGpuGetFullName>(
                query, kNvApiGpuGetFullNameId);
            implementation->nvapi_utilization =
                nvapi_interface<NvApiGpuGetDynamicPstatesInfoEx>(
                    query, kNvApiGpuGetDynamicPstatesInfoExId);
            if (initialize && implementation->nvapi_unload && enumerate &&
                device_name && implementation->nvapi_utilization &&
                initialize() == kNvApiOk) {
                implementation->nvapi_initialized = true;
                std::array<NvPhysicalGpuHandle, kNvApiMaxPhysicalGpus> handles{};
                unsigned int count = 0;
                if (enumerate(handles.data(), &count) == kNvApiOk && count > 0 &&
                    count <= handles.size()) {
                    const std::wstring expected = normalized_gpu_name(adapter_name);
                    std::vector<NvPhysicalGpuHandle> devices;
                    std::vector<NvPhysicalGpuHandle> name_matches;
                    for (unsigned int index = 0; index < count; ++index) {
                        if (!handles[index]) continue;
                        devices.push_back(handles[index]);
                        std::array<char, 64> name{};
                        if (device_name(handles[index], name.data()) == kNvApiOk &&
                            normalized_gpu_name(widen_ascii(name.data())) ==
                                expected) {
                            name_matches.push_back(handles[index]);
                        }
                    }
                    if (name_matches.size() == 1) {
                        implementation->nvapi_device = name_matches.front();
                    } else if (devices.size() == 1) {
                        implementation->nvapi_device = devices.front();
                    }
                    if (implementation->nvapi_device) {
                        implementation->source =
                            NvidiaGpuSource::nvapi_dynamic_pstates;
                        return Result<NvidiaGpuSampler>::success(
                            NvidiaGpuSampler{std::move(implementation)});
                    }
                }
            }
        }
    }

    // NVML remains a fully local driver fallback for NVIDIA systems where the
    // documented NVAPI dynamic-P-state metric is unavailable.
    auto implementation = std::make_unique<Impl>();
    implementation->library = LoadLibraryExW(
        L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!implementation->library) {
        return Result<NvidiaGpuSampler>::failure(
            {ErrorCode::not_found,
             L"The locally installed NVIDIA monitoring library is unavailable",
             GetLastError()});
    }

    const auto initialize = nvml_export<NvmlInit>(
        implementation->library, "nvmlInit_v2");
    implementation->shutdown = nvml_export<NvmlShutdown>(
        implementation->library, "nvmlShutdown");
    const auto device_count = nvml_export<NvmlDeviceGetCount>(
        implementation->library, "nvmlDeviceGetCount_v2");
    const auto device_by_index = nvml_export<NvmlDeviceGetHandleByIndex>(
        implementation->library, "nvmlDeviceGetHandleByIndex_v2");
    const auto device_name = nvml_export<NvmlDeviceGetName>(
        implementation->library, "nvmlDeviceGetName");
    implementation->utilization = nvml_export<NvmlDeviceGetUtilizationRates>(
        implementation->library, "nvmlDeviceGetUtilizationRates");
    if (!initialize || !implementation->shutdown || !device_count ||
        !device_by_index || !device_name || !implementation->utilization) {
        return Result<NvidiaGpuSampler>::failure(
            {ErrorCode::platform_failure,
             L"The installed NVIDIA monitoring library is missing required exports",
             0});
    }

    NvmlReturn status = initialize();
    if (status != kNvmlSuccess) {
        return Result<NvidiaGpuSampler>::failure(
            {ErrorCode::platform_failure,
             L"The NVIDIA monitoring library could not initialize",
             static_cast<std::uint32_t>(status)});
    }
    implementation->initialized = true;

    unsigned int count = 0;
    status = device_count(&count);
    if (status != kNvmlSuccess || count == 0 || count > 64) {
        return Result<NvidiaGpuSampler>::failure(
            {ErrorCode::not_found,
             L"No usable NVIDIA GPU was exposed by the installed driver",
             static_cast<std::uint32_t>(status)});
    }

    const std::wstring expected = normalized_gpu_name(adapter_name);
    std::vector<NvmlDevice> devices;
    std::vector<NvmlDevice> name_matches;
    for (unsigned int index = 0; index < count; ++index) {
        NvmlDevice device{};
        if (device_by_index(index, &device) != kNvmlSuccess || !device) continue;
        devices.push_back(device);
        std::array<char, 256> name{};
        if (device_name(device, name.data(),
                        static_cast<unsigned int>(name.size())) == kNvmlSuccess &&
            normalized_gpu_name(widen_ascii(name.data())) == expected) {
            name_matches.push_back(device);
        }
    }
    if (name_matches.size() == 1) {
        implementation->device = name_matches.front();
    } else if (devices.size() == 1) {
        implementation->device = devices.front();
    } else {
        return Result<NvidiaGpuSampler>::failure(
            {ErrorCode::platform_failure,
             L"The NVIDIA GPU could not be matched unambiguously to the game adapter",
             0});
    }
    return Result<NvidiaGpuSampler>::success(
        NvidiaGpuSampler{std::move(implementation)});
}

Result<double> NvidiaGpuSampler::sample() const {
    if (!implementation_) {
        return Result<double>::failure(
            {ErrorCode::internal_failure,
             L"NVIDIA GPU telemetry is not initialized", 0});
    }
    if (implementation_->source ==
        NvidiaGpuSource::nvapi_dynamic_pstates) {
        if (!implementation_->nvapi_device ||
            !implementation_->nvapi_utilization) {
            return Result<double>::failure(
                {ErrorCode::internal_failure,
                 L"NVIDIA NVAPI GPU telemetry is not initialized", 0});
        }
        NvApiDynamicPstatesInfo utilization{};
        utilization.version = kNvApiDynamicPstatesInfoVersion;
        const NvApiStatus status = implementation_->nvapi_utilization(
            implementation_->nvapi_device, &utilization);
        const auto& graphics = utilization.utilization[0];
        if (status != kNvApiOk || (graphics.present & 1U) == 0 ||
            graphics.percentage > 100) {
            return Result<double>::failure(
                {ErrorCode::platform_failure,
                 L"The NVIDIA NVAPI driver did not return a valid GPU utilization sample",
                 static_cast<std::uint32_t>(status)});
        }
        return Result<double>::success(
            static_cast<double>(graphics.percentage));
    }
    if (!implementation_->device || !implementation_->utilization) {
        return Result<double>::failure(
            {ErrorCode::internal_failure,
             L"NVIDIA NVML GPU telemetry is not initialized", 0});
    }
    NvmlUtilization utilization{};
    const NvmlReturn status = implementation_->utilization(
        implementation_->device, &utilization);
    if (status != kNvmlSuccess || utilization.gpu > 100 ||
        utilization.memory > 100) {
        return Result<double>::failure(
            {ErrorCode::platform_failure,
             L"The NVIDIA driver did not return a valid GPU utilization sample",
             static_cast<std::uint32_t>(status)});
    }
    return Result<double>::success(static_cast<double>(utilization.gpu));
}

NvidiaGpuSource NvidiaGpuSampler::source() const noexcept {
    return implementation_ ? implementation_->source
                           : NvidiaGpuSource::nvml_utilization;
}

std::wstring format_gpu_driver_version(std::uint64_t version) {
    const auto component = [version](unsigned shift) {
        return static_cast<unsigned>((version >> shift) & 0xFFFFULL);
    };
    return std::to_wstring(component(48)) + L"." +
           std::to_wstring(component(32)) + L"." +
           std::to_wstring(component(16)) + L"." +
           std::to_wstring(component(0));
}
Result<std::uint64_t> adapter_luid_for_window(HWND window) {
    if (!IsWindow(window)) return Result<std::uint64_t>::failure(
        {ErrorCode::invalid_argument, L"GPU window is invalid", 0});
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    if (!monitor) return Result<std::uint64_t>::failure(
        {ErrorCode::not_found, L"Window monitor is unavailable", 0});
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    const HRESULT created = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(created)) return Result<std::uint64_t>::failure(
        {ErrorCode::platform_failure, L"DXGI factory is unavailable",
         static_cast<std::uint32_t>(created)});
    for (UINT adapter_index = 0;; ++adapter_index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapter_index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 adapter_description{};
        if (FAILED(adapter->GetDesc1(&adapter_description))) continue;
        for (UINT output_index = 0;; ++output_index) {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(output_index, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC description{};
            if (SUCCEEDED(output->GetDesc(&description)) && description.Monitor == monitor) {
                return Result<std::uint64_t>::success(
                    (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                         adapter_description.AdapterLuid.HighPart)) << 32U) |
                    adapter_description.AdapterLuid.LowPart);
            }
        }
    }
    return Result<std::uint64_t>::failure(
        {ErrorCode::not_found, L"Window monitor adapter LUID was not found", 0});
}
namespace {
template <typename Value>
Result<std::vector<std::pair<std::wstring, Value>>> read_array(
    PDH_HCOUNTER counter, DWORD format) {
    DWORD bytes = 0, count = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, format,
                                                     &bytes, &count, nullptr);
    if (status == PDH_NO_DATA) {
        return Result<std::vector<std::pair<std::wstring, Value>>>::success({});
    }
    if (status != PDH_MORE_DATA) return Result<std::vector<std::pair<std::wstring, Value>>>::failure(
        {ErrorCode::platform_failure, L"GPU counter array size query failed",
         static_cast<std::uint32_t>(status)});
    std::vector<std::byte> storage(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
    status = PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, items);
    if (status != ERROR_SUCCESS) return Result<std::vector<std::pair<std::wstring, Value>>>::failure(
        {ErrorCode::platform_failure, L"GPU counter array query failed",
         static_cast<std::uint32_t>(status)});
    std::vector<std::pair<std::wstring, Value>> result;
    for (DWORD index = 0; index < count; ++index) {
        if (items[index].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
            items[index].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        if constexpr (std::is_same_v<Value, double>) {
            result.emplace_back(items[index].szName, items[index].FmtValue.doubleValue);
        } else {
            result.emplace_back(items[index].szName,
                static_cast<Value>(std::max<LONGLONG>(0, items[index].FmtValue.largeValue)));
        }
    }
    return Result<std::vector<std::pair<std::wstring, Value>>>::success(std::move(result));
}
}

std::optional<GpuInstanceIdentity> parse_gpu_instance(std::wstring_view instance) {
    static const std::wregex pattern{
        LR"(^pid_([0-9]+)_luid_0x([0-9a-fA-F]+)_0x([0-9a-fA-F]+)(?:_.*)?$)"};
    static const std::wregex engine_pattern{
        LR"(_phys_([0-9]+)_eng_([0-9]+)_engtype_(.+)$)"};
    std::wcmatch match;
    const std::wstring text{instance};
    if (!std::regex_match(text.c_str(), match, pattern)) return std::nullopt;
    try {
        const auto pid = static_cast<std::uint32_t>(std::stoul(match[1].str()));
        const auto high = static_cast<std::uint32_t>(std::stoull(match[2].str(), nullptr, 16));
        const auto low = static_cast<std::uint32_t>(std::stoull(match[3].str(), nullptr, 16));
        std::wstring engine = L"memory";
        std::uint32_t physical_index = 0xFFFFFFFFU;
        std::uint32_t engine_index = 0xFFFFFFFFU;
        const auto marker = text.find(L"engtype_");
        if (marker != std::wstring::npos) engine = text.substr(marker + 8);
        std::wsmatch engine_match;
        if (std::regex_search(text, engine_match, engine_pattern)) {
            physical_index = static_cast<std::uint32_t>(
                std::stoul(engine_match[1].str()));
            engine_index = static_cast<std::uint32_t>(
                std::stoul(engine_match[2].str()));
        }
        return GpuInstanceIdentity{pid,
            (static_cast<std::uint64_t>(high) << 32U) | low,
            std::move(engine), physical_index, engine_index};
    } catch (...) { return std::nullopt; }
}

GpuMetrics aggregate_gpu_counters(const std::vector<GpuCounterValue>& values,
                                  std::uint32_t pid, std::uint64_t adapter_luid) {
    std::map<std::wstring, double> engines;
    GpuMetrics result;
    bool matched = false;
    for (const auto& value : values) {
        if (value.identity.pid != pid ||
            (adapter_luid != 0 && value.identity.adapter_luid != adapter_luid)) continue;
        matched = true;
        if (value.utilization_percent < 0 || value.utilization_percent > 100) {
            result.reason = UnavailableReason::source_failure;
            return result;
        }
        if (value.identity.engine != L"memory") {
            engines[value.identity.engine] = std::max(
                engines[value.identity.engine], value.utilization_percent);
        }
        result.dedicated_bytes = std::max(result.dedicated_bytes, value.dedicated_bytes);
        result.shared_bytes = std::max(result.shared_bytes, value.shared_bytes);
    }
    if (!matched || engines.empty()) return result;
    // Overall GPU load follows the busiest physical engine, matching the
    // Windows Task Manager model. Summing independent 3D/Copy/Compute engines
    // can double-count concurrent work and then falsely clamp at 100%.
    double total = 0;
    for (const auto& [ignored, utilization] : engines) {
        static_cast<void>(ignored); total = std::max(total, utilization);
    }
    result.gpu_percent = std::clamp(total, 0.0, 100.0);
    result.quality = SampleQuality::good;
    result.reason = UnavailableReason::none;
    return result;
}

std::optional<double> aggregate_adapter_gpu_percent(
    const std::vector<GpuCounterValue>& values,
    std::uint64_t adapter_luid) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, double> engines;
    for (const auto& value : values) {
        if ((adapter_luid != 0 &&
             value.identity.adapter_luid != adapter_luid) ||
            value.identity.engine == L"memory" ||
            value.identity.physical_index == 0xFFFFFFFFU ||
            value.identity.engine_index == 0xFFFFFFFFU) {
            continue;
        }
        if (value.utilization_percent < 0.0 ||
            value.utilization_percent > 100.0) {
            return std::nullopt;
        }
        engines[{value.identity.physical_index,
                 value.identity.engine_index}] += value.utilization_percent;
    }
    if (engines.empty()) return std::nullopt;
    double busiest = 0.0;
    for (const auto& [ignored, utilization] : engines) {
        static_cast<void>(ignored);
        busiest = std::max(busiest, std::clamp(utilization, 0.0, 100.0));
    }
    return busiest;
}

std::optional<double> choose_total_gpu_percent(
    std::optional<double> driver_gpu_percent,
    std::optional<double> adapter_gpu_percent) {
    const auto valid = [](const std::optional<double>& value) {
        return value && std::isfinite(*value) && *value >= 0.0 &&
               *value <= 100.0;
    };
    if (valid(driver_gpu_percent)) return driver_gpu_percent;
    if (valid(adapter_gpu_percent)) return adapter_gpu_percent;
    return std::nullopt;
}

PdhGpuSampler::PdhGpuSampler(PdhGpuSampler&& other) noexcept { *this = std::move(other); }
PdhGpuSampler& PdhGpuSampler::operator=(PdhGpuSampler&& other) noexcept {
    if (this != &other) {
        if (query_) PdhCloseQuery(query_);
        query_ = std::exchange(other.query_, nullptr);
        utilization_ = std::exchange(other.utilization_, nullptr);
        dedicated_ = std::exchange(other.dedicated_, nullptr);
        shared_ = std::exchange(other.shared_, nullptr);
        pid_ = other.pid_; adapter_luid_ = other.adapter_luid_; warmed_ = other.warmed_;
    }
    return *this;
}
PdhGpuSampler::~PdhGpuSampler() { if (query_) PdhCloseQuery(query_); }

Result<PdhGpuSampler> PdhGpuSampler::create(std::uint32_t pid,
                                            std::uint64_t adapter_luid) {
    PdhGpuSampler sampler; sampler.pid_ = pid; sampler.adapter_luid_ = adapter_luid;
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &sampler.query_);
    if (status == ERROR_SUCCESS) status = PdhAddEnglishCounterW(
        sampler.query_, L"\\GPU Engine(*)\\Utilization Percentage", 0,
        &sampler.utilization_);
    if (status == ERROR_SUCCESS) status = PdhAddEnglishCounterW(
        sampler.query_, L"\\GPU Process Memory(*)\\Dedicated Usage", 0,
        &sampler.dedicated_);
    if (status == ERROR_SUCCESS) status = PdhAddEnglishCounterW(
        sampler.query_, L"\\GPU Process Memory(*)\\Shared Usage", 0,
        &sampler.shared_);
    if (status != ERROR_SUCCESS) return Result<PdhGpuSampler>::failure(
        {ErrorCode::platform_failure, L"GPU PDH counters are unavailable",
         static_cast<std::uint32_t>(status)});
    return Result<PdhGpuSampler>::success(std::move(sampler));
}

Result<GpuMetrics> PdhGpuSampler::sample() {
    const PDH_STATUS collected = PdhCollectQueryData(query_);
    if (collected != ERROR_SUCCESS) return Result<GpuMetrics>::failure(
        {ErrorCode::platform_failure, L"GPU PDH collection failed",
         static_cast<std::uint32_t>(collected)});
    if (!std::exchange(warmed_, true)) return Result<GpuMetrics>::success({});
    auto utilization = read_array<double>(utilization_, PDH_FMT_DOUBLE);
    auto dedicated = read_array<std::uint64_t>(dedicated_, PDH_FMT_LARGE);
    auto shared = read_array<std::uint64_t>(shared_, PDH_FMT_LARGE);
    if (!utilization.has_value()) return Result<GpuMetrics>::failure(utilization.error());
    if (!dedicated.has_value()) return Result<GpuMetrics>::failure(dedicated.error());
    if (!shared.has_value()) return Result<GpuMetrics>::failure(shared.error());
    std::vector<GpuCounterValue> values;
    for (const auto& [name, amount] : utilization.value()) {
        if (auto identity = parse_gpu_instance(name)) values.push_back({*identity, amount, 0, 0});
    }
    for (const auto& [name, amount] : dedicated.value()) {
        if (auto identity = parse_gpu_instance(name)) values.push_back({*identity, 0, amount, 0});
    }
    for (const auto& [name, amount] : shared.value()) {
        if (auto identity = parse_gpu_instance(name)) values.push_back({*identity, 0, 0, amount});
    }
    auto result = aggregate_gpu_counters(values, pid_, adapter_luid_);
    if (const auto memory = query_gpu_memory_budget(adapter_luid_);
        memory.has_value()) {
        result.adapter_local_usage_bytes =
            memory.value().current_usage_bytes;
        result.adapter_local_budget_bytes = memory.value().budget_bytes;
    }
    result.adapter_gpu_percent = aggregate_adapter_gpu_percent(
        values, adapter_luid_);
    if (result.adapter_gpu_percent &&
        result.reason != UnavailableReason::source_failure) {
        result.quality = SampleQuality::good;
        result.reason = UnavailableReason::none;
    }
    return Result<GpuMetrics>::success(std::move(result));
}
}  // namespace kf2::telemetry
