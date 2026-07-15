#include "khdays/game/software_renderer.h"

#include <cstddef>

namespace khdays::game {

SoftwareRenderer::SoftwareRenderer(const int width, const int height)
    : width_(width),
      height_(height),
      rgba_(static_cast<std::size_t>(width) * height * 4U, 0U) {}

void SoftwareRenderer::clear(const Color color) {
    for (std::size_t i = 0; i + 4U <= rgba_.size(); i += 4U) {
        rgba_[i] = color.r;
        rgba_[i + 1U] = color.g;
        rgba_[i + 2U] = color.b;
        rgba_[i + 3U] = color.a;
    }
}

void SoftwareRenderer::fill_overlay(const Color color) {
    if (color.a == 0U) {
        return;
    }
    const int a = color.a;
    for (std::size_t i = 0; i + 4U <= rgba_.size(); i += 4U) {
        rgba_[i] = static_cast<std::uint8_t>((color.r * a + rgba_[i] * (255 - a)) / 255);
        rgba_[i + 1U] = static_cast<std::uint8_t>((color.g * a + rgba_[i + 1U] * (255 - a)) / 255);
        rgba_[i + 2U] = static_cast<std::uint8_t>((color.b * a + rgba_[i + 2U] * (255 - a)) / 255);
        rgba_[i + 3U] = 255U;
    }
}

void SoftwareRenderer::draw_image(const std::uint8_t* src, int sw, int sh,
                                  const int x, const int y, int dw, int dh) {
    if (src == nullptr || sw <= 0 || sh <= 0) {
        return;
    }
    if (dw <= 0) {
        dw = sw;
    }
    if (dh <= 0) {
        dh = sh;
    }
    for (int j = 0; j < dh; ++j) {
        const int dy = y + j;
        if (dy < 0 || dy >= height_) {
            continue;
        }
        const int sy = j * sh / dh;
        for (int i = 0; i < dw; ++i) {
            const int dx = x + i;
            if (dx < 0 || dx >= width_) {
                continue;
            }
            const int sx = i * sw / dw;
            const std::uint8_t* p =
                src + (static_cast<std::size_t>(sy) * sw + sx) * 4U;
            const std::uint8_t a = p[3];
            if (a == 0U) {
                continue;
            }
            std::uint8_t* d =
                &rgba_[(static_cast<std::size_t>(dy) * width_ + dx) * 4U];
            if (a == 255U) {
                d[0] = p[0];
                d[1] = p[1];
                d[2] = p[2];
            } else {
                d[0] = static_cast<std::uint8_t>((p[0] * a + d[0] * (255 - a)) / 255);
                d[1] = static_cast<std::uint8_t>((p[1] * a + d[1] * (255 - a)) / 255);
                d[2] = static_cast<std::uint8_t>((p[2] * a + d[2] * (255 - a)) / 255);
            }
            d[3] = 255U;
        }
    }
}

khdays::assets::DecodedTexture SoftwareRenderer::snapshot() const {
    khdays::assets::DecodedTexture out;
    out.width = width_;
    out.height = height_;
    out.rgba = rgba_;
    return out;
}

}  // namespace khdays::game
