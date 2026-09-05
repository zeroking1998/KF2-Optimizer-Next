cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_SOURCE_DIR OR PROJECT_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "Missing PROJECT_SOURCE_DIR")
endif()

file(READ "${PROJECT_SOURCE_DIR}/CMakeLists.txt" product_cmake)
file(READ "${PROJECT_SOURCE_DIR}/tools/build.ps1" build_script)

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
