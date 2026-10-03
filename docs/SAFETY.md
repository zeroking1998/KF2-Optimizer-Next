# Safety Model

## Permanent gameplay boundary

KF2 Optimizer Next must not modify damage, health, weapons, ammunition, enemy
AI, spawning, movement rules, networking, matchmaking, scoring, progression, or
other competitive gameplay. Protected runtime work is limited to telemetry and
cosmetic corpse/FleX behavior.

## Truthful state

The UI and logs distinguish four states:

- **Native:** observed KF2 or Windows behavior.
- **Proposed:** a bounded action selected by optimizer policy.
- **Applied:** a matching provider receipt or verified readback exists.
- **Unavailable:** the action cannot be proven safe and effective.

Timeouts and missing acknowledgements never become success.

The application commits its unclean-session marker only after runtime and window
initialization succeed. Failed runtime/window initialization preserves prior bytes or
absence; genuine prior interruption evidence is still reported on the next start.

Adaptive quality recovery requires a usable compute signal and memory evidence.
VRAM needs finite nonnegative usage and a finite positive budget. RAM needs a
valid RAM or commit pair, or an explicit finite paging signal in [0, 1]. Partial
tuples and process-private bytes alone never establish available memory headroom.

Adaptive uses one fixed controller timing policy plus user-owned settings.
Retired named aggressiveness values are migration input only: they are removed
from portable settings and cannot silently alter runtime behavior.

## Capability before control

Every runtime control requires a known provider, verified identity, narrow
authority, an addressable target, an acknowledgement path, and restoration.
Capability is evaluated per control; one available feature does not authorize
another.

## Transactional files

Protected configuration uses explicit discovery, preview, backup, atomic write,
readback, and exact restoration. Unknown or locked values are not silently
overridden. Interrupted sessions are recoverable from portable state.

Changing the game folder requires both installations to be confirmed stopped.
The old installation and saved path remain authoritative until all protected
INI, provider, FleX and FPS-cap recovery succeeds. An idle prepared external
launch can be restored before switching; an active launch, restart, live
restoration debt or unconfirmed cleanup blocks the change. Cancelling the
picker or selecting the same folder preserves prepared state.

## Bounded runtime work

Desktop telemetry submits one resource-worker request per regular cycle;
explicit final-log flushing is separate. A verified session pins its process
handle for liveness and shares it with CPU/memory sampling when read rights
permit. Restricted sessions retain read-only observation, and changed process
identities or signaled handles cannot produce accepted samples.

Missing GPU providers retry independently on the existing resource worker,
with exponential waits of 1, 2, 4, 8, 16 and at most 30 seconds. Healthy
providers are never reconstructed for another provider's failure. Immutable
provider reports and diagnostics change only on a construction attempt;
unchanged samples do not allocate or copy provider error strings. Process or
adapter rebinds reset provider state and invalidate old publications. Missing
measurements remain unavailable, not zero load or confirmed headroom.

Runtime queues, per-tick work, action sizes, retry counts, and history are
bounded. Actor-scoped state prevents repeated sleep or wake requests for an
actor already confirmed in that state. Weak world ownership avoids keeping a
finished KF2 world alive through telemetry state.

## Slow motion and scene context

Zed Time is explicit input to corpse-physics policy. Distance and visible scene
density are primary signals; FPS pressure strengthens a decision but does not
erase actor identity or receipt requirements.

## Restoration

Normal shutdown and recovery restore protected INIs, telemetry modules and
sources, optional FleX runtime state, the native viewport client, and temporary
session files. The pre-session snapshot is authoritative.

FleX transactions pin the original runtime directory's Windows volume and file
identity before changing game files. Restore and recovery reject a different
installation, including one with the same DLL hashes. Legacy or missing owner
markers do not authorize writes or evidence cleanup; originals and backups are
retained for explicit recovery. These checks run at transaction boundaries,
not in the solver's per-frame path.

Multi-file configuration rollback attempts every written target and verifies
the exact original bytes. An incomplete rollback or journal completion reports
recovery required and retains its durable recovery state. Native FPS-cap
changes record both originals before mutation, bind recovery to the verified
installation, and address only their two fixed files. Recovery preserves
conflicting edits and runs only with KF2 stopped, before protected session
restoration. Blocked cap recovery also retains the protected session snapshot.

Failed pre-launch preparation checks restoration before reporting safe rollback.
Unconfirmed restoration preserves the snapshot, marks recovery required and
instructs the user not to start KF2 until Repair verifies protected state.
Graphics changes that temporarily dismantle a prepared launch also check its
re-preparation; a second failure is reported separately and requires Repair.

Security issues that could cross these boundaries should follow
[SECURITY.md](../SECURITY.md).
