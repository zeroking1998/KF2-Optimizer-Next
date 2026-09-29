#include <Windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <UIAutomationClient.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include "kf2/app/application.hpp"
#include "kf2/config/setting_catalog.hpp"
#include "kf2/optimizer/startup_gpu_profile.hpp"
#include "kf2/ui/shell_layout.hpp"
#include "app/application_runtime.hpp"
#include "app/runtime/feature_composition.hpp"
#include "features/telemetry/telemetry_session_stage.hpp"
#include "features/telemetry/telemetry_frame.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

template <typename Configure>
void replace_runtime_gameplay(
    kf2::app::UiRuntime& runtime, Configure&& configure) {
    auto session = runtime.game_log_session
        ? *runtime.game_log_session : kf2::game::GameLogSession{};
    configure(session);
    runtime.game_log_session = kf2::game::make_game_log_session_snapshot(
        std::move(session));
}

template <typename Configure>
void replace_frame_gameplay(
    kf2::telemetry_pipeline::TelemetryFrame& frame,
    Configure&& configure) {
    auto session = frame.gameplay
        ? *frame.gameplay : kf2::game::GameLogSession{};
    configure(session);
    frame.gameplay = kf2::game::make_game_log_session_snapshot(
        std::move(session));
}

void throw_update_controller_allocation_failure() {
    kf2::update::detail::set_update_controller_allocation_hook_for_testing(
        nullptr);
    throw std::bad_alloc{};
}

kf2::Result<bool> fail_gpu_profile_settings_write(
    const std::filesystem::path&, std::string_view) {
    return kf2::Result<bool>::failure({
        kf2::ErrorCode::io_failure,
        L"Injected confirmed GPU profile persistence failure", 0});
}

#if defined(KF2_APPLICATION_SHUTDOWN_TESTING)
std::optional<kf2::app::UiRuntimeShutdownPhase>
    shutdown_phase_to_throw;
std::array<unsigned int, 3> shutdown_phase_calls{};

void throw_selected_shutdown_phase(
    kf2::app::UiRuntimeShutdownPhase phase) {
    ++shutdown_phase_calls[static_cast<std::size_t>(phase)];
    if (shutdown_phase_to_throw == phase) {
        throw std::runtime_error{"injected runtime shutdown failure"};
    }
}
#endif

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << bytes;
}

bool wait_for_file(const std::filesystem::path& path,
                   std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error) && !error) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

std::string utf8(std::wstring_view value) {
    const int bytes = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), bytes, nullptr,
            nullptr) != bytes) {
        return {};
    }
    return result;
}

bool write_complete_config_catalog(const std::filesystem::path& root) {
    using namespace kf2::config;
    std::map<std::filesystem::path, std::string> files;
    for (const auto& definition : all_settings()) {
        auto& bytes = files[definition.relative_path];
        bytes += "[";
        for (const wchar_t character : definition.section) {
            bytes.push_back(static_cast<char>(character));
        }
        bytes += "]\r\n";
        for (const wchar_t character : definition.key) {
            bytes.push_back(static_cast<char>(character));
        }
        bytes += "=";
        SettingValue value = definition.type == SettingType::boolean
            ? SettingValue{true}
            : definition.type == SettingType::integer
                ? SettingValue{static_cast<int>(definition.minimum)}
                : SettingValue{definition.minimum};
        if (definition.id == SettingId::target_fps) value = 62;
        if (definition.id == SettingId::minimum_smooth_frame_rate) value = 22;
        if (definition.id == SettingId::corpse_limit) value = 12;
        const auto text = serialize_setting_value(definition, value);
        if (!text.has_value()) return false;
        const std::wstring serialized =
            definition.id == SettingId::target_fps
                ? L"62.000000"
                : definition.id == SettingId::minimum_smooth_frame_rate
                    ? L"22.000000"
                    : *text;
        for (const wchar_t character : serialized) {
            bytes.push_back(static_cast<char>(character));
        }
        bytes += "\r\n";
    }
    for (const auto& [path, bytes] : files) {
        write_bytes(root / path, bytes);
    }
    return true;
}

void write_test_pe(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::vector<unsigned char> bytes(512, 0);
    bytes[0] = 'M'; bytes[1] = 'Z';
    const std::uint32_t pe_offset = 128;
    const WORD machine = IMAGE_FILE_MACHINE_AMD64;
    std::memcpy(bytes.data() + 0x3C, &pe_offset, sizeof(pe_offset));
    bytes[128] = 'P'; bytes[129] = 'E';
    std::memcpy(bytes.data() + 132, &machine, sizeof(machine));
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::optional<POINT> node_center(HWND window, const kf2::ui::UiModel& model,
                                 std::string_view node_id) {
    RECT client{};
    if (!GetClientRect(window, &client)) return std::nullopt;
    const float dpi = static_cast<float>(GetDpiForWindow(window));
    const auto layout = kf2::ui::layout_shell(
        model,
        kf2::ui::pixels_to_dips(static_cast<float>(client.right), dpi),
        kf2::ui::pixels_to_dips(static_cast<float>(client.bottom), dpi));
    const auto iterator = std::find_if(
        layout.nodes.begin(), layout.nodes.end(), [&](const auto& node) {
            return node.id == node_id;
        });
    if (iterator == layout.nodes.end()) return std::nullopt;
    return POINT{
        static_cast<LONG>(std::lround(kf2::ui::dips_to_pixels(
            iterator->bounds.x + iterator->bounds.width * 0.5F, dpi))),
        static_cast<LONG>(std::lround(kf2::ui::dips_to_pixels(
            iterator->bounds.y + iterator->bounds.height * 0.5F, dpi)))};
}

std::optional<POINT> scroll_to_node(HWND window,
                                    const kf2::ui::UiModel& model,
                                    std::string_view node_id) {
    for (int page = 0; page < 16; ++page) {
        SendMessageW(window, WM_KEYDOWN, VK_PRIOR, 0);
    }
    for (int page = 0; page < 16; ++page) {
        const auto center = node_center(window, model, node_id);
        RECT client{};
        if (center && GetClientRect(window, &client)) {
            const float dpi = static_cast<float>(GetDpiForWindow(window));
            const auto layout = kf2::ui::layout_shell(
                model,
                kf2::ui::pixels_to_dips(
                    static_cast<float>(client.right), dpi),
                kf2::ui::pixels_to_dips(
                    static_cast<float>(client.bottom), dpi));
            const LONG content_left = static_cast<LONG>(std::lround(
                kf2::ui::dips_to_pixels(layout.content.x, dpi)));
            const LONG content_top = static_cast<LONG>(std::lround(
                kf2::ui::dips_to_pixels(layout.content.y, dpi)));
            const LONG content_right = static_cast<LONG>(std::lround(
                kf2::ui::dips_to_pixels(
                    layout.content.x + layout.content.width, dpi)));
            const LONG content_bottom = static_cast<LONG>(std::lround(
                kf2::ui::dips_to_pixels(
                    layout.content.y + layout.content.height, dpi)));
            if (center->x >= content_left && center->y >= content_top &&
                center->x < content_right && center->y < content_bottom) {
                return center;
            }
        }
        SendMessageW(window, WM_KEYDOWN, VK_NEXT, 0);
    }
    return std::nullopt;
}

int test_package_repair_worker_start_failure() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    const auto test_root = root / L"repair-worker-start";
    fs::remove_all(test_root);

    kf2::diagnostics::EventLog repair_events{128};
    kf2::app::UiRuntime runtime{test_root / L"Data", false,
        kf2::config::Settings{}, repair_events, std::nullopt,
        kf2::app::StartMode::read_only, test_root / L"portable"};
    int launch_attempts = 0;
    runtime.package_repair_worker_launcher =
        [&](std::function<void()>) {
            ++launch_attempts;
            throw std::system_error{
                std::make_error_code(std::errc::resource_unavailable_try_again)};
        };

    runtime.start_auto_package_repair();
    CHECK(launch_attempts == 1);
    CHECK(!runtime.package_repair_state);
    CHECK(runtime.model.notice().has_value());
    CHECK(runtime.model.notice()->code == L"PACKAGE_AUTO_REPAIR_FAILED");
    const auto events = repair_events.snapshot();
    CHECK(std::none_of(events.begin(), events.end(), [](const auto& event) {
        return event.code == "PACKAGE_AUTO_REPAIR_STARTED";
    }));

    runtime.start_auto_package_repair();
    CHECK(launch_attempts == 2);
    CHECK(!runtime.package_repair_state);
    return EXIT_SUCCESS;
}

int test_runtime_shutdown_exception_boundaries() {
#if !defined(KF2_APPLICATION_SHUTDOWN_TESTING)
    return EXIT_FAILURE;
#else
    namespace fs = std::filesystem;
    using kf2::app::UiRuntimeShutdownPhase;
    static_assert(std::is_nothrow_move_assignable_v<kf2::app::Application>);
    const fs::path root = fs::path{KF2_TEST_ROOT} /
        L"runtime-shutdown-exceptions";
    fs::remove_all(root);

    const std::array phases{
        UiRuntimeShutdownPhase::live_adaptive_restore,
        UiRuntimeShutdownPhase::protected_config_restore,
        UiRuntimeShutdownPhase::event_publication,
    };
    std::uint64_t identity = 8000;
    for (std::size_t index = 0; index < phases.size(); ++index) {
        const auto state_root = root / std::to_wstring(index) / L"Data";
        fs::create_directories(root / std::to_wstring(index) / L"portable");
        const std::wstring instance_name =
            L"Local\\KF2OptimizerNext-ShutdownTest-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(index);
        kf2::app::StartOptions options{
            .state_root = state_root,
            .executable_root = root / std::to_wstring(index) / L"portable",
            .instance_name = instance_name,
            .identity = {GetCurrentProcessId(), identity++},
            .create_window = false,
            .mode = kf2::app::StartMode::read_only,
        };
        {
            auto application = kf2::app::Application::start(options);
            CHECK(application.has_value());
            CHECK(read_bytes(state_root / L"session.marker").ends_with(
                "clean_shutdown=false\n"));
            shutdown_phase_to_throw = phases[index];
            shutdown_phase_calls.fill(0);
            kf2::app::set_ui_runtime_shutdown_probe_for_testing(
                throw_selected_shutdown_phase);
            const auto stopped = application.value().shutdown_cleanly();
            CHECK(!stopped.has_value());
            CHECK(stopped.error().code == kf2::ErrorCode::internal_failure);
            CHECK(std::all_of(shutdown_phase_calls.begin(),
                              shutdown_phase_calls.end(),
                              [](unsigned int calls) { return calls == 1; }));
            CHECK(read_bytes(state_root / L"session.marker").ends_with(
                "clean_shutdown=false\n"));
        }
        // Destruction retries the independent restore surfaces but must never
        // propagate the still-armed injected exception.
        CHECK(read_bytes(state_root / L"session.marker").ends_with(
            "clean_shutdown=false\n"));
        shutdown_phase_to_throw.reset();
        kf2::app::set_ui_runtime_shutdown_probe_for_testing(nullptr);

        options.identity.process_start_id = identity++;
        auto recovered = kf2::app::Application::start(options);
        CHECK(recovered.has_value());
        CHECK(recovered.value().shutdown_cleanly().has_value());
        CHECK(read_bytes(state_root / L"session.marker").ends_with(
            "clean_shutdown=true\n"));
    }

    {
        const auto move_root = root / L"move-assignment";
        const auto destination_root = move_root / L"destination";
        const auto source_root = move_root / L"source";
        fs::create_directories(move_root / L"portable");
        kf2::app::StartOptions destination_options{
            .state_root = destination_root,
            .executable_root = move_root / L"portable",
            .instance_name =
                L"Local\\KF2OptimizerNext-ShutdownMoveDestination-" +
                std::to_wstring(GetCurrentProcessId()),
            .identity = {GetCurrentProcessId(), identity++},
            .create_window = false,
            .mode = kf2::app::StartMode::read_only,
        };
        auto source_options = destination_options;
        source_options.state_root = source_root;
        source_options.instance_name =
            L"Local\\KF2OptimizerNext-ShutdownMoveSource-" +
            std::to_wstring(GetCurrentProcessId());
        source_options.identity.process_start_id = identity++;
        auto destination = kf2::app::Application::start(
            destination_options);
        auto source = kf2::app::Application::start(source_options);
        CHECK(destination.has_value());
        CHECK(source.has_value());
        shutdown_phase_to_throw =
            UiRuntimeShutdownPhase::protected_config_restore;
        kf2::app::set_ui_runtime_shutdown_probe_for_testing(
            throw_selected_shutdown_phase);
        destination.value() = std::move(source.value());
        CHECK(read_bytes(destination_root / L"session.marker").ends_with(
            "clean_shutdown=false\n"));
        shutdown_phase_to_throw.reset();
        kf2::app::set_ui_runtime_shutdown_probe_for_testing(nullptr);
        CHECK(destination.value().shutdown_cleanly().has_value());
        CHECK(read_bytes(source_root / L"session.marker").ends_with(
            "clean_shutdown=true\n"));
    }

    fs::remove_all(root);
    return EXIT_SUCCESS;
#endif
}

int test_update_worker_exception_boundaries() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    const auto test_root = root / L"update-worker-exceptions";
    fs::remove_all(test_root);
    kf2::diagnostics::EventLog events{128};
    kf2::update::detail::set_update_controller_allocation_hook_for_testing(
        throw_update_controller_allocation_failure);
    kf2::app::UiRuntime runtime{test_root / L"Data", false,
        kf2::config::Settings{}, events, std::nullopt,
        kf2::app::StartMode::read_only, test_root / L"portable"};
    CHECK(runtime.model.notice().has_value());
    CHECK(runtime.model.notice()->code == L"UPDATE_STATE_RESTORE_FAILED");
    runtime.model.clear_notice();
    kf2::update::detail::set_update_controller_allocation_hook_for_testing(
        throw_update_controller_allocation_failure);
    runtime.start_update_check(kf2::update::CheckTrigger::manual);
    CHECK(runtime.updates.controller.snapshot().phase ==
          kf2::update::UpdatePhase::idle);
    CHECK(runtime.model.notice().has_value());
    CHECK(runtime.model.notice()->code == L"UPDATE_CHECK_START_FAILED");
    runtime.model.clear_notice();
    runtime.updates.worker_launcher = [](std::function<void()> worker) {
        worker();
    };
    runtime.updates.check_operation = [](std::string_view)
        -> kf2::Result<std::optional<kf2::update::ReleaseInfo>> {
        throw std::runtime_error{"injected update-check failure"};
    };
    try {
        runtime.start_update_check(kf2::update::CheckTrigger::manual);
    } catch (...) {
        return EXIT_FAILURE;
    }
    runtime.poll_update_check();
    CHECK(!runtime.updates.check);
    CHECK(runtime.updates.controller.snapshot().phase ==
          kf2::update::UpdatePhase::error);
    CHECK(runtime.updates.controller.snapshot().status ==
          L"Update check encountered an unexpected local error");

    kf2::update::ReleaseInfo release{
        .repository = "zeroking1998/KF2-Optimizer-Next",
        .tag = "v0.0.5-alpha",
        .version = "0.0.5-alpha",
        .asset = kf2::update::ReleaseAsset{
            .file_name = "KF2OptimizerNext.zip",
            .download_url = "https://example.invalid/update.zip",
            .size_bytes = 1,
            .sha256 = std::string(64, 'a')}};
    CHECK(runtime.updates.controller.begin_check(
              kf2::update::CheckTrigger::manual, 1) ==
          kf2::update::CheckStart::started);
    runtime.updates.controller.complete_check(
        kf2::Result<std::optional<kf2::update::ReleaseInfo>>::success(
            std::move(release)));
    kf2::update::detail::set_update_controller_allocation_hook_for_testing(
        throw_update_controller_allocation_failure);
    runtime.start_update_install();
    CHECK(runtime.updates.controller.snapshot().phase ==
          kf2::update::UpdatePhase::available);
    CHECK(runtime.model.notice().has_value());
    CHECK(runtime.model.notice()->code == L"UPDATE_INSTALL_START_FAILED");
    runtime.model.clear_notice();
    runtime.updates.install_operation = [](
        const kf2::update::ReleaseInfo&, const fs::path&)
        -> kf2::Result<kf2::update::PreparedUpdatePackage> {
        throw std::runtime_error{"injected update-install failure"};
    };
    try {
        runtime.start_update_install();
    } catch (...) {
        return EXIT_FAILURE;
    }
    runtime.poll_update_install();
    CHECK(!runtime.updates.install);
    CHECK(runtime.updates.controller.snapshot().phase ==
          kf2::update::UpdatePhase::available);
    CHECK(runtime.updates.controller.snapshot().status ==
          L"Update preparation encountered an unexpected local error");

    kf2::update::detail::set_update_controller_allocation_hook_for_testing(
        throw_update_controller_allocation_failure);
    runtime.ignore_update();
    CHECK(!runtime.updates.controller.snapshot().dismissed);
    CHECK(runtime.updates.controller.snapshot().ignored_version.empty());
    CHECK(runtime.model.notice().has_value());
    CHECK(runtime.model.notice()->code == L"UPDATE_IGNORE_FAILED");
    runtime.model.clear_notice();

    int launch_attempts = 0;
    runtime.updates.worker_launcher = [&](std::function<void()>) {
        ++launch_attempts;
        throw std::system_error{
            std::make_error_code(std::errc::resource_unavailable_try_again)};
    };
    try {
        runtime.start_update_install();
    } catch (...) {
        return EXIT_FAILURE;
    }
    CHECK(launch_attempts == 1);
    CHECK(!runtime.updates.install);
    CHECK(runtime.updates.controller.snapshot().phase ==
          kf2::update::UpdatePhase::available);
    CHECK(runtime.updates.controller.snapshot().status ==
          L"Update installation could not start its background worker.");

    CHECK(runtime.updates.controller.begin_check(
              kf2::update::CheckTrigger::manual, 2) ==
          kf2::update::CheckStart::started);
    runtime.updates.controller.complete_check(
        kf2::Result<std::optional<kf2::update::ReleaseInfo>>::success(
            std::nullopt));
    try {
        runtime.start_update_check(kf2::update::CheckTrigger::manual);
    } catch (...) {
        return EXIT_FAILURE;
    }
    CHECK(launch_attempts == 2);
    CHECK(!runtime.updates.check);
    CHECK(runtime.updates.controller.snapshot().phase ==
          kf2::update::UpdatePhase::error);
    CHECK(runtime.updates.controller.snapshot().status ==
          L"Update check could not start its background worker");

    fs::remove_all(test_root);
    return EXIT_SUCCESS;
}

int test_map_prewarm_retry_scheduler() {
    namespace fs = std::filesystem;
    using kf2::game::StartupPrewarmOptions;
    using kf2::game::StartupPrewarmState;
    using kf2::game::StorageKind;
    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;

    const fs::path root{KF2_TEST_ROOT};
    const auto test_root = root / L"map-prewarm-retry";
    fs::remove_all(test_root);
    fs::create_directories(test_root);
    kf2::diagnostics::EventLog events{128};
    kf2::app::UiRuntime runtime{test_root / L"Data", false,
        kf2::config::Settings{}, events, std::nullopt,
        kf2::app::StartMode::read_only, test_root / L"portable"};
    runtime.installation = kf2::game::GameInstallation{
        .install_root = test_root};
    runtime.game_process = kf2::game::GameProcessIdentity{.pid = 1};
    replace_runtime_gameplay(runtime, [](auto& gameplay) {
        gameplay.main_menu = true;
    });

    const auto wait_for_state = [&](StartupPrewarmState expected) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (runtime.map_prewarmer.snapshot().state == expected) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return runtime.map_prewarmer.snapshot().state == expected;
    };
    const auto arrange_terminal_skip = [&](StartupPrewarmOptions options,
                                           StartupPrewarmState expected) {
        runtime.map_prewarm_observed = L"KF-Retry";
        runtime.map_prewarm_active = L"KF-Retry";
        runtime.map_prewarm_last_attempted = L"KF-Retry";
        runtime.map_prewarmer.start(test_root, std::move(options));
        return wait_for_state(expected);
    };
    const auto verify_bounded_retry = [&](StartupPrewarmOptions options,
                                          StartupPrewarmState expected) {
        CHECK(arrange_terminal_skip(std::move(options), expected));
        runtime.poll_map_prewarm();
        CHECK(runtime.map_prewarm_active.empty());
        CHECK(runtime.map_prewarm_pending.empty());
        CHECK(runtime.map_prewarm_retry_not_before_ns >
              runtime.monotonic_ns());
        runtime.map_prewarm_retry_not_before_ns = 1;
        runtime.poll_map_prewarm();
        CHECK(runtime.map_prewarm_active == L"KF-Retry");
        runtime.stop_map_prewarm_for_load();
        runtime.map_prewarmer.stop_and_wait();
        return EXIT_SUCCESS;
    };

    CHECK(verify_bounded_retry({
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::solid_state,
        .available_memory_override = 4 * gib,
        .map_name = L"KF-Retry",
        .include_common_startup_files = false,
    }, StartupPrewarmState::skipped_no_files) == EXIT_SUCCESS);
    CHECK(verify_bounded_retry({
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::unknown,
        .available_memory_override = 4 * gib,
        .map_name = L"KF-Retry",
        .include_common_startup_files = false,
    }, StartupPrewarmState::skipped_unknown_storage) == EXIT_SUCCESS);
    CHECK(verify_bounded_retry({
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::solid_state,
        .available_memory_override = 2 * gib,
        .map_name = L"KF-Retry",
        .include_common_startup_files = false,
    }, StartupPrewarmState::skipped_low_memory) == EXIT_SUCCESS);

    CHECK(arrange_terminal_skip({
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::solid_state,
        .available_memory_override = 4 * gib,
        .map_name = L"KF-Retry",
        .include_common_startup_files = false,
    }, StartupPrewarmState::skipped_no_files));
    runtime.poll_map_prewarm();
    CHECK(runtime.map_prewarm_retry_not_before_ns != 0);
    runtime.observe_map_prewarm_selection(L"KF-Other");
    CHECK(runtime.map_prewarm_retry_not_before_ns == 0);
    runtime.poll_map_prewarm();
    CHECK(runtime.map_prewarm_active == L"KF-Other");
    runtime.stop_map_prewarm_for_load();
    runtime.map_prewarmer.stop_and_wait();

    CHECK(arrange_terminal_skip({
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::solid_state,
        .available_memory_override = 4 * gib,
        .map_name = L"KF-Retry",
        .include_common_startup_files = false,
    }, StartupPrewarmState::skipped_no_files));
    runtime.poll_map_prewarm();
    CHECK(runtime.map_prewarm_retry_not_before_ns != 0);
    replace_runtime_gameplay(runtime, [](auto& gameplay) {
        gameplay.main_menu = false;
    });
    runtime.poll_map_prewarm();
    CHECK(runtime.map_prewarm_observed.empty());
    CHECK(runtime.map_prewarm_last_attempted.empty());
    CHECK(runtime.map_prewarm_retry_not_before_ns == 0);

    fs::remove_all(test_root);
    return EXIT_SUCCESS;
}

int test_map_prewarm_start_is_visible_before_worker_entry() {
    namespace fs = std::filesystem;
    using kf2::game::StartupPrewarmState;

    const fs::path root{KF2_TEST_ROOT};
    const auto test_root = root / L"map-prewarm-start-visibility";
    fs::remove_all(test_root);
    fs::create_directories(test_root);
    kf2::diagnostics::EventLog events{128};
    kf2::app::UiRuntime runtime{test_root / L"Data", false,
        kf2::config::Settings{}, events, std::nullopt,
        kf2::app::StartMode::read_only, test_root / L"portable"};
    runtime.installation = kf2::game::GameInstallation{
        .install_root = test_root};
    runtime.game_process = kf2::game::GameProcessIdentity{.pid = 1};
    replace_runtime_gameplay(runtime, [](auto& gameplay) {
        gameplay.main_menu = true;
    });

    kf2::game::detail::delay_next_startup_prewarm_worker_entry(
        std::chrono::seconds{30});
    runtime.observe_map_prewarm_selection(L"KF-DelayedWorker");
    runtime.poll_map_prewarm();
    CHECK(runtime.map_prewarmer.snapshot().state ==
          StartupPrewarmState::waiting);
    CHECK(runtime.map_prewarm_active == L"KF-DelayedWorker");

    runtime.poll_map_prewarm();
    CHECK(runtime.map_prewarm_active == L"KF-DelayedWorker");
    CHECK(runtime.model.status().prewarm_active);
    CHECK(runtime.model.status().prewarm_map == L"KF-DelayedWorker");

    replace_runtime_gameplay(runtime, [](auto& gameplay) {
        gameplay.main_menu = false;
    });
    runtime.poll_map_prewarm();
    CHECK(runtime.map_prewarm_active.empty());
    CHECK(runtime.map_prewarm_pending.empty());
    CHECK(runtime.map_prewarm_observed.empty());
    runtime.map_prewarmer.stop_and_wait();
    const auto stopped = runtime.map_prewarmer.snapshot();
    CHECK(stopped.state == StartupPrewarmState::cancelled);
    CHECK(stopped.bytes_read == 0);

    fs::remove_all(test_root);
    return EXIT_SUCCESS;
}

int test_pending_policy_restage_failure_rollback() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path{KF2_TEST_ROOT} /
        L"pending-policy-transaction";
    fs::remove_all(root);
    const auto config_root = root / L"Config";
    const auto install_root = root / L"KillingFloor2";
    const auto executable = install_root / L"Binaries/Win64/KFGame.exe";
    write_test_pe(executable);
    CHECK(write_complete_config_catalog(config_root));

    const auto state_root = root / L"Data";
    kf2::diagnostics::EventLog events{128};
    kf2::config::Settings initial;
    initial.target_fps = 90;
    initial.corpse_limit = 40;
    kf2::app::UiRuntime runtime{
        state_root, false, initial, events, std::nullopt,
        kf2::app::StartMode::normal, root / L"portable"};
    runtime.installation = kf2::game::GameInstallation{
        .install_root = install_root,
        .executable = executable,
        .config_root = config_root};

    auto captured = kf2::config::capture_session_config(
        config_root, state_root);
    CHECK(captured.has_value());
    runtime.session_config_snapshot = std::move(captured.value());
    const auto control_token = kf2::game::generate_adaptive_control_token();
    CHECK(control_token.has_value());
    runtime.adaptive_control_token = control_token.value();

    int restage_attempts = 0;
    runtime.pending_policy_restage_operation =
        [&](const fs::path&, bool, int, int, bool, int,
            std::string_view, bool, bool, bool) {
            ++restage_attempts;
            return kf2::Result<bool>::failure({
                kf2::ErrorCode::io_failure,
                L"Injected pending-policy restage failure", 0});
        };
    runtime.set_slider_value("settings-target-slider", 144);

    CHECK(restage_attempts == 1);
    CHECK(runtime.optimizer_settings.target_fps == 90);
    CHECK(runtime.model.status().target_fps == 90);
    const auto rolled_back = kf2::config::parse_settings(
        read_bytes(runtime.settings_path));
    CHECK(rolled_back.has_value());
    CHECK(rolled_back.value().target_fps == 90);
    CHECK(!runtime.session_config_snapshot.has_value());
    CHECK(!runtime.model.recovery_required());
    CHECK(runtime.model.notice().has_value());
    CHECK(runtime.model.notice()->code ==
          L"ADAPTIVE_PENDING_POLICY_UPDATE_FAILED");
    const auto failure_log = events.snapshot();
    CHECK(std::any_of(failure_log.begin(), failure_log.end(),
        [](const auto& event) {
            return event.code ==
                   "ADAPTIVE_PENDING_POLICY_UPDATE_FAILED";
        }));
    return EXIT_SUCCESS;
}

int test_gpu_profile_persistence_rollback() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path{KF2_TEST_ROOT} /
        L"gpu-profile-persistence-rollback";
    fs::remove_all(root);

    kf2::diagnostics::EventLog events{32};
    kf2::config::Settings initial;
    initial.extras["confirmed_gpu_physical_key"] = "PCI\\VEN_OLD";
    initial.extras["confirmed_gpu_preference"] = "high_performance";
    initial.extras["confirmed_gpu_dedicated_bytes"] = "2147483648";
    initial.extras["unrelated"] = "preserved";
    kf2::app::UiRuntime runtime{
        root / L"Data", false, initial, events, std::nullopt,
        kf2::app::StartMode::normal, root / L"portable"};
    runtime.confirmed_game_adapter_luid = 22;
    runtime.gpu_profile_settings_write_for_testing =
        fail_gpu_profile_settings_write;
    const auto durable_before =
        kf2::config::serialize_settings(runtime.optimizer_settings);

    CHECK(!runtime.remember_confirmed_gpu_profile(
        "PCI\\VEN_NEW",
        kf2::telemetry::ProcessGpuPreference::minimum_power,
        8ULL * 1024ULL * 1024ULL * 1024ULL));
    CHECK(runtime.confirmed_game_adapter_luid == 22);
    CHECK(kf2::config::serialize_settings(runtime.optimizer_settings) ==
          durable_before);
    CHECK(!fs::exists(runtime.settings_path));

    const std::vector<kf2::telemetry::GpuAdapter> adapters{
        {.luid = 11,
         .name = L"Previously persisted GPU",
         .dedicated_memory_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL,
         .physical_device_key = L"PCI\\VEN_OLD"},
        {.luid = 22,
         .name = L"Currently confirmed GPU",
         .dedicated_memory_bytes = 8ULL * 1024ULL * 1024ULL * 1024ULL,
         .physical_device_key = L"PCI\\VEN_NEW"},
    };
    const auto& persisted_key_text = runtime.optimizer_settings.extras.at(
        "confirmed_gpu_physical_key");
    const std::wstring persisted_key{
        persisted_key_text.begin(), persisted_key_text.end()};
    const bool preference_still_matches =
        runtime.optimizer_settings.extras.at("confirmed_gpu_preference") ==
        "high_performance";
    const auto rebuilt = kf2::optimizer::resolve_startup_gpu_profile(
        adapters, std::nullopt, persisted_key, preference_still_matches);
    CHECK(rebuilt.has_value());
    CHECK(rebuilt->source ==
          kf2::optimizer::StartupGpuProfileSource::
              previously_confirmed_adapter);
    CHECK(rebuilt->adapter.has_value());
    CHECK(rebuilt->adapter->luid == 11);

    const auto log = events.snapshot();
    const auto failure = std::find_if(log.begin(), log.end(),
        [](const auto& event) {
            return event.code == "GAME_GPU_PROFILE_REMEMBER_FAILED";
        });
    CHECK(failure != log.end());
    CHECK(failure->severity == kf2::diagnostics::Severity::warning);
    CHECK(failure->message.find(L"last durable settings") !=
          std::wstring::npos);
    fs::remove_all(root);
    return EXIT_SUCCESS;
}

int test_restore_cap_sync_failure() {
#if !defined(KF2_APPLICATION_RESTORE_TESTING)
    return EXIT_FAILURE;
#else
    namespace fs = std::filesystem;
    const fs::path root = fs::path{KF2_TEST_ROOT} /
        L"restore-cap-sync-failure";
    fs::remove_all(root);
    const auto config_root = root / L"Config";
    const auto target = config_root / L"KFGame.ini";
    const std::string original =
        "[KFGame.KFGameEngine]\r\n"
        "bSmoothFrameRate=True\r\n"
        "MinSmoothedFrameRate=22.000000\r\n"
        "MaxSmoothedFrameRate=60.000000\r\n";
    const std::string changed =
        "[KFGame.KFGameEngine]\r\n"
        "bSmoothFrameRate=True\r\n"
        "MinSmoothedFrameRate=22.000000\r\n"
        "MaxSmoothedFrameRate=90.000000\r\n";
    write_bytes(target, original);

    kf2::diagnostics::EventLog events{32};
    kf2::config::Settings settings;
    settings.target_fps = 60;
    {
        kf2::app::UiRuntime runtime{
            root / L"Data", false, settings, events, std::nullopt,
            kf2::app::StartMode::normal, root / L"portable"};
        runtime.installation = kf2::game::GameInstallation{
            .install_root = root / L"Game",
            .config_root = config_root};

        kf2::config::ConfigPreview preview;
        preview.config_root = config_root;
        preview.files.push_back({L"KFGame.ini", original, changed});
        const auto applied = kf2::config::apply_preview(
            preview, runtime.backups, {.game_running = false});
        CHECK(applied.has_value());
        CHECK(read_bytes(target) == changed);

        int synchronization_attempts = 0;
        runtime.frame_rate_cap_sync_for_testing = [&] {
            ++synchronization_attempts;
            return kf2::Result<kf2::game::FrameRateCapResult>::failure({
                kf2::ErrorCode::io_failure,
                L"Injected native frame-cap synchronization failure", 1234});
        };
        const auto restored = runtime.restore(
            applied.value().backup.id, {.game_running = false});
        CHECK(synchronization_attempts == 1);
        CHECK(!restored.has_value());
        CHECK(restored.error().code ==
              kf2::ErrorCode::recovery_required);
        CHECK(restored.error().native_code == 1234);
        CHECK(restored.error().message.find(L"Restored 1") !=
              std::wstring::npos);
        CHECK(restored.error().message.find(L"pre-restore backup") !=
              std::wstring::npos);
        CHECK(restored.error().message.find(
                  L"Injected native frame-cap synchronization failure") !=
              std::wstring::npos);
        CHECK(read_bytes(target) == original);

        const auto backups = runtime.backups.list_backups();
        CHECK(backups.has_value());
        CHECK(backups.value().size() >= 2);
    }

    const auto log = events.snapshot();
    const auto failure = std::find_if(log.begin(), log.end(),
        [](const auto& event) {
            return event.code == "TARGET_FPS_PERSIST_FAILED";
        });
    CHECK(failure != log.end());
    CHECK(failure->severity == kf2::diagnostics::Severity::error);
    CHECK(failure->message.find(L"Restored 1") != std::wstring::npos);
    CHECK(failure->message.find(L"pre-restore backup") !=
          std::wstring::npos);
    CHECK(std::none_of(log.begin(), log.end(), [](const auto& event) {
        return event.code == "CONFIG_RESTORED";
    }));
    fs::remove_all(root);
    return EXIT_SUCCESS;
#endif
}

int test_gameplay_snapshot_lifetime() {
    namespace fs = std::filesystem;
    const fs::path root = fs::path{KF2_TEST_ROOT} /
        L"gameplay-snapshot-lifetime";
    fs::remove_all(root);
    fs::create_directories(root);
    kf2::diagnostics::EventLog events{32};
    kf2::app::UiRuntime runtime{root / L"Data", false,
        kf2::config::Settings{}, events, std::nullopt,
        kf2::app::StartMode::read_only, root / L"portable"};

    kf2::game::GameLogSession first;
    first.map = "KF-First";
    const auto retained = kf2::game::make_game_log_session_snapshot(
        std::move(first));
    runtime.game_log_session = retained;
    runtime.last_report_gameplay_session = runtime.game_log_session;
    CHECK(runtime.game_log_session.get() ==
          runtime.last_report_gameplay_session.get());

    kf2::game::GameLogSession travelled;
    travelled.map = "KF-Second";
    runtime.game_log_session = kf2::game::make_game_log_session_snapshot(
        std::move(travelled));
    runtime.last_report_gameplay_session = runtime.game_log_session;
    CHECK(runtime.game_log_session.get() ==
          runtime.last_report_gameplay_session.get());
    CHECK(runtime.game_log_session.get() != retained.get());
    CHECK(retained->map == "KF-First");
    CHECK(runtime.game_log_session->map == "KF-Second");

    runtime.detach_telemetry(false);
    CHECK(!runtime.game_log_session);
    CHECK(!runtime.last_report_gameplay_session);
    CHECK(retained->map == "KF-First");
    fs::remove_all(root);
    return EXIT_SUCCESS;
}

#if defined(KF2_APPLICATION_LAUNCH_TESTING)
kf2::config::SettingId launch_failure_setting{};
std::size_t changes_before_launch_failure{};
void throw_launch_change_allocation(
    kf2::config::SettingId id, std::size_t prepared_count) {
    if (id == launch_failure_setting) {
        changes_before_launch_failure = prepared_count;
        throw std::bad_alloc{};
    }
}
#endif

int test_launch_profile_allocation_failures() {
    namespace fs = std::filesystem;
    using kf2::config::RequestedChange;
    using kf2::config::SettingId;
    using kf2::app::set_launch_change_probe_for_testing;
    std::vector<RequestedChange> changes;
    const kf2::optimizer::StartupMemoryProfile memory_profile{
        .texture_pool_size_mb = 6000,
        .memory_margin_mb = 128,
        .streaming_hysteresis_limit = 40};
    CHECK(!noexcept(kf2::app::enforce_temporal_aa_disabled(changes)));
    CHECK(!noexcept(kf2::app::enforce_async_physics_enabled(changes)));
    CHECK(!noexcept(kf2::app::enforce_fixed_flex_substeps(changes, true)));
    CHECK(!noexcept(kf2::app::enforce_one_frame_thread_lag(changes)));
    CHECK(!noexcept(kf2::app::enforce_startup_memory_profile(changes, memory_profile)));

    // Every allocating helper must propagate failure, including a partially
    // assembled multi-setting helper. Existing success tests cover its values.
    for (const auto id : {SettingId::temporal_aa, SettingId::enable_async_scene,
                         SettingId::max_physics_substeps,
                         SettingId::one_frame_thread_lag,
                         SettingId::texture_streaming_memory_margin}) {
        changes.clear();
        launch_failure_setting = id;
        set_launch_change_probe_for_testing(throw_launch_change_allocation);
        bool propagated = false;
        try {
            switch (id) {
            case SettingId::temporal_aa:
                kf2::app::enforce_temporal_aa_disabled(changes); break;
            case SettingId::enable_async_scene:
                kf2::app::enforce_async_physics_enabled(changes); break;
            case SettingId::max_physics_substeps:
                kf2::app::enforce_fixed_flex_substeps(changes, true); break;
            case SettingId::one_frame_thread_lag:
                kf2::app::enforce_one_frame_thread_lag(changes); break;
            default:
                kf2::app::enforce_startup_memory_profile(changes, memory_profile); break;
            }
        } catch (const std::bad_alloc&) {
            propagated = true;
        }
        set_launch_change_probe_for_testing(nullptr);
        CHECK(propagated);
        CHECK(changes.size() == ((id == SettingId::enable_async_scene ||
            id == SettingId::texture_streaming_memory_margin) ? 1U : 0U));
    }

    const fs::path root = fs::path{KF2_TEST_ROOT} / L"launch-allocation";
    fs::remove_all(root);
    const auto config_root = root / L"Config";
    CHECK(write_complete_config_catalog(config_root));
    std::map<fs::path, std::string> original;
    for (const auto& file : fs::directory_iterator{config_root}) {
        original.emplace(file.path(), read_bytes(file.path()));
    }
    kf2::diagnostics::EventLog events{64};
    kf2::app::UiRuntime runtime{root / L"Data", false,
        kf2::config::Settings{}, events, std::nullopt,
        kf2::app::StartMode::normal, root / L"portable"};
    runtime.installation = kf2::game::GameInstallation{.config_root = config_root};
    runtime.adaptive_locks_valid = true;
    for (const bool adaptive : {true, false}) {
        runtime.optimizer_settings.adaptive_optimization_enabled = adaptive;
        for (const auto id : {SettingId::temporal_aa, SettingId::enable_async_scene,
                             SettingId::max_physics_substeps,
                             SettingId::one_frame_thread_lag}) {
            if (!adaptive && id != SettingId::max_physics_substeps) continue;
            // A stale preview must never become a fallback for a failed build.
            runtime.preview = kf2::config::ConfigPreview{};
            launch_failure_setting = id;
            changes_before_launch_failure = 0;
            set_launch_change_probe_for_testing(throw_launch_change_allocation);
            const auto failed = runtime.apply_adaptive_launch_profile(true);
            set_launch_change_probe_for_testing(nullptr);
            CHECK(!failed.has_value());
            CHECK(failed.error().code == kf2::ErrorCode::internal_failure);
            CHECK(failed.error().native_code == ERROR_NOT_ENOUGH_MEMORY);
            CHECK(changes_before_launch_failure >= 1);
            CHECK(!runtime.preview.has_value());
            CHECK(runtime.last_backup_id.empty());
            const auto backups = runtime.backups.list_backups();
            CHECK(backups.has_value());
            CHECK(backups.value().empty());
            for (const auto& [path, bytes] : original) {
                CHECK(read_bytes(path) == bytes);
            }
        }
    }
    const auto log = events.snapshot();
    CHECK(std::none_of(log.begin(), log.end(), [](const auto& event) {
        return event.code == "CONFIG_APPLIED" || event.code == "CONFIG_PREVIEW_READY";
    }));
    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--launch-profile-allocation") {
        return test_launch_profile_allocation_failures();
    }
    if (argc == 2 &&
        std::string_view{argv[1]} == "--package-repair-start-failure") {
        return test_package_repair_worker_start_failure();
    }
    if (argc == 2 &&
        std::string_view{argv[1]} == "--update-worker-exceptions") {
        return test_update_worker_exception_boundaries();
    }
    if (argc == 2 &&
        std::string_view{argv[1]} ==
            "--pending-policy-restage-failure") {
        return test_pending_policy_restage_failure_rollback();
    }
    if (argc == 2 &&
        std::string_view{argv[1]} ==
            "--gpu-profile-persistence-rollback") {
        return test_gpu_profile_persistence_rollback();
    }
    if (argc == 2 &&
        std::string_view{argv[1]} ==
            "--map-prewarm-start-visibility") {
        return test_map_prewarm_start_is_visible_before_worker_entry();
    }
    if (argc == 2 &&
        std::string_view{argv[1]} ==
            "--runtime-shutdown-exceptions") {
        return test_runtime_shutdown_exception_boundaries();
    }
    if (argc == 2 &&
        std::string_view{argv[1]} ==
            "--restore-cap-sync-failure") {
        return test_restore_cap_sync_failure();
    }
    try {
        const auto inaccessible = kf2::app::load_or_create_settings(
            std::filesystem::path{std::wstring(40'000, L'x')});
        CHECK(!inaccessible.has_value());
    } catch (const std::filesystem::filesystem_error&) {
        return EXIT_FAILURE;
    }
    CHECK(test_pending_policy_restage_failure_rollback() == EXIT_SUCCESS);
    CHECK(test_map_prewarm_retry_scheduler() == EXIT_SUCCESS);
    CHECK(test_gameplay_snapshot_lifetime() == EXIT_SUCCESS);
    CHECK(kf2::app::should_prepare_protected_gameplay_provider(
        kf2::app::StartMode::normal));
    CHECK(!kf2::app::should_prepare_protected_gameplay_provider(
        kf2::app::StartMode::read_only));
    CHECK(!kf2::app::should_prepare_fixed_flex_runtime(
        kf2::app::StartMode::normal, 0));
    CHECK(kf2::app::should_prepare_fixed_flex_runtime(
        kf2::app::StartMode::normal, 1));
    CHECK(kf2::app::should_prepare_fixed_flex_runtime(
        kf2::app::StartMode::normal, 2));
    std::vector<kf2::config::RequestedChange> flex_preservation_changes{
        {kf2::config::SettingId::target_fps, 120,
         kf2::config::ChangeSource::adaptive, L"test"},
        {kf2::config::SettingId::physx_level, 2,
         kf2::config::ChangeSource::adaptive, L"must be removed"}};
    kf2::app::preserve_user_flex_activation(flex_preservation_changes);
    CHECK(flex_preservation_changes.size() == 1);
    CHECK(flex_preservation_changes.front().id ==
          kf2::config::SettingId::target_fps);
    kf2::app::enforce_temporal_aa_disabled(flex_preservation_changes);
    CHECK(flex_preservation_changes.size() == 2);
    CHECK(flex_preservation_changes.back().id ==
          kf2::config::SettingId::temporal_aa);
    CHECK(std::get<bool>(flex_preservation_changes.back().value) == false);
    flex_preservation_changes.back().value = true;
    kf2::app::enforce_temporal_aa_disabled(flex_preservation_changes);
    CHECK(flex_preservation_changes.size() == 2);
    CHECK(std::get<bool>(flex_preservation_changes.back().value) == false);
    kf2::app::enforce_async_physics_enabled(flex_preservation_changes);
    CHECK(flex_preservation_changes.size() == 4);
    CHECK(flex_preservation_changes[2].id ==
          kf2::config::SettingId::physics_async_scene);
    CHECK(std::get<bool>(flex_preservation_changes[2].value));
    CHECK(flex_preservation_changes[3].id ==
          kf2::config::SettingId::enable_async_scene);
    CHECK(std::get<bool>(flex_preservation_changes[3].value));
    flex_preservation_changes[2].value = false;
    flex_preservation_changes[3].value = false;
    kf2::app::enforce_async_physics_enabled(flex_preservation_changes);
    CHECK(flex_preservation_changes.size() == 4);
    CHECK(std::get<bool>(flex_preservation_changes[2].value));
    CHECK(std::get<bool>(flex_preservation_changes[3].value));
    kf2::app::enforce_fixed_flex_substeps(
        flex_preservation_changes, false);
    CHECK(flex_preservation_changes.size() == 4);
    kf2::app::enforce_fixed_flex_substeps(
        flex_preservation_changes, true);
    CHECK(flex_preservation_changes.size() == 5);
    CHECK(flex_preservation_changes[4].id ==
          kf2::config::SettingId::max_physics_substeps);
    CHECK(std::get<int>(flex_preservation_changes[4].value) == 1);
    flex_preservation_changes[4].value = 5;
    kf2::app::enforce_fixed_flex_substeps(
        flex_preservation_changes, true);
    CHECK(flex_preservation_changes.size() == 5);
    CHECK(std::get<int>(flex_preservation_changes[4].value) == 1);
    kf2::app::enforce_one_frame_thread_lag(flex_preservation_changes);
    CHECK(flex_preservation_changes.size() == 6);
    CHECK(flex_preservation_changes[5].id ==
          kf2::config::SettingId::one_frame_thread_lag);
    CHECK(std::get<bool>(flex_preservation_changes[5].value));
    const kf2::optimizer::StartupMemoryProfile startup_memory{
        .texture_pool_size_mb = 6000,
        .memory_margin_mb = 128,
        .streaming_hysteresis_limit = 40};
    kf2::app::enforce_startup_memory_profile(
        flex_preservation_changes, startup_memory);
    CHECK(flex_preservation_changes.size() == 9);
    CHECK(std::get<int>(flex_preservation_changes[6].value) == 6000);
    CHECK(std::get<int>(flex_preservation_changes[7].value) == 128);
    CHECK(std::get<int>(flex_preservation_changes[8].value) == 40);
    namespace fs = std::filesystem;
    CHECK(kf2::app::runtime::feature_definitions().size() == 7);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::overlay) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::diagnostics) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::backup) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::settings) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::game) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::graphics) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::advanced) != nullptr);
    CHECK(kf2::app::runtime::valid_feature_registry(
        kf2::app::runtime::feature_definitions()));
    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    const auto documents = root / L"Documents";
    const auto config_root = documents / L"My Games/KillingFloor2/KFGame/Config";
    const auto install_root = root / L"Steam/steamapps/common/KillingFloor2";
    write_test_pe(install_root / L"Binaries/Win64/KFGame.exe");
    fs::create_directories(install_root / L"KFGame");
    write_bytes(install_root / L"Engine/Config/ConsoleVariables.ini",
                "; native startup variables\r\n[Startup]\r\n");
    CHECK(write_complete_config_catalog(config_root));
    write_bytes(config_root / L"KFSystemSettings.ini",
                read_bytes(config_root / L"KFSystemSettings.ini") +
                    "[SystemSettings]\r\n"
                    "Fullscreen=True\r\n"
                    "Borderless=False\r\n");
    CHECK(read_bytes(config_root / L"KFSystemSettings.ini").find(
              "UseVsync=True") != std::string::npos);
    write_bytes(config_root / L"KFGame.ini",
                read_bytes(config_root / L"KFGame.ini") +
                    "[KFGameContent.KFGameInfo_Survival]\r\n"
                    "bLogAICount=False\r\n");
    write_bytes(config_root / L"KFEngine.ini",
                read_bytes(config_root / L"KFEngine.ini") +
                    "[URL]\r\n"
                    "LocalOptions=\r\n"
                    "[Engine.Engine]\r\n"
                    "GameViewportClientClassName=KFGame.KFGameViewportClient\r\n");
    const fs::path telemetry_asset{KF2_TELEMETRY_ASSET};
    CHECK(fs::exists(telemetry_asset));
    const auto portable_telemetry = root / L"portable" / L"Data" / L"Lab" /
        L"KF2OptimizerTelemetry.u";
    write_bytes(portable_telemetry, read_bytes(telemetry_asset));
    const std::wstring instance_name = L"Local\\KF2OptimizerNext-AppTest-" +
                                       std::to_wstring(GetCurrentProcessId());

    kf2::app::StartOptions options{
        .state_root = root / L"Data",
        .executable_root = root / L"portable",
        .instance_name = instance_name,
        .identity = {GetCurrentProcessId(), 1001},
        .create_window = false,
        .game_discovery = kf2::game::GameDiscoveryInput{
            .manual_candidates = {install_root},
            .config_root = config_root,
            .allowed_config_parent = documents,
        },
    };
    auto registry_failure_options = options;
    registry_failure_options.state_root = root / L"RegistryFailureData";
    registry_failure_options.instance_name += L"-RegistryFailure";
    registry_failure_options.adaptive_registry_initialization_probe = [] {
        throw std::runtime_error{"injected registry construction failure"};
    };
    const auto registry_failure =
        kf2::app::Application::start(registry_failure_options);
    CHECK(!registry_failure.has_value());
    CHECK(registry_failure.error().code == kf2::ErrorCode::internal_failure);
    CHECK(registry_failure.error().message ==
          L"Adaptive target registry could not be initialized");
    CHECK(!fs::exists(registry_failure_options.state_root));
    write_bytes(options.state_root / L"settings.ini",
                "schema_version=1\nadaptive_shadow_mode=true\n");

    const auto original_game_config = read_bytes(config_root / L"KFGame.ini");
    const auto original_engine_config = read_bytes(config_root / L"KFEngine.ini");
    const auto published_telemetry = config_root.parent_path() /
        L"Published" / L"BrewedPC" / L"KF2OptimizerTelemetry.u";
    const auto published_runtime_path =
        (config_root.parent_path() / L"Published" / L"BrewedPC")
            .lexically_normal().string();
    std::optional<kf2::optimizer::StartupMemoryProfile>
        expected_startup_memory;
    const auto adapters = kf2::telemetry::enumerate_gpu_adapters();
    if (adapters.has_value()) {
        const auto physical = kf2::telemetry::unique_physical_gpu_adapters(
            adapters.value());
        if (!physical.empty()) {
            // A historical renderer name must not select a high-memory profile
            // for a later launch on another adapter.
            const auto& historical = *std::max_element(
                physical.begin(), physical.end(), [](const auto& left,
                                                      const auto& right) {
                    return left.dedicated_memory_bytes <
                           right.dedicated_memory_bytes;
                });
            write_bytes(config_root.parent_path() / L"Logs" / L"Launch.log",
                        "[0002.55] Log: Adapter : " + utf8(historical.name) +
                            "\r\n");
            const auto resolved = kf2::optimizer::resolve_startup_gpu_profile(
                physical, std::nullopt, std::nullopt, false);
            if (resolved) expected_startup_memory = resolved->profile;
        }
    }

    {
        auto first = kf2::app::Application::start(options);
        CHECK(first.has_value());
        CHECK(read_bytes(config_root / L"KFSystemSettings.ini").find(
                  "UseVsync=False") != std::string::npos);
        CHECK(read_bytes(options.state_root / L"settings.ini").find(
                  "adaptive_shadow_mode") == std::string::npos);
        const auto automatically_prepared =
            read_bytes(config_root / L"KFGame.ini");
        CHECK(automatically_prepared.find(
                  "MaxSmoothedFrameRate=60") != std::string::npos);
        CHECK(automatically_prepared.find(
                  "MinSmoothedFrameRate=22") != std::string::npos);
        CHECK(automatically_prepared.find(
                  "bSmoothFrameRate=True") != std::string::npos);
        const auto automatically_prepared_engine =
            read_bytes(config_root / L"KFEngine.ini");
        CHECK(automatically_prepared_engine.find(
                  "[Engine.Physics]") != std::string::npos);
        CHECK(automatically_prepared_engine.find(
                  "bPhysicsAsyncScene=True") != std::string::npos);
        CHECK(automatically_prepared_engine.find(
                  "bEnableAsyncScene=True") != std::string::npos);
        if (expected_startup_memory) {
            // Adapter preference can change while this desktop integration
            // test runs. Exact profile selection is covered by the isolated
            // startup GPU tests; this boundary verifies that a safe profile
            // was actually staged without coupling to live registry state.
            CHECK(automatically_prepared_engine.find("PoolSize=") !=
                  std::string::npos);
            CHECK(automatically_prepared_engine.find("MemoryMargin=") !=
                  std::string::npos);
            CHECK(automatically_prepared_engine.find("HysteresisLimit=") !=
                  std::string::npos);
        }
        CHECK(read_bytes(config_root / L"KFSystemSettings.ini").find(
                  "OneFrameThreadLag=True") != std::string::npos);
        CHECK(read_bytes(config_root / L"KFSystemSettings.ini").find(
                  "Fullscreen=False") != std::string::npos);
        CHECK(read_bytes(config_root / L"KFSystemSettings.ini").find(
                  "Borderless=True") != std::string::npos);
        CHECK(fs::exists(published_telemetry));
        CHECK(read_bytes(published_telemetry) == read_bytes(telemetry_asset));
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "GameViewportClientClassName=KF2OptimizerTelemetry."
                  "KF2OptimizerGraphicsViewport") !=
              std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "LocalOptions=?Mutator=KF2OptimizerTelemetry."
                  "KF2OptimizerTelemetryMutator") != std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "Package=KF2OptimizerTelemetry") == std::string::npos);
        CHECK(fs::exists(options.state_root / L"settings.ini"));
        CHECK(fs::exists(options.state_root / L"session.marker"));
        CHECK(fs::is_directory(options.state_root / L"logs"));
        CHECK(wait_for_file(
            options.state_root / L"logs/session-events.json",
            std::chrono::seconds{2}));
        CHECK(read_bytes(options.state_root / L"settings.ini").starts_with(
            "schema_version=1\n"));
        CHECK(first.value().game_installation().has_value());
        const auto preview = first.value().prepare_config_changes({
            {kf2::config::SettingId::target_fps, 90,
             kf2::config::ChangeSource::explicit_user, L"integration"}});
        CHECK(preview.has_value());
        const auto applied = first.value().apply_prepared_config(
            {.game_running = false});
        CHECK(applied.has_value());
        CHECK(read_bytes(config_root / L"KFGame.ini").find(
            "MaxSmoothedFrameRate=90") != std::string::npos);
        const auto restored = first.value().restore_config(
            applied.value().backup.id, {.game_running = false});
        CHECK(restored.has_value());
        CHECK(read_bytes(config_root / L"KFGame.ini").find(
            "MaxSmoothedFrameRate=60") != std::string::npos);

        auto duplicate = kf2::app::Application::start(options);
        CHECK(!duplicate.has_value());
        CHECK(duplicate.error().code == kf2::ErrorCode::already_running);
        CHECK(first.value().shutdown_cleanly().has_value());
        auto expected_game_config = original_game_config;
        const auto original_cap = expected_game_config.find(
            "MaxSmoothedFrameRate=62.000000");
        CHECK(original_cap != std::string::npos);
        expected_game_config.replace(
            original_cap, std::strlen("MaxSmoothedFrameRate=62.000000"),
            "MaxSmoothedFrameRate=60.000000");
        CHECK(read_bytes(config_root / L"KFGame.ini") == expected_game_config);
        CHECK(read_bytes(config_root / L"KFEngine.ini") == original_engine_config);
        const auto restored_system_config =
            read_bytes(config_root / L"KFSystemSettings.ini");
        CHECK(restored_system_config.find("Fullscreen=True") !=
              std::string::npos);
        CHECK(restored_system_config.find("Borderless=False") !=
              std::string::npos);
        CHECK(read_bytes(install_root /
            L"Engine/Config/ConsoleVariables.ini").find(
                "t.MaxFPS=60") != std::string::npos);
        CHECK(!fs::exists(published_telemetry));
    }
    CHECK(read_bytes(options.state_root / L"session.marker").ends_with(
        "clean_shutdown=true\n"));

    // A historically poisoned snapshot can leave the legacy optimizer viewport
    // and token behind after the module marker is already gone. Startup must
    // remove that owned residue before capturing the next protected snapshot.
    const auto stale_game_baseline = read_bytes(config_root / L"KFGame.ini");
    auto poisoned_game = stale_game_baseline;
    const std::string native_ai_logging = "bLogAICount=False";
    const auto ai_logging_offset = poisoned_game.find(native_ai_logging);
    CHECK(ai_logging_offset != std::string::npos);
    poisoned_game.replace(
        ai_logging_offset, native_ai_logging.size(), "bLogAICount=True");
    poisoned_game +=
        "[KFGame.KFAISpawnManager_Short]\r\n"
        "bLogWaveSpawnTiming=True\r\n"
        "[KFGame.KFAISpawnManager_Normal]\r\n"
        "bLogWaveSpawnTiming=True\r\n"
        "[KFGame.KFAISpawnManager_Long]\r\n"
        "bLogWaveSpawnTiming=True\r\n";
    write_bytes(config_root / L"KFGame.ini", poisoned_game);
    auto poisoned_engine = read_bytes(config_root / L"KFEngine.ini");
    const std::string native_viewport =
        "GameViewportClientClassName=KFGame.KFGameViewportClient";
    const auto viewport_offset = poisoned_engine.find(native_viewport);
    CHECK(viewport_offset != std::string::npos);
    poisoned_engine.replace(
        viewport_offset, native_viewport.size(),
        "GameViewportClientClassName=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryViewport");
    poisoned_engine +=
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n"
        "AdaptiveTargetFPS=120\r\n"
        "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n"
        "OriginalLogAICount=False\r\n"
        "OriginalLogWaveSpawnTimingShort=Missing\r\n"
        "OriginalLogWaveSpawnTimingNormal=Missing\r\n"
        "OriginalLogWaveSpawnTimingLong=Missing\r\n";
    write_bytes(config_root / L"KFEngine.ini", poisoned_engine);
    options.identity.process_start_id = 10011;
    {
        auto stale_recovered = kf2::app::Application::start(options);
        CHECK(stale_recovered.has_value());
        CHECK(stale_recovered.value().shutdown_cleanly().has_value());
    }
    const auto recovered_engine = read_bytes(config_root / L"KFEngine.ini");
    CHECK(recovered_engine.find(
        "GameViewportClientClassName=KFGame.KFGameViewportClient") !=
        std::string::npos);
    CHECK(recovered_engine.find("[KF2OptimizerTelemetry.") ==
          std::string::npos);
    CHECK(recovered_engine.find("AdaptiveControlToken=") == std::string::npos);
    const auto recovered_game = read_bytes(config_root / L"KFGame.ini");
    CHECK(recovered_game.find("bLogAICount=False") != std::string::npos);
    CHECK(recovered_game.find("bLogWaveSpawnTiming=") == std::string::npos);
    CHECK(read_bytes(options.state_root / L"logs/session-events.json").find(
        "STALE_TELEMETRY_CONFIG_RECOVERED") != std::string::npos);

    {
        std::ofstream corrupt(options.state_root / L"settings.ini",
                              std::ios::binary | std::ios::trunc);
        corrupt << "invalid settings";
    }
    options.identity.process_start_id = 1002;
    {
        auto recovered = kf2::app::Application::start(options);
        CHECK(recovered.has_value());
        CHECK(fs::exists(options.state_root /
                         L"logs/previous-session-events.json"));
        CHECK(read_bytes(options.state_root /
                         L"logs/previous-session-events.json")
                  .find("APP_START") != std::string::npos);
        CHECK(read_bytes(options.state_root / L"logs/session-events.json")
                  .find("PREVIOUS_EVENT_LOG_ARCHIVED") != std::string::npos);
        CHECK(fs::exists(options.state_root / L"settings.ini.corrupt"));
        CHECK(read_bytes(options.state_root / L"settings.ini") ==
        "schema_version=1\noptimizer_mode=adaptive\n"
        "adaptive_optimization_enabled=true\n"
        "automatic_update_checks=true\n"
        "overlay_enabled=true\noverlay_show_fps=true\noverlay_show_frame_time=true\n"
        "overlay_show_cpu=true\noverlay_show_gpu=true\noverlay_show_memory=true\n"
        "debug_corpse_markers=false\ndebug_zed_markers=false\n"
        "debug_flex_diagnostics=false\n"
        "debug_runtime_diagnostics=false\n"
        "debug_corpse_physics_control=false\n"
        "restore_config_after_game=true\n"
        "adaptive_aggressiveness=balanced\n"
        "adaptive_minimum_quality=10\nadaptive_maximum_quality=100\n"
        "adaptive_quality_change_budget=2\nadaptive_headroom_percent=8\n"
        "adaptive_emergency_enabled=true\n"
        "adaptive_quality_recovery_enabled=true\n"
        "adaptive_manual_locks_enabled=true\n"
        "adaptive_calibration_enabled=true\nadaptive_logging=true\n"
        "overlay_position=top_right\n"
        "overlay_scale_percent=100\n"
        "target_fps=60\ncorpse_limit=20\n"
        "quality_policy=exact\n");
        CHECK(recovered.value().shutdown_cleanly().has_value());
    }
    {
        std::ofstream corrupt(options.state_root / L"settings.ini",
                              std::ios::binary | std::ios::trunc);
        corrupt << "invalid settings again";
    }
    options.identity.process_start_id = 10024;
    {
        auto recovered_again = kf2::app::Application::start(options);
        CHECK(recovered_again.has_value());
        CHECK(fs::exists(options.state_root / L"settings.ini.corrupt.2"));
        CHECK(recovered_again.value().shutdown_cleanly().has_value());
    }

    {
        std::ofstream oversized(options.state_root / L"settings.ini",
                                std::ios::binary | std::ios::trunc);
        oversized << std::string(256 * 1024 + 1, 'x');
    }
    options.identity.process_start_id = 100241;
    const auto oversized_settings = kf2::app::Application::start(options);
    CHECK(!oversized_settings.has_value());
    CHECK(oversized_settings.error().code == kf2::ErrorCode::access_denied);
    CHECK(fs::file_size(options.state_root / L"settings.ini") ==
          256 * 1024 + 1);
    CHECK(kf2::platform::windows::atomic_replace_utf8(
              options.state_root / L"settings.ini",
              kf2::config::serialize_settings(kf2::config::Settings{}))
              .has_value());

    const auto discovery = options.game_discovery;
    options.game_discovery.reset();
    options.identity.process_start_id = 10025;
    {
        auto no_game = kf2::app::Application::start(options);
        CHECK(no_game.has_value());
        CHECK(!no_game.value().game_installation().has_value());
        CHECK(no_game.value().ui_model().status().game == L"Game not detected");
        CHECK(no_game.value().shutdown_cleanly().has_value());
    }
    options.game_discovery = discovery;

    options.mode = kf2::app::StartMode::read_only;
    options.identity.process_start_id = 10026;
    {
        const auto settings_before_read_only =
            read_bytes(options.state_root / L"settings.ini");
        auto read_only = kf2::app::Application::start(options);
        CHECK(read_only.has_value());
        CHECK(read_only.value().ui_model().status().mode == L"Read-only");
        const auto preview = read_only.value().prepare_config_changes({
            {kf2::config::SettingId::target_fps, 144,
             kf2::config::ChangeSource::explicit_user, L"read-only integration"}});
        CHECK(preview.has_value());
        const auto blocked = read_only.value().apply_prepared_config(
            {.game_running = false});
        CHECK(!blocked.has_value());
        CHECK(blocked.error().code == kf2::ErrorCode::access_denied);
        CHECK(read_bytes(config_root / L"KFGame.ini").find(
            "MaxSmoothedFrameRate=60.000000") != std::string::npos);
        const auto overlay_blocked =
            read_only.value().set_overlay_enabled(true);
        CHECK(!overlay_blocked.has_value());
        CHECK(overlay_blocked.error().code == kf2::ErrorCode::access_denied);
        CHECK(read_bytes(options.state_root / L"settings.ini") ==
              settings_before_read_only);
        CHECK(read_only.value().shutdown_cleanly().has_value());
    }
    options.mode = kf2::app::StartMode::normal;

    {
        auto degraded_options = options;
        degraded_options.state_root = root / L"Data-component-warning";
        degraded_options.instance_name =
            L"Local\\KF2OptimizerNext-ComponentWarning-" +
            std::to_wstring(GetCurrentProcessId());
        degraded_options.identity.process_start_id = 10028;
        degraded_options.game_discovery.reset();
        degraded_options.startup_warning =
            L"A managed companion component does not match its package hash.";
        auto degraded = kf2::app::Application::start(degraded_options);
        CHECK(degraded.has_value());
        CHECK(degraded.value().ui_model().status().mode ==
              L"Adaptive / Automatic");
        CHECK(degraded.value().ui_model().notice().has_value());
        CHECK(degraded.value().ui_model().notice()->code ==
              L"PACKAGE_INTEGRITY_FAILED");
        const auto overlay_changed =
            degraded.value().set_overlay_enabled(true);
        CHECK(overlay_changed.has_value());
        CHECK(overlay_changed.value());
        CHECK(degraded.value().shutdown_cleanly().has_value());
    }

    {
        auto missing_options = options;
        missing_options.state_root = root / L"Data-missing-game";
        missing_options.instance_name =
            L"Local\\KF2OptimizerNext-MissingGame-" +
            std::to_wstring(GetCurrentProcessId());
        missing_options.identity.process_start_id = 10029;
        missing_options.create_window = true;
        missing_options.game_discovery = kf2::game::GameDiscoveryInput{
            .manual_candidates = {root / L"Missing-KF2"},
            .config_root = config_root,
            .allowed_config_parent = documents,
        };
        auto missing_game = kf2::app::Application::start(missing_options);
        CHECK(missing_game.has_value());
        const auto missing_hwnd =
            missing_game.value().native_window_handle();
        CHECK(missing_hwnd != nullptr);
        const auto unavailable_launch = node_center(
            missing_hwnd, missing_game.value().ui_model(), "dashboard-launch");
        CHECK(unavailable_launch.has_value());
        SendMessageW(missing_hwnd, WM_LBUTTONUP, 0,
                     MAKELPARAM(unavailable_launch->x,
                                unavailable_launch->y));
        CHECK(!missing_game.value().ui_model().notice().has_value());
        CHECK(!fs::exists(missing_options.state_root /
                          L"session-config" / L"active"));
        SendMessageW(missing_hwnd, WM_CLOSE, 0, 0);
        CHECK(missing_game.value().shutdown_cleanly().has_value());
    }

    options.identity.process_start_id = 1003;
    {
        auto interrupted = kf2::app::Application::start(options);
        CHECK(interrupted.has_value());
    }

    options.identity.process_start_id = 1004;
    options.create_window = true;
    {
        std::ofstream cached_update(
            options.state_root / L"update-state.ini",
            std::ios::binary | std::ios::trunc);
        cached_update <<
            "schema_version=2\n"
            "last_check_unix_seconds=1765000000\n"
            "last_result=available\n"
            "available_version=0.0.4-alpha\n"
            "ignored_version=\n";
    }
    auto graphical = kf2::app::Application::start(options);
    if (!graphical.has_value()) {
        std::wcerr << L"Graphical application start failed: "
                   << graphical.error().message << L" (native="
                   << graphical.error().native_code << L")\n";
    }
    CHECK(graphical.has_value());
    CHECK(graphical.value().ui_model().state_path() == options.state_root.wstring());
    CHECK(!graphical.value().ui_model().recovery_required());
    const auto hwnd = graphical.value().native_window_handle();
    CHECK(hwnd != nullptr);
    CHECK(graphical.value().overlay_enabled());
    CHECK(graphical.value().ui_model().status().overlay_show_fps);
    CHECK(graphical.value().ui_model().status().overlay_show_frame_time);
    CHECK(graphical.value().ui_model().status().overlay_show_cpu);
    CHECK(graphical.value().ui_model().status().overlay_show_gpu);
    CHECK(graphical.value().ui_model().status().overlay_show_memory);
    CHECK(graphical.value().ui_model().status().update_newer_version_known);
    CHECK(graphical.value().ui_model().status().adaptive_optimization_enabled);
    CHECK(graphical.value().ui_model().status().update_prompt_visible);
    CHECK(graphical.value().ui_model().status().update_available_version ==
          L"0.0.4-alpha");
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::dashboard);
    const auto ignore_update = node_center(
        hwnd, graphical.value().ui_model(), "settings-updates-ignore");
    CHECK(ignore_update.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(ignore_update->x, ignore_update->y));
    CHECK(!graphical.value().ui_model().status().update_prompt_visible);
    CHECK(read_bytes(options.state_root / L"update-state.ini").find(
              "ignored_version=0.0.4-alpha\n") != std::string::npos);
    const auto auto_update =
        node_center(hwnd, graphical.value().ui_model(), "header-auto-updates");
    CHECK(auto_update.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(auto_update->x, auto_update->y));
    CHECK(!graphical.value().ui_model().status().automatic_update_checks);
    CHECK(read_bytes(options.state_root / L"settings.ini").find(
              "automatic_update_checks=false\n") != std::string::npos);
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(auto_update->x, auto_update->y));
    CHECK(graphical.value().ui_model().status().automatic_update_checks);
    const auto adaptive_toggle = node_center(
        hwnd, graphical.value().ui_model(), "settings-adaptive-toggle");
    CHECK(adaptive_toggle.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(adaptive_toggle->x, adaptive_toggle->y));
    CHECK(!graphical.value().ui_model().status()
               .adaptive_optimization_enabled);
    CHECK(read_bytes(options.state_root / L"settings.ini").find(
              "adaptive_optimization_enabled=false\n") !=
          std::string::npos);
    const auto adaptive_toggle_again = node_center(
        hwnd, graphical.value().ui_model(), "settings-adaptive-toggle");
    CHECK(adaptive_toggle_again.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(adaptive_toggle_again->x,
                            adaptive_toggle_again->y));
    CHECK(graphical.value().ui_model().status()
              .adaptive_optimization_enabled);
    const auto advanced_navigation =
        node_center(hwnd, graphical.value().ui_model(), "nav-3");
    CHECK(advanced_navigation.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(advanced_navigation->x,
                            advanced_navigation->y));
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::advanced);
    CHECK(graphical.value().ui_model().status().advanced_available);
    const auto thread_lag = node_center(
        hwnd, graphical.value().ui_model(), "advanced-one-frame-thread-lag");
    CHECK(thread_lag.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(thread_lag->x, thread_lag->y));
    CHECK(!graphical.value().ui_model().status().advanced_dirty);
    CHECK(graphical.value().ui_model().status().advanced_values[0] == L"Off");
    CHECK(read_bytes(config_root / L"KFSystemSettings.ini").find(
              "OneFrameThreadLag=False") != std::string::npos);
    const auto debug_navigation =
        node_center(hwnd, graphical.value().ui_model(), "nav-4");
    CHECK(debug_navigation.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(debug_navigation->x,
                            debug_navigation->y));
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::debug);
    const auto corpse_markers = node_center(
        hwnd, graphical.value().ui_model(), "debug-corpse-markers");
    CHECK(corpse_markers.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(corpse_markers->x, corpse_markers->y));
    CHECK(graphical.value().ui_model().status().debug_corpse_markers);
    const auto zed_markers = node_center(
        hwnd, graphical.value().ui_model(), "debug-zed-markers");
    CHECK(zed_markers.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(zed_markers->x, zed_markers->y));
    CHECK(graphical.value().ui_model().status().debug_zed_markers);
    const auto physics_control = node_center(
        hwnd, graphical.value().ui_model(),
        "debug-corpse-physics-control");
    CHECK(physics_control.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(physics_control->x, physics_control->y));
    CHECK(graphical.value().ui_model().status().debug_corpse_physics_control);
    const auto flex_diagnostics = node_center(
        hwnd, graphical.value().ui_model(), "debug-flex-diagnostics");
    CHECK(flex_diagnostics.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(flex_diagnostics->x, flex_diagnostics->y));
    CHECK(graphical.value().ui_model().status().debug_flex_diagnostics);
    const auto runtime_diagnostics = node_center(
        hwnd, graphical.value().ui_model(), "debug-runtime-diagnostics");
    CHECK(runtime_diagnostics.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(runtime_diagnostics->x, runtime_diagnostics->y));
    CHECK(graphical.value().ui_model().status().debug_runtime_diagnostics);
    const auto debug_settings_bytes =
        read_bytes(options.state_root / L"settings.ini");
    CHECK(debug_settings_bytes.find("debug_corpse_markers=true\n") !=
          std::string::npos);
    CHECK(debug_settings_bytes.find("debug_zed_markers=true\n") !=
          std::string::npos);
    CHECK(debug_settings_bytes.find(
              "debug_corpse_physics_control=true\n") != std::string::npos);
    CHECK(debug_settings_bytes.find(
              "debug_flex_diagnostics=true\n") != std::string::npos);
    CHECK(debug_settings_bytes.find(
              "debug_runtime_diagnostics=true\n") != std::string::npos);
    CHECK(read_bytes(config_root / L"KFEngine.ini").find(
              "bAdaptiveCorpseDebugMarkers=True") != std::string::npos);
    CHECK(read_bytes(config_root / L"KFEngine.ini").find(
              "bAdaptiveZedDebugMarkers=True") != std::string::npos);
    CHECK(read_bytes(config_root / L"KFEngine.ini").find(
              "bDetailedRuntimeDiagnostics=True") != std::string::npos);
    CHECK(read_bytes(config_root / L"KFEngine.ini").find(
              "bAdaptiveCorpseStagger=False") != std::string::npos);
    const auto diagnostics_navigation =
        node_center(hwnd, graphical.value().ui_model(), "nav-5");
    CHECK(diagnostics_navigation.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(diagnostics_navigation->x,
                            diagnostics_navigation->y));
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::diagnostics);
    const auto overlay_navigation =
        node_center(hwnd, graphical.value().ui_model(), "nav-2");
    CHECK(overlay_navigation.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(overlay_navigation->x, overlay_navigation->y));
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::overlay);
    const auto overlay_toggle =
        node_center(hwnd, graphical.value().ui_model(), "overlay-toggle");
    CHECK(overlay_toggle.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(overlay_toggle->x, overlay_toggle->y));
    CHECK(!graphical.value().overlay_enabled());
    Sleep(450);
    const auto overlay_toggle_off =
        node_center(hwnd, graphical.value().ui_model(), "overlay-toggle");
    CHECK(overlay_toggle_off.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(overlay_toggle_off->x, overlay_toggle_off->y));
    CHECK(graphical.value().overlay_enabled());
    const auto overlay_position =
        node_center(hwnd, graphical.value().ui_model(), "overlay-position");
    CHECK(overlay_position.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(overlay_position->x, overlay_position->y));
    CHECK(graphical.value().ui_model().status().overlay_position ==
          L"bottom right");
    for (int page = 0; page < 3; ++page) {
        SendMessageW(hwnd, WM_KEYDOWN, VK_NEXT, 0);
    }
    const auto overlay_scale_reset =
        node_center(hwnd, graphical.value().ui_model(), "overlay-scale-reset");
    CHECK(overlay_scale_reset.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(overlay_scale_reset->x, overlay_scale_reset->y));
    CHECK(graphical.value().ui_model().status().overlay_scale_percent == 100);
    const auto overlay_memory =
        node_center(hwnd, graphical.value().ui_model(), "overlay-show-memory");
    CHECK(overlay_memory.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(overlay_memory->x, overlay_memory->y));
    CHECK(!graphical.value().ui_model().status().overlay_show_memory);
    const auto overlay_settings_bytes =
        read_bytes(options.state_root / L"settings.ini");
    CHECK(overlay_settings_bytes.find("overlay_show_memory=false\n") !=
          std::string::npos);
    CHECK(overlay_settings_bytes.find("overlay_position=bottom_right\n") !=
          std::string::npos);
    const auto dashboard_after_overlay =
        node_center(hwnd, graphical.value().ui_model(), "nav-0");
    CHECK(dashboard_after_overlay.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(dashboard_after_overlay->x,
                            dashboard_after_overlay->y));
    Sleep(450);
    SendMessageW(hwnd, WM_HOTKEY, 0x4B46, 0);
    CHECK(!graphical.value().overlay_enabled());
    graphical.value().telemetry_tick_for_testing();
    SendMessageW(hwnd, WM_HOTKEY, 0x4B46, 0);
    CHECK(!graphical.value().overlay_enabled());
    Sleep(450);
    SendMessageW(hwnd, WM_HOTKEY, 0x4B46, 0);
    CHECK(graphical.value().overlay_enabled());
    CHECK(!scroll_to_node(
        hwnd, graphical.value().ui_model(),
        "game-offline-telemetry").has_value());
    const auto home_for_goals =
        node_center(hwnd, graphical.value().ui_model(), "nav-0");
    CHECK(home_for_goals.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(home_for_goals->x, home_for_goals->y));
    CHECK(!scroll_to_node(
        hwnd, graphical.value().ui_model(),
        "settings-adaptive-online").has_value());
    CHECK(graphical.value().ui_model().status().mode ==
          L"Adaptive / Automatic");
    CHECK(read_bytes(options.state_root / L"settings.ini").find(
              "optimizer_mode=adaptive\n") != std::string::npos);
    const auto diagnostics_again =
        node_center(hwnd, graphical.value().ui_model(), "nav-5");
    CHECK(diagnostics_again.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(diagnostics_again->x,
                            diagnostics_again->y));
    const auto diagnostics_backup =
        node_center(hwnd, graphical.value().ui_model(), "diagnostics-backup");
    CHECK(diagnostics_backup.has_value());
    const auto engine_before_failed_backup =
        read_bytes(config_root / L"KFEngine.ini");
    CHECK(fs::remove(config_root / L"KFEngine.ini"));
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(diagnostics_backup->x, diagnostics_backup->y));
    CHECK(graphical.value().ui_model().notice().has_value());
    CHECK(graphical.value().ui_model().notice()->code == L"BACKUP_BLOCKED");
    write_bytes(config_root / L"KFEngine.ini", engine_before_failed_backup);
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(diagnostics_backup->x, diagnostics_backup->y));
    CHECK(graphical.value().ui_model().notice().has_value());
    CHECK(graphical.value().ui_model().notice()->code == L"BACKUP_CREATED");
    const auto full_check =
        node_center(hwnd, graphical.value().ui_model(), "diagnostics-full-check");
    CHECK(full_check.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(full_check->x, full_check->y));
    CHECK(fs::exists(options.state_root / L"full-self-check.json"));
    CHECK(graphical.value().ui_model().notice().has_value());
    CHECK(graphical.value().ui_model().notice()->code == L"FULL_CHECK_FAILED" ||
          graphical.value().ui_model().notice()->code == L"FULL_CHECK_PASSED");
    const auto support_export = node_center(
        hwnd, graphical.value().ui_model(), "diagnostics-export-support");
    CHECK(support_export.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(support_export->x, support_export->y));
    const auto support_report =
        read_bytes(options.state_root / L"private-support-bundle.json");
    CHECK(support_report.find("KF2_OPTIMIZER_SUPPORT_BUNDLE_V1") !=
          std::string::npos);
    CHECK(support_report.find("KF2_OPTIMIZER_DIAGNOSTICS_V2") !=
          std::string::npos);
    CHECK(support_report.find("KF2_ISSUE72_INVENTORY_V3") !=
          std::string::npos);
    CHECK(support_report.find("\"content_included\":false") !=
          std::string::npos);
    SendMessageW(hwnd, WM_KEYDOWN, VK_END, 0);
    SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::diagnostics);

    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    CHECK(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                     CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&automation))));
    Microsoft::WRL::ComPtr<IUIAutomationElement> root_element;
    CHECK(SUCCEEDED(automation->ElementFromHandle(hwnd, &root_element)));
    BSTR name = nullptr;
    CHECK(SUCCEEDED(root_element->get_CurrentName(&name)));
    CHECK(std::wstring_view{name} == L"KF2 Optimizer Next");
    SysFreeString(name);
    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    CHECK(graphical.value().shutdown_cleanly().has_value());
    CHECK(read_bytes(options.state_root / L"session.marker").ends_with(
        "clean_shutdown=true\n"));

    // A transient provider gap must not erase the last confirmed corpse limit
    // or start fallback decisions. Exercise the real runtime, not just a label.
    {
        kf2::diagnostics::EventLog transition_events{128};
        kf2::app::UiRuntime runtime{root / L"Data-transition", false,
            kf2::config::Settings{}, transition_events, options.game_discovery,
            kf2::app::StartMode::read_only, root / L"portable"};
        runtime.game_process = kf2::game::GameProcessIdentity{424242, 9001, {}};
        kf2::telemetry_pipeline::TelemetryFrame frame;
        frame.identity = {424242, 9001};
        frame.active_gameplay = true;
        frame.offline_gameplay = true;
        frame.observed_at_ns = 20'000'000'000ULL;
        replace_frame_gameplay(frame, [&](auto& gameplay) {
            gameplay.map = "KF-Test";
            gameplay.net_mode = "NM_Standalone";
            gameplay.telemetry_sample = 1;
            gameplay.telemetry_corpse_limit = 2000;
            gameplay.telemetry_corpse_total = 20;
            gameplay.telemetry_observed_ns = frame.observed_at_ns;
        });
        runtime.update_adaptive_controller(frame);
        frame.observed_at_ns += 250'000'000ULL;
        runtime.update_adaptive_controller(frame);
        CHECK(runtime.model.status().adaptive_runtime_corpse_limit == 2000);
        frame.observed_at_ns += 250'000'000ULL;
        replace_frame_gameplay(frame, [&](auto& gameplay) {
            gameplay.telemetry_corpse_limit = 1950;
            gameplay.telemetry_observed_ns = frame.observed_at_ns;
        });
        runtime.update_adaptive_controller(frame);
        frame.observed_at_ns += 250'000'000ULL;
        replace_frame_gameplay(frame, [&](auto& gameplay) {
            gameplay.telemetry_corpse_limit = 2000;
            gameplay.telemetry_observed_ns = frame.observed_at_ns;
        });
        runtime.update_adaptive_controller(frame);
        CHECK(runtime.model.status().adaptive_corpse_action_status == L"APPLIED");
        // A partial readback used to pass the general gameplay gate and enter
        // fallback selection. Other telemetry is still fresh during this gap.
        replace_frame_gameplay(frame, [](auto& gameplay) {
            gameplay.telemetry_corpse_limit.reset();
        });
        frame.observed_at_ns += 250'000'000ULL;
        runtime.update_adaptive_controller(frame);
        CHECK(runtime.model.status().adaptive_corpse_capability == L"STALE");
        CHECK(runtime.model.status().adaptive_runtime_corpse_limit == 2000);
        CHECK(runtime.model.status().adaptive_action == L"hold");
        const auto gap_start = frame.observed_at_ns;
        const auto event_count = transition_events.snapshot().size();
        for (int tick = 1; tick <= 39; ++tick) {
            frame.observed_at_ns = gap_start + tick * 250'000'000ULL;
            runtime.update_adaptive_controller(frame);
            CHECK(runtime.model.status().adaptive_corpse_capability == L"STALE");
            CHECK(runtime.model.status().adaptive_runtime_corpse_limit == 2000);
            CHECK(runtime.model.status().adaptive_action == L"hold");
            CHECK(runtime.model.status().adaptive_corpse_action_status == L"NONE");
        }
        CHECK(transition_events.snapshot().size() == event_count);
        frame.observed_at_ns = gap_start + 10'000'000'000ULL;
        runtime.update_adaptive_controller(frame);
        CHECK(runtime.model.status().adaptive_corpse_capability == L"UNAVAILABLE");
        CHECK(!runtime.model.status().adaptive_runtime_corpse_limit);
        CHECK(runtime.model.status().adaptive_corpse_action_status == L"NONE");
        frame.observed_at_ns += 250'000'000ULL;
        replace_frame_gameplay(frame, [&](auto& gameplay) {
            gameplay.telemetry_sample = 2;
            gameplay.telemetry_corpse_limit = 1500;
            gameplay.telemetry_corpse_total = 10;
            gameplay.telemetry_observed_ns = frame.observed_at_ns;
        });
        runtime.update_adaptive_controller(frame);
        CHECK(runtime.model.status().adaptive_corpse_capability == L"AVAILABLE");
        CHECK(runtime.model.status().adaptive_runtime_corpse_limit == 1500);
        runtime.game_process.reset();
    }

    // Direct online travel can recreate the provider while the process and
    // numeric UDP port stay unchanged. The old APPLIED receipt must cease to
    // authorize actions as soon as the new World generation is observed.
    {
        kf2::diagnostics::EventLog generation_events{32};
        kf2::app::UiRuntime runtime{root / L"Data-provider-generation", false,
            kf2::config::Settings{}, generation_events,
            options.game_discovery, kf2::app::StartMode::read_only,
            root / L"portable"};
        runtime.game_process = kf2::game::GameProcessIdentity{424243, 9002, {}};
        runtime.optimizer_settings.adaptive_optimization_enabled = true;
        runtime.adaptive_runtime_mode_process_start_id = 9002;
        runtime.adaptive_runtime_mode_provider_generation = 7;
        runtime.adaptive_runtime_mode_port = std::uint16_t{64298};
        runtime.adaptive_runtime_mode_confirmed = true;

        kf2::telemetry_pipeline::TelemetryFrame frame;
        frame.identity = {424243, 9002};
        frame.observed_at_ns = 30'000'000'000ULL;
        frame.active_gameplay = true;
        replace_frame_gameplay(frame, [](auto& gameplay) {
            gameplay.map = "KF-Test";
            gameplay.net_mode = "NM_Client";
            gameplay.optimizer_online_read_only = true;
            gameplay.optimizer_session_generation = 8;
        });
        // The fresh bridge has not announced its endpoint yet.
        runtime.update_adaptive_controller(frame);
        CHECK(!runtime.adaptive_runtime_mode_confirmed);
        CHECK(runtime.model.status().adaptive_action == L"blocked");
        CHECK(runtime.model.status().adaptive_evidence ==
              L"MODE_READBACK_PENDING");
        runtime.game_process.reset();
    }

    // A live Variable frame rate change invalidates every Adaptive frame
    // statistic exactly once. Unchanged settings and loading-time changes do
    // not repeatedly reset the controller boundary.
    {
        kf2::diagnostics::EventLog rate_mode_events{128};
        kf2::app::UiRuntime runtime{root / L"Data-rate-mode", false,
            kf2::config::Settings{}, rate_mode_events, options.game_discovery,
            kf2::app::StartMode::read_only, root / L"portable"};
        runtime.game_process = kf2::game::GameProcessIdentity{
            515151, 7001, runtime.installation->executable};
        runtime.refresh_game_configuration_for_process_start(false);
        CHECK(runtime.adaptive_variable_frame_rate_enabled == false);
        CHECK(runtime.adaptive_frame_rate_config_write_time.has_value());
        const auto initial_rate_config_write_time =
            *runtime.adaptive_frame_rate_config_write_time;

        runtime.optimizer_settings.target_fps = 144;
        runtime.adaptive_frame_not_before_ns = 1;
        runtime.last_frame_metrics.fps = 60.0;
        kf2::optimizer::QualityResponse::Context response_context{};
        response_context.ready = true;
        kf2::telemetry::PresentSource::Window response_window{};
        response_window.complete = true;
        runtime.quality_response.begin(
            41, "gpu", 100, 90, 20'000'000'000ULL,
            response_context, response_window);
        runtime.quality_response.confirm(41, 20'100'000'000ULL);
        CHECK(runtime.quality_response.end_ns() != 0);
        auto game_config = read_bytes(config_root / L"KFGame.ini");
        const auto capped = game_config.find("bSmoothFrameRate=True");
        CHECK(capped != std::string::npos);
        game_config.replace(capped, std::strlen("bSmoothFrameRate=True"),
                            "bSmoothFrameRate=False");
        write_bytes(config_root / L"KFGame.ini", game_config);
        fs::last_write_time(config_root / L"KFGame.ini",
            initial_rate_config_write_time + std::chrono::seconds{2});
        CHECK(runtime.reset_adaptive_frame_window_for_rate_mode_change(
            30'000'000'000ULL, true));
        CHECK(runtime.adaptive_frame_rate_config_metadata_checks_for_testing ==
              1);
        CHECK(runtime.adaptive_variable_frame_rate_enabled == true);
        CHECK(runtime.adaptive_frame_not_before_ns == 30'000'000'000ULL);
        CHECK(!runtime.last_frame_metrics.fps);
        CHECK(runtime.quality_response.end_ns() == 0);
        CHECK(runtime.optimizer_settings.target_fps == 144);
        for (std::uint64_t tick = 1; tick <= 7; ++tick) {
            CHECK(!runtime.reset_adaptive_frame_window_for_rate_mode_change(
                30'000'000'000ULL + tick * 120'000'000ULL, true));
        }
        CHECK(runtime.adaptive_frame_rate_config_metadata_checks_for_testing ==
              1);

        game_config.replace(
            game_config.find("bSmoothFrameRate=False"),
            std::strlen("bSmoothFrameRate=False"),
            "bSmoothFrameRate=True");
        write_bytes(config_root / L"KFGame.ini", game_config);
        fs::last_write_time(config_root / L"KFGame.ini",
            initial_rate_config_write_time + std::chrono::seconds{4});
        CHECK(!runtime.reset_adaptive_frame_window_for_rate_mode_change(
            30'960'000'000ULL, true));
        CHECK(runtime.adaptive_variable_frame_rate_enabled == true);
        CHECK(runtime.adaptive_frame_rate_config_metadata_checks_for_testing ==
              1);
        CHECK(runtime.reset_adaptive_frame_window_for_rate_mode_change(
            31'000'000'000ULL, true));
        CHECK(runtime.adaptive_frame_rate_config_metadata_checks_for_testing ==
              2);
        CHECK(runtime.adaptive_variable_frame_rate_enabled == false);

        game_config.replace(
            game_config.find("bSmoothFrameRate=True"),
            std::strlen("bSmoothFrameRate=True"),
            "bSmoothFrameRate=False");
        write_bytes(config_root / L"KFGame.ini", game_config);
        fs::last_write_time(config_root / L"KFGame.ini",
            initial_rate_config_write_time + std::chrono::seconds{6});
        CHECK(!runtime.reset_adaptive_frame_window_for_rate_mode_change(
            32'000'000'000ULL, false));
        CHECK(runtime.adaptive_frame_rate_config_metadata_checks_for_testing ==
              3);
        CHECK(runtime.adaptive_variable_frame_rate_enabled == true);
        const auto rate_events = rate_mode_events.snapshot();
        CHECK(std::count_if(rate_events.begin(), rate_events.end(),
            [](const auto& event) {
                return event.code == "ADAPTIVE_FRAME_RATE_MODE_CHANGED";
            }) == 2);
        runtime.game_process.reset();
    }

    // KF2 can save video choices while the app is open. The graphics page
    // follows the game files without discarding a different staged choice.
    {
        const auto graphics_config = root / L"graphics-sync-config";
        fs::create_directories(graphics_config);
        for (const auto* graphics_ini : {L"KFSystemSettings.ini", L"KFGame.ini",
                                 L"KFEngine.ini"}) {
            write_bytes(graphics_config / graphics_ini,
                        read_bytes(config_root / graphics_ini));
        }
        kf2::diagnostics::EventLog graphics_events{128};
        kf2::app::UiRuntime graphics_runtime{
            root / L"Data-graphics-sync", false,
            kf2::config::Settings{}, graphics_events, options.game_discovery,
            kf2::app::StartMode::read_only, root / L"portable"};
        CHECK(graphics_runtime.installation.has_value());
        graphics_runtime.installation->config_root = graphics_config;
        graphics_runtime.reload_video_settings();
        CHECK(graphics_runtime.video_saved.has_value());
        const auto vsync_index = static_cast<std::size_t>(
            kf2::game::VideoOption::vsync);
        const int old_vsync =
            graphics_runtime.video_saved->choices[vsync_index];
        const auto flex_index = static_cast<std::size_t>(
            kf2::game::VideoOption::nvidia_flex);
        const int verified_flex =
            graphics_runtime.video_saved->choices[flex_index];
        graphics_runtime.video_pending->film_grain_percent = 75;
        graphics_runtime.refresh_video_presentation();

        auto system_config = read_bytes(
            graphics_config / L"KFSystemSettings.ini");
        const auto old_choice = old_vsync == 0
            ? std::string{"UseVsync=False"}
            : std::string{"UseVsync=True"};
        const auto new_choice = old_vsync == 0
            ? std::string{"UseVsync=True"}
            : std::string{"UseVsync=False"};
        const auto choice_at = system_config.find(old_choice);
        CHECK(choice_at != std::string::npos);
        system_config.replace(choice_at, old_choice.size(), new_choice);
        const auto system_path = graphics_config / L"KFSystemSettings.ini";
        const auto old_write_time = fs::last_write_time(system_path);
        write_bytes(system_path, system_config);
        fs::last_write_time(system_path, old_write_time +
            std::chrono::seconds{2});
        CHECK(graphics_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::synchronized);
        CHECK(graphics_runtime.video_saved->choices[vsync_index] !=
              old_vsync);
        CHECK(graphics_runtime.video_pending->choices[vsync_index] !=
              old_vsync);
        CHECK(graphics_runtime.video_pending->film_grain_percent == 75);
        CHECK(graphics_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::unchanged);
        const auto engine_path = graphics_config / L"KFEngine.ini";
        const auto engine_bytes = read_bytes(engine_path);
        const auto engine_write_time = fs::last_write_time(engine_path);
        fs::remove(engine_path);
        CHECK(graphics_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::retryable_unstable);
        CHECK(graphics_runtime.video_saved->choices[flex_index] ==
              verified_flex);
        CHECK(graphics_runtime.video_pending->choices[flex_index] ==
              verified_flex);
        write_bytes(engine_path, engine_bytes);
        fs::last_write_time(engine_path, engine_write_time +
            std::chrono::seconds{3});
        CHECK(graphics_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::synchronized);

        // A temporarily unreadable file is treated as an overlapping KF2
        // write. The staged choice remains available for a later retry.
        const auto game_path = graphics_config / L"KFGame.ini";
        const auto game_bytes = read_bytes(game_path);
        const auto game_write_time = fs::last_write_time(game_path);
        fs::remove(game_path);
        fs::create_directory(game_path);
        fs::last_write_time(game_path, game_write_time +
            std::chrono::seconds{4});
        const auto staged_film_grain =
            graphics_runtime.video_pending->film_grain_percent;
        CHECK(graphics_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::retryable_unstable);
        CHECK(graphics_runtime.video_pending->film_grain_percent ==
              staged_film_grain);
        fs::remove(game_path);
        write_bytes(game_path, game_bytes);
        fs::last_write_time(game_path, game_write_time +
            std::chrono::seconds{5});
        CHECK(graphics_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::synchronized);

        // A second write-time change after a successful read blocks the save
        // until a later stable pass can rebase it.
        const auto overlap_time = fs::last_write_time(system_path) +
            std::chrono::seconds{2};
        fs::last_write_time(system_path, overlap_time);
        bool overlapped = false;
        graphics_runtime.video_sync_before_verification_for_testing = [&] {
            if (overlapped) return;
            fs::last_write_time(system_path,
                                overlap_time + std::chrono::seconds{2});
            overlapped = true;
        };
        CHECK(graphics_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::retryable_unstable);
        graphics_runtime.video_sync_before_verification_for_testing = {};
        CHECK(graphics_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::synchronized);

        // An invalid staged resolution makes rebasing fail closed. Neither a
        // direct apply nor the UI save path discards that selection.
        const auto resolution_index = static_cast<std::size_t>(
            kf2::game::VideoOption::resolution);
        const int invalid_resolution = static_cast<int>(
            graphics_runtime.video_pending->resolutions.size()) + 1;
        graphics_runtime.video_pending->choices[resolution_index] =
            invalid_resolution;
        fs::last_write_time(system_path, fs::last_write_time(system_path) +
            std::chrono::seconds{2});
        const auto blocked = graphics_runtime.apply_video_settings();
        CHECK(!blocked.has_value());
        CHECK(blocked.error().code == kf2::ErrorCode::stale_data);
        CHECK(graphics_runtime.video_pending->choices[resolution_index] ==
              invalid_resolution);
        graphics_runtime.save_video_selection();
        CHECK(graphics_runtime.video_pending->choices[resolution_index] ==
              invalid_resolution);
        graphics_runtime.video_pending = graphics_runtime.video_saved;
    }

    // Protected teardown must capture one stable final KF2 graphics
    // generation before restoring the personal INI snapshot. A late native
    // write is replayed, an overlapping write is retried, and an incomplete
    // generation leaves both sources untouched with verified evidence.
    std::vector<std::unique_ptr<kf2::diagnostics::EventLog>>
        final_graphics_events;
    const auto make_final_graphics_runtime = [&](std::wstring_view suffix) {
        const auto final_config =
            root / (std::wstring{L"final-graphics-config-"} +
                    std::wstring{suffix});
        const auto final_state =
            root / (std::wstring{L"Data-final-graphics-"} +
                    std::wstring{suffix});
        fs::create_directories(final_config);
        for (const auto* graphics_ini : {L"KFSystemSettings.ini",
                                         L"KFGame.ini", L"KFEngine.ini"}) {
            write_bytes(final_config / graphics_ini,
                        read_bytes(config_root / graphics_ini));
        }
        auto event_log = std::make_unique<kf2::diagnostics::EventLog>(
            128, final_state / L"logs/session-events.json");
        auto* event_log_pointer = event_log.get();
        final_graphics_events.push_back(std::move(event_log));
        auto runtime = std::make_unique<kf2::app::UiRuntime>(
            final_state, false, kf2::config::Settings{}, *event_log_pointer,
            options.game_discovery, kf2::app::StartMode::normal,
            root / L"portable");
        if (!runtime->installation) {
            return std::unique_ptr<kf2::app::UiRuntime>{};
        }
        runtime->installation->config_root = final_config;
        runtime->reload_video_settings();
        if (!runtime->video_saved) {
            return std::unique_ptr<kf2::app::UiRuntime>{};
        }
        const auto captured =
            kf2::config::capture_session_config(final_config, final_state);
        if (!captured.has_value()) {
            return std::unique_ptr<kf2::app::UiRuntime>{};
        }
        runtime->session_config_snapshot = captured.value();
        runtime->session_video_runtime =
            kf2::game::read_video_settings(final_config).value();
        runtime->session_config_waiting_for_launch = false;
        runtime->model.set_recovery_required(false);
        return runtime;
    };
    const auto write_final_motion_change = [&](kf2::app::UiRuntime& runtime)
        -> std::optional<int> {
        const auto motion_index = static_cast<std::size_t>(
            kf2::game::VideoOption::motion_blur);
        const int previous =
            runtime.session_video_runtime->choices[motion_index];
        const int desired = previous == 0 ? 1 : 0;
        const auto system_path =
            runtime.installation->config_root / L"KFSystemSettings.ini";
        auto bytes = read_bytes(system_path);
        const auto old_setting = previous == 0
            ? std::string_view{"MotionBlur=False"}
            : std::string_view{"MotionBlur=True"};
        const auto new_setting = desired == 0
            ? std::string_view{"MotionBlur=False"}
            : std::string_view{"MotionBlur=True"};
        const auto setting_at = bytes.find(old_setting);
        if (setting_at == std::string::npos) return std::nullopt;
        bytes.replace(setting_at, old_setting.size(), new_setting);
        const auto previous_time = fs::last_write_time(system_path);
        write_bytes(system_path, bytes);
        fs::last_write_time(system_path,
                            previous_time + std::chrono::seconds{2});
        return std::optional{desired};
    };

    {
        auto runtime = make_final_graphics_runtime(L"unchanged");
        CHECK(runtime);
        const auto original = *runtime->video_saved;
        runtime->finalize_ended_game_session();
        CHECK(!runtime->session_config_snapshot.has_value());
        const auto restored = kf2::game::read_video_settings(
            runtime->installation->config_root);
        CHECK(restored.has_value());
        CHECK(restored.value().choices == original.choices);
        CHECK(restored.value().film_grain_percent ==
              original.film_grain_percent);
    }

    {
        auto runtime = make_final_graphics_runtime(L"late-write");
        CHECK(runtime);
        const auto desired = write_final_motion_change(*runtime);
        CHECK(desired.has_value());
        runtime->finalize_ended_game_session();
        CHECK(!runtime->session_config_snapshot.has_value());
        const auto restored = kf2::game::read_video_settings(
            runtime->installation->config_root);
        CHECK(restored.has_value());
        CHECK(restored.value().choices[static_cast<std::size_t>(
                  kf2::game::VideoOption::motion_blur)] == *desired);
    }

    {
        auto runtime = make_final_graphics_runtime(L"overlap");
        CHECK(runtime);
        const auto desired = write_final_motion_change(*runtime);
        CHECK(desired.has_value());
        const auto system_path =
            runtime->installation->config_root / L"KFSystemSettings.ini";
        bool overlapped = false;
        runtime->video_sync_before_verification_for_testing = [&] {
            if (overlapped) return;
            fs::last_write_time(system_path,
                fs::last_write_time(system_path) +
                    std::chrono::seconds{2});
            overlapped = true;
        };
        runtime->finalize_ended_game_session();
        CHECK(overlapped);
        CHECK(!runtime->session_config_snapshot.has_value());
        const auto restored = kf2::game::read_video_settings(
            runtime->installation->config_root);
        CHECK(restored.has_value());
        CHECK(restored.value().choices[static_cast<std::size_t>(
                  kf2::game::VideoOption::motion_blur)] == *desired);
    }

    {
        auto runtime = make_final_graphics_runtime(L"incomplete");
        CHECK(runtime);
        const auto game_path =
            runtime->installation->config_root / L"KFGame.ini";
        const auto complete_game = read_bytes(game_path);
        std::string incomplete{"[Engine.GameInfo]\nBroken="};
        incomplete.push_back('\0');
        incomplete += "partial";
        const auto previous_time = fs::last_write_time(game_path);
        write_bytes(game_path, incomplete);
        fs::last_write_time(game_path,
                            previous_time + std::chrono::seconds{2});
        runtime->finalize_ended_game_session();
        CHECK(runtime->session_config_snapshot.has_value());
        CHECK(runtime->final_graphics_capture_pending);
        CHECK(runtime->model.recovery_required());
        CHECK(read_bytes(game_path) == incomplete);
        CHECK(!runtime->last_backup_id.empty());
        const auto evidence =
            runtime->backups.load_backup(runtime->last_backup_id);
        CHECK(evidence.has_value());
        CHECK(runtime->backups.verify(evidence.value()).has_value());
        CHECK(!runtime->restore_protected_session_config(
            L"must remain blocked while final graphics are unstable"));
        CHECK(read_bytes(game_path) == incomplete);
        CHECK(runtime->model.notice().has_value());
        CHECK(runtime->model.notice()->code ==
              L"FINAL_GRAPHICS_CAPTURE_PENDING");

        write_bytes(game_path, complete_game);
        fs::last_write_time(game_path,
                            previous_time + std::chrono::seconds{4});
        runtime->final_graphics_retry_after_ns = 0;
        runtime->try_attach_telemetry();
        CHECK(!runtime->final_graphics_capture_pending);
        CHECK(!runtime->session_config_snapshot.has_value());
    }

    // Releasing a graphics slider and selecting Reset both save directly to
    // KF2's INIs; neither workflow needs a separate Apply action.
    {
        const auto direct_config = root / L"graphics-direct-config";
        fs::create_directories(direct_config);
        for (const auto* graphics_ini : {L"KFSystemSettings.ini", L"KFGame.ini",
                                          L"KFEngine.ini"}) {
            write_bytes(direct_config / graphics_ini,
                        read_bytes(config_root / graphics_ini));
        }
        const auto direct_state = root / L"Data-graphics-direct";
        fs::create_directories(direct_state);
        kf2::diagnostics::EventLog direct_events{128};
        kf2::app::UiRuntime direct_runtime{
            direct_state, false,
            kf2::config::Settings{}, direct_events, options.game_discovery,
            kf2::app::StartMode::normal, root / L"portable"};
        CHECK(direct_runtime.installation.has_value());
        direct_runtime.installation->config_root = direct_config;
        auto vsync_ini = read_bytes(direct_config / L"KFSystemSettings.ini");
        const auto vsync_false = vsync_ini.find("UseVsync=False");
        CHECK(vsync_false != std::string::npos);
        vsync_ini.replace(vsync_false, std::strlen("UseVsync=False"),
                          "UseVsync=True");
        write_bytes(direct_config / L"KFSystemSettings.ini", vsync_ini);
        auto variable_ini = read_bytes(direct_config / L"KFGame.ini");
        const auto smoothing_on = variable_ini.find("bSmoothFrameRate=True");
        CHECK(smoothing_on != std::string::npos);
        variable_ini.replace(smoothing_on, std::strlen("bSmoothFrameRate=True"),
                             "bSmoothFrameRate=False");
        write_bytes(direct_config / L"KFGame.ini", variable_ini);
        direct_runtime.reload_video_settings();
        CHECK(direct_runtime.video_saved.has_value());
        const auto vsync_choice = static_cast<std::size_t>(
            kf2::game::VideoOption::vsync);
        CHECK(direct_runtime.video_saved->choices[vsync_choice] == 1);
        const auto variable_choice = static_cast<std::size_t>(
            kf2::game::VideoOption::variable_frame_rate);
        CHECK(direct_runtime.video_saved->choices[variable_choice] == 1);
        direct_runtime.video_pending->choices[vsync_choice] = 0;
        direct_runtime.video_pending->choices[variable_choice] = 0;
        direct_runtime.save_video_selection();
        CHECK(direct_runtime.model.notice().has_value());
        CHECK(direct_runtime.model.notice()->code == L"GRAPHICS_SAVED");
        const auto saved_vsync = kf2::game::read_video_settings(direct_config);
        CHECK(saved_vsync.has_value());
        CHECK(saved_vsync.value().choices[vsync_choice] == 0);
        CHECK(saved_vsync.value().choices[variable_choice] == 0);
        const auto original_display = kf2::game::video_choice_label(
            kf2::game::VideoOption::display, *direct_runtime.video_saved);
        const auto original_resolution = kf2::game::video_choice_label(
            kf2::game::VideoOption::resolution, *direct_runtime.video_saved);
        direct_runtime.set_slider_value("graphics-film-grain-slider", 75);
        CHECK(direct_runtime.video_saved->film_grain_percent == 75);
        const auto saved_grain = kf2::game::read_video_settings(direct_config);
        CHECK(saved_grain.has_value());
        CHECK(saved_grain.value().film_grain_percent == 75);
        direct_runtime.reset_video_settings();
        CHECK(direct_runtime.video_saved->film_grain_percent == 0);
        const auto reset_graphics = kf2::game::read_video_settings(direct_config);
        CHECK(reset_graphics.has_value());
        CHECK(reset_graphics.value().film_grain_percent == 0);
        CHECK(kf2::game::video_choice_label(
                  kf2::game::VideoOption::display, reset_graphics.value()) ==
              original_display);
        CHECK(kf2::game::video_choice_label(
                  kf2::game::VideoOption::resolution, reset_graphics.value()) ==
              original_resolution);
        CHECK(reset_graphics.value().flex_level == 0);
    }

    // A failed portable settings write must leave the Overlay switch and
    // runtime state at their previously saved value.
    {
        kf2::diagnostics::EventLog overlay_events{128};
        kf2::config::Settings overlay_start;
        overlay_start.overlay_scale_percent = 125;
        kf2::app::UiRuntime overlay_runtime{
            root / L"Data-overlay-save", false,
            overlay_start, overlay_events, options.game_discovery,
            kf2::app::StartMode::normal, root / L"portable"};
        CHECK(overlay_runtime.overlay_enabled);
        overlay_runtime.settings_path =
            root / L"missing-overlay-parent" / L"settings.ini";
        const auto failed = overlay_runtime.set_overlay(false);
        CHECK(!failed.has_value());
        CHECK(overlay_runtime.overlay_enabled);
        CHECK(overlay_runtime.optimizer_settings.overlay_enabled);
        CHECK(overlay_runtime.model.status().overlay_enabled);
        const auto previous_corner = overlay_runtime.overlay_corner;
        const auto previous_position =
            overlay_runtime.optimizer_settings.overlay_position;
        overlay_runtime.execute_action("overlay-position");
        CHECK(overlay_runtime.overlay_corner == previous_corner);
        CHECK(overlay_runtime.optimizer_settings.overlay_position ==
              previous_position);
        overlay_runtime.execute_action("overlay-scale-reset");
        CHECK(overlay_runtime.optimizer_settings.overlay_scale_percent == 125);
        CHECK(overlay_runtime.overlay_scale == 1.25F);
    }

    // The rightmost corpse slider value must survive the real portable
    // settings write and a fresh parse, not just the visual preview.
    {
        kf2::diagnostics::EventLog slider_events{128};
        kf2::app::UiRuntime slider_runtime{
            root / L"Data-corpse-slider", false,
            kf2::config::Settings{}, slider_events, options.game_discovery,
            kf2::app::StartMode::normal, root / L"portable"};
        slider_runtime.set_slider_value("settings-corpses-slider", 2000);
        CHECK(slider_runtime.model.status().corpse_limit == 2000);
        const auto stored = kf2::config::parse_settings(
            read_bytes(slider_runtime.settings_path));
        CHECK(stored.has_value());
        CHECK(stored.value().corpse_limit == 2000);
    }

    // Changing the saved target after KF2 has started must not retarget the
    // current Adaptive session. KF2's native cap is launch-bound, so grading
    // the running 60 FPS process against the newly saved 119 FPS target would
    // cause a false deficit and unnecessary quality reductions.
    {
        kf2::diagnostics::EventLog target_events{128};
        kf2::config::Settings initial;
        initial.target_fps = 60;
        kf2::app::UiRuntime target_runtime{
            root / L"Data-target-staged-for-restart", false,
            initial, target_events, options.game_discovery,
            kf2::app::StartMode::normal, root / L"portable"};
        CHECK(target_runtime.installation.has_value());
        wchar_t current_executable[MAX_PATH + 1]{};
        const DWORD current_executable_length = GetModuleFileNameW(
            nullptr, current_executable, MAX_PATH);
        CHECK(current_executable_length > 0);
        CHECK(current_executable_length < MAX_PATH);
        target_runtime.installation->executable = std::wstring{
            current_executable, current_executable_length};
        target_runtime.adaptive_session_policy =
            kf2::game::OfflineAdaptiveSessionPolicy{20, 60, 2};
        auto status = target_runtime.model.status();
        status.active_target_fps = 60;
        target_runtime.model.set_status(std::move(status));

        target_runtime.set_slider_value("settings-target-slider", 119);

        CHECK(target_runtime.optimizer_settings.target_fps == 119);
        CHECK(target_runtime.adaptive_session_policy->target_fps == 60);
        CHECK(target_runtime.effective_target_fps() == 60);
        CHECK(target_runtime.model.status().target_fps == 119);
        CHECK(target_runtime.model.status().active_target_fps == 60);
        const auto stored = kf2::config::parse_settings(
            read_bytes(target_runtime.settings_path));
        CHECK(stored.has_value());
        CHECK(stored.value().target_fps == 119);
    }

    // A real filesystem failure must roll both Home sliders back to their
    // authoritative saved values instead of leaving a misleading preview.
    {
        kf2::diagnostics::EventLog slider_events{128};
        kf2::config::Settings initial;
        initial.target_fps = 90;
        initial.corpse_limit = 40;
        kf2::app::UiRuntime slider_runtime{
            root / L"Data-slider-save-failure", false,
            initial, slider_events, options.game_discovery,
            kf2::app::StartMode::normal, root / L"portable"};
        slider_runtime.settings_path =
            root / L"missing-slider-parent" / L"settings.ini";

        slider_runtime.set_slider_value("settings-target-slider", 144);
        CHECK(slider_runtime.optimizer_settings.target_fps == 90);
        CHECK(slider_runtime.model.status().target_fps == 90);

        slider_runtime.set_slider_value("settings-corpses-slider", 2000);
        CHECK(slider_runtime.optimizer_settings.corpse_limit == 40);
        CHECK(slider_runtime.model.status().corpse_limit == 40);
    }

    // Turning Adaptive off is fail-closed even while KF2 is between gameplay
    // worlds and no protected control listener is reachable. The preference
    // must change immediately, local actions must stop, and exact runtime
    // confirmation remains pending for the next provider. Enabling records
    // the user's preference too, but remains fail-closed until a live
    // authenticated receipt arrives.
    {
        const auto deferred_state = root / L"Data-adaptive-disable-deferred";
        kf2::diagnostics::EventLog deferred_events{
            128, deferred_state / L"logs/session-events.json"};
        kf2::config::Settings deferred_settings{};
        deferred_settings.adaptive_optimization_enabled = true;
        kf2::app::UiRuntime deferred_runtime{
            deferred_state, false, deferred_settings, deferred_events,
            options.game_discovery, kf2::app::StartMode::normal,
            root / L"portable"};
        CHECK(deferred_runtime.installation.has_value());
        wchar_t current_executable[MAX_PATH + 1]{};
        const DWORD current_executable_length = GetModuleFileNameW(
            nullptr, current_executable, MAX_PATH);
        CHECK(current_executable_length > 0);
        CHECK(current_executable_length < MAX_PATH);
        deferred_runtime.installation->executable = std::wstring{
            current_executable, current_executable_length};
        deferred_runtime.adaptive_control_token.clear();

        deferred_runtime.toggle_adaptive_optimization();

        CHECK(!deferred_runtime.optimizer_settings
                   .adaptive_optimization_enabled);
        CHECK(!deferred_runtime.model.status()
                   .adaptive_optimization_enabled);
        CHECK(deferred_runtime.model.status().adaptive_state == L"off");
        CHECK(!deferred_runtime.adaptive_runtime_mode_confirmed);
        CHECK(deferred_runtime.adaptive_runtime_mode_pending.has_value());
        CHECK(!*deferred_runtime.adaptive_runtime_mode_pending);
        CHECK(read_bytes(deferred_runtime.settings_path).find(
                  "adaptive_optimization_enabled=false") !=
              std::string::npos);
        CHECK(deferred_runtime.model.notice().has_value());
        CHECK(deferred_runtime.model.notice()->code ==
              L"ADAPTIVE_DISABLE_CONFIRMATION_PENDING");

        deferred_runtime.toggle_adaptive_optimization();

        CHECK(deferred_runtime.optimizer_settings
                   .adaptive_optimization_enabled);
        CHECK(deferred_runtime.model.status()
                   .adaptive_optimization_enabled);
        CHECK(!deferred_runtime.adaptive_runtime_mode_confirmed);
        CHECK(deferred_runtime.adaptive_runtime_mode_pending.has_value());
        CHECK(*deferred_runtime.adaptive_runtime_mode_pending);
        CHECK(deferred_runtime.model.notice().has_value());
        CHECK(deferred_runtime.model.notice()->code ==
              L"ADAPTIVE_ENABLE_CONFIRMATION_PENDING");
        CHECK(read_bytes(deferred_runtime.settings_path).find(
                  "adaptive_optimization_enabled=true") !=
              std::string::npos);
    }

    // A single optimizer process can supervise multiple KF2 launches. After
    // one protected session is restored, the next Steam/shortcut launch must
    // receive the provider bootstrap and fixed session policy again.
    {
        const auto rearm_state = root / L"Data-rearm";
        kf2::diagnostics::EventLog rearm_events{
            128, rearm_state / L"logs/session-events.json"};
        kf2::config::Settings rearm_settings{};
        kf2::app::UiRuntime rearm_runtime{
            rearm_state, false, rearm_settings, rearm_events,
            options.game_discovery, kf2::app::StartMode::normal,
            root / L"portable"};
        rearm_runtime.reload_video_settings();
        CHECK(rearm_runtime.video_saved.has_value());
        const auto personal_graphics = *rearm_runtime.video_saved;

        const auto first_prepare =
            rearm_runtime.prepare_automatic_external_launch_profile();
        CHECK(first_prepare.has_value());
        CHECK(first_prepare.value());
        const auto staged_native_graphics =
            kf2::game::read_video_settings(config_root);
        CHECK(staged_native_graphics.has_value());
        const auto display_index = static_cast<std::size_t>(
            kf2::game::VideoOption::display);
        CHECK(staged_native_graphics.value().choices[display_index] == 1);
        for (std::size_t index = 0;
             index < staged_native_graphics.value().choices.size(); ++index) {
            if (index == display_index) continue;
            CHECK(staged_native_graphics.value().choices[index] ==
                  personal_graphics.choices[index]);
        }
        CHECK(staged_native_graphics.value().film_grain_percent ==
              personal_graphics.film_grain_percent);
        CHECK(rearm_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::unchanged);
        CHECK(rearm_runtime.video_saved->choices == personal_graphics.choices);
        CHECK(rearm_runtime.video_saved->film_grain_percent ==
              personal_graphics.film_grain_percent);
        CHECK(fs::exists(published_telemetry));
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "LocalOptions=?Mutator=KF2OptimizerTelemetry."
                  "KF2OptimizerTelemetryMutator") != std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "GameViewportClientClassName=KF2OptimizerTelemetry."
                  "KF2OptimizerGraphicsViewport") !=
              std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "Paths=" + published_runtime_path) != std::string::npos);
        rearm_runtime.video_saved.reset();
        rearm_runtime.video_pending.reset();
        rearm_runtime.reload_video_settings();
        CHECK(rearm_runtime.video_saved->choices == personal_graphics.choices);
        rearm_runtime.refresh_game_configuration_for_process_start(false);
        CHECK(rearm_runtime.video_saved->choices == personal_graphics.choices);
        CHECK(rearm_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::unchanged);
        const auto motion_index = static_cast<std::size_t>(
            kf2::game::VideoOption::motion_blur);
        const int native_motion =
            rearm_runtime.session_video_runtime->choices[motion_index] == 0
                ? 1 : 0;

        rearm_runtime.game_log_new_settings_restart_requested = true;
        rearm_runtime.adaptive_session_policy =
            kf2::game::OfflineAdaptiveSessionPolicy{20, 60, 2};
        auto bound_status = rearm_runtime.model.status();
        bound_status.active_target_fps = 60;
        bound_status.active_corpse_limit = 20;
        rearm_runtime.model.set_status(std::move(bound_status));
        const auto restart_wait_started = rearm_runtime.monotonic_ns();
        rearm_runtime.begin_game_restart_handoff({
            4242, 123456, rearm_runtime.installation->executable});
        CHECK(rearm_runtime.game_restart_handoff_previous_process.has_value());
        CHECK(rearm_runtime.game_restart_handoff_new_settings);
        CHECK(rearm_runtime.game_restart_handoff_deadline_ns >=
              restart_wait_started +
                  kf2::telemetry_pipeline::kNewSettingsRestartHandoffNs);
        CHECK(rearm_runtime.session_config_snapshot.has_value());
        CHECK(!rearm_runtime.model.status().active_target_fps.has_value());
        CHECK(!rearm_runtime.model.status().active_corpse_limit.has_value());
        CHECK(fs::exists(published_telemetry));
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "LocalOptions=?Mutator=KF2OptimizerTelemetry."
                  "KF2OptimizerTelemetryMutator") != std::string::npos);
        const auto restart_wait_events = rearm_events.snapshot();
        CHECK(std::any_of(restart_wait_events.begin(),
                          restart_wait_events.end(),
            [](const auto& event) {
                return event.code == "KF2_SESSION_RESTART_WAIT";
            }));

        // A Steam bootstrap/restart gap has no active KF2 process. Home slider
        // clicks made here must update the imminent replacement policy and
        // must not be presented as values for a later start.
        rearm_runtime.set_slider_value("settings-target-slider", 119);
        rearm_runtime.set_slider_value("settings-corpses-slider", 1242);
        CHECK(rearm_runtime.model.status().target_fps == 119);
        CHECK(rearm_runtime.model.status().corpse_limit == 1242);
        CHECK(!rearm_runtime.model.status().active_target_fps.has_value());
        CHECK(!rearm_runtime.model.status().active_corpse_limit.has_value());
        const auto pending_policy =
            kf2::game::read_offline_adaptive_session_policy(config_root);
        CHECK(pending_policy.has_value());
        CHECK(pending_policy.value().has_value());
        CHECK(pending_policy.value()->target_fps == 119);
        CHECK(pending_policy.value()->corpse_maximum == 1242);
        const auto pending_policy_events = rearm_events.snapshot();
        CHECK(std::any_of(pending_policy_events.begin(),
                          pending_policy_events.end(),
            [](const auto& event) {
                return event.code == "ADAPTIVE_PENDING_POLICY_UPDATED";
            }));

        auto restarted_engine = read_bytes(config_root / L"KFEngine.ini");
        const auto flex_setting = restarted_engine.find("PhysXLevel=0");
        CHECK(flex_setting != std::string::npos);
        restarted_engine.replace(
            flex_setting, std::string_view{"PhysXLevel=0"}.size(),
            "PhysXLevel=2");
        write_bytes(config_root / L"KFEngine.ini", restarted_engine);
        auto restarted_system = read_bytes(config_root / L"KFSystemSettings.ini");
        const auto old_motion = native_motion == 0
            ? std::string_view{"MotionBlur=True"}
            : std::string_view{"MotionBlur=False"};
        const auto motion_setting = restarted_system.find(old_motion);
        CHECK(motion_setting != std::string::npos);
        restarted_system.replace(motion_setting, old_motion.size(),
            native_motion == 0 ? "MotionBlur=False" : "MotionBlur=True");
        write_bytes(config_root / L"KFSystemSettings.ini", restarted_system);
        CHECK(rearm_runtime.synchronize_video_settings_from_game() ==
              kf2::app::VideoSyncDisposition::synchronized);
        CHECK(rearm_runtime.video_saved->choices == personal_graphics.choices);
        rearm_runtime.refresh_game_configuration_for_process_start(true);
        CHECK(rearm_runtime.video_saved->choices == personal_graphics.choices);
        const auto refreshed_events = rearm_events.snapshot();
        CHECK(std::any_of(refreshed_events.begin(), refreshed_events.end(),
            [](const auto& event) {
                return event.code ==
                           "KF2_NEW_SETTINGS_CONFIGURATION_DETECTED" &&
                       event.message.find(L"configured NVIDIA FleX: Gibs and fluids") !=
                           std::wstring::npos;
            }));

        CHECK(rearm_runtime.restore_protected_session_config(
            L"First simulated KF2 session ended"));
        CHECK(!rearm_runtime.game_restart_handoff_previous_process.has_value());
        CHECK(rearm_runtime.game_restart_handoff_deadline_ns == 0);
        CHECK(!rearm_runtime.game_restart_handoff_new_settings);
        CHECK(!fs::exists(published_telemetry));
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "PhysXLevel=2") != std::string::npos);
        const auto native_graphics = kf2::game::read_video_settings(config_root);
        CHECK(native_graphics.has_value());
        CHECK(native_graphics.value().choices[motion_index] == native_motion);
        CHECK(native_graphics.value().choices[static_cast<std::size_t>(
                  kf2::game::VideoOption::shadow_quality)] ==
              personal_graphics.choices[static_cast<std::size_t>(
                  kf2::game::VideoOption::shadow_quality)]);
        // This fixture has no NVIDIA FleX runtime. Reset its simulated native
        // selection before testing the unrelated external-launch rearm path.
        write_bytes(config_root / L"KFEngine.ini", original_engine_config);
        rearm_runtime.reload_video_settings();

        const auto rearmed =
            rearm_runtime.rearm_automatic_external_launch_profile();
        CHECK(rearmed.has_value());
        CHECK(rearmed.value());
        CHECK(rearm_runtime.session_config_snapshot.has_value());
        CHECK(rearm_runtime.session_config_waiting_for_launch);
        CHECK(fs::exists(published_telemetry));
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "LocalOptions=?Mutator=KF2OptimizerTelemetry."
                  "KF2OptimizerTelemetryMutator") != std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "GameViewportClientClassName=KF2OptimizerTelemetry."
                  "KF2OptimizerGraphicsViewport") !=
              std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "Paths=" + published_runtime_path) != std::string::npos);
        const auto rearm_log = rearm_events.snapshot();
        CHECK(std::any_of(rearm_log.begin(), rearm_log.end(),
            [](const auto& event) {
                return event.code == "ADAPTIVE_EXTERNAL_LAUNCH_PREPARED";
            }));
        CHECK(std::none_of(rearm_log.begin(), rearm_log.end(),
            [](const auto& event) {
                return event.code == "ADAPTIVE_EXTERNAL_LAUNCH_READY" ||
                       event.code == "GAMEPLAY_LOG_LAB_READY";
            }));
        CHECK(std::any_of(rearm_log.begin(), rearm_log.end(),
            [](const auto& event) {
                return event.code == "ADAPTIVE_EXTERNAL_LAUNCH_REARMED";
            }));

        // Applying an explicit graphics change after automatic preparation
        // must rebuild the protected snapshot and launch capabilities. This
        // is the orchestration path used when the dedicated FleX control is
        // changed before KF2 starts.
        const auto effect_index = static_cast<std::size_t>(
            kf2::game::VideoOption::motion_blur);
        const int original_effect =
            rearm_runtime.video_pending->choices[effect_index];
        rearm_runtime.cycle_video_option(kf2::game::VideoOption::motion_blur);
        CHECK(rearm_runtime.session_config_snapshot.has_value());
        CHECK(rearm_runtime.session_config_waiting_for_launch);
        CHECK(rearm_runtime.video_saved->choices[effect_index] !=
              original_effect);
        // Rebuilding the protected launch must retain the explicit user
        // choice in both the saved model and the live KF2 configuration.
        const auto graphics_rebuild_log = rearm_events.snapshot();
        CHECK(std::any_of(
            graphics_rebuild_log.begin(), graphics_rebuild_log.end(),
            [](const auto& event) {
                return event.code ==
                       "GRAPHICS_PROTECTED_LAUNCH_REBUILT";
            }));
    }
    CHECK(!fs::exists(published_telemetry));
    CHECK(read_bytes(config_root / L"KFEngine.ini") == original_engine_config);

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
