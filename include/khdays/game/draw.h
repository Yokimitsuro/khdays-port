#pragma once

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

}  // namespace khdays::game
