#include "kh2_sequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace khdays::render::kh2 {
namespace {

using Color = std::array<float, 4>;

Color unpack(std::uint32_t c)
{
    return {float(c & 255), float((c >> 8) & 255), float((c >> 16) & 255), float(c >> 24)};
}

Color texel(const assets::DecodedTexture &atlas, int x, int y)
{
    x = std::clamp(x, 0, atlas.width - 1);
    y = std::clamp(y, 0, atlas.height - 1);
    const auto p = (std::size_t(y) * std::size_t(atlas.width) + std::size_t(x)) * 4;
    return {float(atlas.rgba[p]), float(atlas.rgba[p + 1]), float(atlas.rgba[p + 2]), float(atlas.rgba[p + 3])};
}

Color sample(const assets::DecodedTexture &atlas, float u, float v, bool linear)
{
    u = std::clamp(u, 0.0f, float(atlas.width));
    v = std::clamp(v, 0.0f, float(atlas.height));
    if (!linear)
        return texel(atlas, int(std::floor(u)), int(std::floor(v)));
    u -= 0.5f;
    v -= 0.5f;
    const int x = int(std::floor(u)), y = int(std::floor(v));
    const float s = u - float(x), t = v - float(y);
    const auto a = texel(atlas, x, y), b = texel(atlas, x + 1, y);
    const auto c = texel(atlas, x, y + 1), d = texel(atlas, x + 1, y + 1);
    Color out{};
    for (std::size_t i = 0; i < 4; ++i)
        out[i] = std::lerp(std::lerp(a[i], b[i], s), std::lerp(c[i], d[i], s), t);
    return out;
}

std::uint32_t pack(const Color &c)
{
    std::uint32_t out = 0;
    for (unsigned i = 0; i < 4; ++i)
        out |= std::uint32_t(std::clamp(c[i], 0.0f, 255.0f) + 0.5f) << (8 * i);
    return out;
}

} // namespace

std::uint32_t source_over(std::uint32_t under, std::uint32_t top)
{
    const float a = float(top >> 24) / 255, b = float(under >> 24) / 255 * (1 - a), total = a + b;
    if (total == 0)
        return 0;
    std::uint32_t out = std::uint32_t(total * 255 + 0.5f) << 24;
    for (unsigned s = 0; s < 24; s += 8)
        out |= std::uint32_t((float((top >> s) & 255) * a + float((under >> s) & 255) * b) / total + 0.5f) << s;
    return out;
}

void encode_sequence_quads(KhdaysSequenceImage &image, std::span<const assets::kh2::SequenceQuad> quads)
{
    if (quads.size() > KSI_MAX_QUADS)
        throw std::runtime_error("KH2 renderer: too many native image quads");
    for (std::size_t i = 0; i < quads.size(); ++i) {
        const auto &q = quads[i];
        auto *w = image.words + KSI_HEADER + i * KSI_QUAD_WORDS;
        for (unsigned v = 0; v < 4; ++v) {
            const auto &p = q.vertices[v];
            const float xyuv[]{p.x, p.y, p.u, p.v};
            for (float value : xyuv)
                if (!std::isfinite(value))
                    throw std::runtime_error("KH2 renderer: non-finite vertex");
            std::memcpy(w + v * 5, xyuv, sizeof(xyuv));
            w[v * 5 + 4] = p.color;
        }
        w[KSI_FLAGS] = unsigned(q.depth_test) | unsigned(q.depth_write) << 1 | unsigned(q.color_write) << 2 |
                       unsigned(q.textured) << 3 | unsigned(q.linear_filter) << 4;
        w[KSI_DEPTH] = q.depth;
        w[KSI_BLEND] = q.blend;
    }
    image.words[KSI_COUNT] = unsigned(quads.size());
}

void draw_sequence(std::span<const assets::kh2::SequenceQuad> quads, const assets::DecodedTexture &atlas,
                   int logical_width, int logical_height, std::span<std::uint32_t> rgba, int width, int height,
                   const SequenceView &view)
{
    if (width <= 0 || height <= 0 || std::size_t(width) * std::size_t(height) != rgba.size() ||
        rgba.size() > 16 * 1024 * 1024 || logical_width <= 0 || logical_height <= 0 || atlas.width <= 0 ||
        atlas.height <= 0 || std::size_t(atlas.width) * std::size_t(atlas.height) > atlas.rgba.size() / 4 ||
        !std::isfinite(view.scale) || view.scale <= 0 || !std::isfinite(view.x) || !std::isfinite(view.y)) {
        throw std::runtime_error("KH2 renderer: invalid surface, atlas or view");
    }
    std::vector<std::uint32_t> depths(rgba.size(), 0);
    for (const auto &q : quads) {
        std::array<float, 4> xs{}, ys{};
        std::array<Color, 4> colors{};
        for (std::size_t i = 0; i < 4; ++i) {
            xs[i] = q.vertices[i].x * view.scale + view.x;
            ys[i] = q.vertices[i].y * view.scale + view.y;
            if (!std::isfinite(xs[i]) || !std::isfinite(ys[i]) || !std::isfinite(q.vertices[i].u) ||
                !std::isfinite(q.vertices[i].v))
                throw std::runtime_error("KH2 renderer: non-finite vertex");
            colors[i] = unpack(q.vertices[i].color);
        }
        const float ax = xs[1] - xs[0], ay = ys[1] - ys[0], bx = xs[2] - xs[0], by = ys[2] - ys[0];
        const float det = ax * by - ay * bx;
        if (!std::isfinite(det) || std::fabs(det) < 1e-8f)
            continue;
        const int x0 = int(std::clamp(std::floor(*std::min_element(xs.begin(), xs.end())), 0.0f, float(width)));
        const int x1 = int(std::clamp(std::ceil(*std::max_element(xs.begin(), xs.end())), 0.0f, float(width)));
        const int y0 = int(std::clamp(std::floor(*std::min_element(ys.begin(), ys.end())), 0.0f, float(height)));
        const int y1 = int(std::clamp(std::ceil(*std::max_element(ys.begin(), ys.end())), 0.0f, float(height)));
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float px = float(x) + 0.5f - xs[0], py = float(y) + 0.5f - ys[0];
                const float s = (px * by - py * bx) / det, t = (ax * py - ay * px) / det;
                if (s < 0 || s >= 1 || t < 0 || t >= 1)
                    continue;
                const auto p = std::size_t(y) * std::size_t(width) + std::size_t(x);
                if (q.depth_test && q.depth < depths[p])
                    continue;
                if (q.depth_write)
                    depths[p] = q.depth;
                if (!q.color_write)
                    continue;
                const auto &v = q.vertices;
                Color src{255, 255, 255, 255};
                if (q.textured) {
                    const float u = (v[0].u + (v[1].u - v[0].u) * s + (v[2].u - v[0].u) * t) * float(atlas.width) /
                                    float(logical_width);
                    const float vv = (v[0].v + (v[1].v - v[0].v) * s + (v[2].v - v[0].v) * t) * float(atlas.height) /
                                     float(logical_height);
                    src = sample(atlas, u, vv, q.linear_filter);
                }
                for (std::size_t c = 0; c < 4; ++c) {
                    const float tint = s + t <= 1 ? colors[0][c] + (colors[1][c] - colors[0][c]) * s +
                                                        (colors[2][c] - colors[0][c]) * t
                                                  : colors[3][c] + (colors[2][c] - colors[3][c]) * (1 - s) +
                                                        (colors[1][c] - colors[3][c]) * (1 - t);
                    src[c] = std::clamp(src[c] * tint / 128, 0.0f, 255.0f);
                }
                const auto dst = unpack(rgba[p]);
                const float a = src[3] / 255, da = dst[3] / 255;
                Color out{};
                out[3] = (a + da * (1 - a)) * 255;
                for (std::size_t c = 0; c < 3; ++c) {
                    if (q.blend == 1)
                        out[c] = dst[c] + src[c] * a;
                    else if (q.blend == 2)
                        out[c] = dst[c] - src[c] * a;
                    else
                        out[c] = out[3] == 0 ? 0 : (src[c] * a + dst[c] * da * (1 - a)) * 255 / out[3];
                }
                rgba[p] = pack(out);
            }
        }
    }
}

} // namespace khdays::render::kh2
