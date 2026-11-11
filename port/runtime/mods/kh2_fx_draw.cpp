// KH2's effects drawn in Days's 3D lists (kh2_fx_draw.h).
#include "kh2_fx_draw.h"

#include <cstdio>
#include <exception>

#include "khdays/assets/dds.h"

extern "C" {
#include "../../gfx/g3d.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "textures.h"
}

namespace khdays::kh2_fx {

int Pages::add(const std::vector<const kh2::EffectBlock *> &blocks, const std::string &hd_stem, int first)
{
    std::map<std::uint16_t, std::uint16_t> palette_of; // tbp -> the cbp of its first picture
    for (const kh2::EffectBlock *block : blocks)
        for (const auto &p : block->pictures) {
            pictures_.push_back(&p);
            palette_of.emplace(p.tbp, p.cbp);
        }
    if (!hd_stem.empty()) {
        int n = first;
        for (const auto &[tbp, cbp] : palette_of)
            hd_.push_back({tbp, cbp, hd_stem + "/-" + std::to_string(n++) + ".dds"});
    }
    return static_cast<int>(palette_of.size());
}

int Pages::page(std::uint64_t tex0, bool hd)
{
    const auto key = std::make_pair(tex0, hd);
    if (const auto it = pages_.find(key); it != pages_.end())
        return it->second;
    const int tbp = static_cast<int>(tex0 & 0x3fffu), cbp = static_cast<int>((tex0 >> 37) & 0x3fffu);
    const int width = 1 << ((tex0 >> 26) & 15u), height = 1 << ((tex0 >> 30) & 15u);
    char name[160];
    std::snprintf(name, sizeof(name), "%s@%llx%s", name_.c_str(), static_cast<unsigned long long>(tex0),
                  hd ? "/hd" : "");
    int id = -1;
    for (const HdPage &h : hd_) {
        if (!hd || h.tbp != tbp || h.cbp != cbp)
            continue;
        char path[512];
        if (!khdays_mods_find(h.relative.c_str(), path, static_cast<int>(sizeof(path))))
            break;
        try {
            auto t = khdays::assets::read_dds(path);
            // the same view, larger: its fractions name the same places
            if (static_cast<long long>(t.width) * height == static_cast<long long>(t.height) * width) {
                texels_.push_back(std::make_unique<std::vector<std::uint8_t>>(std::move(t.rgba)));
                id = khdays_textures_add(name, t.width, t.height, texels_.back()->data());
            } else
                khdays_log("kh2 effects: %s is %d x %d, not the shape of its page's %d x %d view: the original drawn",
                           path, t.width, t.height, width, height);
        } catch (const std::exception &e) {
            khdays_log("kh2 effects: %s cannot be read (%s): the original drawn", path, e.what());
        }
        break;
    }
    const bool hd_made = id >= 0;
    if (id < 0) {
        kh2::EffectPage p = kh2::effect_page(pictures_, tex0);
        texels_.push_back(std::make_unique<std::vector<std::uint8_t>>(std::move(p.rgba)));
        id = khdays_textures_add(name, p.width, p.height, texels_.back()->data());
    }
    if (tracing)
        khdays_log("kh2 effects: %s page %04x (palette %04x, %d x %d) %s", name_.c_str(), tbp, cbp, width, height,
                   hd_made ? "from its HD picture"
                   : hd    ? "original (no HD picture of this view)"
                           : "original");
    pages_.emplace(key, id);
    return id;
}

void submit(const std::vector<kh2::EffectDraw> &draws, float units, Pages &pages)
{
    const bool hd = khdays_textures_hd() != 0;
    for (const kh2::EffectDraw &d : draws) {
        if (d.screen) // the screen behind as its picture: not drawn by the port yet
            continue;
        const int id = d.tex0 != 0 ? pages.page(d.tex0, hd) : -1;
        if (d.tex0 != 0 && id < 0)
            continue; // its picture not made: not drawn as bare colour
        const uint32_t flags = (d.depth_write ? KHDAYS_G3D_EFFECT_DEPTH_WRITE : 0u) |
                               (d.depth_only ? KHDAYS_G3D_EFFECT_DEPTH_ONLY : 0u) |
                               (d.depth_test ? 0u : KHDAYS_G3D_EFFECT_NO_DEPTH_TEST);
        for (std::size_t t = 0; t + 2 < d.triangles.size(); t += 3) {
            KhdaysG3dClipVertex tri[3];
            for (std::size_t k = 0; k < 3; ++k) {
                const kh2::EffectVertex &e = d.triangles[t + k];
                KhdaysG3dClipVertex &o = tri[k];
                o.x = e.x * units; // the effect's camera's clip space back to Days's
                o.y = e.y * units;
                o.z = e.z * units;
                o.w = e.w * units;
                o.r = e.r * 63.0F;
                o.g = e.g * 63.0F;
                o.b = e.b * 63.0F;
                o.a = e.a;
                o.s = e.u * 16384.0F; // the page as a mod model's 1024 x 1024 texture
                o.t = e.v * 16384.0F;
            }
            // its identity from tick to tick: the pictures between two game frames place it between
            const std::size_t n = t / 3;
            khdays_g3d_set_effect_key(d.key, std::uint32_t(n < d.triangle_keys.size() ? d.triangle_keys[n] : n));
            khdays_g3d_effect_polygon(tri, 3, id, d.alpha, flags);
        }
    }
}

} // namespace khdays::kh2_fx
