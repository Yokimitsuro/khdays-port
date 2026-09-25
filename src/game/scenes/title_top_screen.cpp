#include "khdays/game/scenes/title_top_screen.h"

#include <algorithm>
#include <map>
#include <string>

#include "khdays/assets/animation.h"  // sample_animation
#include "khdays/assets/mesh.h"       // compute_palette
#include "khdays/assets/screen.h"     // draw_ortho_model

namespace khdays::game::scenes {

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 192;

// func_02023d70's orthographic volume for the title (20.12 in the ROM).
constexpr float kOrthoHalfWidth = static_cast<float>(0x4d9a) / 4096.0F;
constexpr float kOrthoHalfHeight = static_cast<float>(0x3b33) / 4096.0F;

// func_ov000_0204e1dc on one 8-bit channel that came from 5 bits as
// (c << 3) | (c >> 2), so c >> 3 recovers the 5-bit value exactly.
std::uint8_t whiten(const std::uint8_t channel, const int level) {
    const int c = channel >> 3;
    const int w = c + ((level * (31 - c)) >> 4);
    return static_cast<std::uint8_t>((w << 3) | (w >> 2));
}

}  // namespace

bool TitleTopScreen::load() {
    background_ = khdays::resource::load_ui_background("ttl/ttl.p2", 1, 7, 3, 3);
    logo_ = khdays::resource::load_title_logo_model();
    frame_.width = kWidth;
    frame_.height = kHeight;
    frame_.rgba.assign(static_cast<std::size_t>(kWidth) * kHeight * 4U, 255U);
    return background_.has_value() && logo_.has_value();
}

int TitleTopScreen::logo_frames() const {
    return logo_ ? static_cast<int>(logo_->animation.frame_count) : 0;
}

const khdays::assets::DecodedTexture& TitleTopScreen::compose(
    const int logo_frame, const bool show_bg, const int level) {
    std::fill(frame_.rgba.begin(), frame_.rgba.end(), std::uint8_t{255});
    if (show_bg && background_
        && background_->rgba.size() == frame_.rgba.size()) {
        const auto& src = background_->rgba;
        for (std::size_t i = 0; i < src.size(); i += 4U) {
            frame_.rgba[i] = whiten(src[i], level);
            frame_.rgba[i + 1U] = whiten(src[i + 1U], level);
            frame_.rgba[i + 2U] = whiten(src[i + 2U], level);
        }
    }
    if (!logo_) {
        return frame_;
    }
    auto& logo = *logo_;
    if (logo.model.skinning && logo.animation.frame_count > 0) {
        const auto objects = khdays::assets::sample_animation(
            logo.animation, static_cast<float>(logo_frame),
            logo.model.object_matrices);
        logo.model.palette =
            khdays::assets::compute_palette(*logo.model.skinning, objects);
    }
    std::map<std::string, float> alphas;
    if (logo.material_animation) {
        for (const auto& mesh : logo.model.meshes) {
            if (const auto state = khdays::assets::sample_material_color(
                    *logo.material_animation, mesh.material_name,
                    static_cast<std::uint32_t>(std::max(0, logo_frame)))) {
                alphas[mesh.material_name] = state->alpha;
            }
        }
    }
    khdays::assets::draw_ortho_model(
        frame_, logo.model, logo.textures,
        khdays::assets::OrthoView{-kOrthoHalfWidth, kOrthoHalfWidth,
                                  -kOrthoHalfHeight, kOrthoHalfHeight},
        alphas);
    return frame_;
}

}  // namespace khdays::game::scenes
