// A KH2 enemy's reaction command offered to the player (reaction_command.h), as KH2 shows it in two places: the X
// button's glyph (the HD fonts' text puts the pad's own icon in its place) and the command's KH2 name above the
// command deck, and the glyph alone over the enemy offering it (its aim point). The game's 8-dot font, white with a
// black edge, in the party's layer as the players' names are.
#include "reaction_command.h"

extern "C" {
#include "button_icons.h"
#include "elements.h"
#include "parts.h"
#include "../runtime/core/window.h"
}

#include "khdays/assets/font.h"
#include "khdays/vfs/filesystem.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// the KH2 enemies' (runtime/kh2_enemies/shell.cpp)
extern "C" int khdays_kh2_enemies_offered(int *text);
extern "C" int khdays_kh2_enemies_offered_name(unsigned short *out, int room);
extern "C" int khdays_kh2_enemies_offered_mark(float *x, float *y);

namespace {

constexpr char16_t kButtonX = 0x3322; // the game's fonts' X button
// the command deck's box on the upper screen (hud.c's "hud.deck": 0, 136 to 88, 192): the prompt just above it
constexpr int kDeckLeft = 4, kDeckTop = 136;

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
            std::fprintf(stderr, "ui: the reaction commands' font could not be read (%s)\n", e.what());
        }
    }
    return font ? &*font : nullptr;
}

struct Drawn {
    std::u16string text;
    int scale = 0;
    int image = -1;
    int w = 0, h = 0; // the font's dots
    int dot = 0;      // texels per font dot
};
Drawn drawn, mark;

// The icon of the player's input for the pad's top button (KH2's triangle: the DS X), the HD PACK's or Kenney's, as
// an image; -1 none (the game's glyph is drawn then)
struct Icon {
    int image = -1;
    int w = 0, h = 0;
    std::uint32_t signature = 0;
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
    for (int i = 0; i < w * h; ++i) {
        const std::uint8_t *c = rgba + 4 * i;
        texels[i] = std::uint32_t(c[0]) | std::uint32_t(c[1]) << 8 | std::uint32_t(c[2]) << 16 | std::uint32_t(c[3]) << 24;
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

constexpr float kIcon = 11.0f; // the icon's box above the deck (DS dots); over the enemy
constexpr float kMarkIcon = 14.0f;

// The text in white over a black edge one dot round it, a font dot a DS dot's texels
bool draw(Drawn &d, const std::u16string &text, int scale)
{
    if (d.image >= 0 && d.text == text && d.scale == scale) {
        return true;
    }
    const auto *f = the_font();
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

} // namespace

extern "C" void khdays_ui_reaction_command_emit(void)
{
    if (khdays_kh2_enemies_offered(nullptr) == 0 || khdays_ui_mode(KHDAYS_UI_HUD) != KHDAYS_UI_ON) {
        return;
    }
    // the button's icon (the game's glyph without one), then the command's name
    const float top = static_cast<float>(kDeckTop) - kIcon - 1.0f;
    const bool with_icon = emit_icon("hud.reaction_command_button", static_cast<float>(kDeckLeft), top, kIcon);
    std::u16string text;
    if (!with_icon) {
        text = std::u16string(1, kButtonX) + u' ';
    }
    unsigned short name[64];
    const int n = khdays_kh2_enemies_offered_name(name, 64);
    text.append(name, name + n);
    const int scale = khdays_ui_scale();
    if (!text.empty() && draw(drawn, text, scale)) {
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
    float x = 0.0f, y = 0.0f;
    if (!khdays_kh2_enemies_offered_mark(&x, &y)) {
        return;
    }
    if (emit_icon("hud.reaction_command_mark", x - kMarkIcon / 2.0f, y - kMarkIcon - 2.0f, kMarkIcon)) {
        return;
    }
    if (draw(mark, std::u16string(1, kButtonX), scale)) {
        KhdaysUiElement e;
        e.id = "hud.reaction_command_mark";
        e.screen = 0;
        e.slot = KES_DIRECT | 3u;
        e.w = static_cast<float>(mark.w * mark.dot) / static_cast<float>(scale);
        e.h = static_cast<float>(mark.h * mark.dot) / static_cast<float>(scale);
        e.x = x - e.w / 2.0f;
        e.y = y - e.h - 2.0f; // just above its aim point
        e.image = mark.image;
        khdays_ui_emit(KHDAYS_UI_HUD, &e);
    }
}
