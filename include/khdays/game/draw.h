#pragma once

#include <algorithm>

#include "khdays/assets/tex0.h"  // DecodedTexture
#include "khdays/game/renderer.h"

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

// The Nintendo DS is two stacked 256x192 screens. This is where the two screens
// land on the render target (integer-scaled to fit, centred, a small gap
// between them) plus helpers to place a full screen or an overlay in either.
struct DualScreenLayout final {
    static constexpr int kScreenW = 256;
    static constexpr int kScreenH = 192;
    static constexpr int kGap = 8;

    int scale = 1;
    int origin_x = 0;
    int top_y = 0;
    int bottom_y = 0;

    int screen_y(const bool bottom) const { return bottom ? bottom_y : top_y; }
};

inline DualScreenLayout dual_screen_layout(const Renderer& r) {
    DualScreenLayout l;
    const int total_h = DualScreenLayout::kScreenH * 2 + DualScreenLayout::kGap;
    l.scale = std::max(1, std::min(r.width() / DualScreenLayout::kScreenW,
                                   r.height() / total_h));
    l.origin_x = (r.width() - DualScreenLayout::kScreenW * l.scale) / 2;
    l.top_y = (r.height() - total_h * l.scale) / 2;
    l.bottom_y =
        l.top_y + (DualScreenLayout::kScreenH + DualScreenLayout::kGap) * l.scale;
    return l;
}

// Draw a full 256x192 screen image into the top or bottom slot.
inline void draw_screen(Renderer& r, const DualScreenLayout& l,
                        const khdays::assets::DecodedTexture& image,
                        const bool bottom) {
    r.draw_image(image.rgba.data(), image.width, image.height, l.origin_x,
                 l.screen_y(bottom), DualScreenLayout::kScreenW * l.scale,
                 DualScreenLayout::kScreenH * l.scale);
}

// Draw an overlay image at virtual (vx, vy) within a screen, scaled to match.
inline void draw_overlay(Renderer& r, const DualScreenLayout& l,
                         const khdays::assets::DecodedTexture& image,
                         const int vx, const int vy, const bool bottom) {
    r.draw_image(image.rgba.data(), image.width, image.height,
                 l.origin_x + vx * l.scale, l.screen_y(bottom) + vy * l.scale,
                 image.width * l.scale, image.height * l.scale);
}

}  // namespace khdays::game
