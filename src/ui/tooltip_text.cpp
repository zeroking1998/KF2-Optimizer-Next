#include "kf2/ui/shell_layout.hpp"

#include <string_view>

namespace kf2::ui {
namespace {

struct TooltipEntry {
    std::string_view action_id;
    std::wstring_view text;
};

constexpr TooltipEntry kTooltips[]{
    {"dashboard-launch", L"Starts KF2 through the Optimizer and begins Adaptive monitoring for the selected FPS target. It never enables NVIDIA FleX."},
    {"game-select-install", L"Selects the KF2 installation folder that the Optimizer will validate and use. No game file is changed by selecting it."},
    {"game-open-config", L"Opens KF2's local configuration folder in File Explorer."},

    {"header-update-check", L"Checks the official GitHub Releases page now. It only reports a newer version and never downloads or installs automatically."},
    {"settings-updates-check", L"Checks the official GitHub Releases page now. It only reports a newer version and never downloads or installs automatically."},
    {"header-update-install", L"Downloads and verifies the displayed portable Windows x64 release, then asks the update helper to install it with rollback protection."},
    {"settings-updates-install", L"Downloads and verifies the displayed portable Windows x64 release, then asks the update helper to install it with rollback protection."},
    {"settings-updates-automatic", L"Turns the once-per-24-hours startup update check on or off. Downloads and installation still always require your approval."},
    {"settings-updates-later", L"Hides the current update reminder without downloading or changing anything. You can check again at any time."},
    {"settings-updates-ignore", L"Hides this update dialog permanently for the displayed version. A later, newer version will still show a new dialog."},
    {"header-repair", L"Downloads the official release matching this exact installed version and repairs only missing or damaged managed files after SHA-256 verification."},
    {"diagnostics-repair-package", L"Imports missing or damaged managed files from a matching local release folder after version, identity, size, and SHA-256 checks."},

    {"diagnostics-backup", L"Creates and verifies a local backup of KFEngine.ini, KFGame.ini, and KFSystemSettings.ini. KF2 must be closed."},

    {"diagnostics-export-support", L"Creates a privacy-bounded support file in the portable Data folder. Nothing is uploaded automatically."},
    {"diagnostics-flex-restore", L"Restores protected KF2 files and configuration from the last verified original state. KF2 must be closed."},
    {"diagnostics-full-check", L"Checks package integrity, paths, backups, telemetry, and runtime readiness locally without changing gameplay settings."},
    {"diagnostics-open-data", L"Opens the portable Data folder containing settings, backups, reports, and logs. Nothing is uploaded."},
    {"diagnostics-open-log", L"Opens the local JSON session-event log used to explain Optimizer decisions, receipts, warnings, and errors."},

    {"settings-target-slider", L"Sets the FPS goal from 30 to 240 and saves it immediately. Adaptive compares live, average, and 1% low FPS against this target."},
    {"settings-corpses-slider", L"Sets the maximum corpse ceiling and saves it immediately. Adaptive may use fewer corpses under confirmed scene or frame-time pressure."},

    {"graphics-display", L"Windowed is easy to switch away from, Borderless fills the desktop, and Fullscreen can reduce presentation overhead."},
    {"graphics-resolution", L"Sets the number of output pixels. Higher resolutions look sharper but increase GPU work and VRAM use."},
    {"graphics-overall-quality", L"Changes the main quality controls together. Higher presets improve detail and effects but increase CPU, GPU, and VRAM demand."},
    {"graphics-vsync", L"On removes most screen tearing but may cap FPS and add input delay. Off favors lower latency but can show tearing."},
    {"graphics-variable-frame-rate", L"On lets KF2 run outside its built-in smooth-FPS range. Off uses the game's configured frame-rate smoothing limits."},
    {"graphics-environment-detail", L"Higher values keep detailed scenery visible farther away, increasing CPU work, GPU work, and VRAM use."},
    {"graphics-character-detail", L"Higher values keep detailed Zed and character models visible farther away, increasing GPU work and VRAM use."},
    {"graphics-fx", L"Higher values increase particle and visual-effect quality, which can raise CPU, GPU, and VRAM use during busy fights."},
    {"graphics-texture-resolution", L"Higher values keep sharper textures in memory and can use substantially more VRAM. Too little free VRAM can cause stutter."},
    {"graphics-texture-filtering", L"Controls texture sharpness at oblique angles from Bilinear up to 16x Anisotropic, with a small GPU cost."},
    {"graphics-shadow-quality", L"Controls shadow resolution and distance. Higher levels improve shadows and increase CPU/GPU load."},
    {"graphics-realtime-reflections", L"On adds dynamic reflections and raises GPU cost. Off reduces reflection detail and saves GPU time."},
    {"graphics-anti-aliasing", L"On smooths jagged edges with a small GPU cost and may soften the image. Off is sharper but shows more aliasing."},
    {"graphics-bloom", L"Controls the glow around bright lights. Higher levels strengthen the glow and add post-processing cost."},
    {"graphics-motion-blur", L"Blurs the image during camera or object movement. Turning it off gives a clearer moving image and lowers post-processing work."},
    {"graphics-ambient-occlusion", L"Adds contact shadows where objects meet. SSAO and HBAO+ improve depth but increase GPU load."},
    {"graphics-depth-of-field", L"On blurs areas outside the focus range and adds GPU work. Off keeps the image clearer and slightly reduces rendering cost."},
    {"graphics-volumetric-lighting", L"On draws visible light volumes and increases GPU work. Off removes those effects and saves GPU time."},
    {"graphics-lens-flares", L"On adds lens artifacts around bright lights with a small GPU cost. Off removes them."},
    {"graphics-light-shafts", L"On draws visible rays from strong lights and adds post-processing work. Off removes the rays and saves GPU time."},
    {"graphics-flex", L"Selects Off, Gibs, or Gibs and fluids for NVIDIA FleX. Only your selection and Apply graphics can enable it; Adaptive never does."},
    {"graphics-film-grain-slider", L"Adds film-like image noise. 0% is clean, 100% is normal strength, and 200% doubles the grain. It has little performance impact."},
    {"graphics-apply", L"Backs up the current KF2 INIs, writes the selected graphics choices atomically, verifies them, and keeps user Data unchanged."},
    {"graphics-reset", L"Returns the controls to balanced defaults while keeping the current display mode and resolution. NVIDIA FleX returns to Off."},

    {"overlay-toggle", L"Shows or hides the local telemetry overlay and saves the choice immediately. F10 performs the same action while KF2 runs."},
    {"overlay-position", L"Moves the overlay to the next screen corner and saves the position immediately."},
    {"overlay-scale-slider", L"Changes overlay size from 60% to 200% and saves it immediately. It does not change KF2's render resolution."},
    {"overlay-scale-reset", L"Returns the overlay to 100% size and saves the change immediately."},
    {"overlay-show-fps", L"Shows or hides live FPS in the overlay and saves the choice immediately."},
    {"overlay-show-frame-time", L"Shows or hides frame time in milliseconds in the overlay and saves the choice immediately."},
    {"overlay-show-cpu", L"Shows or hides KF2 CPU load in the overlay and saves the choice immediately."},
    {"overlay-show-gpu", L"Shows or hides total GPU load in the overlay and saves the choice immediately."},
    {"overlay-show-memory", L"Shows or hides RAM and VRAM usage in the overlay and saves the choice immediately."},

    {"advanced-one-frame-thread-lag", L"On can improve frame throughput by letting rendering stay one frame ahead, but adds input delay. Off lowers latency and may reduce throughput."},
    {"advanced-per-frame-sleep", L"On lets KF2 briefly sleep each frame, reducing busy CPU use but potentially adding latency or uneven frame pacing. Off avoids that sleep."},
    {"advanced-per-frame-yield", L"On lets other ready threads use the CPU each frame, which may reduce contention but can disturb frame pacing. Off keeps KF2 running without that yield."},
    {"advanced-background-level-streaming", L"On loads map content in the background, using extra RAM and disk activity to reduce large loading pauses. Off may cause longer blocking loads."},
    {"advanced-texture-streaming", L"On loads texture detail as needed, limiting VRAM use but allowing pop-in. Off may use much more VRAM and can stutter when VRAM fills."},
    {"advanced-priority-streaming", L"On gives urgent textures priority, improving nearby texture loading but increasing short I/O and VRAM pressure. Off uses normal streaming order."},
    {"advanced-dynamic-streaming", L"On continually adapts texture loading to the scene, helping control VRAM use with possible pop-in. Off uses less responsive streaming decisions."},
    {"advanced-hardware-shadow-filtering", L"On uses GPU filtering for smoother shadow edges and adds some GPU work. Off uses simpler shadow filtering."},
    {"advanced-downsampled-translucency", L"On renders translucent effects at lower resolution, saving GPU time and VRAM bandwidth but making them softer. Off keeps full-resolution effects."},
    {"advanced-floating-point-render-targets", L"On uses higher-precision lighting buffers and can noticeably increase VRAM use and bandwidth. Off uses lower-cost buffers and is recommended unless required."},
    {"advanced-max-multisamples", L"Higher sample counts smooth geometry edges but multiply render-target memory and GPU work. High values can exhaust VRAM and cause stutter."},
    {"advanced-screen-percentage-slider", L"Sets internal 3D render scale. 100% is native; 200% renders four times as many pixels, greatly increasing GPU work and VRAM use."},
    {"advanced-gore-level", L"Off removes blood and gibs, Reduced lowers them, and Full shows all configured gore. More gore can increase CPU, GPU, and memory use in busy scenes."},
    {"advanced-particle-percentage-slider", L"Controls how many configured particles are emitted. Higher values increase effect density and can raise CPU, GPU, and VRAM use during combat."},
    {"advanced-decal-lifetime-slider", L"Controls how long blood and impact marks remain. Longer times keep more decals in RAM and VRAM and add draw work, which can cause stutter in long fights."},
    {"advanced-apply", L"Backs up the KF2 INIs, writes these settings atomically, and verifies the result. Adaptive does not control them."},
    {"advanced-reset", L"Returns the controls to balanced defaults: 100% render scale, 100% particles, 30-second decals, and low-cost render buffers."},
};

}  // namespace

std::optional<std::wstring> action_help_text(std::string_view action_id) {
    for (const auto& entry : kTooltips) {
        if (entry.action_id == action_id) {
            return std::wstring{entry.text};
        }
    }
    return std::nullopt;
}

}  // namespace kf2::ui
