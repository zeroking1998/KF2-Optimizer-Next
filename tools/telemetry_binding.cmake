set(KF2_OFFLINE_TELEMETRY_SHA256 "AUTO" CACHE STRING
    "AUTO or the expected SHA-256 of the offline telemetry module")
set(KF2_TELEMETRY_MODULE
    "${PROJECT_SOURCE_DIR}/assets/offline_telemetry/KF2OptimizerTelemetry.u")

# Track both SDK-less -> compiled transitions and changes to existing bytes.
file(GLOB KF2_TELEMETRY_MODULE_INPUT CONFIGURE_DEPENDS "${KF2_TELEMETRY_MODULE}")
if(KF2_TELEMETRY_MODULE_INPUT)
    file(SHA256 "${KF2_TELEMETRY_MODULE}" KF2_TELEMETRY_MODULE_HASH)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${KF2_TELEMETRY_MODULE}")
else()
    # SDK-less developer builds use the existing test-fixture checksum only;
    # packaging still requires the source-verified, SDK-compiled module.
    set(KF2_TELEMETRY_MODULE_HASH
        "589aa708392e2c26abc753ce272c6e146f274623181015e8f6bdc201ccb8e2f0")
endif()

if(KF2_OFFLINE_TELEMETRY_SHA256 STREQUAL "AUTO")
    # Keep AUTO in the cache so later module builds cannot retain a stale pin.
    set(KF2_OFFLINE_TELEMETRY_SHA256 "${KF2_TELEMETRY_MODULE_HASH}")
else()
    string(LENGTH "${KF2_OFFLINE_TELEMETRY_SHA256}" KF2_TELEMETRY_HASH_LENGTH)
    if(NOT KF2_TELEMETRY_HASH_LENGTH EQUAL 64 OR
       NOT KF2_OFFLINE_TELEMETRY_SHA256 MATCHES "^[0-9A-Fa-f]+$")
        message(FATAL_ERROR
            "KF2_OFFLINE_TELEMETRY_SHA256 must be AUTO or exactly 64 hexadecimal characters")
    endif()
    string(TOLOWER "${KF2_OFFLINE_TELEMETRY_SHA256}" KF2_OFFLINE_TELEMETRY_SHA256)
    if(KF2_TELEMETRY_MODULE_INPUT AND
       NOT KF2_OFFLINE_TELEMETRY_SHA256 STREQUAL KF2_TELEMETRY_MODULE_HASH)
        message(FATAL_ERROR
            "KF2_OFFLINE_TELEMETRY_SHA256 does not match the current telemetry module; use AUTO or an exact pin")
    endif()
endif()
