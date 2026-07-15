#pragma once

#include <algorithm>

#include "khdays/assets/tex0.h"  // DecodedTexture
#include "khdays/game/renderer.h"
#include "khdays/game/settings.h"  // ScreenLayout

namespace khdays::game {

// Draw an image centred on the render target, scaled by an integer factor and
// offset vertically by `y_offset` (virtual pixels * scale).
inline void draw_centered(Renderer& r,
                          const khdays::assets::DecodedTexture& image,
                          const int scale, const int y_offset = 0) {
    const int w = image.width * scale;
    const int h = image.height * scale;
    r.draw_image(image.rgba.data(), image.width, image.height,
                 (r.width() - w) / 2, (r.height() - h) / 2 + y_offset, w, h);
}

// The Nintendo DS is two 256x192 screens. This is where each lands on the render
// target (integer-scaled to fit, centred, a small gap between them), arranged
// per the user's ScreenLayout preference (stacked or side by side), plus helpers
// to place a full screen or an overlay in either.
struct DualScreenLayout final {
    static constexpr int kScreenW = 256;
    static constexpr int kScreenH = 192;
    static constexpr int kGap = 8;

    int scale = 1;
    int top_x = 0;
    int top_y = 0;
    int bottom_x = 0;
    int bottom_y = 0;

    int screen_x(const bool bottom) const { return bottom ? bottom_x : top_x; }
    int screen_y(const bool bottom) const { return bottom ? bottom_y : top_y; }
};

inline DualScreenLayout dual_screen_layout(const Renderer& r) {
    DualScreenLayout l;
    constexpr int w = DualScreenLayout::kScreenW;
    constexpr int h = DualScreenLayout::kScreenH;
    constexpr int gap = DualScreenLayout::kGap;
    if (screen_layout() == ScreenLayout::Horizontal) {
        const int total_w = w * 2 + gap;
        l.scale = std::max(1, std::min(r.width() / total_w, r.height() / h));
        const int ox = (r.width() - total_w * l.scale) / 2;
        const int oy = (r.height() - h * l.scale) / 2;
        l.top_x = ox;
        l.top_y = oy;
        l.bottom_x = ox + (w + gap) * l.scale;
        l.bottom_y = oy;
    } else {
        const int total_h = h * 2 + gap;
        l.scale = std::max(1, std::min(r.width() / w, r.height() / total_h));
        const int ox = (r.width() - w * l.scale) / 2;
        const int oy = (r.height() - total_h * l.scale) / 2;
        l.top_x = ox;
        l.top_y = oy;
        l.bottom_x = ox;
        l.bottom_y = oy + (h + gap) * l.scale;
    }
    return l;
}

// Draw a full 256x192 screen image into the top or bottom slot.
inline void draw_screen(Renderer& r, const DualScreenLayout& l,
                        const khdays::assets::DecodedTexture& image,
                        const bool bottom) {
    r.draw_image(image.rgba.data(), image.width, image.height,
                 l.screen_x(bottom), l.screen_y(bottom),
                 DualScreenLayout::kScreenW * l.scale,
                 DualScreenLayout::kScreenH * l.scale);
}

// Draw an overlay image at virtual (vx, vy) within a screen, scaled to match.
inline void draw_overlay(Renderer& r, const DualScreenLayout& l,
                         const khdays::assets::DecodedTexture& image,
                         const int vx, const int vy, const bool bottom) {
    r.draw_image(image.rgba.data(), image.width, image.height,
                 l.screen_x(bottom) + vx * l.scale,
                 l.screen_y(bottom) + vy * l.scale, image.width * l.scale,
                 image.height * l.scale);
}

}  // namespace khdays::game
