# Optimized original FleX integration

Tracking: [issue #939](https://github.com/zeroking1998/KF2-Optimizer-Next/issues/939).
Implementation baseline: main `07a853ac94979bf64ed39302a1e29195d603b640`.

KF2 Optimizer Next reconstructs the accepted optimized runtime locally from the
user's exact stock FleX files. NVIDIA and game DLLs are not stored in the source
tree or portable package.

## Accepted runtime

The integrated Core/Extension pair is the last private offline variant that
completed the native checks and a visible-effect gameplay smoke test:

- Core: `DDE8343CF0226D25CD10610F41E2FC3E0C4795B8ECB98399668FE834AEE340E5`.
- Extension: `5ADDDFBD8FDBB7F98413936CAB6ECC9EF631BB39021144A277400C1A3987FC8A`.

It contains the established P4 buffer/transfer/collision work, the accepted P3
fixed-time cadence with two native substeps and one constraint iteration, and
rigid presentation interpolation. Fluid particles and normals remain canonical.
The helper source is the same source used by that private variant and is built
reproducibly as part of the project.

The earlier cumulative candidate (`EC0065DA...` / `1FDA92BF...`) remains useful
historical evidence for its isolated optimizations, but it is not the runtime
installed by this integration. Mixing either pair is rejected.

The private gameplay smoke on 16 October 2026 loaded the accepted pair, showed
Gib/fluid FleX effects and produced no new KFGame crash dump. It was not a
controlled FPS comparison. The final packaged helper still requires one visual
gameplay check before release promotion.

## Runtime boundary

- FleX activation remains owned by the user. `PhysXLevel=0` is never enabled or
  replaced by the optimizer.
- The optimized runtime is independent of Adaptive state and pressure. The
  observation relay forwards the native substep request unchanged.
- Both exact stock identities are verified before reconstruction. The rebuilt
  outputs are hash-verified before installation.
- Core, Extension, helper and observation relay are installed as one
  owner-bound transaction while KF2 is stopped.
- Exact stock Core/Extension files and any pre-existing helper are backed up and
  restored after the session. Partial installation, interruption and the legacy
  schema-3 journal recover through the same path.
- Unknown, mixed or changed files fail closed and are not overwritten.
- Repeated preparation reuses already verified staged outputs. It performs no
  per-frame reconstruction, polling, logging or additional worker activity.
- Detailed FleX counters and reports stay opt-in under Debug.

The optimized helper uses no extra worker thread or host-buffer copy loop. Its
solver-owned synchronization resources are created once, reused and released
on teardown. Push and Pull transfers use the allocated prefix through the
highest sorted active particle ID instead of copying unused capacity. Pull
temporarily narrows all four host views together, restores their exact bounds
on normal and abnormal exit, and keeps the synchronous normals completion
boundary. Invalid or unfamiliar layouts retain the original full transfer.
Necessary completion and error checks remain in place.

## Distribution and proof

The deterministic recipes, project helper and transaction code are included in
the normal build. Follow [Binary Distribution](BINARY_DISTRIBUTION.md) and
[Third-Party Notices](../THIRD_PARTY_NOTICES.md): do not add reconstructed DLLs,
raw private captures or private benchmark sources to the repository.

Fresh local verification covers:

- byte-exact reconstruction of both accepted outputs from the exact stock pair;
- pass-through behavior and all 52 Core exports;
- unsupported/mixed input, partial replacement, rollback, owner-bound recovery,
  legacy-journal recovery and byte-exact restore;
- Debug and Release suites plus two independent deterministic Release builds;
- clean portable-package shape, source identity and managed payload hashes.

## Remaining acceptance

- [x] Exact recipe and matched-pair installation implemented.
- [x] ABI/exports, mixed or unsupported input, interrupted replacement,
      running-process rejection and exact restore tested.
- [x] Portable package identity, update/recovery and Debug receipts verified.
- [x] Private accepted pair exercised offline with visible FleX effects.
- [ ] Final portable build exercised once offline with its freshly built helper.
- [ ] Online behavior qualified separately before it is claimed as verified.

Standalone timing is not gameplay FPS, and byte reconstruction alone is not a
gameplay stability pass. Keep the pull request in draft until the final package
gate is completed.
