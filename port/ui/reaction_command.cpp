// A reaction command offered to this game's own player (reaction_command.h), as KINGDOM HEARTS II shows it: the bar of
// KH2's general command menu over the command deck -- field2d/<language>/zz0command.2dd's "comw" sequences and their
// HD picture, which the KH2 enemies' mod copies from the player's 1.5+2.5 -- with the button's icon in its circle and
// the command's name, and the icon alone over the enemy offering it. Its sequences: 5 the bar coming, 3 the same with
// "COMBO!" (a command offered after one taken), 4 the button's flash at a press, 0 the bar leaving. Two kinds of
// command: a KH2 enemy's (runtime/kh2_enemies) and, with the setting on, Days's own Talk, Examine and the rest, which
// leave the deck's Attack where it is (prepare.py's joins in Ov022_UpdateCommandInput, Ov022_TranslateCommandToAction
// and Ov002_DrawPanelRowText). Without the bar's pieces: the icon and the name in the game's 8-dot font, white with a
// black edge, and Days's deck as it is.
#include "reaction_command.h"

#include "../gfx/kh2_sequence.h"
#include "../host/app/hud_settings.h"
#include "../host/app/profile.h"
#include "../host/render/kh2_sequence_asset.h"
#include "kh2_font.h"

extern "C" {
#include "../gfx/g3d.h"
#include "button_icons.h"
#include "elements.h"
#include "parts.h"
#include "../host/screens/ui_text.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../runtime/core/window.h"
#include "../runtime/mods/font_runtime.h"
#include "../runtime/mods/textures.h"
#include "../runtime/scripts/game_link.h"
int khdays_settings_game_language(void);
unsigned khdays_input_game_frames(void); // runtime/input/input.c
const KhdaysProfile *khdays_devices_profile(void); // host/app/devices.h
}

#include "khdays/assets/font.h"
#include "khdays/assets/image.h"
#include "khdays/assets/kh2_bar.h"
#include "khdays/vfs/filesystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "../runtime/kh2_enemies/shell.h"

// the command deck's main rows (prepare.py's joins in Ov002_PanelAdvanceCursor's file): the row its cursor is on (-1 a
// list open, -2 no deck), how many
extern "C" int khdays_deck_main_row(void);
extern "C" int khdays_deck_main_rows(void);

namespace {

namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;

constexpr char16_t kButtonX = 0x3322; // the game's fonts' X button
constexpr unsigned kKeyX = 0x400;     // the DS's X in the game's pad words: the pad's top button, KH2's triangle
// the command deck's box on the upper screen (hud.c's "hud.deck": 0, 136 to 88, 192): the prompt just above it
constexpr int kDeckLeft = 4, kDeckTop = 136, kDeckRight = 88;
// comw's sequences (our reading of zz0command.2dd against KH2's own play, docs/KH2_ENEMIES.md)
constexpr std::size_t kLeave = 0, kIdle = 1, kCombo = 3, kPress = 4, kAppear = 5;
constexpr int kLeaveFrames = 16, kPressFrames = 25;

std::filesystem::path path_of(const char *s)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s)));
}

// ---- The game's fonts: the name's 8 dots, the buttons' glyphs' 10 (the 8-dot font has none) -------------------

struct TextFont {
    const char *name;
    const char *scope; // the HD fonts' scope whose route it takes (the deck's labels'), nullptr its own
    std::optional<khdays::assets::Font> font;
    std::string route;
    bool tried = false;

    const khdays::assets::Font *get()
    {
        if (!tried) {
            tried = true;
            route = name;
            const auto path = khdays::vfs::resolve(name);
            try {
                if (path) {
                    font = khdays::assets::decode_nftr(*path);
                    char selected[512];
                    const int observing = khdays_hd_font_observe(1);
                    if (scope != nullptr && khdays_hd_font_select(name, scope, selected, sizeof(selected)))
                        route = selected;
                    khdays_hd_font_observe(observing);
                    std::vector<std::uint32_t> codes(font->glyphs.size(), ~0u);
                    for (const auto &[code, index] : font->char_to_glyph)
                        if (index >= 0 && std::size_t(index) < codes.size())
                            codes[std::size_t(index)] = code;
                    khdays_hd_font_bind_charmap(route.c_str(), codes.data(), int(codes.size()));
                }
            } catch (const std::exception &e) {
                std::fprintf(stderr, "ui: the reaction commands' font %s could not be read (%s)\n", name, e.what());
                font.reset();
            }
        }
        return font ? &*font : nullptr;
    }
};
TextFont label_font{"text/font_eu_08.nftr", "field.command"};
TextFont button_font{"text/font_eu_10.nftr", nullptr};

// The text's width in font dots, as the HD font lays it out (the game's own glyphs' without one)
double text_width(TextFont &tf, const std::u16string &s)
{
    const auto *font_of = tf.get();
    if (font_of == nullptr)
        return 0;
    const auto &f = *font_of;
    const char *route = tf.route.c_str();
    double pen = 0;
    char16_t prev = 0;
    for (const char16_t c : s) {
        const int i = f.glyph_of(c);
        if (i < 0) {
            pen += f.cell_width / 2;
            continue;
        }
        pen += khdays_hd_font_kerning_f(route, prev, c);
        pen += khdays_hd_font_metric_f(route, i, KHDF_ADVANCE, f.glyphs[std::size_t(i)].advance);
        prev = c;
    }
    return pen;
}

// The text painted into `out` (`w` x `h` texels, `r` texels a font dot) from x, y font dots: white over a black
// shadow a dot down and right, as the deck's labels
void paint_text(TextFont &tf, const std::u16string &s, std::vector<std::uint32_t> &out, int w, int h, int r, double x,
                double y)
{
    const auto *font_of = tf.get();
    if (font_of == nullptr)
        return;
    const auto &f = *font_of;
    const char *route = tf.route.c_str();
    for (int pass = 0; pass < 2; ++pass) {
        const std::uint32_t tint = pass != 0 ? 0xffffffffu : 0xff000000u;
        const double shadow = pass != 0 ? 0.0 : 1.0;
        double pen = x;
        char16_t prev = 0;
        for (const char16_t c : s) {
            const int i = f.glyph_of(c);
            if (i < 0) {
                pen += f.cell_width / 2;
                continue;
            }
            const auto &g = f.glyphs[std::size_t(i)];
            pen += khdays_hd_font_kerning_f(route, prev, c);
            if (!khdays_hd_font_paint(route, i, pen + shadow, y + shadow, f.cell_width, f.cell_height, tint, out.data(),
                                      w, h, r)) {
                for (int yy = 0; yy < f.cell_height * r; ++yy)
                    for (int xx = 0; xx < f.cell_width * r; ++xx) {
                        const unsigned alpha = g.alpha[std::size_t(yy / r) * std::size_t(f.cell_width) + xx / r];
                        const int px = int(std::lround((pen + g.left + shadow) * r)) + xx;
                        const int py = int(std::lround((y + shadow) * r)) + yy;
                        if (alpha != 0 && px >= 0 && py >= 0 && px < w && py < h)
                            out[std::size_t(py) * std::size_t(w) + std::size_t(px)] = (tint & 0xffffffu) | alpha << 24;
                    }
            }
            pen += khdays_hd_font_metric_f(route, i, KHDF_ADVANCE, g.advance);
            prev = c;
        }
    }
}

// ---- KH2's "sys" font: the bar's names, as KH2 writes them (kh2_font.h) ----------------------------------------

using Kh2Font = khdays::ui::kh2::SysFont;
Kh2Font &kh2_font = khdays::ui::kh2::sys_font();

// The text in KH2's font painted into `out` (`w` x `h` texels) from x, y texels, `k` texels a KH2 dot: white with a
// dark edge, as KH2's command names
void paint_kh2(const std::u16string &text, std::vector<std::uint32_t> &out, int w, int h, float x, float y, float k)
{
    khdays::ui::kh2::paint_text(text, out, w, h, x, y, k);
}

// ---- The button's icon --------------------------------------------------------------------------------------------

// The icon of the player's input for the pad's top button (KH2's triangle: the DS X), the HD PACK's or Kenney's, as
// an image and its texels; -1 none (the game's glyph is drawn then)
struct Icon {
    int image = -1;
    int w = 0, h = 0;
    std::uint32_t signature = 0;
    std::vector<std::uint32_t> texels;
};
Icon icon;

const Icon *the_icon()
{
    const std::uint32_t signature = khdays_button_icons_signature();
    if (icon.image >= 0 && icon.signature == signature) {
        return &icon;
    }
    int w = 0, h = 0;
    const std::uint8_t *rgba = khdays_button_icon(KHDAYS_BUTTON_X, 0, &w, &h);
    khdays_ui_image_put(icon.image);
    icon = {};
    if (rgba == nullptr || w <= 0 || h <= 0 || (icon.image = khdays_ui_image_get(w, h)) < 0) {
        return nullptr;
    }
    std::uint32_t *texels = khdays_ui_image_begin(icon.image);
    if (texels == nullptr) {
        return nullptr;
    }
    icon.texels.resize(std::size_t(w) * std::size_t(h));
    for (int i = 0; i < w * h; ++i) {
        const std::uint8_t *c = rgba + 4 * i;
        icon.texels[std::size_t(i)] =
            std::uint32_t(c[0]) | std::uint32_t(c[1]) << 8 | std::uint32_t(c[2]) << 16 | std::uint32_t(c[3]) << 24;
        texels[i] = icon.texels[std::size_t(i)];
    }
    khdays_ui_image_end(icon.image);
    icon.w = w;
    icon.h = h;
    icon.signature = signature;
    return &icon;
}

// The icon in a box of `size` DS dots whose left, top is x, y, as large as fits; false without one
bool emit_icon(const char *id, float x, float y, float size)
{
    const Icon *i = the_icon();
    if (i == nullptr) {
        return false;
    }
    double ox = 0.0, oy = 0.0, texel = 0.0;
    khdays_button_fit(size, size, i->w, i->h, &ox, &oy, &texel);
    KhdaysUiElement e;
    e.id = id;
    e.screen = 0;
    e.slot = KES_DIRECT | 3u;
    e.x = x + static_cast<float>(ox);
    e.y = y + static_cast<float>(oy);
    e.w = static_cast<float>(i->w * texel);
    e.h = static_cast<float>(i->h * texel);
    e.image = i->image;
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
    return true;
}

// ---- Without the bar: the game's glyph and font --------------------------------------------------------------------

constexpr float kIcon = 11.0f; // the icon's box above the deck (DS dots); over the enemy
constexpr float kMarkIcon = 14.0f;

struct Drawn {
    std::u16string text;
    int scale = 0;
    int image = -1;
    int w = 0, h = 0; // the font's dots
    int dot = 0;      // texels per font dot
};
Drawn drawn, glyph, mark;

// The text in font `tf` in white over a black edge one dot round it, a font dot a DS dot's texels
bool draw(Drawn &d, TextFont &tf, const std::u16string &text, int scale)
{
    if (d.image >= 0 && d.text == text && d.scale == scale) {
        return true;
    }
    const auto *f = tf.get();
    if (f == nullptr) {
        return false;
    }
    const auto r = khdays::assets::render_text(*f, text);
    const int w = r.width + 2, h = r.height + 2;
    std::vector<std::uint32_t> dots(static_cast<std::size_t>(w * h), 0u);
    auto inked = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < r.width && y < r.height &&
               r.rgba[(static_cast<std::size_t>(y) * r.width + x) * 4 + 3] >= 128;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy) {
                for (int dx = -1; dx <= 1 && !edge; ++dx) {
                    edge = inked(x - 1 + dx, y - 1 + dy);
                }
            }
            if (inked(x - 1, y - 1)) {
                dots[static_cast<std::size_t>(y * w + x)] = 0xffffffffu;
            } else if (edge) {
                dots[static_cast<std::size_t>(y * w + x)] = 0xff000000u;
            }
        }
    }
    const int dot = scale > 0 ? scale : 1;
    khdays_ui_image_put(d.image);
    d.image = khdays_ui_image_get(w * dot, h * dot);
    if (d.image < 0) {
        return false;
    }
    std::uint32_t *texels = khdays_ui_image_begin(d.image);
    if (texels == nullptr) {
        return false;
    }
    for (int y = 0; y < h * dot; ++y) {
        for (int x = 0; x < w * dot; ++x) {
            texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w * dot) + static_cast<std::size_t>(x)] =
                dots[static_cast<std::size_t>((y / dot) * w + x / dot)];
        }
    }
    khdays_ui_image_end(d.image);
    d.text = text;
    d.scale = scale;
    d.w = w;
    d.h = h;
    d.dot = dot;
    return true;
}

// The game's X glyph in a box of `size` DS dots whose left, top is x, y (no icon for the input)
bool emit_glyph(Drawn &d, const char *id, float x, float y, float size)
{
    const int scale = khdays_ui_scale();
    if (!draw(d, button_font, std::u16string(1, kButtonX), scale))
        return false;
    KhdaysUiElement e;
    e.id = id;
    e.screen = 0;
    e.slot = KES_DIRECT | 3u;
    e.w = static_cast<float>(d.w * d.dot) / static_cast<float>(scale);
    e.h = static_cast<float>(d.h * d.dot) / static_cast<float>(scale);
    e.x = x + (size - e.w) / 2.0f;
    e.y = y + (size - e.h) / 2.0f;
    e.image = d.image;
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
    return true;
}

// The prompt without the bar: the icon (the game's glyph without one), then the name, above the deck
void emit_plain(const std::u16string &name)
{
    const float top = static_cast<float>(kDeckTop) - kIcon - 1.0f;
    const bool with_icon = emit_icon("hud.reaction_command_button", static_cast<float>(kDeckLeft), top, kIcon) ||
                           emit_glyph(glyph, "hud.reaction_command_button", static_cast<float>(kDeckLeft), top, kIcon);
    const int scale = khdays_ui_scale();
    if (name.empty() || !draw(drawn, label_font, name, scale)) {
        return;
    }
    KhdaysUiElement e;
    e.id = "hud.reaction_command";
    e.screen = 0;
    e.slot = KES_DIRECT | 3u; /* in the party's layer (BG3), as the players' names */
    e.w = static_cast<float>(drawn.w * drawn.dot) / static_cast<float>(scale);
    e.h = static_cast<float>(drawn.h * drawn.dot) / static_cast<float>(scale);
    e.x = static_cast<float>(kDeckLeft) + (with_icon ? kIcon + 2.0f : 0.0f);
    e.y = top + (kIcon - e.h) / 2.0f;
    e.image = drawn.image;
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
}

// Where the 3D build last put the reaction command's mark (khdays_ui_reaction_mark_draw): the upper screen's DS
// dots, and that frame
struct MarkSpot {
    float x = 0.0f, y = 0.0f;
    unsigned frame = ~0u;
} mark_spot;

// The icon over the enemy offering a KH2 reaction command (where KH2's mark goes), when that was drawn this frame or
// the one before
void emit_mark()
{
    if (mark_spot.frame == ~0u || khdays_link_frame() - mark_spot.frame > 1u)
        return;
    const float x = mark_spot.x, y = mark_spot.y;
    // just above its aim point
    if (!emit_icon("hud.reaction_command_mark", x - kMarkIcon / 2.0f, y - kMarkIcon - 2.0f, kMarkIcon))
        emit_glyph(mark, "hud.reaction_command_mark", x - kMarkIcon / 2.0f, y - kMarkIcon - 2.0f, kMarkIcon);
}

// ---- Days's own commands: Talk, Examine and the rest ---------------------------------------------------------------

// What Ov022_UpdateCommandInput offers this game's own player: the command of the thing aimed at (its + 0x17's low
// bits, 1 Examine, 2 Talk ...: its label UI/btl/<language>/cmd.s's record kind + 5, as the deck's rows 1 and 2 have
// it), the game frame it did, and the reaction button's presses
struct DaysOffer {
    int kind = 0;
    bool usable = false;
    unsigned frame = 0;
    unsigned presses = 0;
    bool pressed = false; // this frame's: khdays_reaction_talk_commit makes the command
    // the X's of the pad's last sample (the game keeps its pressed buttons while it does not sample it): a press is
    // X newly there
    unsigned sample = ~0u;
    bool x_before = false, x_now = false;
    bool swallowed = false; // this frame's X is a KH2 enemy's reaction command's: no Days command from it
} days;

std::vector<std::u16string> labels; // UI/btl/<language>/cmd.s's records
int labels_language = -1;

// The deck's label `record` in the game's language (u32 the header's size, u32 the count, then each record's u32
// length -- these 4 bytes in it -- and its UTF-16LE text)
const std::u16string &label(int record)
{
    static const std::u16string none;
    const int language = khdays_settings_game_language();
    if (language != labels_language) {
        labels_language = language;
        labels.clear();
        char name[64];
        const char *code = khdays_language_code(language);
        std::snprintf(name, sizeof(name), "UI/btl/%s/cmd.s.z", code != nullptr && *code ? code : "en");
        static KhdaysRomReader *reader = khdays_rom_reader_new();
        u8 *data = nullptr;
        u32 size = 0;
        if (reader != nullptr && khdays_rom_file_data(reader, name, -1, &data, &size) && size >= 8) {
            std::uint32_t at = 0, count = 0;
            std::memcpy(&at, data, 4);
            std::memcpy(&count, data + 4, 4);
            for (std::uint32_t i = 0; i < count && at <= size - 4; ++i) {
                std::uint32_t n = 0;
                std::memcpy(&n, data + at, 4);
                if (n < 4 || n > size - at)
                    break;
                std::u16string s;
                for (std::uint32_t k = 4; k + 1 < n; k += 2) {
                    const char16_t c = char16_t(data[at + k] | data[at + k + 1] << 8);
                    if (c == 0)
                        break;
                    s.push_back(c);
                }
                labels.push_back(std::move(s));
                at += n;
            }
        }
        std::free(data);
    }
    return record >= 0 && std::size_t(record) < labels.size() ? labels[std::size_t(record)] : none;
}

// ---- The bar ------------------------------------------------------------------------------------------------------

struct Bounds {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
    void add(const seq::SequenceFrame &f)
    {
        for (const auto &q : f.quads)
            for (const auto &v : q.vertices) {
                x0 = any ? std::min(x0, v.x) : v.x;
                x1 = any ? std::max(x1, v.x) : v.x;
                y0 = any ? std::min(y0, v.y) : v.y;
                y1 = any ? std::max(y1, v.y) : v.y;
                any = true;
            }
    }
};

float alpha_of(const seq::SequenceFrame &f)
{
    float a = 0;
    for (const auto &q : f.quads)
        for (const auto &v : q.vertices)
            a = std::max(a, float(v.color >> 24));
    return a;
}

// A sequence's picture registered as `texture_id` (`pixels` keeps its texels)
void register_atlas(const render::SequenceAsset &asset, const char *texture_id, std::vector<std::uint32_t> &pixels,
                    int &atlas)
{
    pixels.resize(asset.texture.rgba.size() / 4);
    std::memcpy(pixels.data(), asset.texture.rgba.data(), asset.texture.rgba.size());
    atlas = khdays_textures_add_info(texture_id, asset.texture.width, asset.texture.height, pixels.data(),
                                     KHDAYS_REPLACEMENT_NO_MIPS);
    if (atlas < 0)
        throw std::runtime_error("its picture could not be registered");
}

// A KH2 2D piece of the mod's: kh2/field2d/<region>/<name> and its HD picture <REGION>_<name with _ for .><page>.png,
// the game's language's region first, then the English one; its sequence entry `entry` loaded and its picture
// registered as `texture_id`. Throws when it is there but cannot be read; false when it is not there.
bool load_piece(const char *name, int page, const char *entry, const char *texture_id,
                std::optional<render::SequenceAsset> &asset, std::vector<std::uint32_t> &pixels, int &atlas,
                std::string &from)
{
    static const char *const regions[][2] = {{"en", "us"}, {"fr", "fr"}, {"de", "gr"}, {"it", "it"}, {"es", "sp"}};
    const char *code = khdays_language_code(khdays_settings_game_language());
    const char *region = "us";
    for (const auto &r : regions)
        if (code != nullptr && std::strcmp(code, r[0]) == 0)
            region = r[1];
    std::string stem = name;
    std::replace(stem.begin(), stem.end(), '.', '_');
    char archive[128], picture[160], found[2048] = "", found_picture[2048] = "";
    for (const char *folder : {region, "us"}) {
        std::string upper = folder;
        for (char &c : upper)
            c = char(std::toupper(static_cast<unsigned char>(c)));
        std::snprintf(archive, sizeof(archive), "kh2/field2d/%s/%s", folder, name);
        std::snprintf(picture, sizeof(picture), "kh2/field2d/%s/%s_%s%d.png", folder, upper.c_str(), stem.c_str(),
                      page);
        if (khdays_mods_find(archive, found, sizeof(found)) &&
            khdays_mods_find(picture, found_picture, sizeof(found_picture)))
            break;
        found[0] = '\0';
    }
    if (found[0] == '\0')
        return false;
    asset = render::load_sequence_asset(path_of(found), entry, path_of(found_picture));
    register_atlas(*asset, texture_id, pixels, atlas);
    from = found;
    return true;
}

// The command menu an enemy's own pictures draw (khdays_kh2_enemies_command_image: Zexion's book): its .a.us and its
// HD picture (-<n>.png, or .dds), "" none
struct OwnImage {
    std::string archive, picture;
    std::string key() const
    {
        return archive.empty() ? std::string() : archive + "|" + picture;
    }
};

OwnImage own_image()
{
    static std::string last_stem;
    static OwnImage last;
    char archive[2048], stem[512], picture[2048];
    if (!khdays_kh2_enemies_command_image(archive, int(sizeof(archive)), stem, int(sizeof(stem))))
        return {};
    if (last_stem == stem && last.archive == archive)
        return last; // looked for once
    last_stem = stem;
    last = {archive, ""};
    for (const char *extension : {".png", ".dds"})
        if (khdays_mods_find((std::string(stem) + extension).c_str(), picture, int(sizeof(picture)))) {
            last.picture = picture;
            break;
        }
    return last;
}

// Its "comd" IMGD and SEQD loaded and its picture registered as `texture_id`; false without its HD picture (the
// port draws KH2's 2D from the HD pictures alone)
bool load_own(const OwnImage &own, const char *texture_id, std::optional<render::SequenceAsset> &asset,
              std::vector<std::uint32_t> &pixels, int &atlas, std::string &from)
{
    if (own.archive.empty() || own.picture.empty())
        return false;
    asset = render::load_sequence_asset(path_of(own.archive.c_str()), "comd", path_of(own.picture.c_str()));
    register_atlas(*asset, texture_id, pixels, atlas);
    from = own.archive;
    return true;
}

struct Bar {
    // the bar in an enemy's own command menu's pictures (its "comd" sequences, whose clips are numbered as comw's: 1:359,
    // Zexion's book): KH2's COMMAND_DRAW::getSqd gives every part of the menu the image set in place of its own
    bool own = false;
    std::string own_key;
    bool tried = false;
    int language = -1;
    std::optional<render::SequenceAsset> asset;
    std::vector<std::uint32_t> atlas_pixels;
    int atlas = -1;
    float scale = 0;                  // DS dots a KH2 unit
    float anchor_x = 0, anchor_y = 0; // KH2's 0, 0 on the upper screen (DS dots)
    Bounds idle, all;                 // the bar standing; every frame of its sequences (KH2 units)
    float idle_alpha = 128;
    int image = -1, image_scale = 0, face = -1, face_w = 0, face_h = 0;
    std::u16string face_name;
    std::uint32_t face_icon = 0;
    KhdaysSequenceImage program{};

    // what it shows: the command (0 none: a KH2 enemy's 0x10000 | its id, Days's 0x20000 | its kind) and its name
    int key = 0;
    std::u16string name;
    bool leaving = false, kept = false;
    seq::SequencePlayer player, flash;
    int leave_frames = 0, flash_frames = -1;
    unsigned taken = 0, presses = 0, last_vblank = 0;
    bool started = false;

    void load()
    {
        const int lang = khdays_settings_game_language();
        const OwnImage want = own ? own_image() : OwnImage{};
        if (tried && lang == language && want.key() == own_key)
            return;
        tried = true;
        language = lang;
        own_key = want.key();
        asset.reset();
        try {
            std::string found;
            if (own ? !load_own(want, "kh2:reaction_command:own_atlas", asset, atlas_pixels, atlas, found)
                    : !load_piece("zz0command.2dd", 0, "comw", "kh2:reaction_command:atlas", asset, atlas_pixels,
                                  atlas, found))
                return;
            const auto &d = asset->sequence;
            idle = {};
            all = {};
            const auto standing = seq::evaluate_sequence(d, kIdle, 0);
            idle.add(standing);
            idle_alpha = std::max(1.0f, alpha_of(standing));
            for (const std::size_t clip : {kLeave, kIdle, kCombo, kPress, kAppear}) {
                const int frames = seq::sequence_frame_count(d, clip);
                if (frames <= 0)
                    throw std::runtime_error("a sequence of the bar has no frames");
                for (int t = 0; t < frames; ++t)
                    all.add(seq::evaluate_sequence(d, clip, float(t)));
            }
            if (!idle.any || idle.x1 - idle.x0 < 1 || !all.any)
                throw std::runtime_error("the bar's sequence draws nothing");
            // the bar as wide as the deck, its left at the deck's, its foot a dot above it
            scale = float(kDeckRight - kDeckLeft) / (idle.x1 - idle.x0);
            anchor_x = float(kDeckLeft) - idle.x0 * scale;
            anchor_y = float(kDeckTop - 1) - idle.y1 * scale;
            khdays_log("ui: the reaction commands' bar from %s", found.c_str());
        } catch (const std::exception &e) {
            asset.reset();
            khdays_log("ui: the reaction commands' bar left out (%s): the prompt and Days's deck as they were",
                       e.what());
        }
    }

    bool available()
    {
        if (!khdays_hud_setting(KHDAYS_OPT_REACTION_COMMANDS) || !khdays_textures_hd())
            return false;
        load();
        return asset.has_value();
    }

    // The face drawn over the bar, at `r` texels a DS dot: the icon in the bar's circle and the name after it; its
    // left, top (DS dots from KH2's 0, 0) in `fx`, `fy`
    bool make_face(int r, float &fx, float &fy)
    {
        const bool kh2 = kh2_font.get();
        const auto *f = kh2 ? nullptr : label_font.get();
        const Icon *ic = the_icon();
        const float h = (idle.y1 - idle.y0) * scale;
        // the circle's middle: the light under the button in comw's bar, a little past its rounded end
        const float cx = (idle.x0 + (idle.x1 - idle.x0) * 0.157f) * scale, cy = (idle.y0 + idle.y1) * 0.5f * scale;
        const float size = h * 0.8f;
        // KH2's: its glyphs' cells 0.62 of the bar's height (their capitals half of it, as KH2's)
        const float kh2_dot = h * 0.62f / float(Kh2Font::kCellH);
        const double width = kh2            ? double(kh2_font.width(name) * kh2_dot) + 2
                             : f != nullptr ? text_width(label_font, name) + 2
                                            : 0;
        const float text_h = kh2 ? float(Kh2Font::kCellH) * kh2_dot : f != nullptr ? float(f->cell_height) + 1 : 0;
        const float fw = size + 1 + float(width), fh = std::max(size, text_h);
        fx = cx - size / 2;
        fy = cy - fh / 2;
        const int w = std::max(1, int(std::ceil(fw * float(r)))), hh = std::max(1, int(std::ceil(fh * float(r))));
        const std::uint32_t signature = ic != nullptr ? ic->signature : 0;
        if (face >= 0 && face_w == w && face_h == hh && face_name == name && face_icon == signature)
            return true;
        std::vector<std::uint32_t> out(std::size_t(w) * std::size_t(hh), 0u);
        if (ic != nullptr) {
            double ox = 0, oy = 0, texel = 0;
            khdays_button_fit(size, size, ic->w, ic->h, &ox, &oy, &texel);
            const double top = (fh - size) / 2 + oy;
            for (int py = 0; py < hh; ++py)
                for (int px = 0; px < w; ++px) {
                    const int sx = int(std::floor((double(px) / r - ox) / texel));
                    const int sy = int(std::floor((double(py) / r - top) / texel));
                    if (sx >= 0 && sy >= 0 && sx < ic->w && sy < ic->h)
                        out[std::size_t(py) * std::size_t(w) + std::size_t(px)] =
                            ic->texels[std::size_t(sy) * std::size_t(ic->w) + std::size_t(sx)];
                }
        }
        if (ic == nullptr && button_font.get() != nullptr) { // no icon for the input: the game's X glyph in the circle
            const std::u16string x(1, kButtonX);
            const float gh = float(button_font.get()->cell_height);
            paint_text(button_font, x, out, w, hh, r, (size - text_width(button_font, x)) / 2, (fh - gh) / 2);
        }
        if (kh2)
            paint_kh2(name, out, w, hh, (size + 1) * float(r), (fh - text_h) / 2 * float(r), kh2_dot * float(r));
        else if (f != nullptr)
            paint_text(label_font, name, out, w, hh, r, size + 1, (fh - text_h) / 2);
        if (face < 0 || face_w != w || face_h != hh) {
            khdays_ui_image_put(face);
            face = khdays_ui_image_get(w, hh);
        }
        std::uint32_t *texels = face >= 0 ? khdays_ui_image_begin(face) : nullptr;
        if (texels == nullptr)
            return false;
        std::copy(out.begin(), out.end(), texels);
        khdays_ui_image_end(face);
        face_w = w;
        face_h = hh;
        face_name = name;
        face_icon = signature;
        return true;
    }

    void press()
    {
        flash.select(asset->sequence, kPress, true);
        flash_frames = 0;
    }

    void show(int what, const std::u16string &text)
    {
        player.select(asset->sequence, kept ? kCombo : kAppear, true);
        key = what;
        name = text;
        leaving = kept = false;
    }

    void leave()
    {
        player.select(asset->sequence, kLeave, true);
        leaving = true;
        kept = false;
        leave_frames = 0;
    }

    void advance(unsigned frames)
    {
        player.advance(asset->sequence, float(frames));
        if (leaving)
            leave_frames += int(frames);
        if (flash_frames >= 0) {
            flash.advance(asset->sequence, float(frames));
            flash_frames += int(frames);
            if (flash_frames >= kPressFrames)
                flash_frames = -1;
        }
    }

    // This VBlank's command (`what` 0 none; `held`: a KH2 enemy holds the player): the bar's state, its program
    void update(int what, const std::u16string &text, bool held)
    {
        const unsigned vblank = khdays_link_frame(), takes = khdays_kh2_enemies_taken(nullptr);
        if (!started) {
            started = true;
            last_vblank = vblank;
            taken = takes;
            presses = days.presses;
        }
        const unsigned frames = khdays_link_paused() ? 0u : std::min(vblank - last_vblank, 4u);
        last_vblank = vblank;
        if (takes != taken) {
            taken = takes;
            // taken as soon as offered, before a VBlank showed it: shown now, taken
            unsigned short taken_name[64];
            int id = 0;
            const int n = khdays_kh2_enemies_taken_name(taken_name, 64, &id);
            if (id != 0 && (key != (0x10000 | id) || leaving))
                show(0x10000 | id, std::u16string(taken_name, taken_name + n));
            press();
            kept = true; // the command taken stays shown while its enemy holds the player
        }
        if (days.presses != presses) {
            presses = days.presses;
            press();
        }
        if (what != 0 && (what != key || leaving))
            show(what, text);
        else if (what == 0 && key != 0 && !leaving && !(kept && held))
            leave();
        if (key != 0)
            advance(frames);
        if (leaving && leave_frames >= kLeaveFrames) {
            key = 0;
            leaving = kept = false;
        }
        program = {};
        if (key != 0)
            emit();
    }

    void emit()
    {
        const int r = khdays_ui_scale();
        const float left = std::floor(all.x0 * scale) - 2, top = std::floor(all.y0 * scale) - 2;
        const float width = std::ceil(all.x1 * scale) - left + 2, height = std::ceil(all.y1 * scale) - top + 2;
        if (image < 0 || image_scale != r) {
            khdays_ui_image_put(image);
            image = khdays_ui_image_get(int(width) * r, int(height) * r);
            image_scale = r;
        }
        float fx = 0, fy = 0;
        if (image < 0 || !make_face(r, fx, fy))
            return;
        const auto frame = seq::evaluate_sequence(asset->sequence, player.clip(), player.frame());
        std::vector<seq::SequenceQuad> quads = frame.quads;
        const std::size_t bar_quads = quads.size();
        if (flash_frames >= 0) {
            const auto f = seq::evaluate_sequence(asset->sequence, kPress, flash.frame());
            quads.insert(quads.end(), f.quads.begin(), f.quads.end());
        }
        if (quads.size() > KSI_MAX_QUADS)
            quads.resize(KSI_MAX_QUADS);
        auto *w = program.words;
        const auto number = [w](unsigned i, float v) { std::memcpy(w + i, &v, 4); };
        w[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(image));
        w[KSI_ATLAS] = unsigned(atlas);
        w[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
        w[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
        w[KSI_WIDTH] = unsigned(width) * unsigned(r);
        w[KSI_HEIGHT] = unsigned(height) * unsigned(r);
        number(KSI_VIEW_X, -left * float(r));
        number(KSI_VIEW_Y, -top * float(r));
        number(KSI_SCALE, scale * float(r));
        w[KSI_CLEAR] = 1;
        render::encode_sequence_quads(program, quads);
        // the face after the bar's own quads, under the button's flash; it fades with the bar
        w[KSI_TEXT_INSERT] = unsigned(std::min(bar_quads, quads.size())) + 1;
        w[KSI_FACE] = unsigned(khdays_ui_image_replacement(face));
        w[KSI_HD_FACE] = ~0u;
        number(KSI_TEXT_SCALE, 1);
        number(KSI_FACE_X, (fx - left) * float(r));
        number(KSI_FACE_Y, (fy - top) * float(r));
        number(KSI_FACE_W, float(face_w));
        number(KSI_FACE_H, float(face_h));
        number(KSI_TEXT_CLIP_X, 0);
        number(KSI_TEXT_CLIP_Y, 0);
        number(KSI_TEXT_CLIP_W, width * float(r));
        number(KSI_TEXT_CLIP_H, height * float(r));
        number(KSI_TEXT_ALPHA, std::clamp(alpha_of(frame) / idle_alpha, 0.0f, 1.0f));
        KhdaysUiElement e{}, box{};
        e.id = box.id = "hud.reaction_command";
        e.screen = box.screen = 0;
        e.slot = box.slot = KES_DIRECT | KES_FINE | 3u; /* in the party's layer (BG3), as the players' names */
        e.x = anchor_x + left;
        e.y = anchor_y + top;
        e.w = width;
        e.h = height;
        e.image = image;
        // its layout box: the bar standing, inside the deck's (the ring and "COMBO!" reach beyond it)
        box.x = anchor_x + idle.x0 * scale;
        box.y = anchor_y + idle.y0 * scale;
        box.w = (idle.x1 - idle.x0) * scale;
        box.h = (idle.y1 - idle.y0) * scale;
        box.image = image;
        khdays_ui_emit_extended(KHDAYS_UI_HUD, &e, &box);
    }
};
Bar bar;

Bar make_own_bar()
{
    Bar b;
    b.own = true;
    return b;
}
Bar own_bar = make_own_bar();

// ---- An enemy's own command menu ------------------------------------------------------------------------------------

// KH2's command menu in an enemy's own pictures (1:359, runtime/kh2_enemies: Zexion's book while his Lexicon stands):
// its .a.us's "comd" sequences in place of Days's deck, which the HUD then leaves out (hud.c), laid out as
// dk::COMMAND_DRAW lays KH2's main menu (our reading of KH2's PS3 build: createBG, drawCommandPage, drawCurrent,
// drawCurrentIcon, getTitleSeq and their tables): the background (clip 7: the book opening, then holding) at 28, 322
// of KH2's screen; n slots 22 dots apart, the last at 388 -- clip 16 each, clip 11 the last, the cursor's clip 9 moved
// 8 to the right with the cursor's icon on it (clip 37, a command's kind's) -- and the title (clip 17) 22 above the
// first. n is KH2's four (Attack, Magic, Items, Drive), or Days's rows if more: Days's rows in the first slots, their
// names in KH2's font (Days's own, or those a script's command menu gives them: the Lexicon's pages), the rest empty.
// Scaled so that the title's top and the background's foot meet the deck's box.
struct Book {
    static constexpr std::size_t kBackground = 7, kTitle = 17, kSlot = 16, kLastSlot = 11, kCurrent = 9, kCursor = 37;
    static constexpr int kKh2Slots = 4;
    static constexpr float kStep = 22, kCurrentShift = 8, kTextLeft = 6; // KH2 dots
    static constexpr std::size_t kBar = 1; // the reaction command's bar standing (comw's numbers)

    std::string key; // the archive and picture loaded
    std::optional<render::SequenceAsset> asset;
    std::vector<std::uint32_t> atlas_pixels;
    int atlas = -1;
    int slot_count = 0; // n
    float text_cell = 0; // the names' glyphs' cells, KH2 dots: as the reaction command's bar's (0.62 of its height)
    // KH2 dots from the background's place: its last frame (the book open), the title at its place, everything
    Bounds held, title_box, all;
    seq::SequencePlayer background, title, current, cursor;
    std::vector<seq::SequencePlayer> slots;
    int row = -2;
    unsigned last_vblank = 0;
    bool shown = false;
    int image = -1, image_w = 0, image_h = 0, face = -1, face_w = 0, face_h = 0;
    std::vector<std::u16string> face_names;
    int face_row = -2;
    KhdaysSequenceImage program{};
    unsigned program_vblank = ~0u; // the VBlank whose HUD made it

    // Whether it stands over the deck this VBlank
    bool live() const
    {
        return shown && program_vblank == khdays_link_frame();
    }

    // Whether it will stand over the deck this VBlank, asked before the HUD draws it (the text part, first): the
    // setting and HD textures on, an enemy's own pictures with their HD picture, the deck on its main rows, and its
    // pictures read (or to be read now)
    bool will_stand()
    {
        if (!khdays_hud_setting(KHDAYS_OPT_REACTION_COMMANDS) || !khdays_textures_hd())
            return false;
        const OwnImage own = own_image();
        return !own.archive.empty() && !own.picture.empty() && khdays_deck_main_row() >= 0 &&
               khdays_deck_main_rows() > 0 && (own.key() != key || asset.has_value());
    }

    static float slot_y(int k, int n)
    {
        return 88.0F - kStep * float(n) + kStep * float(k); // 410 - 22n + 22k of KH2's screen, from 322
    }
    static float title_y(int n)
    {
        return 66.0F - kStep * float(n); // 388 - 22n
    }

    static void add_clip(Bounds &b, const seq::SequenceData &d, std::size_t clip, float x, float y)
    {
        const int frames = seq::sequence_frame_count(d, clip);
        if (frames <= 0)
            throw std::runtime_error("a clip of the command menu has no frames");
        for (int t = 0; t < frames; ++t) {
            auto f = seq::evaluate_sequence(d, clip, float(t));
            for (auto &q : f.quads)
                for (auto &v : q.vertices)
                    v.x += x, v.y += y;
            b.add(f);
        }
    }

    // Its bounds for n slots
    void lay_out(int n)
    {
        const auto &d = asset->sequence;
        slot_count = n;
        held = title_box = all = {};
        held.add(seq::evaluate_sequence(d, kBackground, float(seq::sequence_frame_count(d, kBackground) - 1)));
        auto t = seq::evaluate_sequence(d, kTitle, float(seq::sequence_frame_count(d, kTitle) - 1));
        for (auto &q : t.quads)
            for (auto &v : q.vertices)
                v.y += title_y(n);
        title_box.add(t);
        add_clip(all, d, kBackground, 0, 0);
        add_clip(all, d, kTitle, 0, title_y(n));
        for (int k = 0; k < n; ++k) {
            add_clip(all, d, k + 1 == n ? kLastSlot : kSlot, 0, slot_y(k, n));
            add_clip(all, d, kCurrent, kCurrentShift, slot_y(k, n));
            add_clip(all, d, kCursor, kCurrentShift, slot_y(k, n));
        }
        if (!held.any || !title_box.any || held.y1 - title_box.y0 < 1)
            throw std::runtime_error("its background or title draws nothing");
        Bounds standing;
        standing.add(seq::evaluate_sequence(d, kBar, 0));
        text_cell = 0.62F * (standing.any ? standing.y1 - standing.y0 : kStep);
        slots.assign(std::size_t(n), {});
    }

    // This VBlank's menu in an enemy's own pictures, over the deck's element `deck`; false when there is none (Days's
    // deck then)
    bool update(const KhdaysUiElement &deck)
    {
        program = {};
        program_vblank = khdays_link_frame();
        const int deck_row = khdays_deck_main_row(), rows = khdays_deck_main_rows();
        const OwnImage own = own_image();
        if (!khdays_hud_setting(KHDAYS_OPT_REACTION_COMMANDS) || !khdays_textures_hd() || own.archive.empty() ||
            deck_row < 0 || rows <= 0) {
            shown = false;
            return false;
        }
        if (own.key() != key) {
            key = own.key();
            asset.reset();
            try {
                std::string found;
                if (load_own(own, "kh2:command_image:atlas", asset, atlas_pixels, atlas, found)) {
                    lay_out(std::max(kKh2Slots, rows));
                    khdays_log("ui: the command menu in the pictures of %s", found.c_str());
                } else
                    khdays_log("ui: %s's own command menu left out (no HD picture of it): Days's deck", own.archive.c_str());
            } catch (const std::exception &e) {
                asset.reset();
                khdays_log("ui: %s's own command menu left out (%s): Days's deck", own.archive.c_str(), e.what());
            }
            shown = false;
        }
        if (!asset)
            return false;
        try {
            const auto &d = asset->sequence;
            // a script's menu (Zexion's pages): its slots, all four, and its own cursor; else Days's rows and the deck's
            int menu_cursor = 0;
            const int menu_slots = khdays_kh2_enemies_menu_slots(&menu_cursor);
            const int named = menu_slots > 0 ? menu_slots : rows;
            const int on = menu_slots > 0 ? menu_cursor : deck_row;
            if (std::max(kKh2Slots, named) != slot_count)
                lay_out(std::max(kKh2Slots, named));
            const unsigned vblank = khdays_link_frame();
            if (!shown) { // KH2's menu starts again with the image set: the book opens
                shown = true;
                background.select(d, kBackground, true);
                title.select(d, kTitle, true);
                for (std::size_t k = 0; k < slots.size(); ++k)
                    slots[k].select(d, k + 1 == slots.size() ? kLastSlot : kSlot, true);
                cursor.select(d, kCursor, true);
                row = -2;
                last_vblank = vblank;
            }
            if (on != row) { // the cursor's slot opened again on its row
                row = on;
                current.select(d, kCurrent, true);
            }
            const float frames = khdays_link_paused() ? 0.0F : float(std::min(vblank - last_vblank, 4u));
            last_vblank = vblank;
            for (seq::SequencePlayer *p : {&background, &title, &current, &cursor})
                p->advance(d, frames);
            for (auto &p : slots)
                p.advance(d, frames);
            emit(deck, names(menu_slots, rows));
        } catch (const std::exception &e) {
            khdays_log("ui: the enemy's own command menu left out (%s): Days's deck", e.what());
            asset.reset();
            program = {};
            return false;
        }
        return true;
    }

    // The slots' names: a script's command menu's (`menu_slots` of them), else Days's rows' (UI/btl/<language>/cmd.s's
    // records 0, 1, 2 ...: Attack, Magic, Items)
    static std::vector<std::u16string> names(int menu_slots, int rows)
    {
        std::vector<std::u16string> out;
        for (int k = 0; k < (menu_slots > 0 ? menu_slots : rows); ++k) {
            unsigned short text[64];
            const int n = menu_slots > 0 ? khdays_kh2_enemies_menu_slot(k, text, 64) : 0;
            out.push_back(menu_slots > 0 ? std::u16string(text, text + n) : label(k));
        }
        return out;
    }

    // The slots' names (`now`) painted, `r` texels a DS dot, `s` DS dots a KH2 dot; its left, top in KH2 dots in `fx`,
    // `fy`
    bool make_face(std::vector<std::u16string> now, int r, float s, float &fx, float &fy)
    {
        const int n = slot_count, rows = std::max(1, int(now.size()));
        const float x0 = -kTextLeft, x1 = held.x1 + kCurrentShift;
        const float y0 = slot_y(0, n) - kStep / 2, y1 = slot_y(rows - 1, n) + kStep / 2;
        fx = x0;
        fy = y0;
        const int w = std::max(1, int(std::ceil((x1 - x0) * s * float(r))));
        const int h = std::max(1, int(std::ceil((y1 - y0) * s * float(r))));
        if (face >= 0 && face_w == w && face_h == h && face_names == now && face_row == row)
            return true;
        std::vector<std::uint32_t> out(std::size_t(w) * std::size_t(h), 0u);
        const bool kh2 = kh2_font.get();
        const float cell = text_cell; // KH2 dots
        for (int i = 0; i < int(now.size()); ++i) {
            const float left = kTextLeft + (i == row ? kCurrentShift : 0.0F) - x0;
            const float middle = slot_y(i, n) - y0;
            if (kh2)
                paint_kh2(now[std::size_t(i)], out, w, h, left * s * float(r), (middle - cell / 2) * s * float(r),
                          cell * s * float(r) / float(Kh2Font::kCellH));
            else if (const auto *f = label_font.get())
                paint_text(label_font, now[std::size_t(i)], out, w, h, r, left * s,
                           middle * s - double(f->cell_height) / 2);
        }
        if (face < 0 || face_w != w || face_h != h) {
            khdays_ui_image_put(face);
            face = khdays_ui_image_get(w, h);
        }
        std::uint32_t *texels = face >= 0 ? khdays_ui_image_begin(face) : nullptr;
        if (texels == nullptr)
            return false;
        std::copy(out.begin(), out.end(), texels);
        khdays_ui_image_end(face);
        face_w = w;
        face_h = h;
        face_names = std::move(now);
        face_row = row;
        return true;
    }

    void emit(const KhdaysUiElement &deck, std::vector<std::u16string> names_now)
    {
        const auto &d = asset->sequence;
        const int n = slot_count, r = khdays_ui_scale();
        // the title's top and the open book's foot on the deck's box; the book's left on its left
        const float s = deck.h / (held.y1 - title_box.y0);
        const float ox = deck.x - held.x0 * s, oy = deck.y - title_box.y0 * s;
        const float left = std::floor(all.x0 * s) - 2, top = std::floor(all.y0 * s) - 2;
        const float width = std::ceil(all.x1 * s) - left + 2, height = std::ceil(all.y1 * s) - top + 2;
        const int w = int(width) * r, h = int(height) * r;
        if (image < 0 || image_w != w || image_h != h) {
            khdays_ui_image_put(image);
            image = khdays_ui_image_get(w, h);
            image_w = w;
            image_h = h;
        }
        float fx = 0, fy = 0;
        if (image < 0 || !make_face(std::move(names_now), r, s, fx, fy))
            return;
        std::vector<seq::SequenceQuad> quads;
        const auto put = [&](const seq::SequencePlayer &p, float x, float y) {
            auto f = seq::evaluate_sequence(d, p.clip(), p.frame());
            for (auto &q : f.quads) {
                for (auto &v : q.vertices)
                    v.x += x, v.y += y;
                quads.push_back(q);
            }
        };
        put(background, 0, 0);
        put(title, 0, title_y(n));
        for (int k = 0; k < n; ++k)
            if (k != row)
                put(slots[std::size_t(k)], 0, slot_y(k, n));
        put(current, kCurrentShift, slot_y(row, n));
        const std::size_t under_cursor = quads.size();
        put(cursor, kCurrentShift, slot_y(row, n));
        if (quads.size() > KSI_MAX_QUADS)
            quads.resize(KSI_MAX_QUADS);
        auto *words = program.words;
        const auto number = [words](unsigned i, float v) { std::memcpy(words + i, &v, 4); };
        words[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(image));
        words[KSI_ATLAS] = unsigned(atlas);
        words[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
        words[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
        words[KSI_WIDTH] = unsigned(w);
        words[KSI_HEIGHT] = unsigned(h);
        number(KSI_VIEW_X, -left * float(r));
        number(KSI_VIEW_Y, -top * float(r));
        number(KSI_SCALE, s * float(r));
        words[KSI_CLEAR] = 1;
        render::encode_sequence_quads(program, quads);
        // the names over the slots, under the cursor's icon
        words[KSI_TEXT_INSERT] = unsigned(std::min(under_cursor, quads.size())) + 1;
        words[KSI_FACE] = unsigned(khdays_ui_image_replacement(face));
        words[KSI_HD_FACE] = ~0u;
        number(KSI_TEXT_SCALE, 1);
        number(KSI_FACE_X, (fx * s - left) * float(r));
        number(KSI_FACE_Y, (fy * s - top) * float(r));
        number(KSI_FACE_W, float(face_w));
        number(KSI_FACE_H, float(face_h));
        number(KSI_TEXT_CLIP_X, 0);
        number(KSI_TEXT_CLIP_Y, 0);
        number(KSI_TEXT_CLIP_W, float(w));
        number(KSI_TEXT_CLIP_H, float(h));
        number(KSI_TEXT_ALPHA, 1);
        KhdaysUiElement e = deck;
        e.slot = KES_DIRECT | KES_FINE | 3u; // in the deck's layer (BG3)
        e.x = ox + left;
        e.y = oy + top;
        e.w = width;
        e.h = height;
        e.image = image;
        khdays_ui_emit_extended(KHDAYS_UI_HUD, &e, &deck);
    }
};
Book book;

// ---- The mark over the enemy ----------------------------------------------------------------------------------------

// KH2's mark where an enemy offering a reaction command aims it: zz0field.2dd's "fiel" sequence 50 (the triangle coming, its
// three arrows and glow, the green beams closing into a triangle round it; then it stays, from its frame 71), 49 its
// fading, as KH2 plays them over Cerberus's head. A picture of the 3D layer (as the prizes' and the reticles' are: the
// 3D's own camera places it, an event sequence's too), at KH2's screen's size wherever it stands (KH2's 512 dots the
// DS's 256: half a DS dot a KH2 dot), in front of the 3D; its canvas only as large as the sequences draw.
struct Mark {
    static constexpr std::size_t kOn = 50, kOff = 49;
    static constexpr int kOffFrames = 10;
    static constexpr float kScale = 0.5F, kTexels = 4.0F;     // DS dots a KH2 dot; texels a DS dot
    static constexpr unsigned kProgram = KSI_3D_PROGRAMS - 1; // the frame package's 3D program for it
    bool tried = false;
    int language = -1;
    std::optional<render::SequenceAsset> asset;
    std::vector<std::uint32_t> atlas_pixels;
    int atlas = -1;
    Bounds all; // every frame of its two sequences (KH2 dots, round its place)
    struct Canvas {
        int token = -1;
        KhdaysSequenceImage commands{};
    };
    std::array<Canvas, 4> canvases; // a 3D list's build slot each (khdays_g3d_build_slot: its token's low 2 bits)
    seq::SequencePlayer player;
    bool on = false, leaving = false;
    int leave_frames = 0;
    unsigned last_vblank = 0;
    bool started = false;

    bool available()
    {
        if (!khdays_hud_setting(KHDAYS_OPT_REACTION_COMMANDS) || !khdays_textures_hd())
            return false;
        const int lang = khdays_settings_game_language();
        if (!tried || lang != language) {
            tried = true;
            language = lang;
            asset.reset();
            try {
                std::string found;
                if (load_piece("zz0field.2dd", 0, "fiel", "kh2:reaction_mark:atlas", asset, atlas_pixels, atlas,
                               found)) {
                    all = {};
                    for (const std::size_t clip : {kOn, kOff}) {
                        const int frames = seq::sequence_frame_count(asset->sequence, clip);
                        if (frames <= 0)
                            throw std::runtime_error("no reaction command mark in its sequences");
                        for (int f = 0; f < frames; ++f)
                            all.add(seq::evaluate_sequence(asset->sequence, clip, float(f)));
                    }
                    if (!all.any || all.x1 - all.x0 < 1 || all.y1 - all.y0 < 1)
                        throw std::runtime_error("the mark's sequence draws nothing");
                    khdays_log("ui: the reaction commands' mark from %s", found.c_str());
                }
            } catch (const std::exception &e) {
                asset.reset();
                khdays_log("ui: the reaction commands' mark left out (%s): the button's icon over the enemy", e.what());
            }
        }
        return asset.has_value();
    }

    // This VBlank: whether an enemy offers this game's own player a command
    void update(bool offered)
    {
        const unsigned vblank = khdays_link_frame();
        const unsigned frames = !started || khdays_link_paused() ? 0u : std::min(vblank - last_vblank, 4u);
        started = true;
        last_vblank = vblank;
        if (offered) {
            if (!on || leaving)
                player.select(asset->sequence, kOn, true);
            on = true;
            leaving = false;
        } else if (on && !leaving) {
            player.select(asset->sequence, kOff, true);
            leaving = true;
            leave_frames = 0;
        }
        if (!on)
            return;
        player.advance(asset->sequence, float(frames));
        if (leaving && (leave_frames += int(frames)) >= kOffFrames)
            on = leaving = false;
    }

    // While the game builds its 3D list: the mark round `spot` (clip space, the 3D's projection: the enemy's draw's
    // own)
    void draw(const float spot[4])
    {
        if (!on || !asset)
            return;
        const int slot = khdays_g3d_build_slot();
        Canvas &c = canvases[std::size_t(slot)];
        c.token = int(kProgram) * 4 + slot;
        auto f = seq::evaluate_sequence(asset->sequence, player.clip(), player.frame());
        if (f.quads.size() > KSI_MAX_QUADS)
            f.quads.resize(KSI_MAX_QUADS);
        auto *w = c.commands.words;
        std::memset(w, 0, sizeof(unsigned) * KSI_HEADER);
        const auto number = [w](unsigned i, float v) { std::memcpy(w + i, &v, 4); };
        w[KSI_OUTPUT] = unsigned(c.token);
        w[KSI_ATLAS] = unsigned(atlas);
        w[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
        w[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
        const float texels = kScale * kTexels; // texels a KH2 dot
        w[KSI_WIDTH] = unsigned(std::ceil((all.x1 - all.x0) * texels));
        w[KSI_HEIGHT] = unsigned(std::ceil((all.y1 - all.y0) * texels));
        number(KSI_VIEW_X, -all.x0 * texels);
        number(KSI_VIEW_Y, -all.y0 * texels);
        number(KSI_SCALE, texels);
        w[KSI_FACE] = ~0u;
        w[KSI_HD_FACE] = ~0u;
        w[KSI_CLEAR] = 1;
        render::encode_sequence_quads(c.commands, f.quads);
        // its 0, 0 on the spot, the canvas round it at KH2's size, in front of the 3D
        const float cx = spot[0], cy = spot[1], cw = spot[3];
        KhdaysG3dClipVertex v[4]{};
        constexpr int corners[]{0, 1, 3, 2}; // top-left, top-right, bottom-right, bottom-left
        for (int k = 0; k < 4; ++k) {
            const int corner = corners[k];
            auto &o = v[k];
            const float dx = ((corner & 1) ? all.x1 : all.x0) * kScale, dy = ((corner & 2) ? all.y1 : all.y0) * kScale;
            o.x = cx + dx / 128.0F * cw; // DS dots: the screen 256 x 192, its clip space 2 x 2, y up
            o.y = cy - dy / 96.0F * cw;
            o.z = -0.99F * cw;
            o.w = cw;
            o.r = o.g = o.b = 63;
            o.a = 1;
            o.s = (corner & 1) ? 128.0F : 0.0F;
            o.t = (corner & 2) ? 128.0F : 0.0F;
        }
        constexpr unsigned kPolygonId = 62;
        khdays_g3d_sequence_polygon(v, 4, 0xc0u | (31u << 16) | (kPolygonId << 24), atlas, unsigned(c.token));
    }

    // A frame of it that could not be played or drawn: the button's icon from now on
    void drop(const std::exception &e)
    {
        asset.reset();
        on = leaving = false;
        khdays_log("ui: the reaction commands' mark left out (%s): the button's icon over the enemy", e.what());
    }

    // The frame package's program: the canvas whose polygon the 3D list holds
    void commands(const KhdaysG3dList *list, KhdaysSequenceImage *out) const
    {
        *out = {};
        for (const auto &c : canvases) {
            if (c.token < 0)
                continue;
            for (int k = 0; k < list->npolys; ++k)
                if ((list->polys[k].flags & KHDAYS_G3D_SEQUENCE) && list->polys[k].pltt == unsigned(c.token)) {
                    *out = c.commands;
                    return;
                }
        }
    }
};
Mark mark_over;

std::u16string kh2_name()
{
    unsigned short name[64];
    const int n = khdays_kh2_enemies_offered_name(name, 64);
    return std::u16string(name, name + n);
}

} // namespace

extern "C" void khdays_ui_reaction_command_emit(void)
{
    const bool on = khdays_ui_mode(KHDAYS_UI_HUD) == KHDAYS_UI_ON;
    int held = 0;
    khdays_kh2_enemies_taken(&held);
    // the command now: a KH2 enemy's first, else Days's own while Ov022_UpdateCommandInput offers it
    int what = khdays_kh2_enemies_offered(nullptr);
    std::u16string name;
    if (what != 0) {
        name = kh2_name();
        what |= 0x10000;
    } else if (days.kind != 0 && days.usable && days.frame + 1 >= khdays_input_game_frames()) {
        name = label(days.kind + 5);
        what = 0x20000 | days.kind;
    }
    // in an enemy's own command menu's pictures while they draw the menu (Zexion's book), else KH2's general menu's
    Bar &shown = book.live() && own_bar.available() ? own_bar : bar;
    Bar &other = &shown == &bar ? own_bar : bar;
    other.program = {};
    if (on && shown.available()) {
        shown.update(what, name, held != 0);
    } else {
        shown.program = {};
        if (on && (what & 0x10000) != 0)
            emit_plain(name);
    }
    if (mark_over.available())
        try {
            mark_over.update(on && (what & 0x10000) != 0);
        } catch (const std::exception &e) {
            mark_over.drop(e);
        }
    if (!mark_over.available() && on && (what & 0x10000) != 0)
        emit_mark();
}

extern "C" void khdays_ui_reaction_mark_draw(const float spot[4])
{
    if (spot[3] <= 1e-4F)
        return; // behind the camera
    mark_spot = {(spot[0] / spot[3] + 1.0F) * 128.0F, (1.0F - spot[1] / spot[3]) * 96.0F, khdays_link_frame()};
    if (mark_over.available())
        try {
            mark_over.draw(spot);
        } catch (const std::exception &e) {
            mark_over.drop(e);
        }
}

extern "C" void khdays_ui_reaction_mark_commands(const KhdaysG3dList *list, KhdaysSequenceImage *out)
{
    mark_over.commands(list, out);
}

extern "C" void khdays_ui_reaction_command_commands(KhdaysSequenceImage *out)
{
    *out = own_bar.program.words[KSI_COUNT] != 0 ? own_bar.program : bar.program;
}

extern "C" int khdays_ui_command_image_compose(const KhdaysUiElement *deck)
{
    return deck != nullptr && book.update(*deck) ? 1 : 0;
}

extern "C" int khdays_ui_command_image_owns(void)
{
    return book.live() ? 1 : 0;
}

extern "C" int khdays_ui_command_image_standing(void)
{
    return book.will_stand() ? 1 : 0;
}

extern "C" void khdays_ui_command_image_commands(KhdaysSequenceImage *out)
{
    if (book.live())
        *out = book.program;
    else
        std::memset(out, 0, sizeof(*out));
}

extern "C" int khdays_reaction_talk_active(void)
{
    return bar.available();
}

// KHDAYS_TRACE_CONTROLS reports Days's commands the bar takes, as the controls' hooks report theirs
static bool tracing_controls()
{
    static const bool on = std::getenv("KHDAYS_TRACE_CONTROLS") != nullptr;
    return on;
}

extern "C" int khdays_reaction_talk_offer(int kind, int usable)
{
    if (kind == 0 || !bar.available())
        return 0;
    if (tracing_controls() &&
        (kind != days.kind || (usable != 0) != days.usable || days.frame + 1 < khdays_input_game_frames()))
        khdays_log("controls: the reaction command bar takes Days's command %d (%s)", kind,
                   usable != 0 ? "usable" : "not usable");
    days.kind = kind;
    days.usable = usable != 0;
    days.frame = khdays_input_game_frames();
    return 1;
}

extern "C" int khdays_reaction_talk_pressed(int pressed, int panel_open)
{
    const unsigned sample = khdays_input_game_frames();
    if (sample != days.sample) {
        days.sample = sample;
        days.x_before = days.x_now;
        days.x_now = (unsigned(pressed) & kKeyX) != 0;
    }
    // while a KH2 enemy offers a reaction command or holds the player, X is that command's (the shell takes it), not
    // the deck's cursor, its lists or a Days command; with "reaction button only" never theirs
    int held = 0;
    khdays_kh2_enemies_taken(&held);
    const KhdaysProfile *profile = khdays_devices_profile();
    const bool kh2 = held != 0 || khdays_kh2_enemies_offered(nullptr) != 0;
    const bool fresh = days.x_now && !days.x_before && panel_open == 0;
    days.pressed = fresh && !kh2 && days.kind != 0 && days.usable && days.frame == sample && bar.available();
    days.swallowed = fresh && !days.pressed && (kh2 || (profile != nullptr && profile->enh.reaction_button_only));
    days.x_before |= days.pressed || days.swallowed; // once
    if (days.pressed) {
        ++days.presses;
        if (tracing_controls())
            khdays_log("controls: the reaction button takes Days's command %d", days.kind);
    }
    return days.pressed || days.swallowed;
}

extern "C" int khdays_reaction_talk_commit(signed char *kind, unsigned short *arg)
{
    const bool pressed = days.pressed;
    days.pressed = false;
    if (days.swallowed) { // none
        days.swallowed = false;
        *kind = 7;
        *arg = 0;
    } else if (pressed) {
        *kind = 9;
        *arg = 1;
    } else if (*kind == 9 && days.kind != 0 && days.frame == khdays_input_game_frames()) {
        *arg = 2;
    }
    return pressed;
}
