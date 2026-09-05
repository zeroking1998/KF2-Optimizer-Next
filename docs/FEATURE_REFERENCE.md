# Feature Reference

| Feature | Purpose | Confirmation |
|---|---|---|
| Game discovery | Finds supported KF2 paths and identity | Unambiguous detected paths |
| Performance telemetry | Measures FPS, averages, 1% lows, frame time, CPU, GPU, and memory | Timestamped samples |
| Adaptive controller | Selects bounded quality pressure from performance and scene evidence | Decision record, then action receipt if applied |
| Configuration preview | Shows planned INI changes without writing | Preview diff |
| Transactional apply | Backs up, writes, and verifies supported settings | Verified readback |
| Session restoration | Restores the exact protected pre-session state | Restore report and integrity check |
| External overlay | Displays selected telemetry outside the game process | Visible game-bound window and current sample |
| Corpse ceiling | Sets the maximum permitted dead-body count | Configuration readback and runtime capacity receipt |
| Distance Sleep | Sleeps a sufficiently distant corpse actor | Matching actor ID and `state=sleep` receipt |
| Near Wake | Wakes a nearby previously slept corpse actor | Matching actor ID and `state=wake` receipt |
| Ragdoll Sleep | Stops redundant active ragdoll simulation | Matching actor ID and Ragdoll Sleep receipt |
| Scene-density control | Reduces active cosmetic work when many relevant actors are visible | Scene level plus confirmed actor/capacity actions |
| FleX adaptive control | Adjusts a protected solver level when the laboratory is available | `FLEX_ADAPTIVE_APPLIED` readback |
| Debug markers | Correlates corpse actions and visible living-Zed distances with metre values and Actor IDs | Optional next-session in-game markers plus session log |
| Diagnostics | Explains provider, identity, restore, and evidence status | Explicit checks and exported reports |

## Important distinctions

### Corpse visual protection

New corpse LOD reductions require at least 1,000 units (10 m). At 800 units
(8 m) or closer, the optimizer restores its captured visual LOD constraint,
including for aged corpses, without waking or releasing frozen physics.
Skeleton reductions already applied to a corpse survive optimizer wake actions.
The original `bNoSkeletonUpdate` and `bSkipAllUpdateWhenPhysicsAsleep` values
are captured once per full corpse ID, not rewritten after every wake. Disabling
Adaptive restores flags still matching the optimizer's changes; external changes
are left alone. Visible model LOD can still recover near the player independently.
Stock KF2's wake callback explicitly clears `bNoSkeletonUpdate`. While a corpse
still uses rigid-body physics, the optimizer lets that callback finish and then
restores only its owned skeleton reduction once per observed wake transition.
KF2's native collision, warning, shadow and component behavior remains active.
Unrelated code can still change flags. Retain/release receipts and observed-wake
flag readbacks expose the actual values.

The final freeze stage uses the latest complete combined frame, scene and enemy
pressure sample. It accepts only an already sleeping corpse moving no faster
than 400 units per second. Low pressure requires 15 seconds and 10 m; medium
pressure requires 10 seconds and 8 m; high pressure requires five seconds and
5 m. It then verifies `PHYS_None`, so the corpse no longer has rigid-body
physics that KF2 could wake. No wake count is required and no new skeleton
reduction is applied after this transition. At most one freeze runs per 250 ms.
Adaptive-off restores the owned rigid-body state.

New aging/skeleton actions are also blocked within 8 m. Other corpse physics
controllers keep their own safeguards; this is not a global physics exclusion.

Unchanged effective LOD targets produce no write or applied receipt. Tracking
uses the full corpse lifetime ID. A changed readback emits `external_reset`
with expected/observed values and an unknown writer, then defers reapplication
for two seconds. The external value becomes the restoration baseline, not an
optimizer-owned write. A later verified reapplication names that reset reason.
These receipts verify properties, not the mesh detail actually rendered.
Rejected writes also wait two seconds before retrying and never count as
applied. The visible selector checks a fixed four-candidate shortlist to keep
ownership lookups linear in pool size; if all four are deferred, that pass
makes no visible LOD change. The independent aging cursor still progresses.

### Adaptive frame pressure

Offline quality decisions wait for a fresh gameplay-provider sample after
loading or a telemetry interruption. Pre-boundary frames are excluded from
the next controller window. An unknown connection type also pauses decisions
until KF2 confirms the session type; it is not treated as online gameplay.

Moderately low 1% lows alone do not keep lowering quality while live and
average FPS are at the target. They must persist and be supported by current
frame-time, FPS, or stutter evidence. Substantially worse sustained tails can
still trigger correction on their own. Real FPS drops and memory pressure
remain actionable; holding a target cannot be guaranteed on every scene.

### Corpse telemetry transitions

After a confirmed corpse readback, a temporary missing field or telemetry gap
shows `STALE` and preserves only the last confirmed runtime limit for up to ten
seconds after the gap is detected. Repeated gaps or map changes do not extend
that deadline. Long evaluation pauses are measured against the existing
15-second telemetry freshness limit, not a newly started grace period.

The cached limit is display-only. App quality/corpse decisions and fallback selection
pause during this grace period; cached values never produce an APPLIED receipt.
This does not stop KF2's own simulation, its already-running provider, or the
separate FleX controller using its own telemetry.
Expiry clears the displayed value to `UNAVAILABLE`. A newer valid readback
restores `AVAILABLE`; process changes, provider replacement and rejected session
boundaries discard the old cache. State events are emitted once per transition.

### Quality-response diagnostics

`ADAPTIVE_QUALITY_RESPONSE` compares a five-second pre-request window with a
five-second window starting one second after an authenticated APPLIED receipt.
Late telemetry batches get up to one second to fill each fixed event-time
window. The baseline is reread over its original pre-request bounds; the
post-action window is not moved or shortened. Data still missing at the
deadline makes the comparison inconclusive, without delaying Adaptive actions.
Average FPS, p95 frame time and 1% low use the same window, unlike the overlay's
different rolling windows. The log includes frame counts and actual coverage;
incomplete windows, telemetry gaps, session/GPU/target changes, Zed Time,
substantial observed scene-count changes or another quality action invalidate
the comparison. No result is emitted for an unconfirmed action.

Results are `improved`, `worsened`, `mixed`, `no_clear_change`, or `inconclusive`
with a reason. A 5% metric change is the reporting threshold, not statistical
significance. Scene checks use visible living Zeds and total corpse counts;
they cannot establish identical camera views or effects. Results are therefore
observational, explicitly not proof that the quality change caused the result.
This diagnostic never changes or delays Adaptive actions.

The controller separately spaces ordinary quality changes at least seven
seconds after the previous authenticated APPLIED receipt: one second to settle,
five seconds to observe, and one second for telemetry delivery. This applies
to reductions and recovery, even with diagnostic logging disabled. Emergency
corrections retain the one-second fresh-frame guard. Scene changes or emergency
follow-ups can still make a diagnostic comparison inconclusive.

### Required-file repair

The upper-right **Repair** action is available even in Safe
Mode. It derives an exact tag and asset name from the installed version, never
from a latest-release alias, and performs the download on a background worker.
HTTPS host restrictions, a 64 MiB archive limit, isolated extraction, build
identity, executable identity, safe single-link file identity, SHA-256 values,
atomic writes and a final full-package audit are mandatory.

`Import Local Package` provides the same verification path without network
access. A different executable, build, or release is rejected.

- A selected setting is not proof that KF2 accepted it.
- A decision is not proof that an action ran.
- Native KF2 state is not an optimizer action.
- An optimizer action is confirmed only by its matching receipt or readback.
- Unavailable is a safe capability result, not a hidden fallback.

The exhaustive implementation matrix remains in
[ISSUE_72_PRODUCT_MATRIX.md](ISSUE_72_PRODUCT_MATRIX.md).
