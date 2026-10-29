// The pause part (pause.h): the Retry of Mission Mode and the challenges (runtime/menus/retry.c) in a
// mission played alone, drawn in engine A's BG2 as a third line under the game's Continue and Give Up,
// in the game's 8-dot font. A mod's pictures of the pause (mods/<Mod>/layouts/pause/<lang>/, tools/hd/hd_pause.py) are
// drawn instead of the game's pieces where the game puts them; a language missing any keeps the
// game's.
#include "pause.h"
#include "hud_options.h"

extern "C" {
#include "bg.h"
#include "elements.h"
#include "parts.h"
#include "../host/screens/ui_text.h"
#include "../host/screens/game_pause.h"
#include "../host/render/png_io.h"
#include "../runtime/menus/retry.h"
#include "../runtime/scripts/game_link.h"
#include "../runtime/core/session_log.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/window.h"
}

#include "khdays/assets/font.h"
#include "khdays/assets/graphics2d.h"
#include "khdays/vfs/filesystem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace {

constexpr int kScreen = 0; // engine A
constexpr int kLayer = 2;  // its BG2: the pause's lines
constexpr int kCellX = 10, kCellsW = 11, kCellsH = 2;
constexpr int kW = kCellsW * 8, kH = kCellsH * 8;
constexpr int kGameRow[2] = {11, 13}; // the game's Continue and Give Up
constexpr int kRow[3] = {11, 13, 15}; // the port's Continue, Retry, Give Up
constexpr int kInsideLeft = 18, kInsideRight = 78, kInsideTop = 4, kInsideBottom = 12; // [left, right) x [top, bottom)

using Line = std::array<std::uint32_t, kW * kH>; // texels: 0, or KET_OPAQUE | the BG palette's index

// The game's two lines as VRAM held them last, by line and lit.
struct Seen {
    bool have[2][2] = {};
    Line line[2][2];
};
Seen seen;
bool target_layout = false;
KhdaysUiElement target_element{};
int seen_language = -1;

// A line drawn: its image and the texels it holds.
struct Shown {
    int image = -1;
    int scale = 0;
    Line texels;
};
Shown shown[3];
// this frame's lines: the game's two, the port's three (kept here: lines are 5.5 KB each)
Line live[2], drawn[3];

std::optional<khdays::assets::Font> font;
bool font_tried;

const khdays::assets::Font *the_font()
{
    if (!font_tried) {
        font_tried = true;
        const auto path = khdays::vfs::resolve("text/font_eu_08.nftr");
        try {
            if (path) {
                font = khdays::assets::decode_nftr(*path);
            }
        } catch (const std::exception &e) {
            std::fprintf(stderr, "ui: the pause's Retry font could not be read (%s)\n", e.what());
        }
        if (!font) {
            std::fprintf(stderr, "ui: the pause shows no Retry: no text/font_eu_08.nftr\n");
        }
    }
    return font ? &*font : nullptr;
}

// The line from map cell (kCellX, row) as the map and VRAM hold it; false when a cell shows nothing
// (not drawn).
bool read_line(const KhdaysBgLayer &l, int row, Line &out)
{
    const int size = l.colors256 ? 64 : 32;
    bool any = false;
    out.fill(0);
    for (int j = 0; j < kCellsH; ++j) {
        for (int i = 0; i < kCellsW; ++i) {
            const std::uint32_t entry = khdays_bg_map_entry(kScreen, &l, kCellX + i, row + j);
            const std::uint32_t at = l.chars + (entry & 0x3ffu) * static_cast<std::uint32_t>(size);
            const std::uint32_t pal = entry >> 12;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const int ex = (entry & 0x400u) ? 7 - x : x, ey = (entry & 0x800u) ? 7 - y : y;
                    std::uint32_t v;
                    if (l.colors256) {
                        const std::uint8_t *b = khdays_bg_memory(kScreen, at + static_cast<std::uint32_t>(ey * 8 + ex));
                        v = b != nullptr ? *b : 0u;
                    } else {
                        const std::uint8_t *b =
                            khdays_bg_memory(kScreen, at + static_cast<std::uint32_t>(ey * 4 + ex / 2));
                        v = b != nullptr ? (*b >> ((ex & 1) * 4)) & 15u : 0u;
                        v = v != 0 ? pal * 16u + v : 0u;
                    }
                    if (v != 0) {
                        out[static_cast<std::size_t>((j * 8 + y) * kW + i * 8 + x)] = KET_OPAQUE | v;
                        any = true;
                    }
                }
            }
        }
    }
    return any;
}

// How bright the BG palette's colour `t` names is (its components' sum).
int brightness(std::uint32_t t)
{
    const std::uint16_t c = *static_cast<const volatile std::uint16_t *>(KHDAYS_W(0x05000000u + (t & 0xffu) * 2u));
    return (c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31);
}

std::u16string utf16(const char *s)
{
    std::u16string out;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p != 0;) {
        unsigned c = *p++;
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        c &= more == 3 ? 0x07u : more == 2 ? 0x0fu : more == 1 ? 0x1fu : 0x7fu;
        for (; more > 0 && (*p & 0xc0) == 0x80; --more) {
            c = (c << 6) | (*p++ & 0x3fu);
        }
        out.push_back(static_cast<char16_t>(c));
    }
    return out;
}

// Retry: the line `from` with its words taken out and `words` written in; false without the font.
bool retry_line(const Line &from, const char *words, Line &out)
{
    const auto *f = the_font();
    std::uint32_t inside[kH] = {};
    int left = kW, right = -1;
    std::uint32_t white = 0, shadow = 0;
    int shadow_count[512] = {};
    if (f == nullptr || words == nullptr) {
        return false;
    }
    out = from;
    // each row's inside colour: the most of its dots
    for (int y = kInsideTop; y < kInsideBottom; ++y) {
        int best = 0;
        for (int x = kInsideLeft; x < kInsideRight; ++x) {
            int n = 0;
            for (int k = kInsideLeft; k < kInsideRight; ++k) {
                n += from[static_cast<std::size_t>(y * kW + k)] == from[static_cast<std::size_t>(y * kW + x)];
            }
            if (n > best) {
                best = n;
                inside[y] = from[static_cast<std::size_t>(y * kW + x)];
            }
        }
    }
    // the words: their extent, their white (the brightest), their shadow (right and down of it)
    for (int y = kInsideTop; y < kInsideBottom; ++y) {
        for (int x = kInsideLeft; x < kInsideRight; ++x) {
            const std::uint32_t t = from[static_cast<std::size_t>(y * kW + x)];
            if (t != inside[y]) {
                left = x < left ? x : left;
                right = x > right ? x : right;
                if (t != 0 && (white == 0 || brightness(t) > brightness(white))) {
                    white = t;
                }
            }
        }
    }
    if (right < 0 || white == 0) {
        return false;
    }
    for (int y = kInsideTop; y + 1 < kInsideBottom; ++y) {
        for (int x = kInsideLeft; x + 1 < kInsideRight; ++x) {
            const std::uint32_t below = from[static_cast<std::size_t>((y + 1) * kW + x + 1)];
            if (from[static_cast<std::size_t>(y * kW + x)] == white && below != white && below != inside[y + 1]) {
                ++shadow_count[below & 0x1ffu];
            }
        }
    }
    for (int k = 0; k < 512; ++k) {
        if (shadow_count[k] > shadow_count[shadow & 0x1ffu]) {
            shadow = static_cast<std::uint32_t>(k);
        }
    }
    for (int y = kInsideTop; y < kInsideBottom; ++y) {
        for (int x = kInsideLeft; x < kInsideRight; ++x) {
            out[static_cast<std::size_t>(y * kW + x)] = inside[y];
        }
    }
    {
        const auto text = khdays::assets::render_text(*f, utf16(words));
        const int x0 = (left + right) / 2 - text.width / 2, y0 = kInsideTop;
        auto ink = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < text.width && y < text.height &&
                   text.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(text.width) +
                              static_cast<std::size_t>(x)) *
                                 4u +
                             3u] >= 128;
        };
        for (int y = 0; y < text.height; ++y) {
            for (int x = 0; x < text.width; ++x) {
                const int dx = x0 + x, dy = y0 + y;
                if (dx < kInsideLeft || dx >= kInsideRight || dy < kInsideTop || dy >= kInsideBottom) {
                    continue;
                }
                if (ink(x, y)) {
                    out[static_cast<std::size_t>(dy * kW + dx)] = white;
                } else if (shadow != 0 && ink(x - 1, y - 1)) {
                    out[static_cast<std::size_t>(dy * kW + dx)] = shadow;
                }
            }
        }
    }
    return true;
}

void emit_line(int index, const Line &texels, int x, int y)
{
    const int scale = khdays_ui_scale();
    Shown &s = shown[index];
    static const char *const ids[3] = {"pause.continue", "pause.retry", "pause.give_up"};
    if (s.image < 0 || s.scale != scale || s.texels != texels) {
        if (s.image < 0 || s.scale != scale) {
            khdays_ui_image_put(s.image);
            s.image = khdays_ui_image_get(kW * scale, kH * scale);
            s.scale = scale;
        }
        if (s.image < 0) {
            return;
        }
        std::uint32_t *out = khdays_ui_image_begin(s.image);
        if (out == nullptr) {
            return;
        }
        for (int py = 0; py < kH * scale; ++py) {
            for (int px = 0; px < kW * scale; ++px) {
                out[static_cast<std::size_t>(py) * static_cast<std::size_t>(kW * scale) +
                    static_cast<std::size_t>(px)] = texels[static_cast<std::size_t>((py / scale) * kW + px / scale)];
            }
        }
        khdays_ui_image_end(s.image);
        s.texels = texels;
    }
    KhdaysUiElement e;
    e.id = ids[index];
    e.screen = kScreen;
    e.slot = static_cast<std::uint32_t>(kLayer);
    e.x = static_cast<float>(x);
    e.y = static_cast<float>(y);
    e.w = static_cast<float>(kW);
    e.h = static_cast<float>(kH);
    e.image = s.image;
    khdays_ui_emit(KHDAYS_UI_PAUSE, &e);
}

// --- A mod's pictures ----------------------------------------------------------------------------

const char *const kCodes[6] = {"en", "en", "fr", "de", "it", "es"}; // by the game's language (1-5)
// (the game over's lines have no picture of their own here: drawn by the Menu Overhaul's pause alone)
const char *const kFiles[KHDAYS_PAUSE_KEYS] = {"title", "question", "continue",    "skip",      "withdraw",   "retry",
                                               "yes",   "no",       "go_continue", "go_easier", "go_withdraw"};
// the hand's size and place by its line's strip (DS dots): where the game's lit strip has it, in the
// strip's first 16 dots, the capsule's rim from dot 9
constexpr float kHand = 20.0f;
constexpr float kHandLeft = -3.0f, kHandTop = -2.0f;

// A picture of the mod's at a size: its image (kept while the scale stays).
struct Picture {
    std::string file;
    int w = 0, h = 0;
    int image = -1;
};
std::vector<Picture> pictures;
int pictures_scale = 0;

std::string mod_file(const std::string &relative)
{
    char path[1024];
    return khdays_mods_find(relative.c_str(), path, static_cast<int>(sizeof(path))) ? std::string(path) : std::string();
}

// The picture of `relative` brought down to w x h texels (each texel the average of the dots it
// covers, their colours weighed by their alpha); -1 when the mod has none.
int picture(const std::string &relative, int w, int h)
{
    for (const auto &p : pictures) {
        if (p.file == relative && p.w == w && p.h == h) {
            return p.image;
        }
    }
    Picture p;
    p.file = relative;
    p.w = w;
    p.h = h;
    const std::string path = mod_file(relative);
    int sw = 0, sh = 0;
    const char *error = nullptr;
    std::uint8_t *src = path.empty() ? nullptr : khdays_png_read(path.c_str(), &sw, &sh, &error);
    if (src != nullptr && sw > 0 && sh > 0 && w > 0 && h > 0 && (p.image = khdays_ui_image_get(w, h)) >= 0) {
        std::uint32_t *out = khdays_ui_image_begin(p.image);
        if (out != nullptr) {
            for (int y = 0; y < h; ++y) {
                const int y0 = y * sh / h, y1 = std::max(y0 + 1, (y + 1) * sh / h);
                for (int x = 0; x < w; ++x) {
                    const int x0 = x * sw / w, x1 = std::max(x0 + 1, (x + 1) * sw / w);
                    double r = 0, g = 0, b = 0, a = 0;
                    for (int sy = y0; sy < y1; ++sy) {
                        const std::uint8_t *d = src + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(sw) +
                                                       static_cast<std::size_t>(x0)) *
                                                          4u;
                        for (int sx = x0; sx < x1; ++sx, d += 4) {
                            r += d[0] * d[3], g += d[1] * d[3], b += d[2] * d[3], a += d[3];
                        }
                    }
                    const double n = static_cast<double>((x1 - x0) * (y1 - y0));
                    const auto c = [&](double v) {
                        return static_cast<std::uint32_t>(a > 0 ? std::min(255.0, v / a + 0.5) : 0.0);
                    };
                    out[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                        c(r) | c(g) << 8 | c(b) << 16 | static_cast<std::uint32_t>(a / n + 0.5) << 24;
                }
            }
            khdays_ui_image_end(p.image);
        }
    } else if (src == nullptr && !path.empty()) {
        std::fprintf(stderr, "ui: the pause's %s could not be read (%s)\n", path.c_str(),
                     error != nullptr ? error : "?");
    }
    khdays_png_free(src);
    pictures.push_back(p);
    return p.image;
}

std::string piece_file(const KhdaysPausePiece &p, const char *code)
{
    std::string f = std::string(code) + "/" + kFiles[p.key];
    if (p.key >= KHDAYS_PAUSE_CONTINUE && p.lit != 0) {
        f += "_lit";
    }
    return "layouts/pause/" + f + ".png";
}

void emit_picture(const char *id, int layer, int image, float x, float y, float w, float h)
{
    KhdaysUiElement e;
    e.id = id;
    e.screen = kScreen;
    e.slot = KES_DIRECT | static_cast<std::uint32_t>(layer) | (target_layout ? KES_PICTURE | KES_PAUSE_LAYOUT : 0u);
    e.x = x;
    e.y = target_layout ? (std::strcmp(id, "pause.title") == 0 ? 48 - h : y + 44) : y;
    e.w = w;
    e.h = h;
    e.image = image;
    khdays_ui_emit(KHDAYS_UI_PAUSE, &e);
}

std::uint32_t palette_colour(int engine, unsigned index)
{
    const unsigned c =
        *static_cast<const volatile std::uint16_t *>(KHDAYS_W(0x05000000u + (engine ? 0x400u : 0u) + index * 2));
    const auto expand = [](unsigned v) { return (v << 3) | (v >> 2); };
    return 0xff000000u | expand(c & 31) | expand((c >> 5) & 31) << 8 | expand((c >> 10) & 31) << 16;
}

// Rebuild native tile artwork, not pixels cropped from a composed lower screen.
bool native_rectangle(int engine, int layer, int x, int y, int w, int h, std::vector<unsigned> &out)
{
    KhdaysBgLayer l;
    if (!khdays_bg_text_layer(engine, layer, &l))
        return false;
    out.assign(std::size_t(w * h), 0);
    for (int py = 0; py < h; ++py)
        for (int px = 0; px < w; ++px) {
            const unsigned entry = khdays_bg_map_entry(engine, &l, x / 8 + px / 8, y / 8 + py / 8);
            const int tx = (entry & 0x400) ? 7 - px % 8 : px % 8;
            const int ty = (entry & 0x800) ? 7 - py % 8 : py % 8;
            const unsigned at = l.chars + (entry & 1023) * (l.colors256 ? 64u : 32u) +
                                unsigned(ty * (l.colors256 ? 8 : 4) + (l.colors256 ? tx : tx / 2));
            const auto *p = khdays_bg_memory(engine, at);
            const unsigned value = p ? (l.colors256 ? *p : (*p >> ((tx & 1) * 4)) & 15u) : 0;
            if (value)
                out[std::size_t(py * w + px)] = palette_colour(engine, value + (l.colors256 ? 0u : (entry >> 12) * 16));
        }
    return true;
}

int mission_target_image(int language)
{
    int entry, primary;
    if (!(khdays_ui_hud_options() & KHDAYS_HUD_PAUSE_TARGET) || !khdays_link_mission_target(&entry, &primary))
        return -1;
    static KhdaysRomReader *reader = khdays_rom_reader_new();
    static std::map<std::pair<int, int>, khdays::assets::DecodedTexture> decoded;
    const auto key = std::pair{primary ? language : 0, entry};
    auto found = decoded.find(key);
    if (found == decoded.end()) {
        char file[128];
        std::snprintf(file, sizeof(file), primary ? "UI/btl/%s/map.p2" : "UI/btl/mapchr.p2", kCodes[language]);
        u8 *bytes = nullptr;
        u32 size = 0;
        if (!khdays_rom_file_data(reader, file, entry, &bytes, &size))
            return -1;
        const std::unique_ptr<u8, decltype(&std::free)> owned(bytes, &std::free);
        try {
            const auto pack = khdays::assets::parse_pk2d(bytes, size);
            if (pack.palettes.empty() || pack.tiles.empty() || pack.screens.empty())
                return -1;
            const auto pal = khdays::assets::decode_nclr(pack.palettes[0].data, pack.palettes[0].size);
            const auto chr = khdays::assets::decode_ncgr(pack.tiles[0].data, pack.tiles[0].size);
            const auto map = khdays::assets::decode_nscr(pack.screens[0].data, pack.screens[0].size);
            auto image = khdays::assets::compose_background(map, chr, pal, false);
            if (image.width != 64 || image.height != 64)
                return -1;
            found = decoded.emplace(key, std::move(image)).first;
        } catch (const std::exception &e) {
            khdays_log("pause target: %s; retaining the normal pause", e.what());
            return -1;
        }
    }
    std::vector<unsigned> pixels;
    if (!native_rectangle(1, 2, 32, 48, 64, 96, pixels))
        return -1;
    const auto &art = found->second;
    for (int y = 0; y < 64; ++y)
        std::memcpy(pixels.data() + (y + 24) * 64, art.rgba.data() + y * 64 * 4, 64 * 4);
    static int image = -1;
    static std::vector<unsigned> previous;
    if (image < 0)
        image = khdays_ui_image_get(64, 96);
    if (image < 0)
        return -1;
    if (pixels != previous) {
        auto *out = khdays_ui_image_begin(image);
        if (!out)
            return -1;
        std::copy(pixels.begin(), pixels.end(), out);
        khdays_ui_image_end(image);
        previous = std::move(pixels);
    }
    return image;
}

bool emit_target_pause(const KhdaysPauseView &pv)
{
    if (std::any_of(pv.piece, pv.piece + pv.count, [](const auto &p) {
            return p.key == KHDAYS_PAUSE_SKIP || p.key == KHDAYS_PAUSE_QUESTION || p.key == KHDAYS_PAUSE_YES ||
                   p.key == KHDAYS_PAUSE_NO;
        }))
        return false;
    const int image = mission_target_image(pv.language >= 1 && pv.language <= 5 ? pv.language : 1);
    if (image < 0)
        return false;
    target_layout = true;
    target_element = {"pause.target", 0,    KES_DIRECT | KES_PICTURE | KES_PAUSE_LAYOUT | unsigned(pv.layer),
                      107.2f,         54,   41.6f,
                      62.4f,          image};
    return true;
}

// A piece's drawn size (DS dots): the title a quarter as tall as wide (its strip is 12 x 2 cells:
// it reaches past them), the others as their strips.
void piece_size(const KhdaysPausePiece &p, float &w, float &h)
{
    w = static_cast<float>(p.w * 8);
    h = p.key == KHDAYS_PAUSE_TITLE ? w / 4.0f : static_cast<float>(p.h * 8);
}

// The pause in the mod's pictures; false when the mod has not all of them (the game's then).
bool emit_pictures(const KhdaysPauseView &pv)
{
    KhdaysBgLayer l;
    const int scale = khdays_ui_scale();
    const char *code = kCodes[pv.language >= 1 && pv.language <= 5 ? pv.language : 1];
    int images[KHDAYS_PAUSE_PIECES];
    if (!khdays_bg_text_layer(kScreen, pv.layer, &l)) {
        return false;
    }
    if (scale != pictures_scale) {
        for (const auto &p : pictures) {
            khdays_ui_image_put(p.image);
        }
        pictures.clear();
        pictures_scale = scale;
    }
    const float ox = static_cast<float>(-(l.hofs & (l.wt * 8 - 1))),
                oy = static_cast<float>(-(l.vofs & (l.ht * 8 - 1)));
    for (int i = 0; i < pv.count; ++i) {
        float w, h;
        piece_size(pv.piece[i], w, h);
        images[i] = picture(piece_file(pv.piece[i], code), static_cast<int>(w) * scale, static_cast<int>(h) * scale);
        if (images[i] < 0) {
            return false;
        }
    }
    for (int i = 0; i < pv.count; ++i) {
        const KhdaysPausePiece &p = pv.piece[i];
        float w, h;
        piece_size(p, w, h);
        emit_picture(p.key == KHDAYS_PAUSE_TITLE      ? "pause.title"
                     : p.key == KHDAYS_PAUSE_QUESTION ? "pause.question"
                                                      : "pause.line",
                     pv.layer, images[i], ox + static_cast<float>(p.x * 8),
                     oy + static_cast<float>(p.y * 8) + (static_cast<float>(p.h * 8) - h) / 2.0f, w, h);
        for (int cy = p.y; cy < p.y + p.h; ++cy) {
            for (int cx = p.x; cx < p.x + p.w; ++cx) {
                khdays_ui_left_out_cell(KHDAYS_UI_PAUSE, kScreen, pv.layer, cx % l.wt, cy % l.ht);
            }
        }
    }
    for (int i = 0; i < pv.count; ++i) {
        // then the hand (given later, drawn over its line: ui/elements.c), where the game's lit strip
        // has it
        const KhdaysPausePiece &p = pv.piece[i];
        if (p.key < KHDAYS_PAUSE_CONTINUE || p.lit == 0) {
            continue;
        }
        const float lx = ox + static_cast<float>(p.x * 8), ly = oy + static_cast<float>(p.y * 8);
        const int hand =
            picture("layouts/pause/hand.png", static_cast<int>(kHand) * scale, static_cast<int>(kHand) * scale);
        if (hand >= 0) {
            emit_picture("pause.hand", pv.layer, hand, lx + kHandLeft, ly + kHandTop, kHand, kHand);
        }
    }
    return true;
}

// The game's pause -- a scene's (PAUSE, Continue, Skip; or PAUSE alone), a mission's (Continue, the
// port's Retry, Withdraw; its "Are you sure?" and Yes, No) -- drawn by the window as Kingdom Hearts II's
// (host/screens/game_pause.h): the game's pieces left out; false when it has a piece the window does not
// draw or the window cannot.
int kh2_row;
bool kh2_pause(const KhdaysPauseView &pv)
{
    KhdaysBgLayer l;
    int keys[3] = {0, 0, 0}, rows = 0, lit = -1;
    bool question = false;
    if (pv.count < 1 || pv.piece[0].key != KHDAYS_PAUSE_TITLE || !khdays_game_pause_kh2_ready() ||
        !khdays_bg_text_layer(kScreen, pv.layer, &l)) {
        return false;
    }
    for (int i = 1; i < pv.count; ++i) {
        const KhdaysPausePiece &p = pv.piece[i];
        if (p.key == KHDAYS_PAUSE_QUESTION) {
            question = true;
            continue;
        }
        if (p.key < KHDAYS_PAUSE_CONTINUE || p.key > KHDAYS_PAUSE_NO || rows == 3) {
            return false;
        }
        if (p.lit != 0) {
            lit = rows;
        }
        keys[rows++] = p.key;
    }
    if (lit >= 0) {
        kh2_row = lit; // (the lit strip blinks: the last lit kept between)
    }
    for (int i = 0; i < pv.count; ++i) {
        // (out of the display capture too: a scene's pause during its camera's blend shows the capture)
        const KhdaysPausePiece &p = pv.piece[i];
        for (int cy = p.y; cy < p.y + p.h; ++cy) {
            for (int cx = p.x; cx < p.x + p.w; ++cx) {
                khdays_ui_left_out_cell_captured(KHDAYS_UI_PAUSE, kScreen, pv.layer, cx % l.wt, cy % l.ht);
            }
        }
    }
    khdays_game_pause_publish(1, rows, keys, kh2_row < rows ? kh2_row : 0, question ? 1 : 0, pv.language);
    return true;
}

// The game over's menu (runtime/menus/retry.h) drawn by the window as KH2's pause, as the game's own pause
// is: the game's panel (its BG1, nothing else there) left out whole while it shows.
void game_over()
{
    KhdaysGameOverView v;
    KhdaysBgLayer l;
    const bool ours = khdays_game_over_ours() && khdays_game_pause_kh2_ready();
    khdays_game_over_set_kh2(ours ? 1 : 0);
    if (ours) {
        // the game's own firefly, the orb that floats to the line chosen (an ov002 display object: sprites
        // of engine A, the game over's only ones), left out the whole scene: KH2's is drawn instead
        for (int i = 0; i < 128; ++i) {
            khdays_ui_left_out_obj(KHDAYS_UI_PAUSE, kScreen, i);
        }
    }
    if (!khdays_game_over_view(&v) || !khdays_game_pause_kh2_ready() ||
        !khdays_bg_text_layer(kScreen, KHDAYS_GAME_OVER_LAYER, &l)) {
        khdays_game_over_publish(0, 0, nullptr, 0, 0, 0);
        return;
    }
    for (int cy = 0; cy < l.ht; ++cy) {
        for (int cx = 0; cx < l.wt; ++cx) {
            khdays_ui_left_out_cell(KHDAYS_UI_PAUSE, kScreen, KHDAYS_GAME_OVER_LAYER, cx, cy);
        }
    }
    khdays_game_over_publish(1, v.rows, v.keys, v.row, v.question, v.language);
}

bool prepare_retry_lines(const KhdaysRetryView &v, const KhdaysBgLayer &l)
{
    if (v.language != seen_language) {
        std::memset(seen.have, 0, sizeof(seen.have)); // the lines' words are the language's
        seen_language = v.language;
    }
    // the game's lines now: Continue lit while its cursor is on it, Give Up while on Give Up
    for (int k = 0; k < 2; ++k) {
        const int lit = v.game_cursor == (k == 0 ? 0 : 2);
        if (!read_line(l, kGameRow[k], live[k])) {
            return false;
        }
        seen.line[k][lit] = live[k];
        seen.have[k][lit] = true;
    }
    // the port's: each line lit as its cursor says, in the state VRAM held last
    for (int k = 0; k < 2; ++k) {
        const int lit = v.cursor == (k == 0 ? 0 : 2);
        drawn[k == 0 ? 0 : 2] = seen.have[k][lit] ? seen.line[k][lit] : live[k];
    }
    {
        // Retry from a line in the state it is drawn in: Give Up's lit one, or either's plain one
        const int lit = v.cursor == 1;
        const Line *from = lit ? (seen.have[1][1] ? &seen.line[1][1] : nullptr)
                               : (seen.have[1][0]   ? &seen.line[1][0]
                                  : seen.have[0][0] ? &seen.line[0][0]
                                                    : nullptr);
        if (from == nullptr || !retry_line(*from, khdays_text_in(TXT_PAUSE_RETRY, v.language), drawn[1])) {
            return false; // the game's two lines, as it draws them
        }
    }
    return true;
}

bool emit_native_target_menu(const KhdaysPauseView &pv)
{
    struct NativePiece {
        int image = -1, w = 0, h = 0;
        std::vector<unsigned> pixels;
    };
    static std::array<NativePiece, KHDAYS_PAUSE_PIECES> pieces;
    KhdaysBgLayer l;
    if (!khdays_bg_text_layer(0, pv.layer, &l))
        return false;
    KhdaysRetryView retry_view;
    const bool retry = khdays_retry_view(&retry_view) != 0;
    if (retry && !prepare_retry_lines(retry_view, l))
        return false;
    for (int i = 0; i < pv.count; ++i) {
        const auto &p = pv.piece[i];
        auto &cached = pieces[i];
        const int w = p.w * 8, h = p.h * 8;
        std::vector<unsigned> pixels;
        const int row = p.key == KHDAYS_PAUSE_WITHDRAW ? kGameRow[1] : p.y;
        if (retry && p.key >= KHDAYS_PAUSE_CONTINUE) {
            if (w != kW || h != kH)
                return false;
            const int index = p.key == KHDAYS_PAUSE_CONTINUE ? 0 : p.key == KHDAYS_PAUSE_RETRY ? 1 : 2;
            const auto &line = drawn[index];
            pixels.resize(line.size());
            std::transform(line.begin(), line.end(), pixels.begin(),
                           [](unsigned v) { return v ? palette_colour(0, v & 255u) : 0u; });
        } else if (!native_rectangle(0, pv.layer, p.x * 8, row * 8, w, h, pixels))
            return false;
        if (cached.image < 0 || cached.w != w || cached.h != h) {
            khdays_ui_image_put(cached.image);
            cached.image = khdays_ui_image_get(w, h);
            cached.w = w;
            cached.h = h;
            cached.pixels.clear();
        }
        if (cached.image < 0)
            return false;
        if (cached.pixels != pixels) {
            auto *out = khdays_ui_image_begin(cached.image);
            if (!out)
                return false;
            std::copy(pixels.begin(), pixels.end(), out);
            khdays_ui_image_end(cached.image);
            cached.pixels = std::move(pixels);
        }
    }
    for (int i = 0; i < pv.count; ++i) {
        const auto &p = pv.piece[i];
        float w, h;
        w = float(p.w * 8);
        h = float(p.h * 8);
        emit_picture(p.key == KHDAYS_PAUSE_TITLE ? "pause.title" : "pause.line", pv.layer, pieces[i].image,
                     float(p.x * 8 - l.hofs), float(p.y * 8 - l.vofs), w, h);
        const int row = p.key == KHDAYS_PAUSE_WITHDRAW ? kGameRow[1] : p.y;
        for (int y = row; y < row + p.h; ++y)
            for (int x = p.x; x < p.x + p.w; ++x)
                khdays_ui_left_out_cell(KHDAYS_UI_PAUSE, 0, pv.layer, x % l.wt, y % l.ht);
    }
    return true;
}

void emit()
{
    target_layout = false;
    KhdaysRetryView v;
    KhdaysPauseView pv;
    KhdaysBgLayer l;
    game_over();
    const bool viewed = khdays_pause_view(&pv) != 0;
    if (viewed && kh2_pause(pv)) {
        return; // the Menu Overhaul's KH2 pause (its mod's pieces there)
    }
    khdays_game_pause_publish(0, 0, nullptr, 0, 0, 0);
    if (!viewed) {
        kh2_row = 0; // the next pause opens on Continue
    }
    if (viewed) {
        emit_target_pause(pv);
        if (emit_pictures(pv) || (target_layout && emit_native_target_menu(pv))) {
            if (target_layout) {
                KhdaysBgLayer source;
                if (khdays_bg_text_layer(0, pv.layer, &source))
                    for (int y = 0; y < source.ht; ++y)
                        for (int x = 0; x < source.wt; ++x)
                            khdays_ui_left_out_cell(KHDAYS_UI_PAUSE, 0, pv.layer, x, y);
                khdays_ui_emit(KHDAYS_UI_PAUSE, &target_element);
            }
            return;
        }
        target_layout = false;
    }
    if (!khdays_retry_view(&v) || !khdays_bg_text_layer(kScreen, kLayer, &l)) {
        return;
    }
    if (!prepare_retry_lines(v, l))
        return;
    for (int k = 0; k < 3; ++k) {
        emit_line(k, drawn[k], kCellX * 8 - (l.hofs & (l.wt * 8 - 1)), kRow[k] * 8 - (l.vofs & (l.ht * 8 - 1)));
    }
    for (int row = kGameRow[0]; row < kRow[2] + kCellsH; ++row) {
        for (int i = 0; i < kCellsW; ++i) {
            khdays_ui_left_out_cell(KHDAYS_UI_PAUSE, kScreen, kLayer, (kCellX + i) % l.wt, row % l.ht);
        }
    }
}

} // namespace

extern "C" void khdays_ui_pause_init(void)
{
    khdays_ui_register(KHDAYS_UI_PAUSE, emit);
}
