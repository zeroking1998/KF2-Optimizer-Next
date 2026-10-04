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
if(NOT telemetry_stage_count EQUAL 8)
    message(FATAL_ERROR
        "Telemetry architecture requires exactly eight stage modules; found ${telemetry_stage_count}")
endif()
list(LENGTH app_telemetry_sources app_telemetry_count)
if(NOT app_telemetry_count EQUAL 9)
    message(FATAL_ERROR
        "Telemetry application source list requires one orchestrator plus eight stage modules; found ${app_telemetry_count}")
endif()

set(unique_stage_sources ${telemetry_stage_sources})
list(REMOVE_DUPLICATES unique_stage_sources)
list(LENGTH unique_stage_sources unique_stage_count)
if(NOT unique_stage_count EQUAL telemetry_stage_count)
    message(FATAL_ERROR "Duplicate telemetry stage source in CMake list")
endif()

set(expected_stage_names
    telemetry_adaptive_controller.cpp
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
        "Telemetry stage source list differs from the eight approved modules")
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
    "DxgiFrameTimingSession::start"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "game_log_session_parser"
    "write_fixed_control"
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
    "query_system_memory_metrics"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "->sample()"
    "resource_telemetry_worker.request"
    "resource_telemetry_worker.latest"
    "write_fixed_control"
    "atomic_replace_utf8"
    "evaluate_overlay"
    "apply_adaptive_profile_effect"
    "update_overlay_scene_gate")
reject_literals("${stage_root}/telemetry_adaptive_stage.cpp"
    "Adaptive stage"
    "DxgiFrameTimingSession"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "evaluate_overlay"
    "overlay_window"
    "game_log_session_parser")
reject_literals("${stage_root}/telemetry_adaptive_controller.cpp"
    "Adaptive controller"
    "DxgiFrameTimingSession"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "evaluate_overlay"
    "overlay_window"
    "game_log_session_parser")
reject_literals("${stage_root}/telemetry_presentation_stage.cpp"
    "Presentation stage"
    "DxgiFrameTimingSession"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "game_log_session_parser"
    "write_fixed_control"
    "atomic_replace_utf8"
    "restore_protected_session_config")
reject_literals("${stage_root}/telemetry_effect_stage.cpp"
    "Effect stage"
    "DxgiFrameTimingSession::start"
    "PresentSource"
    "ProcessMetricSampler"
    "PdhGpuSampler"
    "NvidiaGpuSampler"
    "->drain(")

file(READ "${stage_root}/telemetry_session_stage.cpp" session_stage_text)
string(FIND "${session_stage_text}"
    "void UiRuntime::detach_telemetry" detach_start)
string(FIND "${session_stage_text}"
    "void UiRuntime::begin_game_restart_handoff" detach_end)
if(detach_start EQUAL -1 OR detach_end EQUAL -1 OR
   NOT detach_start LESS detach_end)
    message(FATAL_ERROR
        "Session stage must retain an inspectable telemetry detach boundary")
endif()
math(EXPR detach_length "${detach_end} - ${detach_start}")
string(SUBSTRING "${session_stage_text}" ${detach_start} ${detach_length}
    detach_text)
string(FIND "${detach_text}"
    "adaptive_control_sequence = 0" recoverable_sequence_reset)
if(NOT recoverable_sequence_reset EQUAL -1)
    message(FATAL_ERROR
        "Recoverable telemetry detach must preserve the authenticated command sequence")
endif()
string(FIND "${orchestrator_text}"
    "runtime_.detach_telemetry()" rejected_frame_detach)
string(FIND "${orchestrator_text}"
    "void UiRuntime::system_resume()" resume_start)
if(resume_start EQUAL -1)
    set(resume_text "")
else()
    string(SUBSTRING "${orchestrator_text}" ${resume_start} -1 resume_text)
endif()
string(FIND "${resume_text}" "detach_telemetry()" resume_detach)
if(rejected_frame_detach EQUAL -1 OR resume_detach EQUAL -1)
    message(FATAL_ERROR
        "Rejected frames and system resume must share the recoverable detach path")
endif()
file(READ "${stage_root}/telemetry_effect_stage.cpp" effect_stage_text)
string(FIND "${effect_stage_text}"
    "adaptive_control_token.clear()" final_token_reset)
string(FIND "${effect_stage_text}"
    "adaptive_control_sequence = 0" final_sequence_reset)
if(final_token_reset EQUAL -1 OR final_sequence_reset EQUAL -1)
    message(FATAL_ERROR
        "Final protected-session teardown must reset both token and sequence")
endif()
foreach(forbidden_sync_log_call
        "find_active_game_log"
        "CreateFileW"
        "std::ifstream"
        "game_log_session_parser.feed"
        "expire_observations"
        "chunk.bytes"
        "parse_game_menu_graphics_readback"
        "map_prewarm_request_from_log_line"
        "game_log_reports_engine_exit"
        "game_log_requests_settings_restart")
    string(FIND "${session_stage_text}"
        "${forbidden_sync_log_call}" forbidden_sync_log_offset)
    if(NOT forbidden_sync_log_offset EQUAL -1)
        message(FATAL_ERROR
            "Session stage performs raw or structured log work on the UI thread: ${forbidden_sync_log_call}")
    endif()
endforeach()
string(FIND "${session_stage_text}"
    "resource_telemetry_worker.request" worker_request)
string(FIND "${session_stage_text}"
    "take_game_log_chunks" worker_log_result)
if(worker_request EQUAL -1 OR worker_log_result EQUAL -1)
    message(FATAL_ERROR
        "Session stage must request and consume the desktop telemetry worker")
endif()
file(READ "${PROJECT_SOURCE_DIR}/src/telemetry/resource_telemetry_worker.cpp"
    resource_worker_text)
string(FIND "${resource_worker_text}"
    "log_parser.feed" worker_log_parse)
string(FIND "${resource_worker_text}"
    "log_parser.expire_observations" worker_log_expiration)
string(FIND "${resource_worker_text}"
    "log_boundaries.feed" worker_boundary_parse)
if(worker_log_parse EQUAL -1 OR worker_log_expiration EQUAL -1 OR
   worker_boundary_parse EQUAL -1)
    message(FATAL_ERROR
        "Desktop telemetry worker must own session, boundary and expiration parsing")
endif()

# Gameplay publications cross every telemetry stage by immutable shared
# identity. Reintroducing an owning optional here silently restores several
# deep string/catalog copies on every 120-ms frame.
foreach(snapshot_owner
        "${stage_root}/telemetry_frame.hpp"
        "${PROJECT_SOURCE_DIR}/src/app/application_runtime.hpp"
        "${PROJECT_SOURCE_DIR}/include/kf2/telemetry/resource_telemetry_worker.hpp")
    file(READ "${snapshot_owner}" snapshot_owner_text)
    string(FIND "${snapshot_owner_text}"
        "GameLogSessionSnapshot" immutable_snapshot)
    string(FIND "${snapshot_owner_text}"
        "optional<game::GameLogSession>" mutable_snapshot_copy)
    if(immutable_snapshot EQUAL -1 OR NOT mutable_snapshot_copy EQUAL -1)
        message(FATAL_ERROR
            "Telemetry gameplay owners must share immutable snapshots: ${snapshot_owner}")
    endif()
endforeach()
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
        telemetry_adaptive_controller.cpp
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
    "FLEX_MINIMUM_APPLIED" durable_flex_applied_event)
if(accepted_flex_receipt EQUAL -1 OR durable_flex_applied_event EQUAL -1 OR
   NOT accepted_flex_receipt LESS durable_flex_applied_event)
    message(FATAL_ERROR
        "FleX APPLIED diagnostics must follow an accepted shared-memory receipt")
endif()

string(FIND "${flex_stage_text}"
    "if (runtime.model.status().flex_capability != capability_label)"
    flex_capability_compare)
string(FIND "${flex_stage_text}"
    "auto status = runtime.model.status();" flex_capability_copy)
string(FIND "${flex_stage_text}"
    "if (current.flex_telemetry != flex_status" flex_observation_compare)
string(FIND "${flex_stage_text}"
    "auto status = current;" flex_observation_copy)
if(flex_capability_compare EQUAL -1 OR flex_capability_copy EQUAL -1 OR
   NOT flex_capability_compare LESS flex_capability_copy OR
   flex_observation_compare EQUAL -1 OR flex_observation_copy EQUAL -1 OR
   NOT flex_observation_compare LESS flex_observation_copy)
    message(FATAL_ERROR
        "FleX presentation must compare represented fields before copying the complete UI status")
endif()

message(STATUS
    "Telemetry pipeline boundary verified: one orchestrator and eight directional stage modules")
