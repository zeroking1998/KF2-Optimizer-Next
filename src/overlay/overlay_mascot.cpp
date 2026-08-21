#include "overlay_window_internal.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace kf2::overlay::detail {

MascotAnimationAsset load_mascot_animation_asset() {
    MascotAnimationAsset asset;
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(
        module, MAKEINTRESOURCEW(kPremiumMascotAnimationResource), RT_RCDATA);
    if (!resource) return asset;
    const HGLOBAL loaded = LoadResource(module, resource);
    const auto* bytes = static_cast<const char*>(LockResource(loaded));
    const DWORD size = SizeofResource(module, resource);
    if (!loaded || !bytes || size == 0) return asset;
    const std::string_view text(bytes, size);
    const auto number = [&](std::string_view key, float fallback) {
        const std::string token = std::string(key) + "=";
        const std::size_t start = text.find(token);
        if (start == std::string_view::npos) return fallback;
        const std::size_t value_start = start + token.size();
        const std::size_t end = text.find_first_of("\r\n", value_start);
        try {
            return std::stof(std::string(
                text.substr(value_start, end - value_start)));
        } catch (...) {
            return fallback;
        }
    };
    asset.sample_rate_fps = number("sample_rate_fps", asset.sample_rate_fps);
    asset.idle_period_ms = number("idle_period_ms", asset.idle_period_ms);
    asset.idle_body_amplitude = number("idle_body_amplitude", asset.idle_body_amplitude);
    asset.idle_hand_amplitude = number("idle_hand_amplitude", asset.idle_hand_amplitude);
    asset.dock_transition_ms = number("dock_transition_ms", asset.dock_transition_ms);
    asset.dock_reach = number("dock_reach", asset.dock_reach);
    asset.dock_impact_reach = number("dock_impact_reach", asset.dock_impact_reach);
    asset.grip_micro_amplitude = number("grip_micro_amplitude", asset.grip_micro_amplitude);
    asset.leg_step_amplitude = number("leg_step_amplitude", asset.leg_step_amplitude);
    asset.variant_count = std::max(1, static_cast<int>(number(
        "variant_count", static_cast<float>(asset.variant_count))));
    asset.blend_in_fast = number("blend_in_fast", asset.blend_in_fast);
    asset.blend_out_soft = number("blend_out_soft", asset.blend_out_soft);
    return asset;
}

void draw_mood_character(
    OverlayWindowState& state,
    const D2D1_MATRIX_3X2_F& base_transform,
    float linear, float x, float y, float mood,
    ULONGLONG reaction_started, float reaction_strength,
    float tug_offset, float tug_intensity) {
            const float reaction_phase = reaction_started == 0 ? 1.0F :
                std::min(1.0F, static_cast<float>(GetTickCount64() -
                    reaction_started) / 720.0F);
            const float energy = (1.0F - reaction_phase) * reaction_strength;
            const float happy_bounce = mood > 0.0F
                ? std::sin(reaction_phase * 12.5663706F) * energy * mood * 1.7F : 0.0F;
            const float sad_tremble = mood < -0.35F
                ? std::sin(reaction_phase * 31.4159265F) * energy * -mood * 1.1F : 0.0F;
            const float load = std::clamp(tug_intensity, 0.0F, 1.0F);
            const auto& rig = state.mascot_animation;
            const auto idle_period = static_cast<ULONGLONG>(
                std::max(100.0F, rig.idle_period_ms));
            const float idle_phase = static_cast<float>(GetTickCount64() % idle_period) /
                                     static_cast<float>(idle_period) * 6.28318531F +
                                     x * 0.017F;
            const float idle_bob = std::sin(idle_phase) * rig.idle_body_amplitude *
                                   (1.0F - load);
            const float dock_age = state.dock_changed_ms == 0 ? 1.0F :
                std::min(1.0F, static_cast<float>(GetTickCount64() -
                    state.dock_changed_ms) / rig.dock_transition_ms);
            const float catch_force = state.animating
                ? std::sin(std::clamp(linear, 0.0F, 1.0F) * 3.14159265F) *
                      state.dock_impact
                : (1.0F - dock_age) * state.dock_impact * 0.35F;
            const float edge_side = static_cast<float>(state.dock_horizontal);
            const float variant_phase = idle_phase +
                                        state.dock_variant * 0.83F;
            x += sad_tremble;
            x += edge_side * catch_force * 1.4F;
            y -= happy_bounce;
            const float body_tug = tug_offset * 0.56F;
            y += body_tug + idle_bob;
            y += std::max(0.0F, -mood) * 1.2F;
            if (state.mascot_bitmap) {
                const bool is_low_character = x > 250.0F;
                POINT cursor{};
                GetCursorPos(&cursor);
                const float cursor_x = static_cast<float>(
                    cursor.x - state.current_bounds.left);
                const float cursor_y = static_cast<float>(
                    cursor.y - state.current_bounds.top);
                const float pointer_dx = cursor_x - x;
                const float pointer_dy = cursor_y - (y + 14.0F);
                const bool pointer_hover = std::fabs(pointer_dx) <= 19.0F &&
                                           std::fabs(pointer_dy) <= 34.0F;
                const float pointer_lean = pointer_hover
                    ? std::clamp(pointer_dx / 19.0F, -1.0F, 1.0F)
                    : 0.0F;
                ID2D1Bitmap* character_bitmap =
                    is_low_character && state.low_mascot_bitmap
                        ? state.low_mascot_bitmap.Get()
                        : state.mascot_bitmap.Get();
                const float idle_amount = 1.0F - load;
                const float breath = std::sin(idle_phase) * idle_amount;
                const float weight_shift = std::sin(idle_phase * 0.53F + 0.7F) *
                                           idle_amount;
                const float micro_impulse = std::pow(
                    std::max(0.0F, std::sin(idle_phase * 0.37F - 1.1F)), 10.0F) *
                    idle_amount;
                const float strain_scale = 1.0F + load * 0.055F;
                const float hover_scale = pointer_hover ? 1.045F : 1.0F;
                constexpr float kCreatureWidth = 34.0F;
                constexpr float kCreatureHeight = 64.0F;
                const float creature_width = kCreatureWidth * strain_scale * hover_scale *
                                             (1.0F + breath * 0.012F);
                const float creature_height = kCreatureHeight * strain_scale * hover_scale *
                                              (1.0F + breath * 0.022F);
                const float idle_x = weight_shift * 0.55F +
                                     edge_side * micro_impulse * 0.35F +
                                     pointer_lean * 0.55F;
                const float idle_y = -std::fabs(breath) * 0.28F -
                                     micro_impulse * 0.32F +
                                     (pointer_hover ? -0.65F : 0.0F);
                const D2D1_RECT_F destination = D2D1::RectF(
                    x + idle_x - creature_width * 0.5F,
                    y + idle_y - 18.0F - (creature_height - kCreatureHeight) * 0.5F,
                    x + idle_x + creature_width * 0.5F,
                    y + idle_y - 18.0F + creature_height);
                const float sway_degrees = weight_shift * 0.75F +
                                           micro_impulse * edge_side * 0.45F +
                                           pointer_lean * 2.2F;
                state.render_target->SetTransform(
                    D2D1::Matrix3x2F::Rotation(
                        sway_degrees, D2D1::Point2F(x, y + 14.0F)) *
                    base_transform);
                constexpr int kIdleFrameCount = 8;
                constexpr int kIdleColumns = 4;
                constexpr int kIdleRows = 2;
                const D2D1_SIZE_F sheet_size = character_bitmap->GetSize();
                const float frame_width = sheet_size.width / kIdleColumns;
                const float frame_height = sheet_size.height / kIdleRows;
                const float character_period = static_cast<float>(idle_period) *
                                               (is_low_character ? 0.83F : 1.0F);
                const float character_offset = is_low_character ? 0.37F : 0.0F;
                const float normalized_idle = std::fmod(
                    static_cast<float>(GetTickCount64()), character_period) /
                    character_period + character_offset;
                const float frame_position = normalized_idle * kIdleFrameCount;
                const int frame_index = static_cast<int>(std::floor(frame_position)) %
                                        kIdleFrameCount;
                const auto source_for_frame = [&](int frame) {
                    const int column = frame % kIdleColumns;
                    const int row = frame / kIdleColumns;
                    return D2D1::RectF(
                        column * frame_width, row * frame_height,
                        (column + 1) * frame_width, (row + 1) * frame_height);
                };
                state.render_target->DrawBitmap(
                    character_bitmap, destination, 1.0F,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    source_for_frame(frame_index));
                state.render_target->SetTransform(base_transform);
                return;
            }
            const float red = std::clamp(-mood, 0.0F, 1.0F);
            const float green = std::clamp(mood, 0.0F, 1.0F);
            const D2D1_COLOR_F face_color = D2D1::ColorF(
                0.25F + 0.55F * red - 0.04F * green,
                0.56F + 0.16F * green - 0.30F * red,
                0.38F - 0.16F * red + 0.05F * green, 1.0F);
            state.mascot_fill->SetColor(face_color);
            if (energy > 0.25F) {
                state.mascot_fill->SetOpacity(std::min(0.38F, energy * 0.38F));
                state.render_target->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(x, y), 9.0F + energy * 2.0F,
                                  9.0F + energy * 2.0F),
                    state.mascot_fill.Get(), 2.0F);
                state.mascot_fill->SetOpacity(1.0F);
            }

            // Fully separated mini rig: two-bone arms, palms, torso and head.
            const float torso_top = y + 5.0F;
            const float torso_bottom = y + 14.0F;
            const float left_shoulder_x = x - 3.8F;
            const float right_shoulder_x = x + 3.8F;
            const float shoulder_y = y + 7.2F;
            const float idle_hand_wave = std::sin(idle_phase * 1.7F) *
                                         rig.idle_hand_amplitude *
                                         (1.0F - load);
            const bool edge_grip = state.has_target && state.target.visible;
            const float grip_y_shift = std::sin(variant_phase * 1.3F) *
                                       rig.grip_micro_amplitude;
            const D2D1_POINT_2F left_hand = edge_grip
                ? D2D1::Point2F(x + edge_side *
                                 (rig.dock_reach + catch_force * rig.dock_impact_reach),
                                 y + 5.0F + grip_y_shift)
                : load > 0.02F
                ? D2D1::Point2F(x - 7.0F, y + 12.4F + tug_offset * 0.44F)
                : D2D1::Point2F(x - 7.2F, y + 11.0F + idle_hand_wave);
            const bool one_hand_variant = state.dock_variant == 1 ||
                                          state.dock_variant == 4;
            const D2D1_POINT_2F right_hand = edge_grip && !one_hand_variant
                ? D2D1::Point2F(x + edge_side *
                                 (rig.dock_reach + 0.2F + catch_force *
                                  rig.dock_impact_reach * 0.8F),
                                 y + 10.2F - grip_y_shift)
                : load > 0.02F
                ? D2D1::Point2F(x - 6.8F, y + 18.0F + tug_offset * 0.66F)
                : D2D1::Point2F(x + 7.2F, y + 10.8F - idle_hand_wave);
            const D2D1_POINT_2F left_elbow = D2D1::Point2F(
                (left_shoulder_x + left_hand.x) * 0.5F - 1.0F,
                (shoulder_y + left_hand.y) * 0.5F + load * 0.8F);
            const D2D1_POINT_2F right_elbow = D2D1::Point2F(
                (right_shoulder_x + right_hand.x) * 0.5F + (1.0F - load),
                (shoulder_y + right_hand.y) * 0.5F + load * 0.5F);
            const auto limb = [&](D2D1_POINT_2F a, D2D1_POINT_2F joint,
                                  D2D1_POINT_2F hand) {
                state.render_target->DrawLine(a, joint,
                    state.mascot_fill.Get(), 2.2F);
                state.render_target->DrawLine(joint, hand,
                    state.mascot_fill.Get(), 2.0F);
                state.render_target->FillEllipse(
                    D2D1::Ellipse(joint, 1.05F, 1.05F), state.mascot_fill.Get());
                state.render_target->FillEllipse(
                    D2D1::Ellipse(hand, 1.65F, 1.45F), state.mascot_fill.Get());
                state.render_target->DrawEllipse(
                    D2D1::Ellipse(hand, 1.65F, 1.45F),
                    state.mascot_ink.Get(), 0.55F);
                const float claw_direction = hand.x >= x ? 1.0F : -1.0F;
                for (int finger = -1; finger <= 1; ++finger) {
                    state.render_target->DrawLine(
                        D2D1::Point2F(hand.x + claw_direction * 0.8F,
                                     hand.y + finger * 0.48F),
                        D2D1::Point2F(hand.x + claw_direction * 2.25F,
                                     hand.y + finger * 0.72F),
                        state.mascot_highlight.Get(), 0.55F);
                }
            };
            limb(D2D1::Point2F(left_shoulder_x, shoulder_y),
                 left_elbow, left_hand);
            limb(D2D1::Point2F(right_shoulder_x, shoulder_y),
                 right_elbow, right_hand);
            state.render_target->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x - 4.7F, torso_top,
                                               x + 4.7F, torso_bottom),
                                  3.5F, 3.5F), state.mascot_fill.Get());
            state.render_target->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x - 4.7F, torso_top,
                                               x + 4.7F, torso_bottom),
                                  3.5F, 3.5F), state.mascot_ink.Get(), 0.7F);

            // Independent two-bone legs with planted, clearly shaped feet.
            const float hip_y = torso_bottom - 1.0F;
            const float brace = edge_grip ? edge_side * (1.4F + catch_force) : 0.0F;
            const float step = std::sin(variant_phase * 0.72F) *
                               rig.leg_step_amplitude *
                               (1.0F - load);
            const D2D1_POINT_2F left_knee = D2D1::Point2F(
                x - 2.5F - brace, hip_y + 3.0F + step);
            const D2D1_POINT_2F right_knee = D2D1::Point2F(
                x + 2.5F - brace * 0.45F, hip_y + 3.1F - step);
            const bool hanging = edge_grip &&
                (state.dock_variant == 2 || state.dock_variant == 5);
            const D2D1_POINT_2F left_ankle = D2D1::Point2F(
                x - (hanging ? 1.8F : 3.8F) - brace,
                hip_y + (hanging ? 8.2F : 7.0F));
            const D2D1_POINT_2F right_ankle = D2D1::Point2F(
                x + (hanging ? 1.8F : 3.8F) - brace * 0.35F,
                hip_y + (hanging ? 8.5F : 7.0F));
            const auto leg = [&](D2D1_POINT_2F hip, D2D1_POINT_2F knee,
                                 D2D1_POINT_2F ankle, float foot_direction) {
                state.render_target->DrawLine(hip, knee,
                    state.mascot_fill.Get(), 2.3F);
                state.render_target->DrawLine(knee, ankle,
                    state.mascot_fill.Get(), 2.1F);
                state.render_target->FillEllipse(
                    D2D1::Ellipse(knee, 1.05F, 1.05F), state.mascot_fill.Get());
                const D2D1_RECT_F foot = D2D1::RectF(
                    ankle.x - (foot_direction < 0 ? 2.2F : 1.2F), ankle.y - 0.7F,
                    ankle.x + (foot_direction > 0 ? 2.2F : 1.2F), ankle.y + 1.25F);
                state.render_target->FillRoundedRectangle(
                    D2D1::RoundedRect(foot, 1.0F, 1.0F), state.mascot_fill.Get());
                state.render_target->DrawRoundedRectangle(
                    D2D1::RoundedRect(foot, 1.0F, 1.0F),
                    state.mascot_ink.Get(), 0.5F);
                for (int toe = -1; toe <= 1; ++toe) {
                    state.render_target->DrawLine(
                        D2D1::Point2F(ankle.x + toe * 0.55F, ankle.y + 0.6F),
                        D2D1::Point2F(ankle.x + toe * 0.8F +
                                         foot_direction * 1.8F,
                                     ankle.y + 1.55F),
                        state.mascot_highlight.Get(), 0.48F);
                }
            };
            leg(D2D1::Point2F(x - 2.1F, hip_y), left_knee, left_ankle, -1.0F);
            leg(D2D1::Point2F(x + 2.1F, hip_y), right_knee, right_ankle, 1.0F);

            state.render_target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(x, y), 7.0F, 7.2F),
                state.mascot_fill.Get());
            state.render_target->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(x, y), 7.0F, 7.2F),
                state.mascot_ink.Get(), 0.8F);
            state.render_target->DrawLine(
                D2D1::Point2F(x - 4.8F, y - 5.0F),
                D2D1::Point2F(x - 5.8F, y - 8.4F),
                state.mascot_fill.Get(), 1.6F);
            state.render_target->DrawLine(
                D2D1::Point2F(x + 1.0F, y - 6.8F),
                D2D1::Point2F(x + 2.0F, y - 9.2F),
                state.mascot_fill.Get(), 1.45F);
            state.render_target->DrawLine(
                D2D1::Point2F(x + 4.7F, y - 5.1F),
                D2D1::Point2F(x + 6.1F, y - 7.5F),
                state.mascot_fill.Get(), 1.35F);
            state.mascot_highlight->SetOpacity(0.42F);
            state.render_target->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(x - 1.8F, y - 2.1F), 3.6F, 3.2F),
                state.mascot_highlight.Get(), 0.8F);
            state.mascot_highlight->SetOpacity(1.0F);

            const float eye_y = y - 2.0F + std::max(0.0F, -mood) * 0.8F;
            state.render_target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(x - 2.35F, eye_y), 1.25F, 1.55F),
                state.mascot_highlight.Get());
            state.render_target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(x + 2.35F, eye_y), 1.25F, 1.55F),
                state.mascot_highlight.Get());
            const float pupil_pull = tug_offset == 0.0F ? 0.0F :
                std::clamp(tug_offset * 0.06F, -0.55F, 0.55F);
            const float pupil_look = edge_grip ? edge_side * 0.42F : 0.0F;
            state.render_target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(x - 2.35F + pupil_look,
                                            eye_y + pupil_pull),
                              0.58F, 0.76F), state.mascot_ink.Get());
            state.render_target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(x + 2.35F + pupil_look,
                                            eye_y + pupil_pull),
                              0.58F, 0.76F), state.mascot_ink.Get());
            const float brow_drop = load * 1.4F;
            state.render_target->DrawLine(
                D2D1::Point2F(x - 4.0F, eye_y - 2.6F + brow_drop),
                D2D1::Point2F(x - 1.1F, eye_y - 3.15F),
                state.mascot_ink.Get(), 0.9F + load * 0.4F);
            state.render_target->DrawLine(
                D2D1::Point2F(x + 1.1F, eye_y - 3.15F),
                D2D1::Point2F(x + 4.0F, eye_y - 2.6F + brow_drop),
                state.mascot_ink.Get(), 0.9F + load * 0.4F);
            const float mouth_y = y + 2.0F;
            const float mouth_center_y = load > 0.0F
                ? mouth_y - 1.2F * load
                : mouth_y + mood * 2.2F;
            state.render_target->DrawLine(D2D1::Point2F(x - 3.2F, mouth_y),
                                             D2D1::Point2F(x, mouth_center_y),
                                             state.mascot_ink.Get(), 1.05F);
            state.render_target->DrawLine(D2D1::Point2F(x, mouth_center_y),
                                             D2D1::Point2F(x + 3.2F, mouth_y),
                                             state.mascot_ink.Get(), 1.05F);
            state.render_target->DrawLine(
                D2D1::Point2F(x - 1.8F, mouth_y + 0.2F),
                D2D1::Point2F(x - 1.1F, mouth_y + 1.5F),
                state.mascot_highlight.Get(), 0.55F);
            state.render_target->DrawLine(
                D2D1::Point2F(x + 1.8F, mouth_y + 0.2F),
                D2D1::Point2F(x + 1.1F, mouth_y + 1.5F),
                state.mascot_highlight.Get(), 0.55F);
}

}  // namespace kf2::overlay::detail