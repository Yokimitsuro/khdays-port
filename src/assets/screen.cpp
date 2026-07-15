#include "khdays/assets/screen.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace khdays::assets {

namespace {

// Look up a palette colour (sub-palette + index), guarding out-of-range.
std::array<std::uint8_t, 4> palette_color(const Palette2D& palette,
                                          const int sub, const int index) {
    const std::size_t offset =
        static_cast<std::size_t>(sub) * palette.colors_per_palette + index;
    if (offset < palette.colors.size()) {
        return palette.colors[offset];
    }
    return {0U, 0U, 0U, 255U};
}

// Alpha-composite a source RGBA pixel over the destination (straight alpha).
void blend_pixel(std::uint8_t* d, const std::uint8_t* s) {
    const std::uint8_t a = s[3];
    if (a == 0U) {
        return;
    }
    if (a == 255U) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
    } else {
        d[0] = static_cast<std::uint8_t>((s[0] * a + d[0] * (255 - a)) / 255);
        d[1] = static_cast<std::uint8_t>((s[1] * a + d[1] * (255 - a)) / 255);
        d[2] = static_cast<std::uint8_t>((s[2] * a + d[2] * (255 - a)) / 255);
    }
    d[3] = 255U;
}

// Positive modulo (scroll can go negative; the tilemap wraps).
int wrap(const int value, const int modulus) {
    if (modulus <= 0) {
        return 0;
    }
    const int r = value % modulus;
    return r < 0 ? r + modulus : r;
}

// Paint one BG layer onto the frame, honouring scroll (wrapped) and treating
// palette index 0 as transparent.
void draw_bg_layer(DecodedTexture& frame, const ScreenBgLayer& layer) {
    if (layer.map == nullptr || layer.tiles == nullptr
        || layer.palette == nullptr) {
        return;
    }
    const Tilemap& map = *layer.map;
    const TileGraphics& tiles = *layer.tiles;
    const int map_w = map.width_tiles * 8;
    const int map_h = map.height_tiles * 8;
    if (map_w <= 0 || map_h <= 0) {
        return;
    }
    for (int y = 0; y < frame.height; ++y) {
        const int src_y = wrap(y + layer.scroll_y, map_h);
        const int tile_y = src_y / 8;
        const int in_y = src_y % 8;
        for (int x = 0; x < frame.width; ++x) {
            const int src_x = wrap(x + layer.scroll_x, map_w);
            const auto& cell = map.cells[static_cast<std::size_t>(tile_y)
                                             * map.width_tiles + src_x / 8];
            if (cell.tile >= tiles.tile_count) {
                continue;
            }
            const int px = cell.flip_h ? 7 - (src_x % 8) : src_x % 8;
            const int py = cell.flip_v ? 7 - in_y : in_y;
            const auto index =
                tiles.indices[static_cast<std::size_t>(cell.tile) * 64U
                              + static_cast<std::size_t>(py) * 8U + px];
            if (index == 0) {
                continue;  // transparent
            }
            const auto color = palette_color(*layer.palette, cell.palette, index);
            std::uint8_t* d =
                &frame.rgba[(static_cast<std::size_t>(y) * frame.width + x) * 4U];
            const std::uint8_t src[4] = {color[0], color[1], color[2], color[3]};
            blend_pixel(d, src);
        }
    }
}

// Blit a pre-rendered OBJ image at (ox, oy), clipped to the frame.
void draw_obj(DecodedTexture& frame, const ScreenObj& obj) {
    if (obj.image == nullptr) {
        return;
    }
    const DecodedTexture& img = *obj.image;
    for (int j = 0; j < img.height; ++j) {
        const int dy = obj.y + j;
        if (dy < 0 || dy >= frame.height) {
            continue;
        }
        for (int i = 0; i < img.width; ++i) {
            const int dx = obj.x + i;
            if (dx < 0 || dx >= frame.width) {
                continue;
            }
            const std::uint8_t* s =
                &img.rgba[(static_cast<std::size_t>(j) * img.width + i) * 4U];
            std::uint8_t* d =
                &frame.rgba[(static_cast<std::size_t>(dy) * frame.width + dx) * 4U];
            blend_pixel(d, s);
        }
    }
}

}  // namespace

DecodedTexture compose_screen(const std::vector<ScreenBgLayer>& bg_layers,
                              const std::vector<ScreenObj>& objects,
                              const int width, const int height) {
    DecodedTexture frame;
    frame.name = "screen";
    frame.width = width;
    frame.height = height;
    // Opaque black backdrop (the DS backdrop colour is a later refinement).
    frame.rgba.assign(static_cast<std::size_t>(width) * height * 4U, 0U);
    for (std::size_t i = 3; i < frame.rgba.size(); i += 4U) {
        frame.rgba[i] = 255U;
    }

    // Back (priority 3) to front (priority 0); BG before OBJ at equal priority.
    for (int p = 3; p >= 0; --p) {
        for (const auto& layer : bg_layers) {
            if (layer.priority == p) {
                draw_bg_layer(frame, layer);
            }
        }
        for (const auto& obj : objects) {
            if (obj.priority == p) {
                draw_obj(frame, obj);
            }
        }
    }
    return frame;
}

}  // namespace khdays::assets
