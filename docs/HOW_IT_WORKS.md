# How It Works

## The short version

KF2 Optimizer Next measures the system, decides whether a bounded adjustment is
useful, checks whether the responsible provider can prove the action, applies
only supported work, and restores session-scoped changes afterward.

```text
Windows/KF2 signals
        |
        v
Telemetry snapshot -> adaptive decision -> capability gate
                                             |
                                  unavailable|supported
                                             v
                                    bounded action
                                             |
                                     receipt/readback
                                             |
                                    UI, log, restore
```

## 1. Discovery

The application discovers the KF2 installation, user configuration, log
directory, executable identity, and optional telemetry providers. Discovery is
read-only. Ambiguous results are reported instead of guessed.

## 2. Measurement

PresentMon and protected offline telemetry provide timestamped samples such as
live FPS, average FPS, 1% low FPS, frame time, CPU/GPU load, visible living
actors, visible corpses, active corpses, distance, and Zed Time state. Every
decision records the evidence window it used.

## 3. Performance pressure

The controller combines complementary signals:

- live FPS reacts quickly;
- average FPS shows sustained performance;
- 1% low FPS exposes repeated slow frames;
- frame-time trend detects worsening behavior before a long average catches up;
- scene density and distance explain when cosmetic physics work can be reduced.

For target FPS `T`, target frame time is `1000 / T` milliseconds. Warning,
corrective, and critical bands use the same absolute one-, two-, and three-FPS
tolerance at every valid target. Hysteresis, cooldowns, bounded step sizes, and
recovery evidence prevent rapid oscillation.

## 4. Capability gate

A control is enabled only when all required parts are present:

1. the correct provider is loaded;
2. provider identity and authority are valid;
3. the requested actor or setting is addressable;
4. an acknowledgement or reliable readback is available;
5. restoration is possible.

Missing capability produces **Unavailable**, not a simulated success.

Target FPS uses KF2's own GPU-independent `bSmoothFrameRate`,
`MinSmoothedFrameRate`, and `MaxSmoothedFrameRate` controls. The protected
offline provider reapplies them if KF2's graphics menu changes Variable Frame
Rate during a session. A property readback proves that those engine values were
configured; actual PresentMon FPS remains the final runtime evidence.

## 5. Configuration actions

Configuration changes are transactional: preview, backup, write, verify, and
restore. Locked or unknown settings are filtered out. The exact pre-session
snapshot is the restoration source of truth.

The protected startup plan uses KF2's native configuration keys. It enables
`bPhysicsAsyncScene`, `bEnableAsyncScene`, and `OneFrameThreadLag`; it does not
add similarly named `r.*` console variables. When dedicated VRAM is available,
the plan chooses a conservative texture-pool tier from 160 to 6000 MB and pairs
it with a bounded `MemoryMargin` and `HysteresisLimit`. If VRAM cannot be
identified, the existing streaming values are left unchanged. These are
startup-only values and are restored from the exact pre-game snapshot.

## 6. Corpse-physics actions

The corpse maximum selected by the user is a ceiling. A separate runtime budget
uses visible density and distance first, with performance pressure as an
amplifier. Actor IDs correlate each Distance Sleep, Near Wake, or Ragdoll Sleep
request with its receipt. A per-action state machine prevents repeated work on
an actor already in the requested state. Zed Time uses protected behavior so
normal-speed thresholds are not applied blindly during slow motion.

## 7. FleX actions

FleX control is an optional, identity-gated laboratory feature. It begins in
passthrough, uses bounded levels 1 through 5 when capability is confirmed, and
reports an applied level only after matching readback. Releasing control returns
to passthrough.

## 8. Evidence and restoration

Logs distinguish native KF2 state, optimizer proposals, confirmed optimizer
actions, and restoration. On normal exit or recovery, protected files, modules,
runtime state, and temporary session data return to their recorded native state.

For component ownership, continue with [Architecture](architecture.md). For the
non-negotiable boundary, read [Safety](SAFETY.md).
