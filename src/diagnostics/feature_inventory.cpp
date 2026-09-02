#include "kf2/diagnostics/feature_inventory.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <vector>

namespace kf2::diagnostics {
namespace {

struct AreaContract {
    std::string_view name;
    std::string_view code_path;
    std::string_view data_source;
    std::string_view trust_class;
    std::string_view technical_statement;
    std::string_view expected_benefit;
    std::string_view risks;
    std::string_view mode_support;
    std::string_view reversible_path;
    std::string_view dependencies;
    std::string_view required_tests;
    std::string_view evidence;
};

struct ItemSpec {
    FeatureStatus status;
    std::string_view requirement;
    std::string_view assessment;
};

constexpr AreaContract area01{
    "Start, installation and lifecycle",
    "src/app; src/platform/windows; src/game/game_discovery.cpp",
    "Windows process, filesystem and Steam metadata APIs", "Locally verified",
    "The native x64 portable app runs as standard user, binds KF2 by path and file identity, and recovers bounded local transactions.",
    "Predictable portable startup and recoverable local state.",
    "External execution policy can still block an unsigned portable binary.",
    "Adaptive/Automatic plus an explicit read-only diagnostics mode; package damage is handled per component.",
    "Atomic state/session files, verified transaction recovery and replaceable package files.",
    "Windows 10+, Steam metadata and a supported KF2 installation.",
    "Lifecycle, single-instance, PE, package integrity/update, degraded-package GUI launch and clean-exit tests.",
    "kf2_application_lifecycle_test; kf2_single_instance_test; kf2_pe_contract_test; kf2_package_integrity_test; validate_package_update.ps1; validate_package_safe_mode.ps1; validate_release_gui.ps1"};

constexpr std::array area01_items{
    ItemSpec{FeatureStatus::present, "normal and read-only startup", "Adaptive/Automatic startup and the explicit read-only diagnostics mode are tested; package damage does not create a global UI lock."},
    ItemSpec{FeatureStatus::present, "single-instance protection", "A named mutex and second-launch test enforce one instance."},
    ItemSpec{FeatureStatus::present, "UAC and permissions", "The embedded manifest requests asInvoker and uiAccess=false; no elevation is attempted."},
    ItemSpec{FeatureStatus::present, "path, package, and build validation", "Canonical paths, embedded build identity, PE contract and package validation are active."},
    ItemSpec{FeatureStatus::present, "installation, repair, update, and migration", "The portable package replaces only manifest-owned program files, preserves settings, logs, backups and profiles byte-for-byte, and verifies fifteen managed payload hashes. A missing or damaged component is reported for repair without disabling unrelated controls. Auto Repair downloads only the exact installed GitHub release for companion-file recovery. The consent-only updater separately checks newer published versions, verifies repository, asset name, size, SHA-256, build and package identity, then uses a temporary helper for backup, atomic replacement, restart acknowledgement, rollback and cleanup. A verified local package remains the offline repair fallback."},
    ItemSpec{FeatureStatus::present, "launching KF2", "Only the verified Steam/KF2 executable is launched."},
    ItemSpec{FeatureStatus::present, "shutdown, removal, and restoration", "Clean app stop and verified restore exist; portable removal needs no uninstaller, and the app deliberately never force-stops KF2."},
    ItemSpec{FeatureStatus::present, "crash, cancellation, restart, and recovery paths", "Unclean sessions and interrupted config/backup/FleX operations recover; privacy-bounded local crash records identify the failing build and exception without dumps or user content."},
    ItemSpec{FeatureStatus::present, "Steam, KF2, and runtime identity", "Steam libraries, KFGame path/process identity and pinned FleX identity are verified."},
};

constexpr AreaContract area02{
    "Adaptive/Automatic", "src/optimizer; src/config/settings.cpp; src/app/application.cpp",
    "Fresh PresentMon plus CPU, GPU, VRAM and RAM evidence", "B measured",
    "Adaptive uses validated samples, dwell, hysteresis, quality bounds and protected launch-time profile application.",
    "Automatic operation with explicit bounded safety locks.",
    "Telemetry loss must retain the last bounded safe profile instead of guessing.",
    "Adaptive automation respects every explicit bounded safety lock.",
    "Automatic launch application requires a verified session snapshot, backup, readback and safe restore.",
    "PresentMon, system telemetry, settings and verified KF2 catalog.",
    "Adaptive migration, freshness, bounds, lock priority, cooldown, restart and fallback tests.",
    "kf2_adaptive_profile_test; kf2_adaptive_governor_test; kf2_optimizer_engine_test; kf2_settings_test; lifecycle test"};

constexpr std::array area02_items{
    ItemSpec{FeatureStatus::present, "unambiguous Adaptive control", "Adaptive is the only visible and actionable optimizer mode."},
    ItemSpec{FeatureStatus::present, "persistent mode", "Legacy modes migrate to the canonical Adaptive settings document."},
    ItemSpec{FeatureStatus::present, "atomic migration", "A normalized Adaptive settings document is atomically replaced."},
    ItemSpec{FeatureStatus::present, "preview and effective values", "Preview rows expose before, after, source and reason before mutation."},
    ItemSpec{FeatureStatus::present, "safe bounds", "Target FPS, profiles, catalog values and FleX levels are bounded."},
    ItemSpec{FeatureStatus::present, "hardware-, FPS-, and frame-time-driven adaptation", "Fresh identity-bound metrics feed recommendations with confidence and reasons."},
    ItemSpec{FeatureStatus::present, "future use of verified gameplay data", "Fresh official protected-provider living-enemy, corpse and particle-component counts feed the Adaptive evidence model; each unavailable capability remains explicit and can never silently lower quality."},
    ItemSpec{FeatureStatus::present, "user limits without silent side paths", "Explicit safety locks have strict priority; automatic config writes occur only in the protected pre-launch transaction."},
    ItemSpec{FeatureStatus::present, "telemetry loss, restart, passthrough, and restore", "Stale data disables advice; state restarts safely and mutations remain reversible."},
};

constexpr AreaContract area03{
    "Configuration and tweaks", "src/config; src/backup", "Three verified KF2 INIs and a strict 213-setting catalog",
    "A shipped config/B tests", "Only allowlisted keys and ranges may enter preview, import, apply, backup or restore.",
    "Reversible tuning without corrupting unrelated settings.",
    "KF2 updates may change keys; missing, unknown or foreign data fails closed.",
    "Adaptive proposals and explicit validation values.",
    "CAS apply, content-addressed backup, journal, rollback, session snapshot and full restore.",
    "KFEngine.ini, KFGame.ini and KFSystemSettings.ini.",
    "Catalog, preview/import, disk-space, stale-data, path, hardlink, apply and recovery tests.",
    "kf2_catalog_test; kf2_setting_catalog_test; kf2_config_apply_test; kf2_backup_restore_test; kf2_session_config_guard_test"};

constexpr std::array area03_items{
    ItemSpec{FeatureStatus::present, "corpse limit and MaxDeadBodies", "The key is typed, bounded, previewed and restorable."},
    ItemSpec{FeatureStatus::partial, "graphics, physics, gore, particle, shadow, decal, and view-distance values", "Verified cosmetic/render keys are covered; unproven gameplay physics keys are excluded."},
    ItemSpec{FeatureStatus::present, "safe preview", "No mutation occurs until an explicit preview has validated all rows."},
    ItemSpec{FeatureStatus::present, "backup before change", "Apply requires a verified content-addressed backup first."},
    ItemSpec{FeatureStatus::present, "atomic application", "Compare-and-swap preconditions and atomic replacements prevent partial silent writes."},
    ItemSpec{FeatureStatus::present, "range and type validation", "Every setting has an allowlisted file, section, key, type and range."},
    ItemSpec{FeatureStatus::present, "import, export, and version compatibility", "The local preview format is versioned and unknown content is rejected."},
    ItemSpec{FeatureStatus::present, "complete restore", "Verified backup and session restoration cover all protected INI bytes."},
};

constexpr AreaContract area04{
    "Hardware and system telemetry", "src/telemetry; src/platform/windows/presentmon_session.cpp",
    "Windows APIs, local NVIDIA NVAPI/NVML, PDH, DXGI and PresentMon ETW", "A APIs/B measured",
    "Samples carry PID/start identity, adapter LUID, freshness, quality, loss and discontinuity state.",
    "Correct bottleneck evidence and readable overlay metrics.",
    "Driver counters may be unavailable; unavailable values must remain absent.",
    "Read-only evidence in every mode.", "No hardware mutation; detach and reset on process identity change.",
    "Windows ETW, PDH, DXGI, process/system APIs and the installed NVIDIA driver API.",
    "DXGI present, CPU/RAM, GPU/LUID, freshness/loss, enumeration and overhead tests.",
    "kf2_presentmon_dxgi_present_test; kf2_system_metrics_test; kf2_gpu_metrics_test; validate_telemetry_overlay.ps1"};

constexpr std::array area04_items{
    ItemSpec{FeatureStatus::present, "CPU, cores, CPU groups, and utilization", "Logical CPUs, groups and process CPU load are measured."},
    ItemSpec{FeatureStatus::partial, "GPU, adapter/LUID, multi-GPU, and drivers", "All physical adapters and identities are enumerated; a second real adapter is not target-tested."},
    ItemSpec{FeatureStatus::present, "GPU utilization and VRAM", "NVIDIA NVAPI dynamic-Pstate utilization is preferred for MSI Afterburner metric parity; local NVML and adapter-wide PDH are layered fallbacks, while process VRAM stays PID/LUID-bound."},
    ItemSpec{FeatureStatus::present, "process and system RAM", "Process working set and system memory pressure are measured."},
    ItemSpec{FeatureStatus::present, "FPS, frame time, percentiles, and stutter", "PresentMon-derived live, average, 1% low, p95/p99 and stutter data are bounded and fresh."},
    ItemSpec{FeatureStatus::present, "ETW, PresentMon, PDH, NVAPI/NVML, and alternatives", "ETW/PresentMon/PDH/DXGI are integrated. Installed NVIDIA NVAPI is loaded locally for Afterburner-compatible utilization, NVML is the secondary driver fallback, and neither requires starting another program."},
    ItemSpec{FeatureStatus::present, "freshness, measurement quality, loss, and contradictions", "Loss, discontinuity, stale samples and unavailable counters are explicit."},
    ItemSpec{FeatureStatus::partial, "measurement overhead and long-term stability", "Sampling is bounded and focused tests exist; a long physical-system soak remains external evidence."},
};

constexpr AreaContract area05{
    "Gameplay telemetry", "src/game/game_log_session.cpp; src/game/gameplay_log_lab.cpp; src/game/offline_telemetry_lab.cpp; assets/offline_telemetry; src/flex/flex_observation.cpp",
    "Bounded process-bound KF2 Launch.log, protected offline AI/wave logs, a pinned SDK-compiled Published mutator that inserts an interaction into KF2's native viewport, a standalone read-only probe and a separately gated corpse-cleanup actuator, plus identity-bound FleX aggregate counters", "A official SDK source/B verified local",
    "Only explicit log/session facts, fresh standalone SDK telemetry and non-semantic FleX aggregates are exposed; unavailable gameplay meaning is never inferred. The optional actuator uses only KF2's confirmed-corpse pool.",
    "Useful session context without touching gameplay.",
    "The probe reports aggregate states, not per-actor identifiers or exact decal creation events; FleX counters remain non-semantic.",
    "Telemetry remains observation-only; the separately reported protected corpse capability is explicit, bounded and never a guessed input.",
    "Parser reset on file/session/process change; temporary logs, LocalOptions bootstrap and the Published telemetry package are protected by whole-INI snapshot, pinned hash, marker and exact cleanup after KF2 exits. KF2's native viewport remains unchanged. The same prepared session is used when KF2 starts from the optimizer, Steam or a shortcut.",
    "Official KF2 SDK, KF2 Launch.log, protected KFGame/KFEngine session switches, PresentMon and known FleX ABI.",
    "Bounded parser, truncation, rotation, identity, freshness, negative inference and real gameplay tests.",
    "kf2_game_log_session_test; kf2_gameplay_log_lab_test; kf2_offline_telemetry_lab_test; official KFEditor 0-error compile with expected log-expression diagnostics; target gameplay evidence"};

constexpr std::array area05_items{
    ItemSpec{FeatureStatus::present, "living enemies", "For app-started NM_Standalone sessions, a one-second official-SDK AllPawns sample counts only IsAliveAndWell KFPawn_Monster actors; KF2's AIAliveCount remains an independent exact log snapshot."},
    ItemSpec{FeatureStatus::present, "active and inactive corpses", "The official KFGoreManager CorpsePool is sampled read-only and each non-null corpse is classified as awake rigid body, sleeping rigid body or other, with invariant validation."},
    ItemSpec{FeatureStatus::present, "ragdolls and detached limbs", "Official read-only snapshots expose awake/sleeping rigid bodies, bNoSkeletonUpdate final pose, bHasBrokenConstraints corpses, exact HitZones.bPlayedInjury detached-limb totals and ragdoll-insomnia warning level. When its protected capability is available, sustained target-relative frame pressure first sleeps only old slow distant confirmed monster corpses and wakes tracked bodies again on proximity."},
    ItemSpec{FeatureStatus::present, "FleX, fluid, and non-fluid particles", "Each active official particle component is classified from its real ParticleEmitter FlexContainerTemplate as FleX fluid, FleX non-fluid, mixed, non-FleX or explicitly unclassified; solver capacity remains an independent aggregate."},
    ItemSpec{FeatureStatus::partial, "gore, gib, and decal events", "Visible KFGiblet count and dismembered-corpse state are authoritative SDK snapshots; exact blood/decal creation events remain unavailable and are not guessed."},
    ItemSpec{FeatureStatus::present, "wave, map, Zed Time, and relevant states", "Map, class, difficulty, length, menu/match state, AIRemaining, Survival wave totals and official KFGameInfo.IsZedTimeActive are available."},
    ItemSpec{FeatureStatus::present, "unambiguous semantics, session, process, timestamp, and freshness", "The actor self-destructs outside NM_Standalone; package and log identity are bound; every sample is monotonic-timestamped, invariant-checked, expires after 15 seconds and clears on network/map/match transitions."},
    ItemSpec{FeatureStatus::present, "verified read-only data sources only", "Production telemetry is read-only and unknown-address memory reads are prohibited. The separately gated offline corpse actuator uses KFGoreManager.RemoveAndDeleteCorpse, PrimitiveComponent sleep/wake, and Actor PHYS_None/PHYS_RigidBody transitions only for confirmed dead pool members."},
};

constexpr AreaContract area06{
    "Enemy rendering and culling", "src/config/kf2_catalog.cpp; assets/offline_telemetry; src/game/game_log_session.cpp",
    "KF2 global INI keys plus official-SDK skeletal-mesh state", "A official SDK/B partial",
    "Global rendering controls are reversible; the offline probe observes actual living-enemy LOD, render recency, material slots and mesh attachments. During fresh visible-enemy pressure it may reversibly raise MinLodModel on living monster meshes without hiding them, while the authenticated graphics actuator retains the verified global skeletal-mesh LOD path.",
    "Potential GPU and draw-call reduction after proof.",
    "Enemy-specific hiding can remove readable threats and violate gameplay neutrality.",
    "Only explicit Performance proposals for verified global keys.", "Preview and full INI restore.",
    "Fresh gameplay A/B evidence for the bounded living-enemy visual path.",
    "Multiple maps/classes, visibility diff and CPU/GPU/draw-call metrics.",
    "Source contracts, exact readback and restore; fresh gameplay remains pending"};

constexpr std::array area06_items{
    ItemSpec{FeatureStatus::partial, "skeletal-mesh LOD", "The authenticated live actuator may temporarily raise the global bias under CPU pressure. Separately, visible-enemy pressure resolves to five stable tiers with a three-percent dead band, asymmetric hold times and a transition cooldown; recently rendered enemies inside 1,200/600 units add one/two extra pressure points. It may raise living-monster MinLodModel from about 540 toward 300 units, consuming every real mesh LOD available while preserving exact readback, near restore and full session restore; corpse MinLodModel remains independently reversible."},
    ItemSpec{FeatureStatus::partial, "material, texture, and shader complexity", "Verified global material/detail/render controls exist and actual living-enemy material-slot totals are observed; shader complexity and per-enemy mutation remain unavailable."},
    ItemSpec{FeatureStatus::partial, "draw distance, frustum culling, and occlusion culling", "KF2/UE3 already culls living-enemy primitives outside the view frustum or behind occluders. Verified global distance scales and authoritative LastRenderTime-derived visible/offscreen aggregates exist; the probe never hides a living enemy or alters native culling."},
    ItemSpec{FeatureStatus::partial, "shadows and additional render passes", "Verified global shadow resolution, filtering, bias, pre-shadow, per-object/grouped and reflection settings are reversible."},
    ItemSpec{FeatureStatus::partial, "cosmetic submeshes and transparency", "Living-enemy skeletal attachment and material-slot totals are now observed read-only; cosmetic identity, transparency cost and safe removal remain unproven."},
    ItemSpec{FeatureStatus::discarded, "visibility behind the camera and outside the image", "The optimizer will not hide enemies without authoritative engine visibility guarantees."},
    ItemSpec{FeatureStatus::partial, "GPU, CPU, draw-call, memory, and streaming impact", "GPU/CPU/VRAM/RAM/frame metrics exist; draw-call and streaming counters do not."},
};

constexpr AreaContract area07{
    "Enemy animation", "assets/offline_telemetry; src/game/game_log_session.cpp", "Official KF2 SkeletalMeshComponent animation-LOD state", "A official SDK/B partial",
    "The offline probe observes animation LOD, required bones, native skip/interpolation, offscreen tick and kinematic-distance state. Fresh visible-enemy pressure may reversibly raise the engine's writable AnimationLODDistanceFactor and AnimationLODFrameRate on sufficiently distant living monsters; KF2 alone owns its const transient skip/cache/interpolation flags.",
    "Possible CPU reduction in large waves.",
    "Attack timing, hit readability, dismemberment and Zed Time can change.",
    "Only KF2's documented distance-based animation-LOD inputs are writable; attack, bone, collision and authority state remain protected.", "Per-actor originals plus applied values are tracked and restored only while ownership still matches.",
    "Fresh reproducible gameplay-neutral proof.",
    "All enemy classes, bosses, Zed Time, attacks, hits and network truth.",
    "Official compiler, source/readback/restore contracts; fresh gameplay remains pending"};

constexpr std::array area07_items{
    ItemSpec{FeatureStatus::partial, "animation update rate by distance", "Fresh visible-enemy pressure resolves to five stable tiers with hysteresis, asymmetric hold times and a transition cooldown. Each tier has one fixed AnimationLODDistanceFactor and AnimationLODFrameRate target, so unchanged effective pressure produces no mesh write or receipt; sufficiently distant actors gain one final rate step. Exact readback, ownership-aware near restore and session restore are required."},
    ItemSpec{FeatureStatus::partial, "animation LOD and bone LOD", "PredictedLODLevel and RequiredBones totals are observed. Enemy pressure may raise only render MinLodModel and KF2's documented animation-LOD inputs; direct RequiredBones mutation remains excluded."},
    ItemSpec{FeatureStatus::partial, "reduced bone evaluation", "bSkipGetBoneAtoms and bSkipTickAnimNodes are const transient engine outputs. The optimizer never assigns them; KF2 may engage them naturally from its animation-LOD calculation."},
    ItemSpec{FeatureStatus::partial, "interpolation for less frequent updates", "bInterpolateBoneAtoms is a const transient engine output. The optimizer supplies only documented animation-LOD inputs and leaves interpolation ownership to KF2."},
    ItemSpec{FeatureStatus::partial, "visibility- and occlusion-based evaluation", "Fresh LastRenderTime-derived visible-enemy pressure drives only bounded distance-based render/animation LOD; enemies are never hidden and native culling remains untouched."},
    ItemSpec{FeatureStatus::partial, "offscreen and behind-camera behavior", "Visible/offscreen totals and bTickAnimNodesWhenNotRendered are observed; no unsafe behind-camera inference or mutation is performed."},
    ItemSpec{FeatureStatus::present, "Zed Time, attacks, hit reactions, stumble, grab, knockdown, and dismemberment", "The official read-only probe reports KFGameInfo Zed Time, every living Zed SpecialMove in exact attack/grapple/stumble/knockdown/hit-reaction/other buckets, plus living/corpse injury and detached-limb totals. No gameplay state is altered."},
    ItemSpec{FeatureStatus::partial, "bosses and special enemies", "Official IsABoss counts and living class diversity are reported, while per-class mutation remains excluded and final class coverage requires gameplay."},
};

constexpr AreaContract area08{
    "Living-enemy physics", "assets/offline_telemetry; no production mutation path", "Official read-only KFPawn/SkeletalMeshComponent state", "A official SDK/safety boundary",
    "The probe observes native kinematic-distance skipping and injury aggregates; the optimizer never freezes, removes or modifies living-enemy collision, navigation, perception, movement, hits or authority.",
    "Preserved gameplay and network correctness.",
    "Any such mutation can alter difficulty, hits or server truth.", "Observation only.", "No write path exists.",
    "Verified future engine separation would be required.",
    "Negative boundary, import/catalog allowlist and gameplay-truth tests.",
    "Unknown settings and live process mutation are rejected"};

constexpr std::array area08_items{
    ItemSpec{FeatureStatus::partial, "cost analysis for animation, mesh, and rendering", "CPU/GPU/frame evidence now accompanies aggregate living-enemy LOD, required-bone, material, attachment and animation-state observations; per-actor timings remain unavailable."},
    ItemSpec{FeatureStatus::partial, "physics LOD only with proven separation from gameplay", "The engine's native bNotUpdatingKinematicDueToDistance state is observed by class/load, but no optimizer override is enabled because a gameplay-neutral write contract is not proven."},
    ItemSpec{FeatureStatus::present, "no disabling, freezing, or removing living enemies", "Enforced permanent safety boundary."},
    ItemSpec{FeatureStatus::present, "no changes to navigation, perception, movement, hits, or authority", "Enforced permanent safety boundary."},
};

constexpr AreaContract area09{
    "Corpses, ragdolls and gibs", "src/config/kf2_catalog.cpp; src/optimizer/optimizer_engine.cpp; assets/offline_telemetry",
    "Verified KF2 INIs plus official KFGoreManager/SkeletalMeshComponent state and bounded offline corpse cleanup/load control", "A official SDK/B partial",
    "Corpse controls are reversible; official telemetry reports pool capacity, awake/sleep/final-pose, visibility, LOD, injury, age and runtime offscreen/lifetime rules. When the protected capability is available, sustained target-relative pressure first sleeps one old slow distant confirmed monster corpse at a time, preferring native offscreen/occluded state and waking tracked bodies nearby; LOD, cleanup and visible-ragdoll stages follow only if needed.",
    "Lower CPU/GPU/memory pressure from dead-body cosmetics.",
    "Arbitrary per-corpse collision mutation remains excluded. Bounded sleep plus a 15-second final PHYS_None pose for confirmed dead monster corpses remain final-gameplay evidence-gated.",
    "Adaptive profiles for cosmetic budgets, distant-corpse physics with proximity wake, reversible distant-corpse LOD, bounded one-at-a-time cleanup and pressure-triggered visible-ragdoll sleep; sensitive global collision switches remain protected from Adaptive writes.", "Automatic protected launch transaction, preview, session snapshot and backup restore.",
    "KF2 INIs; future SDK evidence for advanced states.",
    "High-load deaths, explosives, map/wave changes, visual and restore tests.",
    "kf2_catalog_test; kf2_optimizer_engine_test; copied-config roundtrip; target gameplay evidence"};

constexpr std::array area09_items{
    ItemSpec{FeatureStatus::present, "maximum corpse count", "MaxDeadBodies is typed, bounded and reversible."},
    ItemSpec{FeatureStatus::partial, "maximum active ragdolls", "Official telemetry reports total and visible awake-ragdoll pressure. No fixed corpse-count cap is imposed. A persistent 50-ms cursor inspects one pool entry and performs at most one aging transition per invocation, while distance, LOD and pressure stages remain additional reductions."},
    ItemSpec{FeatureStatus::partial, "sleep and rest state", "The protected corpse capability uses 3/8/15-second aging tiers for confirmed monster corpses. The early tiers use 1,200/1,000-unit distance guards. After exact sleep readback, the final tier moves a corpse once to PHYS_None only when it is at least 800 units away and no longer recently rendered, so KF2 cannot wake it. The persistent cursor revisits deferred corpses. Final age LOD persists without repeat writes; later physical hit/explosion/collision reaction is intentionally lost. Adaptive-off restoration returns one tracked body every 50 ms. The cursor performs no physics batch and pauses during Zed Time. Broader manual mutation remains unavailable."},
    ItemSpec{FeatureStatus::partial, "transition to a final pose", "After the official sleep transition, bNoSkeletonUpdate and bSkipAllUpdateWhenPhysicsAsleep mirror KF2's own Dying.OnSleepRBPhysics optimization; normal gameplay wake events restore skeleton updates."},
    ItemSpec{FeatureStatus::partial, "update rate and distance thresholds", "Actual corpse LOD and official MaxCorpseOffscreenTime/Distance are reported. A pressure-independent 50-ms aging cursor checks one body at a time: 3/8-second tiers use 1,200/1,000-unit sleep and 300-unit LOD guards, while the 15-second final tier waits for at least 800 units of distance and no recent rendering before selecting the final real LOD and moving the corpse to PHYS_None. KF2 exposes no safe writable fractional PhysX rate or dedicated sleep lock for an awake per-corpse rigid body, so the final tier explicitly trades later physical reaction for a stable final pose. The distance actuator additionally sleeps old slow offscreen/occluded bodies beyond 1,200 units at baseline; visible enemy/corpse density or confirmed frame pressure may move entry to 1,000/850 units while continuously rising enemy pressure shortens action intervals to a 50 ms floor. A tracked distance-slept body wakes inside 800 units before final aging freeze. Reversible corpse MinLodModel starts at stage 2 outside 300 units, advances to stages 3/4/5 at 500/800/1,200 units, consumes every real mesh LOD available as enemy pressure rises, and restores inside 250 units. Fresh enemy pressure also drives reversible living-monster MinLodModel plus the native AnimationLODDistanceFactor and AnimationLODFrameRate inputs; KF2 remains authoritative for its const transient bone-skip, interpolation and cache outputs."},
    ItemSpec{FeatureStatus::partial, "simplified collision after confirmed death", "KF2's three shipped INI corpse-collision switches are protected from Adaptive writes; all four official runtime collision decisions, including living-after-sleep, are observed read-only. Adaptive sleep does not override collision channels."},
    ItemSpec{FeatureStatus::partial, "lifetime and distance of far or occluded corpses", "Corpse visible/offscreen totals, maximum observed age and official runtime offscreen time/distance plus cosmetic lifetimes are reported. The pressure-independent aging cursor progressively reduces visible and offscreen old corpses without hiding or deleting them and performs at most one transition per invocation. The additional distance path uses native LastRenderTime to prefer distant offscreen/occluded corpses, and excess confirmed corpses are removed at most one per 450 ms only while fresh sustained target-relative frame-time pressure is present; all actions pause during Zed Time."},
    ItemSpec{FeatureStatus::partial, "gibs, body parts, and memory release", "Visible gib count, dismemberment/injured-zone totals and actual gib lifetime are reported; allocator control is not exposed."},
    ItemSpec{FeatureStatus::present, "map, wave, and session transitions", "Every map creates a fresh offline actor, parser state is replaced on LoadMap, all observations clear on menu/match/network transitions and the exact temporary package/INIs are restored at process end."},
    ItemSpec{FeatureStatus::present, "Adaptive values with complete restore", "Adaptive uses verified cosmetic budgets, reversible corpse LOD, protected staged cleanup and pressure-triggered corpse sleep; sensitive shipped controls remain blocked, and every applied change has full restore."},
};

constexpr AreaContract area10{
    "Gore, decals and effects", "src/config/kf2_catalog.cpp; src/optimizer/optimizer_engine.cpp; assets/offline_telemetry",
    "Verified KF2 INI keys plus official SDK effect actors, decal managers and emitter pools", "A official SDK/B local verified",
    "Gore, blood, wound, pool, splatter, impact, explosion, decal, shadow and secondary-effect controls are typed; runtime categories are sampled read-only.",
    "Reduced overdraw, simulation and memory under explicit quality policy.",
    "Aggressive values visibly reduce cosmetics, so Exact/Invisible never change them.",
    "Performance Adaptive profiles and verified setting catalog.", "Automatic protected launch, preview, backup, session capture and full restore.",
    "KF2 configuration.", "Bounds, profile difference, visual comparison and restore tests.",
    "213-setting catalog tests; optimizer tests; copied-config roundtrip; target gameplay evidence"};

constexpr std::array area10_items{
    ItemSpec{FeatureStatus::present, "blood, wound, floor, and wall decals", "Verified controls plus separate active wound, splatter, pool, impact and explosion decal-manager counts are present."},
    ItemSpec{FeatureStatus::present, "maximum count, creation rate, lifetime, and cull distance", "All five runtime decal-manager counts and limits plus verified lifetimes and distance scales are bounded; an exact creation rate is deliberately not guessed from pooled snapshots."},
    ItemSpec{FeatureStatus::present, "smoke, fire, acid, vomit, and explosions", "Official read-only counts distinguish fire/toxic/other sprays; fire/toxic/other-damaging, unclassified and lingering explosions; exact Hans smoke explosions/projectiles; exact Bloat puke mines, Bloat King puke mines and Bloat King lingering gas; and ground-fire/impact pools. Classification uses SDK classes and damage inheritance, never object-name inference."},
    ItemSpec{FeatureStatus::partial, "translucency, overdraw, lighting, shadows, and render targets", "Fresh visible-particle occupancy and active decal saturation feed a conservative overdraw signal. Verified standalone pressure uses a separate reversible live group for particle distortion/LOD, blood effects and decal budgets; global translucency, materials, shadows, lights, reflection and compute post-effect controls also exist, while internal render-target allocation does not."},
    ItemSpec{FeatureStatus::present, "purely cosmetic versus gameplay-relevant effects", "KFSprayActor and KFExplosionActor damage classes identify gameplay-capable fire/toxic/other damage families; the official non-damaging Hans smoke class is counted separately and unknown actors remain explicitly unclassified. No effect is mutated from this observation."},
    ItemSpec{FeatureStatus::present, "visual quality, visibility, and complete rollback", "Quality policy is explicit and every setting is fully restorable."},
};

constexpr AreaContract area11{
    "Particle systems", "src/config/kf2_catalog.cpp; src/flex; assets/offline_telemetry", "Verified INIs, official EmitterPool/ParticleSystemComponent state and non-semantic FleX aggregates",
    "A official SDK/B partial", "Known controls plus actual gore/world, ground-fire and impact component/particle aggregates, pool capacities, constant/dynamic spawn-source envelopes, bursts, visibility, LOD and bounds are available; unsupported semantics stay unavailable.",
    "Lower effects cost and clearer capacity diagnostics.",
    "Solver counts do not prove fluid, gore, enemy or corpse meaning.",
    "Explicit verified config; aggregate observation only.", "INI restore and FleX stale-control pass-through.",
    "KF2 config and known FleX ABI.", "Capacity, multisolver, stale, loader-soak and visual gameplay tests.",
    "kf2_flex_observation_test; kf2_flex_forwarder_adaptive_test; catalog tests"};

constexpr std::array area11_items{
    ItemSpec{FeatureStatus::partial, "spawn rate, lifetime, and capacity", "Verified cosmetic lifetimes, FleX aggregate capacity and official emitter-pool capacities are reported. Active emitter templates expose a bounded constant spawn-rate sum, dynamic/unknown source count, burst entries and peak-particle capacity; dynamic distributions and unspawned content remain explicitly unknown."},
    ItemSpec{FeatureStatus::partial, "LOD and distance", "Global ParticleLODBias/distance controls are typed and actual gore/world ParticleSystemComponent LOD totals are reported. The verified offline CPU actuator may temporarily raise only the global bias; no per-system mutation is applied."},
    ItemSpec{FeatureStatus::present, "bounding boxes and visibility", "The official probe reports bounded-component and recently-rendered counts separately for gore and world particle systems; FleX bounds calls remain exact pass-through observations."},
    ItemSpec{FeatureStatus::partial, "CPU/GPU simulation", "Official emitter templates distinguish FleX-backed from non-FleX components; exact execution device for every non-FleX type and safe migration controls are not exposed."},
    ItemSpec{FeatureStatus::present, "fluid and non-fluid particles", "Active engine particle components are classified read-only from their official emitter FlexContainerTemplate and bFluid contract, with mixed and unclassified buckets; no phase payload is inspected or changed."},
    ItemSpec{FeatureStatus::present, "memory, transfers, and synchronization", "Capacity, exact set/wait fence calls and particle/phase/velocity upload/download call counts, element totals and memory kinds are observed without reading payloads, allocating, adding waits or changing calls."},
    ItemSpec{FeatureStatus::partial, "missing or incorrectly classified effects", "Every active observed component is reconciled into FleX-fluid/non-fluid/mixed, non-FleX or an explicit unclassified bucket, while ground-fire and impact pools are separately reported; missing unspawned content cannot be inferred."},
};

constexpr AreaContract area12{
    "NVIDIA FleX and safe hook", "src/flex; include/kf2/flex", "Pinned KF2 FleX 1.0.5 identity, export table and shared observation",
    "A/B verified laboratory", "All 52 exports preserve the pinned ABI: 37 are direct PE forwarders and fifteen local relays validate version, solver lifecycle, active count, update policy, exact fence synchronization, bounds, parameter calls and six bulk buffer-transfer entry points.",
    "Offline FleX diagnostics and reversible controlled experiments.",
    "Unknown DLL/version/ABI, stale owner or online state must remain blocked or pass-through.",
    "Adaptive owns the hysteresis-stabilized 1-5 policy; the verified hook is prepared automatically for the protected offline app start.",
    "Stopped-game install, hash backup, atomic marker and exact original restore.",
    "Pinned original runtime and locally built laboratory DLL.",
    "Export/ABI/load/soak/multisolver/stale/restore and real offline gameplay tests.",
    "FleX audit, loader, adaptive, observation and real offline session evidence"};

constexpr std::array area12_items{
    ItemSpec{FeatureStatus::present, "exact KF2 FleX version, export table, ABI, and original DLL", "Pinned version, SHA-256, PE identity and complete export table are audited."},
    ItemSpec{FeatureStatus::present, "solver creation, update, synchronization, destroy, and recreate", "Create/update/destroy/recreate generations and KF2-requested set/wait fence synchronization are observed by exact ABI relays; unrelated exports remain direct pass-through."},
    ItemSpec{FeatureStatus::partial, "substeps, iterations, time delta, stability, and timers", "Substeps and delta are observed/controlled; iterations and internal timers are not semantically exposed."},
    ItemSpec{FeatureStatus::present, "particles, active/free particles, capacities, and buffers", "Aggregate active/free/capacity values are identity-bound and freshness checked; buffer contents are untouched."},
    ItemSpec{FeatureStatus::partial, "fluid/non-fluid phases and flags", "KF2's shipped SPH fluid-mipmap switch is catalogued but protected from Adaptive writes; no verified solver-to-game phase mapping exists."},
    ItemSpec{FeatureStatus::partial, "rigid bodies, shapes, shape matching, and collision shapes", "KF2's shipped high-level rigid-body collision switch is catalogued but protected from Adaptive writes; per-shape gameplay-neutral mutation remains unavailable."},
    ItemSpec{FeatureStatus::partial, "filters, containers, multiple solvers, and generations", "Multiple solver lifecycles and generations are tracked; filter/container semantics are not."},
    ItemSpec{FeatureStatus::partial, "neighbor, contact, and timer data when exported", "The pinned 1.0.5 runtime exports flexGetContacts at ordinal 16 and the laboratory DLL preserves it as an exact direct PE forwarder. No timer export exists, and no ABI-proven non-invasive semantic observation is added."},
    ItemSpec{FeatureStatus::partial, "feature modes and selective functions", "Adaptive uses the bounded FleX policy only when the user already enabled FleX in KF2. Fresh visible-enemy pressure may proactively lower verified solver substeps from five toward one before a later frame-time drop, with 600 ms confirmation and slow recovery; PhysXLevel and unproved global FleX sleep/fluid/rigid controls remain protected from automatic mutation."},
    ItemSpec{FeatureStatus::partial, "asynchronous computation and synchronization points", "The exported set/wait fence calls are counted after exact relay; no wait or synchronization is added by the optimizer."},
    ItemSpec{FeatureStatus::partial, "subrange updates, mapping, and memory transfers", "All six full-buffer particle/phase/velocity upload and download entry points are relayed exactly and measured; the pinned 1.0.5 ABI exposes no verified subrange or mapping entry point, so none is invented."},
    ItemSpec{FeatureStatus::partial, "GPU context loss, TDR, driver changes, and error states", "Stale/failed observations fall back; physical TDR/driver matrices remain target-system tests."},
    ItemSpec{FeatureStatus::partial, "safe solver/particle LOD only with a proven KF2 contract", "Official ParticleSystemComponent and FleX-surrogate LOD values are observed. The verified offline graphics actuator can use KF2's global ParticleLODBias, but no semantic FleX solver-to-effect write contract is invented."},
    ItemSpec{FeatureStatus::partial, "LIVE8, RDX3, RFT4, session, build ID, and lifecycle", "Process/session/build/solver lifecycle is bound; legacy protocol names are not treated as proof by themselves."},
};

constexpr AreaContract area13{
    "Adaptive performance and profiles", "src/optimizer/adaptive_governor.cpp; src/optimizer/adaptive_profile.cpp; src/optimizer/optimizer_engine.cpp",
    "Fresh frame, CPU, GPU, VRAM and RAM evidence", "B measured",
    "Validated sampling, hysteresis, dwell, bounded profiles, recovery, resource attribution and explicit safety-lock priority are deterministic.",
    "Stable automatic profile control without rapid quality oscillation.",
    "Live changes require verified offline gameplay, an authenticated loopback command and exact engine readback; owned values restore to their captured originals.",
    "Adaptive automatic profile with bounded safety locks.", "Automatic protected launch transaction, session token, exact runtime readback and backup/restore.",
    "Telemetry and catalog profiles.", "Degrade, recover, neutral band, stale, safety-lock and quality-policy tests.",
    "kf2_adaptive_profile_test; kf2_adaptive_governor_test; kf2_telemetry_adaptive_stage_test; kf2_adaptive_control_client_test"};

constexpr std::array area13_items{
    ItemSpec{FeatureStatus::present, "target FPS and frame-time budget", "Every integer target from 30 through 240 drives exact relative frame-time bands."},
    ItemSpec{FeatureStatus::present, "hysteresis, cooldown, and stable measurement windows", "Implemented with EMA, dwell, cooldown and hysteresis."},
    ItemSpec{FeatureStatus::present, "CPU, GPU, VRAM, and RAM limits", "Fresh available evidence selects a resource-specific live quality group; low-confidence pressure uses the mixed group and unavailable metrics remain absent."},
    ItemSpec{FeatureStatus::present, "gradual changes instead of jumps", "Intervention moves one native tier, emergency moves up to two, and stable recovery restores one tier at a time."},
    ItemSpec{FeatureStatus::partial, "profile limits by hardware class", "Telemetry-driven bounds exist; a static hardware-class database is intentionally absent."},
    ItemSpec{FeatureStatus::present, "minimum and maximum visual quality", "Exact, Invisible and Performance policies bound visible changes."},
    ItemSpec{FeatureStatus::present, "anti-oscillation and return to better values", "Dwell, cooldown, hysteresis, a two-second actuation interval and bounded recovery are tested."},
    ItemSpec{FeatureStatus::present, "user priority and complete traceability", "Explicit safety locks win; every live change requires an authenticated APPLIED receipt and exact engine readback."},
};

constexpr AreaContract area14{
    "UI and operation", "src/ui; src/overlay; src/platform/windows/window.cpp",
    "Win32, Direct2D, DirectWrite and UI Automation", "A APIs/B tests",
    "Semantic navigation/actions, scrolling, focus, clipping-safe layout and overlay corners are implemented.",
    "Clear portable operation without a secondary runtime.",
    "HDR, mixed-DPI and physical multi-monitor visuals require target systems.",
    "All product modes with mutation guards.", "Local settings can be reset/quarantined; overlay never edits game memory.",
    "Windows graphics and accessibility APIs.",
    "Deterministic renders, DPI transitions, keyboard, UIA, overlay policy/soak and screenshots.",
    "renderer, automation, shell, lifecycle and overlay tests; validate_gui.ps1"};

constexpr std::array area14_items{
    ItemSpec{FeatureStatus::present, "main window, dashboard, and navigation", "Native window, three visible destinations, Home goals, status, and global Update/automatic-check/Repair actions are tested."},
    ItemSpec{FeatureStatus::present, "Adaptive control", "The single Adaptive mode is explicit, persistent and accessible."},
    ItemSpec{FeatureStatus::present, "effective values, source, and status", "Status, source/reason and active settings are visible in UI/report/preview."},
    ItemSpec{FeatureStatus::present, "preview, confirmation, and error messages", "Typed notices and explicit apply workflow are implemented."},
    ItemSpec{FeatureStatus::partial, "overlay metrics, scale, position, and multiple monitors", "Metrics, scale and four corners work; physical mixed-monitor coverage remains external."},
    ItemSpec{FeatureStatus::partial, "DPI 60–200%, resolutions, and window sizes", "Layout and DPI transitions are tested; the overlay scale is 60-200%, while physical DPI matrices remain external."},
    ItemSpec{FeatureStatus::present, "no overlap, clipping, or invisible elements", "Responsive grid, scrolling, clipped painting and hit testing have direct tests."},
    ItemSpec{FeatureStatus::present, "contrast, focus, keyboard, mouse, and screen reader", "Theme contrast, focus order, pointer/keyboard and UI Automation are implemented."},
    ItemSpec{FeatureStatus::present, "modal dialogs, tooltips, dropdowns, and dynamic content", "The non-blocking action UI intentionally avoids modal/dropdown state, renders bounded contextual help for safety-critical actions, and tests dynamic notices, actions and tooltip placement."},
    ItemSpec{FeatureStatus::partial, "visual regressions and real screenshots", "Deterministic captures exist; additional physical HDR/mixed-DPI screenshots remain target evidence."},
};

constexpr AreaContract area15{
    "Diagnostics and logging", "src/diagnostics; diagnostics actions; src/flex/forwarder_stub.cpp",
    "Bounded product events, verified metrics and shared-memory FleX counters", "Local",
    "Current and previous session logs are bounded; reports carry build, process, telemetry, profile and recovery context.",
    "Faster diagnosis with privacy-safe reproducible reports.",
    "Logs cannot prove unobserved gameplay semantics and may be unavailable on I/O failure.",
    "Read-only in every mode.", "Atomic local files, one previous log and bounded histories.",
    "Portable Data directory and identity-bound shared memory.",
    "Dedup, bounds, concurrency, corruption, inventory, hot-path and report-schema tests.",
    "kf2_event_log_test; kf2_feature_inventory_test; FleX forwarder tests"};

constexpr std::array area15_items{
    ItemSpec{FeatureStatus::present, "structured logs and correlation", "Typed severity/code/source/sequence records are persisted and correlated with process/session context."},
    ItemSpec{FeatureStatus::partial, "LIVE, RDX, and RFT contracts", "Current native contracts supersede legacy names; process/session/build bindings exist but are not renamed as fake legacy proof."},
    ItemSpec{FeatureStatus::present, "feature inventory and instrumentation", "A local offline inventory contains all 149 Issue-72 function bullets and mandatory fields."},
    ItemSpec{FeatureStatus::present, "hot path without file I/O, heap allocation, formatting, or waiting", "The FleX relay uses fixed storage, non-blocking TryAcquire locks, atomics and shared memory only."},
    ItemSpec{FeatureStatus::present, "sequence, overwrite, drop, and error counters", "Event append/deduplication/overwrite/persistence counters and saturated FleX missing-original/tracking-drop/invalid-argument counters are exported."},
    ItemSpec{FeatureStatus::present, "snapshot, map, and binary binding", "Process/start, volume/file, map/log and pinned FleX binary identities are bound; parser byte/line/drop counters expose source loss while absent gameplay semantics stay absent."},
    ItemSpec{FeatureStatus::present, "diagnostics package, status report, and crash data", "A single local privacy-safe support bundle combines status, counters, events and all 149 inventory records; only crash-record counts are included, never dump contents."},
    ItemSpec{FeatureStatus::present, "rotation, storage limits, and privacy", "Logs/history are bounded, only one previous session is retained and nothing is uploaded."},
};

constexpr AreaContract area16{
    "Security and data integrity", "src/platform/windows/atomic_file.cpp; src/backup; src/config/session_guard.cpp",
    "Windows file handles, identities, hashes, manifest and explicit allowlists", "A APIs/B tests",
    "Reparse/hardlink targets are rejected; manifests are bounded and identity-bound; the app runs asInvoker with PE mitigations.",
    "Protect KF2 and user files from stale, foreign or manipulated local state.",
    "External Defender/CFA/WDAC/HVCI policies and signing require target environments or credentials.",
    "Mutations only in normal mode and stopped-game transactions.",
    "Content hashes, journals, rollback, quarantine and exact root binding.",
    "Windows filesystem, BCrypt SHA-256 and package metadata.",
    "Negative path/hardlink/manifest/root/disk-space/recovery/PE/import tests.",
    "atomic, backup, session, config-apply, PE and package validation tests"};

constexpr std::array area16_items{
    ItemSpec{FeatureStatus::present, "fail-closed behavior", "Unknown identities, formats, values, paths and stale state are rejected."},
    ItemSpec{FeatureStatus::present, "path canonicalization, reparse points, hardlinks, and junctions", "Handle identity, reparse checks and single-link requirements protect mutation paths."},
    ItemSpec{FeatureStatus::partial, "ACL, UAC, CFA, Defender, WDAC, AppLocker, HVCI, and Smart App Control", "asInvoker/fail-closed behavior exists; external policy matrices remain unexecuted."},
    ItemSpec{FeatureStatus::present, "DLL search path and hijacking", "Startup permanently enables the safe process search mode, System32/UserDirs-only default loading and removes the current directory from legacy DLL search."},
    ItemSpec{FeatureStatus::partial, "hashes, signatures, build ID, and manifests", "SHA-256/build identity/manifests exist; user-owned code signing is unavailable."},
    ItemSpec{FeatureStatus::present, "temporary files, atomicity, and transactions", "Unique CREATE_NEW temporary files, CAS and journals are hardened."},
    ItemSpec{FeatureStatus::present, "backup, restore, rollback, and disk-full behavior", "Backup verification, transactional rollback, an injected disk-full boundary and a real Windows no-delete-sharing lock test prove fail-closed behavior and residue cleanup; CFA remains separately tracked as external policy evidence."},
    ItemSpec{FeatureStatus::present, "no anti-cheat, kernel, driver, or security bypass", "Permanent production boundary."},
    ItemSpec{FeatureStatus::present, "no unknown process-memory addresses as production sources", "Only documented OS/log/shared-contract sources are read."},
};

constexpr AreaContract area17{
    "Build, package and GitHub", "CMakeLists.txt; tools; .github/workflows", "MSVC, CMake, PowerShell and GitHub Actions",
    "A tool output/B tests", "Debug/Release tests are dynamically counted; package contains the native EXE, local Data and optional isolated lab payload.",
    "Clean repeatable development and portable delivery.",
    "Publishing, signing, merge, tag and release require explicit user authority.",
    "Development/release process, not a runtime mode.", "Rebuild package while preserving user Data; hashes verify output.",
    "MSVC x64, Windows SDK, CMake, PowerShell and self-hosted Windows runners.",
    "Full Debug/Release, clean double build, PE/import/package/GUI validation.",
    "tools scripts and workflows; current run evidence generated separately"};

constexpr std::array area17_items{
    ItemSpec{FeatureStatus::present, "Visual Studio, MSVC, CMake, Ninja, and PowerShell 5.1", "The current host provides MSVC/Visual Studio/CMake, the Visual Studio Ninja tool and Windows PowerShell 5.1; both the default VS generator and an isolated Ninja build contract are validated locally."},
    ItemSpec{FeatureStatus::present, "native dual builds and reproducibility", "A clean deterministic double-build validator exists; it must be rerun after each final change."},
    ItemSpec{FeatureStatus::present, "PE, import, export, manifest, and protection-flag validation", "PE architecture, manifest, mitigations, imports and FleX exports are validated."},
    ItemSpec{FeatureStatus::partial, "source, release, audit, and recovery packages", "The clean source and portable release plus evidence exist; no redundant historical audit package is required."},
    ItemSpec{FeatureStatus::present, "SHA-256, SBOM, and provenance evidence", "Release evidence generates hashes, SBOM and build/commit metadata."},
    ItemSpec{FeatureStatus::partial, "CI, Runner 7, workflows, artifacts, and cleanup", "Permanent build/GUI workflows exist on current self-hosted labels; external runner availability is not locally provable."},
    ItemSpec{FeatureStatus::partial, "branches, pull requests, issues, labels, and release chain", "Issue/branch flow exists; PR/merge/tag/release are intentionally not performed without authority."},
    ItemSpec{FeatureStatus::present, "no merges, tags, or releases without explicit approval", "Enforced collaboration boundary."},
};

constexpr AreaContract area18{
    "Compatibility and real usage", "Automated policy tests plus docs/FINAL_ACCEPTANCE.md",
    "Current Windows PC and explicitly available target systems", "B measured/external",
    "The current system can prove its own GUI, telemetry, overlay, restore and gameplay; absent hardware or policies cannot be claimed.",
    "Honest compatibility evidence without fake PASS results.",
    "HDR, mixed DPI, multi-GPU, EDR policies and long soak differ by system.",
    "All modes and real offline gameplay.", "Fail-closed behavior, diagnostics export and exact restore.",
    "Additional physical systems/policy states and user gameplay.",
    "DPI 100-200, multi-monitor/HDR, GPU/driver matrix, standby, AV and soak.",
    "Current automated tests plus dated real acceptance evidence; unavailable systems remain BLOCKED"};

constexpr std::array area18_items{
    ItemSpec{FeatureStatus::partial, "Windows versions and patch levels", "Current Windows is tested; a multi-version physical matrix is not."},
    ItemSpec{FeatureStatus::partial, "different CPUs, GPUs, and drivers", "Current hardware is measured; additional physical combinations are external."},
    ItemSpec{FeatureStatus::partial, "multiple monitors, HDR, DWM, and fullscreen/windowed modes", "DWM/window/coverage policy is automated; physical HDR/mixed-monitor/fullscreen combinations remain external."},
    ItemSpec{FeatureStatus::present, "standby, resume, Alt-Tab, and process restart", "Power-resume notifications detach stale ETW/PDH/process state and perform an identity-bound telemetry reattach; Alt-Tab and process restart paths are covered."},
    ItemSpec{FeatureStatus::partial, "antivirus/EDR behavior", "Fail-closed I/O exists; product-specific external policy runs remain BLOCKED."},
    ItemSpec{FeatureStatus::partial, "Steam, installation, and game updates", "Current Steam/KF2 identity is verified; future game updates require revalidation."},
    ItemSpec{FeatureStatus::partial, "different maps, waves, enemy classes, and load states", "Prior official-map runs exist; every future build still needs its final gameplay acceptance."},
    ItemSpec{FeatureStatus::partial, "long soak, leak, race, deadlock, and performance tests", "Bounded stress/soak tests exist; long physical gameplay and verifier runs remain external."},
};

const char* status_name(FeatureStatus status) noexcept {
    switch (status) {
        case FeatureStatus::present: return "present";
        case FeatureStatus::partial: return "partial";
        case FeatureStatus::planned: return "planned";
        case FeatureStatus::discarded: return "discarded";
        case FeatureStatus::implementation_ready: return "implementation_ready";
    }
    return "unknown";
}

const char* remaining_scope_name(RemainingScope scope) noexcept {
    switch (scope) {
        case RemainingScope::none: return "none";
        case RemainingScope::external_validation: return "external_validation";
        case RemainingScope::engine_contract: return "engine_contract";
        case RemainingScope::safety_boundary: return "safety_boundary";
        case RemainingScope::user_authority: return "user_authority";
    }
    return "none";
}

RemainingScope remaining_scope_for(std::uint16_t area, std::uint16_t item,
                                   FeatureStatus status) noexcept {
    if (status == FeatureStatus::present) return RemainingScope::none;
    if (status == FeatureStatus::discarded) return RemainingScope::safety_boundary;
    if (status == FeatureStatus::planned ||
        status == FeatureStatus::implementation_ready) {
        return RemainingScope::engine_contract;
    }
    if (area == 17 && item == 7) return RemainingScope::user_authority;
    if (area == 4 && (item == 2 || item == 6 || item == 8)) {
        return RemainingScope::external_validation;
    }
    if (area == 12 && item == 12) return RemainingScope::external_validation;
    if (area == 13 && item == 5) return RemainingScope::safety_boundary;
    if (area == 14 && (item == 5 || item == 6 || item == 10)) {
        return RemainingScope::external_validation;
    }
    if (area == 16 && (item == 3 || item == 5)) {
        return item == 5 ? RemainingScope::user_authority
                         : RemainingScope::external_validation;
    }
    if (area == 17) {
        return item == 6 ? RemainingScope::external_validation
                         : RemainingScope::user_authority;
    }
    if (area == 18) return RemainingScope::external_validation;
    return RemainingScope::engine_contract;
}

std::string decision_for(FeatureStatus status) {
    switch (status) {
        case FeatureStatus::present:
        case FeatureStatus::partial: return "OBSERVE";
        case FeatureStatus::planned: return "LAB";
        case FeatureStatus::discarded: return "DISCARD";
        case FeatureStatus::implementation_ready: return "IMPLEMENTATION_READY";
    }
    return "OBSERVE";
}

std::string status_statement(FeatureStatus status) {
    switch (status) {
        case FeatureStatus::present: return "Implemented current contract. ";
        case FeatureStatus::partial: return "Verified subset only. ";
        case FeatureStatus::planned: return "No production mutation; evidence-gated research. ";
        case FeatureStatus::discarded: return "Intentionally excluded from production. ";
        case FeatureStatus::implementation_ready: return "Implementation contract is sufficiently proven. ";
    }
    return {};
}

template <std::size_t Size>
void append_area(std::vector<FeatureRecord>& records, std::uint16_t area,
                 const AreaContract& contract,
                 const std::array<ItemSpec, Size>& items) {
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        std::ostringstream id;
        id << "I72-A" << std::setw(2) << std::setfill('0') << area
           << "-F" << std::setw(2) << (index + 1);
        FeatureRecord record;
        record.id = id.str();
        record.name = std::to_string(area) + ". " + std::string{contract.name} +
            " / " + std::string{item.requirement};
        record.area = area;
        record.item = static_cast<std::uint16_t>(index + 1);
        record.status = item.status;
        record.remaining_scope = remaining_scope_for(
            static_cast<std::uint16_t>(area),
            static_cast<std::uint16_t>(index + 1), item.status);
        record.user_requirement = item.requirement;
        record.code_path = contract.code_path;
        record.data_source = contract.data_source;
        record.trust_class = contract.trust_class;
        record.technical_statement = status_statement(item.status) +
            std::string{contract.technical_statement} + " Assessment: " +
            std::string{item.assessment};
        record.expected_benefit = contract.expected_benefit;
        record.risks = contract.risks;
        record.mode_support = contract.mode_support;
        record.reversible_path = contract.reversible_path;
        record.dependencies = contract.dependencies;
        record.required_tests = contract.required_tests;
        record.evidence = contract.evidence;
        record.decision = decision_for(item.status);
        record.linkage = "GitHub Issue #72; area " + std::to_string(area) +
            "; current source/tests; docs/ISSUE_72_PRODUCT_MATRIX.md";
        records.push_back(std::move(record));
    }
}

const std::vector<FeatureRecord>& inventory() {
    static const std::vector<FeatureRecord> records = [] {
        std::vector<FeatureRecord> result;
        result.reserve(149);
        append_area(result, 1, area01, area01_items);
        append_area(result, 2, area02, area02_items);
        append_area(result, 3, area03, area03_items);
        append_area(result, 4, area04, area04_items);
        append_area(result, 5, area05, area05_items);
        append_area(result, 6, area06, area06_items);
        append_area(result, 7, area07, area07_items);
        append_area(result, 8, area08, area08_items);
        append_area(result, 9, area09, area09_items);
        append_area(result, 10, area10, area10_items);
        append_area(result, 11, area11, area11_items);
        append_area(result, 12, area12, area12_items);
        append_area(result, 13, area13, area13_items);
        append_area(result, 14, area14, area14_items);
        append_area(result, 15, area15, area15_items);
        append_area(result, 16, area16, area16_items);
        append_area(result, 17, area17, area17_items);
        append_area(result, 18, area18, area18_items);
        return result;
    }();
    return records;
}

std::string escape(std::string_view value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    constexpr char digits[] = "0123456789abcdef";
                    output << "\\u00" << digits[character >> 4]
                           << digits[character & 0x0f];
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    return output.str();
}

}  // namespace

std::span<const FeatureRecord> issue72_feature_inventory() noexcept {
    return inventory();
}

FeatureStatusCounts feature_status_counts(
    std::span<const FeatureRecord> records) noexcept {
    FeatureStatusCounts result;
    for (const auto& record : records) {
        switch (record.status) {
            case FeatureStatus::present: ++result.present; break;
            case FeatureStatus::partial: ++result.partial; break;
            case FeatureStatus::planned: ++result.planned; break;
            case FeatureStatus::discarded: ++result.discarded; break;
            case FeatureStatus::implementation_ready:
                ++result.implementation_ready;
                break;
        }
    }
    return result;
}

RemainingScopeCounts remaining_scope_counts(
    std::span<const FeatureRecord> records) noexcept {
    RemainingScopeCounts result;
    for (const auto& record : records) {
        switch (record.remaining_scope) {
            case RemainingScope::none: ++result.none; break;
            case RemainingScope::external_validation:
                ++result.external_validation;
                break;
            case RemainingScope::engine_contract:
                ++result.engine_contract;
                break;
            case RemainingScope::safety_boundary:
                ++result.safety_boundary;
                break;
            case RemainingScope::user_authority:
                ++result.user_authority;
                break;
        }
    }
    return result;
}

std::string serialize_feature_inventory_json(
    std::string_view build_identity, std::span<const FeatureRecord> records) {
    std::array<std::size_t, 5> status_counts{};
    std::array<std::size_t, 5> remaining_counts{};
    for (const auto& record : records) {
        const auto index = static_cast<std::size_t>(record.status);
        if (index < status_counts.size()) ++status_counts[index];
        const auto remaining_index =
            static_cast<std::size_t>(record.remaining_scope);
        if (remaining_index < remaining_counts.size()) {
            ++remaining_counts[remaining_index];
        }
    }
    std::ostringstream output;
    output << "{\"schema\":\"KF2_ISSUE72_INVENTORY_V3\",\"build_identity\":\""
           << escape(build_identity) << "\",\"issue\":72,\"function_count\":"
           << records.size() << ",\"status_counts\":{\"present\":"
           << status_counts[static_cast<std::size_t>(FeatureStatus::present)]
           << ",\"partial\":"
           << status_counts[static_cast<std::size_t>(FeatureStatus::partial)]
           << ",\"planned\":"
           << status_counts[static_cast<std::size_t>(FeatureStatus::planned)]
           << ",\"discarded\":"
           << status_counts[static_cast<std::size_t>(FeatureStatus::discarded)]
           << ",\"implementation_ready\":"
           << status_counts[static_cast<std::size_t>(FeatureStatus::implementation_ready)]
           << "},\"remaining_scope_counts\":{\"none\":"
           << remaining_counts[static_cast<std::size_t>(RemainingScope::none)]
           << ",\"external_validation\":"
           << remaining_counts[static_cast<std::size_t>(
                  RemainingScope::external_validation)]
           << ",\"engine_contract\":"
           << remaining_counts[static_cast<std::size_t>(
                  RemainingScope::engine_contract)]
           << ",\"safety_boundary\":"
           << remaining_counts[static_cast<std::size_t>(
                  RemainingScope::safety_boundary)]
           << ",\"user_authority\":"
           << remaining_counts[static_cast<std::size_t>(
                  RemainingScope::user_authority)]
           << "},\"records\":[";
    bool first = true;
    for (const auto& record : records) {
        if (!first) output << ',';
        first = false;
        output << "{\"id\":\"" << escape(record.id)
               << "\",\"name\":\"" << escape(record.name)
               << "\",\"area\":" << record.area
               << ",\"item\":" << record.item
               << ",\"status\":\"" << status_name(record.status)
               << "\",\"remaining_scope\":\""
               << remaining_scope_name(record.remaining_scope)
               << "\",\"user_requirement\":\"" << escape(record.user_requirement)
               << "\",\"code_path\":\"" << escape(record.code_path)
               << "\",\"data_source\":\"" << escape(record.data_source)
               << "\",\"trust_class\":\"" << escape(record.trust_class)
               << "\",\"technical_statement\":\"" << escape(record.technical_statement)
               << "\",\"expected_benefit\":\"" << escape(record.expected_benefit)
               << "\",\"risks\":\"" << escape(record.risks)
               << "\",\"mode_support\":\"" << escape(record.mode_support)
               << "\",\"reversible_path\":\"" << escape(record.reversible_path)
               << "\",\"dependencies\":\"" << escape(record.dependencies)
               << "\",\"required_tests\":\"" << escape(record.required_tests)
               << "\",\"evidence\":\"" << escape(record.evidence)
               << "\",\"decision\":\"" << escape(record.decision)
               << "\",\"linkage\":\"" << escape(record.linkage) << "\"}";
    }
    output << "]}";
    return output.str();
}

}  // namespace kf2::diagnostics
