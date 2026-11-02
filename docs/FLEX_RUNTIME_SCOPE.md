# Optimized FleX runtime scope

KF2's FleX choice remains owned by the user:

- **Off:** no runtime replacement is prepared and FleX stays disabled.
- **Gibs / Gibs and fluids:** the protected launch reconstructs the verified
  optimized Core and Extension from the exact installed KF2 FleX files, then
  installs the optimized pair and its project-built helper as one reversible
  transaction.

The optimizer never enables FleX, does not write `MaxPhysicsSubsteps`, and no
longer clamps solver substeps in a relay. Solver cadence, fixed-time stepping,
presentation interpolation and safe overlap are owned by the verified optimized
runtime itself. This prevents two independent controllers from rewriting the
same call.

The small Core relay is observation-only. It forwards the native arguments
unchanged, keeps the optimized Core loaded as `flexRelease_original.dll`, and
publishes bounded health and optional diagnostic counters. The normal hot path
does not perform Adaptive decisions, heartbeat checks, logging or file I/O.

The optimized Core/Extension binary recipes are accepted only for the exact
stock KF2 hashes and their output hashes are verified before installation.
An already staged output with the exact verified target hash is reused; changed
or incomplete output is reconstructed and atomically replaced.
NVIDIA and game DLLs are not stored in the repository. The helper is built from
project source. Installation requires KF2 to be stopped, verifies every source,
uses staged atomic replacement, records installation ownership, and retains
verified stock recovery copies. Game exit restores the exact stock Core and
Extension and removes the known helper. Existing unknown files are never
deleted. Startup recovery also understands the previous schema-3 relay journal
and removes it safely before preparing the optimized runtime.

Detailed FleX diagnostics are off by default and can be enabled from the Debug
tab. Only that mode records min/max input and forwarded substeps, solver and
particle statistics, transfers, bounds, parameters, detailed shared-memory
readback, the session report and additional summary logging. With diagnostics
off, the relay retains only the minimal bounded health evidence needed to detect
a broken native call.

KF2's shipped FleX solver is CUDA/GPU based; the optimizer does not claim or
provide a CPU solver. CPU submission, transfer, synchronization, presentation
and GPU scheduling can all affect frame time. The optimized runtime may reduce
or overlap that work, but it does not infer living-enemy or corpse semantics
from raw solver pointers. Corpse lifetime/count settings and living-enemy
gameplay physics remain separate.

The release test `kf2_flex_forwarder_passthrough_test` verifies exact substep
pass-through, lock-free solver updates, bounded lifecycle observation, optional
diagnostics, warm export caching and native-completion publication. The FleX
transaction and binary-delta tests verify exact reconstruction, all-component
installation, rollback, owner binding, legacy recovery and byte-exact restore.
A fresh gameplay run is still required for each newly packaged helper binary.
