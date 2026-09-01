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
#include "app/application_runtime.hpp"
#include "app/runtime/feature_composition.hpp"
#include "features/telemetry/telemetry_session_stage.hpp"

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

int main() {
    CHECK(kf2::app::should_prepare_protected_gameplay_provider(
        kf2::app::StartMode::normal));
    CHECK(!kf2::app::should_prepare_protected_gameplay_provider(
        kf2::app::StartMode::read_only));
    CHECK(!kf2::app::should_prepare_adaptive_flex_runtime(
        kf2::app::StartMode::normal, 0));
    CHECK(kf2::app::should_prepare_adaptive_flex_runtime(
        kf2::app::StartMode::normal, 1));
    CHECK(kf2::app::should_prepare_adaptive_flex_runtime(
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
    kf2::app::enforce_one_frame_thread_lag(flex_preservation_changes);
    CHECK(flex_preservation_changes.size() == 5);
    CHECK(flex_preservation_changes[4].id ==
          kf2::config::SettingId::one_frame_thread_lag);
    CHECK(std::get<bool>(flex_preservation_changes[4].value));
    const kf2::optimizer::StartupMemoryProfile startup_memory{
        .texture_pool_size_mb = 6000,
        .memory_margin_mb = 128,
        .streaming_hysteresis_limit = 40};
    kf2::app::enforce_startup_memory_profile(
        flex_preservation_changes, startup_memory);
    CHECK(flex_preservation_changes.size() == 8);
    CHECK(std::get<int>(flex_preservation_changes[5].value) == 6000);
    CHECK(std::get<int>(flex_preservation_changes[6].value) == 128);
    CHECK(std::get<int>(flex_preservation_changes[7].value) == 40);
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
            const auto& selected = physical.front();
            write_bytes(config_root.parent_path() / L"Logs" / L"Launch.log",
                        "[0002.55] Log: Adapter : " + utf8(selected.name) +
                            "\r\n");
            expected_startup_memory =
                kf2::optimizer::recommended_startup_memory_profile(
                    selected.dedicated_memory_bytes);
        }
    }

    {
        auto first = kf2::app::Application::start(options);
        CHECK(first.has_value());
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
            CHECK(automatically_prepared_engine.find(
                      "PoolSize=" + std::to_string(
                          expected_startup_memory->texture_pool_size_mb)) !=
                  std::string::npos);
            CHECK(automatically_prepared_engine.find(
                      "MemoryMargin=" + std::to_string(
                          expected_startup_memory->memory_margin_mb)) !=
                  std::string::npos);
            CHECK(automatically_prepared_engine.find(
                      "HysteresisLimit=" + std::to_string(
                          expected_startup_memory->streaming_hysteresis_limit)) !=
                  std::string::npos);
        }
        CHECK(read_bytes(config_root / L"KFSystemSettings.ini").find(
                  "OneFrameThreadLag=True") != std::string::npos);
        CHECK(fs::exists(published_telemetry));
        CHECK(read_bytes(published_telemetry) == read_bytes(telemetry_asset));
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "GameViewportClientClassName=KFGame."
                  "KFGameViewportClient") !=
              std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "LocalOptions=?Mutator=KF2OptimizerTelemetry."
                  "KF2OptimizerTelemetryMutator") != std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "Package=KF2OptimizerTelemetry") == std::string::npos);
        CHECK(fs::exists(options.state_root / L"settings.ini"));
        CHECK(fs::exists(options.state_root / L"session.marker"));
        CHECK(fs::is_directory(options.state_root / L"logs"));
        CHECK(fs::exists(options.state_root / L"logs/session-events.json"));
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
        "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n";
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
    CHECK(graphical.value().ui_model().status().advanced_dirty);
    CHECK(graphical.value().ui_model().status().advanced_values[0] == L"Off");
    for (int step = 0; step < 24 &&
         graphical.value().ui_model().focused_action() !=
             std::optional<std::string>{"advanced-apply"}; ++step) {
        SendMessageW(hwnd, WM_KEYDOWN, VK_TAB, 0);
    }
    CHECK(graphical.value().ui_model().focused_action() ==
          std::optional<std::string>{"advanced-apply"});
    SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(!graphical.value().ui_model().status().advanced_dirty);
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
    const auto debug_settings_bytes =
        read_bytes(options.state_root / L"settings.ini");
    CHECK(debug_settings_bytes.find("debug_corpse_markers=true\n") !=
          std::string::npos);
    CHECK(debug_settings_bytes.find("debug_zed_markers=true\n") !=
          std::string::npos);
    CHECK(read_bytes(config_root / L"KFEngine.ini").find(
              "bAdaptiveCorpseDebugMarkers=True") != std::string::npos);
    CHECK(read_bytes(config_root / L"KFEngine.ini").find(
              "bAdaptiveZedDebugMarkers=True") != std::string::npos);
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

        const auto first_prepare =
            rearm_runtime.prepare_automatic_external_launch_profile();
        CHECK(first_prepare.has_value());
        CHECK(first_prepare.value());
        CHECK(fs::exists(published_telemetry));
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "LocalOptions=?Mutator=KF2OptimizerTelemetry."
                  "KF2OptimizerTelemetryMutator") != std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "GameViewportClientClassName=KFGame."
                  "KFGameViewportClient") !=
              std::string::npos);
        CHECK(read_bytes(config_root / L"KFEngine.ini").find(
                  "Paths=" + published_runtime_path) != std::string::npos);

        rearm_runtime.game_log_new_settings_restart_requested = true;
        const auto restart_wait_started = rearm_runtime.monotonic_ns();
        rearm_runtime.begin_game_restart_handoff({
            4242, 123456, rearm_runtime.installation->executable});
        CHECK(rearm_runtime.game_restart_handoff_previous_process.has_value());
        CHECK(rearm_runtime.game_restart_handoff_new_settings);
        CHECK(rearm_runtime.game_restart_handoff_deadline_ns >=
              restart_wait_started +
                  kf2::telemetry_pipeline::kNewSettingsRestartHandoffNs);
        CHECK(rearm_runtime.session_config_snapshot.has_value());
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

        auto restarted_engine = read_bytes(config_root / L"KFEngine.ini");
        const auto flex_setting = restarted_engine.find("PhysXLevel=0");
        CHECK(flex_setting != std::string::npos);
        restarted_engine.replace(
            flex_setting, std::string_view{"PhysXLevel=0"}.size(),
            "PhysXLevel=2");
        write_bytes(config_root / L"KFEngine.ini", restarted_engine);
        rearm_runtime.refresh_game_configuration_for_process_start(true);
        const auto flex_index = static_cast<std::size_t>(
            kf2::game::VideoOption::nvidia_flex);
        CHECK(rearm_runtime.model.status().graphics_values[flex_index] ==
              L"Gibs and fluids");
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
        CHECK(read_bytes(config_root / L"KFEngine.ini") ==
              original_engine_config);

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
                  "GameViewportClientClassName=KFGame."
                  "KFGameViewportClient") !=
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
    }
    CHECK(!fs::exists(published_telemetry));
    CHECK(read_bytes(config_root / L"KFEngine.ini") == original_engine_config);

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
