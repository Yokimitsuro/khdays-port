// The Days adapter: live HP, the original gauge's tiles, and the KH2 image behind the face.
#include "../host/app/hud_settings.h"
#include "kh2_hp_hud.h"
#include "kh2_hp_gauge.h"
#include "kh2_hp_config.h"
#include "kh2_party_hud.h"
#include "hud_gauge_tiles.h"
#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "parts.h"
#include "../runtime/core/window.h"
#include "../runtime/core/session_log.h"
#include "../runtime/scripts/game_link.h"
#include "../runtime/mods/textures.h"
#include "../runtime/core/rom.h"
#include "../host/screens/ui_text.h"
int khdays_settings_game_language(void);
int khdays_overlay_loaded(int);
unsigned Session_GetLocalPlayerIndex(void);
int Ov002_GetPanelWord0220Idx(int);
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace {
namespace gauge = khdays::ui::kh2;
namespace render = khdays::render::kh2;
std::optional<render::SequenceAsset> asset;
bool tried, valid;
unsigned last_frame;
gauge::HpGaugeSample sample;
int gauge_slot = -1, image = -1, image_width, image_height, image_scale;
gauge::HpGaugeConfig config;
bool reference_draw = true;
int atlas_image = -1;
std::vector<std::uint32_t> atlas_texels;
KhdaysSequenceImage program{};
constexpr unsigned gauge_context = 0x0207f618;            // Ov002_InitGaugeScene, aHandles[4] first
constexpr unsigned offsets[]{0x800, 0x980, 0x9c0, 0xa00}; // Ov002_InitGauges / TickScene
constexpr unsigned sizes[]{320, 64, 64, 64};
constexpr int image_right = 272; // room beyond the portrait's 256-dot edge for the circle

unsigned read_word(unsigned ref)
{
    return *static_cast<const unsigned *>(KHDAYS_W(ref));
}

float limit_marker_hp(int hp, int maximum)
{
    const unsigned context = read_word(gauge_context);
    if (!read_word(context + 0x178))
        return read_word(context + 0x2c) ? float(hp) : -1;
    KhdaysLinkLimitClock clock{};
    if (!khdays_link_player_limit_clock(&clock))
        return -1;
    const int total = int(read_word(context + 0x184));
    if (total <= 0)
        return float(hp);
    const int elapsed = std::clamp(clock.elapsed, 0, total);
    const int outward = read_word(context + 0x17c) ? std::clamp(clock.outward, 0, total) : total;
    if (elapsed < outward)
        return std::lerp(float(std::clamp(clock.start_hp, 0, maximum)), float(maximum),
                         float(elapsed) / float(outward));
    if (outward == total)
        return float(maximum);
    // Land on the turn at the first native update crossing it, even between fixed-point ticks.
    if (elapsed < total && elapsed - outward < clock.step)
        return float(maximum);
    return std::lerp(float(maximum), float(hp), float(elapsed - outward) / float(total - outward));
}

std::filesystem::path utf8_path(std::string_view value)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}

void load()
{
    if (tried)
        return;
    tried = true;
    const char *enabled = std::getenv("KHDAYS_KH2_HUD");
    if (enabled && std::string(enabled) == "0")
        return;
    try {
        char relative[128], path[2048];
        const char *language = khdays_language_code(khdays_settings_game_language());
        std::snprintf(relative, sizeof(relative), "layouts/gauge/%s/gauge.json",
                      language && *language ? language : "en");
        if (!khdays_mods_find(relative, path, sizeof(path)) &&
            !khdays_mods_find("layouts/gauge/en/gauge.json", path, sizeof(path)))
            return;
        config = gauge::load_hp_gauge_config(utf8_path(path));
        if (!config.enabled)
            return;
        const auto dir = utf8_path(path).parent_path();
        asset =
            render::load_sequence_asset(dir / utf8_path(config.archive), config.entry, dir / utf8_path(config.texture));
        const auto &s = config.style;
        for (auto clip : {s.center[0], s.center[1], s.center[2], s.damage[0], s.damage[1], s.damage[2], s.intro, s.fade,
                          s.circle[0], s.circle[1], s.circle[2], s.bar[0], s.bar[1], s.bar[2], s.circle_damage,
                          s.bar_damage, s.circle_fill, s.bar_fill, s.cap})
            if (clip >= asset->sequence.clips.size())
                throw std::runtime_error("gauge JSON: animation index outside this archive");
        if (s.center_override >= int(asset->sequence.clips.size()))
            throw std::runtime_error("gauge JSON: center_override outside this archive");
        if (s.limit_circle >= int(asset->sequence.clips.size()) || s.limit_bar >= int(asset->sequence.clips.size()) ||
            (s.limit_circle < 0) != (s.limit_bar < 0))
            throw std::runtime_error("gauge JSON: limit clips must both be present or both disabled");
        if (s.limit_marker >= int(asset->sequence.clips.size()))
            throw std::runtime_error("gauge JSON: limit_marker outside this archive");
        for (const auto &q : gauge::evaluate_hp_gauge(asset->sequence, {}, config.style).quads)
            if (q.blend != 0)
                throw std::runtime_error("the gauge requires destination-dependent blending");
        atlas_texels.resize(asset->texture.rgba.size() / 4);
        std::memcpy(atlas_texels.data(), asset->texture.rgba.data(), asset->texture.rgba.size());
        atlas_image = khdays_textures_add_info("kh2:gaug:atlas", asset->texture.width, asset->texture.height,
                                               atlas_texels.data(), KHDAYS_REPLACEMENT_NO_MIPS);
        if (atlas_image < 0)
            throw std::runtime_error("could not register the gauge atlas");
        std::fprintf(stderr, "kh2 HUD: loaded %s\n", path);
        khdays_log("kh2 HUD: loaded %s", path);
    } catch (const std::exception &e) {
        asset.reset();
        std::fprintf(stderr, "kh2 HUD: %s; keeping the Days gauge\n", e.what());
        khdays_log("kh2 HUD: %s; keeping the Days gauge", e.what());
    }
}

} // namespace

extern "C" void khdays_ui_kh2_hp_frame(unsigned frame)
{
    load();
    int hp = 0, maximum = 0;
    const bool was = valid;
    valid = khdays_hud_setting(KHDAYS_OPT_PARTY_HP) && khdays_textures_hd() && asset &&
            khdays_ui_mode(KHDAYS_UI_HUD) == KHDAYS_UI_ON && khdays_overlay_loaded(2) &&
            khdays_link_scene(nullptr) == 2 && khdays_game_ram(read_word(gauge_context)) &&
            khdays_link_player_hp(&hp, &maximum) && maximum > 0;
    if (!valid) {
        gauge_slot = -1;
        if (was)
            khdays_ui_changed();
        last_frame = frame;
        return;
    }
    gauge_slot = Ov002_GetPanelWord0220Idx(int(Session_GetLocalPlayerIndex()));
    valid = gauge_slot >= 0 && gauge_slot < 4;
    if (!valid) {
        if (was)
            khdays_ui_changed();
        return;
    }
    const int limit = std::max(0, khdays_link_player_limit_hp());
    const float marker = limit_marker_hp(hp, maximum);
    const bool state_changed = !was || hp != int(sample.hp) || maximum != int(sample.max_hp) ||
                               limit != int(sample.limit_hp) || marker != sample.limit_marker_hp;
    if (!was || frame < last_frame || maximum != int(sample.max_hp)) {
        sample = {};
        sample.entering = !was;
        sample.hp = sample.previous_hp = float(hp);
        sample.max_hp = float(maximum);
    } else {
        const float elapsed = khdays_link_paused() ? 0 : float(frame - last_frame);
        sample.frame += elapsed;
        sample.damage_frame = std::min(100.0f, sample.damage_frame + elapsed);
        if (hp < int(sample.hp)) {
            sample.previous_hp = sample.damage_frame < 100 ? std::max(sample.previous_hp, sample.hp) : sample.hp;
            sample.damage_frame = 0;
            sample.reaction = gauge::HpGaugeReaction::Small;
        } else if (hp > int(sample.hp))
            sample.previous_hp = float(hp);
        sample.hp = float(hp);
    }
    sample.limit_hp = float(limit);
    sample.limit_marker_hp = marker;
    sample.status = hp == 0                       ? gauge::HpGaugeStatus::Dead
                    : khdays_link_player_low_hp() ? gauge::HpGaugeStatus::Alert
                                                  : gauge::HpGaugeStatus::Normal;
    last_frame = frame;
    if (!khdays_link_paused() || state_changed)
        khdays_ui_changed();
}

extern "C" int khdays_ui_kh2_hp_ready(void)
{
    return valid && asset.has_value();
}

extern "C" int khdays_ui_kh2_hp_parent(int slot, KhdaysHudParent *out)
{
    if (slot > 0 && khdays_ui_kh2_party_parent(slot, out))
        return 1;
    if (slot != gauge_slot || !khdays_ui_kh2_hp_ready())
        return 0;
    try {
        const auto frame = gauge::evaluate_hp_gauge(asset->sequence, sample, config.style);
        if (!frame.active)
            return 0;
        const auto &p = *frame.active;
        out->x = 230 + config.offset_x + config.portrait_offset_x +
                 (float(p.x) - config.style.source_origin_x + config.style.center_offset[0]) * config.scale;
        out->y = 160 + config.offset_y + config.portrait_offset_y +
                 (float(p.y) - config.style.source_origin_y + config.style.center_offset[1]) * config.scale;
        out->scale_x = p.scale_x;
        out->scale_y = p.scale_y;
        out->alpha = std::clamp(float(p.color >> 24) / 128, 0.0f, 1.0f);
        return 1;
    } catch (const std::exception &e) {
        khdays_log("kh2 HUD: %s; keeping the Days gauge", e.what());
        valid = false;
        asset.reset();
        program = {};
        khdays_ui_changed();
        return 0;
    }
}

extern "C" void khdays_ui_kh2_hp_reference(int reference)
{
    reference_draw = reference != 0;
    program.words[KSI_COUNT] = 0;
}

extern "C" void khdays_ui_kh2_hp_commands(KhdaysSequenceImage *out)
{
    *out = program;
}

extern "C" int khdays_ui_kh2_hp_cell(int screen, int bg, const KhdaysBgLayer *layer, unsigned entry)
{
    if (!khdays_ui_kh2_hp_ready() || screen != 0 || bg != 3)
        return 0;
    const unsigned address = (entry & 0x3ffu) * (layer->colors256 ? 64u : 32u);
    return address >= offsets[gauge_slot] && address < offsets[gauge_slot] + sizes[gauge_slot];
}

extern "C" void khdays_ui_kh2_hp_prepare(void)
{
    if (!khdays_ui_kh2_hp_ready())
        return;
    KhdaysBgLayer layer;
    if (!khdays_bg_text_layer(0, 3, &layer))
        return;
    for (int y = 0; y < 25; ++y)
        for (int x = 0; x < 33; ++x) {
            const int cx = ((layer.hofs >> 3) + x) % layer.wt, cy = ((layer.vofs >> 3) + y) % layer.ht;
            const unsigned entry = khdays_bg_map_entry(0, &layer, cx, cy);
            if (!khdays_ui_kh2_hp_cell(0, 3, &layer, entry))
                continue;
            const char *origin = khdays_bg_origin(0, layer.chars + (entry & 1023u) * (layer.colors256 ? 64u : 32u));
            if (origin && std::strstr(origin, "UI/btl/") && std::strstr(origin, "main.p2"))
                khdays_ui_left_out_cell(KHDAYS_UI_HUD, 0, 3, cx, cy);
        }
}

extern "C" int khdays_ui_kh2_hp_sprite(const unsigned char *pixels, int width, int height)
{
    if (height != 8 || width < 8 || width > 64 || (width & 7))
        return 0;
    // Match a possible OBJ copy against the live gauge's 4-bit tiles.
    const unsigned context = read_word(gauge_context);
    if (!khdays_game_ram(context))
        return 0;
    for (int slot = 0; slot < 4; ++slot) {
        if (!(slot == gauge_slot && khdays_ui_kh2_hp_ready()) && !khdays_ui_kh2_party_ready(slot))
            continue;
        const unsigned handle = read_word(context + 4u * unsigned(slot));
        if (!khdays_game_ram(handle))
            continue;
        const auto *tiles = static_cast<const unsigned char *>(KHDAYS_W(handle));
        if (khdays_hud_gauge_strip(tiles, sizes[slot], pixels, width, height))
            return 1;
    }
    return 0;
}

extern "C" int khdays_ui_kh2_hp_compose(const KhdaysUiElement *face, const KhdaysUiElement *hd)
{
    if (!khdays_ui_kh2_hp_ready())
        return 0;
    try {
        const int scale = khdays_ui_scale();
        float extra = std::max(std::abs(config.portrait_offset_x), std::abs(config.portrait_offset_y));
        for (const auto &xy : {config.style.center_offset, config.style.circle_offset, config.style.bar_offset,
                               config.style.cap_offset, config.style.limit_marker_offset})
            extra = std::max(extra, config.scale * std::max(std::abs(xy[0]), std::abs(xy[1])));
        const int padding = int(std::ceil(config.canvas_padding + extra));
        const int base_right = std::max(image_right, int(std::ceil(230 + 48 * config.scale)));
        const int base_left = std::min(160, int(std::floor(230 - 48 * config.scale)));
        const int base_top = std::min(128, int(std::floor(160 - 48 * config.scale)));
        const int base_bottom = std::max(192, int(std::ceil(160 + 48 * config.scale)));
        const int right = base_right + padding + int(std::ceil(std::max(0.0f, config.offset_x)));
        const int width =
            std::clamp(int(std::ceil(gauge::hp_gauge_extent(sample.max_hp).bar_length * config.scale + 80)) +
                           2 * padding + int(std::ceil(std::abs(config.offset_x))),
                       base_right - base_left + 2 * padding, 1024);
        const int left = right - width;
        const int top = base_top - padding + int(std::floor(std::min(0.0f, config.offset_y)));
        const int height = base_bottom - base_top + 2 * padding + int(std::ceil(std::abs(config.offset_y)));
        float face_x = face->x + config.offset_x + config.portrait_offset_x - float(left);
        float face_y = face->y + config.offset_y + config.portrait_offset_y - float(top);
        const render::SequenceView view{(230 + config.offset_x - float(left)) * scale,
                                        (160 + config.offset_y - float(top)) * scale, config.scale * scale};
        if (image < 0 || width != image_width || height != image_height || scale != image_scale) {
            khdays_ui_image_put(image);
            image = khdays_ui_image_get(width * scale, height * scale);
            image_width = width;
            image_scale = scale;
            image_height = height;
        }
        if (image < 0)
            return 0;
        const auto commands = gauge::evaluate_hp_gauge(asset->sequence, sample, config.style);
        face_x +=
            (float(commands.active->x) - config.style.source_origin_x + config.style.center_offset[0]) * config.scale;
        face_y +=
            (float(commands.active->y) - config.style.source_origin_y + config.style.center_offset[1]) * config.scale;
        if (commands.quads.size() > KSI_MAX_QUADS)
            throw std::runtime_error("too many gauge quads for the native image");
        if (!reference_draw) {
            auto *w = program.words;
            auto number = [w](unsigned at, float value) { std::memcpy(w + at, &value, 4); };
            const int face_image = khdays_ui_element_direct(face);
            const int hd_image = hd ? khdays_ui_element_direct(hd) : -1;
            if (atlas_image < 0 || face_image < 0)
                throw std::runtime_error("could not allocate native gauge images");
            w[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(image));
            w[KSI_ATLAS] = unsigned(atlas_image);
            w[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
            w[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
            w[KSI_WIDTH] = unsigned(width * scale);
            w[KSI_HEIGHT] = unsigned(height * scale);
            number(KSI_VIEW_X, view.x);
            number(KSI_VIEW_Y, view.y);
            number(KSI_SCALE, view.scale);
            w[KSI_FACE] = unsigned(face_image);
            w[KSI_HD_FACE] = unsigned(hd_image);
            number(KSI_FACE_X, face_x * scale);
            number(KSI_FACE_Y, face_y * scale);
            number(KSI_FACE_W, face->w * scale);
            number(KSI_FACE_H, std::min(face->h, 183 - face->y) * scale);
            render::encode_sequence_quads(program, commands.quads);
        }
        if (reference_draw) {
            std::vector<std::uint32_t> rgba(std::size_t(width * scale) * std::size_t(height * scale));
            render::draw_sequence(commands.quads, asset->texture, asset->logical_size[0], asset->logical_size[1], rgba,
                                  width * scale, height * scale, view);
            for (int y = 0; y < int(face->h) * scale; ++y)
                for (int x = 0; x < int(face->w) * scale; ++x) {
                    // The static HP frame and label surround the separately uploaded fill tiles.
                    if (int(face->y) * scale + y >= 183 * scale)
                        continue;
                    const int dx = x + int(std::round(face_x * scale)), dy = y + int(std::round(face_y * scale));
                    if (dx < 0 || dx >= width * scale || dy < 0 || dy >= height * scale)
                        continue;
                    auto &dst = rgba[std::size_t(dy) * std::size_t(width * scale) + std::size_t(dx)];
                    dst = render::source_over(dst, khdays_ui_element_rgba(face, x, y));
                    if (hd)
                        dst = render::source_over(dst, khdays_ui_element_rgba(hd, x, y));
                }
            auto *out = khdays_ui_image_begin(image);
            if (!out)
                return 0;
            std::copy(rgba.begin(), rgba.end(), out);
            khdays_ui_image_end(image);
        }
        KhdaysUiElement e = *face;
        e.slot = KES_DIRECT | KES_PICTURE | KES_HUD | 3;
        e.x = float(left);
        e.y = float(top);
        e.w = float(width);
        e.h = float(height);
        e.image = image;
        khdays_ui_emit_extended(KHDAYS_UI_HUD, &e, face);
        return 1;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "kh2 HUD: %s; disabling the replacement\n", e.what());
        valid = false;
        asset.reset();
        program = {};
        khdays_ui_changed();
        return 0;
    }
}
