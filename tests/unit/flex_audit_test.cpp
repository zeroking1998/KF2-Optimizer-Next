#include "kf2/flex/flex_audit.hpp"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

int main() {
    const std::filesystem::path active{KF2_FLEX_RUNTIME};
    const auto preserved = active.parent_path() / L"flexRelease_original.dll";
    const auto source = std::filesystem::exists(preserved) ? preserved : active;
    if (!std::filesystem::exists(source)) {
        std::cout << "SKIP: installed KF2 FleX runtime not available\n";
        return 77;
    }
    const auto sandbox = std::filesystem::temp_directory_path() /
        (L"kf2-flex-audit-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(sandbox, ec);
    std::filesystem::create_directories(sandbox, ec);
    if (ec) return 78;
    const auto runtime = sandbox / L"flexRelease_x64.dll";
    std::filesystem::copy_file(source, runtime,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return 78;
    const auto audit = kf2::flex::audit_runtime(runtime);
    if (!audit.has_value() || !audit.value().abi_compatible ||
        !audit.value().exact_known_runtime ||
        audit.value().file_version != L"1.0.5.0" ||
        audit.value().exports.size() != 52 ||
        audit.value().sha256 !=
            "bc5bdb62250281455cf753ff9b7fff599e3e4ee2cfc4c406f4e7c9e99f21172f" ||
        !std::binary_search(audit.value().exports.begin(), audit.value().exports.end(),
                            "flexUpdateSolver")) {
        std::cerr << "installed FleX runtime ABI audit failed\n";
        if (!audit.has_value()) {
            std::wcerr << audit.error().message << L'\n';
        } else {
            std::cerr << "exports=" << audit.value().exports.size()
                      << " hash=" << audit.value().sha256 << " missing=";
            for (const auto& value : audit.value().missing_required_exports)
                std::cerr << value << ',';
            std::cerr << '\n';
        }
        std::filesystem::remove_all(sandbox, ec);
        return 1;
    }
    const auto json = kf2::flex::serialize_audit_json(audit.value());
    if (json.find("offline_lab_only") == std::string::npos ||
        json.find("abi_compatible") == std::string::npos)
        return 2;
    kf2::flex::HookGateEvidence gate{
        .abi_compatible = true,
        .exact_runtime_identity = true,
        .game_running = false,
        .explicit_offline_opt_in = true,
        .platform_offline_confirmed = true,
        .replacement_stability_qualified = false};
    const auto quarantined = kf2::flex::evaluate_hook_gate(gate);
    if (quarantined.writable || quarantined.state !=
            kf2::flex::HookGateState::blocked_stability_quarantine)
        return 3;
    gate.replacement_stability_qualified = true;
    const auto eligible = kf2::flex::evaluate_hook_gate(gate);
    if (!eligible.writable || eligible.state !=
            kf2::flex::HookGateState::eligible_offline_lab)
        return 4;
    gate.game_running = true;
    if (kf2::flex::evaluate_hook_gate(gate).writable) return 5;
    std::filesystem::remove_all(sandbox, ec);
    return 0;
}
