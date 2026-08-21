#include "kf2/flex/flex_audit.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <system_error>

#include "kf2/security/sha256.hpp"

namespace kf2::flex {
namespace {

constexpr std::array<std::string_view, 10> required_exports{
    "flexInit", "flexShutdown", "flexGetVersion", "flexCreateSolver",
    "flexDestroySolver", "flexUpdateSolver", "flexGetActiveCount",
    "flexGetParticles", "flexSetParticles", "flexWaitFence"};

constexpr std::string_view known_kf2_flex_105_sha256 =
    "bc5bdb62250281455cf753ff9b7fff599e3e4ee2cfc4c406f4e7c9e99f21172f";

std::wstring file_version(const std::filesystem::path& path) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0 || size > 1024U * 1024U) return L"unavailable";
    std::vector<unsigned char> block(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data()))
        return L"unavailable";
    VS_FIXEDFILEINFO* info = nullptr;
    UINT info_size = 0;
    if (!VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&info),
                        &info_size) || !info || info_size < sizeof(*info) ||
        info->dwSignature != VS_FFI_SIGNATURE)
        return L"unavailable";
    return std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." +
           std::to_wstring(LOWORD(info->dwFileVersionMS)) + L"." +
           std::to_wstring(HIWORD(info->dwFileVersionLS)) + L"." +
           std::to_wstring(LOWORD(info->dwFileVersionLS));
}

std::string escape_json(std::string_view value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        if (c == '\\' || c == '"') out << '\\' << static_cast<char>(c);
        else if (c < 0x20) out << "\\u" << std::hex << std::setw(4)
                               << std::setfill('0') << static_cast<unsigned>(c)
                               << std::dec;
        else out << static_cast<char>(c);
    }
    return out.str();
}

template <class T>
const T* at(const std::vector<unsigned char>& bytes, std::size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return nullptr;
    return reinterpret_cast<const T*>(bytes.data() + offset);
}

std::optional<std::size_t> rva_to_offset(
    DWORD rva, const IMAGE_SECTION_HEADER* sections, WORD section_count,
    std::size_t file_size) {
    for (WORD i = 0; i < section_count; ++i) {
        const auto& section = sections[i];
        const DWORD span = std::max(section.Misc.VirtualSize,
                                    section.SizeOfRawData);
        if (rva >= section.VirtualAddress &&
            rva - section.VirtualAddress < span) {
            const std::uint64_t offset = section.PointerToRawData +
                static_cast<std::uint64_t>(rva - section.VirtualAddress);
            if (offset < file_size) return static_cast<std::size_t>(offset);
        }
    }
    return std::nullopt;
}

Result<std::vector<std::string>> parse_exports(
    const std::vector<unsigned char>& bytes) {
    const auto* dos = at<IMAGE_DOS_HEADER>(bytes, 0);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) {
        return Result<std::vector<std::string>>::failure(
            {ErrorCode::invalid_argument, L"FleX runtime has no valid DOS header", 0});
    }
    const auto pe_offset = static_cast<std::size_t>(dos->e_lfanew);
    const auto* signature = at<DWORD>(bytes, pe_offset);
    const auto* file = at<IMAGE_FILE_HEADER>(bytes, pe_offset + sizeof(DWORD));
    const auto* optional = at<IMAGE_OPTIONAL_HEADER64>(
        bytes, pe_offset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER));
    if (!signature || *signature != IMAGE_NT_SIGNATURE || !file || !optional ||
        file->Machine != IMAGE_FILE_MACHINE_AMD64 ||
        optional->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        optional->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
        return Result<std::vector<std::string>>::failure(
            {ErrorCode::invalid_argument, L"FleX runtime is not a valid x64 PE image", 0});
    }
    const auto sections_offset = pe_offset + sizeof(DWORD) +
        sizeof(IMAGE_FILE_HEADER) + file->SizeOfOptionalHeader;
    if (file->NumberOfSections == 0 || sections_offset > bytes.size() ||
        sizeof(IMAGE_SECTION_HEADER) * file->NumberOfSections >
            bytes.size() - sections_offset) {
        return Result<std::vector<std::string>>::failure(
            {ErrorCode::invalid_argument, L"FleX runtime section table is invalid", 0});
    }
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        bytes.data() + sections_offset);
    const DWORD export_rva = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]
                                 .VirtualAddress;
    const auto export_offset = rva_to_offset(
        export_rva, sections, file->NumberOfSections, bytes.size());
    if (!export_offset) {
        return Result<std::vector<std::string>>::failure(
            {ErrorCode::invalid_argument, L"FleX runtime export directory is missing", 0});
    }
    const auto* directory = at<IMAGE_EXPORT_DIRECTORY>(bytes, *export_offset);
    if (!directory || directory->NumberOfNames > 4096) {
        return Result<std::vector<std::string>>::failure(
            {ErrorCode::invalid_argument, L"FleX runtime export directory is invalid", 0});
    }
    const auto names_offset = rva_to_offset(
        directory->AddressOfNames, sections, file->NumberOfSections, bytes.size());
    if (!names_offset || static_cast<std::uint64_t>(directory->NumberOfNames) *
            sizeof(DWORD) > bytes.size() - *names_offset) {
        return Result<std::vector<std::string>>::failure(
            {ErrorCode::invalid_argument, L"FleX runtime export name table is invalid", 0});
    }
    const auto* names = reinterpret_cast<const DWORD*>(bytes.data() + *names_offset);
    std::vector<std::string> result;
    result.reserve(directory->NumberOfNames);
    for (DWORD i = 0; i < directory->NumberOfNames; ++i) {
        const auto name_offset = rva_to_offset(
            names[i], sections, file->NumberOfSections, bytes.size());
        if (!name_offset) continue;
        const auto end = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(*name_offset),
                                   bytes.end(), 0);
        if (end == bytes.end() || end - (bytes.begin() +
            static_cast<std::ptrdiff_t>(*name_offset)) > 256) continue;
        result.emplace_back(reinterpret_cast<const char*>(bytes.data() + *name_offset),
                            static_cast<std::size_t>(end - (bytes.begin() +
                                static_cast<std::ptrdiff_t>(*name_offset))));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return Result<std::vector<std::string>>::success(std::move(result));
}

}  // namespace

Result<RuntimeAudit> audit_runtime(const std::filesystem::path& path,
                                   bool allow_transaction_original_name) {
    std::error_code error;
    const auto filename = path.filename();
    const bool accepted_name = filename == L"flexRelease_x64.dll" ||
        (allow_transaction_original_name &&
         filename == L"flexRelease_original.dll");
    if (!std::filesystem::is_regular_file(path, error) || error ||
        !accepted_name) {
        return Result<RuntimeAudit>::failure(
            {ErrorCode::invalid_argument, L"Expected flexRelease_x64.dll was not found", 0});
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > 16ULL * 1024ULL * 1024ULL) {
        return Result<RuntimeAudit>::failure(
            {ErrorCode::invalid_argument, L"FleX runtime size is invalid", 0});
    }
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>{input}, {}};
    if (input.bad() || bytes.size() != size) {
        return Result<RuntimeAudit>::failure(
            {ErrorCode::io_failure, L"FleX runtime could not be read completely", 0});
    }
    const auto digest = security::sha256_hex(std::string_view{
        reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    if (!digest.has_value()) return Result<RuntimeAudit>::failure(digest.error());
    auto exports = parse_exports(bytes);
    if (!exports.has_value()) return Result<RuntimeAudit>::failure(exports.error());
    RuntimeAudit audit{.path = path, .size_bytes = size,
                       .sha256 = digest.value(), .file_version = file_version(path),
                       .exports = std::move(exports.value())};
    for (const auto required : required_exports) {
        if (!std::binary_search(audit.exports.begin(), audit.exports.end(), required))
            audit.missing_required_exports.emplace_back(required);
    }
    audit.abi_compatible = audit.missing_required_exports.empty();
    audit.exact_known_runtime = audit.abi_compatible &&
        audit.exports.size() == 52 && audit.file_version == L"1.0.5.0" &&
        audit.sha256 == known_kf2_flex_105_sha256;
    return Result<RuntimeAudit>::success(std::move(audit));
}

std::string serialize_audit_json(const RuntimeAudit& audit) {
    std::ostringstream out;
    const auto path_utf8_raw = audit.path.generic_u8string();
    const std::string path_utf8{reinterpret_cast<const char*>(path_utf8_raw.data()),
                                path_utf8_raw.size()};
    std::string version;
    version.reserve(audit.file_version.size());
    for (const wchar_t value : audit.file_version) {
        if (value < L'0' || value > L'9') {
            version.push_back(value == L'.' ? '.' : '?');
        } else {
            version.push_back(static_cast<char>('0' + (value - L'0')));
        }
    }
    out << "{\n  \"schema_version\": 1,\n  \"mode\": \"offline_lab_only\",\n"
        << "  \"path\": \"" << escape_json(path_utf8) << "\",\n"
        << "  \"size_bytes\": " << audit.size_bytes << ",\n"
        << "  \"sha256\": \"" << audit.sha256 << "\",\n"
        << "  \"file_version\": \"" << escape_json(version) << "\",\n"
        << "  \"abi_compatible\": " << (audit.abi_compatible ? "true" : "false")
        << ",\n  \"exact_known_runtime\": "
        << (audit.exact_known_runtime ? "true" : "false")
        << ",\n  \"exports\": [";
    for (std::size_t i = 0; i < audit.exports.size(); ++i)
        out << (i ? ", " : "") << '"' << escape_json(audit.exports[i]) << '"';
    out << "],\n  \"missing_required_exports\": [";
    for (std::size_t i = 0; i < audit.missing_required_exports.size(); ++i)
        out << (i ? ", " : "") << '"'
            << escape_json(audit.missing_required_exports[i]) << '"';
    out << "]\n}\n";
    return out.str();
}

HookGateDecision evaluate_hook_gate(const HookGateEvidence& evidence) {
    if (!evidence.abi_compatible || !evidence.exact_runtime_identity) {
        return {HookGateState::blocked_abi, false,
                L"FleX ABI or exact runtime identity is not verified."};
    }
    if (evidence.game_running) {
        return {HookGateState::blocked_game_running, false,
                L"The FleX runtime is never replaced while KF2 is running."};
    }
    if (!evidence.explicit_offline_opt_in ||
        !evidence.platform_offline_confirmed) {
        return {HookGateState::blocked_online_uncertain, false,
                L"Explicit opt-in and a confirmed offline platform state are required."};
    }
    if (!evidence.replacement_stability_qualified) {
        return {HookGateState::blocked_stability_quarantine, false,
                L"The replacement proxy is quarantined after documented access-violation crashes."};
    }
    return {HookGateState::eligible_offline_lab, true,
            L"All offline laboratory gates are satisfied."};
}

}  // namespace kf2::flex
