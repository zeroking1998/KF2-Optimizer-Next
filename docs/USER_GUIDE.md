# User Guide

## 1. Install

KF2 Optimizer Next is portable. Extract the complete package to a writable
folder and keep the executable beside the `Data` directory. Do not copy only
the executable: the package also contains runtime assets and integrity data.

Download the current package from the
[Download section](../README.md#download).

## Repair missing required files

Normal release packages already contain every required runtime companion file.
If one is removed or damaged, the app starts in Safe Mode instead of trusting
an incomplete package.

1. Select **Repair** in the upper-right corner.
2. Wait for the exact installed release to be downloaded and verified in the
   background.
3. Restart KF2 Optimizer after the verified repair completes.

Auto Repair never uses a generic latest-release address. An installation with
version `0.0.3-alpha` requests only tag `v0.0.3-alpha` and its identically
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
even when automatic checks are disabled. **Optimization → Version & Updates**
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

Open **Optimization** and choose any target between 30 and 240 FPS. The target
is not restricted to common refresh rates: values such as 86, 122, 211, and
233 are valid.

The adaptive controller uses three absolute tolerance bands. At a 60 FPS
target, warning begins below 59, correction below 58, and critical pressure
below 57. The same one-, two-, and three-FPS offsets are calculated for every
other target.

## 4. Adaptive control

Target FPS and Maximum corpses are the only performance goals you set.
Adaptive automatically manages verified quality, physics, LOD, FleX, and
corpse-runtime controls while the game is running. Unsupported controls remain
unchanged.

## 5. Start a protected session

Start KF2 through the optimizer when you want session-scoped telemetry,
restoration, and optional protected corpse-physics or FleX controls. Controls
appear only when their provider and acknowledgement path are available.

## 6. Read the status correctly

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

## 7. Overlay

The overlay is a separate, game-bound Windows surface. It displays verified
telemetry without injecting a renderer into KF2. Use F10 to toggle it. Scale,
position, and metric visibility are stored in the portable `Data` directory.

## 8. Recovery

If KF2 or the optimizer ends unexpectedly, reopen the optimizer and use the
recovery status on **Help & Repair**. Recovery restores protected INIs,
runtime modules, and temporary session state from the recorded pre-session
snapshot. Do not delete the `Data` directory before recovery is complete.

See [Support](../SUPPORT.md) before sharing logs publicly.

## 9. License

KF2 Optimizer Next is distributed under `GPL-3.0-only` and without warranty.
The complete terms are in the root `LICENSE` file and, in a portable package,
in `Data\Documentation\LICENSE`. Third-party components retain their own
licenses.
