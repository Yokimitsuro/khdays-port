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
// implements it, so neutral game code never sees the graphics API. Minimal for
// now — clear the frame; image/text draw hooks are added as scenes need them.
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void clear(Color color) = 0;
};

}  // namespace khdays::game
