# Changelog

Release notes stay short and user-focused. Every release uses only **What's
new**, **Bug fixes**, and optional **Important notes** so the important changes
are visible immediately.

## 0.0.5-alpha - Unreleased

### What's new

- Reworked Adaptive into one protected, user-controlled runtime with separate
  CPU, GPU, VRAM, RAM, overdraw, effects, physics, LOD, and corpse-pressure
  paths. Confirmed pressure changes only its matching group, and stable
  headroom restores quality gradually.
- Expanded Adaptive coverage to verified KF2 shadow distance and fade,
  post-processing, lighting, shadow-map textures, wound decals, blood, gore,
  destruction lifetimes, particle LOD, emitter capacity, and cosmetic corpse
  collision controls. Every live action requires an applied receipt before it
  is treated as active.
- Added conservative scene-pressure classification from frame time, sustained
  resource use, visible particle occupancy, decal saturation, active effects,
  enemy pressure, and corpse density. Unknown pressure no longer triggers an
  unrelated broad quality change.
- Added bounded corpse management with capacity control, staggered processing,
  distance sleep, settled-ragdoll sleep, stable low corpse LOD, final-pose
  skeleton handling, and a frozen state outside rigid-body simulation. Frozen
  corpses no longer keep unnecessary actor ticks or collision work.
- Added safe fixed-minimum visual tiers for living Zeds and old corpses while
  preserving attacks, hits, current death animations, active physics, and the
  player's close-range view.
- Added a persistent Home switch for Adaptive optimization. Changes are saved
  safely even during map transitions or temporary telemetry loss and are
  confirmed by the protected runtime before live control resumes.
- Replaced the embedded PresentMon analysis library with a small native DXGI
  frame-timing session. The overlay retains Live FPS, frame time, average,
  1% low, percentiles, and stutter information without injection, a helper
  process, or an additional runtime DLL.
- Added readable in-game Zed and corpse distance diagnostics plus actor-level
  telemetry for controlled gameplay investigations. Diagnostic markers remain
  optional and are kept separate from normal optimization behavior.
- Added a protected native startup profile with asynchronous physics scenes,
  real one-frame render-thread pipelining, and hardware-aware texture-streaming
  memory, margin, and hysteresis values.
- Added storage-aware startup warming for frequently used KF2 files, Steam
  achievement data, and selected map data. Work is bounded, visible in the UI,
  stops when KF2 needs the resources, and avoids blindly loading large files.
- Skips the four vendor startup logos while preserving the main-menu background
  and map-loading movies.
- Moved expensive desktop telemetry, game-log parsing, persistence, frame
  aggregation, and selected diagnostics off the UI path. Remaining world,
  emitter, corpse, Zed, and effect scans are cached, phased, or budgeted across
  frames to reduce periodic CPU spikes.
- Added a controlled corpse-physics A/B mode for repeatable performance tests
  without changing normal user behavior.
- Added reproducible release optimization through interprocedural optimization
  and an optional two-phase MSVC profile-guided optimization workflow.
- Simplified contributor setup, Windows builds, GitHub issue reporting, and
  portable packaging. Parallel local builds, stronger package verification,
  current-source telemetry binding, and clearer public documentation are now
  included.

### Bug fixes

- Target FPS now uses KF2's native, vendor-independent startup cap for launches
  from the optimizer, Steam, and shortcuts. A running session remains bound to
  the target it actually started with, preventing false Adaptive pressure after
  the saved target changes.
- Direct clicks anywhere on Target FPS, Maximum corpses, and other slider
  tracks now save exactly once. Values changed during Steam's startup handoff
  are applied to the imminent KF2 process instead of being mislabeled as
  belonging to a later start.
- Fixed Maximum corpses reaching the end of its range, interrupted slider
  capture, failed setting writes, and startup readback so the UI always returns
  to the authoritative saved value.
- Information, warning, and error banners now use distinct semantic colors,
  and the compact status line has consistent horizontal spacing.
- Synchronized the Optimizer's Game graphics page with KF2's own saved menu
  values. User-owned graphics and FleX selections survive protected sessions,
  settings restarts, app restarts, and exact post-session restoration.
- Preserved protected sessions across KF2's automatic settings restart and
  Steam process handoff without restoring temporary files too early.
- Fixed map polling retaining a completed Unreal world and causing garbage-
  collection failures during later map loads. World-bound cursors, caches,
  telemetry interactions, and map-ready state now reset safely across repeated
  transitions.
- Fixed duplicate telemetry viewport interactions accumulating after map
  changes. One persistent interaction stops and rearms for each gameplay world.
- Prevented transient loading, menu, trader, and early post-map frames from
  entering Adaptive decisions or depressing the displayed 1% low window.
- Preserved one native graphics baseline across consecutive maps so recovery
  never treats a previously reduced value as the user's new 100% setting.
- Fixed Adaptive recovery restoring quality without verified improvement,
  getting stuck below the user's quality, or rolling back through the wrong
  resource group. Pending and applied actions now retain their exact cause and
  generation.
- Prevented broad `mixed` reductions without an attributed CPU, GPU, VRAM, RAM,
  paging, effects, or overdraw cause. Runtime effects and memory controls no
  longer invoke unrelated native graphics work.
- Fixed sustained 1% low pressure, stale frame pressure after recovery, and
  frame-rate-mode changes carrying old Adaptive windows into a new target.
- Fixed effect, gore, blood, impact, explosion, and impact-particle managers
  retaining old limits after a live effect change. Active pools now confirm the
  same reversible values before an applied receipt is accepted.
- Fixed repeated native corpse wakes being fought by Distance Sleep. Per-corpse
  backoff grows from 2 to 30 seconds, expired records are reclaimed, and other
  eligible corpses remain unrestricted.
- Corpse sleep now requires settled motion, preserves current death animation,
  avoids rapid sleep/wake loops, and keeps an unconditional 800-unit safety
  radius around the player under every pressure level.
- Fixed reduced corpse LOD, final-pose skeleton state, frozen physics, tick, and
  collision being unnecessarily restored after a corpse was already finalized.
- Fixed temporary corpse-telemetry gaps disabling otherwise safe processing or
  presenting unconfirmed capability and action states.
- Filtered GPU utilization by physical adapter, sample age, and continuity so
  isolated 0% or 100% readings cannot redirect Adaptive decisions. Sustained
  saturation is still recognized quickly.
- Fixed hybrid-GPU systems choosing the adapter with the most VRAM instead of
  the GPU KF2 actually uses. Startup now uses the confirmed adapter or a safe
  cross-adapter memory budget when selection is uncertain.
- Kept the overlay visible across focus changes and exclusive-fullscreen use by
  using protected borderless presentation when required and restoring the
  user's selected display mode after KF2 exits.
- Fixed low, unstable, or stale overlay FPS caused by mixing KF2 swap chains,
  dropping successful presents later discarded by Windows, or carrying timing
  state across maps. Live FPS now uses the same one-second observation window
  as common external overlays.
- Fixed FleX detection after graphics changes and settings restarts. User-
  enabled FleX runs at the verified minimum solver level; user-disabled FleX
  remains off and is never enabled by Adaptive.
- Fixed invalid Ultra shadow validation and other catalog/readback mismatches
  that could incorrectly report a safe KF2 setting as missing or out of range.
- Fixed stale or mismatched compiled telemetry modules entering a package.
  Builds now bind the executable to the current source module and verify the
  module, manifest, hashes, documentation, and portable package shape.
- Fixed Adaptive and telemetry work continuing while the game was not ready,
  while Zed Time prohibited an action, or after its process/session identity
  changed.
- Preserved bounded applied-action history so early receipts remain available
  for diagnostics after long gameplay sessions.
- Removed unreachable legacy control paths, the redundant telemetry-side FPS
  actuator, obsolete preview behavior, and idle animation-timer work that no
  longer serves the current interface.

### Important notes

- This remains an unsigned alpha release. Windows SmartScreen may display a
  warning on first launch.
- Existing user graphics, FleX choices, portable settings, logs, backups, and
  profiles remain user-owned and are preserved by protected sessions and
  updates.
- Several changes affect startup, map transitions, telemetry, and protected
  restoration together. Final release acceptance must use the exact packaged
  executable and telemetry module that will be published.

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
