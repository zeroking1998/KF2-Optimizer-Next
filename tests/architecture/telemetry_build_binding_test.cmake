cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_SOURCE_DIR OR PROJECT_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "Missing PROJECT_SOURCE_DIR")
endif()

file(READ "${PROJECT_SOURCE_DIR}/CMakeLists.txt" product_cmake)
string(REGEX MATCH
    "add_compile_definitions\\([^\\)]*KF2_OFFLINE_TELEMETRY_SHA256"
    global_module_hash "${product_cmake}")
if(NOT global_module_hash STREQUAL "")
    message(FATAL_ERROR
        "The SDK module hash must not change unrelated translation units' compiler options")
endif()
set(binding_helper "${PROJECT_SOURCE_DIR}/tools/telemetry_binding.cmake")
file(READ "${binding_helper}" binding_cmake)
file(READ "${PROJECT_SOURCE_DIR}/tools/build_kf2_telemetry.ps1"
    telemetry_build_script)
file(READ "${PROJECT_SOURCE_DIR}/tools/package.ps1" package_script)

string(REGEX MATCH
    "set\\(KF2_OFFLINE_TELEMETRY_SHA256[^\\)]*FORCE"
    forced_telemetry_hash "${product_cmake}${binding_cmake}")
if(NOT forced_telemetry_hash STREQUAL "")
    message(FATAL_ERROR
        "CMake must not override the telemetry hash supplied by the build script")
endif()

string(FIND "${product_cmake}"
    "include(\"\${PROJECT_SOURCE_DIR}/tools/telemetry_binding.cmake\")"
    central_binding)
if(central_binding EQUAL -1)
    message(FATAL_ERROR
        "Direct CMake must use the shared telemetry binding")
endif()
foreach(entry IN ITEMS build validate_foundation validate_ninja validate_gui build_pgo)
    file(READ "${PROJECT_SOURCE_DIR}/tools/${entry}.ps1" entry_script)
    string(FIND "${entry_script}"
        "-DKF2_OFFLINE_TELEMETRY_SHA256=AUTO" auto_binding)
    if(auto_binding EQUAL -1)
        message(FATAL_ERROR "${entry}.ps1 must select the shared AUTO binding")
    endif()
endforeach()

string(FIND "${telemetry_build_script}"
    "get_telemetry_source_fingerprint.ps1" build_source_fingerprint)
string(FIND "${telemetry_build_script}"
    "sourceFingerprintPath" build_fingerprint_stamp)
if(build_source_fingerprint EQUAL -1 OR build_fingerprint_stamp EQUAL -1)
    message(FATAL_ERROR
        "Telemetry compilation must stamp the exact UnrealScript source fingerprint")
endif()

# Exercise the real helper in a tiny configure/build project without MSVC
# compilation. Each run owns a new sandbox; no real SDK artifact is changed.
if(NOT DEFINED TEST_ROOT OR NOT DEFINED TEST_BINARY_ROOT OR
   NOT DEFINED TEST_GENERATOR)
    message(FATAL_ERROR "Missing telemetry binding test sandbox or generator")
endif()
cmake_path(ABSOLUTE_PATH TEST_ROOT NORMALIZE OUTPUT_VARIABLE sandbox_root)
cmake_path(ABSOLUTE_PATH TEST_BINARY_ROOT NORMALIZE OUTPUT_VARIABLE allowed_root)
cmake_path(IS_PREFIX allowed_root "${sandbox_root}" NORMALIZE inside_build_root)
if(NOT inside_build_root OR sandbox_root STREQUAL allowed_root)
    message(FATAL_ERROR "Telemetry binding fixture must remain below the test build root")
endif()
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef nonce)
set(source_root "${sandbox_root}/${nonce}/source")
set(build_root "${sandbox_root}/${nonce}/build")
set(module "${source_root}/assets/offline_telemetry/KF2OptimizerTelemetry.u")
file(MAKE_DIRECTORY "${source_root}/assets/offline_telemetry")
file(WRITE "${source_root}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.28)\n"
    "project(TelemetryBindingFixture LANGUAGES NONE)\n"
    "include(\"${binding_helper}\")\n"
    "file(WRITE \"\${CMAKE_BINARY_DIR}/bound-hash.txt\" \"\${KF2_OFFLINE_TELEMETRY_SHA256}\")\n"
    "add_custom_target(binding_probe ALL COMMAND \"\${CMAKE_COMMAND}\" -E true)\n")

set(generator_arguments -G "${TEST_GENERATOR}")
if(NOT TEST_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND generator_arguments -A "${TEST_GENERATOR_PLATFORM}")
endif()
if(NOT TEST_MAKE_PROGRAM STREQUAL "")
    list(APPEND generator_arguments "-DCMAKE_MAKE_PROGRAM=${TEST_MAKE_PROGRAM}")
endif()

function(check_hash expected)
    file(READ "${build_root}/bound-hash.txt" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Telemetry binding mismatch: ${actual} != ${expected}")
    endif()
endfunction()

function(configure_binding pin expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source_root}" -B "${build_root}"
        ${generator_arguments} "-DKF2_OFFLINE_TELEMETRY_SHA256=${pin}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Telemetry fixture configuration failed: ${output}${error}")
    endif()
    check_hash("${expected}")
endfunction()

function(build_binding expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build_root}"
        --config Debug --target binding_probe
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Telemetry fixture rebuild failed: ${output}${error}")
    endif()
    check_hash("${expected}")
endfunction()

function(reject_pin pin expected_error)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source_root}" -B "${build_root}"
        ${generator_arguments} "-DKF2_OFFLINE_TELEMETRY_SHA256=${pin}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    string(FIND "${output}${error}" "${expected_error}" expected_failure)
    if(result EQUAL 0 OR expected_failure EQUAL -1)
        message(FATAL_ERROR "Bad telemetry pin was not rejected correctly: ${output}${error}")
    endif()
endfunction()

set(fixture_hash "589aa708392e2c26abc753ce272c6e146f274623181015e8f6bdc201ccb8e2f0")
# Independent SHA-256 known vectors, not another call to the tested hash code.
set(abc_hash "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
set(empty_hash "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
configure_binding(AUTO "${fixture_hash}")
file(WRITE "${module}" "abc")
build_binding("${abc_hash}")
# Allow coarse Windows/build-tool timestamps before changing existing bytes.
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
file(WRITE "${module}" "")
build_binding("${empty_hash}")
string(TOUPPER "${empty_hash}" uppercase_hash)
configure_binding("${uppercase_hash}" "${empty_hash}")
string(REPEAT "0" 64 wrong_hash)
reject_pin("${wrong_hash}" "does not match the current telemetry module")
reject_pin("invalid" "must be AUTO or exactly 64")
configure_binding(AUTO "${empty_hash}")

string(FIND "${telemetry_build_script}" "'-debug'" debug_compile_flag)
string(FIND "${telemetry_build_script}" "'-final_release'"
    final_release_compile_flag)
if(NOT debug_compile_flag EQUAL -1)
    message(FATAL_ERROR
        "Telemetry compilation must not ship optional UnrealScript debug metadata")
endif()
if(NOT final_release_compile_flag EQUAL -1)
    message(FATAL_ERROR
        "Telemetry compilation must preserve the KF2OPT runtime log protocol")
endif()

string(FIND "${package_script}"
    "get_telemetry_source_fingerprint.ps1" package_source_fingerprint)
string(FIND "${package_script}"
    "storedTelemetryFingerprint -ne $expectedTelemetryFingerprint"
    package_stale_guard)
string(FIND "${package_script}"
    "build_kf2_telemetry.ps1" package_telemetry_rebuild)
if(package_source_fingerprint EQUAL -1 OR package_stale_guard EQUAL -1 OR
   package_telemetry_rebuild EQUAL -1)
    message(FATAL_ERROR
        "Packaging must rebuild telemetry when its source fingerprint is stale")
endif()
