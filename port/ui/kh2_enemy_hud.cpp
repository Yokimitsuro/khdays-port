// Read the native target gauge without changing its HP, selection, text, or tween state.
#include "../host/app/hud_settings.h"
#include "kh2_enemy_hud.h"
#include "kh2_hp_config.h"
#include "hud_gauge_tiles.h"
#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "bg.h"
#include "elements.h"
#include "parts.h"
#include "../runtime/core/window.h"
#include "../runtime/scripts/game_link.h"
#include "../runtime/mods/textures.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../host/screens/ui_text.h"
int khdays_settings_game_language(void);
int khdays_overlay_loaded(int);
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace {
namespace gauge = khdays::ui::kh2;
namespace render = khdays::render::kh2;
gauge::EnemyGaugeConfig config;
gauge::EnemyGaugeSample sample;
std::optional<render::SequenceAsset> asset;
std::vector<std::uint32_t> texels, pixels;
KhdaysSequenceImage program{};
bool tried = false, valid = false, reference = true;
unsigned previous_frame = 0, previous_context = 0;
int atlas = -1, image = -1, width = 0, height = 0, image_scale = 0;
constexpr unsigned context_address = 0x0207f618; // Ov002_InitGaugeScene.
unsigned word(unsigned at)
{
    return *static_cast<const unsigned *>(KHDAYS_W(at));
}
unsigned half(unsigned at)
{
    return *static_cast<const unsigned short *>(KHDAYS_W(at));
}
std::filesystem::path path(std::string_view s)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
void load()
{
    if (tried)
        return;
    tried = true;
    try {
        char name[128], found[2048];
        const char *language = khdays_language_code(khdays_settings_game_language());
        std::snprintf(name, sizeof(name), "layouts/gauge/%s/gauge.json", language && *language ? language : "en");
        if (!khdays_mods_find(name, found, sizeof(found)) &&
            !khdays_mods_find("layouts/gauge/en/gauge.json", found, sizeof(found)))
            return;
        config = gauge::load_enemy_gauge_config(path(found));
        if (!config.enabled)
            return;
        const auto folder = path(found).parent_path();
        asset = render::load_sequence_asset(folder / path(config.archive), config.entry, folder / path(config.texture));
        // Validate every clip, including a depleted tank and both sides of a partial initial bar.
        gauge::evaluate_enemy_gauge(asset->sequence, {250, 375, 375, 250, 0, 0}, config.style);
        gauge::evaluate_enemy_gauge(asset->sequence, {375, 375, 375, 250, 0, 100}, config.style);
        texels.resize(asset->texture.rgba.size() / 4);
        std::memcpy(texels.data(), asset->texture.rgba.data(), asset->texture.rgba.size());
        atlas = khdays_textures_add_info("kh2:enemy:atlas", asset->texture.width, asset->texture.height, texels.data(),
                                         KHDAYS_REPLACEMENT_NO_MIPS);
        if (atlas < 0)
            throw std::runtime_error("could not allocate enemy atlas");
        khdays_log("kh2 enemy: %s", found);
    } catch (const std::exception &e) {
        asset.reset();
        khdays_log("kh2 enemy: %s; keeping the Days gauge", e.what());
    }
}
} // namespace

extern "C" void khdays_ui_kh2_enemy_frame(unsigned frame)
{
    load();
    const bool was = valid;
    valid = false;
    if (khdays_hud_setting(KHDAYS_OPT_ENEMY_HP) && khdays_textures_hd() && asset &&
        khdays_ui_mode(KHDAYS_UI_HUD) == KHDAYS_UI_ON && khdays_overlay_loaded(2) && khdays_link_scene(nullptr) == 2) {
        const unsigned context = word(context_address);
        if (khdays_game_ram(context) && khdays_game_ram(context + 0x188) &&
            ((word(context + 0x48) & 1u) || context == previous_context)) {
            const int maximum = int(half(context + 0xec)), hp = std::min(int(half(context + 0xee)), maximum);
            const unsigned units = word(context + 0x44);
            if (maximum > 0 && units > 0 && units <= 0x7fffffffu / 250u) {
                const int capacity = int(units * 250u);
                const unsigned target = unsigned(hp) * 46u / unsigned(capacity);
                const bool reset = !was || context != previous_context || frame < previous_frame ||
                                   maximum != sample.max_hp || capacity != sample.bar_capacity ||
                                   (hp != sample.hp && !word(context + 0x144) && !word(context + 0x160) &&
                                    half(context + 0x12e) == target && half(context + 0x14a) == target);
                if (reset) {
                    sample = {hp, maximum, hp, capacity, 0, 100};
                } else {
                    const float ticks =
                        khdays_link_paused() ? 0 : float(frame - previous_frame) * config.frames_per_tick;
                    sample.frame += ticks;
                    sample.damage_frame = std::min(65536.0f, sample.damage_frame + ticks);
                    if (hp < sample.hp) {
                        sample.previous_hp = sample.hp;
                        sample.damage_frame = 0;
                    } else if (hp > sample.hp)
                        sample.previous_hp = hp;
                    sample.hp = hp;
                }
                valid = true;
                previous_context = context;
            }
        }
    }
    if (was || valid)
        khdays_ui_changed();
    previous_frame = frame;
}

extern "C" void khdays_ui_kh2_enemy_reference(int enabled)
{
    reference = enabled != 0;
    program.words[KSI_COUNT] = program.words[KSI_CLEAR] = program.words[KSI_ANCHORED] = 0;
}

extern "C" void khdays_ui_kh2_enemy_emit(void)
{
    if (!valid || !asset)
        return;
    struct Cell {
        int bg, x, y, px, py;
        unsigned tile;
    };
    std::vector<Cell> cells;
    int left = 256, top = 192, right = 0, bottom = 0;
    for (int bg = 0; bg < 4; ++bg) {
        KhdaysBgLayer layer;
        if (!khdays_bg_text_layer(0, bg, &layer) || layer.colors256)
            continue;
        for (int y = 0; y < 25; ++y)
            for (int x = 0; x < 33; ++x) {
                const int cx = ((layer.hofs >> 3) + x) % layer.wt, cy = ((layer.vofs >> 3) + y) % layer.ht;
                const unsigned entry = khdays_bg_map_entry(0, &layer, cx, cy), tile = entry & 1023u;
                // main.p2's enemy bar is tiles 47-54; its tank styles are 55-59. Names use another surface.
                if (tile < 47 || tile > 59 || !khdays_bg_cell_shows(0, &layer, entry))
                    continue;
                const int px = x * 8 - (layer.hofs & 7), py = y * 8 - (layer.vofs & 7);
                if (px >= 256 || py >= 192 || px + 8 <= 0 || py + 8 <= 0)
                    continue;
                const char *origin = khdays_bg_origin(0, layer.chars + tile * 32u);
                if (!origin || !std::strstr(origin, "UI/btl/") || !std::strstr(origin, "main.p2"))
                    continue;
                cells.push_back({bg, cx, cy, px, py, tile});
                if (tile == 54) {
                    left = px - 56;
                    top = py;
                    right = px + 8;
                    bottom = py + 8;
                }
            }
    }
    if (right <= left || bottom <= top)
        return;
    try {
        const auto frame = gauge::evaluate_enemy_gauge(asset->sequence, sample, config.style);
        if (frame.quads.empty() || frame.quads.size() > KSI_MAX_QUADS)
            throw std::runtime_error("enemy sequence exceeds the renderer's command capacity");
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        for (const auto &q : frame.quads)
            for (const auto &v : q.vertices) {
                x0 = std::min(x0, v.x * config.scale);
                x1 = std::max(x1, v.x * config.scale);
                y0 = std::min(y0, v.y * config.scale);
                y1 = std::max(y1, v.y * config.scale);
            }
        x0 = std::floor(x0);
        y0 = std::floor(y0);
        const float span_x = std::ceil(x1) - x0, span_y = std::ceil(y1) - y0;
        if (!std::isfinite(span_x) || !std::isfinite(span_y) || span_x <= 0 || span_y <= 0 || span_x > 2048 ||
            span_y > 512)
            throw std::runtime_error("enemy sequence has unsupported canvas dimensions");
        const int w = int(span_x), h = int(span_y), scale = khdays_ui_scale();
        if (image < 0 || width != w || height != h || image_scale != scale) {
            khdays_ui_image_put(image);
            width = w;
            height = h;
            image_scale = scale;
            image = khdays_ui_image_get(w * scale, h * scale);
        }
        if (image < 0)
            return;
        const render::SequenceView view{-x0 * scale, -y0 * scale, config.scale * scale};
        const float root_x = float(right - 2), root_y = float(top + bottom) * 0.5f;
        program.words[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(image));
        std::memcpy(program.words + KSI_ANCHOR_X, &root_x, 4);
        std::memcpy(program.words + KSI_ANCHOR_Y, &root_y, 4);
        program.words[KSI_ANCHORED] = 1;
        if (reference) {
            pixels.assign(std::size_t(w * scale) * std::size_t(h * scale), 0);
            render::draw_sequence(frame.quads, asset->texture, asset->logical_size[0], asset->logical_size[1], pixels,
                                  w * scale, h * scale, view);
            auto *out = khdays_ui_image_begin(image);
            if (!out)
                return;
            std::copy(pixels.begin(), pixels.end(), out);
            khdays_ui_image_end(image);
        } else {
            auto *words = program.words;
            auto number = [words](unsigned at, float v) { std::memcpy(words + at, &v, 4); };
            words[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(image));
            words[KSI_ATLAS] = unsigned(atlas);
            words[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
            words[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
            words[KSI_WIDTH] = unsigned(w * scale);
            words[KSI_HEIGHT] = unsigned(h * scale);
            number(KSI_VIEW_X, view.x);
            number(KSI_VIEW_Y, view.y);
            number(KSI_SCALE, view.scale);
            words[KSI_FACE] = words[KSI_HD_FACE] = 0xffffffffu;
            words[KSI_CLEAR] = 1;
            render::encode_sequence_quads(program, frame.quads);
        }
        KhdaysUiElement box{"hud.enemy",         0,    3, float(left), float(top), float(right - left),
                            float(bottom - top), image};
        auto e = box;
        e.slot = KES_DIRECT | KES_PICTURE | KES_FINE | KES_HUD | 3;
        e.x = root_x + x0 + config.offset_x;
        e.y = root_y + y0 + config.offset_y;
        e.w = float(w);
        e.h = float(h);
        khdays_ui_emit_extended(KHDAYS_UI_HUD, &e, &box);
        for (const auto &cell : cells)
            if (cell.tile >= 55 || (cell.py == top && cell.px >= left && cell.px < right))
                khdays_ui_left_out_cell(KHDAYS_UI_HUD, 0, cell.bg, cell.x, cell.y);
    } catch (const std::exception &e) {
        program.words[KSI_COUNT] = program.words[KSI_CLEAR] = 0;
        valid = false;
        asset.reset();
        khdays_log("kh2 enemy: %s; keeping the Days gauge", e.what());
    }
}
extern "C" void khdays_ui_kh2_enemy_commands(KhdaysSequenceImage *out)
{
    *out = program;
}

extern "C" int khdays_ui_kh2_enemy_ready(void)
{
    return valid && asset.has_value();
}

extern "C" int khdays_ui_kh2_enemy_sprite(const unsigned char *indices, int strip_width, int strip_height)
{
    if (!valid || !asset || previous_context != word(context_address))
        return 0;
    const unsigned strip = word(previous_context + 0x20);
    return khdays_game_ram(strip) && khdays_game_ram(strip + 0xbf) &&
           khdays_hud_gauge_strip(static_cast<const unsigned char *>(KHDAYS_W(strip)), 0xc0, indices, strip_width,
                                  strip_height);
}
