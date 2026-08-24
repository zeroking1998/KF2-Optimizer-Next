# Adaptive offline FleX scope

The portable offline laboratory offers three explicit modes:

- **Off / control 0:** the original game substep argument is forwarded unchanged.
- **Auto / controls 1-5:** a hysteresis policy starts at 5, reduces one step
  under sustained pressure against the exact target-relative corrective band,
  and recovers one step only after five seconds inside the recovery band.

Every solver is observed unchanged for 180 update calls first. Control is bound to
the exact KF2 PID and process start time and must be refreshed within 1.5 seconds.
Missing/stale control, an unknown solver, tracker saturation, lock contention,
invalid values or an app crash immediately preserve the game's original argument.
Particle counts and capacity remain telemetry only; they do not imply a writable
particle-budget, spawn, lifetime or fluid/non-fluid actuator.

KF2's shipped FleX solver is CUDA/GPU based; the optimizer does not claim or
provide a CPU solver. CPU-side submission, transfer and synchronization can still
become the measured frame bottleneck. Therefore adaptive FleX substep control may
respond to confirmed CPU, GPU or VRAM pressure, but only while current frame
pressure and live FleX readback are both present. RAM pressure alone never selects
the FleX actuator.

`flexUpdateSolver(solver, deltaTime, substeps, timers)` contains no semantic label
for a living enemy, an active corpse or an inactive corpse. The old GitHub code
also tracked only opaque solver pointers; it did not prove such a classification.
Consequently the new implementation does not guess addresses or silently assign
different values to those groups. Corpse lifetime/count settings remain separate,
reversible KF2 configuration features; living-enemy gameplay physics is protected.

The release test `kf2_flex_forwarder_adaptive_test` loads the actual built
forwarder against an isolated original-DLL test double. It verifies at argument
level that the first 180 calls remain unchanged, call 181 accepts fresh valid
control, an expired heartbeat immediately restores the original argument, and
Off remains pass-through. This proves the forwarding contract without using or
risking KF2. It does not replace the final visual gameplay judgment.
