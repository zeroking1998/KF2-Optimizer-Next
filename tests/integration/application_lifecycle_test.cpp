#include <Windows.h>
#include <ole2.h>
#include <UIAutomationCore.h>
#include <UIAutomationClient.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "kf2/app/application.hpp"
#include "kf2/config/setting_catalog.hpp"
#include "kf2/ui/shell_layout.hpp"
#include "app/runtime/feature_composition.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << bytes;
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

int main() {
    CHECK(kf2::app::should_prepare_protected_gameplay_provider(
        kf2::app::StartMode::normal));
    CHECK(!kf2::app::should_prepare_protected_gameplay_provider(
        kf2::app::StartMode::safe));
    CHECK(!kf2::app::should_prepare_protected_gameplay_provider(
        kf2::app::StartMode::read_only));
    namespace fs = std::filesystem;
    CHECK(kf2::app::runtime::feature_definitions().size() == 7);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::navigation) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::overlay) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::diagnostics) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::backup) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::optimizer) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::settings) != nullptr);
    CHECK(kf2::app::runtime::find_feature(
              kf2::app::runtime::FeatureId::game) != nullptr);
    CHECK(kf2::app::runtime::valid_feature_registry(
        kf2::app::runtime::feature_definitions()));
    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    const auto documents = root / L"Documents";
    const auto config_root = documents / L"My Games/KillingFloor2/KFGame/Config";
    const auto install_root = root / L"Steam/steamapps/common/KillingFloor2";
    write_test_pe(install_root / L"Binaries/Win64/KFGame.exe");
    CHECK(write_complete_config_catalog(config_root));
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

    {
        auto first = kf2::app::Application::start(options);
        CHECK(first.has_value());
        CHECK(fs::exists(options.state_root / L"settings.ini"));
        CHECK(fs::exists(options.state_root / L"session.marker"));
        CHECK(fs::is_directory(options.state_root / L"logs"));
        CHECK(fs::exists(options.state_root / L"logs/session-events.json"));
        CHECK(read_bytes(options.state_root / L"settings.ini").starts_with(
            "schema_version=1\n"));
        CHECK(first.value().game_installation().has_value());
        CHECK(first.value().ui_model().status().config ==
              kf2::ui::ConfigWorkflowState::detected);
        kf2::optimizer::OptimizerInput optimizer_input;
        optimizer_input.target_fps = 90;
        optimizer_input.profile_preview_requested = true;
        const auto optimized = first.value().prepare_optimizer(optimizer_input);
        CHECK(optimized.has_value());
        CHECK(optimized.value().decision.changes.size() == 2);
        CHECK(read_bytes(config_root / L"KFGame.ini").find(
            "MaxSmoothedFrameRate=62.000000") != std::string::npos);
        const auto current = first.value().prepare_current_optimizer();
        CHECK(current.has_value());
        CHECK(current.value().decision.changes.size() == 2);
        CHECK(current.value().decision.bottleneck ==
              kf2::optimizer::Bottleneck::unavailable);
        const auto preview = first.value().prepare_config_changes({
            {kf2::config::SettingId::target_fps, 90,
             kf2::config::ChangeSource::explicit_user, L"integration"}});
        CHECK(preview.has_value());
        CHECK(first.value().ui_model().status().config ==
              kf2::ui::ConfigWorkflowState::preview_ready);
        const auto applied = first.value().apply_prepared_config(
            {.game_running = false});
        CHECK(applied.has_value());
        CHECK(read_bytes(config_root / L"KFGame.ini").find(
            "MaxSmoothedFrameRate=90") != std::string::npos);
        const auto restored = first.value().restore_config(
            applied.value().backup.id, {.game_running = false});
        CHECK(restored.has_value());
        CHECK(read_bytes(config_root / L"KFGame.ini").find(
            "MaxSmoothedFrameRate=62.000000") != std::string::npos);

        auto duplicate = kf2::app::Application::start(options);
        CHECK(!duplicate.has_value());
        CHECK(duplicate.error().code == kf2::ErrorCode::already_running);
        CHECK(first.value().shutdown_cleanly().has_value());
    }
    CHECK(read_bytes(options.state_root / L"session.marker").ends_with(
        "clean_shutdown=true\n"));

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
        "animations_enabled=true\n"
        "automatic_update_checks=true\n"
        "overlay_enabled=false\noverlay_show_fps=true\noverlay_show_frame_time=true\n"
        "overlay_show_cpu=true\noverlay_show_gpu=true\noverlay_show_memory=false\n"
        "restore_config_after_game=true\noffline_gameplay_telemetry=false\n"
        "adaptive_aggressiveness=balanced\n"
        "adaptive_minimum_quality=70\nadaptive_maximum_quality=100\n"
        "adaptive_quality_change_budget=2\nadaptive_headroom_percent=8\n"
        "adaptive_emergency_enabled=true\n"
        "adaptive_quality_recovery_enabled=true\n"
        "adaptive_manual_locks_enabled=true\n"
        "adaptive_shadow_mode=true\n"
        "adaptive_calibration_enabled=true\nadaptive_logging=true\n"
        "overlay_position=top_right\n"
        "overlay_scale_percent=100\n"
        "target_fps=60\ncorpse_limit=20\n"
        "quality_policy=exact\n"
              "optimizer_profile=balanced\n");
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
            "MaxSmoothedFrameRate=62.000000") != std::string::npos);
        const auto overlay_blocked =
            read_only.value().set_overlay_enabled(true);
        CHECK(!overlay_blocked.has_value());
        CHECK(overlay_blocked.error().code == kf2::ErrorCode::access_denied);
        CHECK(read_bytes(options.state_root / L"settings.ini") ==
              settings_before_read_only);
        CHECK(read_only.value().shutdown_cleanly().has_value());
    }
    options.mode = kf2::app::StartMode::safe;
    options.identity.process_start_id = 10028;
    {
        auto safe = kf2::app::Application::start(options);
        CHECK(safe.has_value());
        CHECK(safe.value().ui_model().status().mode == L"Safe mode");
        CHECK(!safe.value().overlay_enabled());
        CHECK(!safe.value().set_overlay_enabled(true).has_value());
        CHECK(safe.value().shutdown_cleanly().has_value());
    }
    options.mode = kf2::app::StartMode::normal;

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
    CHECK(!graphical.value().overlay_enabled());
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::dashboard);
    const auto dashboard_diagnostics =
        node_center(hwnd, graphical.value().ui_model(), "dashboard-diagnostics");
    CHECK(dashboard_diagnostics.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(dashboard_diagnostics->x,
                            dashboard_diagnostics->y));
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::diagnostics);
    const auto settings_navigation =
        node_center(hwnd, graphical.value().ui_model(), "nav-1");
    CHECK(settings_navigation.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(settings_navigation->x, settings_navigation->y));
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::settings);
    const auto dashboard_again =
        node_center(hwnd, graphical.value().ui_model(), "nav-0");
    CHECK(dashboard_again.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(dashboard_again->x, dashboard_again->y));
    const auto dashboard_overlay =
        node_center(hwnd, graphical.value().ui_model(), "dashboard-overlay");
    CHECK(dashboard_overlay.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(dashboard_overlay->x, dashboard_overlay->y));
    CHECK(graphical.value().ui_model().selected() ==
          kf2::ui::Destination::overlay);
    const auto overlay_toggle =
        node_center(hwnd, graphical.value().ui_model(), "overlay-toggle");
    CHECK(overlay_toggle.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(overlay_toggle->x, overlay_toggle->y));
    CHECK(graphical.value().overlay_enabled());
    Sleep(450);
    const auto overlay_toggle_off =
        node_center(hwnd, graphical.value().ui_model(), "overlay-toggle");
    CHECK(overlay_toggle_off.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(overlay_toggle_off->x, overlay_toggle_off->y));
    CHECK(!graphical.value().overlay_enabled());
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
    CHECK(graphical.value().ui_model().status().overlay_show_memory);
    const auto overlay_settings_bytes =
        read_bytes(options.state_root / L"settings.ini");
    CHECK(overlay_settings_bytes.find("overlay_show_memory=true\n") !=
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
    CHECK(graphical.value().overlay_enabled());
    graphical.value().telemetry_tick_for_testing();
    SendMessageW(hwnd, WM_HOTKEY, 0x4B46, 0);
    CHECK(graphical.value().overlay_enabled());
    Sleep(450);
    SendMessageW(hwnd, WM_HOTKEY, 0x4B46, 0);
    CHECK(!graphical.value().overlay_enabled());
    CHECK(!scroll_to_node(
        hwnd, graphical.value().ui_model(),
        "game-offline-telemetry").has_value());
    const auto settings_for_manual =
        node_center(hwnd, graphical.value().ui_model(), "nav-1");
    CHECK(settings_for_manual.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(settings_for_manual->x, settings_for_manual->y));
    CHECK(!scroll_to_node(
        hwnd, graphical.value().ui_model(),
        "settings-adaptive-online").has_value());

    const auto settings_for_adaptive =
        node_center(hwnd, graphical.value().ui_model(), "nav-1");
    CHECK(settings_for_adaptive.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(settings_for_adaptive->x,
                            settings_for_adaptive->y));
    CHECK(graphical.value().ui_model().status().mode ==
          L"Adaptive / Automatic");
    CHECK(read_bytes(options.state_root / L"settings.ini").find(
              "optimizer_mode=adaptive\n") != std::string::npos);
    const auto diagnostics_navigation =
        node_center(hwnd, graphical.value().ui_model(), "nav-3");
    CHECK(diagnostics_navigation.has_value());
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(diagnostics_navigation->x,
                            diagnostics_navigation->y));
    const auto diagnostics_backup =
        node_center(hwnd, graphical.value().ui_model(), "diagnostics-backup");
    CHECK(diagnostics_backup.has_value());
    const auto engine_before_failed_backup =
        read_bytes(config_root / L"KFEngine.ini");
    const auto config_before_failed_backup =
        graphical.value().ui_model().status().config;
    CHECK(fs::remove(config_root / L"KFEngine.ini"));
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 MAKELPARAM(diagnostics_backup->x, diagnostics_backup->y));
    CHECK(graphical.value().ui_model().notice().has_value());
    CHECK(graphical.value().ui_model().notice()->code == L"BACKUP_BLOCKED");
    CHECK(graphical.value().ui_model().status().config ==
          config_before_failed_backup);
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

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
