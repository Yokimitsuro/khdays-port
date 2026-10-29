// Native PNG artwork and scalar layout overrides for original game glyph identities.
#include "font_runtime.h"
#include "fonts.h"
#include "font_raster.h"
#include <cstdint>
#include <cmath>
#include <cstring>

extern "C" int khdays_hd_font_layout_override(int mode)
{
    return khdays::hd_fonts::layout_override(mode);
}

extern "C" int khdays_hd_font_observe(int enabled)
{
    return khdays::hd_fonts::observe(enabled != 0);
}

extern "C" int khdays_hd_font_mappings_enabled(void)
{
    return khdays::hd_fonts::mappings_enabled();
}

extern "C" int khdays_hd_font_numeric_layout(int layout)
{
    return khdays::hd_fonts::numeric_layout(layout);
}

extern "C" double khdays_hd_font_fit_layout(int layout, double *offset)
{
    return khdays::hd_fonts::fit_layout(layout, offset);
}

extern "C" int khdays_hd_font_select(const char *resource, const char *scope, char *selected, int size)
{
    if (!resource || !scope || !selected || size <= 0)
        return 0;
    const auto result = khdays::hd_fonts::select(resource, scope);
    if (result.empty() || result.size() >= static_cast<std::size_t>(size))
        return 0;
    std::memcpy(selected, result.c_str(), result.size() + 1);
    return 1;
}

namespace {
double exact_units(const khdays::hd_fonts::Atlas &atlas, int value)
{
    return value * (atlas.sized ? atlas.scale : 1.0);
}

bool map_ready(const khdays::hd_fonts::Atlas &atlas)
{
    return atlas.codes_bound || (atlas.charmap.empty() && atlas.original_codepoints.empty() && !atlas.has_original &&
                                 !atlas.has_secondary && !atlas.secondary);
}

int atlas_index(std::shared_ptr<const khdays::hd_fonts::Atlas> &atlas, int original)
{
    if (original < 0)
        return original;
    if (!map_ready(*atlas))
        return -1;
    const int index =
        atlas->original_glyphs.empty()
            ? original
            : (static_cast<std::size_t>(original) < atlas->original_glyphs.size() ? atlas->original_glyphs[original]
                                                                                  : -1);
    if (index >= 0 && static_cast<std::size_t>(index) < atlas->glyphs.size()) {
        if (atlas->glyphs[index].original)
            return -1;
        if (!atlas->glyphs[index].secondary)
            return index;
    }
    if (!atlas->secondary || static_cast<std::size_t>(original) >= atlas->secondary_glyphs.size())
        return -1;
    const int secondary = atlas->secondary_glyphs[original];
    if (secondary < 0)
        return -1;
    atlas = atlas->secondary;
    return secondary;
}
} // namespace

extern "C" int khdays_hd_font_map_ready(const char *resource)
{
    auto atlas = resource ? khdays::hd_fonts::load(resource) : nullptr;
    return !atlas || map_ready(*atlas);
}

extern "C" void khdays_hd_font_bind_charmap(const char *resource, const uint32_t *codes, int count)
{
    if (!resource || !codes || count <= 0)
        return;
    auto atlas = khdays::hd_fonts::load_configured(resource);
    if (!atlas || atlas->codes_bound)
        return;
    std::unordered_map<std::uint32_t, int> by_code, secondary_by_code;
    for (const auto &mapping : atlas->charmap)
        by_code.emplace(mapping.codepoint, static_cast<int>(mapping.glyph));
    if (atlas->secondary)
        for (const auto &mapping : atlas->secondary->charmap)
            secondary_by_code.emplace(mapping.codepoint, static_cast<int>(mapping.glyph));
    std::vector<int> indices(static_cast<std::size_t>(count), -1);
    std::vector<int> secondary_indices(atlas->secondary ? static_cast<std::size_t>(count) : 0, -1);
    for (int i = 0; i < count; ++i) {
        if (atlas->original_codepoints.count(codes[i])) {
            atlas->original_codes.insert(codes[i]);
            continue;
        }
        const auto found = by_code.find(codes[i]);
        if (atlas->charmap.empty())
            indices[i] = static_cast<std::size_t>(i) < atlas->glyphs.size() ? i : -1;
        else if (found != by_code.end())
            indices[i] = found->second;
        const auto *glyph = indices[i] >= 0 ? &atlas->glyphs[indices[i]] : nullptr;
        if (glyph && !glyph->original && !glyph->secondary)
            continue;
        const auto secondary = secondary_by_code.find(codes[i]);
        if ((!glyph || !glyph->original) && secondary != secondary_by_code.end() &&
            !atlas->secondary->original_codepoints.count(codes[i]) &&
            !atlas->secondary->glyphs[secondary->second].original) {
            secondary_indices[i] = secondary->second;
            atlas->secondary_codes.insert(codes[i]);
        } else {
            atlas->original_codes.insert(codes[i]);
        }
    }
    atlas->original_glyphs = std::move(indices);
    atlas->secondary_glyphs = std::move(secondary_indices);
    atlas->codes_bound = true;
}

extern "C" int khdays_hd_font_has(const char *resource)
{
    return resource && khdays::hd_fonts::load(resource) != nullptr;
}

extern "C" int khdays_hd_font_glyph_available(const char *resource, int index)
{
    if (!resource || index < 0)
        return 0;
    auto atlas = khdays::hd_fonts::load(resource);
    if (atlas)
        index = atlas_index(atlas, index);
    return atlas && index >= 0 && static_cast<std::size_t>(index) < atlas->glyphs.size() ? 1 : 0;
}

extern "C" double khdays_hd_font_metric_f(const char *resource, int index, int field, double fallback)
{
    if (!resource)
        return fallback;
    auto atlas = khdays::hd_fonts::load(resource);
    if (!atlas)
        return fallback;
    const bool global = index < 0;
    index = atlas_index(atlas, index);
    if (!global && index < 0)
        return fallback;
    if (atlas->native_metrics)
        return field == KHDF_SIZED ? 0 : fallback;
    if (field == KHDF_SIZED)
        return atlas->sized;
    if ((field == KHDF_CELL_WIDTH || field == KHDF_CELL_HEIGHT) &&
        (index < 0 || static_cast<std::size_t>(index) < atlas->glyphs.size()))
        return atlas->sized ? exact_units(*atlas, field == KHDF_CELL_WIDTH ? atlas->cell_width : atlas->cell_height)
                            : fallback;
    if (field == KHDF_LINE_FEED)
        return exact_units(*atlas, atlas->line_feed);
    if (index < 0 || static_cast<std::size_t>(index) >= atlas->glyphs.size())
        return fallback;
    const auto &glyph = atlas->glyphs[index];
    switch (field) {
    case KHDF_LEFT: return exact_units(*atlas, glyph.left);
    case KHDF_WIDTH: return exact_units(*atlas, glyph.width);
    case KHDF_ADVANCE: return exact_units(*atlas, glyph.advance);
    default: return fallback;
    }
}

extern "C" int khdays_hd_font_metric(const char *resource, int index, int field, int fallback)
{
    return static_cast<int>(std::lround(khdays_hd_font_metric_f(resource, index, field, fallback)));
}

extern "C" double khdays_hd_font_kerning_f(const char *resource, unsigned int left, unsigned int right)
{
    if (!resource || !left || !right || left == '\n' || right == '\n')
        return 0;
    auto atlas = khdays::hd_fonts::load(resource);
    if (!atlas || !map_ready(*atlas))
        return 0;
    if (atlas->original_codes.count(left) || atlas->original_codes.count(right))
        return 0;
    const bool left_secondary = atlas->secondary_codes.count(left) != 0;
    const bool right_secondary = atlas->secondary_codes.count(right) != 0;
    if (left_secondary != right_secondary)
        return 0;
    if (left_secondary)
        atlas = atlas->secondary;
    if (atlas->native_metrics)
        return 0;
    const auto it = atlas->kerning.find((static_cast<std::uint64_t>(left) << 32) | right);
    return it == atlas->kerning.end() ? 0 : exact_units(*atlas, it->second);
}

extern "C" int khdays_hd_font_kerning(const char *resource, unsigned int left, unsigned int right)
{
    return static_cast<int>(std::lround(khdays_hd_font_kerning_f(resource, left, right)));
}

extern "C" double khdays_hd_font_extent_f(const char *resource, int index, int left, int width, int advance)
{
    if (!resource || index < 0)
        return advance;
    auto atlas = khdays::hd_fonts::load(resource);
    if (!atlas)
        return advance;
    const bool sized = atlas->sized;
    index = atlas_index(atlas, index);
    if (index < 0 || atlas->native_metrics || static_cast<std::size_t>(index) >= atlas->glyphs.size())
        return sized && width ? left + width : advance;
    const auto &glyph = atlas->glyphs[index];
    if (!atlas->sized)
        return glyph.advance; // Compatibility mode keeps NFTR advance-based layout.
    const double ratio = std::min(exact_units(*atlas, atlas->cell_width) / glyph.image_w,
                                  exact_units(*atlas, atlas->cell_height) / glyph.image_h);
    const double clip = std::min(exact_units(*atlas, glyph.width), glyph.image_w * ratio);
    return clip ? exact_units(*atlas, glyph.left) + clip : exact_units(*atlas, glyph.advance);
}

extern "C" int khdays_hd_font_extent(const char *resource, int index, int left, int width, int advance)
{
    return static_cast<int>(std::lround(khdays_hd_font_extent_f(resource, index, left, width, advance)));
}

extern "C" double khdays_hd_font_baseline_f(const char *resource, int index, int days_baseline)
{
    if (!resource || index < 0)
        return 0;
    auto atlas = khdays::hd_fonts::load(resource);
    if (atlas)
        index = atlas_index(atlas, index);
    if (!atlas || !atlas->sized || index < 0 || static_cast<std::size_t>(index) >= atlas->glyphs.size())
        return 0;
    return days_baseline - exact_units(*atlas, atlas->baseline) + atlas->baseline_offset;
}

extern "C" int khdays_hd_font_baseline(const char *resource, int index, int days_baseline)
{
    return static_cast<int>(std::lround(khdays_hd_font_baseline_f(resource, index, days_baseline)));
}

extern "C" int khdays_hd_font_geometry(const char *resource, int index, int days_w, int days_h, int days_ink_w,
                                       int *geometry)
{
    if (!resource || !geometry || index < 0)
        return 0;
    auto atlas = khdays::hd_fonts::load(resource);
    if (atlas)
        index = atlas_index(atlas, index);
    if (!atlas || index < 0 || static_cast<std::size_t>(index) >= atlas->glyphs.size())
        return 0;
    const auto g = atlas->native_metrics ? khdays::hd_fonts::GlyphGeometry{days_w, days_h, days_w}
                                         : khdays::hd_fonts::glyph_geometry(*atlas, index, days_w, days_h, days_ink_w);
    geometry[0] = g.width;
    geometry[1] = g.height;
    geometry[2] = g.clip_width;
    return 1;
}

extern "C" int khdays_hd_font_ink(const char *resource, int index, int w, int h, int ink, double *bounds)
{
    auto atlas = resource ? khdays::hd_fonts::load(resource) : nullptr;
    if (atlas)
        index = atlas_index(atlas, index);
    return atlas && index >= 0 && bounds && khdays::hd_fonts::glyph_ink(*atlas, index, w, h, ink, bounds);
}

extern "C" int khdays_hd_font_paint_fitted(const char *resource, int index, double x, double y, int logical_width,
                                           int logical_height, std::uint32_t rgba_color, std::uint32_t *rgba, int width,
                                           int height, int scale, int clip_l, int clip_t, int clip_r, int clip_b,
                                           double x_scale)
{
    if (!resource || !rgba || width <= 0 || height <= 0 || scale <= 0)
        return 0;
    auto atlas = khdays::hd_fonts::load(resource);
    if (atlas)
        index = atlas_index(atlas, index);
    if (!atlas || index < 0 || static_cast<std::size_t>(index) >= atlas->glyphs.size())
        return 0;
    khdays::hd_fonts::GlyphPaint paint;
    paint.x_scale = x_scale;
    paint.glyph_index = index;
    paint.x = x;
    paint.y = y;
    paint.logical_width = logical_width;
    paint.logical_height = logical_height;
    paint.clip_left = clip_l;
    paint.clip_top = clip_t;
    paint.clip_right = clip_r;
    paint.clip_bottom = clip_b;
    paint.red = rgba_color & 255u;
    paint.green = (rgba_color >> 8) & 255u;
    paint.blue = (rgba_color >> 16) & 255u;
    paint.alpha = (rgba_color >> 24) & 255u;
    return khdays::hd_fonts::draw_glyph(*atlas, paint, reinterpret_cast<std::uint8_t *>(rgba), width, height, width * 4,
                                        scale)
               ? 1
               : 0;
}

extern "C" int khdays_hd_font_paint_clipped(const char *resource, int index, double x, double y, int logical_width,
                                            int logical_height, std::uint32_t rgba_color, std::uint32_t *rgba,
                                            int width, int height, int scale, int clip_l, int clip_t, int clip_r,
                                            int clip_b)
{
    return khdays_hd_font_paint_fitted(resource, index, x, y, logical_width, logical_height, rgba_color, rgba, width,
                                       height, scale, clip_l, clip_t, clip_r, clip_b, 1);
}

extern "C" int khdays_hd_font_paint(const char *resource, int index, double x, double y, int logical_width,
                                    int logical_height, std::uint32_t rgba_color, std::uint32_t *rgba, int width,
                                    int height, int scale)
{
    return khdays_hd_font_paint_clipped(resource, index, x, y, logical_width, logical_height, rgba_color, rgba, width,
                                        height, scale, -16384, -16384, 16384, 16384);
}
