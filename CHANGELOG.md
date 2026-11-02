# Changelog

Release notes stay short and user-focused. Every release uses only **What's
new**, **Bug fixes**, and optional **Important notes** so the important changes
are visible immediately.

## 0.0.5-alpha - 2026-11-02

### What's new

- Reworked Adaptive optimization with separate responses to CPU, GPU, memory, effects, physics and corpse pressure, plus gradual quality recovery. A persistent Home switch lets you turn Adaptive on or off.
- Added bounded corpse management, including distance sleep, settled-ragdoll freezing and reduced visual work for eligible Zeds and old corpses. Joined servers use verified client-local controls.
- Added a protected startup profile and storage-aware warming for frequently used game files and selected maps. The four vendor startup logos are skipped.
- Replaced the overlay's frame-timing backend with native DXGI capture while retaining live FPS, frame time, averages, 1% lows and stutter information.
- Integrated a verified optimized NVIDIA FleX runtime for players who enable FleX. The render path reduces transfers and CPU-side vertex packing, readbacks can overlap other work, and original components are restored after the session.
- Automatically select eight Zed optimizations in solo play on the verified KF2 build. Joined and listen-server sessions enable only the local sprint-audio fix.
- Decoupled spray-rotation audio updates from render FPS for flame, healing, Arc Generator and HRG Vampire sprays. Detailed runtime diagnostics are now opt-in. Death-heavy waves also require less telemetry bookkeeping.

### Bug fixes

- Fixed map-transition world retention and duplicate telemetry interactions that could cause later map-loading failures.
- Improved Adaptive recovery and exact restoration when disabling it, pausing in Escape/trader menus, changing maps or recovering an interrupted session.
- Excluded loading and Escape/trader menus from gameplay averages, 1% lows and percentiles while keeping live FPS visible.
- Fixed stale or inconsistent overlay FPS, hybrid-GPU adapter selection and isolated GPU readings affecting Adaptive decisions.
- Corrected particle-pressure calculations and startup validation of supported particle and shadow settings.
- Preserved user graphics and FleX selections across settings restarts and Steam handoffs, and improved slider saving and update-check retries.
- Guarded local damage handling when an attacker's pawn no longer exists.

### Important notes

- This is an unsigned Windows x64 alpha; Windows SmartScreen may show a warning on first launch.
- FleX remains off unless you enable it. Protected sessions preserve your settings, and updates preserve portable user data.

[Detailed development changelog](docs/CHANGELOG_0.0.5-alpha.md)

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
