# Feature Reference

| Feature | Purpose | Confirmation |
|---|---|---|
| Game discovery | Finds supported KF2 paths and identity | Unambiguous detected paths |
| Performance telemetry | Measures FPS, averages, 1% lows, frame time, CPU, GPU, and memory | Timestamped samples |
| Adaptive controller | Selects bounded quality pressure from performance and scene evidence | Decision record, then action receipt if applied |
| Configuration preview | Shows planned INI changes without writing | Preview diff |
| Transactional apply | Backs up, writes, and verifies supported settings | Verified readback |
| Session restoration | Restores the exact protected pre-session state | Restore report and integrity check |
| External overlay | Displays selected telemetry outside the game process | Visible game-bound window and current sample |
| Corpse ceiling | Sets the maximum permitted dead-body count | Configuration readback and runtime capacity receipt |
| Distance Sleep | Sleeps a sufficiently distant corpse actor | Matching actor ID and `state=sleep` receipt |
| Near Wake | Wakes a nearby previously slept corpse actor | Matching actor ID and `state=wake` receipt |
| Ragdoll Sleep | Stops redundant active ragdoll simulation | Matching actor ID and Ragdoll Sleep receipt |
| Scene-density control | Reduces active cosmetic work when many relevant actors are visible | Scene level plus confirmed actor/capacity actions |
| FleX adaptive control | Adjusts a protected solver level when the laboratory is available | `FLEX_ADAPTIVE_APPLIED` readback |
| Diagnostics | Explains provider, identity, restore, and evidence status | Explicit checks and exported reports |

## Important distinctions

### Required-file repair

The upper-right **Repair** action is available even in Safe
Mode. It derives an exact tag and asset name from the installed version, never
from a latest-release alias, and performs the download on a background worker.
HTTPS host restrictions, a 64 MiB archive limit, isolated extraction, build
identity, executable identity, safe single-link file identity, SHA-256 values,
atomic writes and a final full-package audit are mandatory.

`Import Local Package` provides the same verification path without network
access. A different executable, build, or release is rejected.

- A selected setting is not proof that KF2 accepted it.
- A decision is not proof that an action ran.
- Native KF2 state is not an optimizer action.
- An optimizer action is confirmed only by its matching receipt or readback.
- Unavailable is a safe capability result, not a hidden fallback.

The exhaustive implementation matrix remains in
[ISSUE_72_PRODUCT_MATRIX.md](ISSUE_72_PRODUCT_MATRIX.md).
