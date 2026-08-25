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
launch.

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

## 4. Adaptive control

Target FPS and Maximum corpses are the only performance goals you set.
Adaptive automatically manages verified quality, physics, LOD, FleX, and
corpse-runtime controls while the game is running. Unsupported controls remain
unchanged.

Adaptive never enables FleX. If FleX is off in KF2, it remains off and no FleX
runtime hook is installed. If the user has enabled FleX in the game, Adaptive
may use only the verified controls available for that existing setting.

## 5. Game graphics

Open **Game graphics** to view and stage KF2's display, resolution, frame-rate,
quality and effects options. Click an option to move to its next KF2 value, then
select **Apply graphics**. KF2 must be closed. The app creates and verifies a
backup before atomically replacing the managed INI files. **Discard changes**
returns to the values currently stored by KF2.

NVIDIA FleX is an explicit user control with **Off**, **Gibs**, and
**Gibs and fluids**. It starts at the value already stored by KF2. Neither
opening the page, selecting an overall graphics preset, nor Adaptive enables
FleX. Only changing this dedicated value and selecting **Apply graphics** may
write `PhysXLevel`.

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

Click an On/Off or enumerated option to stage its next value, or use the
sliders for render scale, particle amount, and decal lifetime. Nothing is
written until **Apply advanced settings** is selected. KF2 must be closed, and
the app creates and verifies a restore backup before applying the changes.
**Discard changes** reloads the values currently stored by KF2.

Hover over any button or slider to see what it changes, the visual or
performance trade-off, and whether the value is staged until Apply or saved
immediately. These descriptions also distinguish manual Advanced settings from
Adaptive controls.

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
performance pressure requires it. Distance Sleep and Ragdoll Sleep are
actor-scoped and must carry a correlated actor identifier and receipt.
Distance Sleep is permanent for that corpse; the optimizer does not wake dead
bodies again after their cosmetic physics has been stopped. The selected corpse
maximum determines how large the pool can become; Distance Sleep adds no second
fixed limit, so every currently eligible corpse can be put to sleep.

For FleX, effective levels are 1 through 5. Level 0 means release/passthrough;
it does not mean that a zero-step solver is applied.

## 9. Overlay

The overlay is a separate, game-bound Windows surface. It displays verified
telemetry without injecting a renderer into KF2. Use F10 to toggle it. Scale,
position, and metric visibility are stored in the portable `Data` directory.

## 10. Recovery

If KF2 or the optimizer ends unexpectedly, reopen the optimizer and use the
recovery status on **Help & Repair**. Recovery restores protected INIs,
runtime modules, and temporary session state from the recorded pre-session
snapshot. Do not delete the `Data` directory before recovery is complete.

See [Support](../SUPPORT.md) before sharing logs publicly.

## 11. License

KF2 Optimizer Next is distributed under `GPL-3.0-only` and without warranty.
The complete terms are in the root `LICENSE` file and, in a portable package,
in `Data\Documentation\LICENSE`. Third-party components retain their own
licenses.
