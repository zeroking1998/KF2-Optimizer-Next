cmake_minimum_required(VERSION 3.25)

foreach(required PROJECT_SOURCE_DIR KF2_FEATURE_ACTION_SOURCES_PIPE
                 KF2_APP_ACTION_SOURCES_PIPE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing architecture input: ${required}")
    endif()
endforeach()

file(REAL_PATH "${PROJECT_SOURCE_DIR}" project_root)
string(REPLACE "\\" "/" project_root "${project_root}")
string(REPLACE "|" ";" feature_sources
       "${KF2_FEATURE_ACTION_SOURCES_PIPE}")
string(REPLACE "|" ";" app_sources "${KF2_APP_ACTION_SOURCES_PIPE}")

set(feature_names
    backup diagnostics game navigation optimizer overlay settings)
list(LENGTH feature_sources feature_source_count)
if(NOT feature_source_count EQUAL 7)
    message(FATAL_ERROR
        "Expected exactly seven feature action sources, got ${feature_source_count}")
endif()

set(canonical_feature_sources)
foreach(feature IN LISTS feature_names)
    set(expected
        "${project_root}/src/features/${feature}/${feature}_actions.cpp")
    file(REAL_PATH "${expected}" expected_real)
    string(REPLACE "\\" "/" expected_real "${expected_real}")
    list(APPEND canonical_feature_sources "${expected_real}")
endforeach()

set(actual_feature_sources)
foreach(source IN LISTS feature_sources)
    if(NOT IS_ABSOLUTE "${source}" OR source MATCHES "[/\\]\.\.?[/\\]")
        message(FATAL_ERROR "Feature source is not canonical: ${source}")
    endif()
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "Feature source does not exist: ${source}")
    endif()
    file(REAL_PATH "${source}" source_real)
    string(REPLACE "\\" "/" source_real "${source_real}")
    list(APPEND actual_feature_sources "${source_real}")
endforeach()
set(unique_feature_sources ${actual_feature_sources})
list(REMOVE_DUPLICATES unique_feature_sources)
list(LENGTH unique_feature_sources unique_feature_count)
if(NOT unique_feature_count EQUAL 7)
    message(FATAL_ERROR "Feature source list contains duplicates")
endif()
foreach(expected IN LISTS canonical_feature_sources)
    list(FIND actual_feature_sources "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Missing canonical feature source: ${expected}")
    endif()
endforeach()

list(LENGTH app_sources app_source_count)
if(NOT app_source_count EQUAL 12)
    message(FATAL_ERROR
        "Shared app action list must contain five runtime plus seven feature sources")
endif()
set(unique_app_sources ${app_sources})
list(REMOVE_DUPLICATES unique_app_sources)
list(LENGTH unique_app_sources unique_app_count)
if(NOT unique_app_count EQUAL app_source_count)
    message(FATAL_ERROR "Shared app action source list contains duplicates")
endif()
foreach(feature_source IN LISTS actual_feature_sources)
    set(matches 0)
    foreach(app_source IN LISTS app_sources)
        file(REAL_PATH "${app_source}" app_source_real)
        string(REPLACE "\\" "/" app_source_real "${app_source_real}")
        if(app_source_real STREQUAL feature_source)
            math(EXPR matches "${matches} + 1")
        endif()
    endforeach()
    if(NOT matches EQUAL 1)
        message(FATAL_ERROR
            "Feature source must occur exactly once in shared app list: ${feature_source}")
    endif()
endforeach()

file(READ "${project_root}/CMakeLists.txt" product_cmake)
file(READ "${project_root}/tests/CMakeLists.txt" tests_cmake)
string(FIND "${product_cmake}" "\${KF2_APP_ACTION_SOURCES}" product_shared)
string(FIND "${tests_cmake}" "\${KF2_APP_ACTION_SOURCES}" lifecycle_shared)
if(product_shared EQUAL -1 OR lifecycle_shared EQUAL -1)
    message(FATAL_ERROR
        "Product and lifecycle test must both consume KF2_APP_ACTION_SOURCES")
endif()

file(GLOB_RECURSE production_files
    "${project_root}/src/*.cpp" "${project_root}/src/*.hpp")
set(forbidden_legacy_tokens
    execute_legacy_action MigrationActionHandler allow_partial)
foreach(source IN LISTS production_files)
    file(READ "${source}" content)
    foreach(token IN LISTS forbidden_legacy_tokens)
        string(FIND "${content}" "${token}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "Forbidden migration token '${token}' remains in ${source}")
        endif()
    endforeach()
endforeach()

set(contract_path "${project_root}/src/app/runtime/action_contract.cpp")
file(READ "${contract_path}" contract_content)
string(REGEX MATCHALL "\"[a-z][a-z0-9-]+\"" contract_literals
       "${contract_content}")
set(action_literals)
foreach(literal IN LISTS contract_literals)
    string(REGEX REPLACE "^\"|\"$" "" name "${literal}")
    if(name MATCHES
       "^(dashboard|diagnostics|game|header|optimizer|overlay|settings)-" AND
       NOT name MATCHES "-slider$")
        list(APPEND action_literals "${name}")
    endif()
endforeach()
list(REMOVE_DUPLICATES action_literals)

foreach(source IN LISTS production_files)
    string(REPLACE "\\" "/" normalized_source "${source}")
    if(normalized_source STREQUAL contract_path OR
       normalized_source MATCHES "/src/ui/")
        continue()
    endif()
    file(READ "${source}" content)
    foreach(action_name IN LISTS action_literals)
        string(FIND "${content}" "\"${action_name}\"" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "Raw action literal '${action_name}' escaped the contract/UI boundary: ${source}")
        endif()
    endforeach()
endforeach()

foreach(feature IN LISTS feature_names)
    file(GLOB feature_files
        "${project_root}/src/features/${feature}/*.cpp"
        "${project_root}/src/features/${feature}/*.hpp")
    foreach(source IN LISTS feature_files)
        file(READ "${source}" content)
        string(REGEX MATCHALL
            "#include[ \t]+\"features/[a-z]+/[^\"]+\""
            feature_includes "${content}")
        foreach(include_line IN LISTS feature_includes)
            string(FIND "${include_line}" "\"features/${feature}/" own)
            if(own EQUAL -1)
                message(FATAL_ERROR
                    "Cross-feature include in ${source}: ${include_line}")
            endif()
        endforeach()
    endforeach()
endforeach()

# ShellExecuteExW can re-enter the UI thread while Steam confirms custom launch
# arguments. The protected-session wait must already be armed at that point.
set(game_actions
    "${project_root}/src/features/game/game_actions.cpp")
file(READ "${game_actions}" game_actions_content)
string(FIND "${game_actions_content}"
    "runtime.session_config_waiting_for_launch = true" launch_wait_armed)
string(FIND "${game_actions_content}"
    "ShellExecuteExW(&launch_request)" shell_execute)
string(FIND "${game_actions_content}"
    "should_prepare_protected_gameplay_provider" protected_provider_policy)
string(FIND "${game_actions_content}"
    "if (protected_gameplay_provider)" protected_provider_install)
if(launch_wait_armed EQUAL -1 OR shell_execute EQUAL -1 OR
   NOT launch_wait_armed LESS shell_execute)
    message(FATAL_ERROR
        "Protected launch wait must be armed before re-entrant ShellExecuteExW")
endif()
if(protected_provider_policy EQUAL -1 OR protected_provider_install EQUAL -1)
    message(FATAL_ERROR
        "Normal Adaptive launch must use the protected gameplay-provider policy")
endif()

message(STATUS
    "Action architecture verified: 7 feature sources, one shared source list, no migration or raw action-name drift")
