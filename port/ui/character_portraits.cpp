// The mods' characters' own portraits: a HUD slot (the local player's, or an ally's in a room) whose seat plays a
// mod's character with a `portrait` shows that picture where the game's portrait cells are, fitted into them, and
// moved and sized as the KH2 gauge moves the portrait when the HD pieces draw one.
#include "character_portraits.h"
#include "kh2_hp_hud.h"
#include "kh2_portrait.h"
extern "C" {
#include "bg.h"
#include "elements.h"
#include "parts.h"
#include "../host/render/png_io.h"
#include "../runtime/characters/characters.h"
#include "../runtime/characters/forms.h"
#include "../runtime/core/session_log.h"
#include "../runtime/scripts/game_link.h"
}
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {
struct Picture {
    std::vector<std::uint8_t> rgba; // straight alpha, rows top to bottom
    int width = 0, height = 0;
};
struct Slot {
    const Picture *picture = nullptr;
    const Picture *fitted_from = nullptr;
    std::vector<std::uint32_t> fitted; // the picture at the image's size, alpha 1
    int image = -1, width = 0, height = 0;
    float alpha = -1; // the image's texels' alpha as last written
    bool shown = false;
    KhdaysUiElement element{}, box{};
    std::vector<std::array<int, 2>> cells;
};
std::map<std::string, std::optional<Picture>> pictures; // by path; empty: it cannot be read (told once)
std::array<Slot, 4> slots;

const Picture *picture(const KhdaysCharacter *c, bool ally)
{
    const char *relative = ally && c->portrait_ally != nullptr ? c->portrait_ally : c->portrait;
    char path[1024];
    if (relative == nullptr || !khdays_characters_file(c, relative, path, int(sizeof(path))))
        return nullptr;
    auto found = pictures.find(path);
    if (found == pictures.end()) {
        int width = 0, height = 0;
        const char *error = nullptr;
        std::uint8_t *rgba = khdays_png_read(path, &width, &height, &error);
        std::optional<Picture> read;
        if (rgba != nullptr && width > 0 && height > 0) {
            read.emplace();
            read->rgba.assign(rgba, rgba + std::size_t(width) * std::size_t(height) * 4u);
            read->width = width;
            read->height = height;
        } else {
            khdays_log("characters: %s/%s: %s cannot be read as a PNG (%s): the base's portrait is shown",
                       c->identity, relative, path, error != nullptr ? error : "?");
        }
        khdays_png_free(rgba);
        found = pictures.emplace(path, std::move(read)).first;
    }
    return found->second ? &*found->second : nullptr;
}

// The picture fitted into w x h texels, its shape kept and in the middle: each texel the average of the picture's
// pixels it covers, or, where the picture is smaller, its value between them. Straight RGBA, red in bits 0-7.
void fit(const Picture &p, std::vector<std::uint32_t> &out, int w, int h)
{
    const double k = std::min(double(w) / p.width, double(h) / p.height); // texels a picture's pixel
    const double ox = (w - p.width * k) / 2, oy = (h - p.height * k) / 2;
    auto pixel = [&p](int x, int y) { return &p.rgba[(std::size_t(y) * std::size_t(p.width) + std::size_t(x)) * 4u]; };
    auto pack = [](const double sum[4], double area) {
        if (sum[3] <= 0 || area <= 0)
            return std::uint32_t(0);
        std::uint32_t t = 0;
        for (int c = 0; c < 3; ++c)
            t |= std::uint32_t(std::clamp(std::lround(sum[c] / sum[3]), 0L, 255L)) << (8 * c);
        return t | std::uint32_t(std::clamp(std::lround(255.0 * sum[3] / area), 0L, 255L)) << 24;
    };
    out.assign(std::size_t(w) * std::size_t(h), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double sum[4] = {0, 0, 0, 0}, area = 1;
            if (k >= 1) {
                // between the four pixels around the texel's middle (outside the picture: nothing)
                const double sx = (x + 0.5 - ox) / k - 0.5, sy = (y + 0.5 - oy) / k - 0.5;
                if (sx < -0.5 || sy < -0.5 || sx > p.width - 0.5 || sy > p.height - 0.5)
                    continue;
                const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
                const double fx = sx - x0, fy = sy - y0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const std::uint8_t *s =
                            pixel(std::clamp(x0 + i, 0, p.width - 1), std::clamp(y0 + j, 0, p.height - 1));
                        const double weight = (i ? fx : 1 - fx) * (j ? fy : 1 - fy) * s[3] / 255.0;
                        for (int c = 0; c < 3; ++c)
                            sum[c] += weight * s[c];
                        sum[3] += weight;
                    }
            } else {
                const double sx0 = (x - ox) / k, sx1 = (x + 1 - ox) / k, sy0 = (y - oy) / k, sy1 = (y + 1 - oy) / k;
                area = (sx1 - sx0) * (sy1 - sy0);
                for (int sy = std::max(0, int(std::floor(sy0))); sy < std::min(p.height, int(std::ceil(sy1))); ++sy) {
                    const double hy = std::min(sy1, sy + 1.0) - std::max(sy0, double(sy));
                    for (int sx = std::max(0, int(std::floor(sx0))); sx < std::min(p.width, int(std::ceil(sx1)));
                         ++sx) {
                        const std::uint8_t *s = pixel(sx, sy);
                        const double weight = (std::min(sx1, sx + 1.0) - std::max(sx0, double(sx))) * hy * s[3] / 255.0;
                        for (int c = 0; c < 3; ++c)
                            sum[c] += weight * s[c];
                        sum[3] += weight;
                    }
                }
            }
            out[std::size_t(y) * std::size_t(w) + std::size_t(x)] = pack(sum, area);
        }
}

bool draw(Slot &slot, int index, const KhdaysBgLayer &layer)
{
    const int fx = layer.hofs & 7, fy = layer.vofs & 7;
    int x0 = 256, y0 = 192, x1 = 0, y1 = 0;
    for (auto cell : slot.cells) {
        x0 = std::min(x0, cell[0] * 8 - fx);
        y0 = std::min(y0, cell[1] * 8 - fy);
        x1 = std::max(x1, cell[0] * 8 - fx + 8);
        y1 = std::max(y1, cell[1] * 8 - fy + 8);
    }
    slot.box = {"hud.portrait", 0, 3, float(x0), float(y0), float(x1 - x0), float(y1 - y0), -1};
    KhdaysHudParent parent{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 1, 1, 1};
    khdays_ui_kh2_hp_parent(index, &parent);
    const float w = float(x1 - x0) * parent.scale_x, h = float(y1 - y0) * parent.scale_y;
    const int scale = khdays_ui_scale();
    const int tw = int(std::lround(w * float(scale))), th = int(std::lround(h * float(scale)));
    if (tw <= 0 || th <= 0)
        return false;
    if (slot.image < 0 || slot.width != tw || slot.height != th) {
        khdays_ui_image_put(slot.image);
        slot.image = khdays_ui_image_get(tw, th);
        slot.width = tw;
        slot.height = th;
        slot.fitted_from = nullptr;
    }
    if (slot.image < 0)
        return false;
    if (slot.fitted_from != slot.picture) {
        fit(*slot.picture, slot.fitted, tw, th);
        slot.fitted_from = slot.picture;
        slot.alpha = -1;
    }
    if (slot.alpha != parent.alpha) {
        std::uint32_t *texels = khdays_ui_image_begin(slot.image);
        if (texels == nullptr)
            return false;
        const float alpha = std::clamp(parent.alpha, 0.0f, 1.0f);
        for (std::size_t i = 0; i < slot.fitted.size(); ++i) {
            const std::uint32_t t = slot.fitted[i];
            texels[i] = (t & 0xffffffu) | std::uint32_t(std::lround(float(t >> 24) * alpha)) << 24;
        }
        khdays_ui_image_end(slot.image);
        slot.alpha = parent.alpha;
    }
    slot.element = slot.box;
    slot.element.slot = KES_DIRECT | KES_PICTURE | KES_HUD | 3;
    slot.element.x = parent.x - w * 0.5f;
    slot.element.y = parent.y - h * 0.5f;
    slot.element.w = w;
    slot.element.h = h;
    slot.element.image = slot.image;
    return true;
}
} // namespace

extern "C" void khdays_ui_character_portraits_prepare(void)
{
    bool any = false;
    for (int index = 0; index < 4; ++index) {
        auto &slot = slots[std::size_t(index)];
        slot.shown = false;
        slot.picture = nullptr;
        slot.cells.clear();
        const int seat = khdays_ui_mode(KHDAYS_UI_HUD) == KHDAYS_UI_ON ? khdays_link_hud_seat(index) : -1;
        const KhdaysCharacter *c = seat >= 0 ? khdays_characters_seat_form_now(seat) : nullptr;
        slot.picture = c != nullptr ? picture(c, index != 0) : nullptr;
        any |= slot.picture != nullptr;
    }
    KhdaysBgLayer layer;
    if (!any || !khdays_bg_text_layer(0, 3, &layer) || layer.colors256)
        return;
    // the cells Ov002_LoadPanelSlots copies each slot's portrait into
    for (int y = 0; y < 25; ++y)
        for (int x = 0; x < 33; ++x) {
            const int cx = ((layer.hofs >> 3) + x) % layer.wt, cy = ((layer.vofs >> 3) + y) % layer.ht;
            const unsigned entry = khdays_bg_map_entry(0, &layer, cx, cy);
            const int index = khdays::ui::kh2::portrait_tile_slot((entry & 0x3ffu) * 32u);
            if (index < 0 || slots[std::size_t(index)].picture == nullptr || khdays_ui_cell_taken(0, 3, cx, cy))
                continue;
            const char *origin = khdays_bg_origin(0, layer.chars + (entry & 0x3ffu) * 32u);
            if (origin && std::strstr(origin, "UI/btl/") && std::strstr(origin, "main.p2"))
                slots[std::size_t(index)].cells.push_back({x, y});
        }
    for (int index = 0; index < 4; ++index) {
        auto &slot = slots[std::size_t(index)];
        if (slot.picture == nullptr || slot.cells.empty() || !draw(slot, index, layer))
            continue;
        slot.shown = true;
        for (auto cell : slot.cells)
            khdays_ui_left_out_cell(KHDAYS_UI_HUD, 0, 3, ((layer.hofs >> 3) + cell[0]) % layer.wt,
                                    ((layer.vofs >> 3) + cell[1]) % layer.ht);
    }
}

extern "C" void khdays_ui_character_portraits_emit(void)
{
    for (const auto &slot : slots)
        if (slot.shown)
            khdays_ui_emit_extended(KHDAYS_UI_HUD, &slot.element, &slot.box);
}
