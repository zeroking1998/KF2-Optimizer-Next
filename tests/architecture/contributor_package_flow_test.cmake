cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_SOURCE_DIR OR PROJECT_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "Missing PROJECT_SOURCE_DIR")
endif()

file(READ "${PROJECT_SOURCE_DIR}/tools/build_for_contributors.ps1" build_script)
file(READ "${PROJECT_SOURCE_DIR}/tools/package.ps1" package_script)

string(FIND "${package_script}"
    "\nAssert-PackagePathsNoReparsePoint $destinationRoot @($knownManagedPaths)"
    path_preflight)
string(FIND "${package_script}"
    "if (-not (Test-Path -LiteralPath $destinationRoot))" first_destination_mutation)
if(path_preflight EQUAL -1 OR first_destination_mutation EQUAL -1 OR
   path_preflight GREATER first_destination_mutation)
    message(FATAL_ERROR
        "Reject destination reparse points before any managed cleanup or copy")
endif()

string(FIND "${package_script}"
    "if (-not (Test-Path -LiteralPath $forwarder -PathType Leaf))"
    forwarder_check)
string(FIND "${package_script}"
    "Copy-Item -LiteralPath $forwarder -Destination" forwarder_copy)
if(forwarder_check EQUAL -1 OR forwarder_copy EQUAL -1 OR
   forwarder_check GREATER first_destination_mutation OR
   forwarder_check GREATER forwarder_copy)
    message(FATAL_ERROR
        "Check the required FleX build artifact before changing the package destination")
endif()

string(FIND "${package_script}"
    "\n& cmake --build (Join-Path $projectRoot 'out\\build\\windows-x64-release')"
    exporter_build)
if(exporter_build EQUAL -1)
    message(FATAL_ERROR
        "Packaging must incrementally build the exporter outside SkipBuild")
endif()
if(forwarder_check GREATER exporter_build)
    message(FATAL_ERROR
        "Reject a missing FleX artifact before unnecessary exporter work")
endif()

string(FIND "${package_script}"
    "--config Release --target KF2InventoryExport"
    exporter_target)
string(FIND "${package_script}"
    "if (-not (Test-Path -LiteralPath $destinationRoot))"
    destination_step)
if(exporter_target EQUAL -1 OR destination_step EQUAL -1 OR
   exporter_build GREATER exporter_target OR exporter_target GREATER destination_step)
    message(FATAL_ERROR
        "The current exporter must be built before the package destination changes")
endif()

string(FIND "${build_script}" "KF2InventoryExport" redundant_exporter_build)
string(FIND "${build_script}" "package.ps1') -SkipBuild" package_step)
if(NOT redundant_exporter_build EQUAL -1 OR package_step EQUAL -1)
    message(FATAL_ERROR
        "Contributor packaging must reuse the package-owned exporter prerequisite")
endif()

string(FIND "${package_script}" "$previousManagedFiles.Add($managed)" path_capture)
string(FIND "${package_script}" "foreach ($managed in $previousManagedFiles)" removal_pass)
string(FIND "${package_script}" "Remove-Item -LiteralPath $managed -Force" file_removal)
if(path_capture EQUAL -1 OR removal_pass EQUAL -1 OR file_removal EQUAL -1 OR
   path_capture GREATER removal_pass OR removal_pass GREATER file_removal)
    message(FATAL_ERROR
        "Validate every previous managed path before the separate removal pass")
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

execute_process(COMMAND "${TEST_POWERSHELL}" -NoProfile -File
    "${PROJECT_SOURCE_DIR}/tests/tools/repository_validation_test.ps1"
    -TestRoot "${TEST_BINARY_ROOT}/test-sandbox"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Repository validation regression failed: ${output}${error}")
endif()
message(STATUS "${output}")
