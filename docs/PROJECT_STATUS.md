# KF2 Optimizer Next - current project status

- Updated: 2026-08-22
- Default branch: `main`
- Current development release: [`Alpha 0.0.4`](../README.md#download)

The current rewrite is a portable native Windows application. The authoritative
feature inventory is generated from the source and contains all 149 individual
requirements from GitHub Issue 72.

## Current verified state

- The complete current Visual Studio Debug and Release suites pass 81/81
  tests, including updater rollback and native Windows UI coverage.
- The normal interface has three areas: Home, Overlay, and Help & Repair. Home
  contains the two user goals, while Updates, automatic checks, and Repair stay
  visible in the upper-right corner. An available update is highlighted without
  a disruptive pop-up.
- Adaptive / Automatic is the only optimizer system. Home now exposes a
  persistent On/Off control for its automatic adjustments while fixed goals,
  telemetry, overlay, and explicit game settings remain independent. Legacy `smart_mode`,
  `optimizer_mode=smart` and `optimizer_mode=manual` are accepted only during
  migration and are atomically rewritten to the canonical Adaptive schema.
- Adaptive owns baseline selection and control instead of running beside a
  second Smart recommender. It filters absolute per-setting locks, applies the
  verified plan automatically before an app-started protected session, and
  restores the exact pre-game INIs after KF2 exits. The same General Adaptive
  controller runs for every session; each sensor and actuator independently
  reports `AVAILABLE`, `UNAVAILABLE`, `RESTART_REQUIRED` or `SHADOW` instead of
  placing the whole controller behind an Online/Offline switch.
- The Adaptive V2 target registry represents every concrete item from sections
  4-100 of the master specification, its architecture sections 101-138 and all
  212 verified KF2 catalog settings. It records source, bounds, restore path,
  risk, evidence, locks and capability state instead of silently treating a
  name in the specification as a writable game setting.
- The deterministic target-relative governor validates process/session identity,
  sample age, loss, discontinuities, ranges, GPU LUID binding and contradictory
  measurements before classification. It includes prediction, hysteresis,
  exact 60/59, 60/58 and 60/57 target bands, dwell, fast correction, slow
  recovery, budget and overhead watchdogs, explicit safety-lock precedence and
  explicit CPU/GPU/VRAM/RAM/paging/I/O/streaming/thermal/FleX/particle/
  ragdoll/physics/animation/gore/rendering/mixed/unknown bottlenecks.
- CPU classification now combines the true total KF2 process share with
  throttled per-thread sampling of logical-core equivalents, active threads,
  busiest-thread occupancy, dominant-thread CPU-time share and the process'
  current physical/logical affinity capacity. It distinguishes main-thread
  dominance, partial parallelism and broad parallel saturation, and reports an
  external affinity subset without changing it. A saturated game/main thread
  therefore remains visible on many-core CPUs without mislabeling the overlay's
  total CPU percentage. Adaptive evaluates only active gameplay and persists a
  next-launch profile only after 8 seconds of stable degradation or 45 seconds
  of stable recovery with verified headroom; intervention can never be treated
  as quality recovery. A two-second CPU evidence hold removes critical-thread
  threshold flapping without masking another proven bottleneck. Menu/loading
  frames cannot churn the profile.
- All currently unproved game mutations are structurally `SHADOW` and
  `TEST_REQUIRED`; protected gameplay/network/security targets are permanently
  blocked. No generated target becomes live merely because it exists in the
  registry.
- The strict catalog covers 212 typed settings in the three protected KF2 INIs; 143 visibility/physics-sensitive values are protected from pre-launch profile writes. It includes the real active `KFGame.KFGameEngine` FPS cap/smoothing pair and rejects an invalid minimum/maximum smoothing window. The catalog also covers KF2's shipped FleX and APEX controls, global physics substeps, fracture/chunk limits, particle-memory and emitter budgets, CPU/frame-pacing, texture streaming/pool controls, render targets, anti-aliasing, compute effects, shadow/CSM, reflection, translucency, motion blur and clothing controls without granting profile automation permission to change sensitive values.
- Verified standalone gameplay can use a separate authenticated loopback actuator for bounded CPU, GPU, VRAM, RAM, overdraw or mixed graphics tiers. The overdraw tier combines fresh particle occupancy/visibility and decal saturation, then changes only particle distortion/LOD, blood effects and decal budgets. Every action requires exact KF2 readback, pauses during Zed Time, leaves display/pacing/anti-aliasing/FleX ownership untouched and restores only its captured values.
- PresentMon/DXGI, CPU/RAM, GPU/VRAM, overlay, backup/restore, session recovery,
  diagnostics and the offline-only FleX laboratory have direct current tests.
- GPU diagnostics now group multiple DXGI display-path LUIDs by their physical
  PCI PnP identity, while live process sampling keeps every LUID available.
- The independent Ninja Release contract enters a discovered Visual Studio x64
  developer environment itself, so it does not depend on a preconfigured runner
  shell. Its clean current-source suite passes 72/72 tests.
- Dedicated current contracts now cover hardware overview, repeated hardware
  refresh and every overlay corner instead of relying on indirect coverage.
- The Issue 72 inventory currently classifies 88 records as complete, 60 as
  verified but evidence-gated contracts, no item as unstarted research or
  implementation-ready local work, and 1 as deliberately excluded
  gameplay-risky mutations.
- The 149 records are also separated by what remains: 88 need no further work,
  16 require external hardware/gameplay/policy validation, 40 require an engine
  or ABI contract that KF2 does not expose, 2 are safety-boundary records and 3
  require user authority such as signing or publishing. This prevents an
  external blocker from looking like a forgotten local feature.
- No gated or excluded record is silently counted as an implemented function.

## Current completion boundary

The in-tree Debug, Visual Studio Release and clean Ninja Release regressions are
complete for the General-Adaptive source at 72/72 tests each. Two clean Release
builds produced byte-identical EXEs and FleX laboratory forwarders.
The portable package is validated separately with fifteen managed payload hashes
and user-data-preserving update semantics. The UI exposes one Adaptive control
plane and the fail-closed safety policy.

The final packaged executable has reached official Biotics Lab in offline
Survival through the protected Adaptive start. Real gameplay showed live
FPS/AVG/1% low/frame time/CPU/GPU and aggregate living/corpse/particle state,
all four instance-bound corpse-collision decisions, 18 confirmed runtime-budget
actuations, and 8,785 successful FleX solver relays. Normal KF2 termination
restored the original runtime and 16 protected INIs and removed the temporary
telemetry module. A prior candidate also completed all four short waves and the
boss with firearms, explosives, grenades, gore, gibs and heavy corpse load. The
remaining visual-only acceptance is an isolated distance-sleep/proximity-wake
transition and unavailable external hardware variants.

## Technical limits that are not missing implementation work

Issue 72 also asks for writable per-enemy animation, arbitrary per-corpse
physics/collision mutation, exact decal events, FleX gameplay classification and
internal solver features not exposed by the pinned KF2 ABI. The official
read-only offline telemetry schema 6 now covers aggregate living/corpse/gib/Zed-Time,
exact detached-limb state, ragdoll warning state, all four official runtime
corpse-collision decisions, skeletal LOD/animation, visibility, runtime budget
and exact living-Zed special-move buckets. It also distinguishes smoke,
fire, toxic/acid, Bloat puke mines and explosions from official SDK classes
and damage inheritance, plus particle FleX-fluid/non-fluid classification,
pool capacities, constant/dynamic spawn sources, bursts and peak capacity, while
PresentMon measures presents and the FleX ABI exposes non-semantic solver values.

The same pinned package now contains a protected autonomous corpse capability.
A normal app-started General Adaptive session prepares this hash-bound provider
automatically even when the legacy optional lab switch is off; read-only/safe
starts still fail closed. The capability becomes available only after current
process/session identity and freshness are proven. General Adaptive itself
remains active when this capability is unavailable. The selected `MaxDeadBodies`
value from 4-2000 is the stable user ceiling. Merely reaching it does not delete
anything. Only fresh pressure against the exact target-relative warning,
corrective and critical bands lowers a separate runtime threshold in bounded
steps and permits the actuator to drain
confirmed excess corpses through KF2's official cleanup routine, one every 450
ms at most, after a 1.5-second minimum age, preferring sleeping/offscreen bodies
and pausing during Zed Time. Cleanup stops immediately when the current pressure
sample recovers; capacity then rises gradually toward the selected ceiling and
the complete protected session is restored afterward.

FleX solver actions remain `PENDING` until the shared-memory provider reports
the requested value and a matching forwarded value in the current generation.
Each accepted receipt is now retained in the bounded session event log as
`FLEX_ADAPTIVE_APPLIED`, including requested/effective values and ownership
generations; proposals and writes without readback are never logged as applied.

The actuator also has a separate ragdoll-load controller. Its distant-physics
stage is distance-first: without FPS pressure it may select only an old, slow,
offscreen/occluded body beyond 1,200 units at most once per 400 ms. Fresh visible
enemy/corpse density or hysteresis-confirmed target-relative frame pressure
moves entry to 1,000/850 units and the interval to 200/100 ms. It uses KF2's
official `PutRigidBodyToSleep` path and never hides an actor. A
tracked body is woken through `WakeRigidBody` after it comes inside 800 units;
the separated entry/wake distances prevent churn. Only confirmed frame pressure
may continue into native `MinLodModel` on one distant visible corpse every
400/200/100 ms, starting at stage 2 outside 300 units and advancing through
  stages 3/4/5 at 500/800/1,200 units. Visible-enemy pressure scales continuously
  from five to eighty Zeds and consumes every available stage without exceeding the mesh's final LOD
or overriding `ForcedLodModel`; nearby, sleeping,
recovered and session-ending LOD values are restored. Only after those stages
may one old, slow visible corpse sleep every 750/350 ms. Living enemies, death
animations and fast bodies are unchanged. The CPU live-quality group can disable
the three official cosmetic corpse-collision switches at 80/60/40%, with exact
capture, engine readback and restore. Living-enemy collision/navigation,
`bAllowPhysics`, Zed Time, Manual and online sessions remain unchanged. KF2/UE3
already performs native view-frustum and
occlusion culling for living-enemy primitives, so the optimizer never uses
`bHidden` or custom hide/show logic that could create invisible enemies.

An additional time-sliced aging path now checks exactly one corpse-pool entry
every 50 ms and performs at most one state transition per invocation. Ages 3,
8 and 15 seconds progressively raise real corpse mesh LOD, stop skeleton work
after native physics sleep and, at safe distance, request verified rigid-body
sleep. Recently rendered corpses remain eligible for staged LOD outside 300
units and verified physics sleep outside the early-tier distance guards. The
15-second final tier covers every confirmed corpse, including a nearby visible
body; the 3/8-second tiers keep 1,200/1,000-unit guards. After exact sleep
readback, the final tier moves the actor once to `PHYS_None`, so native gameplay
cannot wake it and later hits, explosions or collisions no longer move it. The
public SDK exposes no safe fractional PhysX update rate or dedicated sleep lock,
so this explicit final-pose tradeoff is used instead of pretending that a
wake-event flag prevents wake. Adaptive disable restores one tracked body every
50 ms to avoid a release batch. The path records actor identity, age, distance,
visibility and readback, pauses during Zed Time and never changes collision
channels, cleanup limits or living Zeds. This
candidate still requires large-pool gameplay validation before merge.

The remaining arbitrary mutation functions must stay unavailable or pass-through until an authoritative
read-only source and gameplay-neutral mutation contract exist. Guessing them,
injecting unknown process addresses, altering living-enemy truth or bypassing
security is intentionally excluded.

## External acceptance coverage

Physical HDR, mixed-DPI/multi-monitor, multi-GPU, alternate Windows versions,
Defender/CFA/WDAC/HVCI matrices, code signing and long hardware soaks require
hardware, policy states or credentials not created by the application. They are
recorded as external blockers rather than fake passes.

## Status sources

- `docs/PROJECT_STATUS.md`: this short current overview.
- `docs/function-matrix.md`: technical N/L/X decision matrix.
- `docs/ISSUE_72_PRODUCT_MATRIX.md`: 18-area product mapping.
- `src/optimizer/adaptive_spec_items.inc` and
  `src/optimizer/adaptive_registry.cpp`: complete Adaptive V2 target registry
  and promotion metadata.
- `Data/Documentation/issue72-feature-inventory.json` in a package: exact 149
  machine-readable records with code paths, risks, tests and decisions.
- `docs/FINAL_ACCEPTANCE.md`: current automated, external and final-gameplay gates.
