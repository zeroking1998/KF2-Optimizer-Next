cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_SOURCE_DIR OR PROJECT_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "Missing PROJECT_SOURCE_DIR")
endif()

file(READ "${PROJECT_SOURCE_DIR}/CMakeLists.txt" product_cmake)
file(READ "${PROJECT_SOURCE_DIR}/tools/build.ps1" build_script)
file(READ "${PROJECT_SOURCE_DIR}/tools/build_kf2_telemetry.ps1"
    telemetry_build_script)
file(READ "${PROJECT_SOURCE_DIR}/tools/package.ps1" package_script)

string(REGEX MATCH
    "set\\(KF2_OFFLINE_TELEMETRY_SHA256[^\\)]*FORCE"
    forced_telemetry_hash "${product_cmake}")
if(NOT forced_telemetry_hash STREQUAL "")
    message(FATAL_ERROR
        "CMake must not override the telemetry hash supplied by the build script")
endif()

string(FIND "${build_script}"
    "-DKF2_OFFLINE_TELEMETRY_SHA256=$telemetryHash"
    build_hash_binding)
if(build_hash_binding EQUAL -1)
    message(FATAL_ERROR
        "The build script must bind the application to the current telemetry module")
endif()

string(FIND "${telemetry_build_script}"
    "get_telemetry_source_fingerprint.ps1" build_source_fingerprint)
string(FIND "${telemetry_build_script}"
    "sourceFingerprintPath" build_fingerprint_stamp)
if(build_source_fingerprint EQUAL -1 OR build_fingerprint_stamp EQUAL -1)
    message(FATAL_ERROR
        "Telemetry compilation must stamp the exact UnrealScript source fingerprint")
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
