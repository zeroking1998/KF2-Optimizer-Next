set(KF2_TEST_SANDBOX_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/runtime/$<CONFIG>")

add_executable(kf2_bootstrap_test unit/bootstrap_test.cpp)
add_test(NAME kf2_bootstrap_test COMMAND kf2_bootstrap_test)

add_executable(kf2_result_test unit/result_test.cpp)
target_include_directories(kf2_result_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_result_test COMMAND kf2_result_test)

add_executable(kf2_semantic_version_test
    unit/semantic_version_test.cpp
    "${PROJECT_SOURCE_DIR}/src/update/semantic_version.cpp")
target_include_directories(kf2_semantic_version_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_semantic_version_test COMMAND kf2_semantic_version_test)

add_executable(kf2_update_controller_test
    unit/update_controller_test.cpp
    "${PROJECT_SOURCE_DIR}/src/update/update_controller.cpp")
target_include_directories(kf2_update_controller_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_update_controller_test COMMAND kf2_update_controller_test)

add_executable(kf2_github_release_client_test
    unit/github_release_client_test.cpp
    "${PROJECT_SOURCE_DIR}/src/update/github_release_client.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/release_notes.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/semantic_version.cpp")
target_include_directories(kf2_github_release_client_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_github_release_client_test PRIVATE
    KF2_RELEASE_REPOSITORY="https://github.com/example/KF2-Optimizer-Next")
target_link_libraries(kf2_github_release_client_test PRIVATE winhttp)
add_test(NAME kf2_github_release_client_test
    COMMAND kf2_github_release_client_test)

add_executable(kf2_update_state_test
    windows/update_state_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/update_state.cpp")
target_include_directories(kf2_update_state_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_update_state_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/update-state")
add_test(NAME kf2_update_state_test COMMAND kf2_update_state_test)

add_executable(kf2_update_transaction_test
    windows/update_transaction_test.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp
    ${PROJECT_SOURCE_DIR}/src/security/package_integrity.cpp
    ${PROJECT_SOURCE_DIR}/src/security/sha256.cpp
    ${PROJECT_SOURCE_DIR}/src/update/semantic_version.cpp
    ${PROJECT_SOURCE_DIR}/src/update/update_transaction.cpp
)
target_include_directories(kf2_update_transaction_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_definitions(kf2_update_transaction_test PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/update-transaction"
)
target_link_libraries(kf2_update_transaction_test PRIVATE bcrypt)
add_test(NAME kf2_update_transaction_test COMMAND kf2_update_transaction_test)

add_executable(kf2_release_notes_test
    unit/release_notes_test.cpp
    ${PROJECT_SOURCE_DIR}/src/update/release_notes.cpp
)
target_include_directories(kf2_release_notes_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
add_test(NAME kf2_release_notes_test COMMAND kf2_release_notes_test)

add_executable(kf2_update_package_test
    windows/update_package_test.cpp
    ${PROJECT_SOURCE_DIR}/src/security/package_integrity.cpp
    ${PROJECT_SOURCE_DIR}/src/security/sha256.cpp
    ${PROJECT_SOURCE_DIR}/src/update/github_release_client.cpp
    ${PROJECT_SOURCE_DIR}/src/update/release_notes.cpp
    ${PROJECT_SOURCE_DIR}/src/update/semantic_version.cpp
    ${PROJECT_SOURCE_DIR}/src/update/update_package.cpp
    ${PROJECT_SOURCE_DIR}/src/update/update_transaction.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp
)
target_include_directories(kf2_update_package_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_definitions(kf2_update_package_test PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    KF2_RELEASE_REPOSITORY="https://github.com/example/KF2-Optimizer-Next"
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/update-package"
)
target_link_libraries(kf2_update_package_test PRIVATE
    bcrypt winhttp shell32 ole32 oleaut32 uuid
)
add_test(NAME kf2_update_package_test COMMAND kf2_update_package_test)

add_executable(kf2_update_helper_test
    windows/update_helper_test.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp
    ${PROJECT_SOURCE_DIR}/src/security/package_integrity.cpp
    ${PROJECT_SOURCE_DIR}/src/security/sha256.cpp
    ${PROJECT_SOURCE_DIR}/src/update/semantic_version.cpp
    ${PROJECT_SOURCE_DIR}/src/update/update_helper.cpp
    ${PROJECT_SOURCE_DIR}/src/update/update_transaction.cpp
)
target_include_directories(kf2_update_helper_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_definitions(kf2_update_helper_test PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/update-helper"
)
target_link_libraries(kf2_update_helper_test PRIVATE bcrypt)
add_test(NAME kf2_update_helper_test COMMAND kf2_update_helper_test)

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
    KF2_VERSION="0.0.4-alpha"
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

add_executable(kf2_release_package_repair_test
    windows/release_package_repair_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/package_integrity.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/release_package_repair.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp"
)
target_include_directories(kf2_release_package_repair_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_release_package_repair_test PRIVATE
    KF2_RELEASE_REPOSITORY="${KF2_RELEASE_REPOSITORY}")
target_link_libraries(kf2_release_package_repair_test PRIVATE
    bcrypt ole32 oleaut32 shell32 winhttp)
add_test(NAME kf2_release_package_repair_test
    COMMAND kf2_release_package_repair_test)

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
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/session_guard.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp")
target_include_directories(kf2_session_config_guard_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_session_config_guard_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/session-config-guard")
target_link_libraries(kf2_session_config_guard_test PRIVATE bcrypt)
add_test(NAME kf2_session_config_guard_test COMMAND kf2_session_config_guard_test)

add_executable(kf2_frame_rate_cap_test
    integration/frame_rate_cap_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/frame_rate_cap.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp")
target_include_directories(kf2_frame_rate_cap_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_frame_rate_cap_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/frame-rate-cap")
add_test(NAME kf2_frame_rate_cap_test COMMAND kf2_frame_rate_cap_test)

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
    "${PROJECT_SOURCE_DIR}/src/app/application.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/application_package_repair.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/application_runtime.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/application_update.cpp"
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
    "${PROJECT_SOURCE_DIR}/src/game/frame_rate_cap.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_discovery.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/gameplay_log_lab.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/adaptive_control_client.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/offline_telemetry_lab.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_description.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_locator.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_parsing.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_telemetry.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/advanced_settings.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/video_settings.cpp"
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
    "${PROJECT_SOURCE_DIR}/src/optimizer/resource_pressure.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/startup_gpu_profile.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/optimizer_engine.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/presentmon_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/process_security.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/single_instance.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/window.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/package_integrity.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/release_package_repair.cpp"
    "${PROJECT_SOURCE_DIR}/src/security/sha256.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/gpu_metrics.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/present_source.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/system_metrics.cpp"
    "${PROJECT_SOURCE_DIR}/src/telemetry/telemetry_snapshot.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/github_release_client.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/release_notes.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/semantic_version.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/update_controller.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/update_helper.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/update_package.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/update_state.cpp"
    "${PROJECT_SOURCE_DIR}/src/update/update_transaction.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/automation_provider.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/direct2d_renderer.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_controller.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/tooltip_text.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_animation.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/numeric_presentation.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
)
target_include_directories(kf2_application_lifecycle_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
target_compile_definitions(kf2_application_lifecycle_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/application-lifecycle"
    KF2_VERSION="0.0.4-alpha"
    KF2_BUILD_COMMIT="unknown"
    KF2_BUILD_CHANNEL="dev"
    KF2_RELEASE_REPOSITORY="${KF2_RELEASE_REPOSITORY}"
    KF2_TELEMETRY_ASSET=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerTelemetry.u"
)
target_link_libraries(kf2_application_lifecycle_test PRIVATE
    presentmon_data
    user32 gdi32 shell32 ole32 oleaut32 uuid d2d1 dwrite windowscodecs winmm uiautomationcore bcrypt
    dwmapi psapi advapi32 pdh dxgi tdh version winhttp ws2_32
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

add_executable(kf2_numeric_presentation_test
    unit/numeric_presentation_test.cpp
    "${PROJECT_SOURCE_DIR}/src/ui/numeric_presentation.cpp"
)
target_include_directories(kf2_numeric_presentation_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_numeric_presentation_test
    COMMAND kf2_numeric_presentation_test)

add_executable(kf2_ui_animation_test
    unit/ui_animation_test.cpp
    "${PROJECT_SOURCE_DIR}/src/ui/ui_animation.cpp"
)
target_include_directories(kf2_ui_animation_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_ui_animation_test COMMAND kf2_ui_animation_test)

add_executable(kf2_ui_model_test
    unit/ui_model_test.cpp
    "${PROJECT_SOURCE_DIR}/src/ui/numeric_presentation.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
)
target_include_directories(kf2_ui_model_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_ui_model_test COMMAND kf2_ui_model_test)

add_executable(kf2_shell_layout_test
    unit/shell_layout_test.cpp
    "${PROJECT_SOURCE_DIR}/src/app/runtime/action_contract.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/numeric_presentation.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/tooltip_text.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme.cpp"
)
target_include_directories(kf2_shell_layout_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
)
add_test(NAME kf2_shell_layout_test COMMAND kf2_shell_layout_test)

add_executable(kf2_shell_controller_test
    unit/shell_controller_test.cpp
    "${PROJECT_SOURCE_DIR}/src/ui/numeric_presentation.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/tooltip_text.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_animation.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_controller.cpp"
)
target_include_directories(kf2_shell_controller_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_shell_controller_test COMMAND kf2_shell_controller_test)

add_executable(kf2_direct2d_renderer_test
    windows/direct2d_renderer_test.cpp
    "${PROJECT_SOURCE_DIR}/src/platform/windows/window.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/numeric_presentation.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/tooltip_text.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/theme.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_animation.cpp"
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
    "${PROJECT_SOURCE_DIR}/src/ui/numeric_presentation.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/ui_model.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/shell_layout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/tooltip_text.cpp"
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

add_executable(kf2_video_settings_test
    integration/video_settings_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/video_settings.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp")
target_include_directories(kf2_video_settings_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_video_settings_test PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/video-settings")
add_test(NAME kf2_video_settings_test COMMAND kf2_video_settings_test)

add_executable(kf2_advanced_settings_test
    integration/advanced_settings_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/advanced_settings.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp")
target_include_directories(kf2_advanced_settings_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_advanced_settings_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/advanced-settings")
add_test(NAME kf2_advanced_settings_test COMMAND kf2_advanced_settings_test)

add_executable(kf2_game_log_session_test
    unit/game_log_session_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/game_log_description.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_parsing.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_session.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/game_log_telemetry.cpp"
)
target_include_directories(kf2_game_log_session_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_game_log_session_test COMMAND kf2_game_log_session_test)

add_executable(kf2_game_log_locator_test
    windows/game_log_locator_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/game_log_locator.cpp"
)
target_include_directories(kf2_game_log_locator_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_game_log_locator_test PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/game-log-locator"
)
add_test(NAME kf2_game_log_locator_test COMMAND kf2_game_log_locator_test)

add_executable(kf2_gameplay_log_lab_test
    windows/gameplay_log_lab_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/gameplay_log_lab.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/adaptive_control_client.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/ini_document.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/windows/atomic_file.cpp"
)
target_include_directories(kf2_gameplay_log_lab_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(kf2_gameplay_log_lab_test PRIVATE
    KF2_TEST_ROOT=L"${KF2_TEST_SANDBOX_ROOT}/gameplay-log-lab"
    KF2_TELEMETRY_SOURCE=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerTelemetryProbe.uc"
    KF2_TELEMETRY_MUTATOR_SOURCE=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerTelemetryMutator.uc"
    KF2_TELEMETRY_INTERACTION_SOURCE=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerTelemetryInteraction.uc"
    KF2_ADAPTIVE_LISTENER_SOURCE=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerAdaptiveControlListener.uc"
    KF2_ADAPTIVE_CONNECTION_SOURCE=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerAdaptiveControlConnection.uc"
    KF2_ADAPTIVE_GRAPHICS_SOURCE=L"${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerAdaptiveGraphics.uc")
target_link_libraries(kf2_gameplay_log_lab_test PRIVATE bcrypt ws2_32)
add_test(NAME kf2_gameplay_log_lab_test COMMAND kf2_gameplay_log_lab_test)

add_executable(kf2_adaptive_control_client_test
    unit/adaptive_control_client_test.cpp
    "${PROJECT_SOURCE_DIR}/src/game/adaptive_control_client.cpp")
target_include_directories(kf2_adaptive_control_client_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_adaptive_control_client_test PRIVATE bcrypt ws2_32)
add_test(NAME kf2_adaptive_control_client_test
    COMMAND kf2_adaptive_control_client_test)

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
target_link_libraries(kf2_telemetry_frame_test PRIVATE pdh dxgi gdi32 advapi32)
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

add_test(NAME kf2_telemetry_build_binding_test
    COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/architecture/telemetry_build_binding_test.cmake")
set_tests_properties(kf2_telemetry_build_binding_test PROPERTIES
    LABELS "architecture;telemetry")

add_test(NAME kf2_contributor_package_flow_test
    COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/architecture/contributor_package_flow_test.cmake")
set_tests_properties(kf2_contributor_package_flow_test PROPERTIES
    LABELS "architecture;packaging")

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
    psapi dwmapi pdh dxgi gdi32 advapi32)
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
    psapi dwmapi pdh dxgi gdi32 advapi32)
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
    LABELS "requires-desktop"
    SKIP_RETURN_CODE 77)

add_executable(kf2_gpu_metrics_test
    windows/gpu_metrics_test.cpp
    "${PROJECT_SOURCE_DIR}/src/telemetry/gpu_metrics.cpp"
)
target_include_directories(kf2_gpu_metrics_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(kf2_gpu_metrics_test PRIVATE pdh dxgi gdi32 advapi32)
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

add_executable(kf2_startup_gpu_profile_test
    unit/startup_gpu_profile_test.cpp
    "${PROJECT_SOURCE_DIR}/src/optimizer/startup_gpu_profile.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/optimizer_engine.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
)
target_include_directories(kf2_startup_gpu_profile_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_startup_gpu_profile_test
    COMMAND kf2_startup_gpu_profile_test)

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
    "${PROJECT_SOURCE_DIR}/src/optimizer/resource_pressure.cpp"
    "${PROJECT_SOURCE_DIR}/src/optimizer/adaptive_registry.cpp"
    "${PROJECT_SOURCE_DIR}/src/config/kf2_catalog.cpp"
)
target_include_directories(kf2_adaptive_governor_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_adaptive_governor_test COMMAND kf2_adaptive_governor_test)
add_executable(kf2_resource_pressure_test
    unit/resource_pressure_test.cpp
    "${PROJECT_SOURCE_DIR}/src/optimizer/resource_pressure.cpp"
)
target_include_directories(kf2_resource_pressure_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include")
add_test(NAME kf2_resource_pressure_test COMMAND kf2_resource_pressure_test)
