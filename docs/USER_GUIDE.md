# User Guide

## 1. Install

KF2 Optimizer Next is portable. Extract the complete package to a writable
folder and keep the executable beside the `Data` directory. Do not copy only
the executable: the package also contains runtime assets and integrity data.

Download the current package from the
[Download section](../README.md#download).

## Repair missing required files

Normal release packages already contain every required runtime companion file.
If one is removed or damaged, the app reports that component and keeps
unrelated controls available.

1. Select **Repair** in the upper-right corner.
2. Wait for the exact installed release to be downloaded and verified in the
   background.
3. Restart KF2 Optimizer after the verified repair completes.

If you close the app during Auto Repair, it shows a waiting message and closes
automatically after repair finishes. Do not force it to stop: a forced exit can
still interrupt repair, and the next start will detect mismatched package files.

Auto Repair never uses a generic latest-release address. An installation with
version `0.0.4-alpha` requests only tag `v0.0.4-alpha` and its identically
versioned Windows ZIP. The repair accepts only the same build identity and an
identical verified executable. Every imported companion file must match the
package SHA-256 manifest. Writes are atomic and the running executable is
never replaced.

If the PC is offline, open **Help & Repair**, select **Import Repair Package**,
and choose a complete
extracted package of the same version. Selecting its parent folder also works
when it contains exactly the `KF2OptimizerNext` folder.

No KF2 SDK or compilation is required for the ready-to-run ZIP. The executable
is currently unsigned, so Windows SmartScreen may display a warning on first
launch. The complete official ZIP already contains every project-owned runtime
file required by that version.

## Update the portable app

Select **Updates** in the upper-right corner to check for a newer official
release. The button changes to **Update available** and receives a visible
highlight when a verified newer version is ready. Automatic
checks run at startup no more than once every 24 hours. The manual check works
even when automatic checks are disabled. **Home → Version & Updates**
shows the installed version, last check, release date,
download size and short changelog before asking you to choose **Install
update** or **Later**. It never downloads or installs an update without your
approval. See [Portable updates](UPDATES.md) for verification, rollback and
preserved-data details.

## 2. Check detection

Start the optimizer and open **Home**. If KF2 was not detected, choose
**Select game folder**. A missing or ambiguous path keeps dependent controls
unavailable.

Close KF2 before changing its folder. A pending launch, restart or unfinished
protected recovery blocks the change. Prepared files are restored in the old
installation before the new path is saved; cancelling the picker or selecting
the current folder leaves the prepared state intact.

## 3. Choose the performance target

On **Home**, choose any target between 30 and 240 FPS. The target
is not restricted to common refresh rates: values such as 86, 122, 211, and
233 are valid.

The adaptive controller uses three absolute tolerance bands. At a 60 FPS
target, warning begins below 59, correction below 58, and critical pressure
below 57. The same one-, two-, and three-FPS offsets are calculated for every
other target.

Target FPS uses KF2's own engine frame-pacing controls and does not depend on a
specific GPU vendor or display driver. The optimizer prepares the same settings
for KF2 launched from the optimizer, Steam, or a desktop shortcut, and its
published offline provider reapplies and verifies the value during a running
offline session.

While KF2 is stopped, a target change is confirmed only after its native startup
cap passes readback. If saving fails, the previous target and cap are restored.
An incomplete rollback shows a repair notice and blocks launch from the optimizer.

## 4. Adaptive control

Target FPS and Maximum corpses are the only performance goals you set.
Adaptive automatically manages verified quality, physics, and corpse-runtime
controls while the game is running. The protected offline session separately
keeps eligible Zed/corpse LOD and safe animation work at their fixed minimum.
Unsupported controls remain unchanged.

Use **Adaptive optimization** on Home to enable or disable those automatic
adjustments. The choice is saved. Turning Adaptive off during a supported
offline session requires a confirmed runtime receipt before the app saves the
new state. Monitoring, the overlay, Target FPS, Maximum corpses, graphics
settings selected by the user, user-selected FleX minimum and fixed-minimum
Zed/corpse visual controls remain available while Adaptive is off. The saved
state is also reapplied after a map change.

The saved `adaptive_quality_recovery_enabled` preference controls automatic
quality increases under stable headroom. When disabled, Adaptive can still
reduce quality under confirmed pressure. This preference does not block rollback
of an ineffective change or restoration when Adaptive is disabled or KF2 closes.

The optimizer never enables FleX. If FleX is off in KF2, it remains off and no
FleX runtime hook is installed. If the user enabled FleX in KF2, the protected
session requests the fixed minimum solver work independently of Adaptive mode.
may use only the verified controls available for that existing setting.

## 5. Game graphics

Open **Game graphics** to view KF2's display, resolution, frame-rate, quality
and effects options. Click an option to move to its next value; the app saves
and verifies it immediately. KF2 must be closed because INI changes cannot
reliably alter graphics in a running match. A restore backup is created before
the managed INI files are replaced. **Reset to defaults** also saves immediately.
While KF2 is running in its main menu, the page shows values confirmed by
KF2's own applied Graphics-menu getter, including **INI override** where a
saved value does not match a built-in preset. Values selected in KF2's menu
before pressing **Apply** are not yet treated as applied settings.
Present numeric INI values must be complete, finite, and within the supported
ranges. An invalid scalar value makes the graphics snapshot unavailable and
identifies the setting without rewriting it. Missing optional keys retain their
existing display defaults. Malformed or non-preset texture-tuple integers remain
**Custom** and are preserved unless you explicitly change that texture option.
The app does not expose VSync or Variable frame rate controls. When KF2 is
closed and the app opens, an enabled VSync value or disabled frame-rate
smoothing is corrected through the same verified backup transaction. An
already-running match cannot be changed live by an INI write; the corrected
values take effect on a later KF2 start. The native KF2 video menu remains
under KF2's control and is not disabled by this portable app.

NVIDIA FleX is an explicit user control with **Off**, **Gibs**, and
**Gibs and fluids**. It starts at the value already stored by KF2. Neither
opening the page, selecting an overall graphics preset, nor Adaptive enables
FleX. Only changing this dedicated value may write `PhysXLevel`; the choice is
saved immediately.

The Home-page maximum-corpse goal is preserved when Character Detail changes;
the graphics page does not silently replace that separate user choice.
Aspect ratio is derived from the selected resolution. Gamma remains in KF2
because KF2 stores it in the player profile rather than the protected video
INIs. The shipped PC menu source contains a Foliage label but exposes no
Foliage setting to apply, so the app reports that honestly instead of writing
an invented value.

## 6. Advanced game settings

Open **Advanced settings** for verified KF2 options that exist in the game's
INI files but are not exposed by its normal video menu. The page groups engine
and streaming, rendering, and effects values. These are explicit manual KF2
settings and are independent of Adaptive.

Click an On/Off or enumerated option, or use the sliders for render scale,
particle amount, and decal lifetime. Each change is saved and verified
immediately. KF2 must be closed, and the app creates a restore backup before
writing. When a protected launch is prepared, this page reads personal values
from its snapshot, saves only your change, then rebuilds the prepared launch.
The saved settings survive launch cancellation, app shutdown and restart.
If saving fails, the controls reload the saved personal values. If restoration
cannot finish, keep KF2 closed and use the retained backup or repair the
protected launch before retrying; the app does not report the edit as saved.

Hover over any button or slider to see what it changes and its visual or
performance trade-off. These descriptions also distinguish user-owned
Advanced settings from Adaptive controls.

## 7. Start a protected session

Start KF2 through the optimizer when you want session-scoped telemetry,
restoration, and optional protected corpse-physics or FleX controls. Controls
appear only when their provider and acknowledgement path are available.
When KF2's native log confirms a new-settings restart, the optimizer keeps the
protected session intact for up to five minutes and rebinds only to the verified
KF2 executable. A normal game exit uses only a short replacement check before
restoration proceeds automatically.

## 8. Read the status correctly

- **Proposed** means the optimizer selected a possible change.
- **Applied** means a matching receipt or verified readback exists.
- **Unavailable** means the required provider, authority, identity, or
  acknowledgement is missing.
- **Passthrough** means the optimizer released control to the native game.

For corpse physics, the user-selected corpse maximum is a ceiling. The runtime
may keep fewer bodies active when distance, visible scene density, or measured
performance pressure requires it. Distance Sleep, Near Wake, and Ragdoll Sleep
are actor-scoped and must carry a correlated actor identifier and receipt.

For FleX, effective levels are 1 through 5. Level 0 means release/passthrough;
it does not mean that a zero-step solver is applied.

## 9. Overlay

The overlay is a separate, game-bound Windows surface. It displays verified
telemetry without injecting a renderer into KF2. Use F10 to toggle it. Scale,
position, and metric visibility are stored in the portable `Data` directory.

## 10. Debug

Open **Debug** to enable temporary in-game evidence markers for corpse actions
or living-Zed distances. Both options are off by default and affect only the
next protected KF2 start; they never change a game that is already running.
Living-Zed markers show distance in metres and a session Actor ID. Their
snapshot is limited to 64 visible Zeds and refreshes at most every 100 ms.
**FleX runtime diagnostics** is also off by default. When enabled, the Debug
page shows incoming and forwarded min/max substeps, solver and particle
statistics, shared-memory readback health, and report/log state. It can be
changed during a protected session. Leave it off for the cheapest normal path;
the fixed one-substep safety limit remains active for user-enabled FleX.
Live FleX observations remain in memory; there are no periodic report writes,
even with diagnostics enabled. `flex-session-last.json` is saved only on
session detach or an explicit support export, using the latest verified
observation. Failed saves are reported, and an active-session export can be
retried. An abrupt optimizer termination can lose observations since the last
explicit export; normal detach retains the final counters.
**Runtime diagnostics** is a separate, persistent option and is also off
by default. Enable it before a protected KF2 start only when you need detailed
corpse/Zed Actor, LOD, bone, injury, freeze and scan-timing evidence. KF2 telemetry
also omits the diagnostic-only spray, explosion, projectile and gib Actor scans
while this option is off; enabling it collects a complete initial snapshot.
Adaptive still receives its required counts, visibility and awake/sleep state while the
option is off; unavailable diagnostic-only fields are shown as not measured,
not as zero. The same switch enables detailed Adaptive performance samples,
pressure/decision history and quality-response evidence. Exact controller
readbacks and failures remain active and visible while it is off. Overlay
render duration, redraw/skip counts, placement-cache hits and real window
coverage checks also appear on Debug only while this switch is on; their text
is refreshed at most once per second. Startup and map preparation keep their
normal progress display in every mode, while storage kind, planned/attempted
file counts, cache-fill totals, open/read failures and detailed completion or
cancellation events are recorded only for preparation jobs begun with runtime
diagnostics enabled. File paths are never included in this summary.
The same switch enables detailed corpse-physics tick and collision readbacks,
sleep/wake statistics and successful per-action decision logs. Normal play
still performs every required sleep, wake, freeze, restore and collision/tick
safety check. It also retains the awake/sleep counts needed by Adaptive and the
dashboard, all failures, rollback evidence and the single minimal online
capability receipt; only diagnostic collection and formatting are omitted.
The Debug page also links to the portable data folder and current session log.
Diagnostic reports include unread game-log bytes and time spent catching up
(not the age of individual log records). Large logs are replayed in bounded
worker batches; Adaptive waits for the current tail and fresh measurements.
Current-map one-shot capability receipts remain available after catch-up.

## 11. Recovery

If KF2 or the optimizer ends unexpectedly, reopen the optimizer and use the
recovery status on **Help & Repair**. Recovery restores protected INIs,
runtime modules, and temporary session state from the recorded pre-session
snapshot. Do not delete the `Data` directory before recovery is complete.

If a configuration or native FPS-cap change reports pending recovery, close
KF2 and restart the optimizer. It verifies the original bytes before clearing
the recovery state. Locked files, conflicting edits or a damaged recovery
record remain blocked and visible; recovery does not overwrite unknown edits.
Resolve the reported conflict before launching from the optimizer again.

See [Support](../SUPPORT.md) before sharing logs publicly.

## 12. License

KF2 Optimizer Next is distributed under `GPL-3.0-only` and without warranty.
The complete terms are in the root `LICENSE` file and, in a portable package,
in `Data\Documentation\LICENSE`. Third-party components retain their own
licenses.
