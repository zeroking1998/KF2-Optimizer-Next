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

## Capability before control

Every runtime control requires a known provider, verified identity, narrow
authority, an addressable target, an acknowledgement path, and restoration.
Capability is evaluated per control; one available feature does not authorize
another.

## Transactional files

Protected configuration uses explicit discovery, preview, backup, atomic write,
readback, and exact restoration. Unknown or locked values are not silently
overridden. Interrupted sessions are recoverable from portable state.

## Bounded runtime work

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

Security issues that could cross these boundaries should follow
[SECURITY.md](../SECURITY.md).
