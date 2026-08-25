# Final acceptance ledger

This ledger describes the current working-tree candidate. Automated proof is
kept separate from physical target-system and gameplay evidence. Historical
runs are never promoted to proof for a newly built executable.

| Gate | Current evidence | Current state |
|---|---|---|
| Debug, Release and Ninja regression | The complete current suites pass 87/87 tests in Debug and Visual Studio Release. Ninja is not installed on this validation host, so no current Ninja result is claimed. | PASS (AUTOMATED MSVC); NINJA NOT RUN |
| Adaptive Mode V2 contracts | Complete target registry, one General Adaptive user mode, atomic legacy-Smart/Manual and TargetFPS migration, strict portable locks, exact target-relative 60/59, 60/58 and 60/57 bands, deterministic predictor/governor, active-gameplay gate, critical-thread CPU evidence with classification hysteresis, fast correction, stable-headroom-only slow recovery, automatic protected pre-launch transaction, per-source/per-actuator capabilities, typed proposed/pending/applied receipts, generations, timeout/circuit-breaker handling and replay tests pass. There is no broad Online/Offline latch. Unproved game targets remain SHADOW/TEST_REQUIRED and protected targets remain blocked. | PASS (AUTOMATED DESIGN/SAFETY) |
| Protected gameplay telemetry and adaptive corpse load control | Official KFEditor compiles the pinned package with 0 errors; telemetry stays read-only. A normal General Adaptive session prepares the hash-bound Published mutator for KF2 started from the optimizer, Steam or a shortcut while preserving KF2's native viewport. The provider is reported as prepared until KF2 confirms its runtime capability. A fresh Steam-started Biotics Lab session confirmed the mutator, interaction, probe, adaptive bridge, schema-6 samples and `corpseCapability=AVAILABLE`; shutdown restored all protected INIs and removed the module. When available, the independently reported protected corpse capability keeps the selected 4-2000 count as the stable ceiling and uses official confirmed-corpse cleanup at most once per 450 ms only while a fresh current sample and sustained target-relative pressure are present. Confirmed pressure lowers a separate runtime threshold stepwise; current recovery stops removal and restores capacity gradually toward the selected ceiling. Distant corpse physics is distance-first: eligible bodies beyond 1,200 units may sleep without FPS pressure, while corpse density, dynamic enemy pressure or confirmed frame pressure moves entry to 800/450 units. Enemy pressure is a monotonic 0-100 score from visible enemies, total living enemies and active attacks; levels begin at 30/65, require only one eligible awake corpse and continuously move Distance Sleep toward 450 units. The app reuses it only to strengthen live graphics, physics and already-user-enabled FleX after frame or resource pressure is confirmed. The selected corpse maximum bounds the pool, but Distance Sleep adds no second actor or batch cap: one linear pass may sleep every currently eligible corpse. Distance Sleep is permanent for that corpse and has no proximity-wake pass. Reversible corpse LOD applies outside 1,000 units and restores inside 900 units with actor-correlated restore receipts; an external LOD override releases and permanently excludes that actor from optimizer LOD ownership. Bounded visible-ragdoll sleep begins at low-to-medium scene density. Native living-enemy frustum/occlusion culling is retained with no manual hiding, and every corpse action pauses during Zed Time. Source-contract tests cover the Zed-Time guard, dynamic enemy pressure, unbounded linear distance-first processing, absence of Near Wake and stable LOD ownership. A fresh combat isolation remains required for the revised corpse-policy action thresholds. | PASS COMPILE/CONTRACT/PROVIDER LIVE; PENDING CORPSE-ACTION COMBAT ISOLATION |
| Two clean deterministic builds | Two clean current-source Release builds produce byte-identical EXEs and FleX laboratory forwarders | PASS (AUTOMATED) |
| Portable package shape | One root EXE, fifteen hash-verified payload files, telemetry/FleX lab assets, documentation and release evidence are generated only after the current regression and reproducibility gates; portable user data and Adaptive locks remain preserved during updates | PASS (AUTOMATED) |
| Fresh GUI start/exit | The final portable package starts visibly, enforces single instance and exits cleanly; damaged-package and loose-EXE starts report package-integrity evidence while keeping unrelated controls available for component-level recovery | PASS (AUTOMATED/VISUAL) |
| PresentMon/CPU/GPU/overlay contracts | Current Release tests cover DXGI presents, freshness, total-process CPU, busiest-thread occupancy, effective logical-core use, active threads, dominant-thread share, physical/logical affinity capacity, physical PnP GPU grouping, Afterburner-compatible NVIDIA NVAPI utilization, local NVML and adapter-wide PDH fallbacks, and overlay soak. Affinity is observed but never changed. | PASS (AUTOMATED) |
| Real three-INI roundtrip | All 217 typed values, including the verified `KFEngine.ini` `PhysXLevel` 0-2 contract, the two allowlisted asynchronous-physics keys and the native texture-streaming memory controls, were previewed and changed in copies of the installed three-file KF2 configuration; apply, verified backup and restore returned every copy byte-identically and left the source files unchanged | PASS (AUTOMATED) |
| Installed FleX runtime | Current Release audit verifies the installed KF2 1.0.5 runtime, pinned SHA-256 and 52 exports | PASS (READ-ONLY AUDIT) |
| Offline FleX laboratory | 37 direct PE forwarders plus fifteen local ABI relays, including exact set/wait fence, solver bounds, parameter-call and six particle/phase/velocity bulk-transfer observations, loader/adaptive tests, fail-closed identity gate and transactional restore pass automatically. A solver action becomes `APPLIED` only after current-generation shared-memory requested/forwarded readback; every accepted receipt is durably recorded as `FLEX_ADAPTIVE_APPLIED` with requested/effective values and ownership generations. FleX remains entirely user-owned: the current Steam-started runtime test captured `PhysXLevel=0`, recorded `FLEX_USER_SETTING_PRESERVED`, installed no FleX hook, produced no FleX `APPLIED` receipt and restored `PhysXLevel=0`. Historical user-enabled laboratory evidence is not used to justify enabling FleX automatically. | PASS (AUTOMATED/DISABLED-STATE LIVE/RESTORE) |
| Windows DPI 100-200% | Layout, DPI transitions and deterministic captures are automated. The current machine reported two active monitors at the default per-monitor DPI value; the 125-200% physical scale matrix is not available in this session. | PARTIAL CURRENT 100%; BLOCKED SCALE MATRIX |
| Multi-monitor and mixed DPI | Geometry and policy tests exist and Windows reported two active monitor identities. Live cross-monitor placement could not be completed because the Windows-control helper twice failed before state capture with `foreground window did not report a process id`; mixed-DPI hardware remains unavailable. | PARTIAL CURRENT HARDWARE; BLOCKED LIVE CONTROL/MIXED DPI |
| HDR | No authoritative automated HDR visual comparison exists | BLOCKED EXTERNAL HARDWARE |
| Multi-GPU | Adapter/LUID selection is implemented; a second physical adapter is required | BLOCKED EXTERNAL HARDWARE |
| No Windows system sounds | Sound controls and callbacks are removed; the Release PE is tested to contain no `PlaySoundW`, `MessageBeep` or Windows sound aliases. `winmm` is retained only for the high-resolution animation timer. | PASS (AUTOMATED) |
| Defender/CFA/WDAC/HVCI | Fail-closed paths exist. On 2026-08-20 the current package launched with Microsoft Defender real-time/behavior/tamper protection active and with VBS/HVCI kernel code-integrity enforcement active. Controlled Folder Access was disabled and user-mode code-integrity enforcement was not active, so those variants remain external. | PARTIAL PASS CURRENT DEFENDER/HVCI; BLOCKED CFA/USERMODE WDAC |
| Code signing | No user-owned signing certificate was supplied | BLOCKED EXTERNAL CREDENTIAL |
| Final gameplay acceptance | The prior candidate completed official Biotics Lab offline Survival, Normal, Short through all four waves and the boss with firearms, explosives, grenades, gore, gibs and heavy corpse load; live FPS/AVG/1% low/frame time/CPU/GPU rendered in gameplay, F10 hid/restored the overlay, and a second protected launch/exit restored cleanly. Previous live-tested package EXE `1BDEBCB89C21678FCEB83A5A34DAEAED509D4C2569E21960160A46E84F4A2595` separately proves automatic FleX Gibs-and-Fluids activation, 8,785 relayed solver updates, live aggregate Zed/corpse telemetry, 18 applied corpse runtime-budget receipts and exact runtime/module/16-INI restore on the same official map. The newly packaged distance/density physics policy has not yet received that live gameplay proof. | PASS PRIOR CORE LIVE PATHS; PENDING NEW CORPSE-POLICY ISOLATION |

## Single final gameplay script

Run this once only after the final package hash is fixed:

1. Start the packaged `KF2Optimizer.exe`; confirm KF2 detection and no recovery
   error.
2. Start KF2 through the app. Confirm that the Adaptive protected
   automatic plan is reported; the preview remains optional for inspection.
3. Use an official Tripwire map in offline Survival, Normal, Short. Do not use
   the known faulty Remilly test map.
4. Verify F10, all four overlay corners, FPS, AVG, 1% low, frame-time graph,
   CPU/GPU and Alt-Tab/minimize/cover behavior.
5. Complete all four waves with firearms, explosives, grenades, melee,
   simultaneous kills, Zed Time, gore, ragdolls and a boss if offered.
6. Judge FleX/blood/gib motion and lifetime visually under high load. With the
   corpse pool full, confirm excess bodies disappear individually rather than
   as one cleanup batch. Keep several active ragdolls visible and create real
   frame-time pressure: every currently eligible distant body may settle in the
   same linear Distance-Sleep pass before distant mesh detail is reduced. Return
   to an optimizer-settled corpse and verify that its cosmetic physics remains
   asleep without a proximity-wake cycle. Turn away from living enemies and
   move behind solid walls; they must
   reappear immediately and correctly through KF2's native culling when visible
   again. Current death animations, fast bodies and living enemies remain
   untouched, and no cleanup/load-control action occurs during Zed Time.
7. Exit KF2 normally and export diagnostics; confirm automatic exact INI restore.
8. Start KF2 once more to the main menu, exit, and verify the protected INIs
   were restored.

Acceptance requires no KF2 or optimizer crash/hang, no stale or zero FPS while
presents are active, no missing gameplay-readable effects, no configuration
recovery error, and a verified restore. Evidence must record the exact final
EXE SHA-256 and may not be reused after another build.
