#include "application_runtime.hpp"

#include <exception>
#include <mutex>
#include <optional>
#include <thread>

#include "kf2/app/build_identity.hpp"
#include "kf2/security/release_package_repair.hpp"

namespace kf2::app {

struct PackageRepairAsyncState {
    std::mutex mutex;
    std::optional<Result<security::PackageRepairResult>> outcome;
};

void UiRuntime::start_auto_package_repair() {
    if (package_repair_state) {
        std::scoped_lock lock{package_repair_state->mutex};
        if (!package_repair_state->outcome.has_value()) {
            model.set_notice({ui::NoticeSeverity::info,
                              L"PACKAGE_AUTO_REPAIR_RUNNING",
                              L"Auto Repair is already downloading and checking the exact installed release.",
                              L""});
            invalidate();
            return;
        }
    }

    const auto identity = current_build_identity();
    const auto plan = security::exact_release_repair_plan(identity.version);
    if (!plan.has_value()) {
        model.set_notice({ui::NoticeSeverity::error,
                          L"PACKAGE_AUTO_REPAIR_REJECTED",
                          plan.error().message, L""});
        invalidate();
        return;
    }
    auto state = std::make_shared<PackageRepairAsyncState>();
    package_repair_state = state;
    events->append(
        {0, diagnostics::Severity::info, "PACKAGE_AUTO_REPAIR_STARTED",
         L"Downloading only the exact installed release " + plan.value().tag +
             L" from the official GitHub repository",
         L"package"});
    model.set_notice({
        ui::NoticeSeverity::info, L"PACKAGE_AUTO_REPAIR_STARTED",
        L"Downloading and verifying " + plan.value().asset_name + L".",
        L"Only the exact installed version is accepted."});
    invalidate();

    const auto root = executable_root;
    const auto working = settings_path.parent_path() / L"package-repair";
    std::thread{
        [state, root, working, version = identity.version,
         source_identity = identity.commit]() {
            Result<security::PackageRepairResult> result =
                Result<security::PackageRepairResult>::failure(
                    {ErrorCode::internal_failure,
                     L"Auto Repair ended unexpectedly", 0});
            try {
                result = security::download_and_repair_release_package(
                    root, working, version, source_identity);
            } catch (const std::exception&) {
                result = Result<security::PackageRepairResult>::failure(
                    {ErrorCode::internal_failure,
                     L"Auto Repair encountered an unexpected local error", 0});
            } catch (...) {
                result = Result<security::PackageRepairResult>::failure(
                    {ErrorCode::internal_failure,
                     L"Auto Repair encountered an unknown local error", 0});
            }
            std::scoped_lock lock{state->mutex};
            state->outcome.emplace(std::move(result));
        }}
        .detach();
}

void UiRuntime::poll_auto_package_repair() {
    if (!package_repair_state) return;
    std::optional<Result<security::PackageRepairResult>> outcome;
    {
        std::scoped_lock lock{package_repair_state->mutex};
        if (!package_repair_state->outcome.has_value()) return;
        outcome.emplace(std::move(*package_repair_state->outcome));
    }
    package_repair_state.reset();
    if (!outcome->has_value()) {
        events->append(
            {0, diagnostics::Severity::error, "PACKAGE_AUTO_REPAIR_FAILED",
             outcome->error().message, L"package"});
        model.set_notice({ui::NoticeSeverity::error,
                          L"PACKAGE_AUTO_REPAIR_FAILED",
                          outcome->error().message,
                          L"No installed file was trusted without final verification."});
        invalidate();
        return;
    }
    const auto& repaired = outcome->value();
    if (repaired.repaired_files == 0) {
        events->append(
            {0, diagnostics::Severity::info,
             "PACKAGE_AUTO_REPAIR_NOT_NEEDED",
             L"The exact GitHub release was verified; all installed package files were already valid",
             L"package"});
        model.set_notice({ui::NoticeSeverity::info,
                          L"PACKAGE_AUTO_REPAIR_NOT_NEEDED",
                          L"All required files already match the exact GitHub release.",
                          L""});
    } else {
        events->append(
            {0, diagnostics::Severity::info,
             "PACKAGE_AUTO_REPAIR_APPLIED",
             std::to_wstring(repaired.repaired_files) +
                 L" missing or damaged files were restored from the exact verified GitHub release",
             L"package"});
        model.set_notice({
            ui::NoticeSeverity::info, L"PACKAGE_AUTO_REPAIR_APPLIED",
            std::to_wstring(repaired.repaired_files) +
                L" required files were restored and verified.",
            L"Restart KF2 Optimizer to load the repaired components."});
    }
    invalidate();
}

}  // namespace kf2::app
