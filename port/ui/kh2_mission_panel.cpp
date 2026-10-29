// Mission panel: native progress/tallies, authored sequences, and Days NFTR/HD font glyphs.
#include "../host/app/hud_settings.h"
#include "kh2_mission_panel.h"
#include "kh2_hp_config.h"
#include "kh2_information.h"
#include "../host/render/kh2_sequence_asset.h"
#include "../gfx/kh2_sequence.h"
#include "khdays/assets/font.h"
#include "khdays/vfs/filesystem.h"
extern "C" {
#include "elements.h"
#include "parts.h"
#include "bg.h"
#include "situation.h"
#include "mission_list.h"
#include "../runtime/menus/camp.h"
#include "../runtime/core/window.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../runtime/mods/textures.h"
#include "../runtime/mods/font_runtime.h"
#include "../runtime/scripts/game_link.h"
#include "../host/screens/ui_text.h"
int GetLanguage(void);
int GameState_GetField(int field, int bits);
int khdays_settings_game_language(void);
}
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;
namespace ui = khdays::ui::kh2;
ui::MissionPanelConfig config;
std::optional<render::SequenceAsset> asset;
std::vector<unsigned> atlas_pixels;
KhdaysSequenceImage program{};
KhdaysSituationInputs context{};
bool tried = false, allowed = false, active = false, closing = false, owns = false, native = false;
unsigned now = 0, previous = 0, epoch = 0;
float age = 0;
int atlas = -1, image = -1, text_image = -1, backdrop = -1, image_scale = 0;
int width = 0, height = 0;
float left = 0, top = 0;
seq::SequencePlayer player;
std::string last_text;
KhdaysRomReader *reader;
std::array<std::u16string, 25> messages;
int message_language = -1;
struct Bounds {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
std::map<int, Bounds> bounds;
std::filesystem::path path(const std::string &s)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
bool valid(unsigned p, unsigned n)
{
    return p && n && p <= 0x7fffffffu - n && khdays_game_ram(p) && khdays_game_ram(p + n - 1);
}
template <class T> T at(unsigned p)
{
    if (!valid(p, sizeof(T)))
        throw std::runtime_error("invalid mission context");
    T v;
    std::memcpy(&v, KHDAYS_W(p), sizeof(v));
    return v;
}
std::u16string string_at(unsigned p)
{
    std::u16string s;
    if (!p)
        return s;
    for (unsigned i = 0; i < 1024; ++i) {
        const auto c = at<unsigned short>(p + 2 * i);
        if (!c)
            return s;
        s.push_back(char16_t(c));
    }
    throw std::runtime_error("unterminated mission status text");
}
void disable(const char *why)
{
    asset.reset();
    owns = active = closing = false;
    program = {};
    khdays_log("kh2 mission panel: %s; retaining Days panel", why);
    khdays_ui_changed();
}
Bounds frame_bounds(const seq::SequenceFrame &f)
{
    Bounds b;
    bool first = true;
    for (const auto &q : f.quads)
        for (const auto &v : q.vertices) {
            if (first) {
                b = {v.x, v.y, v.x, v.y};
                first = false;
            } else {
                b.x0 = std::min(b.x0, v.x);
                b.x1 = std::max(b.x1, v.x);
                b.y0 = std::min(b.y0, v.y);
                b.y1 = std::max(b.y1, v.y);
            }
        }
    return b;
}
void include_counter_bounds(Bounds &b);
void load()
{
    if (tried)
        return;
    tried = true;
    try {
        char name[128], found[2048];
        const char *lang = khdays_language_code(khdays_settings_game_language());
        std::snprintf(name, sizeof(name), "layouts/gauge/%s/gauge.json", lang && *lang ? lang : "en");
        if (!khdays_mods_find(name, found, sizeof(found)) &&
            !khdays_mods_find("layouts/gauge/en/gauge.json", found, sizeof(found)))
            return;
        config = ui::load_mission_panel_config(path(found));
        if (!config.enabled)
            return;
        auto folder = path(found).parent_path();
        asset = render::load_sequence_asset(folder / path(config.archive), config.entry, folder / path(config.texture));
        const auto &d = asset->sequence;
        for (int clip :
             {config.reference, config.intro, config.outro, config.track, config.fill, config.ready_fill,
              config.full_fill, config.heading, config.complete_heading, config.full_marker, config.minimum_marker,
              config.bead_empty, config.bead_full, config.unity, config.rank, config.chest}) {
            int count = seq::sequence_frame_count(d, clip);
            if (count < 1 || count > 4096)
                throw std::runtime_error("animation duration outside 1..4096");
            Bounds b;
            bool first = true;
            for (int t = 0; t < count; ++t) {
                auto f = seq::evaluate_sequence(d, clip, float(t));
                if (f.quads.size() > KSI_MAX_QUADS)
                    throw std::runtime_error("animation exceeds GPU capacity");
                auto bb = frame_bounds(f);
                if (first) {
                    b = bb;
                    first = false;
                } else {
                    b.x0 = std::min(b.x0, bb.x0);
                    b.x1 = std::max(b.x1, bb.x1);
                    b.y0 = std::min(b.y0, bb.y0);
                    b.y1 = std::max(b.y1, bb.y1);
                }
            }
            bounds[clip] = b;
        }
        if (!seq::evaluate_sequence(d, config.intro, 0).active || !seq::evaluate_sequence(d, config.outro, 0).active)
            throw std::runtime_error("background animations need an active parent track");
        // The reference clip contributes authored extent, but its debugging sprites are never drawn.
        auto b = bounds.at(config.reference);
        for (int clip : {config.intro, config.outro}) {
            auto c = bounds.at(clip);
            b.x0 = std::min(b.x0, c.x0);
            b.y0 = std::min(b.y0, c.y0);
            b.x1 = std::max(b.x1, c.x1);
            b.y1 = std::max(b.y1, c.y1);
        }
        auto include = [&](int clip, float x, float y) {
            const auto c = bounds.at(clip);
            b.x0 = std::min(b.x0, x + c.x0);
            b.x1 = std::max(b.x1, x + c.x1);
            b.y0 = std::min(b.y0, y + c.y0);
            b.y1 = std::max(b.y1, y + c.y1);
        };
        include(config.track, config.bar_start_x, config.bar_y);
        for (int clip : {config.fill, config.ready_fill, config.full_fill})
            include(clip, config.bar_start_x, config.bar_y);
        for (int clip : {config.heading, config.complete_heading})
            include(clip, config.heading_x, config.heading_y);
        include(config.full_marker, config.full_marker_x, config.full_marker_y);
        include(config.minimum_marker, config.bar_start_x, config.minimum_marker_y);
        include(config.minimum_marker, config.bar_end_x, config.minimum_marker_y);
        for (int clip : {config.bead_empty, config.bead_full}) {
            include(clip, config.beads_x, config.beads_y);
            include(clip, config.beads_x + 31 * config.bead_spacing, config.beads_y);
        }
        b.x0 = std::min({b.x0, config.description_x, config.tally_x - 200 * config.text_scale});
        b.x1 = std::max({b.x1, config.counters_right, config.tally_x, config.bar_end_x});
        b.y0 = std::min(b.y0, config.description_y);
        b.y1 = std::max({b.y1, config.tally_y + 24 * config.text_scale, config.description_y + 24 * config.text_scale});
        include_counter_bounds(b);
        if (!std::isfinite(b.x0) || !std::isfinite(b.y0) || !std::isfinite(b.x1) || !std::isfinite(b.y1) ||
            b.x1 - b.x0 > 8192 || b.y1 - b.y0 > 4096)
            throw std::runtime_error("invalid panel bounds");
        left = std::floor((b.x0 - config.source_origin_x) * config.scale) - 2;
        top = std::floor((b.y0 - config.source_origin_y) * config.scale) - 2;
        width = int(std::ceil((b.x1 - config.source_origin_x) * config.scale) - left) + 2;
        height = int(std::ceil((b.y1 - config.source_origin_y) * config.scale) - top) + 2;
        if (width < 1 || width > 1024 || height < 1 || height > 512)
            throw std::runtime_error("panel canvas exceeds 1024x512");
        atlas_pixels.resize(asset->texture.rgba.size() / 4);
        std::memcpy(atlas_pixels.data(), asset->texture.rgba.data(), asset->texture.rgba.size());
        atlas = khdays_textures_add_info("kh2:mission:atlas", asset->texture.width, asset->texture.height,
                                         atlas_pixels.data(), KHDAYS_REPLACEMENT_NO_MIPS);
        if (atlas < 0)
            throw std::runtime_error("could not register atlas");
        khdays_log("kh2 mission panel: %s, %s", found, config.archive.c_str());
    } catch (const std::exception &e) {
        disable(e.what());
    }
}
bool pause_only()
{
    const int setting = khdays_hud_setting(KHDAYS_OPT_MISSION_VISIBILITY);
    return setting < 0 ? config.pause_only : setting != 0;
}
bool enabled()
{
    load();
    return khdays_hud_setting(KHDAYS_OPT_MISSION) && asset && khdays_textures_hd() &&
           khdays_ui_mode(KHDAYS_UI_HUD) == KHDAYS_UI_ON && khdays_ui_mode(KHDAYS_UI_TEXT) != KHDAYS_UI_OFF;
}
void load_messages()
{
    int language = GetLanguage();
    if (message_language == language)
        return;
    message_language = language;
    messages = {};
    char name[96];
    std::snprintf(name, sizeof(name), "UI/btl/%s/info.s.z", khdays_language_code(language));
    if (!reader)
        reader = khdays_rom_reader_new();
    unsigned char *d = nullptr;
    unsigned size = 0;
    if (!reader || !khdays_rom_file_data(reader, name, -1, &d, &size))
        throw std::runtime_error("Days status messages are unavailable");
    // The ROM reader supplies decompressed resources: size-prefixed UTF-16 records after the two-word header.
    auto u32 = [&](unsigned p) {
        unsigned v = 0;
        if (p + 4 > size)
            throw std::runtime_error("truncated status messages");
        std::memcpy(&v, d + p, 4);
        return v;
    };
    try {
        unsigned p = 8, n = size >= 8 ? u32(4) : 0;
        for (unsigned i = 0; i < std::min(n, 25u); ++i) {
            unsigned len = u32(p);
            if (len < 6 || len > size - p || (len & 1))
                throw std::runtime_error("invalid status message record");
            for (unsigned k = p + 4; k + 1 < p + len; k += 2) {
                unsigned c = d[k] | unsigned(d[k + 1]) << 8;
                if (!c)
                    break;
                messages[i].push_back(char16_t(c));
            }
            p += len;
        }
    } catch (...) {
        std::free(d);
        throw;
    }
    std::free(d);
}
std::u16string format(std::u16string s, std::initializer_list<int> values)
{
    size_t p = 0;
    for (int v : values) {
        p = s.find(u"%d", p);
        if (p == s.npos)
            break;
        auto n = std::to_string(v);
        std::u16string u(n.begin(), n.end());
        s.replace(p, 2, u);
        p += u.size();
    }
    return s;
}
struct Font {
    std::optional<khdays::assets::Font> data;
    std::string route;
    bool tried = false;
    void load(const char *name)
    {
        if (tried)
            return;
        tried = true;
        if (auto p = khdays::vfs::resolve(name))
            data = khdays::assets::decode_nftr(*p);
        if (!data)
            throw std::runtime_error("Days status font is unavailable");
        char selected[512];
        const int observing = khdays_hd_font_observe(1);
        const int scoped = khdays_hd_font_select(name, "field.hud.status", selected, sizeof(selected));
        khdays_hd_font_observe(observing);
        route = scoped ? selected : name;
        std::vector<unsigned> codes(data->glyphs.size(), ~0u);
        for (auto [code, index] : data->char_to_glyph)
            if (index >= 0 && size_t(index) < codes.size())
                codes[index] = code;
        khdays_hd_font_bind_charmap(route.c_str(), codes.data(), int(codes.size()));
    }
    double metric(int index, int field, double fallback) const
    {
        return khdays_hd_font_metric_f(route.c_str(), index, field, fallback);
    }

    struct Measure {
        double left = 0, right = 0;
    };
    Measure measure(const std::u16string &s) const
    {
        double pen = 0;
        Measure m;
        unsigned prev = 0;
        for (auto c : s) {
            int i = data->glyph_of(c);
            if (i < 0) {
                pen += data->cell_width / 2;
                continue;
            }
            const auto &g = data->glyphs[i];
            pen += khdays_hd_font_kerning_f(route.c_str(), prev, c);
            m.left = std::min(m.left, pen + metric(i, KHDF_LEFT, g.left));
            m.right = std::max(m.right, pen + metric(i, KHDF_LEFT, g.left) + metric(i, KHDF_WIDTH, g.width) + 1);
            pen += metric(i, KHDF_ADVANCE, g.advance);
            prev = c;
        }
        m.right = std::max(m.right, pen) + 1;
        if (!std::isfinite(m.right - m.left) || m.right - m.left > 8192)
            throw std::runtime_error("status text exceeds 8192 pixels");
        return m;
    }
    double width(const std::u16string &s) const
    {
        auto m = measure(s);
        return s.empty() ? 0 : m.right - m.left;
    }
    double height(const std::u16string &s) const
    {
        double result = data->cell_height;
        for (auto c : s) {
            int index = data->glyph_of(c);
            if (index >= 0)
                result = std::max(result, metric(index, KHDF_CELL_HEIGHT, data->cell_height));
        }
        if (!std::isfinite(result) || result < 1 || result > 126)
            throw std::runtime_error("invalid status font height");
        return result;
    }
    void paint(const std::u16string &s, std::vector<unsigned> &out, int w, int h, int resolution, double x, double y,
               double sizing, bool counter, double fit = 1) const
    {
        if (s.empty() || sizing <= 0 || fit <= 0)
            return;
        auto m = measure(s);
        int sw = int(std::ceil(m.right - m.left)) + 1, sh = int(std::ceil(height(s))) + 2;
        if (sw < 1 || sh < 1 || sh > 128)
            throw std::runtime_error("invalid status font dimensions");
        std::vector<unsigned> pixels(size_t(sw * resolution) * sh * resolution);
        // Paint all shadows first, keeping a later glyph's shadow off the preceding foreground.
        for (int pass = 0; pass < 2; ++pass) {
            double pen = -m.left;
            unsigned prev = 0;
            for (auto c : s) {
                int i = data->glyph_of(c);
                if (i < 0) {
                    pen += data->cell_width / 2;
                    continue;
                }
                const auto &g = data->glyphs[i];
                pen += khdays_hd_font_kerning_f(route.c_str(), prev, c);
                unsigned tint = pass ? (counter && c != u'/' ? 0xff00ffffu : 0xffffffffu) : 0xff000000u;
                const int shadow = pass ? 0 : 1;
                if (!khdays_hd_font_paint(route.c_str(), i, pen + shadow, shadow, data->cell_width, data->cell_height,
                                          tint, pixels.data(), sw * resolution, sh * resolution, resolution)) {
                    for (int yy = 0; yy < data->cell_height * resolution; ++yy)
                        for (int xx = 0; xx < data->cell_width * resolution; ++xx) {
                            unsigned alpha = g.alpha[size_t(yy / resolution) * data->cell_width + xx / resolution];
                            if (!alpha)
                                continue;
                            int px = int(std::round((pen + g.left + shadow) * resolution)) + xx,
                                py = yy + shadow * resolution;
                            if (px >= 0 && px < sw * resolution && py < sh * resolution)
                                pixels[size_t(py) * sw * resolution + px] = (tint & 0xffffffu) | (alpha << 24);
                        }
                }
                pen += metric(i, KHDF_ADVANCE, g.advance);
                prev = c;
            }
        }
        // Text is cached. Uniform size follows the panel; only overlong descriptions are fitted horizontally.
        int x0 = std::max(0, int(std::floor(x * resolution))), y0 = std::max(0, int(std::floor(y * resolution)));
        int x1 = std::min(w, int(std::ceil((x + sw * sizing * fit) * resolution)));
        int y1 = std::min(h, int(std::ceil((y + sh * sizing) * resolution)));
        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px) {
                int sx = int((px + .5 - x * resolution) / (sizing * fit)),
                    sy = int((py + .5 - y * resolution) / sizing);
                if (sx < 0 || sy < 0 || sx >= sw * resolution || sy >= sh * resolution)
                    continue;
                unsigned src = pixels[size_t(sy) * sw * resolution + sx];
                if (!(src >> 24))
                    continue;
                out[size_t(py) * w + px] = src;
            }
    }
};
Font description_font, counter_font;
void include_counter_bounds(Bounds &b)
{
    counter_font.load("text/font_eu_08.nftr");
    const float scale = config.counters_scale;
    const float text_y = config.counters_y + (config.counter_text_offset_y - 4 * config.counter_scale / .65f) * scale;
    float right = config.counters_right;
    // Reserve the largest native chest/rank/unity counts, including their scaled animated bounds.
    for (auto [clip, text] : {std::pair{config.chest, u"127/127"}, {config.unity, u"1/1"}, {config.rank, u"2/2"}}) {
        const auto icon = bounds.at(clip);
        const float start = right - ((icon.x1 - icon.x0) + config.icon_gap +
                                     float(counter_font.width(text)) * config.counter_scale / .65f) *
                                        scale;
        b.x0 = std::min(b.x0, start);
        b.x1 = std::max(b.x1, right);
        b.y0 = std::min({b.y0, config.counters_y + icon.y0 * scale, text_y});
        b.y1 = std::max({b.y1, config.counters_y + icon.y1 * scale,
                         text_y + float(counter_font.height(text) + 2) * config.counter_scale / .65f * scale});
        right = start - config.counter_gap * scale;
    }
}
struct Status {
    std::u16string description, tally;
    int total = 0, done = 0, goal = 0, displayed = 0, beads = 0, beads_done = 0;
    int chest_total = 0, chest_found = 0, rank_total = 0, rank_found = 0, unity_total = 0, unity_found = 0;
    bool day = false, gauge = false, complete = false, highlight = false;
};
Status last_status;
Status snapshot()
{
    Status s;
    unsigned p = at<unsigned>(0x0207f630u);
    if (!valid(p, 0x1e0))
        throw std::runtime_error("mission context is unavailable");
    unsigned panel = at<unsigned>(0x0207f614u);
    s.day = valid(panel, 0x5c) && at<int>(panel + 0x58) != 0;
    s.total = at<int>(p + 0x188);
    s.done = at<int>(p + 0x18c);
    s.goal = at<int>(p + 0x190);
    s.displayed = std::clamp(at<int>(p + 0x194), 0, 224);
    s.gauge = !s.day && at<int>(p + 0xa8) != 0 && s.total > 0;
    s.complete = s.gauge && s.done >= s.goal && s.displayed >= int((int64_t(s.goal) * 224) / s.total);
    load_messages();
    if (s.day) {
        s.description = format(messages[0], {GameState_GetField(0, 9)});
        s.beads_done = std::clamp(at<int>(p + 0x1c0), 0, 32);
        s.beads = std::clamp(at<int>(p + 0x1c4), 0, 32);
        if (at<int>(p + 0x1d0))
            s.tally = format(messages[1], {at<int>(p + 0x1cc) - at<int>(p + 0x1c8), at<int>(p + 0x1cc)});
        if (GameState_GetField(0, 9) == 0x165) {
            s.beads = 0;
            s.tally.clear();
        }
    } else
        s.description = string_at(at<unsigned>(p + 0xb8));
    unsigned short room = at<unsigned short>(0x0204c242u), mission = at<unsigned short>(0x0204c23cu);
    unsigned flags = at<unsigned char>(0x0204c240u);
    if (!s.day) {
        if ((flags & 6u) == 2u && at<int>(p + 0x178) != 0) {
            static constexpr unsigned entries[] = {15, 16, 17, 18, 19, 21, 22, 23, 24};
            unsigned kind = at<unsigned short>(0x0204c262u);
            if (kind < std::size(entries)) {
                s.description = messages[14];
                auto pos = s.description.find(u"%s");
                if (pos != s.description.npos)
                    s.description.replace(pos, 2, messages[entries[kind]]);
                s.highlight = true;
            }
        }
        int count = 0;
        const int *chests = khdays_mission_chests(room, &count);
        if (count > 0 && count <= 127 && chests) {
            s.chest_total = count;
            for (int i = 0; i < count; ++i)
                s.chest_found += GameState_GetField(chests[i], 1) != 0;
        }
        if (!(flags & 2u)) {
            unsigned kinds = khdays_mission_badges(GetLanguage(), room, mission);
            unsigned found = unsigned(GameState_GetField(0x92b + 4 * mission, 4));
            unsigned field = at<unsigned>(0x0207fa00u);
            if (valid(field, 0x8d5b))
                for (unsigned i = 0; i < 4; ++i)
                    if (at<unsigned char>(field + 0x8d4e + 4 * i))
                        found |= 1u << i;
            s.rank_total = int(kinds & 1u) + int((kinds >> 1) & 1u);
            s.rank_found = int((kinds & found) & 1u) + int(((kinds & found) >> 1) & 1u);
            s.unity_total = (kinds & 8u) != 0;
            s.unity_found = (kinds & found & 8u) != 0;
        }
    }
    return s;
}
bool panel_cell(const KhdaysBgLayer &l, int x, int y)
{
    int px = ((x * 8 - l.hofs) % (l.wt * 8) + l.wt * 8) % (l.wt * 8);
    int py = ((y * 8 - l.vofs) % (l.ht * 8) + l.ht * 8) % (l.ht * 8);
    return px < 256 && py < 192 && py + 8 > 152;
}
bool visible()
{
    KhdaysBgLayer l;
    if (!khdays_bg_text_layer(1, 2, &l))
        return false;
    for (int y = 18; y <= 20; ++y)
        for (int x = 0; x < 3; ++x) {
            int cx = (l.hofs >> 3) + x, cy = (l.vofs >> 3) + y;
            unsigned e = khdays_bg_map_entry(1, &l, cx, cy) & 1023u;
            if (e == 0x90 && (khdays_bg_map_entry(1, &l, cx + 1, cy) & 1023u) == 0x91 &&
                (khdays_bg_map_entry(1, &l, cx + 2, cy) & 1023u) == 0x92)
                return true;
        }
    return false;
}
std::u16string counter(int a, int b)
{
    auto s = std::to_string(a) + "/" + std::to_string(b);
    return std::u16string(s.begin(), s.end());
}
float loop_frame(int clip)
{
    auto &d = asset->sequence;
    float end = float(seq::sequence_frame_count(d, clip));
    const auto &c = d.clips[clip];
    if (age < end)
        return age;
    if (c.restart >= unsigned(end))
        return end - 1;
    return float(c.restart) + std::fmod(age - end, end - float(c.restart));
}
void draw(const Status &s)
{
    const auto &d = asset->sequence;
    int resolution = khdays_ui_scale();
    if (image < 0 || resolution != image_scale) {
        khdays_ui_image_put(image);
        khdays_ui_image_put(text_image);
        image = khdays_ui_image_get(width * resolution, height * resolution);
        text_image = khdays_ui_image_get(width * resolution, height * resolution);
        image_scale = resolution;
        last_text.clear();
    }
    if (image < 0 || text_image < 0)
        throw std::runtime_error("could not allocate panel images");
    auto frame = seq::evaluate_sequence(d, player.clip(), player.frame());
    if (!frame.active)
        throw std::runtime_error("background lost its active parent");
    const auto resting = seq::evaluate_sequence(d, config.intro, float(seq::sequence_frame_count(d, config.intro) - 1));
    auto rest = resting.active;
    if (!rest)
        throw std::runtime_error("background has no resting anchor");
    const float dx = float(frame.active->x - rest->x), dy = float(frame.active->y - rest->y);
    const float alpha = std::clamp(float(frame.active->color >> 24) / 128, 0.f, 1.f);
    auto add = [&](int clip, float x, float y, float fraction = -1.f, float sprite_scale = 1.f) {
        seq::SequencePose pose;
        pose.x = x + dx;
        pose.y = y + dy;
        pose.color_rate[3] = alpha;
        if (fraction >= 0 || clip == config.track) {
            auto b = bounds.at(clip);
            if (b.x1 <= b.x0)
                throw std::runtime_error("progress bar has no width");
            pose.scale_x = (config.bar_end_x - config.bar_start_x) / (b.x1 - b.x0);
            pose.x -= (b.x0 - .5f) * pose.scale_x;
        }
        auto f = seq::evaluate_sequence(d, clip, loop_frame(clip), pose);
        if (fraction >= 0) {
            float edge = x + dx + .5f + (config.bar_end_x - config.bar_start_x) * fraction;
            for (auto &q : f.quads) {
                float lo = q.vertices[0].x, hi = q.vertices[1].x;
                float t = hi != lo ? std::clamp((edge - lo) / (hi - lo), 0.f, 1.f) : 0;
                for (int row = 0; row < 2; ++row) {
                    auto &a = q.vertices[row * 2], &b = q.vertices[row * 2 + 1];
                    b.x = std::lerp(a.x, b.x, t);
                    b.y = std::lerp(a.y, b.y, t);
                    b.u = std::lerp(a.u, b.u, t);
                    b.v = std::lerp(a.v, b.v, t);
                }
            }
        }
        if (sprite_scale != 1) {
            for (auto &q : f.quads)
                for (auto &v : q.vertices) {
                    v.x = pose.x + (v.x - pose.x) * sprite_scale;
                    v.y = pose.y + (v.y - pose.y) * sprite_scale;
                }
        }
        frame.quads.insert(frame.quads.end(), f.quads.begin(), f.quads.end());
    };
    description_font.load("text/font_eu_10.nftr");
    counter_font.load("text/font_eu_08.nftr");
    struct Block {
        int clip, found, total;
    };
    std::vector<std::pair<float, std::u16string>> numbers;
    float right = config.counters_right, description_right = config.counters_right;
    const float description_bottom =
        config.description_y + float(description_font.height(s.description) + 1) * config.text_scale / .65f;
    const float row_scale = config.counters_scale;
    const float counter_text_y =
        config.counters_y + (config.counter_text_offset_y - 4 * config.counter_scale / .65f) * row_scale;
    for (auto block :
         {Block{config.chest, s.chest_found, s.chest_total}, Block{config.unity, s.unity_found, s.unity_total},
          Block{config.rank, s.rank_found, s.rank_total}}) {
        if (!block.total)
            continue;
        auto text = counter(block.found, block.total);
        float tw = float(counter_font.width(text)) * config.counter_scale / .65f * row_scale;
        auto bb = bounds.at(block.clip);
        float iw = (bb.x1 - bb.x0) * row_scale;
        float start = right - iw - config.icon_gap * row_scale - tw;
        add(block.clip, start - bb.x0 * row_scale, config.counters_y, -1, row_scale);
        auto overlaps_description = [&](float band_top, float band_bottom) {
            return band_top < description_bottom && band_bottom > config.description_y;
        };
        if (overlaps_description(config.counters_y + bb.y0 * row_scale, config.counters_y + bb.y1 * row_scale) ||
            overlaps_description(counter_text_y, counter_text_y + float(counter_font.height(text) + 1) *
                                                                      config.counter_scale / .65f * row_scale))
            description_right = std::min(description_right, start - config.counter_gap * row_scale);
        numbers.emplace_back(start + iw + config.icon_gap * row_scale, std::move(text));
        right = start - config.counter_gap * row_scale;
    }
    if (s.gauge) {
        add(config.track, config.bar_start_x, config.bar_y);
        add(s.displayed == 224 ? config.full_fill
            : s.complete       ? config.ready_fill
                               : config.fill,
            config.bar_start_x, config.bar_y, float(s.displayed) / 224);
        add(s.complete ? config.complete_heading : config.heading, config.heading_x, config.heading_y);
        add(config.full_marker, config.full_marker_x, config.full_marker_y);
        if (s.goal != s.total) {
            int mark = std::clamp(int(int64_t(s.goal) * 224 / s.total), 0, 223);
            add(config.minimum_marker, config.bar_start_x + float(mark) * (config.bar_end_x - config.bar_start_x) / 224,
                config.minimum_marker_y);
        }
    } else if (s.day) {
        for (int i = 0; i < s.beads; ++i)
            add(i < s.beads_done ? config.bead_full : config.bead_empty, config.beads_x + i * config.bead_spacing,
                config.beads_y);
    }
    if (frame.quads.size() > KSI_MAX_QUADS)
        throw std::runtime_error("composed panel exceeds GPU quad capacity");
    std::string signature;
    auto key = [&](const auto &v) { signature.append(reinterpret_cast<const char *>(&v), sizeof(v)); };
    for (auto c : s.description)
        key(c);
    key(unsigned(0));
    for (auto c : s.tally)
        key(c);
    key(s.highlight);
    key(description_right);
    key(row_scale);
    key(resolution);
    for (auto &[x, t] : numbers) {
        key(x);
        for (auto c : t)
            key(c);
        key(unsigned(0));
    }
    if (signature != last_text) {
        std::vector<unsigned> pixels(size_t(width * resolution) * height * resolution, 0);
        const float factor = config.text_scale / .65f;
        auto paint = [&](const Font &font, const std::u16string &t, float x, float y, float sizing, bool digits,
                         float fit = 1) {
            float px = (x - config.source_origin_x) * config.scale - left,
                  py = (y - config.source_origin_y) * config.scale - top;
            font.paint(t, pixels, width * resolution, height * resolution, resolution, px, py, sizing, digits, fit);
        };
        float available = std::max(0.f, description_right - config.description_x),
              dw = float(description_font.width(s.description)) * factor;
        if (available > 0)
            paint(description_font, s.description, config.description_x, config.description_y,
                  config.text_scale * config.scale / .65f, s.highlight, std::min(1.f, available / std::max(1.f, dw)));
        paint(description_font, s.tally, config.tally_x - float(description_font.width(s.tally)) * factor,
              config.tally_y, config.text_scale * config.scale / .65f, false);
        for (auto &[x, t] : numbers)
            paint(counter_font, t, x, counter_text_y, config.counter_scale * config.scale / .65f * row_scale, true);
        auto *out = khdays_ui_image_begin(text_image);
        if (!out)
            throw std::runtime_error("could not update panel text");
        std::copy(pixels.begin(), pixels.end(), out);
        khdays_ui_image_end(text_image);
        last_text = std::move(signature);
    }
    auto *w = program.words;
    program = {};
    auto number = [&](unsigned i, float v) { std::memcpy(w + i, &v, 4); };
    w[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(image));
    w[KSI_ATLAS] = unsigned(atlas);
    w[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
    w[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
    w[KSI_WIDTH] = unsigned(width * resolution);
    w[KSI_HEIGHT] = unsigned(height * resolution);
    number(KSI_VIEW_X, (-left - config.source_origin_x * config.scale) * resolution);
    number(KSI_VIEW_Y, (-top - config.source_origin_y * config.scale) * resolution);
    number(KSI_SCALE, config.scale * resolution);
    w[KSI_CLEAR] = w[KSI_ANCHORED] = 1;
    number(KSI_ANCHOR_Y, 152);
    render::encode_sequence_quads(program, frame.quads);
    w[KSI_FACE] = unsigned(khdays_ui_image_replacement(text_image));
    w[KSI_HD_FACE] = ~0u;
    number(KSI_FACE_X, dx * config.scale * resolution);
    number(KSI_FACE_Y, dy * config.scale * resolution);
    number(KSI_FACE_W, float(width * resolution));
    number(KSI_FACE_H, float(height * resolution));
    number(KSI_TEXT_SCALE, 1);
    number(KSI_TEXT_ALPHA, alpha);
    number(KSI_TEXT_CLIP_W, float(width * resolution));
    number(KSI_TEXT_CLIP_H, float(height * resolution));
    KhdaysUiElement e{"hud.mission_panel",
                      1,
                      KES_DIRECT | KES_FINE | KES_PICTURE | 2,
                      left + config.offset_x + (context.pause_mode ? config.pause_offset_x : 0),
                      152 + top + config.offset_y + (context.pause_mode ? config.pause_offset_y : 0),
                      float(width),
                      float(height),
                      image};
    const int preference =
        khdays_hud_setting(context.pause_mode ? KHDAYS_OPT_MISSION_PAUSE_ANCHOR : KHDAYS_OPT_MISSION_ANCHOR);
    const unsigned anchor = preference           ? unsigned(preference)
                            : context.pause_mode ? config.pause_anchor
                                                 : config.anchor;
    if (anchor) {
        const auto b = frame_bounds(resting);
        if (b.x1 <= b.x0 || b.y1 <= b.y0)
            throw std::runtime_error("anchored background has no resting bounds");
        const float ax = float((anchor - 1) % 3) * .5f, ay = float((anchor - 1) / 3) * .5f;
        e.x += 256 * ax - (std::lerp(b.x0, b.x1, ax) - config.source_origin_x) * config.scale;
        e.y += 192 * ay - 152 - (std::lerp(b.y0, b.y1, ay) - config.source_origin_y) * config.scale;
        w[KSI_PANEL_ANCHOR] = anchor;
    }
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
}
} // namespace
extern "C" void khdays_ui_mission_panel_frame(unsigned frame)
{
    owns = false;
    program = {};
    static unsigned settings_generation;
    if (settings_generation != khdays_hud_settings_generation()) {
        settings_generation = khdays_hud_settings_generation();
        last_text.clear();
        khdays_ui_changed();
    }
    now = frame;
    khdays_situation_take(&context, frame);
    bool event = context.event_status >= 0 && !(context.event_status & 8) &&
                 (khdays_situation_ran(&context, KHDAYS_UPD_Ov023_SceneRun) || context.pause_mode);
    allowed = context.scene == 2 && (context.overlays & (1ull << 2)) && !context.movie && !context.tutorial && !event &&
              context.mission_start != 10 && context.camp_screen == KHDAYS_CAMP_NONE &&
              !(context.overlays & ((1ull << 8) | (1ull << 25) | (1ull << 26) | (1ull << 27)));
    unsigned e = khdays_ui_information_epoch();
    if (e != epoch || frame < previous || !allowed) {
        active = closing = owns = false;
        program = {};
        last_text.clear();
    }
    epoch = e;
    if (enabled() && (active || (allowed && visible())))
        khdays_ui_changed();
}
extern "C" void khdays_ui_mission_panel_emit(void)
{
    program = {};
    owns = false;
    if (!allowed || !enabled()) {
        active = closing = false;
        previous = now;
        return;
    }
    try {
        native = visible();
        const bool hidden = pause_only() && !context.pause_mode;
        if (hidden) {
            active = closing = false;
            previous = now;
            if (!native)
                return;
        } else {
            float delta = khdays_link_paused() || now < previous ? 0 : float(now - previous) * config.frames_per_tick;
            previous = now;
            if (native && (!active || closing)) {
                active = true;
                closing = false;
                age = 0;
                player.select(asset->sequence, config.intro, true);
                // A newly shown paused panel must not freeze on a transparent entrance frame.
                if (khdays_link_paused())
                    player.seek(float(seq::sequence_frame_count(asset->sequence, config.intro) - 1));
                delta = 0;
            }
            if (!native && active && !closing) {
                closing = true;
                player.select(asset->sequence, config.outro, true);
                delta = 0;
            }
            if (!active)
                return;
            age += delta;
            if (closing) {
                float end = float(seq::sequence_frame_count(asset->sequence, config.outro));
                float t = player.frame() + delta;
                if (t >= end) {
                    active = false;
                    return;
                }
                player.seek(t);
            } else
                player.advance(asset->sequence, delta);
            if (native)
                last_status = snapshot();
            draw(last_status);
        }
        owns = true;
        for (int bg : {0, 2}) {
            KhdaysBgLayer l;
            if (!khdays_bg_text_layer(1, bg, &l))
                continue;
            for (int y = 0; y < l.ht; ++y)
                for (int x = 0; x < l.wt; ++x)
                    if (panel_cell(l, x, y))
                        khdays_ui_left_out_cell(KHDAYS_UI_HUD, 1, bg, x, y);
        }
    } catch (const std::exception &e) {
        disable(e.what());
    }
}
extern "C" int khdays_ui_mission_panel_owns(void)
{
    return owns;
}
extern "C" int khdays_ui_mission_panel_cell(int engine, int bg, int x, int y)
{
    if (!owns || engine != 1 || (bg != 0 && bg != 2))
        return 0;
    KhdaysBgLayer l;
    if (!khdays_bg_text_layer(engine, bg, &l))
        return 0;
    return panel_cell(l, x, y);
}
extern "C" void khdays_ui_mission_panel_commands(KhdaysSequenceImage *out)
{
    *out = program;
}
extern "C" unsigned khdays_ui_mission_panel_state(void)
{
    return (owns ? 1u : 0u) | (owns && native ? 2u : 0u) |
           (allowed && pause_only() && !context.pause_mode && enabled() ? 4u : 0u);
}
extern "C" int khdays_ui_mission_panel_backdrop(void)
{
    if (!owns)
        return -1;
    if (backdrop < 0) {
        backdrop = khdays_ui_image_get(1, 1);
        if (backdrop < 0)
            return -1;
        auto *p = khdays_ui_image_begin(backdrop);
        if (p) {
            *p = 0x80000000u;
            khdays_ui_image_end(backdrop);
        }
    }
    return khdays_ui_image_replacement(backdrop);
}
