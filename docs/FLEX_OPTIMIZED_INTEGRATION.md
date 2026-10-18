# Optimized original FleX integration

Tracking: [issue #939](https://github.com/zeroking1998/KF2-Optimizer-Next/issues/939).
Implementation baseline: main `1db568139e5a8da2343a2b8c45873d5858455633`.

KF2 Optimizer Next reconstructs the accepted optimized runtime locally from the
user's exact stock FleX files. NVIDIA and game DLLs are not stored in the source
tree or portable package.

## Accepted runtime

The current Core/Extension pair derives from the last private offline variant
that completed the native checks and a visible-effect gameplay smoke test:

- Core: `DDE8343CF0226D25CD10610F41E2FC3E0C4795B8ECB98399668FE834AEE340E5`.
- Extension: `CF5A98B007CAC5DD3FD5E198AB373F152957B79AC5ACC9E101378C329752C325`.

It contains the established P4 buffer/transfer/collision work, the accepted P3
fixed-time cadence with two native substeps and one constraint iteration, and
rigid presentation interpolation. Fluid particles and normals remain canonical.
The project helper additionally contains the exact-image DX11/CUDA render
bridge described below. It builds reproducibly, but this newer helper and the
current Extension still require fresh packaged gameplay validation.

The Extension initializes its opaque particle free list so the unchanged
last-in-first-out allocator assigns low contiguous IDs first. NVIDIA's API does
not promise an allocation order, and the Extension maps each asset particle
through the returned ID, so this changes neither particle identity nor visible
simulation behavior. For favorable allocation histories it lets the existing
highest-active-ID transfer boundary cover a small active prefix instead of
almost the entire reserved capacity. A retained high ID still requires the
full prefix; live-particle count alone does not determine transfer size.

The earlier cumulative candidate (`EC0065DA...` / `1FDA92BF...`) remains useful
historical evidence for its isolated optimizations, but it is not the runtime
installed by this integration. Mixing either pair is rejected.

The private gameplay smoke on 16 October 2026 loaded the predecessor Extension,
showed Gib/fluid FleX effects and produced no new KFGame crash dump. It was not
a controlled FPS comparison. The low-ID allocation update has CPU-level binary
and allocator proof but still requires one visual packaged gameplay check before
release promotion.

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
boundary. A clean empty container reads one inactive slot instead of its full
reserved capacity; pending births and constraint changes retain the full Pull.
The known extension Pull also avoids reading immutable phase IDs back
from the solver; direct phase queries remain unchanged. Invalid or unfamiliar
layouts retain the original full transfer.
Necessary completion and error checks remain in place.

The exact stock Pull calls only the verified getters in sequence.
When its issuing thread already owns the solver's CUDA context, the helper
validates that context once for the complete Pull instead of repeating the same
driver query for every routed payload and the render snapshot. This reduces the
helper's duplicate context queries from six to one when rigid transforms are
present (four to one without them). A different issuing context keeps every
original per-copy validation and disables this shortcut; copy errors,
completion and teardown behavior are unchanged.

The same exact Pull also validates its immutable thread, commit marker and
generation contract once. Routed payloads then use the solver-owned token while
still checking the native error and ready generation before every copy. With
rigid transforms, this reduces the full pull-invariant eligibility validation
from seven executions to one. A native error immediately returns subsequent
payloads to the original stream, and the token is cleared on every normal,
abnormal and readiness-invalidating exit.

For the exact supported KFGame image, an optional DX11/CUDA bridge snapshots
canonical solver positions and packs each solver normal once into KF2's exact
32-bit vertex format on the GPU. The existing dynamic vertex buffer then reuses
those packed normals instead of repeating KF2's per-resource conversion.
Mesh-local indices are composed once with the owning
FleX instance's particle IDs. Attachments, unfamiliar layouts, missing or stale
snapshots, unsupported mappings and every API error keep KF2's original upload.
The bridge registers and caches only resources that pass exact identity and
bounds checks, and releases them before the matching RHI resource or scene
proxy is destroyed. Snapshot readiness and source-buffer reuse are ordered with
CUDA events between the solver and render streams. The normal path therefore
does not append the device-to-device snapshot to KF2's host-waited readback
stream or add a CPU synchronization. API errors, an unexpected CUDA context
and teardown still drain the relevant GPU work synchronously before any source
or registered resource can be changed or released. If a render upload is already
using a snapshot slot, the non-blocking handoff keeps KF2's original upload for
that generation instead of stalling the solver thread or reusing the slot. The
snapshot covers only the highest particle ID required by a registered render
resource, and optional registration, publication and upload lock handoffs never
wait behind concurrent bridge work.

Rigid presentation keeps two owned history buffers. Their pointers rotate
instead of copying the old current transforms into the previous buffers. A
topology reset needs only the current sample because interpolation remains
disabled until the next commit; that next rotation preserves it before the
other buffer is overwritten. Topology is compared directly with the stable
container IDs instead of copying a work array first. This removes four CPU
copies totalling exactly 68 bytes per active rigid and commit, plus four bytes
of reserved work storage per capacity slot, without changing the samples,
interpolation or allocation lifetime.

For the explicitly modelled sparse path of 512 live particles in the observed
26,746-slot container, low contiguous IDs reduce Push bytes by 98.0857%, Pull
bytes by 98.2452%, and combined Push/Pull bytes by 98.1655% versus full-capacity
copies. This is transfer-byte accounting, not a gameplay-FPS claim. The
percentages cover host/device Push/Pull particle payload only. They exclude
the render bridge's render-bounded position and packed-normal snapshots, GPU
vertex-buffer writes, one-time index upload, reserved snapshot memory and all
other engine or driver work.

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
- [x] Predecessor pair exercised offline with visible FleX effects.
- [x] Current low-ID allocator, representative high-ID counterexample and exact
      binary reconstruction verified on CPU.
- [x] GPU vertex pack matched KF2's SSE result bit-for-bit for 65,549 indexed
      finite and exceptional test vertices on the current RTX driver.
- [ ] Final portable build exercised once offline with its freshly built helper.
- [ ] Online behavior qualified separately before it is claimed as verified.

Standalone timing is not gameplay FPS, and byte reconstruction alone is not a
gameplay stability pass. Keep the pull request in draft until the final package
gate is completed.
