# Native Application Architecture

KF2 Optimizer Next is a native C++20 Windows x64 application delivered as one
portable GUI executable. The current foundation contains a narrow application
entry point, deterministic build identity, typed results, portable state,
session recovery, bounded diagnostics, and an RAII Win32 window boundary.

The `game` module discovers only canonical KF2 installations with an exact x64
`Binaries/Win64/KFGame.exe` identity and a configuration root beneath the
allowed Documents tree. The `config` module parses verified UTF-8 INI files
losslessly, applies only catalogued typed keys, and produces an immutable
before/after preview. Manual requests and persisted locks override Adaptive.

The `backup` module performs compare-and-swap guarded changes. Before any
replacement it writes and verifies content-addressed SHA-256 objects, a manifest,
and an explicit transaction journal. Replacements are atomic, re-read after the
write, recoverable at every journal boundary, and byte-identically restorable.
A restore creates its own pre-restore backup. Retention keeps at least the newest
verified set; import/export accepts only versioned, catalogued setting requests.

The `telemetry` module binds samples to the exact KFGame executable, PID and
process-start identity. FPS and frame-time statistics come from statically
embedded PresentMon analysis and its process-filtered DXGI application-present
timestamps. The trace buffer is flushed periodically so low frame rates remain
live without waiting for shutdown. No PresentMon service, helper process,
injection or extra runtime DLL is required. CPU/RAM use documented process
queries. Total process CPU remains the user-facing utilization value; a
throttled per-thread sampler separately measures the busiest KF2 thread, total
logical-core equivalents, active CPU threads, dominant-thread CPU-time share
and the process' current logical/physical affinity capacity. A main-thread
limit is therefore not diluted by logical-processor count, and an externally
restricted affinity is reported without being changed. The visible GPU value
uses the dynamic P-state utilization domain from the locally installed NVIDIA
NVAPI driver so it matches MSI Afterburner's one-second metric; the application
never starts `nvidia-smi` or MSI Afterburner and does not bundle a driver DLL.
Local NVML is the secondary NVIDIA fallback and adapter-wide English PDH is the
vendor-neutral fallback. Process VRAM remains PID- and adapter-LUID-filtered.
Missing, stale, lossy or mismatched samples remain explicitly unavailable.

## Internal telemetry pipeline

The 120 ms application tick is a statically composed, ten-phase pipeline. Its
implementation is split into one orchestrator and seven stage sources:

```text
src/app/application_telemetry.cpp
src/features/telemetry/
  telemetry_frame.cpp
  telemetry_session_stage.cpp
  telemetry_collection_stage.cpp
  telemetry_flex_stage.cpp
  telemetry_adaptive_stage.cpp
  telemetry_effect_stage.cpp
  telemetry_presentation_stage.cpp
```

The order is fixed: attach sources; refresh the process-bound session gate;
observe FleX; inspect the exact window/process; drain PresentMon; capture one
typed frame; decide/apply FleX control; evaluate Adaptive and its permitted
persistence; derive presentation; publish the model and overlay. Typed early
outcomes stop the suffix when the session, Present stream, or completed frame
is unavailable. FleX observation deliberately precedes every window and
Present early return.

`TelemetryFrame` is the authoritative immutable value for one completed tick.
It owns one PID/start identity, one observation time, the inspected window,
frame metrics, optional CPU/GPU/RAM/VRAM, the captured game-log and FleX
observations, and normalized optimizer evidence. Collection samples each source
at most once and missing values remain absent. FleX, Adaptive, diagnostics,
model text, and overlay presentation consume that same frame; they do not
resample. Only read-only last-completed diagnostic/report projections survive
the tick, and they never feed a later decision.

Runtime, configuration, and protected-session mutations are restricted to the
effect stage: process-local FleX control, Adaptive profile persistence with
rollback, and verified protected session restoration. The FleX observation
stage separately persists only its bounded diagnostic report. Collection
cannot write configuration or update the overlay; Adaptive cannot read
platform samplers; presentation cannot sample or write configuration/FleX;
effects cannot start or drain measurement sources. The session stage alone
owns process/source attachment, detachment, and Launch.log binding. A mandatory
CTest architecture check verifies the complete source list and these
directional boundaries.

Stage headers forward-declare `UiRuntime`; only stage implementations may see
the shared runtime. Passing `UiRuntime&` into those implementations is a
deliberate temporary dependency retained for behavioral compatibility. Narrow
service ports and grouped runtime state belong to the later architecture
roadmap and are not claimed by the current implementation.

The same process owns a click-through, no-activate layered overlay window. A
pure visibility policy hides it whenever KF2 is minimized, cloaked, not the
foreground window, has invalid geometry, changes identity, or lacks fresh FPS.
Direct2D renders a complete DIB before one atomic layered-window update, and
unchanged presentations do not redraw.

The `optimizer` module has one Adaptive decision path. It validates fresh
identity-bound evidence, applies quality bounds and emits catalog-backed plans
with source, reason and confidence. It evaluates only active gameplay; menus,
loading screens and shutdown frames reset the fast controller window and
cannot alter the persisted baseline. A separate slow persistence gate requires
8 seconds of stable degradation or 45 seconds of stable recovery before saving
a next-launch profile; recovery additionally requires the governor's verified
stable-headroom state. CPU evidence is classified as idle/frame-limited,
main-thread dominant, partially parallel or broadly parallel. Frame pressure
plus a dominant thread can prove a CPU bottleneck below a brittle 90-percent
point threshold, while broadly parallel saturation is evaluated against the
observed affinity capacity. Confirmed CPU classifications use a two-second
evidence hold to avoid CPU/unknown threshold flapping, while another proven
bottleneck replaces the hold immediately. Explicit manual values and
locks replace Adaptive values per key. A separate deterministic resource
pressure estimator fuses process CPU, busiest-thread occupancy, effective
core use, KF2-attributed GPU engines, whole-adapter GPU contention, live DXGI
local-memory usage/budget, physical RAM and Windows commit reserve. Each
resource reports raw and asymmetric-smoothed pressure, confidence and trend.
The estimator expresses current and two-second projected frame-budget deficits
in milliseconds, resets across telemetry boundaries, and permits quality
recovery only while every resource has measured reserve. Whole-adapter load
without matching KF2 GPU work remains visible but cannot by itself become a
confirmed KF2 GPU cause.
Config-only Adaptive plans run before an app-started session through
snapshot, compare-and-swap backup, atomic apply and readback; the exact pre-game
INIs are restored afterward. General Adaptive has no broad Online/Offline
actuation gate: each source and actuator independently reports its runtime
capability, while unproved live knobs remain unavailable or shadow-only.
The protected startup plan also writes only catalogued native KF2 keys for its
asynchronous physics scene, one-frame render-thread pipeline, and texture
streaming memory profile. The last renderer reported by KF2 is matched uniquely
to a physical adapter before its dedicated VRAM selects a deterministic bounded
pool tier. A sole physical adapter is already unambiguous; otherwise missing or
ambiguous renderer evidence preserves the existing streaming values. These
startup values never become live mid-match actuators and remain covered by the
same byte-exact session restore.

Verified standalone gameplay also exposes a session-scoped loopback actuator.
The optimizer sends a monotonically sequenced command with a random 128-bit
token and a CPU, GPU, VRAM, RAM, mixed or recovery group. KF2 rebuilds the
requested group from its current graphics settings, changes only the owned
fields and returns `APPLIED` only after exact readback. Intervention lowers one
native tier, emergency may lower two and stable recovery raises one. Missing or
stale telemetry, Zed Time, an online/unknown session, shadow mode, missing bridge
capability or a readback mismatch prevents the change. Display mode, resolution,
VSync, variable frame rate, anti-aliasing and FleX are not owned by this path.
The four resource groups retain separate quality levels: CPU pressure controls
scene/mesh/particle LOD, light functions and the three official cosmetic corpse
collision switches; GPU pressure controls shadows,
post-processing, reflections and cosmetic lighting; VRAM pressure controls
texture and shadowmap bias plus anisotropy; RAM pressure controls bounded effect,
decal and lifetime budgets. The displayed effective quality is the lowest group,
not a value reused as the starting point for unrelated resources.

Adaptive settings use three operational safety levels. Verified live settings
require an authenticated command, exact engine readback and session restore.
Startup-only settings use an atomic pre-launch snapshot/apply/readback transaction
and are never presented as mid-match changes. User-owned or protected settings,
including display, anti-aliasing, FleX mode, corpse maximum and living-enemy
collision/navigation, are never changed by Adaptive. Corpse collision is a
separate verified live CPU group: collision between sleeping/dead corpses is
disabled at 80%, dead-to-dead collision at 60%, and living-to-corpse collision
at 40%. Every switch requires exact engine readback and restores the captured
user value on recovery or session shutdown.

For a protected app-started standalone session, the optional telemetry
package can also enable a narrowly bounded corpse-stagger actuator. The chosen
`MaxDeadBodies` value from 4-2000 is the stable user ceiling. Merely reaching or
crossing it performs no deletion. Only while a fresh current sample and the
hysteresis-confirmed controller both report pressure against the exact
target-relative 60/59, 60/58 and 60/57 bands are
bounded steps applied to a separate runtime threshold and excess confirmed
`KFGoreManager.CorpsePool` members removed through KF2's official
`RemoveAndDeleteCorpse` routine, one at a time, no faster than once per 450 ms
and never during Zed Time. Recovery stops removal immediately and raises the
runtime threshold gradually back toward the selected ceiling. The same
controller has a separate distance-first physics stage. At baseline it sleeps
only an old slow corpse with stale native `LastRenderTime` beyond 1,200 units,
at most once per 400 ms. Fresh visible enemy/corpse density or confirmed
target-relative frame pressure moves the threshold to 1,000/850 units and the
interval to 200/100 ms; FPS is an amplifier rather than the sole gate.
Optimizer-slept bodies are identity
tracked and use the official `WakeRigidBody` path inside 800 units. Only then
may the pressure path raise native `MinLodModel` on one distant visible corpse
every 400/200/100 ms. It starts at stage 2 outside 300 units and advances to
  stages 3/4/5 at 500/800/1,200 units. Visible-enemy pressure scales continuously
  from five to eighty Zeds and consumes every stage the mesh actually exposes;
forced, near, sleeping, recovered and session-ending LOD
state is preserved or restored. After eligible distance/LOD work is exhausted,
one old, slow, non-death-animation visible ragdoll may sleep at most once per
750/350 ms, but never inside the fixed 800-unit player safety radius. Rejected
nearby candidates are reported with bounded policy evidence rather than
mutated. KF2/UE3 native frustum and occlusion culling already avoid drawing
living-enemy primitives outside the view or behind occluders. No living pawn is
manually hidden or mutated and its collision/navigation are never changed. If this
protected provider is absent, only its corpse/ragdoll/LOD capabilities report
`UNAVAILABLE`; the rest of General Adaptive continues independently.

The `app` module owns startup and shutdown. A heap-stable `UiRuntime` owns the
model, typed event controller, window, renderer and UI Automation provider in
dependency order. It destroys those components in reverse order before writing
the clean-session marker. The product intentionally has no Windows system-sound
service; interaction feedback is visual and cannot trigger desktop sound cues.

UI action and slider strings are compatibility-only boundary values. The app
resolves them once through a static typed catalog that owns canonical aliases,
feature ownership, restricted-mode access policy, payload kind, and slider
ranges. The 68 UI action names resolve to 60 active typed action IDs. Every active ID has
exactly one direct handler in one of seven modules: Navigation, Game,
Settings, Overlay, Diagnostics, Optimizer, or Backup. A complete registry is
validated before dispatch, and the central start-mode access gate runs before
the single selected handler. Unknown IDs, invalid payloads, incomplete
registries, and duplicate ownership fail closed without an alternate action
path. Slider controls retain their separate typed catalog and mutation gate.
CTest enforces source-list completeness, module ownership, UI-boundary string
containment, and the absence of migration fallbacks. Handlers temporarily
receive the shared `UiRuntime&` to preserve established behavior and lifetime;
the narrower feature ports described by the long-term design are not yet
implemented.

`platform/windows` owns raw Windows handles and translates documented messages
into typed events. `ui` owns the platform-independent semantic layout in DIPs,
Direct2D/DirectWrite rendering, and UI Automation nodes backed by the same
layout. High contrast is resolved by the theme boundary.
No legacy implementation is linked or copied.

The optional FleX/runtime laboratory is integrated but default-off. Its
read-only audit validates the actual x64 PE, full export surface and SHA-256
without loading the DLL. The legacy writable hook remains discarded; the new
minimal unchanged offline laboratory wrapper is isolated, transactional and gated because
historical proxy has documented real `0xc0000005` failures, and is not required
by telemetry, overlay, configuration safety or the optimizer.
