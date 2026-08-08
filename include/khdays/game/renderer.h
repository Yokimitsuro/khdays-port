#pragma once

#include <cstdint>

namespace khdays::game {

// A straight RGBA colour.
struct Color final {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

// The abstract frame renderer scenes draw through. The platform (SDL) backend
// implements it, so neutral game code never sees the graphics API. Images are
// passed as raw RGBA8888 (the neutral form every decoder produces), keeping the
// interface free of any asset type.
class Renderer {
public:
    virtual ~Renderer() = default;

    // Fill the whole frame (opaque).
    virtual void clear(Color color) = 0;

    // Draw a full-target rectangle of `color` over the current frame, blended by
    // `color.a` (used for fades to/from black or white).
    virtual void fill_overlay(Color color) = 0;

    // Draw an RGBA8888 image (row-major, `width`*`height`*4 bytes) at (x, y),
    // alpha-blended. `dst_width`/`dst_height` of 0 use the source size; other
    // values scale. `alpha` (0..255) modulates the image's own alpha, for
    // pulsing/fading a layer without touching its pixels. `rgba` must stay valid
    // for the frame.
    virtual void draw_image(
        const std::uint8_t* rgba,
        int width,
        int height,
        int x,
        int y,
        int dst_width = 0,
        int dst_height = 0,
        int alpha = 255) = 0;

    // Draw an RGBA8888 image under a 2x3 affine transform mapping source pixel
    // (sx, sy) to screen: screen_x = m[0]*sx + m[2]*sy + m[4];
    // screen_y = m[1]*sx + m[3]*sy + m[5]. Covers translation, scale and
    // rotation (the node-transform cases). `alpha` (0..255) modulates the image's
    // own alpha. `rgba` must stay valid for the frame. The default implementation
    // falls back to an axis-aligned draw when the matrix has no rotation/shear.
    virtual void draw_image_affine(
        const std::uint8_t* rgba,
        int width,
        int height,
        const float matrix[6],
        int alpha = 255) {
        // Fallback for renderers without a true affine path: handle pure
        // scale + translation (no rotation/shear) through the axis-aligned draw.
        if (rgba == nullptr || width <= 0 || height <= 0) {
            return;
        }
        const int dst_w = static_cast<int>(matrix[0] * static_cast<float>(width));
        const int dst_h = static_cast<int>(matrix[3] * static_cast<float>(height));
        draw_image(rgba, width, height, static_cast<int>(matrix[4]),
                   static_cast<int>(matrix[5]), dst_w, dst_h, alpha);
    }

    // Current output size in pixels (for centering/layout).
    virtual int width() const = 0;
    virtual int height() const = 0;
};

}  // namespace khdays::game
