#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "kf2/overlay/overlay_window.hpp"

namespace kf2::overlay {

struct MascotAnimationAsset {
    float sample_rate_fps{120.0F};
    float idle_period_ms{5000.0F};
    float idle_body_amplitude{0.32F};
    float idle_hand_amplitude{0.65F};
    float dock_transition_ms{900.0F};
    float dock_reach{8.0F};
    float dock_impact_reach{2.0F};
    float grip_micro_amplitude{0.55F};
    float leg_step_amplitude{0.45F};
    int variant_count{6};
    float blend_in_fast{0.24F};
    float blend_out_soft{0.075F};
};

struct OverlayWindowState {
    HWND window{};
    HDC memory_dc{};
    HBITMAP bitmap{};
    HGDIOBJ old_bitmap{};
    SIZE bitmap_size{};
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> render_target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> foreground;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> muted;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> metric_panel;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> graph_line;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mascot_fill;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mascot_ink;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mascot_highlight;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> mascot_bitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> low_mascot_bitmap;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> title_format;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> value_format;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> metric_format;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> summary_format;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> system_value_format;
    MascotAnimationAsset mascot_animation;
    OverlayPresentation target;
    OverlayPresentation visual;
    RECT current_bounds{};
    RECT animation_from_bounds{};
    float animation_from_opacity{0.0F};
    float opacity{0.0F};
    ULONGLONG animation_started_ms{0};
    bool has_target{false};
    bool has_visual{false};
    bool animating{false};
    bool visibility_animation{false};
    bool appearing{false};
    bool metrics_initialized{false};
    bool metrics_animating{false};
    double displayed_fps{0.0};
    double displayed_average_fps{0.0};
    double displayed_one_percent_low_fps{0.0};
    double displayed_frame_time_ms{0.0};
    double displayed_cpu_percent{0.0};
    double displayed_gpu_percent{0.0};
    double displayed_process_ram_gib{0.0};
    double displayed_dedicated_vram_gib{0.0};
    double metrics_from_fps{0.0};
    double metrics_from_average_fps{0.0};
    double metrics_from_one_percent_low_fps{0.0};
    double metrics_from_frame_time_ms{0.0};
    double metrics_from_cpu_percent{0.0};
    double metrics_from_gpu_percent{0.0};
    double metrics_from_process_ram_gib{0.0};
    double metrics_from_dedicated_vram_gib{0.0};
    ULONGLONG metrics_started_ms{0};
    ULONGLONG system_metrics_sample_ms{0};
    ULONGLONG fps_bounce_started_ms{0};
    ULONGLONG average_bounce_started_ms{0};
    ULONGLONG low_bounce_started_ms{0};
    float fps_bounce_strength{0.0F};
    float average_bounce_strength{0.0F};
    float low_bounce_strength{0.0F};
    ULONGLONG frame_time_bounce_started_ms{0};
    ULONGLONG cpu_bounce_started_ms{0};
    ULONGLONG gpu_bounce_started_ms{0};
    std::array<bool, 3> cpu_changed_digits{};
    std::array<bool, 3> gpu_changed_digits{};
    ULONGLONG fps_trend_started_ms{0};
    float fps_trend_intensity{0.0F};
    int fps_trend_direction{0};
    ULONGLONG average_trend_started_ms{0};
    float average_trend_intensity{0.0F};
    int average_trend_direction{0};
    ULONGLONG low_trend_started_ms{0};
    float low_trend_intensity{0.0F};
    int low_trend_direction{0};
    double normal_average_fps{0.0};
    float average_mood{0.0F};
    float average_mood_target{0.0F};
    float low_mood{0.0F};
    float low_mood_target{0.0F};
    ULONGLONG average_mood_reaction_ms{0};
    ULONGLONG low_mood_reaction_ms{0};
    float average_mood_reaction{0.0F};
    float low_mood_reaction{0.0F};
    float average_tug_offset{0.0F};
    float average_tug_velocity{0.0F};
    float average_tug_load{0.0F};
    float low_tug_offset{0.0F};
    float low_tug_velocity{0.0F};
    float low_tug_load{0.0F};
    int dock_horizontal{1};
    int dock_vertical{-1};
    int dock_variant{0};
    float dock_impact{0.0F};
    ULONGLONG dock_changed_ms{0};
    std::array<float, 48> frame_time_history{};
    std::size_t frame_time_history_count{0};
    std::size_t frame_time_history_next{0};
    ULONGLONG frame_time_history_sample_ms{0};
    std::size_t renders{0};
    ~OverlayWindowState();
};


namespace detail {

inline constexpr wchar_t kClassName[] = L"KF2OptimizerNext-Overlay";
inline constexpr int kPremiumMascotAnimationResource = 201;
inline constexpr int kPremiumMutantRigPngResource = 202;
inline constexpr int kPremiumMutantLowIdlePngResource = 203;

[[nodiscard]] MascotAnimationAsset load_mascot_animation_asset();
[[nodiscard]] HWND create_overlay_native_window(HINSTANCE instance);
[[nodiscard]] bool same_rect(const RECT& left, const RECT& right);
[[nodiscard]] RECT visibility_pose(
    const RECT& bounds, float scale, LONG outward);
void draw_mood_character(
    OverlayWindowState& state,
    const D2D1_MATRIX_3X2_F& base_transform,
    float linear, float x, float y, float mood,
    ULONGLONG reaction_started, float reaction_strength,
    float tug_offset = 0.0F, float tug_intensity = 0.0F);

}  // namespace detail
}  // namespace kf2::overlay