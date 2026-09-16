# Fixed minimum offline FleX scope

KF2's FleX choice remains owned by the user:

- **Off:** no runtime hook is installed and FleX stays disabled.
- **Gibs / Gibs and fluids:** the verified runtime hook requests one solver
  substep after its observation warmup.

The fixed request is independent of Adaptive mode, target FPS, frame pressure,
resource pressure and visible-enemy pressure. Adaptive never enables FleX and
never raises or lowers its solver level.

Every solver is observed unchanged for 180 update calls first. Control is bound
to the exact KF2 PID and process start time and must be refreshed within 1.5
seconds. Missing or stale control, an unknown solver, tracker saturation, lock
contention, invalid values or an app crash immediately preserve the game's
original argument. Particle counts and capacity remain telemetry only; they do
not imply a writable particle-budget, spawn, lifetime or fluid/non-fluid
actuator.

KF2's shipped FleX solver is CUDA/GPU based; the optimizer does not claim or
provide a CPU solver. CPU-side submission, transfer and synchronization can
still affect frame time, but those measurements do not change the fixed
one-substep request.

`flexUpdateSolver(solver, deltaTime, substeps, timers)` contains no semantic
label for a living enemy, an active corpse or an inactive corpse. The runtime
therefore does not guess addresses or assign different values to those groups.
Corpse lifetime/count settings remain separate and reversible; living-enemy
gameplay physics is protected.

The release test `kf2_flex_forwarder_fixed_minimum_test` loads the actual built
forwarder against an isolated original-DLL test double. It verifies that the
first 180 calls remain unchanged, call 181 accepts fresh valid control, an
expired heartbeat immediately restores the original argument, and Off remains
pass-through. Exact shared-memory readback is required before the app reports
`FLEX_MINIMUM_APPLIED`.
