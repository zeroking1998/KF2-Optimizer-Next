set(KF2_TEST_SANDBOX_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/runtime/$<CONFIG>")

add_executable(kf2_bootstrap_test unit/bootstrap_test.cpp)
add_test(NAME kf2_bootstrap_test COMMAND kf2_bootstrap_test)

add_executable(kf2_result_test unit/result_test.cpp)
target_include_directories(kf2_result_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_result_test COMMAND kf2_result_test)

add_executable(kf2_action_contract_test
    unit/action_contract_test.cpp
)
target_include_directories(kf2_action_contract_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
target_sources(kf2_action_contract_test PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app/runtime/action_contract.cpp"
)
add_test(NAME kf2_action_contract_test COMMAND kf2_action_contract_test)

add_executable(kf2_feature_registry_test
    unit/feature_registry_test.cpp
    "${PROJECT_SOURCE_DIR}/src/app/runtime/action_contract.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/runtime/feature_registry.cpp"
)
target_include_directories(kf2_feature_registry_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_feature_registry_test COMMAND kf2_feature_registry_test)

add_executable(kf2_action_router_test
    unit/action_router_test.cpp
    "${PROJECT_SOURCE_DIR}/src/app/runtime/action_contract.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/runtime/action_router.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/runtime/feature_registry.cpp"
)
target_include_directories(kf2_action_router_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_action_router_test COMMAND kf2_action_router_test)

add_test(NAME kf2_action_architecture_test
    COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DKF2_FEATURE_ACTION_SOURCES_PIPE=${KF2_FEATURE_ACTION_SOURCES_PIPE}"
        "-DKF2_APP_ACTION_SOURCES_PIPE=${KF2_APP_ACTION_SOURCES_PIPE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/architecture/action_module_boundary_test.cmake")
set_tests_properties(kf2_action_architecture_test PROPERTIES
    LABELS "architecture;actions")

add_executable(kf2_build_identity_test
    unit/build_identity_test.cpp
    "${PROJECT_SOURCE_DIR}/src/app/build_identity.cpp"
)
target_include_directories(kf2_build_identity_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_build_identity_test PRIVATE
    KF2_VERSION="0.0.1-alpha"
    KF2_BUILD_COMMIT="unknown"
    KF2_BUILD_CHANNEL="dev"
)
add_test(NAME kf2_build_identity_test COMMAND kf2_build_identity_test)

add_executable(kf2_pe_contract_test windows/pe_contract_test.cpp)
add_dependencies(kf2_pe_contract_test KF2Optimizer)
add_test(NAME kf2_pe_contract_test
    COMMAND kf2_pe_contract_test "$<TARGET_FILE:KF2Optimizer>")

add_executable(kf2_window_smoke_test
    windows/window_smoke_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/window.cpp"
)
target_include_directories(kf2_window_smoke_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_window_smoke_test PRIVATE user32)
add_test(NAME kf2_window_smoke_test COMMAND kf2_window_smoke_test)

add_executable(kf2_process_security_test
    windows/process_security_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/process_security.cpp"
)
target_include_directories(kf2_process_security_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_process_security_test COMMAND kf2_process_security_test)

add_executable(kf2_package_integrity_test
    windows/package_integrity_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/package_integrity.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp"
)
target_include_directories(kf2_package_integrity_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_package_integrity_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/package-integrity")
target_link_libraries(kf2_package_integrity_test PRIVATE bcrypt)
add_test(NAME kf2_package_integrity_test COMMAND kf2_package_integrity_test)

add_executable(kf2_state_paths_test
    unit/state_paths_test.cpp
    "${PROJECT_SOURCE_DIR}/src/app/state_paths.cpp"
)
target_include_directories(kf2_state_paths_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_state_paths_test COMMAND kf2_state_paths_test)

add_executable(kf2_settings_test
    unit/settings_test.cpp
    "${PROJECT_SOURCE_DIR}/src/config/settings.cpp"
)
target_include_directories(kf2_settings_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_settings_test COMMAND kf2_settings_test)

add_executable(kf2_session_config_guard_test
    integration/session_config_guard_test.cpp
    "${PROJECT_SOURCE_DIR}/src/config/session_guard.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp")
target_include_directories(kf2_session_config_guard_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_session_config_guard_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/session-config-guard")
target_link_libraries(kf2_session_config_guard_test PRIVATE bcrypt)
add_test(NAME kf2_session_config_guard_test COMMAND kf2_session_config_guard_test)

add_executable(kf2_atomic_file_test
    windows/atomic_file_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
)
target_include_directories(kf2_atomic_file_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_atomic_file_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/atomic-file"
)
add_test(NAME kf2_atomic_file_test COMMAND kf2_atomic_file_test)

add_executable(kf2_session_test
    unit/session_test.cpp
    "${PROJECT_SOURCE_DIR}/src/app/session.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
)
target_include_directories(kf2_session_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_session_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/session"
)
add_test(NAME kf2_session_test COMMAND kf2_session_test)

add_executable(kf2_single_instance_test
    windows/single_instance_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/single_instance.cpp"
)
target_include_directories(kf2_single_instance_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_single_instance_test COMMAND kf2_single_instance_test)

add_executable(kf2_event_log_test
    unit/event_log_test.cpp
    "${PROJECT_SOURCE_DIR}/src/diagnostics/event_log.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
)
target_include_directories(kf2_event_log_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_event_log_test COMMAND kf2_event_log_test)

add_executable(kf2_feature_inventory_test
    unit/feature_inventory_test.cpp
    "${PROJECT_SOURCE_DIR}/src/diagnostics/feature_inventory.cpp"
)
target_include_directories(kf2_feature_inventory_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_feature_inventory_test COMMAND kf2_feature_inventory_test)

add_executable(kf2_crash_recorder_test
    windows/crash_recorder_test.cpp
    "${PROJECT_SOURCE_DIR}/src/diagnostics/crash_recorder.cpp"
)
target_include_directories(kf2_crash_recorder_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_crash_recorder_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/crash-recorder")
target_link_libraries(kf2_crash_recorder_test PRIVATE bcrypt)
add_test(NAME kf2_crash_recorder_test COMMAND kf2_crash_recorder_test)

add_executable(kf2_flex_audit_test
    unit/flex_audit_test.cpp
    "${PROJECT_SOURCE_DIR}/src/flex/flex_audit.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp"
)
target_include_directories(kf2_flex_audit_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_flex_audit_test PRIVATE
    KF2_FLEX_RUNTIME=L"D:/Steam/steamapps/common/killingfloor2/Binaries/Win64/flexRelease_x64.dll"
)
target_link_libraries(kf2_flex_audit_test PRIVATE bcrypt version)
add_test(NAME kf2_flex_audit_test COMMAND kf2_flex_audit_test)
set_tests_properties(kf2_flex_audit_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(kf2_flex_lab_test
    integration/flex_lab_test.cpp
    "${PROJECT_SOURCE_DIR}/src/flex/flex_lab.cpp"
    "${PROJECT_SOURCE_DIR}/src/flex/flex_observation.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp")
target_include_directories(kf2_flex_lab_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_flex_lab_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/flex-lab")
target_link_libraries(kf2_flex_lab_test PRIVATE bcrypt)
add_test(NAME kf2_flex_lab_test COMMAND kf2_flex_lab_test)

add_executable(kf2_offline_telemetry_lab_test
    windows/offline_telemetry_lab_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/offline_telemetry_lab.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp")
target_include_directories(kf2_offline_telemetry_lab_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_offline_telemetry_lab_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/offline-telemetry-lab"
    KF2_TELEMETRY_ASSET=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerTelemetry.u")
target_link_libraries(kf2_offline_telemetry_lab_test PRIVATE bcrypt)
add_test(NAME kf2_offline_telemetry_lab_test
    COMMAND kf2_offline_telemetry_lab_test)
set_tests_properties(kf2_offline_telemetry_lab_test PROPERTIES
    SKIP_RETURN_CODE 77)

if(TARGET KF2FlexForwarder)
    add_library(kf2_flex_original_test_double SHARED
        windows/flex_original_test_double.cpp)
    set_target_properties(kf2_flex_original_test_double PROPERTIES
        OUTPUT_NAME "flex_original_test_double")

    add_executable(kf2_flex_forwarder_loader_test windows/flex_forwarder_loader_test.cpp)
    target_include_directories(kf2_flex_forwarder_loader_test PRIVATE
        "${PROJECT_SOURCE_DIR}/include")
    target_compile_definitions(kf2_flex_forwarder_loader_test PRIVATE
        KF2_FLEX_RUNTIME=L"D:/Steam/steamapps/common/killingfloor2/Binaries/Win64/flexRelease_x64.dll"
    )
    add_dependencies(kf2_flex_forwarder_loader_test KF2FlexForwarder)
    add_test(NAME kf2_flex_forwarder_loader_test COMMAND kf2_flex_forwarder_loader_test
        "${PROJECT_SOURCE_DIR}/out/tests/flex-forwarder"
        "${KF2_FLEX_FORWARDER}")
    set_tests_properties(kf2_flex_forwarder_loader_test PROPERTIES
        SKIP_RETURN_CODE 77)

    add_executable(kf2_flex_forwarder_adaptive_test
        windows/flex_forwarder_adaptive_test.cpp)
    target_include_directories(kf2_flex_forwarder_adaptive_test PRIVATE
        "${PROJECT_SOURCE_DIR}/include")
    add_dependencies(kf2_flex_forwarder_adaptive_test
        KF2FlexForwarder kf2_flex_original_test_double)
    add_test(NAME kf2_flex_forwarder_adaptive_test
        COMMAND kf2_flex_forwarder_adaptive_test
        "${PROJECT_SOURCE_DIR}/out/tests/flex-forwarder-adaptive"
        "${KF2_FLEX_FORWARDER}"
        "$<TARGET_FILE:kf2_flex_original_test_double>")
endif()

add_executable(kf2_flex_observation_test
    windows/flex_observation_test.cpp
    "${PROJECT_SOURCE_DIR}/src/flex/flex_observation.cpp")
target_include_directories(kf2_flex_observation_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_flex_observation_test COMMAND kf2_flex_observation_test)

add_executable(kf2_flex_adaptive_policy_test
    unit/flex_adaptive_policy_test.cpp
    "${PROJECT_SOURCE_DIR}/src/flex/flex_adaptive_policy.cpp")
target_include_directories(kf2_flex_adaptive_policy_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_flex_adaptive_policy_test COMMAND kf2_flex_adaptive_policy_test)

add_executable(kf2_application_lifecycle_test
    integration/application_lifecycle_test.cpp
    ${KF2_APP_ACTION_SOURCES}
    "${PROJECT_SOURCE_DIR}/src/benchmark/benchmark.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/application.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/application_runtime.cpp"
    ${KF2_APP_TELEMETRY_SOURCES}
    "${PROJECT_SOURCE_DIR}/src/app/application_window.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/build_identity.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/session.cpp"
    "${PROJECT_SOURCE_DIR}/src/backup/backup_store.cpp"
    "${PROJECT_SOURCE_DIR}/src/backup/restore_transaction.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/apply_transaction.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/config_preview.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/adaptive_locks.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/setting_catalog.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/settings.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/session_guard.cpp"
    "${PROJECT_SOURCE_DIR}/src/diagnostics/event_log.cpp"
    "${PROJECT_SOURCE_DIR}/src/diagnostics/crash_recorder.cpp"
    "${PROJECT_SOURCE_DIR}/src/diagnostics/feature_inventory.cpp"
    "${PROJECT_SOURCE_DIR}/src/flex/flex_audit.cpp"
    "${PROJECT_SOURCE_DIR}/src/flex/flex_adaptive_policy.cpp"
    "${PROJECT_SOURCE_DIR}/src/flex/flex_lab.cpp"
    "${PROJECT_SOURCE_DIR}/src/flex/flex_observation.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_discovery.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/gameplay_log_lab.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/offline_telemetry_lab.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_description.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_parsing.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_telemetry.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_mascot.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_policy.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window_animation.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window_native.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window_render.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_profile.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_registry.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_actuation.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_governor.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/optimizer_engine.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/presentmon_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/process_security.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/single_instance.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/window.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/package_integrity.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/gpu_metrics.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/present_source.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/system_metrics.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/telemetry_snapshot.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/automation_provider.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/direct2d_renderer.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_controller.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
)
target_include_directories(kf2_application_lifecycle_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
target_compile_definitions(kf2_application_lifecycle_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/application-lifecycle"
    KF2_VERSION="0.0.1-alpha"
    KF2_BUILD_COMMIT="unknown"
    KF2_BUILD_CHANNEL="dev"
)
target_link_libraries(kf2_application_lifecycle_test PRIVATE
    presentmon_data
    user32 gdi32 shell32 ole32 uuid d2d1 dwrite windowscodecs winmm uiautomationcore bcrypt
    dwmapi psapi advapi32 pdh dxgi tdh version
)
add_test(NAME kf2_application_lifecycle_test COMMAND kf2_application_lifecycle_test)
set_tests_properties(kf2_application_lifecycle_test PROPERTIES
    LABELS "requires-desktop")

add_executable(kf2_state_environment_test
    windows/state_environment_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/state_environment.cpp"
)
target_include_directories(kf2_state_environment_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_state_environment_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/state-environment"
)
target_link_libraries(kf2_state_environment_test PRIVATE shell32 ole32)
add_test(NAME kf2_state_environment_test COMMAND kf2_state_environment_test)

add_executable(kf2_ui_model_test
    unit/ui_model_test.cpp
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
)
target_include_directories(kf2_ui_model_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_ui_model_test COMMAND kf2_ui_model_test)

add_executable(kf2_shell_layout_test
    unit/shell_layout_test.cpp
    "${PROJECT_SOURCE_DIR}/src/app/runtime/action_contract.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme.cpp"
)
target_include_directories(kf2_shell_layout_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_shell_layout_test COMMAND kf2_shell_layout_test)

add_executable(kf2_shell_controller_test
    unit/shell_controller_test.cpp
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_controller.cpp"
)
target_include_directories(kf2_shell_controller_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_shell_controller_test COMMAND kf2_shell_controller_test)

add_executable(kf2_direct2d_renderer_test
    windows/direct2d_renderer_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/window.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/direct2d_renderer.cpp"
)
target_include_directories(kf2_direct2d_renderer_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_direct2d_renderer_test PRIVATE
    KF2_GUI_TEST_ROOT=L"${PROJECT_SOURCE_DIR}/out/tests/gui"
)
target_link_libraries(kf2_direct2d_renderer_test PRIVATE
    user32 ole32 d2d1 dwrite windowscodecs
)
add_test(NAME kf2_direct2d_renderer_test COMMAND kf2_direct2d_renderer_test)
set_tests_properties(kf2_direct2d_renderer_test PROPERTIES
    LABELS "requires-desktop")

add_executable(kf2_system_sound_absence_test
    windows/system_sound_absence_test.cpp
)
add_dependencies(kf2_system_sound_absence_test KF2Optimizer)
add_test(NAME kf2_system_sound_absence_test
    COMMAND kf2_system_sound_absence_test "$<TARGET_FILE:KF2Optimizer>")

add_executable(kf2_automation_provider_test
    windows/automation_provider_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/window.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/automation_provider.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
)
target_include_directories(kf2_automation_provider_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_automation_provider_test PRIVATE user32 ole32 uiautomationcore)
add_test(NAME kf2_automation_provider_test COMMAND kf2_automation_provider_test)

add_executable(kf2_game_discovery_test
    windows/game_discovery_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/game_discovery.cpp"
)
target_include_directories(kf2_game_discovery_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_game_discovery_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/game-discovery"
)
target_link_libraries(kf2_game_discovery_test PRIVATE user32)
add_test(NAME kf2_game_discovery_test COMMAND kf2_game_discovery_test)

add_executable(kf2_game_log_session_test
    unit/game_log_session_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/game_log_description.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_parsing.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_telemetry.cpp"
)
target_include_directories(kf2_game_log_session_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_game_log_session_test COMMAND kf2_game_log_session_test)

add_executable(kf2_gameplay_log_lab_test
    windows/gameplay_log_lab_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/gameplay_log_lab.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
)
target_include_directories(kf2_gameplay_log_lab_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_gameplay_log_lab_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/gameplay-log-lab"
    KF2_TELEMETRY_SOURCE=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerTelemetryProbe.uc"
    KF2_TELEMETRY_VIEWPORT_SOURCE=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerTelemetryViewport.uc")
add_test(NAME kf2_gameplay_log_lab_test COMMAND kf2_gameplay_log_lab_test)

add_executable(kf2_ini_document_test
    unit/ini_document_test.cpp
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
)
target_include_directories(kf2_ini_document_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_ini_document_test COMMAND kf2_ini_document_test)

add_executable(kf2_catalog_test
    unit/kf2_catalog_test.cpp
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
)
target_include_directories(kf2_catalog_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_catalog_test COMMAND kf2_catalog_test)

add_executable(kf2_setting_catalog_test
    unit/setting_catalog_test.cpp
    "${PROJECT_SOURCE_DIR}/src/config/setting_catalog.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
)
target_include_directories(kf2_setting_catalog_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_setting_catalog_test PRIVATE user32)
add_test(NAME kf2_setting_catalog_test COMMAND kf2_setting_catalog_test)

add_executable(kf2_config_preview_test
    unit/config_preview_test.cpp
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/config_preview.cpp"
)
target_include_directories(kf2_config_preview_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_config_preview_test COMMAND kf2_config_preview_test)

add_executable(kf2_config_apply_test
    integration/config_apply_test.cpp
    "${PROJECT_SOURCE_DIR}/src/backup/backup_store.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/apply_transaction.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp"
)
target_include_directories(kf2_config_apply_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_config_apply_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/config-apply"
)
target_link_libraries(kf2_config_apply_test PRIVATE bcrypt)
add_test(NAME kf2_config_apply_test COMMAND kf2_config_apply_test)

add_executable(kf2_backup_restore_test
    integration/backup_restore_test.cpp
    "${PROJECT_SOURCE_DIR}/src/backup/backup_store.cpp"
    "${PROJECT_SOURCE_DIR}/src/backup/restore_transaction.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/apply_transaction.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/setting_catalog.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp"
)
target_include_directories(kf2_backup_restore_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_backup_restore_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/backup-restore"
)
target_link_libraries(kf2_backup_restore_test PRIVATE bcrypt)
add_test(NAME kf2_backup_restore_test COMMAND kf2_backup_restore_test)

add_executable(kf2_telemetry_snapshot_test
    unit/telemetry_snapshot_test.cpp
    "${PROJECT_SOURCE_DIR}/src/telemetry/telemetry_snapshot.cpp"
)
target_include_directories(kf2_telemetry_snapshot_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_telemetry_snapshot_test COMMAND kf2_telemetry_snapshot_test)

add_executable(kf2_telemetry_frame_test
    unit/telemetry_frame_test.cpp
    "${PROJECT_SOURCE_DIR}/src/features/telemetry/telemetry_frame.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_description.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/gpu_metrics.cpp"
)
target_include_directories(kf2_telemetry_frame_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(kf2_telemetry_frame_test PRIVATE pdh dxgi gdi32)
add_test(NAME kf2_telemetry_frame_test COMMAND kf2_telemetry_frame_test)

add_executable(kf2_telemetry_session_stage_test
    unit/telemetry_session_stage_test.cpp
)
target_include_directories(kf2_telemetry_session_stage_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_telemetry_session_stage_test
    COMMAND kf2_telemetry_session_stage_test)

add_executable(kf2_telemetry_flex_stage_test
    unit/telemetry_flex_stage_test.cpp
    "${PROJECT_SOURCE_DIR}/src/flex/flex_adaptive_policy.cpp"
)
target_include_directories(kf2_telemetry_flex_stage_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_telemetry_flex_stage_test
    COMMAND kf2_telemetry_flex_stage_test)

add_executable(kf2_telemetry_collection_stage_test
    unit/telemetry_collection_stage_test.cpp
)
target_include_directories(kf2_telemetry_collection_stage_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_telemetry_collection_stage_test
    COMMAND kf2_telemetry_collection_stage_test)

add_executable(kf2_telemetry_adaptive_stage_test
    unit/telemetry_adaptive_stage_test.cpp
)
target_include_directories(kf2_telemetry_adaptive_stage_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_telemetry_adaptive_stage_test
    COMMAND kf2_telemetry_adaptive_stage_test)

add_executable(kf2_telemetry_effect_stage_test
    unit/telemetry_effect_stage_test.cpp
)
target_include_directories(kf2_telemetry_effect_stage_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_telemetry_effect_stage_test
    COMMAND kf2_telemetry_effect_stage_test)

add_executable(kf2_telemetry_presentation_stage_test
    unit/telemetry_presentation_stage_test.cpp
)
target_include_directories(kf2_telemetry_presentation_stage_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_telemetry_presentation_stage_test
    COMMAND kf2_telemetry_presentation_stage_test)

add_executable(kf2_telemetry_pipeline_test
    unit/telemetry_pipeline_test.cpp
)
target_include_directories(kf2_telemetry_pipeline_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_telemetry_pipeline_test
    COMMAND kf2_telemetry_pipeline_test)

add_test(NAME kf2_telemetry_pipeline_boundary_test
    COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DKF2_TELEMETRY_STAGE_SOURCES_PIPE=${KF2_TELEMETRY_STAGE_SOURCES_PIPE}"
        "-DKF2_APP_TELEMETRY_SOURCES_PIPE=${KF2_APP_TELEMETRY_SOURCES_PIPE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/architecture/telemetry_pipeline_boundary_test.cmake")
set_tests_properties(kf2_telemetry_pipeline_boundary_test PROPERTIES
    LABELS "architecture;telemetry")

add_executable(kf2_game_session_test
    windows/game_session_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/game_session.cpp"
)
target_include_directories(kf2_game_session_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_game_session_test PRIVATE user32 dwmapi)
add_test(NAME kf2_game_session_test COMMAND kf2_game_session_test)

add_executable(kf2_system_metrics_test
    windows/system_metrics_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/game_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/system_metrics.cpp"
)
target_include_directories(kf2_system_metrics_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_system_metrics_test PRIVATE psapi dwmapi)
add_test(NAME kf2_system_metrics_test COMMAND kf2_system_metrics_test)

add_executable(kf2_hardware_overview_contract_test
    windows/hardware_overview_contract_test.cpp
    "${PROJECT_SOURCE_DIR}/src/telemetry/system_metrics.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/gpu_metrics.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_session.cpp")
target_include_directories(kf2_hardware_overview_contract_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_hardware_overview_contract_test PRIVATE
    psapi dwmapi pdh dxgi gdi32)
add_test(NAME kf2_hardware_overview_contract_test
    COMMAND kf2_hardware_overview_contract_test)

add_executable(kf2_hardware_refresh_contract_test
    windows/hardware_refresh_contract_test.cpp
    "${PROJECT_SOURCE_DIR}/src/telemetry/system_metrics.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/gpu_metrics.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_session.cpp")
target_include_directories(kf2_hardware_refresh_contract_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_hardware_refresh_contract_test PRIVATE
    psapi dwmapi pdh dxgi gdi32)
add_test(NAME kf2_hardware_refresh_contract_test
    COMMAND kf2_hardware_refresh_contract_test)

add_executable(kf2_present_source_test
    integration/present_source_test.cpp
    "${PROJECT_SOURCE_DIR}/src/telemetry/present_source.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/telemetry_snapshot.cpp"
)
target_include_directories(kf2_present_source_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_present_source_test COMMAND kf2_present_source_test)

add_executable(kf2_presentmon_session_test
    windows/presentmon_session_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/presentmon_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/present_source.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/telemetry_snapshot.cpp"
)
target_include_directories(kf2_presentmon_session_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_presentmon_session_test PRIVATE presentmon_data advapi32 tdh)
add_test(NAME kf2_presentmon_session_test COMMAND kf2_presentmon_session_test)

add_executable(kf2_presentmon_dxgi_present_test
    windows/presentmon_dxgi_present_test.cpp
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_mascot.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_policy.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window_animation.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window_native.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window_render.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/presentmon_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/present_source.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/telemetry_snapshot.cpp"
)
target_include_directories(kf2_presentmon_dxgi_present_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_presentmon_dxgi_present_test PRIVATE
    presentmon_data advapi32 tdh d3d11 dxgi user32 gdi32 d2d1 dwrite dwmapi
)
add_test(NAME kf2_presentmon_dxgi_present_test COMMAND kf2_presentmon_dxgi_present_test)
set_tests_properties(kf2_presentmon_dxgi_present_test PROPERTIES
    LABELS "requires-desktop")

add_executable(kf2_gpu_metrics_test
    windows/gpu_metrics_test.cpp
    "${PROJECT_SOURCE_DIR}/src/telemetry/gpu_metrics.cpp"
)
target_include_directories(kf2_gpu_metrics_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_gpu_metrics_test PRIVATE pdh dxgi gdi32)
add_test(NAME kf2_gpu_metrics_test COMMAND kf2_gpu_metrics_test)

add_executable(kf2_overlay_policy_test
    unit/overlay_policy_test.cpp
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_policy.cpp"
)
target_include_directories(kf2_overlay_policy_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_overlay_policy_test COMMAND kf2_overlay_policy_test)

add_executable(kf2_overlay_position_contract_test
    unit/overlay_position_contract_test.cpp
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_policy.cpp")
target_include_directories(kf2_overlay_position_contract_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_overlay_position_contract_test
    COMMAND kf2_overlay_position_contract_test)

add_executable(kf2_overlay_window_test
    windows/overlay_window_test.cpp
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_mascot.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window_animation.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window_native.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window_render.cpp"
    "${PROJECT_SOURCE_DIR}/src/overlay/overlay_window.cpp"
)
target_include_directories(kf2_overlay_window_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_overlay_window_test PRIVATE user32 gdi32 d2d1 dwrite)
add_test(NAME kf2_overlay_window_test COMMAND kf2_overlay_window_test)

add_executable(kf2_optimizer_engine_test
    unit/optimizer_engine_test.cpp
    "${PROJECT_SOURCE_DIR}/src/optimizer/optimizer_engine.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
)
target_include_directories(kf2_optimizer_engine_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_optimizer_engine_test COMMAND kf2_optimizer_engine_test)

add_executable(kf2_adaptive_profile_test
    unit/adaptive_profile_test.cpp
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_profile.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/setting_catalog.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
)
target_include_directories(kf2_adaptive_profile_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_adaptive_profile_test PRIVATE user32)
add_test(NAME kf2_adaptive_profile_test COMMAND kf2_adaptive_profile_test)

add_executable(kf2_adaptive_registry_test
    unit/adaptive_registry_test.cpp
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_registry.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
)
target_include_directories(kf2_adaptive_registry_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_adaptive_registry_test COMMAND kf2_adaptive_registry_test)

add_executable(kf2_adaptive_actuation_test
    unit/adaptive_actuation_test.cpp
    ../src/optimizer/adaptive_actuation.cpp
)
target_include_directories(kf2_adaptive_actuation_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_adaptive_actuation_test COMMAND kf2_adaptive_actuation_test)

add_executable(kf2_adaptive_locks_test
    unit/adaptive_locks_test.cpp
    "${PROJECT_SOURCE_DIR}/src/config/adaptive_locks.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_registry.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
)
target_include_directories(kf2_adaptive_locks_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_adaptive_locks_test COMMAND kf2_adaptive_locks_test)

add_executable(kf2_adaptive_governor_test
    unit/adaptive_governor_test.cpp
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_governor.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_registry.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
)
target_include_directories(kf2_adaptive_governor_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_adaptive_governor_test COMMAND kf2_adaptive_governor_test)

add_executable(kf2_benchmark_test
    unit/benchmark_test.cpp
    "${PROJECT_SOURCE_DIR}/src/benchmark/benchmark.cpp"
)
target_include_directories(kf2_benchmark_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_benchmark_test COMMAND kf2_benchmark_test)
