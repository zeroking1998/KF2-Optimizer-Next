# Changelog

Release notes stay short and user-focused. Every release uses only **What's
new**, **Bug fixes**, and optional **Important notes** so the important changes
are visible immediately.

## 0.0.5-alpha - 2026-11-02

### What's new

**Killing Floor 2**

- KF2's Zed AI does less repeated work when looking for a reachable navigation point. Solo also optimizes sprint audio, enemy selection and attacker counts, bump and grab checks, King Fleshpound beam targeting and AI registration. Of those eight solo changes, only sprint audio also runs when joining or hosting a listen server.
- Flame, healing, Arc Generator and HRG Vampire sprays update their rotation sound at most 30 times per second. Starting or stopping a spray updates the sound immediately. The spray simulation and visuals keep their normal timing.
- With NVIDIA FleX enabled, particle data can go from the GPU to the render buffers without CPU vertex packing on eligible draws. Render resources reuse a packed snapshot, and eligible readbacks overlap other work until the CPU needs the result. The session restores the original DLLs afterward.
- Adaptive starts from your graphics settings. It can reduce the work behind a confirmed bottleneck, such as particle effects under effects pressure, and gradually restore quality when headroom returns. Moving corpses finish their death animation before becoming eligible for sleep.
- Startup skips four vendor logos and warms frequently used files according to storage type. Selected maps can be warmed again after travel. Target FPS uses KF2's native startup cap, including when launching via Steam or a shortcut.

**KF2 Optimizer Next**

- The Optimizer's Home page has a persistent Adaptive switch. Debug lets you enable detailed runtime diagnostics separately, including FleX solver statistics when available. Routine play skips diagnostic-only actor scans and bookkeeping.
- The overlay now uses native Windows DXGI frame timing. Live FPS, frame time, averages, 1% lows and stutter information remain available without a separate capture program.

### Bug fixes

**Killing Floor 2**

- Map changes in KF2 could retain the previous world and accumulate duplicate viewport interactions. The telemetry module now releases old world references and removes duplicate interactions so they cannot build up over successive loads.
- HRG Warthog and Auto Turret ammo indicators could be left without their material. The runtime repairs the missing material state.
- A replicated weapon-fire notification can reach the client before its weapon class is available. The runtime checks the class before using it.
- Active fire afflictions could flood the KF2 log with repeated FIRE warnings. The fix preserves the effect and its timer.
- In solo and on listen hosts, an attacker can lose its pawn before damage is resolved. Damage handling now checks the pawn before reading it, avoiding repeated Accessed None warnings.
- Moving ragdolls could go to sleep too early or immediately be put back to sleep after KF2 woke them. Sleep now checks for settled motion and respects recent wakes. Failed wake or readback attempts on one corpse no longer prevent later corpses from being processed.
- Adaptive could lose track of graphics it had changed when the Escape or trader menu opened. It keeps the original values across pauses and map changes, retries unconfirmed restores, and returns the online corpse limit to the value supplied by the server or mod.
- Adaptive compared particle counts with a capacity in different units. It now uses particle-count limits for pressure decisions.
- Changes in a manager or emitter could leave an old quality setting attached to its replacement. Adaptive now tracks emitter lifetimes and rechecks ownership before it changes or restores effects.

**KF2 Optimizer Next**

- The Optimizer could block launch after KF2 or Adaptive saved a valid graphics value. Startup validation accepts the native whole-scene shadow resolution of 1204, intermediate shadow thresholds and particle LOD bias from 0 through 5.
- Loading screens and confirmed Escape or trader menus no longer feed gameplay averages, 1% lows or percentiles. Live FPS continues to update. Frames from unrelated swap chains and previously lost capture sessions no longer contaminate the displayed values.
- The overlay can reconnect when KF2 replaces its window. Frame capture recovers after a transient interruption, and closing the Optimizer while its capture worker is stopping no longer leaves shutdown waiting indefinitely.
- On hybrid-GPU systems, telemetry could select the adapter with the most VRAM instead of the one running KF2. Adapter selection now uses process activity, and stale or isolated GPU readings cannot trigger a quality reduction on their own.
- Clicking the slider track now saves Target FPS and Maximum corpses just like dragging the thumb. Failed saves restore the previous value. Changing the saved FPS target during a match keeps the current session target until the next launch.
- The Game graphics page now reads KF2's saved menu settings, including film grain and the user's FleX choice. Failed writes or inconsistent readbacks are reported. Settings restarts and Steam process handoffs retain the protected session.
- Closing the Optimizer while KF2 is still running keeps the protected files and recovery record available. Restoration verifies the original installation and retains pending work if a write fails. Concurrent user edits are preserved during configuration updates and package repair.
- Interrupted portable updates can recover on the next startup. Update, repair and package import can no longer modify the package concurrently. A failed automatic update check retains the last successful result and retries.
- Updates and repairs support long Windows paths. Startup also works on older Windows 10 installations that lack the newer temporary-directory API.

### Important notes

- FleX stays off unless you enable it. Its observation relay forwards the game's requested solver substeps. Portable updates preserve user settings, logs and backups.
- This is an unsigned Windows x64 alpha. Windows SmartScreen may show a warning on first launch.

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
