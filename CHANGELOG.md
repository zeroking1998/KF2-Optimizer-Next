# Changelog

Release notes stay short and user-focused. Every release uses only **What's
new**, **Bug fixes**, and optional **Important notes** so the important changes
are visible immediately.

## Unreleased

### What's new

- Adaptive live quality now keeps independent CPU, GPU, VRAM, and RAM levels.
  Each confirmed bottleneck changes only its matching group, while recovery
  restores the groups gradually. The live groups now also cover verified KF2
  shadow distance/fade, post-processing quality, lighting, shadowmap textures,
  wound decals, blood effects, and destruction lifetime controls.
- CPU Adaptive quality now progressively reduces the three official cosmetic
  corpse-collision options at 80%, 60%, and 40%, with exact engine readback and
  restoration of the user's original values.

### Bug fixes

- Fixed resource-specific quality changes being tracked as one shared value,
  which could make a later CPU, GPU, VRAM, or RAM correction start from the
  wrong quality level.
- Target FPS now uses KF2's native startup cap independently of telemetry and
  Adaptive runtime capabilities, including Steam and shortcut launches.
- Removed the redundant telemetry-side FPS actuator and its unused application
  preview path.
- Fixed low or unstable overlay FPS values caused by mixing multiple KF2
  swapchains and excluding successful presents later discarded by Windows.

## 0.0.4-alpha - 2026-08-22

### What's new

- Added a subtle startup fade and a short closing fade for the portable app.
- Tooltips now fade in and out, while navigation, buttons, and sliders use a compact
  press-and-release animation.
- Slider tracks, thumbs, and live values animate together while dragging.
- UI animation behavior now lives in a dedicated, testable animation module.
- Home now contains Target FPS, Maximum corpses, and concise update details.
- Automatic update checks are directly accessible in the upper-right corner.
- Added a dedicated Game graphics page using KF2's own video-option names and
  values, with staged changes, verified backup, atomic apply and restore.
- Added an explicit NVIDIA FleX control to Game graphics. Overall quality and
  Adaptive never enable FleX; only the dedicated user selection plus Apply can
  change it.
- Added a separate Advanced settings page for verified KF2 INI-only engine,
  streaming, rendering and effects options. These manual game settings are not
  Adaptive controls; changes remain staged until the user applies them.
- Every button and slider now has a specific tooltip explaining its effect and
  performance trade-off, including clear RAM and VRAM warnings where relevant.
- Graphics and Advanced settings now provide Reset to defaults buttons. The
  recommended values are prepared first and are saved only after Apply.
- The automatic startup check now restores its last verified result, clearly
  shows whether a newer version exists, and opens an in-app update dialog with
  Update, Later, and Don't show again actions.

### Bug fixes

- Windows high-contrast colors are now detected during startup, and animation
  timer cadence follows later theme changes.
- Scrollbars now fade together with the rest of the interface during startup
  and shutdown animations.
- Applied/verified status messages now scroll with their page and can no longer
  cover headings, buttons, or Advanced settings at intermediate scroll positions.
- Mouse-wheel scrolling over a slider no longer changes its value accidentally.
- Tooltips now explain the practical On and Off behavior and no longer repeat
  workflow phrases such as "manual" or "staged until Apply".
- Removed the misleading Optimization destination and Animations button from
  the normal interface.
- Removed the confusing Home summary that grouped Quality, Physics, LOD, FleX,
  and corpses under Adaptive even though availability and user control differ.
- Adaptive launch now preserves the user's native KF2 FleX setting. FleX stays
  off unless the user has already enabled it in the game.
- Advanced INI changes are blocked while KF2 runs and now create a verified
  restore backup before atomic application.
- A restart within the 24-hour check interval no longer loses the last known
  update result or incorrectly asks the user to run a manual check.

## 0.0.3-alpha - 2026-08-22

### What's new

- The interface now has four clear areas: Home, Optimization, Overlay, and
  Help & Repair.
- Optimization now exposes only Target FPS and Maximum corpses. Adaptive
  automatically manages verified quality, physics, LOD, FleX, and corpse
  runtime controls.
- Update and Repair are always visible in the upper-right corner. The Update
  button is highlighted when a newer version is available.
- A safe portable updater checks official GitHub Releases automatically at
  most once every 24 hours or manually from the upper-right Update button.
- Available updates show the version, publication date, download size and this
  concise changelog before the user decides whether to install or wait.
- Approved updates verify the official repository, version, Windows-x64 asset
  name, exact size and SHA-256 before installation.

### Bug fixes

- Removed the footer, advanced-settings toggle, manual fine-tuning entry, and
  redundant explanatory text from the normal interface.
- Failed downloads, invalid packages, failed replacement and failed restart
  now leave or restore the previous working application version.
- Updates replace only managed program files and preserve portable settings,
  logs, backups, profiles and other user data.

### Important notes

- Updates are never downloaded or installed without explicit user approval.
- The application remains fully portable; the temporary update helper removes
  itself and its working files after restart.

## 0.0.2-alpha - 2026-08-22

### What's new

- **Auto Repair from GitHub** downloads the Windows package for the exact
  installed version and repairs missing or damaged managed files.
- **Import Local Package** remains available for offline recovery.

### Bug fixes

- Package repair now verifies the build identity and every managed SHA-256
  value before changing files.

### Important notes

- The executable is not code-signed yet, so Windows SmartScreen may warn on
  first launch.

## 0.0.1-alpha - 2026-08-21

### What's new

- Diagnostics can import required files from a complete matching portable
  package.
- Public versions use the `0.0.x-alpha` format.

### Bug fixes

- Local repair rejects mismatched builds and unsafe package files.

## 0.1.0-beta.2 - 2026-08-21

### Bug fixes

- Complete portable packages now start normally instead of incorrectly
  entering Safe Mode.
- Release validation now fails when a package rejects its own integrity
  manifest.

## 0.1.0-beta.1 - 2026-08-21

### What's new

- Added English documentation, GPL-3.0-only licensing, contribution templates,
  Windows CI and the ready-to-run portable package.
- Added actor-correlated corpse physics evidence and variable 30–240 FPS
  targets.

### Bug fixes

- Telemetry world tracking no longer keeps a completed world alive.
- Corpse and FleX reporting no longer presents unconfirmed requests as applied.
- Packaging now continues correctly after building a missing telemetry module.
