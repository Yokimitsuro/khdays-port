// KH2's "sys" font (kh2_font.h), as the reaction commands' bar first drew it.
#include "kh2_font.h"

extern "C" {
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
}

#include "khdays/assets/image.h"
#include "khdays/assets/kh2_bar.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <utility>

namespace khdays::ui::kh2 {
namespace {
std::filesystem::path path_of(const char *s)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s)));
}
} // namespace

bool SysFont::get()
{
    if (!tried) {
        tried = true;
        char info[2048], picture[2048];
        try {
            if (khdays_mods_find("kh2/msg/us/fontinfo.bar", info, sizeof(info)) &&
                khdays_mods_find("kh2/msg/us/US_fontimage_bar0.png", picture, sizeof(picture))) {
                std::ifstream in(path_of(info), std::ios::binary);
                const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                                      std::istreambuf_iterator<char>());
                for (const auto &e : khdays::assets::kh2::bar_entries(bytes.data(), bytes.size()))
                    if (e.name == "sys" && e.size == std::size_t(kGlyphs))
                        widths.assign(bytes.begin() + std::ptrdiff_t(e.offset),
                                      bytes.begin() + std::ptrdiff_t(e.offset + e.size));
                atlas = khdays::assets::read_image(path_of(picture));
                texel = float(atlas.width) / 512.0f;
                if (widths.empty() || atlas.width <= 0 || atlas.width * 256 != atlas.height * 512)
                    throw std::runtime_error("not KH2's sys font");
                khdays_log("ui: names in KH2's font from %s", picture);
            }
        } catch (const std::exception &e) {
            khdays_log("ui: KH2's font left out (%s): names in Days's", e.what());
            widths.clear();
        }
        if (widths.empty())
            atlas = {};
    }
    return !widths.empty();
}

int SysFont::slot(char16_t c)
{
    static const std::map<char16_t, int> slots = [] {
        static const struct {
            int byte;
            const char16_t *text;
        } groups[] = {
            {0x20,
             u"\u2b1b\uff10\uff11\uff12\uff13\uff14\uff15\uff16\uff17\uff18\uff19+\u2212\u2093ABCDEFGHIJKLMNOPQ"
             u"RSTUVWXYZ!?%/\u203b\u3001\u3002.,;:\u2026-\u2013\u301c'"},
            {0x5a, u"()\u300c\u300d\u300e\u300f\u201c\u201d[]<>-\u2013\u2937\u2669\u21fe\u21fd\u25ef\u2715"},
            {0x7f, u"\u03b1\u03b2\u03b3\u2b51\u2b52"},
            {0x86, u"&#\u00ae\u25b4\u25be\u25b8\u25c2\u00b0\u266a"},
            {0x90,
             u"0123456789abcdefghijklmnopqrstuvwxyz\u0152\u0153\u00df\u00e0\u00e1\u00e2\u00e4\u00e8\u00e9\u00ea"
             u"\u00eb\u00ec\u00ed\u00ee\u00ef\u00f1\u00f2\u00f3\u00f4\u00f6\u00f9\u00fa\u00fb\u00fc\u00ba\u2014"
             u"\u00bb\u00ab"},
            {0xd0,
             u"\u00c0\u00c1\u00c2\u00c4\u00c8\u00c9\u00ca\u00cb\u00cc\u00cd\u00ce\u00cf\u00d1\u00d2\u00d3\u00d4"
             u"\u00d6\u00d9\u00da\u00db\u00dc\u00a1\u00bf\u00c7\u00e7\u201b\u2019`\u00b4\"'\u2605\u2606\u25a0"
             u"\u25a1\u25b2\u25b3\u25cf\u25cb\u266a\u266b\u2192\u2190\u2191\u2193\u30fb\u2764"},
        };
        std::map<char16_t, int> m;
        for (const auto &g : groups)
            for (int i = 0; g.text[i] != 0; ++i)
                m.emplace(g.text[i], g.byte + i - 0x20);
        for (const auto &[alias, of] : {std::pair{u'~', u'\u301c'}, std::pair{u'\u2010', u'-'}})
            m.emplace(alias, m.at(of));
        return m;
    }();
    if (c == u' ' || c == 0xa0 || c == 0x3000)
        return kGlyphs - 1;
    const auto it = slots.find(c);
    return it != slots.end() && it->second < kGlyphs ? it->second : -1;
}

float SysFont::advance(int s) const
{
    return s == kGlyphs - 1 ? float(kSpace) : float(widths[std::size_t(s)]);
}

float SysFont::width(const std::u16string &text) const
{
    float w = 0;
    for (const char16_t c : text)
        if (const int s = slot(c); s >= 0)
            w += advance(s);
    return w;
}

void SysFont::cover(const std::u16string &text, std::vector<float> &out, int w, int h, float x, float y, float k) const
{
    const auto alpha = [&](float u, float v) {
        const int u0 = int(std::floor(u - 0.5f)), v0 = int(std::floor(v - 0.5f));
        const float fu = u - 0.5f - float(u0), fv = v - 0.5f - float(v0);
        float a = 0;
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const int ux = std::clamp(u0 + i, 0, atlas.width - 1), vy = std::clamp(v0 + j, 0, atlas.height - 1);
                const float wt = (i != 0 ? fu : 1 - fu) * (j != 0 ? fv : 1 - fv);
                a += wt * float(atlas.rgba[(std::size_t(vy) * std::size_t(atlas.width) + std::size_t(ux)) * 4 + 3]);
            }
        return a / 255.0f;
    };
    float pen = x;
    for (const char16_t c : text) {
        const int s = slot(c);
        if (s < 0)
            continue;
        if (s != kGlyphs - 1) {
            const float cu = float(s % kColumns * kCellW) * texel, cv = float(s / kColumns * kCellH) * texel;
            const int px0 = std::max(0, int(pen)), px1 = std::min(w, int(std::ceil(pen + kCellW * k)));
            const int py0 = std::max(0, int(y)), py1 = std::min(h, int(std::ceil(y + kCellH * k)));
            for (int py = py0; py < py1; ++py)
                for (int px = px0; px < px1; ++px) {
                    const float u = cu + (float(px) + 0.5f - pen) / k * texel;
                    const float v = cv + (float(py) + 0.5f - y) / k * texel;
                    if (u < cu || v < cv || u >= cu + kCellW * texel || v >= cv + kCellH * texel)
                        continue;
                    float &o = out[std::size_t(py) * std::size_t(w) + std::size_t(px)];
                    o = std::max(o, alpha(u, v));
                }
        }
        pen += advance(s) * k;
    }
}

SysFont &sys_font()
{
    static SysFont font;
    return font;
}

void paint_text(const std::u16string &text, std::vector<std::uint32_t> &out, int w, int h, float x, float y, float k)
{
    std::vector<float> cover(std::size_t(w) * std::size_t(h), 0.0f);
    sys_font().cover(text, cover, w, h, x, y, k);
    const int edge = std::max(1, int(std::lround(k * 1.5f)));
    for (int py = 0; py < h; ++py)
        for (int px = 0; px < w; ++px) {
            float near = 0;
            for (int dy = -edge; dy <= edge; ++dy)
                for (int dx = -edge; dx <= edge; ++dx) {
                    const int sx = px + dx, sy = py + dy;
                    if (dx * dx + dy * dy <= edge * edge && sx >= 0 && sy >= 0 && sx < w && sy < h)
                        near = std::max(near, cover[std::size_t(sy) * std::size_t(w) + std::size_t(sx)]);
                }
            const float a = cover[std::size_t(py) * std::size_t(w) + std::size_t(px)];
            const float total = a + near * 0.85f * (1 - a);
            if (total <= 0)
                continue;
            const unsigned grey = unsigned(std::lround(255.0f * a / total)); // white over black, by their shares
            const unsigned alpha = unsigned(std::lround(255.0f * std::min(1.0f, total)));
            std::uint32_t &o = out[std::size_t(py) * std::size_t(w) + std::size_t(px)];
            o = grey | grey << 8 | grey << 16 | alpha << 24;
        }
}

} // namespace khdays::ui::kh2
