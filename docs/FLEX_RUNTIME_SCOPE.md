# Native one-substep FleX scope

KF2's FleX choice remains owned by the user:

- **Off:** no runtime hook is installed and FleX stays disabled.
- **Gibs / Gibs and fluids:** the protected launch sets KF2's native
  `MaxPhysicsSubsteps=1` ceiling. The verified runtime hook clamps only a rare
  solver request above one that bypasses the native setting.

The fixed request is independent of Adaptive mode, target FPS, frame pressure,
resource pressure and visible-enemy pressure. Adaptive never enables FleX and
never raises or lowers its solver level.

The hot update path has no warmup, heartbeat, solver-lock lookup or adaptive
decision. It preserves values at or below one and converts only values above
one to one before calling the original FleX runtime. The hook is installed only
for user-enabled FleX and is restored after the protected session. Particle
counts and capacity remain telemetry only; they do not imply a writable
particle-budget, spawn, lifetime or fluid/non-fluid actuator.

Solver creation, destruction and diagnostics-only active-count observations
are rare and serialize in fixed storage so transient lock contention cannot
drop an exact solver identity. Unknown identities or exhausted fixed storage
remain permanently quarantined for the session. None of this adds locking to
the update path.

Detailed FleX diagnostics are off by default and can be enabled from the Debug
tab. Only that mode records min/max substeps, per-call solver/particle transfer
statistics, detailed shared-memory readback, the session report and additional
summary logging. The fixed clamp and minimal failure/readback counters remain
active independently of diagnostics.

KF2's shipped FleX solver is CUDA/GPU based; the optimizer does not claim or
provide a CPU solver. CPU-side submission, transfer and synchronization can
still affect frame time, but those measurements do not change the fixed
one-substep request.

`flexUpdateSolver(solver, deltaTime, substeps, timers)` contains no semantic
label for a living enemy, an active corpse or an inactive corpse. The runtime
therefore does not guess addresses or assign different values to those groups.
Corpse lifetime/count settings remain separate and reversible; living-enemy
gameplay physics is protected.

The release test `kf2_flex_forwarder_fixed_minimum_test` loads the same
forwarder source with test-only lock-control exports against an isolated
original-DLL test double. It verifies that updates remain lock-free while rare
lifecycle and diagnostics observations wait and recover exactly. It also
verifies immediate one-substep clamping, independence from legacy heartbeat
and control values, and exact preservation of values at or below one. Exact
shared-memory readback is
required before the app reports `FLEX_MINIMUM_APPLIED`.
