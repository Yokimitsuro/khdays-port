#include "khdays/assets/screen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

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

namespace {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

// Twice the signed area of triangle (a, b, c).
float edge(const Vec2 a, const Vec2 b, const Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

}  // namespace

DecodedTexture compose_flat_model(
    const NeutralModel& model,
    const std::map<std::string, DecodedTexture>& textures, const int width,
    const int height, const float fill, const float top_margin) {
    DecodedTexture frame;
    frame.name = "flat-model";
    frame.width = width;
    frame.height = height;
    frame.rgba.assign(static_cast<std::size_t>(width) * height * 4U, 0U);

    // Rest-pose XY bounding box over every vertex.
    float min_x = 1e9F;
    float max_x = -1e9F;
    float min_y = 1e9F;
    float max_y = -1e9F;
    bool any = false;
    for (const auto& mesh : model.meshes) {
        for (const auto& v : mesh.vertices) {
            const auto p = posed_position(model, v);
            min_x = std::min(min_x, p[0]);
            max_x = std::max(max_x, p[0]);
            min_y = std::min(min_y, p[1]);
            max_y = std::max(max_y, p[1]);
            any = true;
        }
    }
    if (!any || max_x <= min_x || max_y <= min_y) {
        return frame;
    }

    const float model_w = max_x - min_x;
    const float scale = static_cast<float>(width) * fill / model_w;
    const float off_x = (static_cast<float>(width) - model_w * scale) * 0.5F;
    const float off_y = static_cast<float>(height) * top_margin;
    const auto to_screen = [&](const std::array<float, 3>& p) {
        return Vec2{off_x + (p[0] - min_x) * scale,
                    off_y + (max_y - p[1]) * scale};  // flip Y (up is +Y)
    };

    // Draw meshes back-to-front by mean depth (Z ascending = furthest first).
    std::vector<std::pair<float, const NeutralMesh*>> order;
    order.reserve(model.meshes.size());
    for (const auto& mesh : model.meshes) {
        float z_sum = 0.0F;
        for (const auto& v : mesh.vertices) {
            z_sum += posed_position(model, v)[2];
        }
        const float z_avg =
            mesh.vertices.empty() ? 0.0F : z_sum / mesh.vertices.size();
        order.emplace_back(z_avg, &mesh);
    }
    std::sort(order.begin(), order.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [z_avg, mesh_ptr] : order) {
        const NeutralMesh& mesh = *mesh_ptr;
        const auto tex_it = textures.find(mesh.texture_name);
        const DecodedTexture* tex =
            tex_it != textures.end() ? &tex_it->second : nullptr;
        for (std::size_t i = 0; i + 3U <= mesh.indices.size(); i += 3U) {
            const NeutralVertex& a = mesh.vertices[mesh.indices[i]];
            const NeutralVertex& b = mesh.vertices[mesh.indices[i + 1U]];
            const NeutralVertex& c = mesh.vertices[mesh.indices[i + 2U]];
            const Vec2 sa = to_screen(posed_position(model, a));
            const Vec2 sb = to_screen(posed_position(model, b));
            const Vec2 sc = to_screen(posed_position(model, c));
            const float area = edge(sa, sb, sc);
            if (std::fabs(area) < 1e-4F) {
                continue;
            }
            const int lo_x = std::max(0, static_cast<int>(std::floor(
                                             std::min({sa.x, sb.x, sc.x}))));
            const int hi_x = std::min(width - 1, static_cast<int>(std::ceil(
                                                     std::max({sa.x, sb.x, sc.x}))));
            const int lo_y = std::max(0, static_cast<int>(std::floor(
                                             std::min({sa.y, sb.y, sc.y}))));
            const int hi_y = std::min(height - 1, static_cast<int>(std::ceil(
                                                      std::max({sa.y, sb.y, sc.y}))));
            for (int py = lo_y; py <= hi_y; ++py) {
                for (int px = lo_x; px <= hi_x; ++px) {
                    const Vec2 pc{static_cast<float>(px) + 0.5F,
                                  static_cast<float>(py) + 0.5F};
                    const float w0 = edge(sb, sc, pc) / area;
                    const float w1 = edge(sc, sa, pc) / area;
                    const float w2 = edge(sa, sb, pc) / area;
                    if (w0 < 0.0F || w1 < 0.0F || w2 < 0.0F) {
                        continue;
                    }
                    // Interpolate texel coords and vertex colour.
                    const float u = w0 * a.texcoord[0] + w1 * b.texcoord[0]
                        + w2 * c.texcoord[0];
                    const float v = w0 * a.texcoord[1] + w1 * b.texcoord[1]
                        + w2 * c.texcoord[1];
                    std::uint8_t tr = 255;
                    std::uint8_t tg = 255;
                    std::uint8_t tb = 255;
                    std::uint8_t ta = 255;
                    if (tex != nullptr && tex->width > 0 && tex->height > 0) {
                        const int tx = std::clamp(
                            static_cast<int>(u + 0.5F), 0, tex->width - 1);
                        const int ty = std::clamp(
                            static_cast<int>(v + 0.5F), 0, tex->height - 1);
                        const std::size_t o =
                            (static_cast<std::size_t>(ty) * tex->width + tx) * 4U;
                        tr = tex->rgba[o];
                        tg = tex->rgba[o + 1U];
                        tb = tex->rgba[o + 2U];
                        ta = tex->rgba[o + 3U];
                    }
                    const float cr =
                        (w0 * a.color[0] + w1 * b.color[0] + w2 * c.color[0]) / 255.0F;
                    const float cg =
                        (w0 * a.color[1] + w1 * b.color[1] + w2 * c.color[1]) / 255.0F;
                    const float cb =
                        (w0 * a.color[2] + w1 * b.color[2] + w2 * c.color[2]) / 255.0F;
                    const std::uint8_t src[4] = {
                        static_cast<std::uint8_t>(tr * cr),
                        static_cast<std::uint8_t>(tg * cg),
                        static_cast<std::uint8_t>(tb * cb), ta};
                    blend_pixel(
                        &frame.rgba[(static_cast<std::size_t>(py) * width + px) * 4U],
                        src);
                }
            }
        }
    }
    return frame;
}

}  // namespace khdays::assets
