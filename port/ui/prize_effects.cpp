// The field's prize pictures drawn as a mod's effect: the pictures named as the game loads them, each billboard of
// one with an effect drawn instead on the 3D layer's native sequences (as ui/kh2_reticles.cpp draws KH2's markers).
#include "prize_effects.h"
#include "prize_effects_model.h"
#include "../gfx/kh2_sequence.h"
#include "../host/app/hud_settings.h"
#include "khdays/assets/image.h"
extern "C" {
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../runtime/core/window.h"
#include "../runtime/display/frame_package.h"
#include "../runtime/mods/textures.h"
#include "../runtime/scripts/game_link.h"
#include "../host/app/host.h"
}
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace prizes = khdays::ui::prizes;
namespace render = khdays::render::kh2;

struct Canvas {
    int token = -1;
    unsigned frame = ~0u; // the game frame its program was made for
    KhdaysSequenceImage commands{};
};

struct Picture {
    std::uint32_t words = 0; // its texture words' reference in the prize table
    std::string name;
    bool tried = false; // its effect looked for
    bool drawn = false; // an effect to draw
    prizes::Effect effect;
    prizes::Atlas atlas;
    prizes::Reach reach;
    std::vector<std::uint32_t> pixels; // the atlas, kept while the texture registry points at it
    int atlas_id = -1;
    std::array<Canvas, 4> canvases;
};

std::array<Picture, KHDAYS_PRIZE_PROGRAMS> pictures;
// the program numbers in the frame package: after the reticles' and the heart counter's
constexpr unsigned first_program = KSI_3D_PROGRAMS - KHDAYS_PRIZE_PROGRAMS;

bool traced()
{
    static const bool on = std::getenv("KHDAYS_TRACE_PRIZES") != nullptr;
    return on;
}

// A line to stderr and the session log (as the KH2 HUD's).
void note(const char *format, ...)
{
    va_list args, again;
    va_start(args, format);
    va_copy(again, args);
    const int n = std::vsnprintf(nullptr, 0, format, args);
    va_end(args);
    std::string line(std::size_t(n > 0 ? n : 0) + 1, '\0');
    std::vsnprintf(line.data(), line.size(), format, again);
    va_end(again);
    line.pop_back();
    std::fprintf(stderr, "%s\n", line.c_str());
    khdays_log("%s", line.c_str());
}

std::uint32_t read32(const unsigned char *p)
{
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
std::uint16_t read16(const unsigned char *p)
{
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

// A BTX0's first texture's name, through its texture dictionary (TEX0 +0x0e; the dictionary's entries at +6, a
// name's offset at the entries' +2); empty when it is not one.
std::string picture_name(KhdaysRef btx0)
{
    const auto in_ram = [](KhdaysRef r, unsigned n) { return khdays_game_ram(r) && khdays_game_ram(r + n - 1); };
    if (!in_ram(btx0, 0x14))
        return {};
    const auto *b = static_cast<const unsigned char *>(KHDAYS_W(btx0));
    if (std::memcmp(b, "BTX0", 4) != 0)
        return {};
    const KhdaysRef tex0 = btx0 + read32(b + 0x10);
    if (!in_ram(tex0, 0x10) || std::memcmp(KHDAYS_W(tex0), "TEX0", 4) != 0)
        return {};
    const KhdaysRef dict = tex0 + read16(static_cast<const unsigned char *>(KHDAYS_W(tex0)) + 0x0e);
    if (!in_ram(dict, 8))
        return {};
    const KhdaysRef entries = dict + read16(static_cast<const unsigned char *>(KHDAYS_W(dict)) + 6);
    if (!in_ram(entries, 4))
        return {};
    const KhdaysRef name = entries + read16(static_cast<const unsigned char *>(KHDAYS_W(entries)) + 2);
    if (!in_ram(name, 16))
        return {};
    const auto *n = static_cast<const char *>(KHDAYS_W(name));
    return std::string(n, strnlen(n, 16));
}

std::filesystem::path path_of(const char *s)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s)));
}

// Picture p's effect from the first mod that has one, its pages stacked into one atlas; logged once when there is
// one that cannot be used.
void load(Picture &p)
{
    p.tried = true;
    p.drawn = false;
    char relative[160], found[2048];
    std::snprintf(relative, sizeof(relative), "graphics/mo/prize.pak.z/fx/%s.txt", p.name.c_str());
    if (p.name.empty() || !khdays_mods_find(relative, found, sizeof(found)))
        return;
    try {
        std::ifstream in(path_of(found), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::string why;
        auto effect = prizes::parse_effect(text.str(), why);
        if (!effect)
            throw std::runtime_error(why);
        const auto folder = path_of(found).parent_path();
        std::vector<khdays::assets::DecodedTexture> pages;
        std::vector<std::pair<int, int>> sizes;
        for (const auto &page : effect->pages) {
            pages.push_back(khdays::assets::read_image(folder / path_of(page.c_str())));
            sizes.emplace_back(pages.back().width, pages.back().height);
        }
        p.atlas = prizes::stack_pages(sizes);
        p.pixels.assign(std::size_t(p.atlas.width) * std::size_t(p.atlas.height), 0u);
        for (std::size_t i = 0; i < pages.size(); ++i)
            for (int y = 0; y < pages[i].height; ++y)
                std::memcpy(&p.pixels[std::size_t(p.atlas.top[i] + y) * std::size_t(p.atlas.width)],
                            &pages[i].rgba[std::size_t(y) * std::size_t(pages[i].width) * 4],
                            std::size_t(pages[i].width) * 4);
        const auto id = "prize:effect:" + p.name;
        p.atlas_id = khdays_textures_add_info(id.c_str(), p.atlas.width, p.atlas.height, p.pixels.data(),
                                              KHDAYS_REPLACEMENT_NO_MIPS);
        if (p.atlas_id < 0)
            throw std::runtime_error("its pages could not be kept as a texture");
        p.effect = std::move(*effect);
        p.reach = prizes::reach_of(p.effect);
        if (p.reach.half <= 0 || p.reach.body_width <= 0)
            throw std::runtime_error("its body layer has no width");
        p.drawn = true;
        note("prizes: %s drawn as %s (%zu frames)", p.name.c_str(), found, p.effect.frames.size());
    } catch (const std::exception &e) {
        note("prizes: %s: %s; Days's picture kept", found, e.what());
    }
}

bool available()
{
    // The reference DS renderer leaves out the port's own polygons, as for mod models.
    return khdays_hud_setting(KHDAYS_OPT_PRIZES) && khdays_textures_hd() &&
           (khdays_host_frame_wants() & KHDAYS_WANT_3D) && khdays_link_scene(nullptr) == 2;
}

// Picture i's program for this build slot, made for game frame `now`; its token.
int program(Picture &p, std::size_t i, unsigned now)
{
    const int slot = khdays_g3d_build_slot();
    auto &c = p.canvases[std::size_t(slot)];
    c.token = int(first_program + i) * 4 + slot;
    if (c.frame == now)
        return c.token;
    c.frame = now;
    const auto quads = prizes::frame_quads(p.effect, prizes::frame_at(p.effect, now), p.reach, p.atlas);
    auto *w = c.commands.words;
    std::memset(w, 0, sizeof(unsigned) * KSI_HEADER);
    const float side = 2 * p.reach.half;
    w[KSI_OUTPUT] = unsigned(c.token);
    w[KSI_ATLAS] = unsigned(p.atlas_id);
    w[KSI_LOGICAL_W] = unsigned(p.atlas.width);
    w[KSI_LOGICAL_H] = unsigned(p.atlas.height);
    w[KSI_WIDTH] = unsigned(std::ceil(side));
    w[KSI_HEIGHT] = unsigned(std::ceil(side));
    const float zero = 0, one = 1;
    std::memcpy(w + KSI_VIEW_X, &zero, 4);
    std::memcpy(w + KSI_VIEW_Y, &zero, 4);
    std::memcpy(w + KSI_SCALE, &one, 4);
    w[KSI_HD_FACE] = ~0u;
    w[KSI_CLEAR] = 1;
    render::encode_sequence_quads(c.commands, quads);
    return c.token;
}

// The trace: the player's HP and munny when they change (KHDAYS_TRACE_PRIZES).
void trace_counters(const char *when)
{
    static int last_hp = -1, last_munny = -1;
    int hp = 0, max = 0;
    khdays_link_player_hp(&hp, &max);
    const int munny = khdays_link_munny();
    if (hp != last_hp || munny != last_munny)
        note("prizes: f%u %s: HP %d/%d, munny %d", khdays_link_frame(), when, hp, max, munny);
    last_hp = hp;
    last_munny = munny;
}

std::vector<std::uint32_t> table_words()
{
    std::vector<std::uint32_t> words;
    for (const auto &p : pictures)
        words.push_back(p.words);
    return words;
}
} // namespace

extern "C" void khdays_prize_picture(int index, unsigned btx0, unsigned words)
{
    if (index < 0 || index >= KHDAYS_PRIZE_PROGRAMS)
        return;
    auto &p = pictures[std::size_t(index)];
    const std::string name = picture_name(btx0);
    if (name != p.name)
        p.tried = p.drawn = false; // another pack: its effect looked for again
    p.name = name;
    p.words = words;
    if (traced())
        note("prizes: picture %d is %s (words %08x)", index, name.empty() ? "?" : name.c_str(), words);
}

extern "C" int khdays_prize_node(const void *node)
{
    if (traced())
        trace_counters("drawn");
    if (!available())
        return 0;
    const auto *n = static_cast<const unsigned char *>(node);
    const int i = prizes::picture_of(read32(n + 0x08), table_words());
    if (i < 0)
        return 0;
    auto &p = pictures[std::size_t(i)];
    if (!p.tried)
        load(p);
    if (!p.drawn)
        return 0;
    try {
        const unsigned now = khdays_link_frame();
        const int token = program(p, std::size_t(i), now);
        // the billboard as Billboard_DrawList places it: its position through the view matrix (data_020473e0),
        // its quad on the billboard matrix's turn (data_020428e0), from its foot (y 0) to its height
        const auto *view = static_cast<const unsigned char *>(KHDAYS_W(0x020473e0u));
        const auto *board = static_cast<const unsigned char *>(KHDAYS_W(0x020428e0u));
        const auto fx = [](const unsigned char *at, int k) {
            std::int32_t v;
            std::memcpy(&v, at + 4 * k, 4);
            return float(v) / 4096.0F;
        };
        const float pos[3] = {fx(n + 0x0c, 0), fx(n + 0x0c, 1), fx(n + 0x0c, 2)};
        float t[3];
        for (int j = 0; j < 3; ++j)
            t[j] = pos[0] * fx(view, j) + pos[1] * fx(view, 3 + j) + pos[2] * fx(view, 6 + j) + fx(view, 9 + j);
        const auto width = float(std::int16_t(read16(n + 0x18))) / 4096.0F;
        const auto height = float(std::int16_t(read16(n + 0x1a))) / 4096.0F;
        const unsigned alpha = read16(n + 0x20) & 31u, poly = read16(n + 0x22) & 63u;
        // the effect's body as wide as the picture, the effect's middle the picture's
        const float half = p.reach.half * width / p.reach.body_width, middle = height / 2;
        float proj[16], unused_pos[16], unused_vec[16];
        khdays_g3d_matrices(unused_pos, unused_vec, proj);
        KhdaysG3dClipVertex v[4]{};
        constexpr int corners[]{0, 1, 3, 2}; // top-left, top-right, bottom-right, bottom-left
        for (int k = 0; k < 4; ++k) {
            const int c = corners[k];
            const float x = (c & 1) ? half : -half, y = middle + ((c & 2) ? -half : half);
            float e[3];
            for (int j = 0; j < 3; ++j)
                e[j] = x * fx(board, j) + y * fx(board, 3 + j) + t[j];
            auto &o = v[k];
            o.x = proj[0] * e[0] + proj[4] * e[1] + proj[8] * e[2] + proj[12];
            o.y = proj[1] * e[0] + proj[5] * e[1] + proj[9] * e[2] + proj[13];
            o.z = proj[2] * e[0] + proj[6] * e[1] + proj[10] * e[2] + proj[14];
            o.w = proj[3] * e[0] + proj[7] * e[1] + proj[11] * e[2] + proj[15];
            o.r = o.g = o.b = 63;
            o.a = 1;
            o.s = (c & 1) ? 128.0F : 0;
            o.t = (c & 2) ? 128.0F : 0;
        }
        const unsigned attr = 0xc0u | (alpha << 16) | (poly << 24);
        khdays_g3d_sequence_polygon(v, 4, attr, p.atlas_id, unsigned(token));
        return 1;
    } catch (const std::exception &e) {
        p.drawn = false;
        note("prizes: %s: %s; Days's picture kept", p.name.c_str(), e.what());
        return 0;
    }
}

extern "C" void khdays_prize_collected(int kind, int tier, unsigned row)
{
    if (!traced() || !khdays_game_ram(row) || !khdays_game_ram(row + 0x0b))
        return;
    const auto *r = static_cast<const unsigned char *>(KHDAYS_W(row));
    const int i = prizes::picture_of(read32(r + 0x08), table_words());
    note("prizes: f%u taken: kind %d tier %d picture %s", khdays_link_frame(), kind, tier,
         i < 0 ? "?" : pictures[std::size_t(i)].name.c_str());
    trace_counters("taken");
}

extern "C" void khdays_prize_commands(const KhdaysG3dList *list, KhdaysSequenceImage *out)
{
    for (std::size_t i = 0; i < pictures.size(); ++i) {
        out[i] = {};
        for (const auto &c : pictures[i].canvases) {
            if (c.token < 0)
                continue;
            for (int k = 0; k < list->npolys; ++k)
                if ((list->polys[k].flags & KHDAYS_G3D_SEQUENCE) && list->polys[k].pltt == unsigned(c.token)) {
                    out[i] = c.commands;
                    break;
                }
        }
    }
}
