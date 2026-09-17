cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_SOURCE_DIR OR PROJECT_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "Missing PROJECT_SOURCE_DIR")
endif()

file(READ "${PROJECT_SOURCE_DIR}/CMakeLists.txt" project_cmake)

string(FIND "${project_cmake}"
    "include(CheckIPOSupported)" ipo_module)
if(ipo_module EQUAL -1)
    message(FATAL_ERROR "Release IPO must be capability-checked")
endif()

string(FIND "${project_cmake}"
    "check_ipo_supported(" ipo_check)
if(ipo_check EQUAL -1)
    message(FATAL_ERROR "Release IPO support must be verified at configure time")
endif()

string(CONCAT target_release_ipo_contract
    "set_property(TARGET KF2Optimizer PROPERTY\n"
    "        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)")
string(FIND "${project_cmake}" "${target_release_ipo_contract}"
    target_release_ipo)
if(target_release_ipo EQUAL -1)
    message(FATAL_ERROR
        "IPO must be enabled only for the KF2Optimizer Release target")
endif()

string(FIND "${project_cmake}"
    "$<$<CONFIG:Release>:/LTCG>" release_ltcg)
if(release_ltcg EQUAL -1)
    message(FATAL_ERROR
        "MSVC Release linking must use full LTCG for reproducible output")
endif()

string(FIND "${project_cmake}"
    "CMAKE_INTERPROCEDURAL_OPTIMIZATION" global_ipo)
if(NOT global_ipo EQUAL -1)
    message(FATAL_ERROR
        "IPO must not be enabled globally for tests or UnrealScript tooling")
endif()
