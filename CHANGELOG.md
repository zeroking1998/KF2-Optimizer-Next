# Changelog

Release notes stay short and user-focused. Every release uses only **What's
new**, **Bug fixes**, and optional **Important notes** so the important changes
are visible immediately.

## 0.0.5-alpha - Unreleased

### What's new

- Skip diagnostic particle-template attribution and its detailed reports while
  Runtime diagnostics is off; normal particle measurements and Adaptive inputs
  remain available, without changing the report format or sampling interval.

- Discover KF2 thread membership with a process-scoped snapshot instead of
  walking every Windows thread when query rights permit. Keep the original
  read-only fallback, CPU-time measurements and sampling intervals unchanged.

- Reuse one physics awake observation per corpse in each offline telemetry
  sample, preserving pressure and published counts without a retained cache.

- Let native pawn/weapon iterators filter runtime-guard targets, avoiding script
  casts for unrelated actors without changing repairs or their 50–250 ms cadence.

- Skip diagnostic-only spray, explosion, projectile and gib Actor scans while
  Runtime diagnostics is off; Adaptive inputs remain active and uncollected
  counters are not reported as measured zero.

- Reuse unchanged selected Adaptive action text instead of rebuilding it;
  exact labels, decisions, diagnostics and restoration are unchanged.

- Transfer completed Adaptive performance-sample and quality-response diagnostic
  buffers instead of copying them; exact text, logging gates, sample cadence and
  retained-event bounds are unchanged.

- Transfer completed Adaptive diagnostic buffers instead of copying them;
  log text, bounds, diagnostic gates and controller behavior are unchanged.
- Include both runtime-guard sources in the telemetry build fingerprint so changes
  cannot silently reuse an older module; no gameplay logic is changed.
- Reuse completed payload hashes across both package manifests, avoiding a second
  full payload read while preserving independent release validation.
- Preserve existing packages when the required FleX build artifact is missing,
  checking it before cleanup, copying or unnecessary exporter work.
- Reject linked package destinations and managed paths before cleanup or writes,
  preserving existing files outside the intended package directory.
- Reject unsafe previous package manifests before deleting any managed file;
  validated paths are reused once and portable user Data remains protected.
- Refresh the package inventory exporter even when skipping the app build;
  stale inventory output can no longer be relabeled as the current build.
- Correct stale feature-inventory descriptions of catalog totals, payload hashes
  and CI coverage without changing feature classifications or runtime behavior.
- Transfer the completed package/support inventory JSON buffer without copying
  the full document; its schema, contents and owning return value are unchanged.
- Remove an unused internal FleX audit JSON exporter and its dedicated escape
  helper; active DLL verification, hook safety and diagnostic exports are unchanged.
- Avoid completed-stream copies when displaying live UI metrics and the FPS
  status suffix, preserving exact text, numeric animation and refresh behavior.
- Write support diagnostics directly into the export and transfer completed
  JSON buffers, preserving exact bytes, event persistence and retry behavior.
- Transfer completed telemetry display buffers instead of copying them,
  preserving exact text, locale, live values and Adaptive behavior without a cache.
- Recompile only the owning telemetry component when the SDK module hash
  changes, preserving exact module verification without rebuilding unrelated code.
- Reuse the current emitter-owner check within each particle-control iteration,
  retaining discovery, identity verification, readbacks and restoration.
- Build waiting or failed-telemetry text only for incomplete frame data, avoiding
  a discarded string while preserving exact values and optional-zero behavior.
- Reuse the same GiB text formatter for hardware and telemetry displays,
  removing duplicate code while preserving precision, locale and units.
- Build unavailable-analysis text only when FPS is absent, avoiding discarded
  temporary strings without changing the telemetry or Adaptive display.
- Publish only changed prewarm display fields and avoid constructing unused
  map diagnostic labels; worker behavior, budgets and progress rules are unchanged.
- Publish optional overlay diagnostic text without copying unrelated UI,
  graphics or Adaptive state; diagnostic timing and displayed text are unchanged.
- Escape shared JSON string contents directly into the returned owned string,
  avoiding stream/buffer-copy work while preserving exact serialized bytes.
- Remove the unused internal Adaptive profile recommendation; user settings,
  real quality budgets, decisions and restoration remain unchanged.
- Borrow constant telemetry limit-explanation labels instead of allocating
  identical strings; classifications, confidence and displayed text are unchanged.
- Build Adaptive CPU workload, action, prediction and safety labels in owned
  buffers, avoiding temporary strings while preserving values, precision and units.
- Reuse unchanged Adaptive reason, capability, source and evidence text instead
  of recreating temporary display strings; readback and restore policy is unchanged.
- Publish Adaptive's own display fields without copying unrelated graphics,
  telemetry, FleX, prewarm and update status on every controller tick.
- Avoid Weapon classification and repeated null checks for already identified
  pawns, retaining the existing runtime guard's repair coverage and cadence.
- Simplify six corpse/Zed ownership lookups using KF2's native array search,
  preserving first-match identity and missing-entry behavior without new state.
- Skip the full world-emitter restoration scan when no Optimizer-owned emitter
  original values remain; nonempty restore and retry behavior is unchanged.
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
- Added optional FleX runtime diagnostics to Debug with live min/max substeps,
  solver and particle statistics, shared-memory readback health, and bounded
  reports. Normal play keeps those detailed counters and logs disabled.
- Added a separate, persistent runtime-scan diagnostics switch. Normal play
  keeps only the corpse/Zed counts and visibility data required by Adaptive;
  Actor, LOD, bone, injury, freeze-detail and scan-timing evidence is collected
  only for an explicitly requested diagnostic run.
- Detailed Adaptive performance samples, decision history, pressure evidence,
  quality requests and response windows now use the same Debug switch. Normal
  play retains the identical controller and exact safety readbacks without
  formatting or retaining those diagnostic-only event payloads.
- Overlay render duration, update/redraw/skip counts, placement-cache hits and
  real game-window coverage checks are now collected only with runtime
  diagnostics enabled and published to Debug at a bounded one-second cadence.
  Normal play performs no high-resolution overlay timing or diagnostic count.
- Startup and map prewarm now retain only the byte progress required for normal
  operation. Storage kind, planned/attempted files, cache-fill totals and
  open/read failures are collected and shown only for Debug diagnostic jobs;
  detailed success/cancellation events are likewise opt-in.
- Detailed corpse-physics tick, collision, sleep and wake statistics plus
  successful action/decision logs now use the runtime diagnostics switch.
  Adaptive control counts, mutation limits, rollback checks, failures and the
  minimal online capability receipt remain active in normal sessions.
- Added reproducible release optimization through interprocedural optimization
  and an optional two-phase MSVC profile-guided optimization workflow.
- Simplified contributor setup, Windows builds, GitHub issue reporting, and
  portable packaging. Parallel local builds, stronger package verification,
  current-source telemetry binding, and clearer public documentation are now
  included.

### Bug fixes

- Use the shared Windows SHA-256 algorithm handle instead of repeatedly opening
  providers, eliminating their allocation-failure leak without changing digests.

- Release thread-discovery snapshots and newly opened thread handles if cache
  allocation fails, preserving bounded retries and the existing sampling cadence.

- Keep valid cached thread measurements after an incomplete Toolhelp traversal
  and retry on the next sample instead of accepting partial membership.

- Close the game-discovery snapshot even if candidate inspection throws,
  preserving process identity checks without adding queries or allocations.

- Continue independent SDK cleanup after a file-restoration error, retaining
  recovery backups and reporting failures without hiding the original build error.

- Reject incomplete diagnostic JSON on serialization failure instead of
  overwriting a valid log or reporting a partial support export as successful.

- Restore queued Adaptive quality changes against the originating resource,
  keeping rollback selection and command previous values consistent when pressure shifts.

- Keep the Optimizer usable in memory if its optional event-log worker cannot
  start, reporting persistence unavailable without claiming a successful flush.

- Keep optional prewarming unavailable instead of terminating the Optimizer if
  storage detection runs out of memory; release its volume handle on failure.

- Keep startup compatible with older Windows 10 installations lacking
  GetTempPath2W, without falling back to an unisolated temporary directory for SYSTEM.

- Stop configured-GPU lookup on DXGI errors instead of repeatedly querying or
  reporting an unresolved selection as successful.

- Handle GPU monitor-enumeration failures without crashing the Optimizer;
  preserve the native error and existing telemetry fallback.

- Preserve coherent update-check state and its completed background result if
  completion runs out of memory, allowing a later retry without repeating the request.

- Report GPU startup-profile allocation failures through the existing launch
  rollback instead of terminating; avoid a redundant selected-adapter copy.

- Keep wrapped changelog bullet text intact in the in-app update notes.

- Restore the configured update and Auto Repair repository in the shipped app.

- Let telemetry snapshot allocation failures reach the existing worker recovery
  path instead of terminating the Optimizer when old log measurements expire.

- Avoid temporary allocations in recovery-setting lookup, preventing process
  termination if memory allocation fails during backup-manifest recovery.

- Preserve concurrent file edits during portable-package repair and rollback;
  require the expected original content or absence before committing a repair.

- Clear startup Warm-up progress when preparation is skipped for unknown storage,
  insufficient memory or no eligible files, instead of leaving it stuck at 0%.

- Reject malformed leading-zero file sizes in GitHub release metadata while
  preserving valid sizes and the existing installation limits.

- Reject trailing data after empty GitHub release lists instead of reporting
  a successful update check with no newer release.

- Exclude ignored build trees before repository validation, avoiding unnecessary
  traversal and interference from changing test fixtures without skipping source checks.

- Keep Target FPS and corpse-limit slider values correct when a settings
  callback rebuilds the UI during keyboard changes.

- Preserve update files and recovery records when the owner process cannot be
  queried, instead of treating an unknown process state as permission to roll back.

- Preserve bounded diagnostic text when a UTF-16 character crosses the message
  or source limit, instead of exporting an empty field.

- Apply the existing online corpse mutation cooldown after rejected restores,
  preventing repeated native writes while retaining originals and fair retries.

- Keep retained-log test timestamps deterministic through live appends and file
  recreation, without changing runtime log ownership or freshness checks.

- Stop passing the Optimizer's own GPU-memory usage and budget to Adaptive as
  KF2/adapter-wide VRAM pressure. Keep KF2's process-bound memory counters and
  remove the unnecessary recurring DXGI adapter lookup and budget query.

- Convert the raw Adaptive decision reason once for its two presentation fields
  while retaining distinct queued-restoration warnings.
- Reuse the existing Adaptive stability, bottleneck and no-setting action labels
  instead of constructing temporary strings or rewriting unchanged text.
  Controller decisions, displayed values and safety readbacks are unchanged.

- Recover FleX particle text on the next fresh observation after failed display
  preparation, while keeping live observations and confirmed receipts current.
  Successful unchanged observations retain their existing text reuse.

- Update the changed FleX availability label without copying unrelated graphics,
  settings and live telemetry display state. Capability checks and control
  behavior remain unchanged.

- Update only live telemetry display fields instead of copying graphics,
  settings and other unrelated UI status during changing measurements.
  Measurement cadence, Adaptive decisions and overlay updates are unchanged.

- FleX diagnostics no longer serialize or flush a report every two seconds.
  Live observations stay in memory; session detach and explicit support export
  save the latest verified counters and report persistence failures.

- Reuse unchanged FleX display state instead of copying the complete UI status
  on every observation and capability check. Borrow constant diagnostic labels
  and reuse unchanged particle values instead of allocating their text every
  sample. Live readbacks and control timing remain unchanged.

- Keep healthy minimal FleX readback available when solver capacity is known
  but active/free particle counts have not been observed. Missing counts stay
  unavailable; normal play adds no particle queries or diagnostic collection.

- Stop rebuilding unrelated UI nodes during numeric animation. Reuse desktop
  text formats and target-bound brushes, pause hidden/minimized animations,
  and keep accessibility values aligned with the visible presentation.

- Current project and acceptance documents now share source-checked test and
  settings-catalog inventories instead of contradictory old totals. Historical
  gameplay evidence is kept separate from new-build acceptance.

- FleX now withdraws its shared-memory publication before the forwarder DLL
  unloads and rejects observations/control writes after the verified process
  exits, even if a reader retains its fresh mapping. Normal solver calls remain
  unchanged; the app reuses its existing process handle for the liveness check.

- Keep one verified FleX observation mapping per attached process instead of
  reopening it on every sample. Values remain live; process replacement,
  invalid publication and session detach release the view immediately.

- Reuse each FleX relay's successfully resolved native export instead of
  looking it up on every call. Missing runtimes remain retryable, the original
  DLL stays alive only until forwarder unload, and fixed substeps stay unchanged.

- FleX control confirmation now requires healthy, completed native readback.
  Fresh intended values from a failed or still running relay no longer become
  an applied action; existing acknowledgement timeouts and fixed substeps
  remain unchanged.

- Baseline corpse sleep now stops before scanning or changing corpses when its
  action-tracking table is full or invalid, retaining fair cursor progress.

- Format FleX diagnostic labels without chains of temporary number strings.
  Live values and exact displayed text stay unchanged; diagnostics-off still
  performs no detailed formatting.

- Format changed FleX particle/capacity labels without temporary numeric-string
  chains, keeping unchanged labels borrowed and all displayed values current.

- Update only the six FleX observation display fields when they change, avoiding
  copies of unrelated graphics, settings and update status.

- Update and Auto Repair now wait for synchronous Windows ZIP extraction to
  finish before checking or cleaning staged files; partial or aborted copies
  fail safely without a fixed-delay completion guess.

- Auto Repair now verifies the exact-version release archive against GitHub's
  published SHA-256 and size before extraction, using the same checks as updates.
  Missing or mismatched metadata cannot authorize repairs.

- Confirmed in-game graphics changes now survive failed session restoration
  and Optimizer restarts. Recovery retains the protected originals until the
  user changes have been written and verified.
- Portable packaging and release validation now compare the executable's embedded
  build identity with its manifests and source revision. Stale or dirty packages
  cannot be silently certified as a new release candidate.

- Removed unused named-profile generation and persistence code. Adaptive still
  starts from user graphics and retains resource-specific control and locks.
- Shared the backup and protected-session byte codec without changing saved
  formats, path checks, or configuration restoration.
- Simplified graphics preset recognition without changing KF2's vanilla
  values, custom settings, or either graphics readback path.

- Advanced settings changed before a prepared protected launch now remain
  personal settings after cancellation, shutdown and restart. Failed saves
  roll back safely; blocked restoration retains verified recovery data.

- GPU telemetry now retries missing Windows and NVIDIA providers independently
  after transient initialization failures, with backoff capped at 30 seconds.
  Working providers are retained and retry diagnostics cannot spam every tick.

- Desktop telemetry now submits one resource-worker request per regular cycle
  and shares verified process handles instead of reopening them for each
  liveness or CPU/memory sample. Process exits, PID reuse and restricted access
  still reject unsafe samples; final log flushing remains explicit.
- Removed unused runtime helpers and graphics-preview bookkeeping. Corpse
  telemetry no longer queries native awake state for an unpublished counter;
  controller inputs, graphics readback and active restoration remain unchanged.
- Configuration-preview, diagnostics and inventory exports now share one JSON
  string-escaping function instead of three identical copies. Existing output
  bytes and recovery contracts are unchanged.

- DXGI frame timestamps now use exact integer clock conversion instead of
  floating-point scaling, preserving nanosecond intervals and rejecting
  values outside the supported timestamp range.

- Rebind telemetry and overlay to a replacement KF2 window without discarding
  the same process's FPS history. Missing-window discovery is rate-limited and
  retains immutable process-identity checks.

- Bound FleX recovery to the original installation's directory identity, so
  startup discovery cannot restore one game's DLL into another. Legacy or
  missing ownership evidence is retained for explicit recovery, not guessed.

- Game-folder changes now preserve the original installation until protected
  files are fully restored. Running games, pending launches/restarts and
  unconfirmed recovery block switching; cancelling preserves prepared state.

- GPU counter sampling now reuses its buffers and remembers parsed counter
  identities instead of repeatedly allocating and parsing unchanged names.
  Measurements, GPU attribution and sampling cadence remain unchanged.
- DXGI live statistics now share one interval sort for their overlapping
  windows instead of rebuilding four independent arrays. FPS, 1% lows,
  percentiles, freshness and diagnostic comparisons are unchanged.
- Idle KF2 process discovery now backs off instead of repeatedly scanning the
  Windows process list. App launches and restart handoffs wake discovery;
  skipped queries no longer imply that KF2 has closed.
- Concurrent DXGI streams no longer falsely interrupt Adaptive quality-response
  windows. Only the selected stream's lifetime and real source loss invalidate
  the comparison; unrelated streams no longer allocate copied windows.
- Initial DXGI frame-timing failures now share the existing bounded startup
  retry, so a transient failure can recover FPS telemetry without restarting KF2.
- Verified online Adaptive graphics changes now use the same session checks
  for selection and response evaluation, avoiding false restoration after a
  measured improvement. Unknown sessions and incomplete comparisons still fail closed.
- Missing or shortened prewarm files now retain their actual byte progress
  instead of being reported as 100% complete. Incomplete map preparation does
  not repeatedly reread files or block KF2 loading.
- Hybrid-GPU attribution ranks the combined dedicated and shared process
  allocation correctly, while preserving renderer preference and tie handling.
- Removed the inert Adaptive calibration preference. Older settings migrate
  safely without changing the user's active preferences or controller behavior.
- The full self-check uses the authoritative catalog size and reports its
  actual setting count instead of falsely rejecting the valid catalog because
  of an obsolete independent total.
- Ordinary diagnostic events now share a fixed 250 ms persistence batch,
  avoiding repeated full-history copies and atomic file replacements during
  bursts. Explicit flush and healthy shutdown bypass the batch delay; bounded
  failure retries, retention and the atomic JSON format are unchanged.
- All native build paths now share automatic binding to the exact local
  telemetry module. Direct CMake and incremental builds no longer inherit
  an outdated default hash; explicit mismatched pins fail during configuration.
- Noisy game-log batches now compact their incomplete tail only once instead
  of moving the remaining buffer after every line. Session markers, receipts,
  partial lines and existing size limits are unchanged.
- Large Launch logs now catch up in bounded worker batches instead of exposing
  old maps and measurements as live telemetry. Adaptive waits for the current
  tail, historical measurements are discarded, and diagnostics report unread
  bytes and catch-up duration without additional UI-thread file work.
- Launch.log sampling now retains one verified read handle instead of reopening
  the file every poll. Rotation, deletion, truncation and process changes reset
  parser ownership safely; detach releases the handle without another sample.
- Thread CPU telemetry now keeps its measurement baseline with the verified
  thread handle, resets it for a changed thread instance, and releases exited
  threads during membership refresh without rebuilding a sample hash table.
- Adaptive now honors disabled automatic quality recovery. Confirmed pressure
  can still reduce quality, and disabling Adaptive or rolling back an ineffective
  change still restores protected settings.
- Configuration rollback now verifies every restored file and retains recovery
  state when a write, readback or journal completion fails. Native FPS-cap
  changes retain both original files until confirmed, so the next stopped-game
  startup can recover an interrupted change before restoring protected INIs.
- Auto Repair now finishes its background work before the app closes, with a
  visible waiting message. Unverified repair rollback or unexpected repair
  errors preserve the interrupted-session marker instead of marking shutdown clean.
- Temporary event-log write failures now retry in the background with bounded
  backoff, preserving the latest events. Help & Repair shows storage availability
  without generating more log events about the writer itself.
- Online corpse sleep and capacity checks now use bounded, rotating scans,
  including retry pauses after misses. The local controller reuses its
  viewport-owned monitor, and missing corpse support no longer logs every frame.
- Online Adaptive enable verifies its prerequisites before changing the corpse
  maximum. Rejected re-enables restore the previous limit and retain rollback
  ownership until verified, without losing the session-original value.

- DXGI frame telemetry identifies its ETW owner by PID and process start time,
  so a crashed session cannot block a new optimizer instance after PID reuse.

- Enabling Adaptive no longer treats the selected corpse maximum as a
  confirmed graphics-quality reduction. Genuine graphics evidence is preserved.

- Adaptive now counts each Present observation once. Delayed or repeated
  asynchronous FPS windows cannot manufacture pressure or recovery evidence.

- Process inspection failures no longer mean KF2 is closed. Configuration,
  package recovery, launch preparation and other stopped-game writes wait
  until the process state can be verified safely.
- Keep unsuccessful corpse wake attempts tracked for fair, bounded retries,
  and restore skeleton updates when wake is confirmed, including late wakes.
- Offline corpse restoration now yields after a failed attempt and fairly
  shares release turns with active/retired wake queues. Failed collision/tick
  readbacks no longer consume the physics slot; original retry state is kept.
- Temporary gameplay-telemetry expiry no longer discards the Adaptive listener
  port. Fresh measurements can resume control without another readiness line;
  map/provider changes and explicit listener failures still invalidate the port.
- DXGI stale-session cleanup now processes safely returned ETW entries when more
  sessions exist than its fixed buffer can hold. Array bounds and malformed
  session names are checked without adding retries or a larger startup scan.
- Failed native FPS-cap synchronization now keeps session finalization incomplete,
  preserves an actionable error and blocks automatic launch rearming instead of
  reporting that all required state was restored.
- Closing the optimizer while KF2 is running now retains protected INIs, runtime
  packages and their durable recovery snapshots. Cleanup is retried after KF2
  ends instead of rewriting live state or marking incomplete recovery clean.
- FPS telemetry now recovers after a transient capture loss once the measurement
  window is wholly fresh. Affected windows and stale cached results stay blocked;
  Adaptive and quality-response diagnostics no longer require a session restart.
- Graphics saves now retain a Repair warning if rebuilding the protected KF2
  launch fails or is unavailable. Successfully saved values remain intact
  instead of hiding the partial failure behind a generic success notice.
- Adaptive toggles now save the preference before changing live or deferred
  control. Failed writes leave the previous mode intact and cannot apply an
  unsaved change when gameplay confirmation returns later.
- Fixed a shutdown race that could leave the native frame-statistics worker
  asleep forever when stopping capture or changing sessions.
- Native FPS capture now bounds orphaned Present starts and invalidates pending
  pairs after ETW loss, avoiding session-long thread-state accumulation.
- Failed online corpse readbacks no longer stall Freeze, LOD or skeleton scans.
  Failed attempts also respect the existing cooldowns, while freeze rollback
  retains original state until restoration is verified.
- Removed the inert saved quality-policy preference and its misleading status
  and diagnostic labels. Valid legacy values migrate away once while user
  graphics, goals and Adaptive control remain unchanged.
- Online corpse LOD and sleeping-skeleton scans now keep independent progress,
  so alternating checks no longer permanently skip parts of the corpse pool.
  Existing scan budgets and action cadence remain unchanged.
- Retired hidden Adaptive aggressiveness profiles. Legacy values migrate away,
  so identical user settings now use the same fixed controller policy instead
  of different behavior inherited from old portable settings.
- Failed application initialization no longer creates a false interrupted-session
  marker. Genuine prior interruption evidence remains unchanged until startup
  reaches the operational boundary.
- Adaptive quality recovery now requires a complete valid memory measurement
  or explicit valid paging signal. Partial, invalid or zero-budget memory
  values no longer masquerade as confirmed free capacity.
- Failed graphics preparation now reports both the graphics error and loss
  of protected launch preparation, with a recovery-required Repair instruction
  instead of an incomplete “graphics unchanged” notice.
- Failed protected launch preparation now distinguishes verified rollback
  from recovery-required state. Startup no longer claims safe restoration
  when rollback failed; the authoritative recovery snapshot is retained.
- Deferred update cleanup now records why helper exit could not be confirmed.
  Recovery receipts, journals and verified rollback files remain unchanged;
  a diagnostic-write failure cannot authorize deletion or fail a ready update.
- Exited KF2 processes no longer remain bound while telemetry retains a
  process handle. Nonblocking liveness checks preserve PID-reuse protection
  and let normal restart handoff bind the replacement process.
- KF2 launch preparation and new telemetry bindings now recheck the discovered
  executable's volume/file identity. Replaced files require full installation
  validation; invalid replacements cannot reuse a prepared start or discard
  its recovery snapshot. Normal sampling adds no executable file queries.
- Graphics INI reads now reject incomplete, non-finite, overflowing, and
  out-of-range numbers before converting them to resolution or quality values.
  Invalid texture-tuple integers remain Custom without silent normalization.
- DXGI timing startup now stops its ETW session and joins any created worker
  if another worker cannot start, returning unavailable telemetry instead of
  terminating the application. A later attempt can start normally.
- Accessibility slider requests are bounded before numeric conversion, so
  extreme values reach the correct endpoint. Invalid slider metadata and
  non-finite requests are rejected without calling the setting callback.
- Automatic update checks no longer inherit an extended cooldown from future
  saved timestamps after a backward clock jump. Normal daily throttling and
  failed-check retry delays remain unchanged.
- Update-state write failures are now visible without discarding a completed
  check. Don't show again changes only after its preference is saved; retrying
  a successful save clears the matching warning. Failed checks no longer
  write unchanged cache data twice.
- Update installation, Auto Repair and manual package import now share a busy
  gate. Repair cannot start during an update check or installation; updates
  cannot install during Repair. Buttons match the gate, and safe close-waiting
  and verified repair rollback remain unchanged.
- Embedded overlay PNGs now require a loaded handle, readable bytes and a
  non-empty buffer before decoding. Failed resources keep the procedural
  overlay fallback without breaking otherwise valid images.
- FleX recovery now uses the remaining transaction-hash-verified original when
  the other recovery copy is missing, damaged, or unreadable. Diagnostics
  identify the selected source without weakening marker-free legacy recovery.
- Failed live graphics restoration now remains pending across telemetry
  reconnects. Adaptive preserves the last confirmed quality and retries in the
  background before resuming control instead of assuming an unconfirmed 100%.
- Target FPS changes while KF2 is stopped now confirm its native startup cap
  before publishing success. Failed writes restore the previous target and cap;
  incomplete rollback is reported as requiring repair.
- Protected launch preparation reports allocation failures instead of terminating
  the Optimizer, discards failed previews, and keeps the existing rollback path.
- Optional crash-recorder setup no longer terminates startup on allocation or
  filesystem exceptions; failed setup leaves no partial reservation and preserves
  the existing exception filter.
- Overlay fullscreen compatibility now stages only the display-mode delta and
  rejects invalid graphics choices before any preset lookup.
- Malformed KF2 texture-group tuples now fail closed instead of reporting
  unverified texture-resolution or filtering changes as saved.
- Failed automatic update checks now keep the last successful result and retry
  after a bounded short delay instead of suppressing retries for 24 hours.
- Failed offline and online graphics baseline or menu-restore readbacks now
  retry with bounded backoff instead of repeating writes every frame.
- The online Adaptive listener now recovers with bounded backoff after socket
  or corpse-controller loss, including safe reset across map travel.
- The live KF2 graphics-menu readback now includes the applied film-grain
  slider value, so its verified snapshot can no longer show a stale local
  percentage.
- Frozen-corpse ownership is now released only after verified restoration,
  confirmed actor reuse, or world destruction. Pool removal and reused actors
  can no longer leave a live corpse non-physical, non-colliding, or unticked.
- Kept schema 7 living-Zed telemetry in sync between the UnrealScript producer
  and native parser, preventing complete snapshots from being discarded when
  offscreen skeleton-update counts are present.
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
- Tooltips now separate the control name, current slider value, and detailed
  explanation in a larger adaptive card. Refined easing, navigation emphasis,
  and metric-card typography improve readability without adding a UI runtime.
- Removed the redundant Display control from Game graphics because protected
  Optimizer sessions already use borderless fullscreen. Display-mode reading,
  validation, and safe session handling remain internal.
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
- Re-armed selected-map warming after travel so consecutive rotations to the
  same map are prepared again without retaining the previous world.
- Prevented transient loading, menu, trader, and early post-map frames from
  entering Adaptive decisions or depressing the displayed 1% low window.
- Preserved one native graphics baseline across consecutive maps so recovery
  never treats a previously reduced value as the user's new 100% setting.
- Restored the exact server- or mod-provided online corpse limit when Adaptive
  is disabled or an online world ends, with verified readback before the
  captured value is released.
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
- Bound ambient-particle idle recovery to each emitter lifetime, so a recreated
  component cannot inherit or restore another component's saved value.
- Fixed repeated native corpse wakes being fought by Distance Sleep. Per-corpse
  backoff grows from 2 to 30 seconds, expired records are reclaimed, and other
  eligible corpses remain unrestricted.
- Corpse sleep now requires settled motion, preserves current death animation,
  avoids rapid sleep/wake loops, and keeps an unconditional 800-unit safety
  radius around the player under every pressure level.
- Fixed reduced corpse LOD, final-pose skeleton state, frozen physics, tick, and
  collision being unnecessarily restored after a corpse was already finalized.
- Preserved newer KF2-owned living-Zed offscreen animation flags across repeated
  optimizer reduce/restore cycles, including asymmetric native flag pairs.
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
- Invalidated published and in-flight frame metrics immediately when DXGI
  reports an unsupported event schema, preventing stale Adaptive samples.
- Preserved the authenticated Adaptive command sequence across recoverable
  telemetry rebinds and stopped safely instead of reusing sequence numbers.
- Bounded Adaptive control connections with a one-request deadline and
  destroyed their child actors after success, rejection, disconnect, or timeout.
- Prevented continuous live-FPS drain requests from starving the bounded frame
  windows Adaptive needs after maps, target changes, and quality actions.
- Isolated transient FleX availability changes from in-flight graphics actions,
  while stale FleX receipts remain rejected after their own capability change.
- Fixed FleX detection after graphics changes and settings restarts. User-
  enabled FleX runs at the verified minimum solver level; user-disabled FleX
  remains off and is never enabled by Adaptive.
- Reduced the fixed FleX minimum hot path to an immediate one-substep safety
  clamp with cached export resolution. It no longer performs solver scans,
  locking, warm-up, heartbeat evaluation, or Adaptive decisions per update.
- Bound FleX laboratory backups, transaction markers, and installed forwarders
  to the exact bytes that were verified, rejecting source-file replacement
  races without replacing the active runtime or losing recovery evidence.
- Fixed invalid Ultra shadow validation and other catalog/readback mismatches
  that could incorrectly report a safe KF2 setting as missing or out of range.
- Fixed stale or mismatched compiled telemetry modules entering a package.
  Builds now bind the executable to the current source module and verify the
  module, manifest, hashes, documentation, and portable package shape.
- Restored KF2's original AI-count and wave-timing log switches after normal
  shutdown and stale-session recovery, including settings that were absent.
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
