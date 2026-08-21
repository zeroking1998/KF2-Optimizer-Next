cmake_minimum_required(VERSION 3.25)

foreach(required PROJECT_SOURCE_DIR KF2_TELEMETRY_STAGE_SOURCES_PIPE
                 KF2_APP_TELEMETRY_SOURCES_PIPE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required architecture input: ${required}")
    endif()
endforeach()

string(REPLACE "|" ";" telemetry_stage_sources
       "${KF2_TELEMETRY_STAGE_SOURCES_PIPE}")
string(REPLACE "|" ";" app_telemetry_sources
       "${KF2_APP_TELEMETRY_SOURCES_PIPE}")

list(LENGTH telemetry_stage_sources telemetry_stage_count)
if(NOT telemetry_stage_count EQUAL 7)
    message(FATAL_ERROR
        "Telemetry architecture requires exactly seven stage .cpp files; found ${telemetry_stage_count}")
endif()
list(LENGTH app_telemetry_sources app_telemetry_count)
if(NOT app_telemetry_count EQUAL 8)
    message(FATAL_ERROR
        "Telemetry application source list requires one orchestrator plus seven stages; found ${app_telemetry_count}")
endif()

set(unique_stage_sources ${telemetry_stage_sources})
list(REMOVE_DUPLICATES unique_stage_sources)
list(LENGTH unique_stage_sources unique_stage_count)
if(NOT unique_stage_count EQUAL telemetry_stage_count)
    message(FATAL_ERROR "Duplicate telemetry stage source in CMake list")
endif()

set(expected_stage_names
    telemetry_adaptive_stage.cpp
    telemetry_collection_stage.cpp
    telemetry_effect_stage.cpp
    telemetry_frame.cpp
    telemetry_flex_stage.cpp
    telemetry_presentation_stage.cpp
    telemetry_session_stage.cpp)
set(actual_stage_names)
foreach(source IN LISTS telemetry_stage_sources)
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "Listed telemetry stage does not exist: ${source}")
    endif()
    get_filename_component(name "${source}" NAME)
    list(APPEND actual_stage_names "${name}")
endforeach()
list(SORT expected_stage_names)
list(SORT actual_stage_names)
if(NOT actual_stage_names STREQUAL expected_stage_names)
    message(FATAL_ERROR
        "Telemetry stage source list differs from the seven approved stages")
endif()

list(GET app_telemetry_sources 0 orchestrator)
if(NOT orchestrator STREQUAL
       "${PROJECT_SOURCE_DIR}/src/app/application_telemetry.cpp")
    message(FATAL_ERROR
        "The first application telemetry source must be the single orchestrator")
endif()
set(app_stage_sources ${app_telemetry_sources})
list(REMOVE_AT app_stage_sources 0)
list(SORT app_stage_sources)
set(sorted_stage_sources ${telemetry_stage_sources})
list(SORT sorted_stage_sources)
if(NOT app_stage_sources STREQUAL sorted_stage_sources)
    message(FATAL_ERROR
        "Application telemetry sources contain an unlisted or missing stage")
endif()

file(GLOB stage_headers
     "${PROJECT_SOURCE_DIR}/src/features/telemetry/*_stage.hpp")
foreach(header IN LISTS stage_headers)
    file(READ "${header}" header_text)
    string(FIND "${header_text}" "application_runtime.hpp" forbidden_header)
    if(NOT forbidden_header EQUAL -1)
        message(FATAL_ERROR
            "Stage header must forward-declare UiRuntime: ${header}")
    endif()
endforeach()

function(reject_literals source role)
    file(READ "${source}" source_text)
    foreach(literal IN LISTS ARGN)
        string(FIND "${source_text}" "${literal}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "${role} contains forbidden dependency '${literal}': ${source}")
        endif()
    endforeach()
endfunction()

reject_literals("${orchestrator}" "Telemetry orchestrator"
    "PresentMonSession::start"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "game_log_session_parser"
    "write_adaptive_control"
    "adaptive_governor.evaluate"
    "atomic_replace_utf8"
    "evaluate_overlay"
    "->sample()")
file(READ "${orchestrator}" orchestrator_text)
string(FIND "${orchestrator_text}" "run_ordered_telemetry_pipeline" ordered_call)
if(ordered_call EQUAL -1)
    message(FATAL_ERROR
        "Telemetry orchestrator must use the ordered pipeline contract")
endif()

set(stage_root "${PROJECT_SOURCE_DIR}/src/features/telemetry")
reject_literals("${stage_root}/telemetry_collection_stage.cpp"
    "Collection stage"
    "write_adaptive_control"
    "atomic_replace_utf8"
    "evaluate_overlay"
    "apply_adaptive_profile_effect"
    "update_overlay_scene_gate")
reject_literals("${stage_root}/telemetry_adaptive_stage.cpp"
    "Adaptive stage"
    "PresentMonSession"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "evaluate_overlay"
    "overlay_window"
    "game_log_session_parser")
reject_literals("${stage_root}/telemetry_presentation_stage.cpp"
    "Presentation stage"
    "PresentMonSession"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "game_log_session_parser"
    "write_adaptive_control"
    "atomic_replace_utf8"
    "restore_protected_session_config")
reject_literals("${stage_root}/telemetry_effect_stage.cpp"
    "Effect stage"
    "PresentMonSession::start"
    "PresentSource"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "->drain(")

file(READ "${stage_root}/telemetry_session_stage.cpp" session_stage_text)
string(FIND "${session_stage_text}"
    "game_window = found_window.value()" visible_window_bound)
string(FIND "${session_stage_text}"
    "session_config_waiting_for_launch = false" launch_wait_cleared)
if(visible_window_bound EQUAL -1 OR launch_wait_cleared EQUAL -1 OR
   NOT visible_window_bound LESS launch_wait_cleared)
    message(FATAL_ERROR
        "Protected launch wait must survive transient pre-window KFGame processes")
endif()

foreach(non_session_stage
        telemetry_collection_stage.cpp
        telemetry_adaptive_stage.cpp
        telemetry_effect_stage.cpp
        telemetry_flex_stage.cpp
        telemetry_presentation_stage.cpp)
    reject_literals("${stage_root}/${non_session_stage}"
        "Non-session telemetry stage"
        "game_log_path"
        "game_log_offset"
        "game_log_bound_to_process")
endforeach()

file(READ "${stage_root}/telemetry_flex_stage.cpp" flex_stage_text)
string(FIND "${flex_stage_text}"
    "AdaptiveReceiptResult::accepted" accepted_flex_receipt)
string(FIND "${flex_stage_text}"
    "FLEX_ADAPTIVE_APPLIED" durable_flex_applied_event)
if(accepted_flex_receipt EQUAL -1 OR durable_flex_applied_event EQUAL -1 OR
   NOT accepted_flex_receipt LESS durable_flex_applied_event)
    message(FATAL_ERROR
        "FleX APPLIED diagnostics must follow an accepted shared-memory receipt")
endif()

message(STATUS
    "Telemetry pipeline boundary verified: one orchestrator and seven directional stages")
