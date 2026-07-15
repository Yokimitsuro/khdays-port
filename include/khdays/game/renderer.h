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
    // values scale. `rgba` must stay valid for the frame.
    virtual void draw_image(
        const std::uint8_t* rgba,
        int width,
        int height,
        int x,
        int y,
        int dst_width = 0,
        int dst_height = 0) = 0;

    // Current output size in pixels (for centering/layout).
    virtual int width() const = 0;
    virtual int height() const = 0;
};

}  // namespace khdays::game
