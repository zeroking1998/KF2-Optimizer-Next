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
