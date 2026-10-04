cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_SOURCE_DIR OR PROJECT_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "Missing PROJECT_SOURCE_DIR")
endif()

file(READ "${PROJECT_SOURCE_DIR}/tools/build_for_contributors.ps1" build_script)

string(FIND "${build_script}"
    "--config Release --target KF2InventoryExport"
    exporter_build)
if(exporter_build EQUAL -1)
    message(FATAL_ERROR
        "Contributor packaging must build the inventory exporter explicitly")
endif()

string(FIND "${build_script}"
    "package.ps1') -SkipBuild"
    package_step)
if(package_step EQUAL -1 OR exporter_build GREATER package_step)
    message(FATAL_ERROR
        "The inventory exporter must be built before packaging starts")
endif()

string(REGEX MATCH
    "build_kf2_telemetry\\.ps1[^\n]*\n[ \t]*if \\(\\$LASTEXITCODE"
    stale_nested_exit_check "${build_script}")
if(NOT stale_nested_exit_check STREQUAL "")
    message(FATAL_ERROR
        "Contributor packaging must not read a stale native exit code after a successful PowerShell telemetry build")
endif()

execute_process(COMMAND "${TEST_POWERSHELL}" -NoProfile -File
    "${PROJECT_SOURCE_DIR}/tests/tools/release_source_identity_test.ps1"
    -Executable "${TEST_EXECUTABLE}" -TestRoot "${TEST_BINARY_ROOT}/test-sandbox"
    -ExpectedBuildIdentity "${EXPECTED_BUILD_IDENTITY}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Release source identity regression failed: ${output}${error}")
endif()
message(STATUS "${output}")
