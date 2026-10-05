#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "kf2/game/gameplay_log_lab.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

std::string normalize_newlines(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

std::size_t count_occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    for (auto offset = text.find(needle); offset != std::string_view::npos;
         offset = text.find(needle, offset + needle.size())) {
        ++count;
    }
    return count;
}

bool adaptive_mode_preserves_graphics_receipt(std::string_view body) {
    constexpr std::string_view reset =
        "if (Resource ~= \"disable\")\n        {\n"
        "            AdaptiveGraphicsQuality = 100;\n"
        "            AdaptiveGraphicsResource = Resource;\n        }";
    return body.find(reset) != std::string_view::npos &&
        count_occurrences(body, "AdaptiveGraphicsQuality =") == 1 &&
        count_occurrences(body, "AdaptiveGraphicsResource =") == 1 &&
        body.find("SetAdaptiveRuntimeEnabled") < body.find(reset) &&
        body.find(reset) < body.find("AdaptiveLastControlSequence = Sequence;");
}

bool online_enable_is_transactional(std::string_view body) {
    const auto prerequisite = body.find("!EnsureOnlineFixedEffectsBaseline(CurrentWorld)");
    const auto reject = body.find("return false;", prerequisite);
    const auto pending_check = body.find("if (!RestoreOnlineEnableMaximum(GoreManager))");
    const auto previous = body.find(
        "OnlineCorpseEnablePreviousMaximum = GoreManager.MaxDeadBodies;");
    const auto write = body.find("GoreManager.MaxDeadBodies = Quality;");
    const auto mismatch = body.find("if (GoreManager.MaxDeadBodies != Quality)");
    const auto debt = body.find("bOnlineCorpseEnableRestorePending = true;", mismatch);
    const auto rollback = body.find("RestoreOnlineEnableMaximum(GoreManager);", debt);
    const auto failure = body.find("return false;", rollback);
    const auto commit = body.find("bOnlineGraphicsEnabled = true;");
    return prerequisite != std::string_view::npos && reject < write &&
        pending_check < prerequisite && previous < write && write < mismatch &&
        mismatch < debt && debt < rollback && rollback < failure && failure < commit &&
        count_occurrences(body, "GoreManager.MaxDeadBodies = Quality;") == 1 &&
        body.find("RestoreOnlineCorpseMaximum") == std::string_view::npos;
}

bool online_pool_scan_is_bounded(std::string_view body, std::string_view cursor,
                                 std::string_view timestamp) {
    const auto attempt = body.find(std::string{timestamp} +
                                  " = CurrentWorld.RealTimeSeconds;");
    const auto manager = body.find("GoreManager =");
    const auto empty = body.find("if (PoolLength == 0) return false;");
    const auto budget = body.find("ScanCount = Min(OnlineCorpseScanBudget, PoolLength);");
    const auto loop = body.find("for (Scanned = 0; Scanned < ScanCount; ++Scanned)");
    const auto advance = body.find(std::string{cursor} + " = (Index + 1) % PoolLength;");
    const auto candidate = body.find("Candidate = GoreManager.CorpsePool[Index];");
    return attempt != std::string_view::npos && attempt < manager &&
        empty != std::string_view::npos && empty < budget && budget < loop &&
        loop < advance && advance < candidate &&
        body.find("Index = " + std::string{cursor} + ";", loop) < advance &&
        count_occurrences(body, std::string{timestamp} + " =") == 1;
}

std::string online_visual_cursor(std::string_view body) {
    constexpr std::string_view prefix = "Index = (";
    const auto start = body.find(prefix);
    if (start == std::string_view::npos) return {};
    const auto end = body.find(" + Offset) % PoolLength;", start);
    if (end == std::string_view::npos) return {};
    return std::string{body.substr(start + prefix.size(),
                                  end - start - prefix.size())};
}

// Source contracts below bind this deterministic model to the UC cursor,
// budget and advance rules. This does not execute UnrealScript or physics.
struct OnlineVisualScanModel {
    bool shared_cursor{};
    std::array<int, 2> cursors{};
    int phase{};
};

bool covers_online_visual_pool(OnlineVisualScanModel& model,
                              const std::vector<int>& pool,
                              bool intermittent_actions,
                              bool failed_readbacks = false) {
    const auto length = static_cast<int>(pool.size());
    std::array<std::vector<bool>, 2> seen{
        std::vector<bool>(pool.size()), std::vector<bool>(pool.size())};
    auto done = seen;
    // Each successful action advances at least one slot; a no-action visit
    // advances up to eight. Two full per-category cycles are ample for both.
    for (int visit = 0; visit < 4 * length + 2; ++visit) {
        const auto phase = model.phase;
        auto& cursor = model.cursors[model.shared_cursor ? 0 : phase];
        if (length == 0) {
            cursor = 0;
        } else {
            cursor = std::clamp(cursor, 0, length - 1);
            const auto scan_count = std::min(8, length);
            int inspected = 0;
            int actions = 0;
            for (int offset = 0; offset < scan_count; ++offset) {
                const auto index = (cursor + offset) % length;
                ++inspected;
                seen[phase][index] = true;
                if (intermittent_actions && pool[index] % 3 == phase &&
                    !done[phase][index]) {
                    // The first eligible entry of either category may fail
                    // permanently; later eligible entries can still succeed.
                    if (!failed_readbacks || pool[index] > 1) {
                        done[phase][index] = true;
                    }
                    ++actions;
                    cursor = (index + 1) % length;
                    break;
                }
            }
            if (actions == 0) cursor = (cursor + scan_count) % length;
            if (inspected > 8 || actions > 1 || cursor < 0 ||
                cursor >= length) return false;
        }
        model.phase = (phase + 1) % 2;
    }
    return std::all_of(seen[0].begin(), seen[0].end(),
                       [](bool value) { return value; }) &&
           std::all_of(seen[1].begin(), seen[1].end(),
                       [](bool value) { return value; });
}

bool advances_online_attempt(std::string_view body, std::string_view cursor,
                             std::string_view first_write,
                             std::string_view attempt_clock) {
    const auto write = body.find(first_write);
    const auto advance = body.find(std::string{cursor} +
        " = (Index + 1) % PoolLength;");
    const auto stamp = body.find(std::string{attempt_clock} +
        " = WorldInfo.RealTimeSeconds;");
    if (write == std::string_view::npos || advance >= write || stamp >= write) {
        std::cerr << cursor << ": mismatch can bypass cursor/cooldown progress\n";
        return false;
    }
    return true;
}

bool online_freeze_progress_after_rollback(int length) {
    std::vector<bool> completed(static_cast<std::size_t>(length));
    int cursor = 0;
    for (int visit = 0; visit < 2 * length; ++visit) {
        const auto scan_count = std::min(8, length);
        bool attempted = false;
        for (int offset = 0; offset < scan_count; ++offset) {
            const auto index = (cursor + offset) % length;
            if (completed[index]) continue;
            cursor = (index + 1) % length;
            attempted = true;
            // Entry zero always fails readback, but immediate rollback
            // succeeds, removing its ledger entry and leaving it eligible.
            if (index != 0) completed[index] = true;
            break;
        }
        if (!attempted) cursor = (cursor + scan_count) % length;
    }
    return !completed[0] &&
        std::all_of(completed.begin() + 1, completed.end(),
                    [](bool value) { return value; });
}

bool online_restore_advances_after_failure(std::string_view body) {
    const auto attempt = body.find("TryRestoreOnlineCorpse(Index, bRestoreAll ?");
    const auto advance = body.find(
        "        ReleaseScanCursor = FrozenCorpses.Length > 0 ?\n"
        "            (bRemoved ? Index % FrozenCorpses.Length :\n"
        "             (Index + 1) % FrozenCorpses.Length) : 0;");
    return attempt != std::string_view::npos &&
        advance != std::string_view::npos && attempt < advance &&
        body.substr(attempt, advance - attempt).find("return false;") ==
            std::string_view::npos;
}

constexpr std::string_view online_restore_readback_fields[] = {
    "Candidate.Physics != PHYS_RigidBody ||",
    "Candidate.bCollideActors != Original.bOriginalCollideActors ||",
    "Candidate.bBlockActors != Original.bOriginalBlockActors ||",
    "Candidate.bIgnoreEncroachers != Original.bOriginalIgnoreEncroachers ||",
    "Candidate.bTickIsDisabled != Original.bOriginalTickDisabled ||",
    "(Candidate.CollisionComponent != None) !=\n"
    "            Original.bHadCollisionComponent ||",
    "(Original.bHadCollisionComponent &&\n"
    "         Candidate.CollisionComponent.BlockRigidBody !=\n"
    "             Original.bOriginalBlockRigidBody)",
};

bool online_restore_has_complete_readback(std::string_view body) {
    const auto begin = body.find("if (Candidate.Physics != PHYS_RigidBody ||");
    const auto end = body.find("LastPhysicsMutationRealTime =", begin);
    if (begin == std::string_view::npos || end == std::string_view::npos) {
        return false;
    }
    const auto guard = body.substr(begin, end - begin);
    return std::all_of(std::begin(online_restore_readback_fields),
                       std::end(online_restore_readback_fields),
                       [guard](std::string_view field) {
                           return guard.find(field) != std::string_view::npos;
                       }) &&
        guard.find("\"restore_readback_mismatch\");\n        return false;") !=
            std::string_view::npos &&
        body.find("\"collision_component_missing\");\n            return false;") !=
            std::string_view::npos &&
        body.find("FrozenCorpses.Remove(") == std::string_view::npos;
}

// Models the source-bound restore-all loop, not UnrealScript/PhysX execution.
// A failed ledger record keeps its original collision/tick state for retry.
bool online_restore_progress(int length, int failed_id,
                             bool legacy_reverse_scan = false) {
    struct OriginalState {
        int id;
        std::array<bool, 6> collision_and_tick;
        bool operator==(const OriginalState&) const = default;
    };
    const auto fails = [failed_id](int id) {
        return failed_id == -2 || id == failed_id;
    };
    std::vector<OriginalState> ledger;
    std::vector<OriginalState> expected_remaining;
    for (int id = 0; id < length; ++id) {
        OriginalState original{id, {}};
        for (std::size_t bit = 0; bit < original.collision_and_tick.size(); ++bit) {
            original.collision_and_tick[bit] = (id & (1 << bit)) != 0;
        }
        ledger.push_back(original);
        if (fails(id)) expected_remaining.push_back(original);
    }
    std::size_t cursor = 0;
    for (int visit = 0; visit < 2 * length + 1 && !ledger.empty(); ++visit) {
        int inspected = 0;
        int successful_mutations = 0;
        while (!ledger.empty() && inspected < 8) {
            const auto index = legacy_reverse_scan ? ledger.size() - 1
                : std::min(cursor, ledger.size() - 1);
            ++inspected;
            if (fails(ledger[index].id)) {
                if (legacy_reverse_scan) break;
                cursor = (index + 1) % ledger.size();
            } else {
                ledger.erase(ledger.begin() + index);
                cursor = ledger.empty() ? 0 : index % ledger.size();
                ++successful_mutations;
                break;
            }
        }
        if (inspected > 8 || successful_mutations > 1 ||
            (!ledger.empty() && cursor >= ledger.size())) return false;
    }
    return ledger == expected_remaining;
}

struct OfflineReleasePolicy {
    bool late_reservation;
    bool yield_after_attempt;
    bool fair_lanes;
};

bool offline_restore_yields_after_attempt(std::string_view body,
                                         std::string_view restore_call) {
    const auto flag = body.find("bRestoreAttempted = true;");
    const auto attempt = body.find(restore_call);
    const auto advance = body.find("(Index + 1) %", attempt);
    const auto yield = body.find("if (bRestoreAttempted)", advance);
    return flag != std::string_view::npos &&
        attempt != std::string_view::npos && flag < attempt &&
        advance != std::string_view::npos && yield != std::string_view::npos;
}

// Source-bound model of the shared one-physics-attempt-per-frame budget and
// active/retired freeze/wake queues. It does not execute UnrealScript/PhysX.
bool offline_release_progress(int length, int failed_id, bool physics_failure,
                              OfflineReleasePolicy policy) {
    std::array<std::vector<int>, 4> queues;
    std::array<std::vector<int>, 4> expected;
    for (int id = 0; id < length; ++id) {
        for (int lane = 0; lane < 2; ++lane) {
            queues[lane].push_back(id);
            if (id == failed_id) expected[lane].push_back(id);
        }
    }
    queues[2] = queues[3] = {0, 1};
    std::array<std::size_t, 2> cursors{};
    int phase = 0;
    for (int callback = 0; callback < 8 * length + 32; ++callback) {
        int lane = -1;
        for (int slot = 0; slot < 4; ++slot) {
            const int candidate = policy.fair_lanes ? phase : slot;
            if (policy.fair_lanes) phase = (phase + 1) % 4;
            if (!queues[candidate].empty()) {
                lane = candidate;
                break;
            }
        }
        if (lane < 0) break;
        auto& queue = queues[lane];
        bool reserved = false;
        int reservations = 0;
        int physics_attempts = 0;
        int inspected = 0;
        if (lane >= 2) {
            queue.erase(queue.begin());
            ++physics_attempts;
        } else {
            auto& cursor = cursors[lane];
            while (!queue.empty() && inspected < 64) {
                const auto index = std::min(cursor, queue.size() - 1);
                ++inspected;
                const bool failed = queue[index] == failed_id;
                bool restored = false;
                if (!reserved) {
                    if (!policy.late_reservation || !failed || physics_failure) {
                        reserved = true;
                        ++reservations;
                    }
                    if (!failed || physics_failure) ++physics_attempts;
                    restored = !failed;
                }
                if (restored) {
                    queue.erase(queue.begin() + index);
                    cursor = queue.empty() ? 0 : index % queue.size();
                    break;
                }
                cursor = (index + 1) % queue.size();
                if (policy.yield_after_attempt) break;
            }
        }
        if (inspected > 64 || reservations > 1 || physics_attempts > 1 ||
            (lane < 2 && !queue.empty() && cursors[lane] >= queue.size())) {
            return false;
        }
    }
    return queues == expected;
}

struct OfflineWakePolicy {
    bool retain_failed;
    bool advance_failed;
    bool restore_already_awake;
    bool validate_identity;
};

OfflineWakePolicy offline_wake_policy(std::string_view body) {
    const auto call = body.find("Candidate.Mesh.WakeRigidBody();");
    const auto readback = body.find(
        "if (!Candidate.Mesh.RigidBodyIsAwake())", call);
    const auto skeleton = body.find(
        "Candidate.Mesh.bNoSkeletonUpdate = false;", call);
    const auto failure = readback != std::string_view::npos &&
        skeleton != std::string_view::npos && readback < skeleton
        ? body.substr(readback, skeleton - readback) : std::string_view{};
    return {
        .retain_failed = !failure.empty() &&
            failure.find("return 0;") != std::string_view::npos &&
            failure.find("RemoveAdaptiveDistanceSleptCorpseEntry") ==
                std::string_view::npos,
        .advance_failed = failure.find("(Index + 1) %") !=
            std::string_view::npos && failure.find(
                "AdaptiveDistanceReleaseWakeCursor") != std::string_view::npos,
        .restore_already_awake = body.find(
            "\n        }\n        Candidate.Mesh.bNoSkeletonUpdate = false;") !=
                std::string_view::npos,
        .validate_identity = body.find(
            "GetAdaptiveCorpseActionId(Candidate) !=") < call,
    };
}

// Source-bound release model, not a PhysX simulation. Each invocation may
// inspect 64 entries, but only one wake attempt may consume the frame slot.
bool offline_wake_progress(int length, int failed_id, bool late_wake,
                           bool invalid_entries, OfflineWakePolicy policy) {
    struct Entry {
        int id;
        int invalid_kind;
        bool awake;
        bool skeleton_disabled;
    };
    std::vector<Entry> ledger;
    std::vector<int> expected;
    for (int id = 0; id < length; ++id) {
        const int invalid_kind = invalid_entries ? id % 5 : 0;
        ledger.push_back({id, invalid_kind, id % 3 == 1, true});
        if (!late_wake && invalid_kind == 0 &&
            (failed_id == -2 || id == failed_id)) {
            ledger.back().awake = false;
            expected.push_back(id);
        }
        if (id == failed_id) ledger.back().awake = false;
    }
    std::size_t cursor = 0;
    for (int callback = 0; callback < 4 * length + 16 && !ledger.empty();
         ++callback) {
        if (late_wake && callback >= length + 2) {
            for (auto& entry : ledger) {
                if (entry.id == failed_id) entry.awake = true;
            }
        }
        int inspected = 0;
        int attempts = 0;
        while (!ledger.empty() && inspected < 64) {
            const auto index = policy.advance_failed
                ? std::min(cursor, ledger.size() - 1) : ledger.size() - 1;
            auto& entry = ledger[index];
            ++inspected;
            // Kinds 1/2/3/4: deleted, reused, missing mesh, non-rigid.
            const bool invalid = entry.invalid_kind != 0 &&
                (entry.invalid_kind != 2 || policy.validate_identity);
            if (!invalid && entry.invalid_kind != 0) return false;
            if (!invalid && !entry.awake) {
                // Another physics action can already own this frame.
                if (callback % 5 == 0) break;
                ++attempts;
                entry.awake = !(failed_id == -2 || entry.id == failed_id);
                if (!entry.awake && policy.retain_failed) {
                    cursor = (index + 1) % ledger.size();
                    break;
                }
                if (entry.awake) entry.skeleton_disabled = false;
            } else if (!invalid && policy.restore_already_awake) {
                entry.skeleton_disabled = false;
            }
            if (!invalid && (!entry.awake || entry.skeleton_disabled)) {
                return false;
            }
            ledger.erase(ledger.begin() + index);
            cursor = ledger.empty() ? 0 : index % ledger.size();
            if (attempts > 0) break;
        }
        if (attempts > 1 || inspected > 64 ||
            (!ledger.empty() && cursor >= ledger.size())) return false;
    }
    std::vector<int> remaining;
    for (const auto& entry : ledger) {
        if (entry.awake || !entry.skeleton_disabled) return false;
        remaining.push_back(entry.id);
    }
    return remaining == expected;
}

std::size_t settled_after_bounded_scans(
    std::size_t pool_size, std::size_t scan_budget,
    double category_visit_interval, double tracking_timeout) {
    struct SettleSample {
        bool initialized{};
        bool settled{};
        double stable_since{};
        double last_observed{};
    };

    std::vector<SettleSample> samples(pool_size);
    std::size_t cursor = 0;
    std::size_t settled = 0;
    double now = 0.0;
    for (std::size_t visit = 0; visit < 96 && settled < pool_size; ++visit) {
        const auto scan_count = std::min(scan_budget, pool_size);
        for (std::size_t offset = 0; offset < scan_count; ++offset) {
            auto& sample = samples[(cursor + offset) % pool_size];
            if (sample.settled) continue;
            if (!sample.initialized ||
                now - sample.last_observed > tracking_timeout) {
                sample.initialized = true;
                sample.stable_since = now;
                sample.last_observed = now;
                continue;
            }
            sample.last_observed = now;
            if (now - sample.stable_since >= 0.75) {
                sample.settled = true;
                ++settled;
            }
        }
        cursor = (cursor + scan_count) % pool_size;
        now += category_visit_interval;
    }
    return settled;
}

std::vector<std::string> telemetry_schema_fields(
    std::string_view source, std::string_view block_start,
    std::string_view block_end) {
    const auto start = source.find(block_start);
    if (start == std::string_view::npos) return {};
    const auto end = source.find(block_end, start + block_start.size());
    if (end == std::string_view::npos) return {};

    std::vector<std::string> fields{"sample"};
    auto cursor = start + block_start.size();
    while (true) {
        const auto name_start = source.find("\" ", cursor);
        if (name_start == std::string_view::npos || name_start >= end) break;
        const auto equals = source.find('=', name_start + 2);
        if (equals == std::string_view::npos || equals >= end) return {};
        const auto name = source.substr(name_start + 2,
                                        equals - (name_start + 2));
        if (name.empty() || name.find_first_of(" \t\r\n\"") !=
                                std::string_view::npos) {
            return {};
        }
        fields.emplace_back(name);
        cursor = equals + 1;
    }
    return fields;
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    constexpr std::string_view control_token =
        "0123456789abcdef0123456789abcdef";
    const auto telemetry_source = normalize_newlines(
        read_bytes(KF2_TELEMETRY_SOURCE));
    const auto telemetry_parser_source = normalize_newlines(
        read_bytes(KF2_GAME_LOG_TELEMETRY_PARSER_SOURCE));
    const auto mutator_source = normalize_newlines(
        read_bytes(KF2_TELEMETRY_MUTATOR_SOURCE));
    const auto interaction_source = normalize_newlines(
        read_bytes(KF2_TELEMETRY_INTERACTION_SOURCE));
    const auto graphics_interaction_source = normalize_newlines(
        read_bytes(KF2_GRAPHICS_INTERACTION_SOURCE));
    const auto fire_affliction_source = normalize_newlines(
        read_bytes(KF2_FIRE_AFFLICTION_SOURCE));
    const auto weapon_fallback_source = normalize_newlines(
        read_bytes(KF2_WEAPON_FALLBACK_SOURCE));
    const auto online_context_source = normalize_newlines(
        read_bytes(KF2_ONLINE_CONTEXT_INTERACTION_SOURCE));
    const auto graphics_viewport_source = normalize_newlines(
        read_bytes(KF2_GRAPHICS_VIEWPORT_SOURCE));
    const auto listener_source = read_bytes(KF2_ADAPTIVE_LISTENER_SOURCE);
    const auto online_corpse_controller_source = normalize_newlines(
        read_bytes(KF2_ONLINE_CORPSE_CONTROLLER_SOURCE));
    const auto connection_source = normalize_newlines(
        read_bytes(KF2_ADAPTIVE_CONNECTION_SOURCE));
    const auto online_graphics_connection_source = normalize_newlines(
        read_bytes(KF2_ONLINE_GRAPHICS_CONNECTION_SOURCE));
    const auto graphics_source = normalize_newlines(
        read_bytes(KF2_ADAPTIVE_GRAPHICS_SOURCE));
    const auto telemetry_session_source = normalize_newlines(
        read_bytes(KF2_TELEMETRY_SESSION_SOURCE));
    const auto producer_schema_fields = telemetry_schema_fields(
        telemetry_source, "KF2OPT_TELEMETRY schema=7 sample=", ");\n}");
    const auto parser_schema_fields = telemetry_schema_fields(
        telemetry_parser_source, "KF2OPT_TELEMETRY schema=7 sample=",
        "if (!sample");
    CHECK(!producer_schema_fields.empty());
    CHECK(producer_schema_fields == parser_schema_fields);
    CHECK(telemetry_source.find("AdaptiveControlToken") != std::string::npos);
    CHECK(telemetry_source.find("ValidAdaptiveControlToken") !=
          std::string::npos);
    CHECK(telemetry_source.find("KF2OPT_ADAPTIVE_QUALITY state=applied") !=
          std::string::npos);
    const auto control_vocabulary_start = graphics_source.find(
        "static function bool IsAdaptiveControlResource(");
    const auto quality_vocabulary_start = graphics_source.find(
        "static function bool IsAdaptiveQualityResource(");
    const auto online_vocabulary_start = graphics_source.find(
        "static function bool IsOnlineAdaptiveQualityResource(");
    const auto effective_overdraw_start = graphics_source.find(
        "static function int GetEffectiveOverdrawQuality(");
    CHECK(control_vocabulary_start != std::string::npos);
    CHECK(quality_vocabulary_start != std::string::npos);
    CHECK(online_vocabulary_start != std::string::npos);
    CHECK(effective_overdraw_start != std::string::npos);
    const auto control_vocabulary = graphics_source.substr(
        control_vocabulary_start,
        quality_vocabulary_start - control_vocabulary_start);
    const auto quality_vocabulary = graphics_source.substr(
        quality_vocabulary_start,
        online_vocabulary_start - quality_vocabulary_start);
    const auto online_vocabulary = graphics_source.substr(
        online_vocabulary_start,
        effective_overdraw_start - online_vocabulary_start);
    for (const auto resource : {"cpu", "gpu", "vram", "ram", "overdraw",
                                "effects", "mixed", "recover", "enable",
                                "disable"}) {
        CHECK(count_occurrences(control_vocabulary,
            std::string{"Resource ~= \""} + resource + "\"") == 1);
    }
    CHECK(quality_vocabulary.find("IsAdaptiveControlResource(Resource)") !=
          std::string::npos);
    CHECK(count_occurrences(
        quality_vocabulary, "Resource ~= \"enable\"") == 1);
    CHECK(count_occurrences(
        quality_vocabulary, "Resource ~= \"disable\"") == 1);
    for (const auto resource : {"cpu", "gpu", "vram", "ram", "mixed",
                                "recover"}) {
        CHECK(count_occurrences(online_vocabulary,
            std::string{"Resource ~= \""} + resource + "\"") == 1);
    }
    CHECK(online_vocabulary.find("Resource ~= \"overdraw\"") ==
          std::string::npos);
    CHECK(online_vocabulary.find("Resource ~= \"effects\"") ==
          std::string::npos);
    CHECK(telemetry_source.find("var globalconfig bool bAdaptiveRuntimeEnabled") !=
          std::string::npos);
    CHECK(telemetry_source.find("function bool SetAdaptiveRuntimeEnabled") !=
          std::string::npos);
    CHECK(telemetry_source.find("function int WakeAdaptiveDistanceSleptCorpseBatch") !=
          std::string::npos);
    CHECK(telemetry_source.find("function BeginAdaptiveCorpsePhysicsRelease") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (!bAdaptiveRuntimeEnabled)\n    {\n        `log(\"KF2OPT_ADAPTIVE_QUALITY") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_ADAPTIVE_MODE state=disabled fixed_effect_quality=") !=
          std::string::npos);
    const auto adaptive_runtime_function = telemetry_source.find(
        "function bool SetAdaptiveRuntimeEnabled");
    const auto adaptive_control_function = telemetry_source.find(
        "function bool ApplyAdaptiveResourceControl");
    CHECK(adaptive_runtime_function != std::string::npos);
    CHECK(adaptive_control_function != std::string::npos);
    const auto adaptive_control_end = telemetry_source.find(
        "function bool ApplyAdaptiveEffectRuntimeReadback(",
        adaptive_control_function);
    CHECK(adaptive_control_end != std::string::npos);
    const auto adaptive_control_body = telemetry_source.substr(
        adaptive_control_function,
        adaptive_control_end - adaptive_control_function);
    const auto mode_start = adaptive_control_body.find(
        "if ((Resource ~= \"enable\") || (Resource ~= \"disable\"))");
    const auto mode_end = adaptive_control_body.find(
        "if (!bAdaptiveRuntimeEnabled)", mode_start);
    CHECK(mode_start != std::string::npos && mode_end != std::string::npos);
    const auto mode_body = adaptive_control_body.substr(mode_start, mode_end - mode_start);
    const bool preserves_graphics_receipt =
        adaptive_mode_preserves_graphics_receipt(mode_body);
    CHECK(preserves_graphics_receipt);
    auto polluted_mode = mode_body;
    polluted_mode += "AdaptiveGraphicsQuality = Quality;";
    CHECK(!adaptive_mode_preserves_graphics_receipt(polluted_mode));
    auto wrong_mode_guard = mode_body;
    const auto disable_guard = wrong_mode_guard.find("if (Resource ~= \"disable\")");
    CHECK(disable_guard != std::string::npos);
    wrong_mode_guard.replace(disable_guard,
        std::string_view{"if (Resource ~= \"disable\")"}.size(),
        "if (Resource ~= \"enable\")");
    CHECK(!adaptive_mode_preserves_graphics_receipt(wrong_mode_guard));
    // Source-bound transition model; it does not execute UnrealScript. The
    // unchanged pressure predicate below is checked against the source too.
    for (const int corpse_limit : {4, 20, 99, 100, 2000}) {
        int confirmed_quality = 100;
        std::string confirmed_resource = "recover";
        int sequence = 0;
        const auto pressure = [&] {
            return sequence > 0 && confirmed_quality >= 10 &&
                confirmed_quality < 100 && confirmed_resource != "recover";
        };
        const auto mode = [&](bool enabled, bool applied) {
            if (!applied) return false;
            if (!enabled) {
                confirmed_quality = 100;
                confirmed_resource = "disable";
            } else if (!preserves_graphics_receipt) {
                confirmed_quality = corpse_limit;
                confirmed_resource = "enable";
            }
            ++sequence;
            return true;
        };
        CHECK(corpse_limit >= 4 && corpse_limit <= 2000);
        CHECK(!mode(true, false));
        CHECK(sequence == 0 && confirmed_quality == 100 && !pressure());
        CHECK(mode(true, true));
        CHECK(confirmed_quality == 100 && confirmed_resource == "recover");
        CHECK(!pressure());
        for (const auto resource : {"cpu", "gpu"}) {
            confirmed_quality = 75;
            confirmed_resource = resource;
            ++sequence;
            CHECK(pressure());
            const auto receipt_sequence = sequence;
            CHECK(!mode(true, false));
            CHECK(sequence == receipt_sequence);
            CHECK(mode(true, true));
            CHECK(confirmed_quality == 75 && confirmed_resource == resource);
            CHECK(pressure());
        }
        CHECK(mode(false, true));
        CHECK(confirmed_quality == 100 && !pressure());
        CHECK(mode(true, true));
        CHECK(!pressure());
    }
    CHECK(adaptive_control_body.find("IsAdaptiveControlResource(Resource)") !=
          std::string::npos);
    CHECK(adaptive_control_body.find("IsAdaptiveQualityResource(Resource)") !=
          std::string::npos);
    CHECK(adaptive_control_body.find(
        "ApplyAdaptiveEffectRuntimeReadback(Resource, Quality)") !=
          std::string::npos);
    const auto adaptive_disable_body = telemetry_source.substr(
        adaptive_runtime_function,
        adaptive_control_function - adaptive_runtime_function);
    CHECK(adaptive_disable_body.find(
        "class'KF2OptimizerAdaptiveGraphics'.static.RestoreOriginal(\n"
        "            AdaptiveGraphicsState)") != std::string::npos);
    CHECK(adaptive_disable_body.find(
        "class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(\n"
        "            AdaptiveGraphicsState, \"recover\", 100)") ==
          std::string::npos);
    const auto offline_disable_commit = adaptive_disable_body.find(
        "bAdaptiveRuntimeEnabled = false;");
    const auto offline_disable_release = adaptive_disable_body.find(
        "BeginAdaptiveCorpsePhysicsRelease();", offline_disable_commit);
    const auto offline_disable_restore = adaptive_disable_body.find(
        "class'KF2OptimizerAdaptiveGraphics'.static.RestoreOriginal(",
        offline_disable_commit);
    const auto offline_disable_fixed = adaptive_disable_body.find(
        "bFixedEffectsApplied = EnsureFixedSessionEffects();",
        offline_disable_commit);
    const auto offline_disable_deferred = adaptive_disable_body.find(
        "readback=deferred", offline_disable_commit);
    CHECK(offline_disable_commit != std::string::npos);
    CHECK(offline_disable_release != std::string::npos);
    CHECK(offline_disable_restore != std::string::npos);
    CHECK(offline_disable_fixed != std::string::npos);
    CHECK(offline_disable_deferred != std::string::npos);
    CHECK(offline_disable_commit < offline_disable_release);
    CHECK(offline_disable_release < offline_disable_restore);
    CHECK(offline_disable_restore < offline_disable_fixed);
    CHECK(adaptive_disable_body.find(
        "return true;", offline_disable_deferred) != std::string::npos);
    const auto fixed_effects_start = telemetry_source.find(
        "function bool EnsureFixedSessionEffects()");
    const auto restore_world_runtime_start = telemetry_source.find(
        "function bool RestoreSessionWorldRuntime()");
    CHECK(fixed_effects_start != std::string::npos);
    CHECK(restore_world_runtime_start != std::string::npos);
    const auto fixed_effects_body = telemetry_source.substr(
        fixed_effects_start,
        restore_world_runtime_start - fixed_effects_start);
    CHECK(fixed_effects_body.find(
        "bGraphicsRestored = class'KF2OptimizerAdaptiveGraphics'.static.\n"
        "            RestoreOriginal(AdaptiveGraphicsState)") !=
          std::string::npos);
    CHECK(fixed_effects_body.find(
        "bEffectRuntimeRestored = ApplyAdaptiveEffectRuntimeReadback(\n"
        "            \"rollback\", 100, true)") != std::string::npos);
    CHECK(fixed_effects_body.find("domain=graphics") != std::string::npos);
    CHECK(fixed_effects_body.find("domain=effect_runtime") !=
          std::string::npos);
    CHECK(fixed_effects_body.find(
        "RestoreOriginal(\n                AdaptiveGraphicsState) ||") ==
          std::string::npos);
    const auto adaptive_control_start = telemetry_source.find(
        "function bool ApplyAdaptiveResourceControl(", restore_world_runtime_start);
    CHECK(adaptive_control_start != std::string::npos);
    const auto restore_world_runtime_end = telemetry_source.find(
        "\nfunction ", restore_world_runtime_start);
    CHECK(restore_world_runtime_end != std::string::npos);
    const auto restore_world_runtime_body = telemetry_source.substr(
        restore_world_runtime_start,
        restore_world_runtime_end - restore_world_runtime_start);
    CHECK(restore_world_runtime_body.find(
        "ApplyAdaptiveEffectRuntimeReadback(\n"
        "        \"restore\", 100, true)") != std::string::npos);
    CHECK(restore_world_runtime_body.find("RestoreOriginal(") ==
          std::string::npos);
    CHECK(restore_world_runtime_body.find(
        "domain=world_runtime") != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_FIXED_EFFECT_BASELINE state=world_runtime_restored") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.RestoreSessionGraphics()") == std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.RestoreSessionWorldRuntime()") != std::string::npos);
    const auto app_restore_function = telemetry_session_source.find(
        "bool UiRuntime::restore_live_adaptive_quality(");
    const auto app_toggle_function = telemetry_session_source.find(
        "bool UiRuntime::set_live_adaptive_enabled(");
    CHECK(app_restore_function != std::string::npos);
    CHECK(app_toggle_function != std::string::npos);
    CHECK(telemetry_session_source.substr(
              app_restore_function,
              app_toggle_function - app_restore_function).find(
                  ".resource = game::AdaptiveResourceControl::disable") !=
          std::string::npos);
    CHECK(telemetry_source.substr(
              adaptive_runtime_function,
              adaptive_control_function - adaptive_runtime_function).find(
                  "ClearTimer(nameof(SampleTelemetry), self)") ==
          std::string::npos);
    CHECK(interaction_source.find(
        "var bool bProcessAdaptiveRuntimeEnabled") != std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.SetAdaptiveRuntimeEnabled(") != std::string::npos);
    CHECK(connection_source.find(
        "CurrentInteraction.SetProcessAdaptiveRuntimeEnabled(") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "class'KF2OptimizerAdaptiveControlListener'") != std::string::npos);
    CHECK(interaction_source.find("var transient WorldInfo ActiveWorld") ==
          std::string::npos);
    CHECK(interaction_source.find("var bool bGameSessionEnding") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "var KF2OptimizerAdaptiveGraphicsState "
        "ProcessAdaptiveGraphicsState;") != std::string::npos);
    CHECK(interaction_source.find(
        "function KF2OptimizerAdaptiveGraphicsState "
        "GetProcessAdaptiveGraphicsState()") != std::string::npos);
    CHECK(interaction_source.find(
        "ProcessAdaptiveGraphicsState = new(self)\n"
        "            class'KF2OptimizerAdaptiveGraphicsState'") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.AdaptiveGraphicsState =\n"
        "            GetProcessAdaptiveGraphicsState()") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "ProcessAdaptiveGraphicsState = None") == std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveGraphicsState = new(self)") == std::string::npos);
    const auto interaction_tick = interaction_source.find(
        "event Tick(float DeltaTime)");
    CHECK(interaction_tick != std::string::npos);
    const auto interaction_session_end = interaction_source.find(
        "function NotifyGameSessionEnded()", interaction_tick);
    CHECK(interaction_session_end != std::string::npos);
    const auto interaction_tick_body = interaction_source.substr(
        interaction_tick, interaction_session_end - interaction_tick);
    CHECK(interaction_source.find(
        "const TelemetryMaintenanceInitialSeconds=0.05;") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "const TelemetryMaintenanceMaximumSeconds=1.0;") !=
          std::string::npos);
    const auto telemetry_cadence_guard = interaction_tick_body.find(
        "TelemetryMaintenanceElapsedSeconds <\n"
        "        TelemetryMaintenanceIntervalSeconds");
    const auto telemetry_context_lookup = interaction_tick_body.find(
        "GetStandaloneGameplayContext(PrimaryController, CurrentWorld)");
    const auto telemetry_probe_scan = interaction_tick_body.find(
        "CurrentWorld.DynamicActors(\n"
        "        class'KF2OptimizerTelemetryProbe'");
    const auto telemetry_listener_scan = interaction_tick_body.find(
        "CurrentWorld.DynamicActors(\n"
        "        class'KF2OptimizerAdaptiveControlListener'");
    CHECK(telemetry_cadence_guard != std::string::npos);
    CHECK(telemetry_context_lookup != std::string::npos);
    CHECK(telemetry_probe_scan != std::string::npos);
    CHECK(telemetry_listener_scan != std::string::npos);
    CHECK(telemetry_cadence_guard < telemetry_context_lookup);
    CHECK(telemetry_context_lookup < telemetry_probe_scan);
    CHECK(telemetry_probe_scan < telemetry_listener_scan);
    CHECK(interaction_tick_body.find(
        "ScheduleTelemetryMaintenance(true);") != std::string::npos);
    CHECK(count_occurrences(interaction_tick_body,
        "ScheduleTelemetryMaintenance(false);") >= 7);
    const auto telemetry_world_reset = interaction_tick_body.find(
        "TelemetryMaintenanceLastObservedRealTime ||");
    CHECK(telemetry_world_reset != std::string::npos);
    CHECK(interaction_tick_body.find(
        "!(CurrentMapName ~= TelemetryMaintenanceMapName)",
        telemetry_world_reset) != std::string::npos);
    CHECK(interaction_tick_body.find(
        "ResetTelemetryMaintenanceCadence();", telemetry_world_reset) <
          telemetry_probe_scan);
    CHECK(interaction_source.find(
        "TelemetryMaintenanceIntervalSeconds = FMin(") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "TelemetryMaintenanceMaximumSeconds,") != std::string::npos);
    CHECK(interaction_source.find(
        "var KF2OptimizerTelemetryProbe") == std::string::npos);
    CHECK(interaction_source.find(
        "var KF2OptimizerAdaptiveControlListener") == std::string::npos);
    {
        double interval = 0.05;
        for (int converged_pass = 0; converged_pass < 8;
             ++converged_pass) {
            interval = std::min(1.0, interval * 2.0);
        }
        CHECK(interval == 1.0);
        interval = 0.05;
        CHECK(interval == 0.05);
    }
    const auto graphics_interaction_tick = graphics_interaction_source.find(
        "event Tick(float DeltaTime)");
    CHECK(graphics_interaction_tick != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "var float LastObservedRealTime;") != std::string::npos);
    CHECK(graphics_interaction_source.find("LastObservedWorld") ==
          std::string::npos);
    const auto graphics_world_reset = graphics_interaction_source.find(
        "if (CurrentWorld.RealTimeSeconds < LastObservedRealTime ||",
        graphics_interaction_tick);
    const auto graphics_timer_guard = graphics_interaction_source.find(
        "CurrentWorld.RealTimeSeconds < NextReadRealTime",
        graphics_interaction_tick);
    CHECK(graphics_world_reset != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "!(CurrentMapName ~= LastRuntimeGuardMapName)",
        graphics_world_reset) != std::string::npos);
    CHECK(graphics_timer_guard != std::string::npos);
    CHECK(graphics_world_reset < graphics_timer_guard);
    CHECK(graphics_interaction_source.find(
        "NextReadRealTime = 0.0;", graphics_world_reset) <
          graphics_timer_guard);
    CHECK(graphics_interaction_source.find(
        "LastVotedMap = \"\";", graphics_world_reset) <
          graphics_timer_guard);
    CHECK(graphics_interaction_source.find(
        "LastObservedRealTime = CurrentWorld.RealTimeSeconds;",
        graphics_world_reset) < graphics_timer_guard);
    CHECK(graphics_interaction_source.find(
        "KF2OPT_GFX_MENU schema=2 state=applied") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "function bool EnsureTurretWeaponMaterial(KFWeapon Weapon)") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "KFWeap_HRG_Warthog(Weapon)") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "KFWeap_AutoTurret(Weapon)") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "Weapon.Mesh.GetNumElements() <= 2") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "Weapon.WeaponMICs.Length = 3") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "Weapon.Mesh.CreateAndSetMaterialInstanceConstant(2)") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "KF2OPT_WEAPON_MIC state=repaired") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "const RuntimeGuardInitialSeconds=0.05;") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "const RuntimeGuardMaximumSeconds=0.25;") != std::string::npos);
    const auto runtime_guard_function = graphics_interaction_source.find(
        "function GuardRuntimeActors(WorldInfo CurrentWorld)");
    const auto runtime_guard = graphics_interaction_source.find(
        "GuardRuntimeActors(CurrentWorld);", graphics_interaction_tick);
    const auto weapon_standalone_guard = graphics_interaction_source.find(
        "if (CurrentWorld.NetMode != NM_Standalone)", runtime_guard);
    CHECK(runtime_guard_function != std::string::npos);
    CHECK(runtime_guard != std::string::npos);
    CHECK(weapon_standalone_guard != std::string::npos);
    CHECK(runtime_guard < weapon_standalone_guard);
    CHECK(count_occurrences(graphics_interaction_source,
        "foreach CurrentWorld.DynamicActors(class'Actor', Candidate)") == 1);
    CHECK(graphics_interaction_source.find(
        "DynamicActors(class'KFWeap_HRG_Warthog'") == std::string::npos);
    CHECK(graphics_interaction_source.find(
        "DynamicActors(class'KFWeap_AutoTurret'") == std::string::npos);
    CHECK(graphics_interaction_source.find(
        "DynamicActors(class'KFPawn'") == std::string::npos);
    CHECK(graphics_interaction_source.find(
        "RuntimeGuardIntervalSeconds = FMin(", runtime_guard_function) !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "RuntimeGuardMaximumSeconds,", runtime_guard_function) !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "RuntimeGuardIntervalSeconds = RuntimeGuardInitialSeconds;",
        runtime_guard_function) != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "ResetRuntimeGuardCadence();", graphics_world_reset) <
          runtime_guard);
    {
        double interval = 0.05;
        for (int clean_scan = 0; clean_scan < 4; ++clean_scan) {
            interval = std::min(0.25, interval * 2.0);
        }
        CHECK(interval == 0.25);
        interval = 0.05;
        CHECK(interval == 0.05);
    }
    CHECK(fire_affliction_source.find(
        "class KF2OptimizerFireAffliction extends KFAffliction_Fire") !=
          std::string::npos);
    CHECK(fire_affliction_source.find(
        "function bool AdoptExisting(KFAffliction_Fire Existing)") !=
          std::string::npos);
    CHECK(fire_affliction_source.find(
        "Existing.Class != class'KFAffliction_Fire'") !=
          std::string::npos);
    CHECK(fire_affliction_source.find(
        "RemainingActiveTime = PawnOwner.GetTimerRate(") !=
          std::string::npos);
    CHECK(fire_affliction_source.find(
        "PawnOwner.ClearTimer(nameof(DeActivate), Existing)") !=
          std::string::npos);
    CHECK(fire_affliction_source.find(
        "RemainingActiveTime, false, nameof(DeActivate), self") !=
          std::string::npos);
    CHECK(fire_affliction_source.find(
        "Existing.BurningEffect = None") != std::string::npos);
    CHECK(fire_affliction_source.find("`warn(\"FIRE\")") ==
          std::string::npos);
    CHECK(fire_affliction_source.find(
        "BurningEffect.SetTemplate(BurningTemplate)") != std::string::npos);
    CHECK(fire_affliction_source.find(
        "PawnOwner.PlaySoundBase(OnFireSound, true, true, true)") !=
          std::string::npos);
    CHECK(fire_affliction_source.find(
        "PawnOwner.PlaySoundBase(OnFireEndSound, true, true)") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "function bool EnsureWeaponClassFallback(KFPawn Pawn)") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "KFPawn_Human(Pawn) == None") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "Pawn.WeaponClassForAttachmentTemplate = CurrentWeapon.Class") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "class'KF2OptimizerWeaponFallback'") !=
          std::string::npos);
    CHECK(weapon_fallback_source.find(
        "class KF2OptimizerWeaponFallback extends KFWeapon") !=
          std::string::npos);
    CHECK(weapon_fallback_source.find(
        "GetKFProjectileClassByFiringMode(") != std::string::npos);
    CHECK(weapon_fallback_source.find("return None;") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "KF2OPT_WEAPON_CLASS_FALLBACK state=active") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "function bool ReplaceExistingFireAffliction(KFPawn Pawn)") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "ExistingBase.Class != class'KFAffliction_Fire'") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "AfflictionTickArray.Find(ExistingBase)") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "AfflictionTickArray[TickIndex] = Replacement") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "Afflictions[AF_FirePanic] = Replacement") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "AfflictionClasses[AF_FirePanic] =\n"
        "            class'KF2OptimizerFireAffliction'") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "KF2OPT_FIRE_AFFLICTION state=active") != std::string::npos);
    CHECK(graphics_interaction_source.find(
        "UpdatedAfflictionCount > 0 || ReplacedAfflictionCount > 0") !=
          std::string::npos);
    CHECK(graphics_viewport_source.find(
        "class'KF2OptimizerOnlineContextInteraction'") != std::string::npos);
    CHECK(graphics_viewport_source.find(
        "InsertInteraction(OnlineMonitor)") != std::string::npos);
    CHECK(online_context_source.find(
        "KF2OPT_SESSION_CONTEXT schema=2 state=") != std::string::npos);
    CHECK(online_context_source.find("NM_Client") != std::string::npos);
    CHECK(online_context_source.find("online_client_read_only") !=
          std::string::npos);
    CHECK(online_context_source.find("NM_ListenServer") != std::string::npos);
    CHECK(online_context_source.find("online_host_read_only") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "function ReportOnlineCorpseCapability(WorldInfo CurrentWorld)") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "KFGoreManager(CurrentWorld.MyGoreEffectManager)") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "KF2OPT_ONLINE_CORPSE state=available") != std::string::npos);
    CHECK(online_context_source.find(
        "KF2OPT_ONLINE_CORPSE state=pool") != std::string::npos);
    CHECK(online_context_source.find("local_only=true readback=verified") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "function bool TrySleepOneOnlineCorpse(WorldInfo CurrentWorld)") !=
          std::string::npos);
    CHECK(online_context_source.find("Candidate.TimeOfDeath <= 0.0") !=
          std::string::npos);
    CHECK(online_context_source.find("Candidate.IsAliveAndWell()") !=
          std::string::npos);
    CHECK(online_context_source.find("Candidate.SpecialMove == SM_DeathAnim") !=
          std::string::npos);
    CHECK(online_context_source.find("Candidate.Mesh.PutRigidBodyToSleep()") !=
          std::string::npos);
    CHECK(online_context_source.find("Candidate.Mesh.RigidBodyIsAwake()") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "KF2OPT_ONLINE_CORPSE_ACTION state=sleep") != std::string::npos);
    CHECK(online_context_source.find(
        "function bool TryEnforceOnlineCorpseCapacity(WorldInfo CurrentWorld)") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "GoreManager.RemoveAndDeleteCorpse(Index)") != std::string::npos);
    CHECK(online_context_source.find(
        "GoreManager.MaxDeadBodies = Quality") != std::string::npos);
    CHECK(online_context_source.find(
        "GoreManager.MaxDeadBodies != Quality") != std::string::npos);
    CHECK(online_context_source.find(
        "var int OnlineCorpseOriginalMaximum") != std::string::npos);
    CHECK(online_context_source.find(
        "var bool bOnlineCorpseOriginalMaximumCaptured") !=
          std::string::npos);
    const auto corpse_capture_function = online_context_source.find(
        "function bool CaptureOnlineCorpseMaximum(");
    const auto corpse_restore_function = online_context_source.find(
        "function bool RestoreOnlineCorpseMaximum(");
    const auto online_apply_function = online_context_source.find(
        "function bool ApplyOnlineGraphicsControl(");
    const auto online_sleep_function = online_context_source.find(
        "function bool TrySleepOneOnlineCorpse(");
    CHECK(corpse_capture_function != std::string::npos);
    CHECK(corpse_restore_function != std::string::npos);
    CHECK(online_apply_function != std::string::npos);
    CHECK(online_sleep_function != std::string::npos);
    const auto corpse_capture_body = online_context_source.substr(
        corpse_capture_function,
        corpse_restore_function - corpse_capture_function);
    CHECK(corpse_capture_body.find(
        "if (bOnlineCorpseOriginalMaximumCaptured)") != std::string::npos);
    CHECK(corpse_capture_body.find(
        "OnlineCorpseOriginalMaximum = GoreManager.MaxDeadBodies") !=
          std::string::npos);
    const auto corpse_restore_body = online_context_source.substr(
        corpse_restore_function,
        online_apply_function - corpse_restore_function);
    CHECK(corpse_restore_body.find(
        "GoreManager.MaxDeadBodies = OnlineCorpseOriginalMaximum") !=
          std::string::npos);
    const auto corpse_restore_mismatch = corpse_restore_body.find(
        "GoreManager.MaxDeadBodies != OnlineCorpseOriginalMaximum");
    const auto corpse_snapshot_clear = corpse_restore_body.find(
        "ClearOnlineCorpseMaximumSnapshot()");
    CHECK(corpse_restore_mismatch != std::string::npos);
    CHECK(corpse_snapshot_clear != std::string::npos);
    CHECK(corpse_restore_mismatch < corpse_snapshot_clear);
    CHECK(corpse_restore_body.find("return false", corpse_restore_mismatch) <
          corpse_snapshot_clear);
    const auto online_apply_body = online_context_source.substr(
        online_apply_function, online_sleep_function - online_apply_function);
    CHECK(online_apply_body.find("IsAdaptiveControlResource(Resource)") !=
          std::string::npos);
    CHECK(online_apply_body.find("IsAdaptiveQualityResource(Resource)") !=
          std::string::npos);
    CHECK(online_apply_body.find(
        "IsOnlineAdaptiveQualityResource(Resource)") != std::string::npos);
    CHECK(online_apply_body.find(
        "bLastOnlineGraphicsCapabilityRejected = true") !=
          std::string::npos);
    CHECK(online_apply_body.find("reason=capability_unavailable") !=
          std::string::npos);
    const auto corpse_capture_call = online_apply_body.find(
        "CaptureOnlineCorpseMaximum(CurrentWorld, GoreManager)");
    const auto corpse_limit_write = online_apply_body.find(
        "GoreManager.MaxDeadBodies = Quality");
    CHECK(corpse_capture_call != std::string::npos);
    CHECK(corpse_limit_write != std::string::npos);
    CHECK(corpse_capture_call < corpse_limit_write);
    const auto online_enable_start = online_apply_body.find("if (Resource ~= \"enable\")");
    const auto online_enable_end = online_apply_body.find("if (Quality < 10 || Quality > 100)");
    CHECK(online_enable_start != std::string::npos &&
          online_enable_end != std::string::npos);
    const auto online_enable_body = online_apply_body.substr(
        online_enable_start, online_enable_end - online_enable_start);
    CHECK(online_enable_is_transactional(online_enable_body));
    const auto enable_failure_restore = online_apply_body.find(
        "RestoreOnlineEnableMaximum(GoreManager);",
        corpse_limit_write);
    CHECK(enable_failure_restore != std::string::npos);
    CHECK(enable_failure_restore < online_apply_body.find(
        "bOnlineGraphicsEnabled = true"));
    auto premature_enable = online_enable_body;
    const auto previous_capture = premature_enable.find(
        "OnlineCorpseEnablePreviousMaximum = GoreManager.MaxDeadBodies;");
    CHECK(previous_capture != std::string::npos);
    premature_enable.replace(previous_capture,
        std::string_view{"OnlineCorpseEnablePreviousMaximum = GoreManager.MaxDeadBodies;"}.size(),
        "OnlineCorpseEnablePreviousMaximum = OnlineCorpseOriginalMaximum;");
    CHECK(!online_enable_is_transactional(premature_enable));
    CHECK(!online_enable_is_transactional(
        online_enable_body + "GoreManager.MaxDeadBodies = Quality;"));
    const auto enable_restore_start = online_context_source.find(
        "function bool RestoreOnlineEnableMaximum(");
    CHECK(enable_restore_start != std::string::npos);
    const auto enable_restore_body = online_context_source.substr(
        enable_restore_start, online_apply_function - enable_restore_start);
    const auto enable_restore_write = enable_restore_body.find(
        "GoreManager.MaxDeadBodies = OnlineCorpseEnablePreviousMaximum;");
    const auto enable_restore_mismatch = enable_restore_body.find(
        "GoreManager.MaxDeadBodies != OnlineCorpseEnablePreviousMaximum");
    const auto enable_restore_reject = enable_restore_body.find(
        "return false;", enable_restore_mismatch);
    const auto enable_debt_clear = enable_restore_body.find(
        "bOnlineCorpseEnableRestorePending = false;");
    CHECK(enable_restore_write != std::string::npos &&
          enable_restore_mismatch != std::string::npos &&
          enable_restore_reject != std::string::npos &&
          enable_debt_clear != std::string::npos);
    CHECK(enable_restore_write < enable_restore_mismatch &&
          enable_restore_mismatch < enable_restore_reject &&
          enable_restore_reject < enable_debt_clear);
    CHECK(enable_restore_body.find("ownership=retained") != std::string::npos);
    CHECK(enable_restore_body.find("ClearOnlineCorpseMaximumSnapshot") ==
          std::string::npos);
    const auto capacity_start = online_context_source.find(
        "function bool TryEnforceOnlineCorpseCapacity(");
    const auto capacity_end = online_context_source.find(
        "function ResetOnlineGraphicsListenerHealth(", capacity_start);
    CHECK(capacity_start != std::string::npos && capacity_end != std::string::npos);
    const auto capacity_body = online_context_source.substr(
        capacity_start, capacity_end - capacity_start);
    const auto pending_capacity_hold = capacity_body.find(
        "if (!bOnlineGraphicsEnabled || bOnlineCorpseEnableRestorePending ||");
    CHECK(pending_capacity_hold != std::string::npos);
    CHECK(capacity_body.find("return false;", pending_capacity_hold) <
          capacity_body.find("GoreManager ="));
    const auto sleep_body = online_context_source.substr(
        online_sleep_function, capacity_start - online_sleep_function);
    CHECK(online_context_source.find("const OnlineCorpseScanBudget=64;") !=
          std::string::npos);
    CHECK(online_pool_scan_is_bounded(sleep_body,
        "OnlineCorpseSleepScanCursor", "OnlineCorpseLastSleepRealTime"));
    CHECK(online_pool_scan_is_bounded(capacity_body,
        "OnlineCorpseCapacityScanCursor", "OnlineCorpseLastCapacityRealTime"));
    CHECK(sleep_body.find(
        "CurrentWorld.RealTimeSeconds - OnlineCorpseLastSleepRealTime < 0.15") !=
          std::string::npos);
    CHECK(capacity_body.find(
        "CurrentWorld.RealTimeSeconds - OnlineCorpseLastCapacityRealTime < 0.45") !=
          std::string::npos);
    CHECK(count_occurrences(sleep_body, "PutRigidBodyToSleep();") == 1);
    CHECK(count_occurrences(capacity_body, "RemoveAndDeleteCorpse(Index)") == 1);
    CHECK(count_occurrences(online_context_source,
        "OnlineCorpseSleepScanCursor = 0;") == 2);
    CHECK(count_occurrences(online_context_source,
        "OnlineCorpseCapacityScanCursor = 0;") == 2);
    CHECK(count_occurrences(online_context_source,
        "OnlineCorpseLastSleepRealTime = 0.0;") == 2);
    CHECK(count_occurrences(online_context_source,
        "OnlineCorpseLastCapacityRealTime = 0.0;") == 2);
    auto unbounded_scan = sleep_body;
    const std::string budget_rule = "ScanCount = Min(OnlineCorpseScanBudget, PoolLength);";
    unbounded_scan.replace(unbounded_scan.find(budget_rule), budget_rule.size(),
                           "ScanCount = PoolLength;");
    CHECK(!online_pool_scan_is_bounded(unbounded_scan,
        "OnlineCorpseSleepScanCursor", "OnlineCorpseLastSleepRealTime"));
    auto stuck_scan = capacity_body;
    const std::string advance_rule = "OnlineCorpseCapacityScanCursor = (Index + 1) % PoolLength;";
    stuck_scan.erase(stuck_scan.find(advance_rule), advance_rule.size());
    CHECK(!online_pool_scan_is_bounded(stuck_scan,
        "OnlineCorpseCapacityScanCursor", "OnlineCorpseLastCapacityRealTime"));
    // Source-bound scheduling model, not execution of UnrealScript/physics.
    for (const int length : {0, 1, 63, 64, 65, 2000}) {
        for (const bool failed_readback : {false, true}) {
            std::vector<bool> seen(static_cast<std::size_t>(length));
            int cursor = 0;
            for (int visit = 0; visit <= length; ++visit) {
                int inspected = 0;
                int actions = 0;
                for (; inspected < std::min(64, length);) {
                    const auto index = cursor;
                    cursor = (index + 1) % length;
                    seen[index] = true;
                    ++inspected;
                    // Permanent readback failure at every slot must still
                    // progress; misses process only the admitted budget.
                    if (failed_readback) { ++actions; break; }
                }
                CHECK(inspected <= 64 && actions <= 1);
                CHECK(length == 0 || (cursor >= 0 && cursor < length));
            }
            CHECK(std::all_of(seen.begin(), seen.end(), [](bool v) { return v; }));
            // Shrunken/repopulated pools never use an old out-of-range index.
            for (const int changed_length : {0, 1, 7, 2000}) {
                if (changed_length == 0) continue;
                cursor = std::clamp(cursor, 0, changed_length - 1);
                CHECK(cursor >= 0 && cursor < changed_length);
            }
        }
    }
    for (const double interval : {0.15, 0.45}) {
        for (const int fps : {30, 60, 120, 240}) {
            double last_attempt = 0.0;
            int attempts = 0;
            for (int frame = 1; frame <= fps * 10; ++frame) {
                const auto now = static_cast<double>(frame) / fps;
                if (now - last_attempt < interval) continue;
                last_attempt = now; // Includes missing manager, miss and failure.
                ++attempts;
            }
            CHECK(attempts > 0 && attempts <= static_cast<int>(10.0 / interval));
            last_attempt = 0.0; // Existing world/session resets clear cadence.
            CHECK(1.0 - last_attempt >= interval);
        }
    }
    const auto capability_start = online_context_source.find(
        "function ReportOnlineCorpseCapability(");
    const auto capability_end = online_context_source.find(
        "function bool RestoreOnlineSessionState(", capability_start);
    CHECK(capability_start != std::string::npos && capability_end != std::string::npos);
    const auto capability_body = online_context_source.substr(
        capability_start, capability_end - capability_start);
    const auto unavailable_guard = capability_body.find("if (!bOnlineCorpseUnavailableReported)");
    const auto unavailable_log = capability_body.find("state=unavailable reason=no_gore_manager");
    CHECK(unavailable_guard != std::string::npos && unavailable_guard < unavailable_log);
    CHECK(capability_body.find("bOnlineCorpseUnavailableReported = true;",
        unavailable_guard) < unavailable_log);
    const auto recovered = capability_body.find("if (bOnlineCorpseUnavailableReported)");
    CHECK(recovered > unavailable_log && recovered < capability_body.find("state=available"));
    CHECK(capability_body.find("bOnlineCorpseCapabilityReported = false;", recovered) <
          capability_body.find("state=available"));
    CHECK(capability_body.find("bOnlineCorpseUnavailableReported = false;", recovered) <
          capability_body.find("state=available"));
    CHECK(count_occurrences(online_context_source,
        "bOnlineCorpseUnavailableReported = false;") == 3);
    CHECK(capability_body.find("SinceLastReport < 0.5") != std::string::npos);
    CHECK(capability_body.find("SinceLastReport < 5.0") != std::string::npos);
    CHECK(capability_body.find("LastReportedOnlineCorpsePool == PoolLength") !=
          std::string::npos);
    CHECK(capability_body.find("LastReportedOnlineCorpseMaximum == Maximum") !=
          std::string::npos);
    CHECK(count_occurrences(capability_body, "GoreManager.CorpsePool.Length") == 1);
    CHECK(capability_body.find("AllActors") == std::string::npos);
    CHECK(count_occurrences(online_context_source,
        "OnlineCorpseLastPoolReportRealTime = 0.0;") == 2);
    // Source-bound cadence model, not live UnrealScript execution.
    for (const int fps : {30, 60, 120, 240}) {
        double last_report = 0.0;
        int reports = 0;
        for (int frame = 1; frame <= fps * 10; ++frame) {
            const double now = static_cast<double>(frame) / fps;
            if (now - last_report < 0.5) continue;
            last_report = now;
            ++reports;
        }
        CHECK(reports > 0 && reports <= 20);
        last_report = 0.0;
        reports = 0;
        for (int frame = 1; frame <= fps * 10; ++frame) {
            const double now = static_cast<double>(frame) / fps;
            if (now - last_report < 5.0) continue;
            last_report = now;
            ++reports;
        }
        CHECK(reports == 2);
    }
    const auto bridge_start = online_corpse_controller_source.find(
        "function KF2OptimizerOnlineContextInteraction GetOnlineInteraction()");
    const auto bridge_end = online_corpse_controller_source.find(
        "function int FindFrozenCorpse(", bridge_start);
    CHECK(bridge_start != std::string::npos && bridge_end != std::string::npos);
    const auto bridge_body = online_corpse_controller_source.substr(
        bridge_start, bridge_end - bridge_start);
    CHECK(bridge_body.find("KF2OptimizerGraphicsViewport(CurrentEngine.GameViewport)") !=
          std::string::npos);
    CHECK(bridge_body.find("return CurrentViewport.GetOnlineMonitor();") != std::string::npos);
    CHECK(bridge_body.find("FindObject") == std::string::npos);
    CHECK(bridge_body.find("PathName") == std::string::npos);
    CHECK(graphics_viewport_source.find(
        "var private KF2OptimizerOnlineContextInteraction OnlineMonitor;") != std::string::npos);
    CHECK(graphics_viewport_source.find(
        "OnlineMonitor == None || GlobalInteractions.Find(OnlineMonitor) == -1") !=
          std::string::npos);
    CHECK(graphics_viewport_source.find("var WorldInfo") == std::string::npos);
    CHECK(graphics_viewport_source.find("var Actor") == std::string::npos);
    const auto maximum_clear_start = online_context_source.find(
        "function ClearOnlineCorpseMaximumSnapshot()");
    const auto maximum_clear_end = online_context_source.find(
        "function DiscardOnlineCorpseMaximumSnapshot(");
    CHECK(maximum_clear_start != std::string::npos &&
          maximum_clear_end != std::string::npos);
    CHECK(online_context_source.substr(maximum_clear_start,
        maximum_clear_end - maximum_clear_start).find(
            "bOnlineCorpseEnableRestorePending = false;") != std::string::npos);
    // Source-bound failure model, not live UnrealScript/engine injection.
    struct EnableState {
        int limit{20}, original{}, previous{}, sequence{};
        bool captured{}, pending{}, enabled{};
        bool apply(int requested, int prerequisite_failure,
                   bool write_mismatch, bool rollback_mismatch) {
            if (!captured) { original = limit; captured = true; }
            if (pending) {
                if (rollback_mismatch) return false;
                limit = previous;
                pending = false;
            }
            if (prerequisite_failure != 0) return false;
            previous = limit;
            if (write_mismatch) {
                limit = requested + 1;
                pending = true;
                if (!rollback_mismatch) { limit = previous; pending = false; }
                return false;
            }
            limit = requested;
            enabled = true;
            ++sequence;
            return true;
        }
        bool restore(bool mismatch) {
            if (mismatch) return false;
            limit = original;
            captured = pending = enabled = false;
            return true;
        }
    };
    for (const int maximum : {4, 20, 99, 100, 2000}) {
        for (const int prerequisite : {1, 2, 3}) {
            EnableState state;
            CHECK(!state.apply(maximum, prerequisite, false, false));
            CHECK(state.limit == 20 && state.sequence == 0 && !state.enabled);
            CHECK(!state.apply(maximum, 0, true, false));
            CHECK(state.limit == 20 && !state.pending && state.sequence == 0);
            CHECK(!state.apply(maximum, 0, true, true));
            CHECK(state.pending && state.previous == 20 && !state.enabled);
            CHECK(!state.apply(maximum, prerequisite, false, false));
            CHECK(state.limit == 20 && !state.pending && state.sequence == 0);
            CHECK(state.apply(maximum, 0, false, false));
            const int next = maximum == 4 ? 2000 : 4;
            CHECK(!state.apply(next, prerequisite, false, false));
            CHECK(state.limit == maximum && state.sequence == 1 && state.enabled);
            CHECK(!state.apply(next, 0, true, false));
            CHECK(state.limit == maximum && !state.pending && state.sequence == 1);
            CHECK(!state.apply(next, 0, true, true));
            CHECK(state.pending && state.previous == maximum && state.original == 20);
            const auto partial = state.limit;
            CHECK(!state.apply(next, 0, false, true));
            CHECK(state.limit == partial && state.previous == maximum && state.sequence == 1);
            CHECK(!state.apply(next, prerequisite, false, false));
            CHECK(state.limit == maximum && !state.pending && state.sequence == 1);
            CHECK(state.apply(next, 0, false, false));
            CHECK(state.limit == next && state.sequence == 2 && state.original == 20);
            CHECK(!state.apply(maximum, 0, true, true));
            CHECK(!state.restore(true) && state.pending && state.captured);
            CHECK(state.restore(false) && state.limit == 20 && !state.pending);
            state.limit = 60; // New World: no previous-world restore ownership.
            CHECK(!state.apply(maximum, prerequisite, false, false));
            CHECK(state.original == 60 && state.limit == 60 && state.sequence == 2);
        }
    }
    CHECK(online_apply_body.find(
        "RestoreOnlineCorpseMaximum(CurrentWorld, \"disable\")") !=
          std::string::npos);
    const auto online_disable_start = online_apply_body.find(
        "if (Resource ~= \"disable\")");
    const auto online_disable_end = online_apply_body.find(
        "if (!class'KF2OptimizerAdaptiveGraphics'.static.\n"
        "            IsAdaptiveQualityResource(Resource))",
        online_disable_start);
    CHECK(online_disable_start != std::string::npos);
    CHECK(online_disable_end != std::string::npos);
    const auto online_disable_body = online_apply_body.substr(
        online_disable_start, online_disable_end - online_disable_start);
    const auto online_disable_commit = online_disable_body.find(
        "bOnlineGraphicsEnabled = false;");
    const auto online_disable_disarm = online_disable_body.find(
        "bOnlineCorpseSleepArmed = false;");
    const auto online_disable_sequence = online_disable_body.find(
        "OnlineGraphicsLastSequence = Sequence;");
    const auto online_disable_restore = online_disable_body.find(
        "class'KF2OptimizerAdaptiveGraphics'.static.RestoreOriginal(");
    const auto online_disable_fixed = online_disable_body.find(
        "EnsureOnlineFixedEffectsBaseline(CurrentWorld)");
    const auto online_disable_corpse_limit = online_disable_body.find(
        "RestoreOnlineCorpseMaximum(CurrentWorld, \"disable\")");
    CHECK(online_disable_commit != std::string::npos);
    CHECK(online_disable_disarm != std::string::npos);
    CHECK(online_disable_sequence != std::string::npos);
    CHECK(online_disable_restore != std::string::npos);
    CHECK(online_disable_fixed != std::string::npos);
    CHECK(online_disable_corpse_limit != std::string::npos);
    CHECK(online_disable_commit < online_disable_disarm);
    CHECK(online_disable_disarm < online_disable_sequence);
    CHECK(online_disable_sequence < online_disable_restore);
    CHECK(online_disable_restore < online_disable_fixed);
    CHECK(online_disable_fixed < online_disable_corpse_limit);
    CHECK(online_disable_body.find(
        "return bCorpseMaximumRestored;") != std::string::npos);
    CHECK(online_disable_body.find("readback=deferred") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "RestoreOnlineSessionState(CurrentWorld, \"main_menu\")") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "function NotifyGameSessionEnded()") != std::string::npos);
    CHECK(online_context_source.find(
        "RestoreOnlineSessionState(CurrentWorld, \"session_end\")") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "KF2OPT_ONLINE_CORPSE_ACTION state=capacity") != std::string::npos);
    CHECK(online_context_source.find(
        "local_only=true readback=verified") != std::string::npos);
    CHECK(online_context_source.find("bOnlineCorpseSleepArmed = true") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "CurrentWorld.DynamicActors(\n"
        "        class'KF2OptimizerAdaptiveControlListener'") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "PrimaryController.Spawn(\n"
        "            class'KF2OptimizerAdaptiveControlListener'") !=
          std::string::npos);
    CHECK(online_context_source.find("SetTimer(") == std::string::npos);
    CHECK(online_context_source.find("ConsoleCommand(") == std::string::npos);
    CHECK(online_context_source.find(
        "function bool ApplyOnlineGraphicsControl(") != std::string::npos);
    CHECK(online_context_source.find(
        "class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "KF2OptimizerTelemetryProbe'.default.AdaptiveControlToken") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "var KF2OptimizerAdaptiveControlListener") == std::string::npos);
    CHECK(online_context_source.find(
        "bOnlineGraphicsListenerStarted") == std::string::npos);
    CHECK(online_context_source.find(
        "CurrentListener.LinkState != STATE_Listening") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "CurrentListener.OnlineCorpseController == None") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "OnlineGraphicsListenerNextCheckRealTime") != std::string::npos);
    CHECK(online_context_source.find(
        "CurrentWorld.RealTimeSeconds + 1.0") != std::string::npos);
    CHECK(online_context_source.find(
        "FMin(8.0, OnlineGraphicsListenerRetryDelay * 2.0)") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "if (OnlineGraphicsListenerMapName != MapName)") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "ResetOnlineGraphicsListenerHealth(\"\")") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "if (Status == LastOnlineGraphicsListenerStatus)") !=
          std::string::npos);
    CHECK(listener_source.find("event Destroyed()") != std::string::npos);
    CHECK(listener_source.find("OnlineCorpseController.Destroy()") ==
          std::string::npos);
    CHECK(listener_source.find(
        "function bool EnsureOnlineCorpseController()") !=
          std::string::npos);
    CHECK(listener_source.find(
        "class'KF2OptimizerOnlineCorpseController', CurrentController") !=
          std::string::npos);
    CHECK(listener_source.find(
        "AcceptClass = class'KF2OptimizerOnlineGraphicsControlConnection'") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "struct OnlineFrozenCorpseState") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "var bool bRestorePending;") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "function bool FreezeOneOnlineCorpse()") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "function bool RestoreOneOnlineCorpse()") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "var string CorpseId;") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "var int ReleaseScanCursor;") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "function int AdoptRestoreOwnership(") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "Replacement.AdoptRestoreOwnership(self)") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "state=ownership_transferred") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "function PrepareForWorldTeardown()") != std::string::npos);
    CHECK(online_context_source.find(
        "CurrentListener.EnsureOnlineCorpseController()") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "CurrentController.PrepareForWorldTeardown()") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "bOnlineSessionEnding = true") != std::string::npos);
    CHECK(online_context_source.find(
        "function bool IsOnlineSessionEnding()") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "CurrentInteraction.IsOnlineSessionEnding()") !=
          std::string::npos);
    const auto online_release_start = online_corpse_controller_source.find(
        "function bool ReleaseOneOnlineCorpse(bool bRestoreAll)");
    const auto online_release_end = online_corpse_controller_source.find(
        "function bool RestoreOneOnlineCorpse()", online_release_start);
    CHECK(online_release_start != std::string::npos);
    CHECK(online_release_end != std::string::npos);
    const auto online_release_body = online_corpse_controller_source.substr(
        online_release_start, online_release_end - online_release_start);
    CHECK(online_release_body.find(
        "while (FrozenCorpses.Length > 0 && Scanned < 8)") !=
          std::string::npos);
    CHECK(online_release_body.find(
        "Candidate == None || Candidate.bDeleteMe") != std::string::npos);
    CHECK(online_release_body.find(
        "CurrentId != FrozenCorpses[Index].CorpseId") !=
          std::string::npos);
    CHECK(online_release_body.find(
        "IsOnlineCorpseRecycledStateSafe(Candidate)") !=
          std::string::npos);
    CHECK(online_release_body.find(
        "reused_state_unverified") != std::string::npos);
    CHECK(online_release_body.find(
        "!IsOnlineCorpseInPool(Candidate, GoreManager)") !=
          std::string::npos);
    CHECK(online_release_body.find(
        "FrozenCorpses[Index].bRestorePending ||") !=
          std::string::npos);
    CHECK(online_release_body.find("\"freeze_rollback\"") !=
          std::string::npos);
    CHECK(online_release_body.find(
        "TryRestoreOnlineCorpse(Index, bRestoreAll ?") !=
          std::string::npos);
    CHECK(online_release_body.find(
        "(Index + 1) % FrozenCorpses.Length") != std::string::npos);
    CHECK(online_restore_advances_after_failure(online_release_body));
    auto blocked_release = online_release_body;
    const auto attempt = blocked_release.find(
        "TryRestoreOnlineCorpse(Index, bRestoreAll ?");
    const auto failure_exit = blocked_release.find(
        "        ReleaseScanCursor = FrozenCorpses.Length", attempt);
    CHECK(failure_exit != std::string::npos);
    blocked_release.insert(failure_exit, "        return false;\n");
    CHECK(!online_restore_advances_after_failure(blocked_release));
    CHECK(!online_restore_progress(9, 8, true));
    for (const auto length : {0, 1, 2, 8, 9, 16, 17, 2000}) {
        CHECK(online_restore_progress(length, -1));
        CHECK(online_restore_progress(length, -2));
        CHECK(online_restore_progress(length, 0));
        CHECK(online_restore_progress(length, length / 2));
        CHECK(online_restore_progress(length, length - 1));
    }
    CHECK(online_corpse_controller_source.find(
        "state=release_failed corpse_id=") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "ownership=retained local_only=true") != std::string::npos);
    const auto online_tick_start = online_corpse_controller_source.find(
        "event Tick(float DeltaTime)");
    const auto online_tick_prune = online_corpse_controller_source.find(
        "PruneOneOnlineFrozenCorpse()", online_tick_start);
    const auto online_tick_freeze = online_corpse_controller_source.find(
        "FreezeOneOnlineCorpse()", online_tick_start);
    CHECK(online_tick_start != std::string::npos);
    CHECK(online_tick_prune != std::string::npos);
    CHECK(online_tick_freeze != std::string::npos);
    CHECK(online_tick_prune < online_tick_freeze);
    CHECK(online_corpse_controller_source.find(
        "reason=world_teardown count=") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "safe_boundary=world_destroy local_only=true") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "WorldInfo.TimeSeconds - Candidate.TimeOfDeath < 10.0") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "VSizeSq(Candidate.Location - LocalPC.Pawn.Location) < 640000.0") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "Candidate.Mesh.RigidBodyIsAwake()") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "Candidate.SetCollision(false, false,") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "Candidate.CollisionComponent.SetBlockRigidBody(false)") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "Candidate.SetTickIsDisabled(true)") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "Candidate.SetPhysics(PHYS_None)") != std::string::npos);
    const auto online_freeze_start = online_corpse_controller_source.find(
        "function bool FreezeOneOnlineCorpse()");
    const auto online_freeze_end = online_corpse_controller_source.find(
        "function bool ApplyOneFixedMinimumCorpseLod()", online_freeze_start);
    CHECK(online_freeze_start != std::string::npos);
    CHECK(online_freeze_end != std::string::npos);
    const auto online_freeze_body = online_corpse_controller_source.substr(
        online_freeze_start, online_freeze_end - online_freeze_start);
    const auto online_ledger = online_freeze_body.find(
        "FrozenCorpses.AddItem(Original)");
    const auto online_mutation = online_freeze_body.find(
        "Candidate.SetCollision(false, false,");
    CHECK(online_ledger != std::string::npos);
    CHECK(online_ledger < online_mutation);
    CHECK(count_occurrences(
        online_freeze_body, ".bRestorePending = true;") == 2);
    CHECK(count_occurrences(
        online_freeze_body, "TryRestoreOnlineCorpse(") == 2);
    CHECK(count_occurrences(
        online_freeze_body, "FrozenCorpses.Remove(LedgerIndex, 1)") == 2);
    const auto online_prephysics_failure = online_freeze_body.find(
        "if (Candidate.bCollideActors || Candidate.bBlockActors ||");
    const auto online_postphysics_failure = online_freeze_body.find(
        "if (Candidate.Physics != PHYS_None)");
    CHECK(online_prephysics_failure != std::string::npos);
    CHECK(online_postphysics_failure != std::string::npos);
    CHECK(online_freeze_body.find(
        ".bRestorePending = true;", online_prephysics_failure) <
          online_postphysics_failure);
    CHECK(online_freeze_body.find(
        "TryRestoreOnlineCorpse(", online_prephysics_failure) <
          online_postphysics_failure);
    CHECK(online_freeze_body.find(
        ".bRestorePending = true;", online_postphysics_failure) !=
          std::string::npos);
    CHECK(online_freeze_body.find(
        "TryRestoreOnlineCorpse(", online_postphysics_failure) !=
          std::string::npos);
    const auto online_restore_start = online_corpse_controller_source.find(
        "function bool TryRestoreOnlineCorpse(");
    CHECK(online_restore_start != std::string::npos);
    const auto online_restore_body = online_corpse_controller_source.substr(
        online_restore_start, online_release_start - online_restore_start);
    CHECK(online_restore_has_complete_readback(online_restore_body));
    // Removing any single readback rejects the restore contract, including
    // ignore-encroachers and missing/unexpected collision-component cases.
    for (const auto field : online_restore_readback_fields) {
        auto incomplete_readback = online_restore_body;
        const auto position = incomplete_readback.find(field);
        CHECK(position != std::string::npos);
        incomplete_readback.erase(position, field.size());
        CHECK(!online_restore_has_complete_readback(incomplete_readback));
    }
    auto discarded_original = online_restore_body;
    discarded_original.insert(0, "FrozenCorpses.Remove(Index, 1);\n");
    CHECK(!online_restore_has_complete_readback(discarded_original));
    auto missing_component_accepted = online_restore_body;
    const auto missing_component_return = missing_component_accepted.find(
        "return false;", missing_component_accepted.find(
            "\"collision_component_missing\""));
    CHECK(missing_component_return != std::string::npos);
    missing_component_accepted.erase(missing_component_return,
                                    std::string_view{"return false;"}.size());
    CHECK(!online_restore_has_complete_readback(missing_component_accepted));
    CHECK(online_restore_body.find("Original = FrozenCorpses[Index];") !=
          std::string::npos);
    CHECK(online_restore_body.find("FrozenCorpses.Remove(") ==
          std::string::npos);
    CHECK(online_restore_body.find(
        "\"restore_readback_mismatch\");\n        return false;") !=
          std::string::npos);
    CHECK(online_restore_body.find(
        "if (Candidate.Physics != PHYS_RigidBody)") != std::string::npos);
    CHECK(online_restore_body.find(
        "(Candidate.CollisionComponent != None) !=\n"
        "            Original.bHadCollisionComponent") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "KF2OPT_ONLINE_CORPSE_ACTION state=freeze") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "KF2OPT_ONLINE_CORPSE_ACTION state=restored") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "function bool ApplyOneFixedMinimumCorpseLod()") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "Candidate.Mesh.MinLodModel = TargetMinLod") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "function bool ApplyOneSleepingCorpseSkeletonMinimum()") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "Candidate.Mesh.bNoSkeletonUpdate = true") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "KF2OPT_ONLINE_CORPSE_ACTION state=lod") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "KF2OPT_ONLINE_CORPSE_ACTION state=skeleton") != std::string::npos);
    CHECK(online_corpse_controller_source.find(
        "fixed_minimum=true local_only=true readback=verified") !=
          std::string::npos);
    CHECK(online_corpse_controller_source.find("DynamicActors") ==
          std::string::npos);
    CHECK(online_corpse_controller_source.find("AllActors") ==
          std::string::npos);
    CHECK(online_corpse_controller_source.find("RemoteRole=ROLE_None") !=
          std::string::npos);
    const auto online_lod_start = online_corpse_controller_source.find(
        "function bool ApplyOneFixedMinimumCorpseLod()");
    const auto online_skeleton_start = online_corpse_controller_source.find(
        "function bool ApplyOneSleepingCorpseSkeletonMinimum()");
    const auto online_visual_start = online_corpse_controller_source.find(
        "function bool RunOneFixedMinimumVisualAction()");
    CHECK(online_lod_start < online_skeleton_start);
    CHECK(online_skeleton_start < online_visual_start);
    CHECK(online_visual_start < online_tick_start);
    const auto online_lod_body = online_corpse_controller_source.substr(
        online_lod_start, online_skeleton_start - online_lod_start);
    const auto online_skeleton_body = online_corpse_controller_source.substr(
        online_skeleton_start, online_visual_start - online_skeleton_start);
    const auto lod_cursor = online_visual_cursor(online_lod_body);
    const auto skeleton_cursor = online_visual_cursor(online_skeleton_body);
    CHECK(!lod_cursor.empty());
    CHECK(!skeleton_cursor.empty());
    std::vector<int> sixteen_corpses;
    for (int id = 0; id < 16; ++id) sixteen_corpses.push_back(id);
    OnlineVisualScanModel no_action_model{lod_cursor == skeleton_cursor};
    CHECK(covers_online_visual_pool(no_action_model, sixteen_corpses, false));
    for (const auto length : {8, 9, 16, 17, 2000}) {
        std::vector<int> pool;
        for (int id = 0; id < length; ++id) pool.push_back(id);
        for (const auto actions : {false, true}) {
            OnlineVisualScanModel model{lod_cursor == skeleton_cursor};
            CHECK(covers_online_visual_pool(model, pool, actions));
            // Preserve progress through growth, shrink and middle removal.
            for (int id = length; id < length + 9; ++id) pool.push_back(id);
            CHECK(covers_online_visual_pool(model, pool, actions));
            pool.resize(9);
            model.cursors = {-5, 2001}; // The same clamp as the UC functions.
            CHECK(covers_online_visual_pool(model, pool, actions));
            pool.erase(pool.begin() + 3);
            CHECK(covers_online_visual_pool(model, pool, actions));
            CHECK(covers_online_visual_pool(model, {}, actions));
            CHECK(model.cursors[0] == 0 && model.cursors[1] == 0);
            CHECK(covers_online_visual_pool(model, pool, actions));
            // A new world-owned controller starts with fresh zero cursors.
            model = OnlineVisualScanModel{lod_cursor == skeleton_cursor};
            CHECK(covers_online_visual_pool(model, pool, actions));
            pool.clear();
            for (int id = 0; id < length; ++id) pool.push_back(id);
        }
    }
    for (const auto* body : {&online_lod_body, &online_skeleton_body}) {
        const auto cursor = online_visual_cursor(*body);
        CHECK(body->find("ScanCount = Min(8, PoolLength);") !=
              std::string::npos);
        CHECK(body->find(cursor + " = 0;") != std::string::npos);
        CHECK(body->find(cursor + " = Clamp(\n        " + cursor +
              ", 0, PoolLength - 1);") != std::string::npos);
        CHECK(body->find(cursor + " = (Index + 1) % PoolLength;") !=
              std::string::npos);
        CHECK(body->find(cursor + " =\n        (" + cursor +
              " + ScanCount) % PoolLength;") != std::string::npos);
        CHECK(count_occurrences(*body, cursor) == 7);
        CHECK(online_corpse_controller_source.find("var int " + cursor +
              ";") != std::string::npos);
    }
    CHECK(lod_cursor != skeleton_cursor);
    bool mismatch_progress = advances_online_attempt(
        online_freeze_body, "FreezeScanCursor", "Candidate.SetCollision(",
        "LastPhysicsMutationRealTime");
    mismatch_progress = advances_online_attempt(
        online_lod_body, lod_cursor,
        "Candidate.Mesh.MinLodModel = TargetMinLod;",
        "LastVisualMutationRealTime") && mismatch_progress;
    mismatch_progress = advances_online_attempt(
        online_skeleton_body, skeleton_cursor,
        "Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true;",
        "LastVisualMutationRealTime") && mismatch_progress;
    CHECK(mismatch_progress);
    CHECK(online_freeze_body.find(
        "WorldInfo.RealTimeSeconds - LastPhysicsMutationRealTime < 0.45") !=
          std::string::npos);
    CHECK(count_occurrences(online_freeze_body,
        "FreezeScanCursor = (Index + 1) % PoolLength;") == 1);
    CHECK(count_occurrences(online_freeze_body,
        "LastPhysicsMutationRealTime = WorldInfo.RealTimeSeconds;") == 1);
    for (const auto length : {1, 2, 8, 9, 16, 17, 2000}) {
        std::vector<int> pool;
        for (int id = 0; id < length; ++id) pool.push_back(id);
        OnlineVisualScanModel model;
        CHECK(covers_online_visual_pool(model, pool, true, true));
        CHECK(online_freeze_progress_after_rollback(length));
    }
    const auto online_visual_body = online_corpse_controller_source.substr(
        online_visual_start, online_tick_start - online_visual_start);
    CHECK(online_visual_body.find(
        "WorldInfo.RealTimeSeconds - LastVisualMutationRealTime < 0.20") !=
          std::string::npos);
    CHECK(online_visual_body.find(
        "VisualControlPhase = (VisualControlPhase + 1) % 2;") !=
          std::string::npos);
    CHECK(count_occurrences(online_visual_body,
        "bActionTaken = ApplyOneFixedMinimumCorpseLod();") == 1);
    CHECK(count_occurrences(online_visual_body,
        "bActionTaken = ApplyOneSleepingCorpseSkeletonMinimum();") == 1);
    CHECK(online_visual_body.find("else\n    {\n        bActionTaken = "
        "ApplyOneSleepingCorpseSkeletonMinimum();") != std::string::npos);
    CHECK(listener_source.find(
        "class'KF2OptimizerOnlineCorpseController'") != std::string::npos);
    CHECK(online_graphics_connection_source.find(
        "ApplyOnlineGraphicsControl(") != std::string::npos);
    CHECK(online_graphics_connection_source.find(
        "WasOnlineGraphicsCapabilityRejected()") != std::string::npos);
    CHECK(online_graphics_connection_source.find(
        " unsupported \"$Resource$\" \"$Quality") != std::string::npos);
    CHECK(online_graphics_connection_source.find("DynamicActors") ==
          std::string::npos);
    CHECK(online_graphics_connection_source.find("AllActors") ==
          std::string::npos);
    CHECK(online_graphics_connection_source.find("SetPhysics") ==
          std::string::npos);
    CHECK(online_graphics_connection_source.find("KF2Pawn") ==
          std::string::npos);
    for (const auto source : {std::string_view{connection_source},
                              std::string_view{online_graphics_connection_source}}) {
        CHECK(source.find("var bool bCleanupStarted;") !=
              std::string::npos);
        CHECK(source.find("SetTimer(ConnectionDeadlineSeconds, false,") !=
              std::string::npos);
        CHECK(source.find("nameof(ConnectionTimedOut), self") !=
              std::string::npos);
        CHECK(source.find("function RequestClose()") !=
              std::string::npos);
        CHECK(source.find("function ConnectionTimedOut()") !=
              std::string::npos);
        CHECK(source.find("event Closed()") != std::string::npos);
        CHECK(source.find("ClearTimer(nameof(ConnectionTimedOut), self)") !=
              std::string::npos);
        CHECK(count_occurrences(source, "\n    Close();") == 2);
        CHECK(count_occurrences(source, "RequestClose();") >= 4);
        CHECK(count_occurrences(source, "Destroy();") == 2);

        const auto receive_start = source.find("event ReceivedLine(");
        const auto properties_start = source.find("defaultproperties");
        CHECK(receive_start != std::string::npos);
        CHECK(properties_start != std::string::npos);
        CHECK(receive_start < properties_start);
        const auto receive_body = source.substr(
            receive_start, properties_start - receive_start);
        CHECK(receive_body.find("if (bCleanupStarted)") !=
              std::string::npos);
        CHECK(receive_body.find("\n    Close();") == std::string::npos);
        CHECK(receive_body.find("\n        Close();") == std::string::npos);
        CHECK(receive_body.find("RequestClose();") != std::string::npos);
    }
    CHECK(interaction_source.find(
        "KF2OPT_GAMEPLAY_CONTEXT schema=1 state=") != std::string::npos);
    CHECK(interaction_source.find(
        "KFPC.MyGFxManager.bMenusOpen") != std::string::npos);
    CHECK(interaction_source.find(
        "KFPC.MyGFxManager.CurrentMenu == KFPC.MyGFxManager.TraderMenu") !=
        std::string::npos);
    CHECK(interaction_source.find(
        "ReportGameplayUiState(\"unavailable\")") != std::string::npos);
    CHECK(online_context_source.find(
        "KF2OPT_GAMEPLAY_CONTEXT schema=2 state=") != std::string::npos);
    CHECK(online_context_source.find(
        "function UpdateOnlineGameplayUiState(") != std::string::npos);
    CHECK(online_context_source.find(
        "KFPC.MyGFxManager.CurrentMenu == KFPC.MyGFxManager.TraderMenu") !=
        std::string::npos);
    CHECK(online_context_source.find(
        "\"unavailable\", NetModeName, MapName") != std::string::npos);
    CHECK(online_context_source.find(
        "var int OnlineContextGeneration;") != std::string::npos);
    CHECK(online_context_source.find(
        "CurrentWorld.RealTimeSeconds < LastObservedRealTime") !=
        std::string::npos);
    CHECK(online_context_source.find(
        "!(MapName ~= LastOnlineContextMapName)") != std::string::npos);
    CHECK(online_context_source.find("replication") == std::string::npos);
    const auto online_tick = online_context_source.find(
        "event Tick(float DeltaTime)");
    const auto client_context = online_context_source.find(
        "\"online_client_read_only\", \"NM_Client\", MapName)",
        online_tick);
    const auto client_ui = online_context_source.find(
        "PrimaryController, \"NM_Client\", MapName)", client_context);
    const auto client_listener = online_context_source.find(
        "EnsureOnlineGraphicsListener(CurrentWorld, PrimaryController)",
        client_ui);
    const auto listen_context = online_context_source.find(
        "\"online_host_read_only\", \"NM_ListenServer\", MapName)",
        client_listener);
    const auto listen_ui = online_context_source.find(
        "PrimaryController, \"NM_ListenServer\", MapName)", listen_context);
    CHECK(online_tick != std::string::npos);
    CHECK(client_context < client_ui && client_ui < client_listener);
    CHECK(client_listener < listen_context && listen_context < listen_ui);
    CHECK(interaction_source.find(
        "if (bGameSessionEnding)", interaction_tick) !=
          std::string::npos);
    const auto prepare_for_world = interaction_source.find(
        "function PrepareForGameplayWorld()");
    const auto session_ended = interaction_source.find(
        "function NotifyGameSessionEnded()");
    const auto player_added = interaction_source.find(
        "function NotifyPlayerAdded(");
    CHECK(prepare_for_world != std::string::npos);
    CHECK(session_ended != std::string::npos);
    CHECK(player_added != std::string::npos);
    CHECK(interaction_source.find(
        "ResetTelemetryMaintenanceCadence();", prepare_for_world) <
          session_ended);
    const auto achievement_prewarm = interaction_source.find(
        "function TryPrewarmAchievements(");
    const auto achievement_complete = interaction_source.find(
        "function OnAchievementPrewarmComplete(");
    CHECK(achievement_prewarm != std::string::npos);
    CHECK(achievement_complete != std::string::npos);
    CHECK(interaction_source.find(
        "const AchievementPrewarmRequestTimeoutSeconds=10.0;") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "var float AchievementPrewarmRequestStartedRealTime;") !=
          std::string::npos);
    const auto prewarm_read = interaction_source.find(
        "ReadAchievements(", achievement_prewarm);
    CHECK(prewarm_read != std::string::npos);
    const auto pending_prewarm_guard = interaction_source.find(
        "if (bAchievementPrewarmRequested)", achievement_prewarm);
    CHECK(pending_prewarm_guard != std::string::npos);
    const auto timed_out_prewarm = interaction_source.find(
        "AchievementPrewarmRequestTimeoutSeconds", pending_prewarm_guard);
    CHECK(timed_out_prewarm != std::string::npos);
    const auto timeout_delegate_clear = interaction_source.find(
        "ClearAchievementPrewarmDelegate();", timed_out_prewarm);
    const auto timeout_pending_clear = interaction_source.find(
        "bAchievementPrewarmRequested = false", timed_out_prewarm);
    const auto timeout_retry = interaction_source.find(
        "AchievementPrewarmNextAttemptRealTime =", timed_out_prewarm);
    const auto timeout_log = interaction_source.find(
        "state=timeout", timed_out_prewarm);
    CHECK(timeout_delegate_clear < timeout_pending_clear);
    CHECK(timeout_pending_clear < timeout_retry);
    CHECK(timeout_retry < timeout_log);
    CHECK(timeout_log < prewarm_read);
    CHECK(interaction_source.find(
        "PlayerControllerId, 0, true, true)", prewarm_read) !=
        std::string::npos);
    const auto prewarm_started = interaction_source.rfind(
        "AchievementPrewarmRequestStartedRealTime =", prewarm_read);
    CHECK(prewarm_started != std::string::npos);
    CHECK(pending_prewarm_guard < prewarm_started &&
          prewarm_started < prewarm_read);
    const auto failed_prewarm = interaction_source.find(
        "if (!OnlineSub.PlayerInterface.ReadAchievements(",
        achievement_prewarm);
    CHECK(failed_prewarm != std::string::npos);
    CHECK(interaction_source.find(
        "AchievementPrewarmRequestStartedRealTime = 0.0;",
        failed_prewarm) != std::string::npos);
    const auto login_guard = interaction_source.find(
        "GetLoginStatus(PlayerControllerId)", achievement_prewarm);
    CHECK(login_guard != std::string::npos);
    CHECK(interaction_source.find("LS_NotLoggedIn", login_guard) !=
          std::string::npos);
    CHECK(interaction_source.find(
        "IsGuestLogin(PlayerControllerId)", achievement_prewarm) !=
        std::string::npos);
    const auto prewarm_requested = interaction_source.find(
        "state=requested", achievement_prewarm);
    CHECK(prewarm_requested != std::string::npos);
    CHECK(prewarm_requested < prewarm_read);
    CHECK(interaction_source.find(
        "text=true images=true", prewarm_requested) != std::string::npos);
    CHECK(interaction_source.find(
        "state=complete", achievement_complete) != std::string::npos);
    CHECK(interaction_source.find(
        "AchievementPrewarmRequestStartedRealTime = 0.0;",
        achievement_complete) < achievement_prewarm);
    CHECK(interaction_source.find(
        "TryPrewarmAchievements(PrimaryController);", interaction_tick) <
        interaction_source.find("UpdateGameplayUiState(", interaction_tick));
    CHECK(interaction_source.find(
        "bAchievementPrewarmRequested = false", prepare_for_world) <
        session_ended);
    CHECK(interaction_source.find(
        "bAchievementPrewarmComplete = false", prepare_for_world) <
        session_ended);
    CHECK(interaction_source.find(
        "ClearAchievementPrewarmDelegate();", session_ended) < player_added);
    CHECK(interaction_source.find(
        "AchievementPrewarmRequestStartedRealTime = 0.0;",
        prepare_for_world) < session_ended);
    CHECK(interaction_source.find(
        "AchievementPrewarmRequestStartedRealTime = 0.0;",
        session_ended) < player_added);
    CHECK(interaction_source.find("EnableSteamStats") == std::string::npos);
    CHECK(interaction_source.find("ClearAchievements(") ==
          std::string::npos);
    CHECK(interaction_source.find("bGameSessionEnding = true",
        interaction_source.find("function NotifyGameSessionEnded()")) !=
          std::string::npos);
    const auto session_ended_body = interaction_source.substr(
        session_ended, player_added - session_ended);
    const auto teardown_process_restore = session_ended_body.find(
        "RestoreOriginal(\n        ProcessAdaptiveGraphicsState)");
    const auto teardown_world_lookup = session_ended_body.find(
        "if (CurrentWorld != None)");
    const auto teardown_restore = session_ended_body.find(
        "CurrentProbe.RestoreSessionWorldRuntime()");
    const auto teardown_quiesce = interaction_source.find(
        "CurrentProbe.QuiesceForWorldTeardown()", session_ended);
    CHECK(teardown_process_restore != std::string::npos);
    CHECK(teardown_world_lookup != std::string::npos);
    CHECK(teardown_process_restore < teardown_world_lookup);
    CHECK(teardown_restore != std::string::npos);
    CHECK(teardown_quiesce != std::string::npos);
    CHECK(session_ended + teardown_restore < teardown_quiesce);
    CHECK(session_ended_body.find(
        "bProcessGraphicsRestorePending = true") != std::string::npos);
    CHECK(session_ended_body.find(
        "bProcessGraphicsRestorePending = false") != std::string::npos);
    CHECK(session_ended_body.find(
        "state=probe_missing") != std::string::npos);
    CHECK(session_ended_body.find(
        "boundary=session_end") != std::string::npos);
    CHECK(session_ended_body.find(
        "domain=process_graphics") != std::string::npos);
    CHECK(count_occurrences(
        session_ended_body, "domain=world_runtime") == 1);
    CHECK(session_ended_body.find(
        "ProcessAdaptiveGraphicsState = None") == std::string::npos);
    CHECK(interaction_source.find("CurrentProbe.QuiesceForWorldTeardown()",
        interaction_source.find("function NotifyGameSessionEnded()")) !=
          std::string::npos);
    CHECK(interaction_source.find("function NotifyPlayerAdded(") !=
          std::string::npos);
    CHECK(interaction_source.find("bGameSessionEnding = false",
        prepare_for_world) < session_ended);
    CHECK(interaction_source.find("state=rearmed", prepare_for_world) <
          session_ended);
    CHECK(interaction_source.find("if (bGameSessionEnding)", session_ended) <
          player_added);
    CHECK(interaction_source.find("bGameSessionEnding = false",
        player_added) == std::string::npos);
    CHECK(interaction_source.find(
        "var bool bProcessGraphicsRestorePending;") != std::string::npos);
    const auto process_restore_retry_start = interaction_source.find(
        "function bool RestorePendingProcessGraphicsWithBackoff(");
    const auto process_restore_retry_end = interaction_source.find(
        "function UpdateGameplayUiState(", process_restore_retry_start);
    CHECK(process_restore_retry_start != std::string::npos);
    CHECK(process_restore_retry_end != std::string::npos);
    const auto process_restore_retry_body = interaction_source.substr(
        process_restore_retry_start,
        process_restore_retry_end - process_restore_retry_start);
    CHECK(process_restore_retry_body.find(
        "ProcessGraphicsRestoreNextAttemptRealTime") <
          process_restore_retry_body.find(
              "RestoreOriginal(ProcessAdaptiveGraphicsState)"));
    CHECK(process_restore_retry_body.find(
        "FMin(8.0, ProcessGraphicsRestoreRetryDelay * 2.0)") !=
          std::string::npos);
    CHECK(process_restore_retry_body.find(
        "bProcessGraphicsRestorePending = false") != std::string::npos);
    const auto process_restore_retry_call = interaction_source.find(
        "RestorePendingProcessGraphicsWithBackoff(CurrentWorld)",
        interaction_tick);
    const auto fixed_effects_retry_call = interaction_source.find(
        "EnsureFixedEffectsBaselineWithBackoff(CurrentProbe, CurrentWorld)",
        interaction_tick);
    CHECK(process_restore_retry_call != std::string::npos);
    CHECK(fixed_effects_retry_call != std::string::npos);
    CHECK(process_restore_retry_call < fixed_effects_retry_call);
    CHECK(mutator_source.find("InsertInteraction(CurrentInteraction)") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "var bool bTelemetryBootstrapInserted;") != std::string::npos);
    CHECK(interaction_source.find(
        "function bool IsTelemetryBootstrapInserted()") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "function MarkTelemetryBootstrapInserted()") !=
          std::string::npos);
    CHECK(mutator_source.find(
        "const TelemetryBootstrapMaxAttempts=5;") != std::string::npos);
    CHECK(mutator_source.find(
        "const TelemetryBootstrapInitialRetrySeconds=0.25;") !=
          std::string::npos);
    CHECK(mutator_source.find(
        "const TelemetryBootstrapMaximumRetrySeconds=2.0;") !=
          std::string::npos);
    const auto bootstrap_attempt = mutator_source.find(
        "function TryBootstrapTelemetry()");
    const auto bootstrap_guard = mutator_source.find(
        "function bool CanBootstrapTelemetry()");
    const auto bootstrap_retry_schedule = mutator_source.find(
        "function ScheduleTelemetryBootstrapRetry(");
    const auto bootstrap_retry_callback = mutator_source.find(
        "function RetryTelemetryBootstrap()");
    const auto bootstrap_destroyed = mutator_source.find(
        "event Destroyed()");
    CHECK(bootstrap_attempt != std::string::npos);
    CHECK(bootstrap_guard != std::string::npos);
    CHECK(bootstrap_retry_schedule != std::string::npos);
    CHECK(bootstrap_retry_callback != std::string::npos);
    CHECK(bootstrap_destroyed != std::string::npos);
    CHECK(mutator_source.find("!bDeleteMe", bootstrap_guard) <
          bootstrap_retry_schedule);
    CHECK(mutator_source.find(
        "WorldInfo.NetMode == NM_Standalone", bootstrap_guard) <
          bootstrap_retry_schedule);
    CHECK(mutator_source.find(
        "TelemetryBootstrapAttempts >= TelemetryBootstrapMaxAttempts",
        bootstrap_retry_schedule) < bootstrap_attempt);
    CHECK(mutator_source.find(
        "SetTimer(TelemetryBootstrapRetryDelay, false,",
        bootstrap_retry_schedule) < bootstrap_attempt);
    CHECK(mutator_source.find(
        "nameof(RetryTelemetryBootstrap), self)",
        bootstrap_retry_schedule) < bootstrap_attempt);
    const auto viewport_unavailable = mutator_source.find(
        "state=viewport_unavailable", bootstrap_attempt);
    const auto viewport_retry = mutator_source.find(
        "ScheduleTelemetryBootstrapRetry(\"viewport_unavailable\")",
        viewport_unavailable);
    CHECK(viewport_unavailable < viewport_retry);
    const auto interaction_path = mutator_source.find(
        "InteractionPath = PathName(CurrentViewport)$\n"
        "        \".KF2OptimizerTelemetryInteraction\"");
    const auto interaction_lookup = mutator_source.find(
        "FindObject(InteractionPath,\n"
        "            class'KF2OptimizerTelemetryInteraction')");
    const auto existing_interaction = mutator_source.find(
        "state=ready interaction=existing");
    const auto create_interaction = mutator_source.find(
        "new(CurrentViewport,\n"
        "            \"KF2OptimizerTelemetryInteraction\")");
    const auto insert_interaction = mutator_source.find(
        "InsertInteraction(CurrentInteraction)");
    CHECK(interaction_path != std::string::npos);
    CHECK(interaction_lookup != std::string::npos);
    CHECK(existing_interaction != std::string::npos);
    CHECK(create_interaction != std::string::npos);
    CHECK(insert_interaction != std::string::npos);
    CHECK(interaction_path < interaction_lookup);
    CHECK(interaction_lookup < existing_interaction);
    CHECK(mutator_source.find(
        "CurrentInteraction.IsTelemetryBootstrapInserted()",
        interaction_lookup) < existing_interaction);
    CHECK(mutator_source.find(
        "CurrentInteraction.PrepareForGameplayWorld();",
        interaction_lookup) < existing_interaction);
    CHECK(existing_interaction < create_interaction);
    CHECK(create_interaction < insert_interaction);
    const auto insertion_failure = mutator_source.find(
        "if (CurrentViewport.InsertInteraction(CurrentInteraction) == -1)",
        create_interaction);
    const auto insertion_failure_log = mutator_source.find(
        "state=insertion_failed", insertion_failure);
    const auto insertion_retry = mutator_source.find(
        "ScheduleTelemetryBootstrapRetry(\"insertion_failed\")",
        insertion_failure_log);
    const auto mark_inserted = mutator_source.find(
        "CurrentInteraction.MarkTelemetryBootstrapInserted()",
        insertion_retry);
    const auto inserted_prepare = mutator_source.find(
        "CurrentInteraction.PrepareForGameplayWorld();", mark_inserted);
    const auto inserted_ready = mutator_source.find(
        "state=ready interaction=inserted", inserted_prepare);
    CHECK(insertion_failure != std::string::npos);
    CHECK(insertion_failure < insertion_failure_log);
    CHECK(insertion_failure_log < insertion_retry);
    CHECK(insertion_retry < mark_inserted);
    CHECK(mark_inserted < inserted_prepare);
    CHECK(inserted_prepare < inserted_ready);
    CHECK(mutator_source.find("return;", insertion_retry) < mark_inserted);
    CHECK(mutator_source.find(
        "if (!CanBootstrapTelemetry())", bootstrap_retry_callback) <
          mutator_source.find(
              "TryBootstrapTelemetry();", bootstrap_retry_callback));
    CHECK(mutator_source.find(
        "TryBootstrapTelemetry();", bootstrap_retry_callback) <
          bootstrap_destroyed);
    CHECK(mutator_source.find(
        "ClearTimer(nameof(RetryTelemetryBootstrap), self);",
        bootstrap_destroyed) <
          mutator_source.find("Super.Destroyed();", bootstrap_destroyed));
    CHECK(mutator_source.find(
        "FindObject(\"KF2OptimizerTelemetryInteraction\"") ==
          std::string::npos);
    CHECK(count_occurrences(mutator_source,
        "new(CurrentViewport,\n"
        "            \"KF2OptimizerTelemetryInteraction\")") == 1);
    CHECK(count_occurrences(mutator_source,
        "InsertInteraction(CurrentInteraction)") == 1);
    CHECK(count_occurrences(mutator_source,
        "CurrentInteraction.PrepareForGameplayWorld();") == 2);

    // Each map creates a new mutator, but all three mutators share the same
    // process-lifetime viewport outer. The fully qualified object path must
    // therefore resolve to one interaction instead of one per gameplay world.
    std::set<std::string> viewport_interactions;
    std::size_t created_interactions = 0;
    std::size_t existing_interactions = 0;
    std::size_t session_ended_callbacks = 0;
    const std::string persistent_interaction_path =
        "KFGameEngine.KFGameViewportClient."
        "KF2OptimizerTelemetryInteraction";
    for (int world = 0; world < 3; ++world) {
        const auto [unused, inserted] =
            viewport_interactions.insert(persistent_interaction_path);
        static_cast<void>(unused);
        if (inserted) {
            ++created_interactions;
        } else {
            ++existing_interactions;
        }
        ++session_ended_callbacks;
    }
    CHECK(viewport_interactions.size() == 1);
    CHECK(created_interactions == 1);
    CHECK(existing_interactions == 2);
    CHECK(session_ended_callbacks == 3);
    CHECK(count_occurrences(interaction_source,
        "state=session_ended") == 1);
    CHECK(interaction_source.find("var transient WorldInfo") ==
          std::string::npos);
    CHECK(interaction_source.find("var transient PlayerController") ==
          std::string::npos);
    CHECK(interaction_source.find(
        "var transient KF2OptimizerTelemetryProbe") == std::string::npos);
    CHECK(interaction_source.find(
        "var transient KF2OptimizerAdaptiveControlListener") ==
          std::string::npos);
    CHECK(interaction_source.find("var transient Canvas") ==
          std::string::npos);
    CHECK(mutator_source.find("WorldInfo.NetMode != NM_Standalone") !=
          std::string::npos);
    const auto standalone_guard = mutator_source.find(
        "WorldInfo.NetMode != NM_Standalone");
    const auto map_settle_request = mutator_source.find(
        "WorldInfo.bRequestedBlockOnAsyncLoading = true;");
    const auto map_settle_call = mutator_source.find(
        "RequestInitialMapSettle();");
    const auto map_settle_receipt = mutator_source.find(
        "KF2OPT_MAP_SETTLE schema=1 state=requested map=");
    const auto init_mutator = mutator_source.find(
        "function InitMutator(");
    const auto bootstrap_call = mutator_source.find(
        "TryBootstrapTelemetry();", init_mutator);
    CHECK(map_settle_request != std::string::npos);
    CHECK(map_settle_call != std::string::npos);
    CHECK(map_settle_receipt != std::string::npos);
    CHECK(standalone_guard < map_settle_call);
    CHECK(init_mutator < map_settle_call);
    CHECK(map_settle_call < bootstrap_call);
    CHECK(bootstrap_call < bootstrap_destroyed);
    CHECK(count_occurrences(mutator_source,
        "WorldInfo.bRequestedBlockOnAsyncLoading = true;") == 1);
    CHECK(count_occurrences(mutator_source,
        "RequestInitialMapSettle();") == 1);
    CHECK(listener_source.find("BindPort(0, false)") != std::string::npos);
    CHECK(listener_source.find("state=ready port=") != std::string::npos);
    CHECK(connection_source.find("KF2OPT_ACK ") != std::string::npos);
    CHECK(connection_source.find("Probe.ApplyAdaptiveResourceControl") !=
          std::string::npos);
    CHECK(connection_source.find("ApplyAdaptiveTargetFPS") ==
          std::string::npos);
    CHECK(connection_source.find(
        "Left(Peer, 10) != \"127.0.0.1:\"") != std::string::npos);
    CHECK(connection_source.find("reason=non_loopback") !=
          std::string::npos);
    CHECK(connection_source.find("Len(Line) > 128") != std::string::npos);
    CHECK(graphics_source.find("Requested.Flex") == std::string::npos);
    CHECK(graphics_source.find(
        "Current.FilmGrain.FilmGrainScale") != std::string::npos);
    CHECK(graphics_source.find(
        "\" variable_fps=\" $ int(Current.VariableFPS.VariableFramerate) $\n"
        "        \" film_grain=\" $ FilmGrainPercent $\n"
        "        \" environment=\"") != std::string::npos);
    CHECK(graphics_source.find(
        "static function ApplyOverdraw") != std::string::npos);
    CHECK(graphics_source.find(
        "static function ApplyEffects") != std::string::npos);
    CHECK(graphics_source.find("const FixedSessionEffectsQuality=50") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "static function bool ApplyFixedSessionEffects") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.OverdrawQuality = Max(Snapshot.OverdrawQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.EffectsQuality = Max(Snapshot.EffectsQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.FixedOverdrawQuality = Quality") != std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.FixedEffectsQuality = Quality") != std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.FixedOverdrawQuality = PreviousFixedOverdrawQuality") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.FixedEffectsQuality = PreviousFixedEffectsQuality") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "return ApplyResource(Snapshot, \"fixed\", FixedSessionEffectsQuality)") !=
          std::string::npos);
    CHECK(telemetry_source.find("ApplyFixedSessionEffects(") !=
          std::string::npos);
    CHECK(telemetry_source.find("EnsureFixedSessionEffects()") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "EnsureFixedEffectsBaselineWithBackoff(CurrentProbe, CurrentWorld)") !=
          std::string::npos);
    const auto offline_baseline_retry_start = interaction_source.find(
        "function bool EnsureFixedEffectsBaselineWithBackoff(");
    const auto offline_baseline_retry_end = interaction_source.find(
        "function UpdateGameplayUiState(", offline_baseline_retry_start);
    CHECK(offline_baseline_retry_start != std::string::npos);
    CHECK(offline_baseline_retry_end != std::string::npos);
    const auto offline_baseline_retry_body = interaction_source.substr(
        offline_baseline_retry_start,
        offline_baseline_retry_end - offline_baseline_retry_start);
    CHECK(offline_baseline_retry_body.find(
        "CurrentWorld.RealTimeSeconds <\n"
        "        FixedEffectsBaselineNextAttemptRealTime") <
          offline_baseline_retry_body.find(
              "CurrentProbe.EnsureFixedSessionEffects()"));
    CHECK(offline_baseline_retry_body.find(
        "FMin(8.0, FixedEffectsBaselineRetryDelay * 2.0)") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "ResetFixedEffectsBaselineRetry();") != std::string::npos);
    CHECK(interaction_source.find(
        "KF2OPT_FIXED_EFFECT_RETRY mode=offline state=") !=
          std::string::npos);
    CHECK(online_context_source.find("ApplyFixedSessionEffects(") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "EnsureOnlineFixedEffectsBaseline(CurrentWorld)") !=
          std::string::npos);
    const auto online_baseline_retry_start = online_context_source.find(
        "function bool EnsureOnlineFixedEffectsBaseline(");
    const auto online_baseline_retry_end = online_context_source.find(
        "function ClearOnlineCorpseMaximumSnapshot(",
        online_baseline_retry_start);
    CHECK(online_baseline_retry_start != std::string::npos);
    CHECK(online_baseline_retry_end != std::string::npos);
    const auto online_baseline_retry_body = online_context_source.substr(
        online_baseline_retry_start,
        online_baseline_retry_end - online_baseline_retry_start);
    CHECK(online_baseline_retry_body.find(
        "OnlineFixedEffectsBaselineNextAttemptRealTime") <
          online_baseline_retry_body.find("ApplyFixedSessionEffects"));
    CHECK(online_baseline_retry_body.find(
        "FMin(8.0, OnlineFixedEffectsBaselineRetryDelay * 2.0)") !=
          std::string::npos);
    const auto online_restore_retry_start = online_context_source.find(
        "function RestoreOnlineGraphicsAtMainMenu(");
    const auto online_restore_retry_end = online_context_source.find(
        "function NotifyGameSessionEnded()", online_restore_retry_start);
    CHECK(online_restore_retry_start != std::string::npos);
    CHECK(online_restore_retry_end != std::string::npos);
    const auto online_restore_retry_body = online_context_source.substr(
        online_restore_retry_start,
        online_restore_retry_end - online_restore_retry_start);
    CHECK(online_restore_retry_body.find(
        "OnlineMainMenuRestoreNextAttemptRealTime") <
          online_restore_retry_body.find(
              "RestoreOnlineSessionState(CurrentWorld, \"main_menu\")"));
    CHECK(online_restore_retry_body.find(
        "FMin(8.0, OnlineMainMenuRestoreRetryDelay * 2.0)") !=
          std::string::npos);
    CHECK(online_restore_retry_body.find(
        "bOnlineMainMenuRestoreComplete = true") != std::string::npos);
    CHECK(online_context_source.find(
        "if (OnlineGraphicsRetryMapName != MapName)") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "ResetOnlineGraphicsRetryState(MapName)") != std::string::npos);
    CHECK(online_context_source.find(
        "KF2OPT_GRAPHICS_RETRY mode=online operation=") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "KF2OPT_FIXED_EFFECT_BASELINE state=applied mode=online") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "ApplyOverdraw(Requested, GetEffectiveOverdrawQuality(Snapshot))") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "ApplyEffects(Requested, GetEffectiveEffectsQuality(Snapshot))") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "return Min(Snapshot.OverdrawQuality, Snapshot.FixedOverdrawQuality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "return Min(Snapshot.EffectsQuality, Snapshot.FixedEffectsQuality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "else if (Resource ~= \"overdraw\") Snapshot.OverdrawQuality = Quality") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "else if (Resource ~= \"effects\") Snapshot.EffectsQuality = Quality") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.RamQuality = Quality;\n"
        "        Snapshot.OverdrawQuality = Quality;") ==
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.RamQuality = Quality;\n"
        "        Snapshot.EffectsQuality = Quality;") ==
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.FX.DropParticleDistortion = true") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.FX.MaxPersistentSplatsPerFrame = Min") !=
          std::string::npos);
    CHECK(graphics_source.find("Requested.CharacterDetail.MaxDeadBodies") ==
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.CharacterDetail.ShouldCorpseCollideWithDeadAfterSleep = false") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.CharacterDetail.ShouldCorpseCollideWithDead = false") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.CharacterDetail.ShouldCorpseCollideWithLiving = false") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.bOriginalCorpseCollideWithDead") != std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.bOriginalCorpseCollideWithLiving") != std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.bOriginalCorpseCollideWithDeadAfterSleep") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.CharacterDetail.ShouldCorpseCollideWithDead =\n"
        "        Snapshot.bOriginalCorpseCollideWithDead") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Observed.CharacterDetail.ShouldCorpseCollideWithLiving") !=
          std::string::npos);
    CHECK(graphics_source.find("Requested.CharacterDetail.bAllowPhysics") ==
          std::string::npos);
    CHECK(graphics_source.find("KinematicUpdateDistFactorScale = FMax") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.TextureResolution.ShadowmapBias = Max") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.MotionBlur.MotionBlurQuality = Min") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.EnvironmentDetail.AllowLightFunctions = false") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.Shadows.ShadowFadeResolution = Max") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.OriginalShadowmapTextureBias") != std::string::npos);
    CHECK(graphics_source.find(
        "Observed.TextureResolution.ShadowmapBias") != std::string::npos);
    CHECK(graphics_source.find("RestoreOwnedSettings") != std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.GpuQuality = Max(Snapshot.GpuQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.CpuQuality = Max(Snapshot.CpuQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.VramQuality = Max(Snapshot.VramQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.RamQuality = Max(Snapshot.RamQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.OverdrawQuality = Max(Snapshot.OverdrawQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.EffectsQuality = Max(Snapshot.EffectsQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find("Snapshot.EffectsQuality = 100") !=
          std::string::npos);
    CHECK(graphics_source.find("Snapshot.FixedEffectsQuality = 100") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GetEffectiveEffectsQuality(AdaptiveGraphicsState)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.FX.MaxGoreEffects, Max(2, Quality / 10)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool ApplyAdaptiveEffectRuntimeReadback(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "string Resource, int Quality, optional bool "
        "bRestoreWorldParticleOriginals") != std::string::npos);
    CHECK(telemetry_source.find(
        "if (bRestoreWorldParticleOriginals || AdaptiveGraphicsState == None)\n"
        "    {\n        WorldParticleQuality = 100;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GoreManager.MaxBloodEffects =\n"
        "        class'KFGoreManager'.default.MaxBloodEffects") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GoreManager.BloodFXEmitterPool.MaxActiveEffects =\n"
        "            GoreManager.MaxBloodEffects") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "ImpactEffectManager.ImpactEffectDecalManager.MaxActiveDecals =\n"
        "                ImpactEffectManager.MaxImpactEffectDecals") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.ImpactFXEmitterPool.MaxActiveEffects =\n"
        "            DesiredImpactEffects") != std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.ImpactFXEmitterPool.MaxActiveEffects ==\n"
        "            DesiredImpactEffects") != std::string::npos);
    CHECK(telemetry_source.find(
        " impact_pool=\"$") != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_EFFECT_RUNTIME state=applied") != std::string::npos);
    CHECK(telemetry_source.find(
        "!ApplyAdaptiveEffectRuntimeReadback(Resource, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find("Quality >= 90") != std::string::npos);
    CHECK(graphics_source.find("Quality <= 80") != std::string::npos);
    CHECK(graphics_source.find(
        "FMax(0.25, float(Quality) / 100.0)") != std::string::npos);
    CHECK(telemetry_source.find("AdaptivePresetIndex") == std::string::npos);
    CHECK(telemetry_source.find(" preset=") == std::string::npos);
    CHECK(telemetry_source.find(" stage=") != std::string::npos);
    CHECK(telemetry_source.find(
        "Resource ~= \"recover\" || Quality >= 95") ==
          std::string::npos);
    CHECK(graphics_source.find(
        "KF2OPT_ADAPTIVE_ROLLBACK state=applied") != std::string::npos);
    CHECK(graphics_source.find(
        "KF2OPT_ADAPTIVE_ROLLBACK state=failed") != std::string::npos);
    const auto rollback_start = telemetry_source.find(
        "function bool RollbackAdaptiveResourceControl(");
    const auto restore_debt_start = telemetry_source.find(
        "function bool ResolveAdaptiveQualityRestoreDebt()", rollback_start);
    CHECK(rollback_start != std::string::npos);
    CHECK(restore_debt_start != std::string::npos);
    const auto rollback_body = telemetry_source.substr(
        rollback_start, restore_debt_start - rollback_start);
    CHECK(rollback_body.find("SetQualityRestoreDebt(") != std::string::npos);
    CHECK(rollback_body.find("ApplyResource(") == std::string::npos);
    CHECK(rollback_body.find("ResolveAdaptiveQualityRestoreDebt()") !=
          std::string::npos);
    CHECK(rollback_body.find(
        "bAdaptiveQualityRestoreCompletedForLastCommand = true") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "static function bool ApplyQualityComposition(") !=
          std::string::npos);
    // A confirmed KF2 graphics-menu change must replace only the changed
    // user-owned baseline groups.  The next Adaptive composition and final
    // recovery then derive from that rebased baseline instead of the stale
    // first-action snapshot.
    CHECK(graphics_source.find(
        "static function bool OwnedSettingsDiffer(") !=
          std::string::npos);
    const auto menu_rebase_start = graphics_source.find(
        "static function bool RebaseOriginalFromMenuChange(");
    const auto menu_rebase_end = graphics_source.find(
        "static function CaptureOriginal(", menu_rebase_start);
    CHECK(menu_rebase_start != std::string::npos);
    CHECK(menu_rebase_end != std::string::npos);
    const auto menu_rebase_body = graphics_source.substr(
        menu_rebase_start, menu_rebase_end - menu_rebase_start);
    CHECK(menu_rebase_body.find("!Snapshot.bOriginalCaptured") !=
          std::string::npos);
    CHECK(menu_rebase_body.find("Snapshot.OriginalMaxShadowResolution") !=
          std::string::npos);
    CHECK(menu_rebase_body.find("Snapshot.OriginalParticleLODBias") !=
          std::string::npos);
    CHECK(menu_rebase_body.find("Snapshot.GpuQuality =") ==
          std::string::npos);
    CHECK(menu_rebase_body.find("Snapshot.bQualityRestorePending =") ==
          std::string::npos);
    const auto owned_copy_start = graphics_source.find(
        "static function CopyOwnedSettings(");
    const auto owned_copy_end = graphics_source.find(
        "static function bool OwnedSettingsDiffer(", owned_copy_start);
    CHECK(owned_copy_start != std::string::npos);
    CHECK(owned_copy_end != std::string::npos);
    const auto owned_copy_body = graphics_source.substr(
        owned_copy_start, owned_copy_end - owned_copy_start);
    std::set<std::string> owned_fields;
    for (auto cursor = owned_copy_body.find("Snapshot.");
         cursor != std::string::npos;
         cursor = owned_copy_body.find("Snapshot.", cursor + 9)) {
        const auto name_start = cursor + 9;
        const auto name_end = owned_copy_body.find_first_of(
            " \t\r\n=", name_start);
        if (name_end == std::string::npos) break;
        const auto equals = owned_copy_body.find_first_not_of(
            " \t\r\n", name_end);
        if (equals != std::string::npos && owned_copy_body[equals] == '=') {
            owned_fields.emplace(owned_copy_body.substr(
                name_start, name_end - name_start));
        }
    }
    CHECK(owned_fields.size() == 56);
    for (const auto& field : owned_fields) {
        CHECK(menu_rebase_body.find("Snapshot." + field) !=
              std::string::npos);
    }
    CHECK(graphics_interaction_source.find(
        "MenuReadback(CurrentMenuGraphicsState)") != std::string::npos);
    CHECK(graphics_interaction_source.find("OwnedSettingsDiffer(") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "PreviousMenuGraphicsState, CurrentMenuGraphicsState") !=
          std::string::npos);
    CHECK(graphics_interaction_source.find(
        "RebaseOriginalFromMenuChange(") != std::string::npos);
    CHECK(interaction_source.find(
        "function KF2OptimizerAdaptiveGraphicsState "
        "PeekProcessAdaptiveGraphicsState()") != std::string::npos);
    CHECK(online_context_source.find(
        "function KF2OptimizerAdaptiveGraphicsState "
        "PeekOnlineGraphicsState()") != std::string::npos);
    CHECK(graphics_source.find(
        "static function bool ApplyQualityRestoreDebt(") !=
          std::string::npos);
    CHECK(graphics_source.find("Snapshot.RestoreGpuQuality") !=
          std::string::npos);
    CHECK(graphics_source.find("Snapshot.RestoreEffectsQuality") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveGraphicsState.bQualityRestorePending") !=
          std::string::npos);
    CHECK(telemetry_source.find("reason=restore_pending") !=
          std::string::npos);
    CHECK(connection_source.find(
        "KF2OPT_ACK \"$Sequence$\" unknown \"$Resource") !=
          std::string::npos);
    CHECK(connection_source.find(
        "KF2OPT_ACK \"$Sequence$\" restored \"$Resource") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "request_applied=false readback=verified") != std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.NetMode != NM_Standalone") != std::string::npos);
    CHECK(telemetry_source.find(
        "GoreManager.CorpsePool.Length <= AdaptiveCorpseRuntimeLimit") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig int AdaptiveCorpseMaximum") != std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig int AdaptiveTargetFPS") != std::string::npos);
    CHECK(telemetry_source.find("ApplyAdaptiveTargetFPS") ==
          std::string::npos);
    CHECK(interaction_source.find("t.MaxFPS") == std::string::npos);
    CHECK(interaction_source.find("MaxSmoothedFrameRate") ==
          std::string::npos);
    CHECK(interaction_source.find("KF2OPT_FRAME_RATE") ==
          std::string::npos);
    CHECK(interaction_source.find("KF2OPT_TARGET_FPS") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig int AdaptiveQualityChangeBudget") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveCorpseAttackScale()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool HasConfirmedAdaptivePerformancePressure()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "return AdaptiveLastControlSequence > 0 &&\n"
        "        AdaptiveGraphicsQuality >= 10 && AdaptiveGraphicsQuality < 100 &&\n"
        "        !(AdaptiveGraphicsResource ~= \"recover\")") != std::string::npos);
    CHECK(telemetry_source.find(
        "if (!HasConfirmedAdaptivePerformancePressure())") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "PhysicsPressureLevel = AdaptiveCorpsePressureLevel > 0") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveGraphicsQuality = 100") != std::string::npos);
    CHECK(telemetry_source.find(
        "Step = Step * AttackScale") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceActionInterval / float(AttackScale)") !=
          std::string::npos);
    CHECK(telemetry_source.find("LodActionInterval") == std::string::npos);
    CHECK(telemetry_source.find(
        "0.20 / float(PhysicsPressureLevel)") != std::string::npos);
    CHECK(telemetry_source.find(
        "ActionInterval / float(AttackScale)") != std::string::npos);
    CHECK(telemetry_source.find(
        "TargetFrameMs * 60.0 / 58.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveFrameBaselineMs * 1.3") == std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseTarget = AdaptiveCorpseMaximum") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseTarget = Clamp(AdaptiveCorpseOriginalLimit, 4, 2000)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseRuntimeLimit = AdaptiveCorpseTarget") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseRuntimeLimit = 72") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "function AdjustAdaptiveCorpseCapacity") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdjustAdaptiveCorpseCapacity(GoreManager, 0)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpsePressureLevel)") != std::string::npos);
    CHECK(telemetry_source.find(
        "SetTimer(0.45, true, nameof(StaggerCorpseCleanup), self)") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "GoreManager.RemoveAndDeleteCorpse(SelectedIndex)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpsePressureLevel <= 0") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseCurrentFramePressureLevel <= 0") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.RealTimeSeconds - AdaptiveFramePressureObservedRealTime > 0.4") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.TimeOfDeath < 1.5") != std::string::npos);
    CHECK(telemetry_source.find(
        "GameInfo.IsZedTimeActive()") != std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseControlInterval=0.25;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseControlInitialDelay=0.125;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseControlIdleInterval=1.0;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseControlCalmInterval=0.5;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseControlUrgentInterval=0.125;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseControlSliceInterval=0.05;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseControlPhaseCount=8;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const FixedMinimumVisualControlPhaseCount=5;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseScanBudget=64;") != std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseSettleTrackingTimeout=60.0;") !=
          std::string::npos);
    CHECK(settled_after_bounded_scans(2000, 64, 0.85, 0.75) == 0);
    CHECK(settled_after_bounded_scans(2000, 64, 0.85, 60.0) == 2000);
    CHECK(settled_after_bounded_scans(2000, 64, 1.20, 60.0) == 2000);
    CHECK(telemetry_source.find(
        "SetTimer(FMax(0.05, DelaySeconds), false,") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "ScheduleAdaptiveCorpseControlTimer(\n"
        "                AdaptiveCorpseControlInitialDelay);") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GetAdaptiveCorpseControlDelay(bActionTaken)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int SleepBaselineAwakeMonsterCorpses(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumSettleAge = 0.75") != std::string::npos);
    CHECK(telemetry_source.find("MaximumFullPhysicsAge") ==
          std::string::npos);
    const auto shared_settle_guard = telemetry_source.find(
        "function bool IsAdaptiveCorpseSettled(");
    CHECK(shared_settle_guard != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumStableTime = 0.75") != std::string::npos);
    CHECK(telemetry_source.find(
        "GetRootBodyInstance()") != std::string::npos);
    CHECK(telemetry_source.find(
        "GetUnrealWorldVelocity()") != std::string::npos);
    CHECK(telemetry_source.find(
        "GetUnrealWorldAngularVelocity()") != std::string::npos);
    CHECK(telemetry_source.find(
        "VSizeSq(Candidate.Mesh.Bounds.Origin - Entry.StableLocation)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "CurrentRealTime - Entry.LastObservedRealTime >\n"
        "            AdaptiveCorpseSettleTrackingTimeout") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "CurrentRealTime - Entry.LastObservedRealTime > MinimumStableTime") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "var float CorpseTimeOfDeath;") != std::string::npos);
    CHECK(telemetry_source.find(
        "Entry.CorpseTimeOfDeath != Candidate.TimeOfDeath") !=
          std::string::npos);
    CHECK(count_occurrences(
        telemetry_source, "AdaptiveBaselineSettleEntries.Length = 0;") >= 2);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_BASELINE state=deferred reason=") !=
          std::string::npos);
    CHECK(telemetry_source.find("forced=") == std::string::npos);
    CHECK(telemetry_source.find("MaximumSleepsPerPass") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_BASELINE state=sleep") != std::string::npos);
    CHECK(telemetry_source.find(
        "FindAdaptiveCorpsePhysicsActionId(\"baseline\",") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "RegisterAdaptiveCorpsePhysicsAction(Candidate, \"baseline\")") !=
          std::string::npos);
    const auto baseline_start = telemetry_source.find(
        "function int SleepBaselineAwakeMonsterCorpses(");
    const auto baseline_end = telemetry_source.find(
        "function int FindFixedMinimumCorpseLodEntry(", baseline_start);
    CHECK(baseline_end != std::string::npos);
    const auto baseline_body = telemetry_source.substr(
        baseline_start, baseline_end - baseline_start);
    const auto baseline_capacity = baseline_body.find(
        "if (!EnsureAdaptiveCorpsePhysicsActionIds() ||\n"
        "        AdaptiveCorpsePhysicsActionIdCount >= 8192)");
    const auto baseline_loop = baseline_body.find(
        "for (Offset = 0; Offset < ScanCount; ++Offset)");
    CHECK(baseline_capacity != std::string::npos);
    CHECK(baseline_body.find("if (PoolLength <= 0)") < baseline_capacity);
    CHECK(baseline_body.find(
        "ScanCount = Min(AdaptiveCorpseScanBudget, PoolLength);") <
          baseline_capacity);
    CHECK(baseline_capacity < baseline_loop);
    const auto baseline_capacity_body = baseline_body.substr(
        baseline_capacity, baseline_loop - baseline_capacity);
    CHECK(baseline_capacity_body.find(
        "AdaptiveBaselineScanCursor =\n"
        "            (AdaptiveBaselineScanCursor + ScanCount) % PoolLength;\n"
        "        return 0;") != std::string::npos);
    CHECK(baseline_loop < baseline_body.find("IsAdaptiveCorpseSettled("));
    CHECK(baseline_loop < baseline_body.find(
        "ReserveAdaptivePhysicsMutationForCurrentFrame()"));
    CHECK(baseline_loop < baseline_body.find("PutRigidBodyToSleep()"));
    CHECK(baseline_loop < baseline_body.find(
        "bSkipAllUpdateWhenPhysicsAsleep = true;"));
    CHECK(baseline_loop < baseline_body.find("bNoSkeletonUpdate = true;"));
    CHECK(telemetry_source.find(
        "0.05 + ((WeightedVisibleZeds - 1.0) / 79.0) * 0.95") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.bDropDetail") != std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.DeltaSeconds * 1000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "KFPawn_Monster(Candidate) == None") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.PutRigidBodyToSleep()") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.bNoSkeletonUpdate = true") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.SpecialMove == SM_DeathAnim") != std::string::npos);
    CHECK(telemetry_source.find(
        "VSizeSq(Candidate.Velocity) > MaximumSpeedSquared") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.SkeletalMesh.LODInfo.Length < 2") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.ForcedLodModel != 0") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.MinLodModel = TargetMinLod") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "RestoreAllAdaptiveCorpseLods()") == std::string::npos);
    const auto lod_selector_function = telemetry_source.find(
        "function KFPawn SelectVisibleMonsterCorpseForMinimumLod(");
    CHECK(lod_selector_function != std::string::npos);
    const auto lod_apply_function = telemetry_source.find(
        "function bool ApplyOneFixedMinimumCorpseLod(",
        lod_selector_function);
    CHECK(lod_apply_function != std::string::npos);
    CHECK(telemetry_source.substr(
        lod_selector_function,
        lod_apply_function - lod_selector_function).find(
            "DistanceSquared < 640000.0") == std::string::npos);
    CHECK(telemetry_source.find("RestoreNearAdaptiveCorpseLods") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_LOD state=fixed_minimum") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "SelectDistantAwakeMonsterCorpseForSleep") != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumDistanceSquared = 1440000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumDistanceSquared = 1000000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumDistanceSquared = 722500.0") != std::string::npos);
    CHECK(telemetry_source.find("MinimumDistanceSquared = 7840000.0") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDistanceSleptCorpses.Length >=") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.LastRenderTime >") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.WakeRigidBody()") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared >= 640000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "function CollectAdaptiveCorpseCounts(") == std::string::npos);
    CHECK(telemetry_source.find(
        "VisibleCorpses = AdaptiveCachedVisibleCorpses;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "VisibleAwake = AdaptiveCachedVisibleAwakeCorpses;") !=
          std::string::npos);
    // Only published metrics and controller inputs justify native awake queries.
    CHECK(telemetry_source.find("CorpseVisibleAwake") == std::string::npos);
    CHECK(telemetry_source.find(
        "AwakeTotal = AdaptiveCachedAwakeCorpses;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseCountsObservedRealTime") != std::string::npos);
    CHECK(telemetry_source.find(
        "if (!bCorpseCountsFresh)\n    {\n        return false;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "!Corpse.bDeleteMe && KFPawn_Monster(Corpse) != None") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int CountVisibleAwakeMonsterCorpses(") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int CountAwakeMonsterCorpses(") == std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_DISTANCE state=sleep") != std::string::npos);
    const auto distance_sleep_function = telemetry_source.find(
        "function bool SleepOneDistantMonsterCorpse(");
    const auto distance_settle_guard = telemetry_source.find(
        "IsAdaptiveCorpseSettled(Candidate,", distance_sleep_function);
    const auto distance_sleep_call = telemetry_source.find(
        "Candidate.Mesh.PutRigidBodyToSleep()", distance_sleep_function);
    CHECK(distance_sleep_function != std::string::npos);
    CHECK(distance_settle_guard != std::string::npos);
    CHECK(distance_sleep_call != std::string::npos);
    CHECK(distance_settle_guard < distance_sleep_call);
    CHECK(telemetry_source.find(
        "FindAdaptiveDistanceSleptCorpse(Candidate) != -1") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "struct AdaptiveDistanceSleepEntry") != std::string::npos);
    CHECK(telemetry_source.find(
        "struct AdaptiveDistanceSleepTransitionEntry") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var float NativeWakeObservedRealTime") != std::string::npos);
    CHECK(telemetry_source.find("var int NativeWakeCount") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var float NativeWakeCooldownUntilRealTime") !=
          std::string::npos);
    CHECK(telemetry_source.find("var float ExpiresRealTime") !=
          std::string::npos);
    CHECK(telemetry_source.find("var bool bReusable") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool DeferAdaptiveDistanceResleepAfterNativeWake(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DeferAdaptiveDistanceResleepAfterNativeWake(Candidate)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function float GetAdaptiveNativeWakeBackoffSeconds(") !=
          std::string::npos);
    CHECK(telemetry_source.find("return 2.0;") != std::string::npos);
    CHECK(telemetry_source.find("return 5.0;") != std::string::npos);
    CHECK(telemetry_source.find("return 10.0;") != std::string::npos);
    CHECK(telemetry_source.find("return 20.0;") != std::string::npos);
    CHECK(telemetry_source.find("return 30.0;") != std::string::npos);
    CHECK(telemetry_source.find(
        "NativeWakeCooldownUntilRealTime;") != std::string::npos);
    CHECK(telemetry_source.find("NativeWakeDistanceUnits + 250") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "CurrentDistanceUnits < 0") == std::string::npos);
    CHECK(telemetry_source.find(
        "NativeWakeCount = Min(\n            5,") != std::string::npos);
    CHECK(telemetry_source.find(
        "ExpiresRealTime <=\n            WorldInfo.RealTimeSeconds") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool RememberAdaptiveDistanceSleepTransition(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDistanceSleepTransitions.Length = 8192") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function ClearAdaptiveDistanceSleepTransition(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function PruneAdaptiveDistanceSleepTransitions()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDistanceSleepTransitionPruneCursor + 1") !=
          std::string::npos);
    CHECK(telemetry_source.find("Index < 64") != std::string::npos);
    CHECK(telemetry_source.find(
        "RemovalReason != \"tracked\"") != std::string::npos);
    CHECK(telemetry_source.find(".bReusable = true") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "!AdaptiveDistanceSleepTransitions[Slot].bReusable") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Slot = (Slot + 1) & 8191") != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_DISTANCE state=removed") != std::string::npos);
    CHECK(count_occurrences(
        telemetry_source, "KF2OPT_CORPSE_DISTANCE state=removed") == 2);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseManager.CorpsePool.Find(Candidate) >= 0") !=
          std::string::npos);
    CHECK(telemetry_source.find("Index, \"native_wake\"") !=
          std::string::npos);
    CHECK(telemetry_source.find("Index, \"physics_changed\"") !=
          std::string::npos);
    CHECK(telemetry_source.find("Index, \"deleted\"") !=
          std::string::npos);
    CHECK(telemetry_source.find("Index, \"reused\"") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_DISTANCE state=resleep") != std::string::npos);
    CHECK(telemetry_source.find(" previous_reason=") != std::string::npos);
    CHECK(telemetry_source.find(" native_wake_count=") !=
          std::string::npos);
    CHECK(telemetry_source.find(" resleep_after_ms=") !=
          std::string::npos);
    CHECK(telemetry_source.find("removal_reason=tracking_lost") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "state=tracking_full capacity=8192") != std::string::npos);
    CHECK(telemetry_source.find(
        "action=disabled_to_preserve_traceability") != std::string::npos);
    const auto distance_prune = telemetry_source.find(
        "function PruneAdaptiveDistanceSleptCorpses()");
    const auto native_wake_transition = telemetry_source.find(
        "Candidate.Mesh.RigidBodyIsAwake()", distance_prune);
    const auto native_wake_release = telemetry_source.find(
        "Index, \"native_wake\"", native_wake_transition);
    CHECK(distance_prune != std::string::npos);
    CHECK(native_wake_transition != std::string::npos);
    CHECK(native_wake_release != std::string::npos);
    CHECK(native_wake_transition < native_wake_release);
    CHECK(telemetry_source.find(
        "function string GetAdaptiveCorpseActionId(KFPawn Candidate)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "string(Candidate.Name)$\":\"$") != std::string::npos);
    CHECK(telemetry_source.find(
        "int(Candidate.TimeOfDeath * 1000.0)") != std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveCorpseDistanceUnits(KFPawn Candidate)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function string FormatAdaptiveCorpseDistanceMeters(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceDecimeters = (DistanceUnits + 5) / 10") !=
          std::string::npos);
    // All settle, LOD and manager-generation release paths emit
    // actor-correlated receipts; the isolated living offscreen animation
    // policy adds two distance receipts.
    CHECK(count_occurrences(telemetry_source, "corpse_id=") == 24);
    CHECK(count_occurrences(telemetry_source, " distance_units=") == 14);
    CHECK(count_occurrences(telemetry_source, " distance_m=") == 14);
    const auto distance_marker = telemetry_source.find(
        "FormatAdaptiveDebugMarkerAction(");
    CHECK(distance_marker != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceUnits, true)", distance_marker) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig bool bAdaptiveCorpseDebugMarkers") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig bool bAdaptiveZedDebugMarkers") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig bool bDetailedRuntimeDiagnostics") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "bSubmitNativeProfileNodes = bDetailedRuntimeDiagnostics &&") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (bDetailedRuntimeDiagnostics)\n            {\n"
        "                if (LivingClasses.Find(Zed.Class)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (bDetailedRuntimeDiagnostics)\n            {\n"
        "                if (Corpse.bHasBrokenConstraints)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (bDetailedRuntimeDiagnostics)\n    {\n"
        "        if (CollisionProbeCorpse != None &&\n"
        "            CollisionProbeCorpse.ShouldCorpseCollideWithDead())") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (bDetailedRuntimeDiagnostics)\n        {\n"
        "            ++AdaptiveBaselinePhysicsSleeps;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (bDetailedRuntimeDiagnostics)\n    {\n"
        "        ++AdaptiveDistancePhysicsSleeps;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (!bDetailedRuntimeDiagnostics || Candidate == None") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (bDetailedRuntimeDiagnostics)\n    {\n"
        "        ++FixedMinimumLivingOffscreenAnimRestores;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (bDetailedRuntimeDiagnostics)\n                {\n"
        "                    ++FixedMinimumLivingOffscreenAnimReductions;") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "function bool DetailedRuntimeDiagnosticsEnabled()") !=
          std::string::npos);
    CHECK(online_context_source.find(
        "if (DetailedRuntimeDiagnosticsEnabled())\n        {\n"
        "            `log(\"KF2OPT_ONLINE_CORPSE_ACTION state=sleep "
        "corpse_id=\"") != std::string::npos);
    CHECK(online_context_source.find(
        "`log(\"KF2OPT_ONLINE_CORPSE_ACTION state=sleep\"$\n"
        "                 \" awake=false local_only=true "
        "readback=verified\");") != std::string::npos);
    CHECK(telemetry_source.find(
        "function RegisterAdaptiveCorpseDebugMarker(") != std::string::npos);
    CHECK(telemetry_source.find(
        "function DrawAdaptiveCorpseDebugMarkers(Canvas MarkerCanvas)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function DrawAdaptiveZedDebugMarkers(Canvas MarkerCanvas)") !=
          std::string::npos);
    const auto draw_zed_markers = telemetry_source.find(
        "function DrawAdaptiveZedDebugMarkers(Canvas MarkerCanvas)");
    const auto after_draw_zed_markers = telemetry_source.find(
        "function int GetProfileElapsedMilliseconds(", draw_zed_markers);
    CHECK(draw_zed_markers != std::string::npos);
    CHECK(after_draw_zed_markers != std::string::npos);
    const auto zed_marker_draw_source = telemetry_source.substr(
        draw_zed_markers, after_draw_zed_markers - draw_zed_markers);
    CHECK(zed_marker_draw_source.find(
        "LocalPC.GetPlayerViewPoint(ViewLocation, ViewRotation)") !=
          std::string::npos);
    CHECK(zed_marker_draw_source.find(
        "vector(ViewRotation) dot (MarkerLocation - ViewLocation)") !=
          std::string::npos);
    CHECK(zed_marker_draw_source.find("ScreenPosition.Z") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool ReserveAdaptiveDebugMarkerScreenPosition(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDebugMarkerScreenEntries.Length = 0") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "MarkerCanvas.CreateFontRenderInfo(false, true)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function float GetAdaptiveDebugMarkerTextScale(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "ResolutionScale = MarkerCanvas.ClipY / 1080.0") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceUnits >= 4000") != std::string::npos);
    CHECK(count_occurrences(
        telemetry_source, "GetAdaptiveDebugMarkerTextScale(") == 3);
    CHECK(telemetry_source.find(
        "function string FormatAdaptiveDebugMarkerId(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function string FormatAdaptiveDebugMarkerAction(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDebugMarkerScreenEntries.Length >= 10") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDebugMarkerScreenEntries.Length >= 5") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "MarkerCanvas.DrawRect(") != std::string::npos);
    CHECK(telemetry_source.find(
        "OutlineOffset = 1.25 * TextScale") != std::string::npos);
    CHECK(telemetry_source.find(
        "ScreenPosition.X - OutlineOffset, ScreenPosition.Y") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "ScreenPosition.X, ScreenPosition.Y + OutlineOffset") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function InsertAdaptiveZedDebugMarkerByDistance(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var vector Velocity;") != std::string::npos);
    CHECK(telemetry_source.find(
        "MarkerLocation += AdaptiveZedDebugMarkers[Index].Velocity *") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "PredictionSeconds > 0.10") != std::string::npos);
    CHECK(telemetry_source.find(
        "!LocalPC.FastTrace(MarkerLocation, ViewLocation)") !=
          std::string::npos);
    CHECK(zed_marker_draw_source.find("KF2OPT ZED | ") ==
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveZedDebugMarkers.Length >= 24") !=
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveZedDebugRefreshRealTime + 0.10") !=
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveCorpseDebugMarkers.Length >= 24") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GetAdaptiveCorpseActionId(Candidate)") != std::string::npos);
    CHECK(count_occurrences(
        telemetry_source, "RegisterAdaptiveCorpseDebugMarker(Candidate,") == 6);
    CHECK(interaction_source.find("event PostRender(Canvas MarkerCanvas)") ==
          std::string::npos);
    CHECK(interaction_source.find(
        "var transient KF2OptimizerTelemetryProbe ActiveProbe") ==
          std::string::npos);
    CHECK(interaction_source.find("var transient WorldInfo ActiveWorld") ==
          std::string::npos);
    CHECK(interaction_source.find(
        "foreach CurrentWorld.DynamicActors(") != std::string::npos);
    CHECK(telemetry_source.find(
        "function EnsureAdaptiveDebugMarkerPostRender()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDebugMarkerHUD.AddPostRenderedActor(self)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDebugMarkerHUD.bShowOverlays = true") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var bool bAdaptiveDebugMarkerOriginalShowOverlays") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var bool bAdaptiveDebugMarkerOwnsShowOverlays") !=
          std::string::npos);
    const auto remove_debug_post_render = telemetry_source.find(
        "function RemoveAdaptiveDebugMarkerPostRender()");
    const auto ensure_debug_post_render = telemetry_source.find(
        "function EnsureAdaptiveDebugMarkerPostRender()");
    const auto debug_post_render = telemetry_source.find(
        "simulated event PostRenderFor(", ensure_debug_post_render);
    CHECK(remove_debug_post_render != std::string::npos);
    CHECK(ensure_debug_post_render != std::string::npos);
    CHECK(debug_post_render != std::string::npos);
    const auto remove_debug_post_render_body = telemetry_source.substr(
        remove_debug_post_render,
        ensure_debug_post_render - remove_debug_post_render);
    const auto ensure_debug_post_render_body = telemetry_source.substr(
        ensure_debug_post_render,
        debug_post_render - ensure_debug_post_render);
    CHECK(remove_debug_post_render_body.find(
        "bAdaptiveDebugMarkerOwnsShowOverlays &&") != std::string::npos);
    CHECK(remove_debug_post_render_body.find(
        "AdaptiveDebugMarkerHUD.bShowOverlays =\n"
        "                bAdaptiveDebugMarkerOriginalShowOverlays;") !=
          std::string::npos);
    CHECK(remove_debug_post_render_body.find(
        "bAdaptiveDebugMarkerOwnsShowOverlays = false;") !=
          std::string::npos);
    CHECK(count_occurrences(
        ensure_debug_post_render_body,
        "RemoveAdaptiveDebugMarkerPostRender();") == 3);
    CHECK(ensure_debug_post_render_body.find(
        "if (AdaptiveDebugMarkerHUD == LocalPC.MyHUD)") !=
          std::string::npos);
    CHECK(count_occurrences(
        ensure_debug_post_render_body,
        "AdaptiveDebugMarkerHUD.AddPostRenderedActor(self)") == 1);
    CHECK(ensure_debug_post_render_body.find(
        "bAdaptiveDebugMarkerOriginalShowOverlays =\n"
        "        AdaptiveDebugMarkerHUD.bShowOverlays;") !=
          std::string::npos);
    CHECK(telemetry_source.find("bPostRenderIfNotVisible = true") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "simulated event PostRenderFor(PlayerController PC,") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DrawAdaptiveCorpseDebugMarkers(MarkerCanvas)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DrawAdaptiveZedDebugMarkers(MarkerCanvas)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDebugMarkerHUD.RemovePostRenderedActor(self)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_DEBUG_MARKERS state=rendered") != std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.DrawAdaptiveCorpseDebugMarkers(MarkerCanvas)") ==
          std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.DrawAdaptiveZedDebugMarkers(MarkerCanvas)") ==
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveDistancePhysicsSleeps % 4") ==
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveVisibleRagdollSleeps % 4") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveCorpseScenePressureLevel(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveLivingEnemyPressureLevel(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int ResolveAdaptiveLivingEnemyPressureLevel(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveLivingEnemyNoPendingPressureLevel=-1;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveLivingEnemyPendingPressureLevel=-1") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveLivingEnemyPressureLevel") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveLivingEnemyPendingPressureLevel") != std::string::npos);
    CHECK(telemetry_source.find(
        "float(AdaptiveLivingEnemyPressureLevel) / 5.0 + 0.03") !=
          std::string::npos);
    CHECK(telemetry_source.find("HoldSeconds = 0.75") != std::string::npos);
    CHECK(telemetry_source.find("HoldSeconds = 1.25") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveLivingEnemyPendingPressureLevel = 0;") ==
          std::string::npos);
    CHECK(count_occurrences(telemetry_source,
        "AdaptiveLivingEnemyNoPendingPressureLevel") >= 7);
    CHECK(telemetry_source.find(
        "AdaptiveLivingEnemyPendingPressureLevel != RequestedLevel") !=
          std::string::npos);
    CHECK(count_occurrences(telemetry_source,
        "AdaptiveLivingEnemyLastChangeRealTime = 0.0") >= 2);
    {
        constexpr int no_pending = -1;
        int current = 3;
        int pending = no_pending;
        double pending_since = 0.0;
        const auto advance = [&](int requested, double now,
                                 double hold_seconds) {
            if (pending != requested) {
                pending = requested;
                pending_since = now;
                return current;
            }
            if (now - pending_since < hold_seconds) return current;
            current = requested;
            pending = no_pending;
            pending_since = 0.0;
            return current;
        };
        CHECK(advance(0, 10.0, 1.25) == 3);
        CHECK(pending == 0);
        CHECK(advance(0, 11.249, 1.25) == 3);
        CHECK(advance(0, 11.25, 1.25) == 0);
        CHECK(pending == no_pending);
    }
    CHECK(telemetry_source.find(
        "AdaptiveLivingEnemyLastChangeRealTime < 1.5") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function float GetAdaptiveLivingEnemyPressureScale(") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCachedLivingEnemyPressureScale = FClamp(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_TELEMETRY_PROFILE schema=2") != std::string::npos);
    CHECK(telemetry_source.find("SampleSequence % 10 == 0") !=
          std::string::npos);
    CHECK(telemetry_source.find("GetSystemTime(") !=
          std::string::npos);
    CHECK(telemetry_source.find("GetProfileElapsedMilliseconds(") !=
          std::string::npos);
    CHECK(telemetry_source.find("timer=system_clock_ms resolution_us=1000") !=
          std::string::npos);
    CHECK(telemetry_source.find("clock_anomalies=") != std::string::npos);
    CHECK(telemetry_source.find("ElapsedMilliseconds <= 10000") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "StartMilliseconds >= 86390000 && EndMilliseconds <= 10000") !=
          std::string::npos);
    CHECK(telemetry_source.find("state=\"$ProfileState") !=
          std::string::npos);
    CHECK(telemetry_source.find("native_nodes=submitted") !=
          std::string::npos);
    CHECK(telemetry_source.find("ProfNodeStart(\"KF2OPT_Telemetry_Total\")") !=
          std::string::npos);
    CHECK(telemetry_source.find("ProfNodeStop(ProfileTotalNode)") !=
          std::string::npos);
    CHECK(telemetry_source.find("particle_pools_ms=") != std::string::npos);
    CHECK(telemetry_source.find("world_emitters_ms=") != std::string::npos);
    CHECK(telemetry_source.find("max_total_ms=") != std::string::npos);
    CHECK(telemetry_source.find("max_effect_actors_ms=") !=
          std::string::npos);
    CHECK(telemetry_source.find("max_world_emitters_ms=") !=
          std::string::npos);
    CHECK(telemetry_source.find("unclassified_ms=") != std::string::npos);
    const auto effect_profile_start = telemetry_source.find(
        "ProfNodeStart(\"KF2OPT_Telemetry_EffectActors\")");
    const auto effect_profile_end = telemetry_source.find(
        "ProfileEffectActorMilliseconds +=", effect_profile_start);
    CHECK(effect_profile_start != std::string::npos);
    CHECK(effect_profile_end != std::string::npos);
    const auto effect_scan = telemetry_source.substr(
        effect_profile_start, effect_profile_end - effect_profile_start);
    CHECK(effect_scan.find("class'Actor'") == std::string::npos);
    CHECK(effect_scan.find("RefreshDiagnosticEffectCache();") !=
          std::string::npos);
    const auto diagnostic_scan_start = telemetry_source.find(
        "function RefreshDiagnosticEffectCache()");
    const auto diagnostic_scan_end = telemetry_source.find(
        "function SampleTelemetry()", diagnostic_scan_start);
    CHECK(diagnostic_scan_start != std::string::npos);
    CHECK(diagnostic_scan_end != std::string::npos);
    const auto diagnostic_scan = telemetry_source.substr(
        diagnostic_scan_start, diagnostic_scan_end - diagnostic_scan_start);
    CHECK(diagnostic_scan.find("class'Actor'") == std::string::npos);
    CHECK(diagnostic_scan.find("class'KFSprayActor'") != std::string::npos);
    CHECK(diagnostic_scan.find("class'KFExplosionActor'") !=
          std::string::npos);
    CHECK(diagnostic_scan.find("class'KFProj_HansSmokeGrenade'") !=
          std::string::npos);
    CHECK(diagnostic_scan.find("class'KFProj_BloatPukeMine'") !=
          std::string::npos);
    CHECK(diagnostic_scan.find("class'KFGiblet'") != std::string::npos);
    CHECK(telemetry_source.find(
        "SampleSequence % DiagnosticEffectScanInterval == 0") !=
          std::string::npos);
    for (const auto* phased_effect_scan : {
             "SampleSequence % DiagnosticEffectScanInterval == 1",
             "SampleSequence % DiagnosticEffectScanInterval == 2",
             "SampleSequence % DiagnosticEffectScanInterval == 3",
             "SampleSequence % DiagnosticEffectScanInterval == 4"}) {
        CHECK(telemetry_source.find(phased_effect_scan) != std::string::npos);
    }
    CHECK(count_occurrences(
        telemetry_source, "SampleSequence == 0 ||") >= 5);
    CHECK(telemetry_source.find(
        "rotate one typed iterator per sample") != std::string::npos);
    CHECK(telemetry_source.find(
        "const DiagnosticEffectScanInterval=6;") != std::string::npos);
    CHECK(telemetry_source.find(
        "SampleSequence % DiagnosticEffectScanInterval == 5") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var WorldEmitterTelemetrySnapshot CachedWorldEmitters;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "world_emitter_scan_interval=\"$DiagnosticEffectScanInterval") !=
          std::string::npos);
    const auto world_snapshot_start = telemetry_source.find(
        "struct WorldEmitterTelemetrySnapshot");
    const auto world_snapshot_end = telemetry_source.find(
        "};", world_snapshot_start);
    CHECK(world_snapshot_start != std::string::npos);
    CHECK(world_snapshot_end != std::string::npos);
    const auto world_snapshot = telemetry_source.substr(
        world_snapshot_start, world_snapshot_end - world_snapshot_start);
    CHECK(world_snapshot.find("Emitter ") == std::string::npos);
    CHECK(world_snapshot.find("ParticleSystemComponent") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "const MaxWorldEmitterTemplateSnapshots=256;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var array<WorldEmitterTemplateTelemetrySnapshot> "
        "CachedWorldEmitterTemplates;") != std::string::npos);
    CHECK(telemetry_source.find(
        "var array<WorldEmitterTemplateTelemetrySnapshot> "
        "CachedWorldEmitterTraversalSnapshots;") != std::string::npos);
    CHECK(telemetry_source.find(
        "InspectWorldEmitterParticleComponentCached(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "FindWorldEmitterTemplateSnapshot(CacheKey, NewCacheIndex)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "CachedWorldEmitterTraversalSnapshots[TraversalIndex].Key == "
        "CacheKey") != std::string::npos);
    const auto world_emitter_refresh = telemetry_source.find(
        "function RefreshWorldEmitterCache(");
    CHECK(world_emitter_refresh != std::string::npos);
    CHECK(telemetry_source.find(
        "RefreshWorldEmitterCache(bCollectWorldParticleGroups);") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "InspectWorldEmitterParticleComponentCached(",
        world_emitter_refresh) != std::string::npos);
    CHECK(telemetry_source.find(
        "WorldEmitterComponents - 1,", world_emitter_refresh) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "CachedWorldEmitterTemplates.Find('Key', CacheKey)") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "CachedWorldEmitterTemplates.Insert(NewCacheIndex, 1)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "CacheKey = PathName(ParticleComponent.Template)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "world_emitter_template_cache_entries=") != std::string::npos);
    CHECK(telemetry_source.find(
        "world_emitter_template_cache_hits=") != std::string::npos);
    CHECK(telemetry_source.find(
        "world_emitter_template_cache_misses=") != std::string::npos);
    CHECK(telemetry_source.find(
        "world_emitter_template_position_hits=") != std::string::npos);
    const auto profile_window_start = telemetry_source.find(
        "if (bDetailedRuntimeDiagnostics && SampleSequence % 10 == 0)");
    const auto profile_window_end = telemetry_source.find(
        "`log(\"KF2OPT_TELEMETRY schema=7 sample=", profile_window_start);
    CHECK(profile_window_start != std::string::npos);
    CHECK(profile_window_end != std::string::npos);
    const auto profile_window = telemetry_source.substr(
        profile_window_start, profile_window_end - profile_window_start);
    CHECK(count_occurrences(
        profile_window, "ProfileWorldEmitterTemplatePositionHits = 0;") == 1);
    {
        int position_hits = 0;
        for (int window = 0; window < 2; ++window) {
            for (int sample = 0; sample < 10; ++sample) ++position_hits;
            CHECK(position_hits == 10);
            position_hits = 0;
        }
    }
    CHECK(telemetry_source.find(
        "const WorldParticleGroupScanInterval=30;") != std::string::npos);
    CHECK(telemetry_source.find(
        "struct WorldParticleGroupTelemetrySnapshot") != std::string::npos);
    CHECK(telemetry_source.find(
        "var array<WorldParticleGroupTelemetrySnapshot> "
        "ScannedWorldParticleGroups;") != std::string::npos);
    CHECK(telemetry_source.find(
        "CollectWorldParticlePoolGroups(WorldInfo.MyEmitterPool, "
        "\"world_pool\")") != std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.GroundFireEmitterPool, \"ground_fire\"") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.ImpactFXEmitterPool, \"impact_pool\"") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "\"placed\", WorldEmitter.ParticleSystemComponent") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_WORLD_PARTICLE_SOURCES schema=1") != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_WORLD_PARTICLE_GROUP schema=1") != std::string::npos);
    CHECK(telemetry_source.find(
        "struct AdaptiveWorldParticleIdleSnapshot") != std::string::npos);
    const auto idle_snapshot_start = telemetry_source.find(
        "struct AdaptiveWorldParticleIdleSnapshot");
    const auto idle_snapshot_end = telemetry_source.find(
        "};", idle_snapshot_start);
    CHECK(idle_snapshot_start != std::string::npos);
    CHECK(idle_snapshot_end != std::string::npos);
    const auto idle_snapshot = telemetry_source.substr(
        idle_snapshot_start, idle_snapshot_end - idle_snapshot_start);
    CHECK(idle_snapshot.find("var float OwnerCreationTime;") !=
          std::string::npos);
    CHECK(idle_snapshot.find("var int LastSeenGeneration;") !=
          std::string::npos);
    CHECK(idle_snapshot.find("Emitter ") == std::string::npos);
    CHECK(idle_snapshot.find("ParticleSystemComponent") == std::string::npos);
    CHECK(idle_snapshot.find("Object ") == std::string::npos);
    CHECK(telemetry_source.find(
        "function bool IsAdaptiveWorldParticleCosmetic(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool AdaptiveWorldParticleIdleOwnerMatches(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool ApplyAdaptiveWorldParticleIdleControl(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool RestoreAdaptiveWorldParticleIdleControl(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_WORLD_PARTICLE_IDLE state=applied") != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_WORLD_PARTICLE_IDLE state=restored") != std::string::npos);
    CHECK(telemetry_source.find(
        "reason=world_particle_idle_readback_mismatch") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveWorldParticleIdleStates.Length = 0;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveWorldParticleIdleScanGeneration = 0;") !=
          std::string::npos);
    const auto idle_restore_start = telemetry_source.find(
        "function bool RestoreAdaptiveWorldParticleIdleControl(");
    const auto idle_apply_start = telemetry_source.find(
        "function bool ApplyAdaptiveWorldParticleIdleControl(");
    const auto idle_apply_end = telemetry_source.find(
        "function bool ApplyAdaptiveEffectRuntimeReadback(",
        idle_apply_start);
    CHECK(idle_restore_start != std::string::npos);
    CHECK(idle_apply_start != std::string::npos);
    CHECK(idle_apply_end != std::string::npos);
    const auto idle_restore = telemetry_source.substr(
        idle_restore_start, idle_apply_start - idle_restore_start);
    const auto idle_apply = telemetry_source.substr(
        idle_apply_start, idle_apply_end - idle_apply_start);
    const auto restore_owner_check = idle_restore.find(
        "AdaptiveWorldParticleIdleOwnerMatches(");
    const auto restore_write = idle_restore.find(
        "ParticleComponent.SecondsBeforeInactive =");
    CHECK(restore_owner_check != std::string::npos);
    CHECK(restore_write != std::string::npos);
    CHECK(restore_owner_check < restore_write);
    const auto replacement_check = idle_apply.find(
        "!AdaptiveWorldParticleIdleOwnerMatches(");
    const auto replacement_capture = idle_apply.find(
        "OriginalSecondsBeforeInactive =");
    CHECK(replacement_check != std::string::npos);
    CHECK(replacement_capture != std::string::npos);
    CHECK(replacement_check < replacement_capture);
    CHECK(idle_apply.find(
        "OwnerCreationTime = WorldEmitter.CreationTime;") !=
          std::string::npos);
    CHECK(idle_apply.find("LastSeenGeneration = CurrentGeneration;") !=
          std::string::npos);
    CHECK(idle_apply.find(
        "AdaptiveWorldParticleIdleStates.Remove(") != std::string::npos);
    CHECK(idle_restore.find(
        "AdaptiveWorldParticleIdleStates.Remove(") != std::string::npos);
    CHECK(telemetry_source.find(
        "FX_Gameplay_EMIT.FX_Objective_White_Trail") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "FX_Gameplay_EMIT.Chr.FX_CHR_Fire_DOT") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "WEP_HRG_Warthog_EMIT.FX_HRG_Warthog_Grenade_Explosion") ==
          std::string::npos);
    const auto group_snapshot_start = telemetry_source.find(
        "struct WorldParticleGroupTelemetrySnapshot");
    const auto group_snapshot_end = telemetry_source.find(
        "};", group_snapshot_start);
    CHECK(group_snapshot_start != std::string::npos);
    CHECK(group_snapshot_end != std::string::npos);
    const auto group_snapshot = telemetry_source.substr(
        group_snapshot_start, group_snapshot_end - group_snapshot_start);
    CHECK(group_snapshot.find("Emitter ") == std::string::npos);
    CHECK(group_snapshot.find("ParticleSystemComponent") ==
          std::string::npos);
    CHECK(group_snapshot.find("ParticleSystem ") == std::string::npos);
    CHECK(group_snapshot.find("Object ") == std::string::npos);
    const auto template_snapshot_start = telemetry_source.find(
        "struct WorldEmitterTemplateTelemetrySnapshot");
    const auto template_snapshot_end = telemetry_source.find(
        "};", template_snapshot_start);
    CHECK(template_snapshot_start != std::string::npos);
    CHECK(template_snapshot_end != std::string::npos);
    const auto template_snapshot = telemetry_source.substr(
        template_snapshot_start,
        template_snapshot_end - template_snapshot_start);
    CHECK(template_snapshot.find("ParticleSystem ") == std::string::npos);
    CHECK(template_snapshot.find("ParticleSystemComponent") ==
          std::string::npos);
    CHECK(template_snapshot.find("Object ") == std::string::npos);
    CHECK(telemetry_source.find(
        "effect_actor_scan_interval=\"$DiagnosticEffectScanInterval") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function InspectParticleComponent(") != std::string::npos);
    CHECK(telemetry_source.find(
        "function ClassifyParticleComponent(") == std::string::npos);
    CHECK(telemetry_source.find(
        "function CountParticleSpawnEnvelope(") == std::string::npos);
    const auto particle_inspection_start = telemetry_source.find(
        "function InspectParticleComponent(");
    const auto particle_inspection_end = telemetry_source.find(
        "function InspectWorldEmitterParticleComponentCached(",
        particle_inspection_start);
    CHECK(particle_inspection_start != std::string::npos);
    CHECK(particle_inspection_end != std::string::npos);
    const auto particle_inspection = telemetry_source.substr(
        particle_inspection_start,
        particle_inspection_end - particle_inspection_start);
    CHECK(count_occurrences(
        particle_inspection, "ParticleComponent.GetLODLevel()") == 1);
    CHECK(count_occurrences(
        particle_inspection, "ParticleComponent.Template.Emitters.Length") ==
          1);
    CHECK(telemetry_source.find(
        "adaptive_controller_samples=") != std::string::npos);
    CHECK(telemetry_source.find(
        "max_adaptive_controller_ms=") != std::string::npos);
    CHECK(telemetry_source.find("zed_debug_samples=") != std::string::npos);
    CHECK(telemetry_source.find("max_zed_debug_ms=") != std::string::npos);
    CHECK(telemetry_source.find(
        "bActionTaken = RunAdaptiveCorpseLoadControl();") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "CollectAdaptiveZedDebugMarkers();") != std::string::npos);
    CHECK(telemetry_source.find("Clock(ProfileTotalSeconds)") ==
          std::string::npos);
    CHECK(telemetry_source.find("total_us=") == std::string::npos);
    CHECK(telemetry_source.find(
        "WeightedVisibleZeds += 1.0") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared < 360000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared < 1440000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.3") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "(WeightedVisibleZeds - 4.0) / 76.0") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveVisibleLivingZeds = LivingRecentlyRendered") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "PhysicsPressureLevel <= 0 && bRecentlyRendered") !=
          std::string::npos);
    const auto stagger_start = telemetry_source.find(
        "function bool RunAdaptiveCorpseLoadControl()");
    const auto zed_time_guard = telemetry_source.find(
        "GameInfo.IsZedTimeActive()", stagger_start);
    const auto wake_stage = telemetry_source.find(
        "WakeNearAdaptiveDistanceSleptCorpses()", stagger_start);
    const auto freeze_stage = telemetry_source.find(
        "FreezeOnePressureEligibleCorpse(", stagger_start);
    const auto baseline_sleep_stage = telemetry_source.find(
        "SleepBaselineAwakeMonsterCorpses(GoreManager)", stagger_start);
    const auto distant_sleep_stage = telemetry_source.find(
        "SleepOneDistantMonsterCorpse(", stagger_start);
    CHECK(stagger_start != std::string::npos);
    CHECK(zed_time_guard != std::string::npos);
    CHECK(baseline_sleep_stage != std::string::npos);
    CHECK(wake_stage != std::string::npos);
    CHECK(freeze_stage != std::string::npos);
    CHECK(distant_sleep_stage != std::string::npos);
    const auto adaptive_off_restore = telemetry_source.find(
        "if (!bAdaptiveCorpseStagger || !bAdaptiveRuntimeEnabled)\n"
        "    {\n        BeginAdaptiveCorpsePhysicsRelease();",
        stagger_start);
    CHECK(adaptive_off_restore != std::string::npos);
    CHECK(adaptive_off_restore < baseline_sleep_stage);
    CHECK(zed_time_guard < baseline_sleep_stage);
    CHECK(telemetry_source.find(
        "switch (AdaptiveCorpseControlPhase)", stagger_start) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseControlPhase + 1", stagger_start) !=
          std::string::npos);
    CHECK(count_occurrences(
        telemetry_source, "ScanCount = Min(AdaptiveCorpseScanBudget,") >= 6);
    CHECK(count_occurrences(
        telemetry_source, "Scanned < AdaptiveCorpseScanBudget") >= 7);
    CHECK(telemetry_source.find(
        "var float AdaptiveLastPhysicsMutationWorldTime") !=
          std::string::npos);
    const auto physics_frame_reservation = telemetry_source.find(
        "function bool ReserveAdaptivePhysicsMutationForCurrentFrame()");
    CHECK(physics_frame_reservation != std::string::npos);
    const auto physics_frame_reservation_end = telemetry_source.find(
        "\nfunction ", physics_frame_reservation + 1);
    CHECK(physics_frame_reservation_end != std::string::npos);
    const auto physics_frame_reservation_body = telemetry_source.substr(
        physics_frame_reservation,
        physics_frame_reservation_end - physics_frame_reservation);
    CHECK(physics_frame_reservation_body.find(
        "AdaptiveLastPhysicsMutationWorldTime == WorldInfo.TimeSeconds") !=
          std::string::npos);
    CHECK(physics_frame_reservation_body.find(
        "AdaptiveLastPhysicsMutationWorldTime = WorldInfo.TimeSeconds") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveLastPhysicsMutationWorldTime=-1.0") != std::string::npos);
    const char* physics_mutation_functions[] = {
        "function int SleepBaselineAwakeMonsterCorpses(",
        "function bool RestoreAdaptiveCorpseFreezeState(",
        "function bool FreezeOnePressureEligibleCorpse(",
        "function int WakeNearAdaptiveDistanceSleptCorpses()",
        "function int WakeAdaptiveDistanceSleptCorpseBatch()",
        "function bool SleepOneDistantMonsterCorpse(",
        "function bool SleepOneVisibleMonsterCorpse("};
    const char* physics_mutation_calls[] = {
        "Candidate.Mesh.PutRigidBodyToSleep()",
        "Candidate.SetPhysics(PHYS_RigidBody)",
        "Candidate.SetPhysics(PHYS_None)",
        "Candidate.Mesh.WakeRigidBody()",
        "Candidate.Mesh.WakeRigidBody()",
        "Candidate.Mesh.PutRigidBodyToSleep()",
        "Candidate.Mesh.PutRigidBodyToSleep()"};
    for (std::size_t mutation_index = 0;
         mutation_index < std::size(physics_mutation_functions);
         ++mutation_index) {
        const auto mutation_function_start =
            telemetry_source.find(physics_mutation_functions[mutation_index]);
        CHECK(mutation_function_start != std::string::npos);
        const auto mutation_function_end = telemetry_source.find(
            "\nfunction ", mutation_function_start + 1);
        CHECK(mutation_function_end != std::string::npos);
        const auto mutation_function_body = telemetry_source.substr(
            mutation_function_start,
            mutation_function_end - mutation_function_start);
        const auto frame_reservation_call = mutation_function_body.find(
            "ReserveAdaptivePhysicsMutationForCurrentFrame()");
        const auto physics_mutation_call = mutation_function_body.find(
            physics_mutation_calls[mutation_index]);
        CHECK(frame_reservation_call != std::string::npos);
        CHECK(physics_mutation_call != std::string::npos);
        CHECK(frame_reservation_call < physics_mutation_call);
    }
    for (const auto* cursor : {"AdaptiveCleanupScanCursor",
             "AdaptiveBaselineScanCursor", "AdaptiveFreezeScanCursor",
             "AdaptiveDistanceScanCursor", "AdaptiveRagdollScanCursor",
             "FixedMinimumCorpseLodScanCursor",
             "FixedMinimumAnimationScanCursor"}) {
        CHECK(telemetry_source.find(cursor) != std::string::npos);
    }
    CHECK(zed_time_guard < wake_stage);
    CHECK(zed_time_guard < distant_sleep_stage);
    const auto frame_only_action_gate = telemetry_source.find(
        "if (AdaptiveCorpsePressureLevel <= 0)", stagger_start);
    CHECK(frame_only_action_gate == std::string::npos);
    CHECK(telemetry_source.find(
        "PhysicsPressureLevel = Max(", stagger_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "RagdollPressureLevel = Max(", stagger_start) != std::string::npos);

    const auto pressure_freeze = telemetry_source.find(
        "function bool FreezeOnePressureEligibleCorpse(");
    const auto pressure_freeze_end = telemetry_source.find(
        "function RemoveAdaptiveDistanceSleptCorpseEntry(", pressure_freeze);
    CHECK(pressure_freeze != std::string::npos);
    CHECK(pressure_freeze_end != std::string::npos);
    const auto pressure_freeze_body = telemetry_source.substr(
        pressure_freeze, pressure_freeze_end - pressure_freeze);
    for (const auto* threshold : {"MinimumAgeSeconds = 15",
             "MinimumAgeSeconds = 10", "MinimumAgeSeconds = 5",
             "MinimumDistanceUnits = 1000", "MinimumDistanceUnits = 800",
             "MinimumDistanceUnits = 500",
             "MinimumFreezeInterval = 0.25",
             "MinimumFreezeInterval = AdaptiveCorpseFreezeBurstInterval"}) {
        CHECK(pressure_freeze_body.find(threshold) != std::string::npos);
    }
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseFreezeBurstInterval=0.05;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseFreezeBurstLimit=12;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const AdaptiveCorpseFreezeBurstAwakeThreshold=24;") !=
          std::string::npos);
    const auto burst_scheduler = telemetry_source.find(
        "AdaptiveCorpseBurstFreezeCount <");
    CHECK(burst_scheduler != std::string::npos);
    CHECK(telemetry_source.find(
        "bAdaptiveCorpseControlUrgentRepeat = true;",
        burst_scheduler) != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseBurstFreezeCount = 0;",
        burst_scheduler) != std::string::npos);
    CHECK(pressure_freeze_body.find("Candidate.Mesh.RigidBodyIsAwake()") !=
          std::string::npos);
    CHECK(pressure_freeze_body.find("Candidate.SetPhysics(PHYS_None)") !=
          std::string::npos);
    CHECK(telemetry_source.find("var bool bOriginalTickDisabled;") !=
          std::string::npos);
    CHECK(telemetry_source.find("var bool bOriginalCollideActors;") !=
          std::string::npos);
    CHECK(telemetry_source.find("var bool bOriginalBlockActors;") !=
          std::string::npos);
    CHECK(telemetry_source.find("var bool bOriginalIgnoreEncroachers;") !=
          std::string::npos);
    CHECK(telemetry_source.find("var bool bOriginalBlockRigidBody;") !=
          std::string::npos);
    CHECK(pressure_freeze_body.find(
        "AdaptiveFrozenCorpses[Index].bRestorePending = false;") !=
          std::string::npos);
    const auto freeze_disable_collision = pressure_freeze_body.find(
        "Candidate.SetCollision(false, false,");
    const auto freeze_disable_rigid_body_collision = pressure_freeze_body.find(
        "Candidate.CollisionComponent.SetBlockRigidBody(false)");
    const auto freeze_disable_tick = pressure_freeze_body.find(
        "Candidate.SetTickIsDisabled(true)");
    const auto freeze_physics_none = pressure_freeze_body.find(
        "Candidate.SetPhysics(PHYS_None)");
    CHECK(freeze_disable_collision != std::string::npos);
    CHECK(freeze_disable_rigid_body_collision != std::string::npos);
    CHECK(freeze_disable_tick != std::string::npos);
    CHECK(freeze_disable_collision < freeze_physics_none);
    CHECK(freeze_disable_rigid_body_collision < freeze_physics_none);
    CHECK(freeze_disable_tick < freeze_physics_none);
    CHECK(count_occurrences(
        pressure_freeze_body, ".bRestorePending = true;") == 2);
    CHECK(count_occurrences(
        pressure_freeze_body, "TryRestoreAdaptiveCorpseFreeze(") == 2);
    CHECK(count_occurrences(
        pressure_freeze_body,
        "AdaptiveFrozenCorpses.Remove(Index, 1)") == 2);
    const auto offline_prephysics_failure = pressure_freeze_body.find(
        "if (Candidate.bCollideActors || Candidate.bBlockActors ||");
    const auto offline_postphysics_failure = pressure_freeze_body.find(
        "if (Candidate.Physics != PHYS_None)");
    CHECK(offline_prephysics_failure != std::string::npos);
    CHECK(offline_postphysics_failure != std::string::npos);
    CHECK(pressure_freeze_body.find(
        ".bRestorePending = true;", offline_prephysics_failure) <
          offline_postphysics_failure);
    CHECK(pressure_freeze_body.find(
        "TryRestoreAdaptiveCorpseFreeze(", offline_prephysics_failure) <
          offline_postphysics_failure);
    CHECK(pressure_freeze_body.find(
        ".bRestorePending = true;", offline_postphysics_failure) !=
          std::string::npos);
    CHECK(pressure_freeze_body.find(
        "TryRestoreAdaptiveCorpseFreeze(", offline_postphysics_failure) !=
          std::string::npos);
    CHECK(pressure_freeze_body.find(
        "state=frozen reason=pressure_eligible") != std::string::npos);
    CHECK(pressure_freeze_body.find("physics=none readback=verified") !=
          std::string::npos);
    CHECK(pressure_freeze_body.find(
        "collision=disabled tick=disabled") != std::string::npos);
    CHECK(pressure_freeze_body.find("RequiredWakeCount") ==
          std::string::npos);
    CHECK(pressure_freeze_body.find("PutRigidBodyToSleep") ==
          std::string::npos);
    CHECK(pressure_freeze_body.find("bNoSkeletonUpdate") ==
          std::string::npos);
    CHECK(pressure_freeze_body.find("Candidate.Mesh.RigidBodyIsAwake()") <
          pressure_freeze_body.find("Candidate.SetPhysics(PHYS_None)"));
    CHECK(pressure_freeze_body.find(
        "FindAdaptiveCorpseFreeze(Candidate) >= 0") != std::string::npos);
    const auto restore_freeze_function = telemetry_source.find(
        "function bool RestoreAdaptiveCorpseFreezeState(");
    CHECK(restore_freeze_function != std::string::npos);
    const auto restore_freeze_end = telemetry_source.find(
        "function bool TryRestoreAdaptiveCorpseFreeze(",
        restore_freeze_function);
    CHECK(restore_freeze_end != std::string::npos);
    const auto restore_freeze_body = telemetry_source.substr(
        restore_freeze_function, restore_freeze_end - restore_freeze_function);
    CHECK(restore_freeze_body.find("Candidate.SetPhysics(PHYS_RigidBody)") !=
          std::string::npos);
    CHECK(restore_freeze_body.find("Candidate.SetCollision(") !=
          std::string::npos);
    CHECK(restore_freeze_body.find(
        "Candidate.CollisionComponent.SetBlockRigidBody(") !=
          std::string::npos);
    CHECK(restore_freeze_body.find("Candidate.SetTickIsDisabled(") !=
          std::string::npos);
    CHECK(restore_freeze_body.find(
        "state=unfrozen reason=\"$Reason") != std::string::npos);
    CHECK(restore_freeze_body.find("physics=rigid_body readback=verified") !=
          std::string::npos);
    CHECK(restore_freeze_body.find(
        "collision=restored tick=restored") != std::string::npos);
    CHECK(restore_freeze_body.find(
        "GetAdaptiveCorpseActionId(Candidate) !=") != std::string::npos);
    CHECK(restore_freeze_body.find(
        "LogAdaptiveCorpseFreezeReleaseFailure(") != std::string::npos);
    CHECK(restore_freeze_body.find(
        "(Candidate.CollisionComponent != None) != bHadCollisionComponent") !=
          std::string::npos);

    const auto manager_retire_function = telemetry_source.find(
        "function RetireAdaptiveCorpseManagerOwnership(");
    const auto manager_retire_end = telemetry_source.find(
        "function InitializeAdaptiveCorpseStagger(",
        manager_retire_function);
    CHECK(manager_retire_function != std::string::npos);
    CHECK(manager_retire_end != std::string::npos);
    const auto manager_retire_body = telemetry_source.substr(
        manager_retire_function,
        manager_retire_end - manager_retire_function);
    CHECK(manager_retire_body.find(
        "AdaptiveCorpseManager == None &&\n"
        "         !bAdaptiveCorpseStaggerInitialized") !=
          std::string::npos);
    const auto retire_frozen = manager_retire_body.find(
        "AdaptiveRetiredFrozenCorpses.AddItem(");
    const auto clear_active_frozen = manager_retire_body.find(
        "AdaptiveFrozenCorpses.Length = 0;");
    const auto retire_slept = manager_retire_body.find(
        "AdaptiveRetiredDistanceSleptCorpses.AddItem(");
    const auto clear_active_slept = manager_retire_body.find(
        "AdaptiveDistanceSleptCorpses.Length = 0;");
    CHECK(retire_frozen != std::string::npos);
    CHECK(clear_active_frozen != std::string::npos);
    CHECK(retire_slept != std::string::npos);
    CHECK(clear_active_slept != std::string::npos);
    CHECK(retire_frozen < clear_active_frozen);
    CHECK(retire_slept < clear_active_slept);
    CHECK(manager_retire_body.find(
        "AdaptiveCorpseManager.MaxDeadBodies = AdaptiveCorpseOriginalLimit") !=
          std::string::npos);
    for (const auto* old_manager_state : {
             "AdaptiveBaselineSettleEntries.Length = 0",
             "FixedMinimumCorpseLodCorpses.Length = 0",
             "FixedMinimumLivingVisualZeds.Length = 0",
             "AdaptiveDistanceSleepTransitions.Length = 0",
             "AdaptiveCorpsePhysicsActionIds.Length = 0",
             "FixedMinimumLivingScanPawn = None"}) {
        CHECK(manager_retire_body.find(old_manager_state) !=
              std::string::npos);
    }
    const auto initialize_manager = telemetry_source.find(
        "function InitializeAdaptiveCorpseStagger(");
    const auto initialize_manager_end = telemetry_source.find(
        "function AdjustAdaptiveCorpseCapacity(", initialize_manager);
    CHECK(initialize_manager != std::string::npos);
    CHECK(initialize_manager_end != std::string::npos);
    const auto initialize_manager_body = telemetry_source.substr(
        initialize_manager, initialize_manager_end - initialize_manager);
    CHECK(initialize_manager_body.find(
        "RetireAdaptiveCorpseManagerOwnership(GoreManager);") <
          initialize_manager_body.find(
              "AdaptiveCorpseManager = GoreManager;"));
    CHECK(initialize_manager_body.find(
        "AdaptiveFrozenCorpses.Length = 0") == std::string::npos);

    const auto retired_freeze_release = telemetry_source.find(
        "function int ReleaseOneRetiredAdaptiveCorpseFreeze()");
    const auto retired_freeze_release_end = telemetry_source.find(
        "function int WakeOneRetiredAdaptiveDistanceSleptCorpse()",
        retired_freeze_release);
    CHECK(retired_freeze_release != std::string::npos);
    CHECK(retired_freeze_release_end != std::string::npos);
    const auto retired_freeze_release_body = telemetry_source.substr(
        retired_freeze_release,
        retired_freeze_release_end - retired_freeze_release);
    CHECK(retired_freeze_release_body.find(
        "RestoreAdaptiveCorpseFreezeState(") != std::string::npos);
    CHECK(retired_freeze_release_body.find(
        "(Index + 1) % AdaptiveRetiredFrozenCorpses.Length") !=
          std::string::npos);

    const auto retired_sleep_release = retired_freeze_release_end;
    const auto retired_sleep_release_end = telemetry_source.find(
        "function int ReleaseOneRetiredAdaptiveCorpseOwnership()",
        retired_sleep_release);
    CHECK(retired_sleep_release_end != std::string::npos);
    const auto retired_sleep_release_body = telemetry_source.substr(
        retired_sleep_release,
        retired_sleep_release_end - retired_sleep_release);
    CHECK(retired_sleep_release_body.find(
        "Candidate.Mesh.WakeRigidBody()") != std::string::npos);
    CHECK(retired_sleep_release_body.find(
        "manager_replaced_wake_readback") != std::string::npos);
    CHECK(retired_sleep_release_body.find(
        "(Index + 1) %\n"
        "                        AdaptiveRetiredDistanceSleptCorpses.Length") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "ReleaseOneRetiredAdaptiveCorpseOwnership() > 0",
        stagger_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveRetiredFrozenCorpses.Length > 0 ||") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveRetiredDistanceSleptCorpses.Length > 0") !=
          std::string::npos);

    const auto freeze_release_function = telemetry_source.find(
        "function int ReleaseOneAdaptiveCorpseFreeze(bool bRestoreAll)");
    const auto freeze_release_end = telemetry_source.find(
        "function PruneAdaptiveCorpseFreezes()", freeze_release_function);
    CHECK(freeze_release_function != std::string::npos);
    CHECK(freeze_release_end != std::string::npos);
    const auto freeze_release_body = telemetry_source.substr(
        freeze_release_function,
        freeze_release_end - freeze_release_function);
    CHECK(freeze_release_body.find(
        "Candidate == None || Candidate.bDeleteMe") != std::string::npos);
    CHECK(freeze_release_body.find(
        "CurrentId != AdaptiveFrozenCorpses[Index].CorpseId") !=
          std::string::npos);
    CHECK(freeze_release_body.find(
        "IsAdaptiveCorpseRecycledStateSafe(Candidate)") !=
          std::string::npos);
    CHECK(freeze_release_body.find(
        "reason=reused_state_verified") != std::string::npos);
    CHECK(freeze_release_body.find(
        "reused_state_unverified") != std::string::npos);
    CHECK(freeze_release_body.find(
        "!IsAdaptiveCorpseInPool(Candidate)") !=
          std::string::npos);
    CHECK(freeze_release_body.find(
        "AdaptiveFrozenCorpses[Index].bRestorePending ||") !=
          std::string::npos);
    CHECK(freeze_release_body.find("\"freeze_rollback\"") !=
          std::string::npos);
    CHECK(freeze_release_body.find(
        "Index, bRestoreAll ?") != std::string::npos);
    CHECK(freeze_release_body.find(
        "(Index + 1) % AdaptiveFrozenCorpses.Length") !=
          std::string::npos);
    const auto restore_state_start = telemetry_source.find(
        "function bool RestoreAdaptiveCorpseFreezeState(");
    const auto restore_state_end = telemetry_source.find(
        "function bool TryRestoreAdaptiveCorpseFreeze(", restore_state_start);
    CHECK(restore_state_start != std::string::npos);
    CHECK(restore_state_end != std::string::npos);
    const auto restore_state_body = telemetry_source.substr(
        restore_state_start, restore_state_end - restore_state_start);
    const auto reservation = restore_state_body.find(
        "!ReserveAdaptivePhysicsMutationForCurrentFrame()");
    const auto readback = restore_state_body.find(
        "if (Candidate.bCollideActors != bOriginalCollideActors ||");
    const auto physics_write = restore_state_body.find(
        "Candidate.SetPhysics(PHYS_RigidBody);");
    const auto release_callback_start = telemetry_source.find(
        "function AdaptiveCorpsePhysicsRelease()");
    const auto release_callback_end = telemetry_source.find(
        "function BeginAdaptiveCorpsePhysicsRelease()", release_callback_start);
    CHECK(release_callback_start != std::string::npos);
    CHECK(release_callback_end != std::string::npos);
    const auto release_callback = telemetry_source.substr(
        release_callback_start, release_callback_end - release_callback_start);
    const OfflineReleasePolicy release_policy{
        .late_reservation = readback != std::string::npos &&
            reservation != std::string::npos && readback < reservation &&
            reservation < physics_write,
        .yield_after_attempt = offline_restore_yields_after_attempt(
            freeze_release_body, "TryRestoreAdaptiveCorpseFreeze(") &&
            offline_restore_yields_after_attempt(retired_freeze_release_body,
                "RestoreAdaptiveCorpseFreezeState("),
        .fair_lanes = release_callback.find(
            "for (Attempt = 0; Attempt < 4; ++Attempt)") != std::string::npos &&
            release_callback.find(
                "AdaptiveCorpsePhysicsReleasePhase = (Phase + 1) % 4;") !=
                std::string::npos &&
            count_occurrences(release_callback, "bHandled = true;") == 4 &&
            release_callback.find(
                "if (bHandled)\n        {\n            break;") !=
                std::string::npos};
    CHECK(restore_state_body.find("AdaptiveFrozenCorpses") ==
          std::string::npos);
    CHECK(telemetry_source.find("const AdaptiveCorpseScanBudget=64;") !=
          std::string::npos);
    for (const auto* queue : {"AdaptiveFrozenCorpses",
        "AdaptiveRetiredFrozenCorpses", "AdaptiveDistanceSleptCorpses",
        "AdaptiveRetiredDistanceSleptCorpses"}) {
        CHECK(release_callback.find(std::string{queue} + ".Length > 0)") !=
              std::string::npos);
    }
    // Existing main's early reservation + 64-slot wrap can revisit the same
    // failed actor forever; a single physics-failing actor also blocks wakes.
    CHECK(!offline_release_progress(64, 0, false, {false, false, false}));
    CHECK(!offline_release_progress(1, 0, true, {true, true, false}));
    CHECK(!offline_release_progress(64, 0, true, {true, false, true}));
    CHECK(offline_release_progress(64, 0, false, release_policy));
    for (const auto length : {0, 1, 2, 8, 9, 64, 65, 128, 129, 2000}) {
        for (const bool physics_failure : {false, true}) {
            for (const int failed_id : {-1, 0, length / 2, length - 1}) {
                CHECK(offline_release_progress(
                    length, failed_id, physics_failure, release_policy));
            }
        }
    }
    CHECK(telemetry_source.find(
        "function PruneAdaptiveCorpseFreezes()\n"
        "{\n    ReleaseOneAdaptiveCorpseFreeze(false);") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int RestoreOneAdaptiveCorpseFreeze()\n"
        "{\n    return ReleaseOneAdaptiveCorpseFreeze(true);") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "state=release_failed corpse_id=") != std::string::npos);
    CHECK(telemetry_source.find(
        "ownership=retained") != std::string::npos);
    CHECK(telemetry_source.find(
        "reason=world_teardown count=") != std::string::npos);
    CHECK(telemetry_source.find(
        "safe_boundary=world_destroy") != std::string::npos);

    const auto cleanup_start = telemetry_source.find(
        "function bool StaggerCorpseCleanup()");
    const auto cleanup_zed_time_guard = telemetry_source.find(
        "GameInfo.IsZedTimeActive()", cleanup_start);
    const auto cleanup_delete = telemetry_source.find(
        "GoreManager.RemoveAndDeleteCorpse(SelectedIndex)", cleanup_start);
    CHECK(cleanup_start != std::string::npos);
    CHECK(cleanup_zed_time_guard != std::string::npos);
    CHECK(cleanup_delete != std::string::npos);
    CHECK(cleanup_zed_time_guard < cleanup_delete);

    const auto wake_function = telemetry_source.find(
        "function int WakeNearAdaptiveDistanceSleptCorpses()");
    const auto wake_threshold = telemetry_source.find(
        "DistanceSquared >= 640000.0", wake_function);
    const auto wake_call = telemetry_source.find(
        "Candidate.Mesh.WakeRigidBody()", wake_function);
    const auto wake_readback = telemetry_source.find(
        "if (!Candidate.Mesh.RigidBodyIsAwake())", wake_call);
    const auto wake_tracking_release = telemetry_source.find(
        "RemoveAdaptiveDistanceSleptCorpseEntry(Index, \"optimizer_wake\")",
        wake_function);
    const auto wake_receipt = telemetry_source.find(
        "KF2OPT_CORPSE_DISTANCE state=wake", wake_function);
    CHECK(wake_function != std::string::npos);
    CHECK(wake_threshold != std::string::npos);
    CHECK(wake_call != std::string::npos);
    CHECK(wake_readback != std::string::npos);
    CHECK(wake_tracking_release != std::string::npos);
    CHECK(wake_receipt != std::string::npos);
    CHECK(wake_threshold < wake_call);
    CHECK(wake_call < wake_readback);
    CHECK(wake_readback < wake_tracking_release);
    CHECK(wake_tracking_release < wake_receipt);
    const auto release_wake_start = telemetry_source.find(
        "function int WakeAdaptiveDistanceSleptCorpseBatch()");
    const auto release_wake_end = telemetry_source.find(
        "\nfunction ", release_wake_start + 1);
    CHECK(release_wake_start != std::string::npos);
    CHECK(release_wake_end != std::string::npos);
    const auto release_wake_body = telemetry_source.substr(
        release_wake_start, release_wake_end - release_wake_start);
    const auto release_wake_policy = offline_wake_policy(release_wake_body);
    CHECK(release_wake_policy.retain_failed);
    CHECK(release_wake_policy.advance_failed);
    CHECK(release_wake_policy.restore_already_awake);
    CHECK(release_wake_policy.validate_identity);
    auto lost_retry_release = release_wake_body;
    const auto release_wake_call = lost_retry_release.find(
        "Candidate.Mesh.WakeRigidBody();");
    const auto failed_wake_return = lost_retry_release.find(
        "return 0;", release_wake_call);
    CHECK(failed_wake_return != std::string::npos);
    lost_retry_release.insert(failed_wake_return,
        "RemoveAdaptiveDistanceSleptCorpseEntry(Index, \"adaptive_disabled\");\n");
    CHECK(!offline_wake_policy(lost_retry_release).retain_failed);
    auto stuck_cursor_release = release_wake_body;
    const auto failed_cursor_advance = stuck_cursor_release.find(
        "(Index + 1) %", release_wake_call);
    CHECK(failed_cursor_advance != std::string::npos);
    stuck_cursor_release.replace(failed_cursor_advance,
        std::string_view{"(Index + 1) %"}.size(), "Index %");
    CHECK(!offline_wake_policy(stuck_cursor_release).advance_failed);
    CHECK(count_occurrences(release_wake_body,
        "Candidate.Mesh.WakeRigidBody();") == 1);
    CHECK(release_wake_body.find("Scanned < AdaptiveCorpseScanBudget") !=
          std::string::npos);
    CHECK(release_wake_body.find(
        "if (WakeCount > 0)\n        {\n            return WakeCount;") !=
          std::string::npos);
    CHECK(release_wake_body.find("AdaptiveDistanceWakeScanCursor") ==
          std::string::npos);
    CHECK(retired_sleep_release_body.find(
        "else if (Candidate.Mesh.RigidBodyIsAwake())\n"
        "            {\n"
        "                Candidate.Mesh.bNoSkeletonUpdate = false;\n"
        "                AdaptiveRetiredDistanceSleptCorpses.Remove(Index, 1);") !=
          std::string::npos);
    const OfflineWakePolicy correct_wake_policy{true, true, true, true};
    CHECK(!offline_wake_progress(1, 0, false, false,
                                {false, false, false, false}));
    CHECK(!offline_wake_progress(2, 1, false, false,
                                {true, false, true, true}));
    CHECK(!offline_wake_progress(2, -1, false, false,
                                {true, true, false, true}));
    CHECK(!offline_wake_progress(5, -1, false, true,
                                {true, true, true, false}));
    for (const int length : {0, 1, 2, 8, 9, 64, 65, 128, 129, 2000}) {
        for (const int failed_id : {-2, -1, 0, length / 2, length - 1}) {
            CHECK(offline_wake_progress(length, failed_id, false, false,
                                       correct_wake_policy));
            CHECK(offline_wake_progress(length, failed_id, false, false,
                                       release_wake_policy));
        }
        CHECK(offline_wake_progress(length, 0, true, false,
                                   release_wake_policy));
        CHECK(offline_wake_progress(length, -1, false, true,
                                   release_wake_policy));
    }
    CHECK(telemetry_source.find(
        "function int WakeNearAdaptiveDistanceSleptCorpses()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "WakeCount < Max(3, AttackScale)", stagger_start) ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "WakeNearAdaptiveDistanceSleptCorpses() > 0", stagger_start) !=
          std::string::npos);
    const auto freeze_capacity = telemetry_source.find(
        "CanRegisterAdaptiveCorpsePhysicsAction(Candidate, \"aging_freeze\")");
    const auto freeze_mutation = telemetry_source.find(
        "Candidate.SetPhysics(PHYS_None)", freeze_capacity);
    const auto freeze_registration = telemetry_source.find(
        "RegisterAdaptiveCorpsePhysicsAction(Candidate, \"aging_freeze\")",
        freeze_mutation);
    CHECK(freeze_capacity != std::string::npos);
    CHECK(freeze_mutation != std::string::npos);
    CHECK(freeze_registration != std::string::npos);
    CHECK(freeze_capacity < freeze_mutation);
    CHECK(freeze_mutation < freeze_registration);

    const auto lod_selector = telemetry_source.find(
        "function KFPawn SelectVisibleMonsterCorpseForMinimumLod(");
    const auto lod_apply = telemetry_source.find(
        "function bool ApplyOneFixedMinimumCorpseLod(", lod_selector);
    CHECK(lod_selector != std::string::npos);
    CHECK(lod_apply != std::string::npos);
    CHECK(telemetry_source.substr(
        lod_selector, lod_apply - lod_selector).find(
            "Candidate.Mesh.RigidBodyIsAwake()") == std::string::npos);
    CHECK(telemetry_source.substr(
        lod_selector, lod_apply - lod_selector).find(
            "FindAdaptiveDistanceSleptCorpse(Candidate)") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "MaximumMinLod = Candidate.Mesh.SkeletalMesh.LODInfo.Length - 1",
        lod_selector) != std::string::npos);
    CHECK(telemetry_source.find(
        "FixedMinimumCorpseLodCorpses.Length >=", lod_apply) ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "CandidateTarget = MaximumMinLod", lod_selector) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "CandidateTarget = 2", lod_selector) == std::string::npos);
    CHECK(telemetry_source.find(
        "CandidateTarget += Clamp(PressureLevel, 0, 5)", lod_selector) ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared >= 12960000.0", lod_selector) ==
          std::string::npos);
    CHECK(count_occurrences(telemetry_source,
        "Max(AdaptiveCorpsePressureLevel, ScenePressureLevel)") >= 2);
    CHECK(count_occurrences(telemetry_source, "EnemyPressureLevel);") >= 2);
    CHECK(telemetry_source.find(
        "function bool ApplyLivingEnemyMinimumVisuals()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "EnemyPressureLevel = ResolveAdaptiveLivingEnemyPressureLevel(",
        stagger_start) != std::string::npos);
    const auto fixed_visual_start = telemetry_source.find(
        "function bool RunFixedMinimumVisualControl()");
    CHECK(fixed_visual_start != std::string::npos);
    CHECK(telemetry_source.find(
        "bActionTaken = ApplyLivingEnemyMinimumVisuals();",
        fixed_visual_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "const FixedMinimumLivingVisualBurstInterval=0.05;") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "const FixedMinimumLivingVisualBurstLimit=16;") !=
          std::string::npos);
    const auto living_burst_scheduler = telemetry_source.find(
        "FixedMinimumLivingVisualBurstCount <",
        fixed_visual_start);
    CHECK(living_burst_scheduler != std::string::npos);
    CHECK(telemetry_source.find(
        "bFixedMinimumLivingVisualUrgentRepeat = true;",
        living_burst_scheduler) != std::string::npos);
    CHECK(telemetry_source.find(
        "FixedMinimumLivingVisualBurstCount = 0;",
        living_burst_scheduler) != std::string::npos);
    CHECK(telemetry_source.find(
        "if (!bFixedMinimumLivingVisualUrgentRepeat)",
        living_burst_scheduler) != std::string::npos);
    CHECK(telemetry_source.find(
        "while (ScanPawn != None && Scanned < AdaptiveCorpseScanBudget)") !=
          std::string::npos);
    const auto living_apply_start = telemetry_source.find(
        "function bool ApplyLivingEnemyMinimumVisuals()");
    const auto living_apply_end = telemetry_source.find(
        "function bool ShouldPreserveNearCorpseDetail(", living_apply_start);
    CHECK(living_apply_start != std::string::npos);
    CHECK(living_apply_end != std::string::npos);
    const auto living_apply_body = telemetry_source.substr(
        living_apply_start, living_apply_end - living_apply_start);
    CHECK(living_apply_body.find("WorldInfo.AllPawns") == std::string::npos);
    CHECK(living_apply_body.find(
        "FixedMinimumLivingScanPawn = ScanPawn.NextPawn") !=
          std::string::npos);
    CHECK(count_occurrences(
        telemetry_source, "FixedMinimumLivingScanPawn = None;") >= 2);
    CHECK(telemetry_source.find(
        "function AdaptiveCorpsePhysicsRelease()") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveFrozenCorpses.Length > 0 ||\n"
        "                AdaptiveDistanceSleptCorpses.Length > 0") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.AnimationLODDistanceFactor =") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.AnimationLODFrameRate =") != std::string::npos);
    CHECK(living_apply_body.find(
        "TargetMinLod = MaximumMinLod") != std::string::npos);
    CHECK(living_apply_body.find(
        "TargetAnimRate = 6") != std::string::npos);
    CHECK(living_apply_body.find(
        "TargetAnimDistance = 0.55") != std::string::npos);
    CHECK(telemetry_source.find(
        "FixedMinimumLivingOffscreenDistanceSquared") == std::string::npos);
    const auto living_offscreen_policy_start = telemetry_source.find(
        "function bool LivingOffscreenAnimationRequiresNativeTick(");
    const auto living_offscreen_policy_end = telemetry_source.find(
        "function bool ApplyLivingEnemyMinimumVisuals()",
        living_offscreen_policy_start);
    CHECK(living_offscreen_policy_start != std::string::npos);
    CHECK(living_offscreen_policy_end != std::string::npos);
    const auto living_offscreen_policy_body = telemetry_source.substr(
        living_offscreen_policy_start,
        living_offscreen_policy_end - living_offscreen_policy_start);
    CHECK(living_apply_body.find(
        "Candidate.Mesh.bTickAnimNodesWhenNotRendered = false;") !=
          std::string::npos);
    CHECK(living_apply_body.find(
        "Candidate.Mesh.bUpdateSkelWhenNotRendered = false;") !=
          std::string::npos);
    const auto living_offscreen_reduce_guard = living_apply_body.find(
        "if (!FixedMinimumLivingOffscreenAnimReduced[EntryIndex]");
    const auto living_tick_original_rebase = living_apply_body.find(
        "FixedMinimumLivingOriginalTickAnimOffscreen[EntryIndex] =\n"
        "                Candidate.Mesh.bTickAnimNodesWhenNotRendered",
        living_offscreen_reduce_guard);
    const auto living_skeleton_original_rebase = living_apply_body.find(
        "FixedMinimumLivingOriginalUpdateSkelOffscreen[EntryIndex] =\n"
        "                Candidate.Mesh.bUpdateSkelWhenNotRendered",
        living_offscreen_reduce_guard);
    const auto living_tick_reduction = living_apply_body.find(
        "Candidate.Mesh.bTickAnimNodesWhenNotRendered = false;",
        living_offscreen_reduce_guard);
    CHECK(living_offscreen_reduce_guard != std::string::npos);
    CHECK(living_tick_original_rebase != std::string::npos);
    CHECK(living_skeleton_original_rebase != std::string::npos);
    CHECK(living_tick_reduction != std::string::npos);
    CHECK(living_offscreen_reduce_guard < living_tick_original_rebase);
    CHECK(living_tick_original_rebase < living_skeleton_original_rebase);
    CHECK(living_skeleton_original_rebase < living_tick_reduction);
    CHECK(telemetry_source.find(
        "FixedMinimumLivingOriginalTickAnimOffscreen") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "FixedMinimumLivingOriginalUpdateSkelOffscreen") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "living_updates_skeleton_offscreen=") != std::string::npos);
    CHECK(living_offscreen_policy_body.find(
        "Candidate.Mesh.LastRenderTime > WorldInfo.TimeSeconds - 0.3") !=
          std::string::npos);
    CHECK(living_offscreen_policy_body.find(
        "Candidate.SpecialMove != SM_None") != std::string::npos);
    CHECK(living_offscreen_policy_body.find("Candidate.IsABoss()") !=
          std::string::npos);
    CHECK(living_offscreen_policy_body.find(
        "Candidate.Mesh.bUpdateKinematicBonesFromAnimation") !=
          std::string::npos);
    CHECK(living_offscreen_policy_body.find(
        "Candidate.Mesh.RootMotionMode != RMM_Ignore") !=
          std::string::npos);
    CHECK(living_apply_body.find(
        "KF2OPT_LIVING_OFFSCREEN_ANIM state=reduced") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_LIVING_OFFSCREEN_ANIM state=restored") !=
          std::string::npos);
    CHECK(living_apply_body.find("EnemyPressureLevel") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.MinLodModel != TargetMinLod") != std::string::npos);
    CHECK(telemetry_source.substr(
        lod_selector, lod_apply - lod_selector).find(
            "DistanceSquared < 640000.0") == std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.bSkipTickAnimNodes =") == std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.bSkipGetBoneAtoms =") == std::string::npos);
    const auto adaptive_disable = telemetry_source.find(
        "function bool SetAdaptiveRuntimeEnabled(bool bEnabled)");
    const auto adaptive_resource = telemetry_source.find(
        "function bool ApplyAdaptiveResourceControl(", adaptive_disable);
    const auto fixed_visual_adaptive_disable_body = telemetry_source.substr(
        adaptive_disable, adaptive_resource - adaptive_disable);
    CHECK(fixed_visual_adaptive_disable_body.find(
        "RestoreAllAdaptiveCorpseLods();") ==
          std::string::npos);
    CHECK(fixed_visual_adaptive_disable_body.find(
        "RestoreAllAdaptiveLivingVisuals();") ==
          std::string::npos);
    CHECK(fixed_visual_adaptive_disable_body.find(
        "ClearTimer(nameof(FixedMinimumVisualControl)") ==
          std::string::npos);
    const auto fixed_schedule = telemetry_source.find(
        "function ScheduleFixedMinimumVisualControlTimer(");
    const auto fixed_schedule_end = telemetry_source.find(
        "function bool SetAdaptiveRuntimeEnabled(", fixed_schedule);
    CHECK(fixed_schedule != std::string::npos);
    CHECK(fixed_schedule_end != std::string::npos);
    CHECK(telemetry_source.substr(
        fixed_schedule, fixed_schedule_end - fixed_schedule).find(
            "bAdaptiveRuntimeEnabled") == std::string::npos);
    CHECK(telemetry_source.find(
        "function PruneFixedMinimumLivingVisualEntries()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "PruneFixedMinimumLivingVisualEntries();") != std::string::npos);
    CHECK(telemetry_source.find(
        "readback=verified", lod_apply) != std::string::npos);
    const auto lod_prune = telemetry_source.find(
        "function PruneFixedMinimumCorpseLodEntries()");
    const auto lod_native_reset = telemetry_source.find(
        "KF2OPT_CORPSE_LOD state=native_reset", lod_prune);
    const auto lod_unowned_state = telemetry_source.find(
        "FixedMinimumCorpseLodAppliedMinModels[Index] = -1", lod_prune);
    const auto lod_reapply_reason = telemetry_source.find(
        "ApplyReason = \"native_state_changed\"", lod_apply);
    const auto lod_reason_receipt = telemetry_source.find(
        "state=fixed_minimum reason=\"$ApplyReason", lod_apply);
    CHECK(lod_prune != std::string::npos);
    CHECK(lod_native_reset != std::string::npos);
    CHECK(lod_unowned_state != std::string::npos);
    CHECK(lod_reapply_reason != std::string::npos);
    CHECK(lod_reason_receipt != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_LOD state=restored reason=near_player") ==
          std::string::npos);

    const auto near_corpse_detail_guard = telemetry_source.find(
        "function bool ShouldPreserveNearCorpseDetail(");
    const auto corpse_animation_refresh = telemetry_source.find(
        "function RefreshSleepingCorpseMinimumAnimationState(");
    const auto corpse_animation_end = telemetry_source.find(
        "function int FindAdaptiveBaselineSettleEntry(",
        corpse_animation_refresh);
    CHECK(near_corpse_detail_guard != std::string::npos);
    CHECK(corpse_animation_refresh != std::string::npos);
    CHECK(corpse_animation_end != std::string::npos);
    const auto corpse_animation_body = telemetry_source.substr(
        corpse_animation_refresh,
        corpse_animation_end - corpse_animation_refresh);
    const auto near_corpse_detail_body = telemetry_source.substr(
        near_corpse_detail_guard,
        corpse_animation_refresh - near_corpse_detail_guard);
    CHECK(near_corpse_detail_body.find(
        "LocalPC = GetALocalPlayerController()") != std::string::npos);
    CHECK(near_corpse_detail_body.find(
        "Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.3") !=
          std::string::npos);
    CHECK(near_corpse_detail_body.find(
        "DistanceSquared < 640000.0") != std::string::npos);
    CHECK(corpse_animation_body.find(
        "ShouldPreserveNearCorpseDetail(Candidate)") == std::string::npos);
    CHECK(corpse_animation_body.find(
        "Candidate.Mesh.bNoSkeletonUpdate = true") != std::string::npos);
    CHECK(telemetry_source.find(
        "ScheduleFixedMinimumVisualControlTimer(") != std::string::npos);

    const auto destroyed = telemetry_source.find("event Destroyed()");
    const auto destroyed_end = telemetry_source.find(
        "Super.Destroyed();", destroyed);
    CHECK(destroyed != std::string::npos);
    CHECK(destroyed_end != std::string::npos);
    const auto quiesce = telemetry_source.find(
        "function QuiesceForWorldTeardown()");
    CHECK(quiesce != std::string::npos);
    CHECK(telemetry_source.find("if (bAdaptiveRuntimeQuiesced)", quiesce) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "state=stopped reason=world_teardown", quiesce) != std::string::npos);
    CHECK(telemetry_source.find("QuiesceForWorldTeardown();", destroyed) <
          destroyed_end);
    const auto quiesce_end = telemetry_source.find(
        "event Destroyed()", quiesce);
    CHECK(quiesce_end != std::string::npos);
    const auto quiesce_body = telemetry_source.substr(
        quiesce, quiesce_end - quiesce);
    const auto destroyed_body = telemetry_source.substr(
        destroyed, destroyed_end - destroyed);
    CHECK(quiesce_body.find("RemoveAdaptiveDebugMarkerPostRender();") !=
          std::string::npos);
    CHECK(destroyed_body.find("RestoreAdaptiveGraphics()") ==
          std::string::npos);
    CHECK(destroyed_body.find("RestoreAllAdaptiveCorpseLods()") ==
          std::string::npos);
    CHECK(destroyed_body.find("RestoreAllAdaptiveLivingVisuals()") ==
          std::string::npos);
    CHECK(quiesce_body.find("FixedMinimumLivingVisualZeds.Length = 0") !=
          std::string::npos);
    CHECK(quiesce_body.find("FixedMinimumCorpseLodCorpses.Length = 0") !=
          std::string::npos);
    CHECK(quiesce_body.find("AdaptiveRetiredFrozenCorpses.Length = 0") !=
          std::string::npos);
    CHECK(quiesce_body.find(
        "AdaptiveRetiredDistanceSleptCorpses.Length = 0") !=
          std::string::npos);

    const auto ragdoll_selector = telemetry_source.find(
        "function KFPawn SelectVisibleAwakeMonsterCorpseForSleep(");
    const auto ragdoll_function = telemetry_source.find(
        "function bool SleepOneVisibleMonsterCorpse(");
    const auto ragdoll_ownership_array = telemetry_source.find(
        "var array<string> AdaptiveCorpsePhysicsActionIds");
    const auto ragdoll_ownership_count = telemetry_source.find(
        "var int AdaptiveCorpsePhysicsActionIdCount");
    const auto ragdoll_ownership_capacity = telemetry_source.find(
        "AdaptiveCorpsePhysicsActionIds.Length = 8192");
    const auto ragdoll_ownership_hash = telemetry_source.find(
        "function int GetAdaptiveCorpsePhysicsActionHash(string ActionId)");
    const auto ragdoll_ownership_lookup = telemetry_source.find(
        "function int FindAdaptiveCorpsePhysicsActionId(");
    const auto ragdoll_ownership_filter = telemetry_source.find(
        "FindAdaptiveCorpsePhysicsActionId(\"ragdoll\",", ragdoll_selector);
    const auto ragdoll_ownership_id = telemetry_source.find(
        "GetAdaptiveCorpseActionId(Candidate)) != -1",
        ragdoll_ownership_filter);
    const auto ragdoll_ownership_register = telemetry_source.find(
        "RegisterAdaptiveCorpsePhysicsAction(Candidate, \"ragdoll\")",
        ragdoll_selector);
    const auto ragdoll_awake_filter = telemetry_source.find(
        "!Candidate.Mesh.RigidBodyIsAwake()", ragdoll_selector);
    const auto ragdoll_local_player = telemetry_source.find(
        "LocalPC = GetALocalPlayerController();", ragdoll_selector);
    const auto ragdoll_player_required = telemetry_source.find(
        "if (LocalPC == None || LocalPC.Pawn == None)",
        ragdoll_local_player);
    const auto ragdoll_distance_measurement = telemetry_source.find(
        "DistanceSquared = VSizeSq(", ragdoll_player_required);
    const auto ragdoll_near_safety_gate = telemetry_source.find(
        "DistanceSquared < 640000.0", ragdoll_distance_measurement);
    const auto ragdoll_sleep_call = telemetry_source.find(
        "Candidate.Mesh.PutRigidBodyToSleep()", ragdoll_function);
    const auto ragdoll_settle_guard = telemetry_source.find(
        "IsAdaptiveCorpseSettled(Candidate,", ragdoll_function);
    const auto ragdoll_sleep_readback = telemetry_source.find(
        "if (Candidate.Mesh.RigidBodyIsAwake())", ragdoll_sleep_call);
    const auto ragdoll_sleep_register = telemetry_source.find(
        "RegisterAdaptiveCorpsePhysicsAction(Candidate, \"ragdoll\")",
        ragdoll_sleep_readback);
    const auto ragdoll_counter = telemetry_source.find(
        "++AdaptiveVisibleRagdollSleeps", ragdoll_function);
    const auto ragdoll_receipt = telemetry_source.find(
        "KF2OPT_CORPSE_RAGDOLL state=sleep", ragdoll_function);
    const auto ragdoll_near_rejection = telemetry_source.find(
        "KF2OPT_CORPSE_RAGDOLL state=rejected_near", ragdoll_selector);
    const auto ragdoll_ownership_receipt = telemetry_source.find(
        "ownership_tracked=", ragdoll_receipt);
    CHECK(ragdoll_selector != std::string::npos);
    CHECK(ragdoll_function != std::string::npos);
    CHECK(ragdoll_ownership_array != std::string::npos);
    CHECK(ragdoll_ownership_count != std::string::npos);
    CHECK(ragdoll_ownership_capacity != std::string::npos);
    CHECK(ragdoll_ownership_hash != std::string::npos);
    CHECK(ragdoll_ownership_lookup != std::string::npos);
    CHECK(ragdoll_ownership_filter != std::string::npos);
    CHECK(ragdoll_ownership_id != std::string::npos);
    CHECK(ragdoll_ownership_register != std::string::npos);
    CHECK(telemetry_source.find("EligibleAfterRealTime") ==
          std::string::npos);
    CHECK(telemetry_source.find("WorldInfo.RealTimeSeconds + 3.0") ==
          std::string::npos);
    CHECK(telemetry_source.find("struct AdaptiveCorpseRagdollSleepEntry") ==
          std::string::npos);
    CHECK(ragdoll_awake_filter != std::string::npos);
    CHECK(ragdoll_local_player != std::string::npos);
    CHECK(ragdoll_player_required != std::string::npos);
    CHECK(ragdoll_distance_measurement != std::string::npos);
    CHECK(ragdoll_near_safety_gate != std::string::npos);
    CHECK(ragdoll_settle_guard != std::string::npos);
    CHECK(ragdoll_sleep_call != std::string::npos);
    CHECK(ragdoll_sleep_readback != std::string::npos);
    CHECK(ragdoll_sleep_register != std::string::npos);
    CHECK(ragdoll_counter != std::string::npos);
    CHECK(ragdoll_receipt != std::string::npos);
    CHECK(ragdoll_near_rejection != std::string::npos);
    CHECK(ragdoll_ownership_receipt != std::string::npos);
    CHECK(ragdoll_ownership_filter < ragdoll_awake_filter);
    CHECK(ragdoll_local_player < ragdoll_function);
    CHECK(ragdoll_player_required < ragdoll_function);
    CHECK(ragdoll_distance_measurement < ragdoll_function);
    CHECK(ragdoll_near_safety_gate < ragdoll_function);
    CHECK(ragdoll_settle_guard < ragdoll_sleep_call);
    CHECK(ragdoll_sleep_call < ragdoll_sleep_readback);
    CHECK(ragdoll_sleep_readback < ragdoll_sleep_register);
    CHECK(ragdoll_sleep_register < ragdoll_counter);
    CHECK(ragdoll_counter < ragdoll_receipt);
    CHECK(telemetry_source.find("minimum_distance_units=800",
                                ragdoll_near_rejection) != std::string::npos);
    CHECK(telemetry_source.find("minimum_distance_m=8.0",
                                ragdoll_near_rejection) != std::string::npos);
    CHECK(telemetry_source.find("eligible=0 reason=near_player",
                                ragdoll_near_rejection) != std::string::npos);
    CHECK(telemetry_source.find("scene_level=", ragdoll_receipt) !=
          std::string::npos);
    CHECK(telemetry_source.find("enemy_level=", ragdoll_receipt) !=
          std::string::npos);
    CHECK(telemetry_source.find("frame_level=", ragdoll_receipt) !=
          std::string::npos);
    CHECK(telemetry_source.find("minimum_distance_units=800",
                                ragdoll_receipt) != std::string::npos);
    CHECK(telemetry_source.find("minimum_distance_m=8.0",
                                ragdoll_receipt) != std::string::npos);
    CHECK(telemetry_source.find("zed_time=0 eligible=1",
                                ragdoll_receipt) != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumAge = SeverePressure ? 0.75 : 1.5", ragdoll_selector) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "VisibleAwake >= 3", stagger_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "VisibleAwake >= 6", stagger_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "DesiredVisibleAwake = 2", stagger_start) != std::string::npos);
    CHECK(telemetry_source.find("Zed.SetHidden(") == std::string::npos);
    CHECK(telemetry_source.find("Zed.bHidden") == std::string::npos);

    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);
    const auto game_ini = root / L"KFGame.ini";
    const auto engine_ini = root / L"KFEngine.ini";
    constexpr std::string_view original =
        "\xEF\xBB\xBF[Other]\r\nKeep=1\r\n\r\n"
        "[KFGameContent.KFGameInfo_Survival]\r\n"
        "bLogAICount=False ; temporary lab switch\r\n"
        "MaxPlayers=6\r\n"
        "\r\n[KFGame.KFAISpawnManager_Short]\r\n"
        "bLogWaveSpawnTiming=True\r\n"
        "\r\n[KFGame.KFAISpawnManager_Normal]\r\n"
        "bLogWaveSpawnTiming=False\r\n"
        "\r\n[KFGame.KFAISpawnManager_Long]\r\n"
        "MaxZeds=42\r\n";
    write_bytes(game_ini, original);
    constexpr std::string_view original_engine =
        "[URL]\r\n"
        "LocalOptions=\r\n"
        "\r\n[Core.System]\r\n"
        "ScriptPaths=..\\..\\KFGame\\Script\r\n"
        "\r\n[Engine.Engine]\r\n"
        "GameViewportClientClassName=KFGame.KFGameViewportClient\r\n"
        "\r\n[Engine.GameEngine]\r\n"
        "ServerActors=IpDrv.WebServer\r\n"
        "bUseTextureStreaming=True\r\n";
    write_bytes(engine_ini, original_engine);

    const auto enabled = kf2::game::enable_offline_gameplay_logging(root);
    CHECK(enabled.has_value());
    CHECK(enabled.value());
    const auto changed = read_bytes(game_ini);
    CHECK(changed.starts_with("\xEF\xBB\xBF"));
    CHECK(changed.find("bLogAICount=True ; temporary lab switch\r\n") !=
          std::string::npos);
    CHECK(changed.find("MaxPlayers=6\r\n") != std::string::npos);
    const auto changed_engine = read_bytes(engine_ini);
    const auto published_runtime_path =
        (root.parent_path() / L"Published" / L"BrewedPC")
            .lexically_normal().string();
    CHECK(changed_engine.find("ServerActors=IpDrv.WebServer\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find(
        "Package=KF2OptimizerTelemetry\r\n") == std::string::npos);
    CHECK(changed_engine.find(
        "ScriptPaths=..\\..\\KFGame\\Script\r\n") != std::string::npos);
    CHECK(changed_engine.find(
        "Paths=" + published_runtime_path + "\r\n") != std::string::npos);
    CHECK(changed_engine.find(
        "GameViewportClientClassName=KF2OptimizerTelemetry."
        "KF2OptimizerGraphicsViewport\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find(
        "LocalOptions=?Mutator=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryMutator\r\n") != std::string::npos);
    CHECK(changed_engine.find(
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("bAdaptiveCorpseStagger=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("bAdaptiveCorpseDebugMarkers=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("bAdaptiveZedDebugMarkers=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("bDetailedRuntimeDiagnostics=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("AdaptiveCorpseMaximum=0\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("AdaptiveTargetFPS=0\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("AdaptiveQualityChangeBudget=1\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("OriginalLogAICount=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find(
              "OriginalLogWaveSpawnTimingShort=True\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find(
              "OriginalLogWaveSpawnTimingNormal=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find(
              "OriginalLogWaveSpawnTimingLong=Missing\r\n") !=
          std::string::npos);
    std::size_t wave_logging_count = 0;
    for (std::size_t position = 0;
         (position = changed.find("bLogWaveSpawnTiming=True", position)) !=
         std::string::npos;
         position += std::string_view{"bLogWaveSpawnTiming=True"}.size()) {
        ++wave_logging_count;
    }
    CHECK(wave_logging_count == 3);

    const auto already_enabled =
        kf2::game::enable_offline_gameplay_logging(root);
    CHECK(already_enabled.has_value());
    CHECK(!already_enabled.value());
    CHECK(read_bytes(game_ini) == changed);
    CHECK(read_bytes(engine_ini) == changed_engine);

    const auto adaptive_enabled =
        kf2::game::enable_offline_gameplay_logging(
            root, true, 350, 137, true, 2, control_token, true, true, true);
    CHECK(adaptive_enabled.has_value());
    CHECK(adaptive_enabled.value());
    const auto adaptive_engine = read_bytes(engine_ini);
    CHECK(adaptive_engine.find("bAdaptiveCorpseStagger=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("bAdaptiveRuntimeEnabled=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("bAdaptiveCorpseDebugMarkers=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("bAdaptiveZedDebugMarkers=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("bDetailedRuntimeDiagnostics=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("AdaptiveCorpseMaximum=350\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("AdaptiveTargetFPS=137\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("AdaptiveQualityChangeBudget=2\r\n") !=
           std::string::npos);
    CHECK(adaptive_engine.find(
        "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n") !=
          std::string::npos);
    const auto observed_policy =
        kf2::game::read_offline_adaptive_session_policy(root);
    CHECK(observed_policy.has_value());
    CHECK(observed_policy.value().has_value());
    CHECK(observed_policy.value()->target_fps == 137);
    CHECK(observed_policy.value()->corpse_maximum == 350);
    CHECK(observed_policy.value()->quality_change_budget == 2);
    CHECK(observed_policy.value()->runtime_enabled);
    const auto adaptive_unchanged =
        kf2::game::enable_offline_gameplay_logging(
            root, true, 350, 137, true, 2, control_token, true, true, true);
    CHECK(adaptive_unchanged.has_value());
    CHECK(!adaptive_unchanged.value());
    const auto physics_control =
        kf2::game::enable_offline_gameplay_logging(
            root, false, 350, 137, true, 2, control_token, true, true, true);
    CHECK(physics_control.has_value());
    CHECK(physics_control.value());
    const auto physics_control_engine = read_bytes(engine_ini);
    CHECK(physics_control_engine.find(
              "bAdaptiveCorpseStagger=False\r\n") != std::string::npos);
    CHECK(physics_control_engine.find(
              "AdaptiveCorpseMaximum=350\r\n") != std::string::npos);
    CHECK(physics_control_engine.find(
              "AdaptiveTargetFPS=137\r\n") != std::string::npos);
    CHECK(physics_control_engine.find(
              "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n") !=
          std::string::npos);
    const auto adaptive_initially_off =
        kf2::game::enable_offline_gameplay_logging(
            root, true, 350, 137, true, 2, control_token, true, false);
    CHECK(adaptive_initially_off.has_value());
    CHECK(adaptive_initially_off.value());
    CHECK(read_bytes(engine_ini).find(
              "bAdaptiveRuntimeEnabled=False\r\n") != std::string::npos);
    const auto disabled_policy =
        kf2::game::read_offline_adaptive_session_policy(root);
    CHECK(disabled_policy.has_value());
    CHECK(disabled_policy.value().has_value());
    CHECK(!disabled_policy.value()->runtime_enabled);

    CHECK(!kf2::game::cleanup_stale_offline_gameplay_configuration(
        root, true).has_value());
    const auto stale_cleaned =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(stale_cleaned.has_value());
    CHECK(stale_cleaned.value());
    const auto cleaned_engine = read_bytes(engine_ini);
    CHECK(cleaned_engine.find(
        "GameViewportClientClassName=KFGame.KFGameViewportClient") !=
          std::string::npos);
    CHECK(cleaned_engine.find(
        "ScriptPaths=..\\..\\KFGame\\Script") != std::string::npos);
    CHECK(cleaned_engine.find("Paths=" + published_runtime_path) ==
          std::string::npos);
    CHECK(cleaned_engine.find("ServerActors=IpDrv.WebServer") !=
          std::string::npos);
    CHECK(cleaned_engine.find("KF2OptimizerTelemetryBootstrap") ==
          std::string::npos);
    CHECK(cleaned_engine.find("Package=KF2OptimizerTelemetry") ==
          std::string::npos);
    CHECK(cleaned_engine.find("KF2OptimizerTelemetryMutator") ==
          std::string::npos);
    CHECK(cleaned_engine.find("[KF2OptimizerTelemetry.") ==
          std::string::npos);
    CHECK(cleaned_engine.find("AdaptiveControlToken=") == std::string::npos);
    CHECK(read_bytes(game_ini) == original);
    const auto already_clean =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(already_clean.has_value());
    CHECK(!already_clean.value());

    constexpr std::string_view unowned_game =
        "[KFGameContent.KFGameInfo_Survival]\r\n"
        "bLogAICount=True\r\n";
    constexpr std::string_view unowned_engine =
        "[Engine.Engine]\r\n"
        "GameViewportClientClassName=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryViewport\r\n"
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n"
        "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n";
    write_bytes(game_ini, unowned_game);
    write_bytes(engine_ini, unowned_engine);
    CHECK(!kf2::game::cleanup_stale_offline_gameplay_configuration(
        root, false).has_value());
    CHECK(read_bytes(game_ini) == unowned_game);
    CHECK(read_bytes(engine_ini) == unowned_engine);
    write_bytes(game_ini, original);

    const std::string existing_local_options_engine =
        "[URL]\r\n"
        "LocalOptions=?Foo=Bar\r\n"
        "\r\n[Core.System]\r\n"
        "ScriptPaths=..\\..\\KFGame\\Script\r\n"
        "\r\n[Engine.Engine]\r\n"
        "GameViewportClientClassName=KFGame.KFGameViewportClient\r\n";
    write_bytes(engine_ini, existing_local_options_engine);
    const auto existing_options_enabled =
        kf2::game::enable_offline_gameplay_logging(root);
    CHECK(existing_options_enabled.has_value());
    CHECK(existing_options_enabled.value());
    CHECK(read_bytes(engine_ini).find(
        "LocalOptions=?Foo=Bar?Mutator=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryMutator\r\n") != std::string::npos);
    const auto existing_options_cleaned =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(existing_options_cleaned.has_value());
    CHECK(existing_options_cleaned.value());
    CHECK(read_bytes(engine_ini).find(
        "LocalOptions=?Foo=Bar\r\n") != std::string::npos);

    const std::string foreign_mutator_engine =
        "[URL]\r\n"
        "LocalOptions=?Mutator=Example.Custom\r\n"
        "\r\n[Core.System]\r\n"
        "ScriptPaths=..\\..\\KFGame\\Script\r\n"
        "\r\n[Engine.Engine]\r\n"
        "GameViewportClientClassName=KFGame.KFGameViewportClient\r\n";
    write_bytes(engine_ini, foreign_mutator_engine);
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());
    CHECK(read_bytes(engine_ini) == foreign_mutator_engine);

    const std::string legacy_absolute_path_engine =
        "[Core.System]\r\n"
        "ScriptPaths=" +
        (root.parent_path() / L"Published" / L"BrewedPC")
            .lexically_normal().string() +
        "\r\n[Engine.Engine]\r\n"
        "GameViewportClientClassName=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryViewport\r\n";
    write_bytes(engine_ini, legacy_absolute_path_engine);
    const auto legacy_path_cleaned =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(!legacy_path_cleaned.has_value());
    CHECK(read_bytes(engine_ini) == legacy_absolute_path_engine);

    const std::string foreign_viewport_engine =
        "[Engine.Engine]\r\n"
        "GameViewportClientClassName=Example.CustomViewport\r\n"
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n"
        "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n";
    write_bytes(engine_ini, foreign_viewport_engine);
    const auto foreign_viewport_cleaned =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(!foreign_viewport_cleaned.has_value());
    CHECK(read_bytes(engine_ini) == foreign_viewport_engine);

    const std::string ambiguous_engine =
        "[Engine.Engine]\r\n"
        "GameViewportClientClassName=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryViewport\r\n"
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n"
        "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n"
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n"
        "AdaptiveTargetFPS=60\r\n";
    write_bytes(engine_ini, ambiguous_engine);
    CHECK(!kf2::game::read_offline_adaptive_session_policy(root).has_value());
    CHECK(!kf2::game::cleanup_stale_offline_gameplay_configuration(
        root, false).has_value());
    CHECK(read_bytes(engine_ini) == ambiguous_engine);
    write_bytes(engine_ini, adaptive_engine);

    write_bytes(engine_ini,
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\n"
        "bAdaptiveCorpseStagger=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 137, false, 1, control_token).has_value());
    write_bytes(engine_ini,
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\n"
        "bAdaptiveCorpseDebugMarkers=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 137, true, 1, control_token).has_value());
    write_bytes(engine_ini,
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\n"
        "bAdaptiveZedDebugMarkers=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 137, false, 1, control_token, true).has_value());
    write_bytes(engine_ini,
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\n"
        "bDetailedRuntimeDiagnostics=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 137, false, 1, control_token, false, true, true)
               .has_value());
    write_bytes(engine_ini, adaptive_engine);

    write_bytes(engine_ini,
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\n"
        "AdaptiveTargetFPS=invalid\n");
    CHECK(!kf2::game::read_offline_adaptive_session_policy(root).has_value());
    write_bytes(engine_ini,
        "[Engine.Engine]\n"
        "GameViewportClientClassName=KFGame.KFGameViewportClient\n");
    const auto missing_policy =
        kf2::game::read_offline_adaptive_session_policy(root);
    CHECK(missing_policy.has_value());
    CHECK(!missing_policy.value().has_value());
    write_bytes(engine_ini, adaptive_engine);

    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 3, 60, false, 1, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 2001, 60, false, 1, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 29, false, 1, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 241, false, 1, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 60, false, 0, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 60, false, 6, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(root, false, 350, 0).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(root, false, 0, 60).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, false, 0, 0, true).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, false, 0, 0, false, 1, {}, true).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 60, false, 2, "invalid").has_value());

    write_bytes(engine_ini, original_engine);
    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=False\nbLogAICount=False\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(engine_ini, original_engine);
    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(engine_ini, original_engine);
    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\nMaxPlayers=6\n");
    const auto missing_ai_count =
        kf2::game::enable_offline_gameplay_logging(root);
    CHECK(missing_ai_count.has_value());
    CHECK(missing_ai_count.value());
    CHECK(read_bytes(game_ini).find(
        "[KFGameContent.KFGameInfo_Survival]\n"
        "MaxPlayers=6\n"
        "bLogAICount=True\n") != std::string::npos);

    write_bytes(engine_ini, original_engine);
    write_bytes(game_ini,
        "[Other]\nbLogAICount=False\n");
    const auto unrelated_ai_count =
        kf2::game::enable_offline_gameplay_logging(root);
    CHECK(unrelated_ai_count.has_value());
    CHECK(unrelated_ai_count.value());
    CHECK(read_bytes(game_ini).find(
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=True\n") != std::string::npos);

    write_bytes(engine_ini, original_engine);
    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=False\n"
        "[KFGame.KFAISpawnManager_Short]\n"
        "bLogWaveSpawnTiming=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(engine_ini, original_engine);
    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=False\n"
        "[KFGame.KFAISpawnManager_Short]\n"
        "bLogWaveSpawnTiming=False\n"
        "bLogWaveSpawnTiming=False\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(engine_ini, original_engine);
    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=True\n");
    const auto wave_only = kf2::game::enable_offline_gameplay_logging(root);
    CHECK(wave_only.has_value());
    CHECK(wave_only.value());
    CHECK(read_bytes(game_ini).find(
        "[KFGame.KFAISpawnManager_Short]\n"
        "bLogWaveSpawnTiming=True\n") != std::string::npos);

    write_bytes(engine_ini, original_engine);
    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=False\n");
    const auto hardlink = root / L"KFGame-hardlink.ini";
    CHECK(CreateHardLinkW(hardlink.c_str(), game_ini.c_str(), nullptr) != FALSE);
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());
    fs::remove(hardlink, error);

    fs::remove(game_ini, error);
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(game_ini, original);
    fs::remove(engine_ini, error);
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(game_ini, original);
    write_bytes(engine_ini,
        "[Engine.GameEngine]\nServerActors=IpDrv.WebServer\n"
        "[Engine.GameEngine]\nServerActors=Other.Actor\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());
    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
