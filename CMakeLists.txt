cmake_minimum_required(VERSION 3.28)

project(KF2OptimizerNext VERSION 0.0.4 LANGUAGES CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "KF2 Optimizer Next supports x64 only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(KF2_VERSION "${PROJECT_VERSION}-alpha" CACHE STRING "Product version")
set(KF2_BUILD_COMMIT "unknown" CACHE STRING "Source commit identity")
set(KF2_BUILD_CHANNEL "dev" CACHE STRING "Build channel")
set(KF2_RELEASE_REPOSITORY "" CACHE STRING "HTTPS GitHub repository used for exact-version release repair")
if(KF2_RELEASE_REPOSITORY STREQUAL "")
    execute_process(
        COMMAND git -C "${PROJECT_SOURCE_DIR}" config --get remote.origin.url
        OUTPUT_VARIABLE KF2_ORIGIN_URL
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    string(REGEX REPLACE "\\.git$" "" KF2_ORIGIN_URL "${KF2_ORIGIN_URL}")
    if(KF2_ORIGIN_URL MATCHES "^https://github\\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
        set(KF2_RELEASE_REPOSITORY "${KF2_ORIGIN_URL}" CACHE STRING
            "HTTPS GitHub repository used for exact-version release repair" FORCE)
    endif()
endif()
if(NOT KF2_RELEASE_REPOSITORY STREQUAL "" AND
   NOT KF2_RELEASE_REPOSITORY MATCHES "^https://github\\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
    message(FATAL_ERROR "KF2_RELEASE_REPOSITORY must be an HTTPS GitHub repository URL")
endif()

add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc /Brepro)
    add_link_options(/Brepro)
endif()

include(CTest)

add_library(presentmon_data STATIC
    third_party/presentmon/presentmon_debug_stubs.cpp
    third_party/presentmon/PresentData/GpuTrace.cpp
    third_party/presentmon/PresentData/NvidiaTraceConsumer.cpp
    third_party/presentmon/PresentData/PresentMonTraceConsumer.cpp
    third_party/presentmon/PresentData/PresentMonTraceSession.cpp
    third_party/presentmon/PresentData/TraceConsumer.cpp
    third_party/presentmon/PresentData/TraceLogging.cpp
)
target_include_directories(presentmon_data PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/presentmon/PresentData"
)
set_target_properties(presentmon_data PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(presentmon_data PRIVATE
    _CRT_SECURE_NO_WARNINGS NDEBUG PRESENTMON_ENABLE_DEBUG_TRACE=0
)
if(MSVC)
    target_compile_options(presentmon_data PRIVATE /w /permissive)
endif()
target_link_libraries(presentmon_data PUBLIC advapi32 tdh)

set(KF2_FEATURE_ACTION_SOURCES
    "${PROJECT_SOURCE_DIR}/src/features/advanced/advanced_actions.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/backup/backup_actions.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/diagnostics/diagnostics_actions.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/game/game_actions.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/graphics/graphics_actions.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/overlay/overlay_actions.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/settings/settings_actions.cpp"
)
set(KF2_APP_ACTION_SOURCES
    "${PROJECT_SOURCE_DIR}/src/app/application_advanced.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/application_actions.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/runtime/action_contract.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/runtime/action_router.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/runtime/feature_composition.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/runtime/feature_registry.cpp"
    ${KF2_FEATURE_ACTION_SOURCES}
)
string(JOIN "|" KF2_FEATURE_ACTION_SOURCES_PIPE
    ${KF2_FEATURE_ACTION_SOURCES})
string(JOIN "|" KF2_APP_ACTION_SOURCES_PIPE
    ${KF2_APP_ACTION_SOURCES})

set(KF2_TELEMETRY_STAGE_SOURCES
    "${PROJECT_SOURCE_DIR}/src/features/telemetry/telemetry_adaptive_stage.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/telemetry/telemetry_collection_stage.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/telemetry/telemetry_effect_stage.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/telemetry/telemetry_frame.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/telemetry/telemetry_flex_stage.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/telemetry/telemetry_presentation_stage.cpp"
    "${PROJECT_SOURCE_DIR}/src/features/telemetry/telemetry_session_stage.cpp"
)
set(KF2_APP_TELEMETRY_SOURCES
    "${PROJECT_SOURCE_DIR}/src/app/application_telemetry.cpp"
    ${KF2_TELEMETRY_STAGE_SOURCES}
)
string(JOIN "|" KF2_TELEMETRY_STAGE_SOURCES_PIPE
    ${KF2_TELEMETRY_STAGE_SOURCES})
string(JOIN "|" KF2_APP_TELEMETRY_SOURCES_PIPE
    ${KF2_APP_TELEMETRY_SOURCES})

add_executable(KF2Optimizer WIN32
    assets/app.manifest
    assets/mascot/mascot_resources.rc
    src/benchmark/benchmark.cpp
    src/backup/backup_store.cpp
    src/backup/restore_transaction.cpp
    src/app/main.cpp
    src/app/application.cpp
    src/app/application_package_repair.cpp
    ${KF2_APP_ACTION_SOURCES}
    src/app/application_runtime.cpp
    src/app/application_update.cpp
    ${KF2_APP_TELEMETRY_SOURCES}
    src/app/application_window.cpp
    src/app/build_identity.cpp
    src/app/session.cpp
    src/app/state_paths.cpp
    src/config/settings.cpp
    src/config/adaptive_locks.cpp
    src/config/session_guard.cpp
    src/config/ini_document.cpp
    src/config/kf2_catalog.cpp
    src/config/setting_catalog.cpp
    src/config/config_preview.cpp
    src/config/apply_transaction.cpp
    src/diagnostics/event_log.cpp
    src/diagnostics/crash_recorder.cpp
    src/diagnostics/feature_inventory.cpp
    src/flex/flex_audit.cpp
    src/flex/flex_adaptive_policy.cpp
    src/flex/flex_lab.cpp
    src/flex/flex_observation.cpp
    src/game/game_discovery.cpp
    src/game/gameplay_log_lab.cpp
    src/game/offline_telemetry_lab.cpp
    src/game/game_log_description.cpp
    src/game/game_log_parsing.cpp
    src/game/game_log_session.cpp
    src/game/game_log_telemetry.cpp
    src/game/game_session.cpp
    src/game/advanced_settings.cpp
    src/game/video_settings.cpp
    src/overlay/overlay_mascot.cpp
    src/overlay/overlay_policy.cpp
    src/overlay/overlay_window_animation.cpp
    src/overlay/overlay_window_native.cpp
    src/overlay/overlay_window_render.cpp
    src/overlay/overlay_window.cpp
    src/optimizer/adaptive_profile.cpp
    src/optimizer/adaptive_registry.cpp
    src/optimizer/adaptive_actuation.cpp
    src/optimizer/adaptive_governor.cpp
    src/optimizer/optimizer_engine.cpp
    src/platform/windows/atomic_file.cpp
    src/platform/windows/presentmon_session.cpp
    src/platform/windows/process_security.cpp
    src/platform/windows/single_instance.cpp
    src/platform/windows/state_environment.cpp
    src/platform/windows/window.cpp
    src/security/sha256.cpp
    src/security/package_integrity.cpp
    src/security/release_package_repair.cpp
    src/telemetry/telemetry_snapshot.cpp
    src/telemetry/system_metrics.cpp
    src/telemetry/present_source.cpp
    src/telemetry/gpu_metrics.cpp
    src/update/semantic_version.cpp
    src/update/github_release_client.cpp
    src/update/release_notes.cpp
    src/update/update_controller.cpp
    src/update/update_state.cpp
    src/update/update_package.cpp
    src/update/update_helper.cpp
    src/update/update_transaction.cpp
    src/ui/direct2d_renderer.cpp
    src/ui/automation_provider.cpp
    src/ui/shell_controller.cpp
    src/ui/shell_layout.cpp
    src/ui/tooltip_text.cpp
    src/ui/theme.cpp
    src/ui/ui_animation.cpp
    src/ui/numeric_presentation.cpp
    src/ui/ui_model.cpp
)
target_include_directories(KF2Optimizer PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
target_compile_definitions(KF2Optimizer PRIVATE
    KF2_VERSION="${KF2_VERSION}"
    KF2_BUILD_COMMIT="${KF2_BUILD_COMMIT}"
    KF2_BUILD_CHANNEL="${KF2_BUILD_CHANNEL}"
    KF2_RELEASE_REPOSITORY="${KF2_RELEASE_REPOSITORY}"
)
if(MSVC)
    set_source_files_properties(assets/app.manifest PROPERTIES
        VS_DEPLOYMENT_CONTENT 0)
    target_compile_options(KF2Optimizer PRIVATE /guard:cf)
    target_link_options(KF2Optimizer PRIVATE
        /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf /CETCOMPAT
    )
endif()
target_link_libraries(KF2Optimizer PRIVATE
    presentmon_data
    user32 gdi32 shell32 ole32 oleaut32 uuid d2d1 dwrite windowscodecs winmm uiautomationcore bcrypt dwmapi psapi advapi32 pdh dxgi tdh version winhttp
)

add_executable(KF2ConfigRoundtrip EXCLUDE_FROM_ALL
    tools/config_roundtrip_validator.cpp
    src/backup/backup_store.cpp
    src/backup/restore_transaction.cpp
    src/config/apply_transaction.cpp
    src/config/config_preview.cpp
    src/config/ini_document.cpp
    src/config/kf2_catalog.cpp
    src/config/setting_catalog.cpp
    src/platform/windows/atomic_file.cpp
    src/security/sha256.cpp
)
target_include_directories(KF2ConfigRoundtrip PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(KF2ConfigRoundtrip PRIVATE bcrypt)

add_executable(KF2InventoryExport EXCLUDE_FROM_ALL
    tools/feature_inventory_export.cpp
    src/diagnostics/feature_inventory.cpp
)
target_include_directories(KF2InventoryExport PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(BUILD_TESTING)
    find_program(KF2_LLD_LINK NAMES lld-link
        PATHS "C:/Program Files/Swift/Toolchains/6.2.1+Asserts/usr/bin")
    if(KF2_LLD_LINK)
        set(KF2_FLEX_FORWARDER "${CMAKE_BINARY_DIR}/flexRelease_x64.forwarder-lab.dll")
        set(KF2_FLEX_ANCHOR "${CMAKE_BINARY_DIR}/flex_forwarder_anchor.obj")
        add_custom_command(OUTPUT "${KF2_FLEX_FORWARDER}"
            COMMAND "${CMAKE_CXX_COMPILER}" /nologo /c /O2 /Oi- /GS- /GR- /EHs-c- /std:c++20
                "/Fo${KF2_FLEX_ANCHOR}"
                "/I${PROJECT_SOURCE_DIR}/include"
                "${PROJECT_SOURCE_DIR}/src/flex/forwarder_stub.cpp"
            COMMAND "${KF2_LLD_LINK}" /dll /entry:DllMain
                "/def:${PROJECT_SOURCE_DIR}/src/flex/flex_forwarder.def"
                "${KF2_FLEX_ANCHOR}"
                "/out:${KF2_FLEX_FORWARDER}" /machine:x64 /nodefaultlib
                kernel32.lib
                /subsystem:windows /opt:ref /opt:icf /brepro
            DEPENDS "${PROJECT_SOURCE_DIR}/src/flex/flex_forwarder.def"
                "${PROJECT_SOURCE_DIR}/src/flex/forwarder_stub.cpp"
                "${PROJECT_SOURCE_DIR}/include/kf2/flex/flex_observation_shared.hpp"
            VERBATIM)
        add_custom_target(KF2FlexForwarder DEPENDS "${KF2_FLEX_FORWARDER}")
        add_dependencies(KF2Optimizer KF2FlexForwarder)
    endif()
    add_subdirectory(tests)
endif()
