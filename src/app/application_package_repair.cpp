#include "application_runtime.hpp"

#include <exception>
#include <mutex>
#include <optional>
#include <thread>

#include "kf2/app/build_identity.hpp"
#include "kf2/security/release_package_repair.hpp"

namespace kf2::app {

#if defined(KF2_APPLICATION_SHUTDOWN_TESTING)
namespace {
AutoPackageRepairOperation repair_operation_for_testing{};
}

void set_auto_package_repair_operation_for_testing(
    AutoPackageRepairOperation operation) noexcept {
    repair_operation_for_testing = operation;
}
#endif

struct PackageRepairAsyncState {
    std::mutex mutex;
    std::optional<Result<security::PackageRepairResult>> outcome;
};

void UiRuntime::start_auto_package_repair() {
    if (package_repair_state) {
        poll_auto_package_repair();
        if (package_repair_state) {
            if (package_repair_close_requested) return;
            model.set_notice({ui::NoticeSeverity::info,
                              L"PACKAGE_AUTO_REPAIR_RUNNING",
                              L"Auto Repair is already downloading and checking the exact installed release.",
                              L""});
            invalidate();
            return;
        }
    }
    if (package_actions_busy()) {
        model.set_notice({ui::NoticeSeverity::info, L"PACKAGE_ACTIONS_BUSY",
            L"Wait for the current update check, update installation or repair to finish.", L""});
        refresh_update_presentation();
        return;
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
    package_repair_state.reset();
    auto state = std::make_shared<PackageRepairAsyncState>();
    const auto root = executable_root;
    const auto working = settings_path.parent_path() / L"package-repair";
#if defined(KF2_APPLICATION_SHUTDOWN_TESTING)
    const auto repair_for_testing = repair_operation_for_testing;
#endif
    std::function<void()> worker =
        [state, root, working, version = identity.version,
         source_identity = identity.commit
#if defined(KF2_APPLICATION_SHUTDOWN_TESTING)
         , repair_for_testing
#endif
        ]() {
            Result<security::PackageRepairResult> result =
                Result<security::PackageRepairResult>::failure(
                    {ErrorCode::internal_failure,
                     L"Auto Repair ended unexpectedly", 0});
            try {
#if defined(KF2_APPLICATION_SHUTDOWN_TESTING)
                if (repair_for_testing) {
                    result = repair_for_testing();
                } else
#endif
                {
                    result = security::download_and_repair_release_package(
                        root, working, version, source_identity);
                }
            } catch (const std::exception&) {
                result = Result<security::PackageRepairResult>::failure(
                    {ErrorCode::recovery_required,
                     L"Auto Repair encountered an unexpected local error", 0});
            } catch (...) {
                result = Result<security::PackageRepairResult>::failure(
                    {ErrorCode::recovery_required,
                     L"Auto Repair encountered an unknown local error", 0});
            }
            std::scoped_lock lock{state->mutex};
            state->outcome.emplace(std::move(result));
        };
    try {
        package_repair_worker = package_repair_worker_launcher(std::move(worker));
    } catch (...) {
        events->append(
            {0, diagnostics::Severity::error, "PACKAGE_AUTO_REPAIR_FAILED",
             L"Auto Repair could not start its background worker", L"package"});
        model.set_notice({
            ui::NoticeSeverity::error, L"PACKAGE_AUTO_REPAIR_FAILED",
            L"Auto Repair could not start its background worker.",
            L"Try Auto Repair again. No installed file was changed."});
        invalidate();
        return;
    }

    package_repair_state = std::move(state);
    refresh_update_presentation();
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
}

void UiRuntime::poll_auto_package_repair() {
    if (!package_repair_state) return;
    std::optional<Result<security::PackageRepairResult>> outcome;
    {
        std::scoped_lock lock{package_repair_state->mutex};
        if (!package_repair_state->outcome.has_value()) return;
        outcome.emplace(std::move(*package_repair_state->outcome));
    }
    if (package_repair_worker.joinable()) package_repair_worker.join();
    package_repair_state.reset();
    if (std::exchange(package_repair_close_requested, false) && window) {
        PostMessageW(static_cast<HWND>(window->native_handle_for_testing()),
                     WM_CLOSE, 0, 0);
    }
    if (!outcome->has_value()) {
        package_repair_recovery_required = package_repair_recovery_required ||
            outcome->error().code == ErrorCode::recovery_required;
        refresh_update_presentation();
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
    package_repair_recovery_required = false;
    refresh_update_presentation();
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

bool UiRuntime::can_close_after_package_repair() {
    if (!package_repair_state) return true;
    if (!std::exchange(package_repair_close_requested, true)) {
        model.set_notice({
            ui::NoticeSeverity::info, L"PACKAGE_AUTO_REPAIR_CLOSE_WAIT",
            L"Waiting for Auto Repair before closing.",
            L"The app will close after repair and verification finish. "
            L"Do not force it to stop."});
        invalidate();
    }
    return false;
}

}  // namespace kf2::app
