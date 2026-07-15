#pragma once

#include <cstdint>
#include <vector>

#include "khdays/assets/tex0.h"  // DecodedTexture
#include "khdays/game/renderer.h"

namespace khdays::game {

// An offscreen RGBA renderer (a Renderer implementation with no platform deps),
// for headless scene snapshots and tests. It draws with nearest-neighbour
// scaling and straight alpha blending, and hands the frame back as a
// DecodedTexture (e.g. to dump a BMP).
class SoftwareRenderer final : public Renderer {
public:
    SoftwareRenderer(int width, int height);

    void clear(Color color) override;
    void draw_image(const std::uint8_t* rgba, int width, int height, int x,
                    int y, int dst_width, int dst_height) override;
    int width() const override { return width_; }
    int height() const override { return height_; }

    // The current frame as a neutral texture (copy).
    khdays::assets::DecodedTexture snapshot() const;

private:
    int width_;
    int height_;
    std::vector<std::uint8_t> rgba_;
};

}  // namespace khdays::game
