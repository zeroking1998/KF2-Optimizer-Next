# Optimized original FleX integration (draft)

Tracking: [issue #939](https://github.com/zeroking1998/KF2-Optimizer-Next/issues/939).
Planning baseline: main `07a853ac94979bf64ed39302a1e29195d603b640`.

This is an integration plan, not a shipped feature. No runtime, game DLL,
configuration or Windows behavior changes in this draft.

## Already verified together

The cumulative original-FleX candidate contains the earlier P4 base and all
six intended additions below. Fresh read-only reconstruction reproduces both
images byte-for-byte. Its Core/Extension identities are:

- Core: `EC0065DAEF13C53A449D6FD9A07739F230B2B2DFE64DC2A4EC2DFBF19E06C4B0`.
- Extension: `1FDA92BFA883BC7C98A6EF4634E43019830DA4EE297A06A1AB20B2DDFCFA7FE7`.

Existing CPU contract checks and real native GPU comparisons used this combined
pair, not just isolated source changes. The two sort comparisons contain
128 scene rows / 64 output-matched pairs in total, with the complete cumulative
candidate loaded as B. Selected solid/fluid scenes and native sorting were
exercised. This does not cover every specialized branch, KFGame rendering,
indirect buffer consumers, asynchronous lifetime or packaged gameplay.

The most recent gameplay capture used the earlier P4 pair
(`F696AE...` / `ECA8A2...`), not the cumulative pair above. Keep that distinction:
the prior combined checks remain valid and do not need to be discarded.

## Intended stack

| Layer | Included work |
| --- | --- |
| P4 base | Seven rigid-buffer capacity reuse; reduced solver cadence and iteration count; gated transfers and collision refresh; density caching; positive-phase particle-pair filtering |
| Lifecycle commits | Coalesce pending changes on the existing cadence; preserve uncommitted host slots |
| Idle uploads | Suppress clean-empty Push work |
| Upload prefixes | Transfer the bounded required prefix without truncating sparse allocated slots |
| Uniform contacts | Use the owner-bound identical-positive-phase specialization only when certified |
| Fluid grid reset | Remove the duplicate selected grid clear |
| Particle sorting | Sort the actual 21-bit keys without discarding valid key bits |

P4 intentionally changes physics cadence/convergence and some particle collision
behavior. Visible effects and particle counts are retained; identical per-frame
physics is not promised. Required completion fences and readback ownership
remain intact.

Do not include the slower readback-tail reorder, unqualified experiments,
FleX 1.2, speculative wait removal, or the global Windows MPO workaround.

## Small integration boundary

Extend the existing FleX preparation and recovery path instead of introducing a
second loader, solver framework, background service or profile system.

1. Accept only the exact supported user-installed original Core/Extension pair
   and matching runtime dependencies. Do not merely whitelist arbitrary edited
   DLLs to bypass the current stock-Core audit.
2. Prepare the matching optimized pair locally using one deterministic,
   versioned recipe; verify the final hashes before any installation.
3. Reuse the existing relay and Debug facilities without adding another proxy
   layer or duplicate actuation. Extend the existing installation-bound
   transaction to cover both files and their exact pre-session recovery copies.
4. Stage both files first. If either replacement or final verification fails,
   prevent launch and recover the previous pair. A file replacement is atomic;
   two separate DLL replacements are not collectively atomic.
5. Recover interrupted transactions before another protected Steam launch.
   Never patch a running KF2 process or reprepare images in a per-frame loop.
6. Keep the initial optimized mode explicitly experimental and offline opt-in,
   independent of Adaptive. FleX remains off when the user selected Off.
7. Preserve default-off detailed Debug counters. Distinguish verified preparation,
   loaded identities, forwarded update calls and completed solver steps; do not
   turn a request or skipped update into a falsely confirmed applied result.

One manifest must bind the recipe version, original identities, resulting pair
and portable build. Package/update validation must not mix artifacts from two
builds. Follow [Binary Distribution](BINARY_DISTRIBUTION.md) and
[Third-Party Notices](../THIRD_PARTY_NOTICES.md): no game/NVIDIA DLLs, raw private
captures or private benchmark sources in this PR or its source history.

## Remaining acceptance

- [x] Current-main seams and missing Extension ownership identified.
- [x] Cumulative candidate identities and reconstruction freshly verified.
- [x] Prior combined CPU/native-GPU evidence identified, with its scope retained.
- [ ] Local recipe and matched-pair installation implemented.
- [ ] Existing tests extended for ABI/exports, mixed or unsupported input,
      interruption between replacements, process-running rejection and exact restore.
- [ ] Portable package identity, update/recovery and Debug receipts verified.
- [ ] One complete packaged offline session confirms loaded identities, effects,
      map changes, teardown and byte-exact restoration.
- [ ] Online compatibility qualified separately before enabling this mode online.

Prefer extensions to existing tests over a new framework. Standalone timing is
not gameplay FPS, and byte reconstruction is not a gameplay stability pass.
This planning PR must remain draft until integration and required boundary
checks are complete; documentation alone must not close issue #939.
