// The same native gauge evaluator, with small atlas art and independent party samples.
#include "../host/app/hud_settings.h"
#include "kh2_party_hud.h"
#include "kh2_hp_config.h"
#include "kh2_portrait.h"
#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "parts.h"
#include "../runtime/scripts/game_link.h"
#include "../runtime/mods/textures.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../host/screens/ui_text.h"
int khdays_settings_game_language(void);
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
struct Slot {
    bool valid = false, seen = false;
    int icon = -1, image = -1, width = 0, height = 0, scale = 0;
    gauge::HpGaugeSample sample;
    KhdaysUiElement box{};
    float center_x = 0, center_y = 0;
    KhdaysSequenceImage program{};
};
std::array<Slot, 3> slots;
std::optional<render::SequenceAsset> asset;
std::array<gauge::HpGaugeConfig, 3> configs;
gauge::HpGaugeConfig player_config;
std::vector<std::uint32_t> atlas_texels;
bool tried = false, reference = true;
unsigned previous_frame = 0;
int atlas = -1;
constexpr unsigned fill_offsets[]{0x980, 0x9c0, 0xa00};

std::filesystem::path path(std::string_view value)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
void log(const char *reason)
{
    std::fprintf(stderr, "kh2 party: %s\n", reason);
    khdays_log("kh2 party: %s", reason);
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
        const auto all = gauge::load_hp_gauge_configs(path(found));
        player_config = all[0];
        for (int i = 0; i < 3; ++i)
            configs[i] = all[i + 1];
        if (std::none_of(configs.begin(), configs.end(), [](const auto &c) { return c.enabled; }))
            return;
        const auto &resource_config =
            *std::find_if(configs.begin(), configs.end(), [](const auto &c) { return c.enabled; });
        const auto directory = path(found).parent_path();
        asset = render::load_sequence_asset(directory / path(resource_config.archive), resource_config.entry,
                                            directory / path(resource_config.texture));
        for (const auto &config : configs) {
            if (!config.enabled)
                continue;
            if (config.archive != resource_config.archive || config.texture != resource_config.texture ||
                config.entry != resource_config.entry)
                throw std::runtime_error("party slots must share the party gauge archive and atlas");
            for (int state = 0; state < 3; ++state) {
                gauge::HpGaugeSample sample;
                sample.status = state == 0   ? gauge::HpGaugeStatus::Normal
                                : state == 1 ? gauge::HpGaugeStatus::Alert
                                             : gauge::HpGaugeStatus::Dead;
                gauge::evaluate_hp_gauge(asset->sequence, sample, config.style);
            }
            for (auto clip : {config.style.damage[0], config.style.damage[1], config.style.damage[2],
                              config.style.intro, config.style.fade})
                if (clip >= asset->sequence.clips.size())
                    throw std::runtime_error("party JSON animation is outside the archive");
        }
        atlas_texels.resize(asset->texture.rgba.size() / 4);
        std::memcpy(atlas_texels.data(), asset->texture.rgba.data(), asset->texture.rgba.size());
        atlas = khdays_textures_add_info("kh2:party:atlas", asset->texture.width, asset->texture.height,
                                         atlas_texels.data(), KHDAYS_REPLACEMENT_NO_MIPS);
        if (atlas < 0)
            throw std::runtime_error("could not allocate party atlas");
        log(found);
    } catch (const std::exception &e) {
        log(e.what());
        asset.reset();
    }
}
} // namespace

extern "C" void khdays_ui_kh2_party_frame(unsigned frame)
{
    load();
    if (!asset)
        return;
    const float elapsed = frame >= previous_frame && !khdays_link_paused() ? float(frame - previous_frame) : 0;
    for (int i = 0; i < 3; ++i) {
        auto &s = slots[std::size_t(i)];
        const bool was = s.valid;
        int icon = 0, hp = 0, maximum = 0, low = 0;
        s.valid = khdays_hud_setting(KHDAYS_OPT_PARTY_HP) && khdays_textures_hd() && configs[std::size_t(i)].enabled &&
                  khdays_ui_mode(KHDAYS_UI_HUD) == KHDAYS_UI_ON &&
                  khdays_link_hud_portrait(i + 1, &icon, &hp, &maximum, &low);
        if (s.valid) {
            if (!was || icon != s.icon || frame < previous_frame || maximum != int(s.sample.max_hp)) {
                s.sample = {};
                s.sample.hp = s.sample.previous_hp = float(hp);
                s.sample.entering = true;
            } else {
                s.sample.frame += elapsed;
                s.sample.damage_frame = std::min(100.0f, s.sample.damage_frame + elapsed);
                if (hp < int(s.sample.hp)) {
                    s.sample.previous_hp =
                        s.sample.damage_frame < 100 ? std::max(s.sample.hp, s.sample.previous_hp) : s.sample.hp;
                    s.sample.damage_frame = 0;
                    s.sample.reaction = gauge::HpGaugeReaction::Small;
                } else if (hp > int(s.sample.hp))
                    s.sample.previous_hp = float(hp);
                s.sample.hp = float(hp);
            }
            s.icon = icon;
            s.sample.max_hp = float(maximum);
            s.sample.status = hp == 0 ? gauge::HpGaugeStatus::Dead
                              : low   ? gauge::HpGaugeStatus::Alert
                                      : gauge::HpGaugeStatus::Normal;
        }
        if (was || s.valid)
            khdays_ui_changed();
    }
    previous_frame = frame;
}
extern "C" int khdays_ui_kh2_party_ready(int slot)
{
    return asset && slot >= 1 && slot <= 3 && slots[std::size_t(slot - 1)].valid;
}
extern "C" void khdays_ui_kh2_party_reference(int enabled)
{
    reference = enabled != 0;
    for (auto &s : slots) {
        s.seen = false;
        s.program.words[KSI_COUNT] = 0;
    }
}
extern "C" void khdays_ui_kh2_party_prepare(void)
{
    if (!asset)
        return;
    KhdaysBgLayer layer;
    if (!khdays_bg_text_layer(0, 3, &layer) || layer.colors256)
        return;
    std::array<std::array<int, 4>, 3> bounds{{{256, 192, 0, 0}, {256, 192, 0, 0}, {256, 192, 0, 0}}};
    for (int y = 0; y < 25; ++y)
        for (int x = 0; x < 33; ++x) {
            const int cx = ((layer.hofs >> 3) + x) % layer.wt, cy = ((layer.vofs >> 3) + y) % layer.ht;
            const unsigned entry = khdays_bg_map_entry(0, &layer, cx, cy), address = (entry & 0x3ffu) * 32u;
            const char *origin = khdays_bg_origin(0, layer.chars + address);
            if (!origin || !std::strstr(origin, "UI/btl/") || !std::strstr(origin, "main.p2"))
                continue;
            const int slot = gauge::portrait_tile_slot(address);
            if (khdays_ui_kh2_party_ready(slot)) {
                auto &b = bounds[std::size_t(slot - 1)];
                b[0] = std::min(b[0], x * 8 - (layer.hofs & 7));
                b[1] = std::min(b[1], y * 8 - (layer.vofs & 7));
                b[2] = std::max(b[2], x * 8 - (layer.hofs & 7) + 8);
                b[3] = std::max(b[3], y * 8 - (layer.vofs & 7) + 8);
            }
            for (int i = 0; i < 3; ++i)
                if (slots[std::size_t(i)].valid && address >= fill_offsets[i] && address < fill_offsets[i] + 64)
                    khdays_ui_left_out_cell(KHDAYS_UI_HUD, 0, 3, cx, cy);
        }
    float previous_y = 0, previous_radius = 0, gap = 0;
    bool first = true;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        auto &s = slots[i];
        const auto &b = bounds[i];
        s.seen = s.valid && b[2] > b[0] && b[3] > b[1];
        if (s.seen) {
            s.box = {"hud.portrait", 0, 3, float(b[0]), float(b[1]), float(b[2] - b[0]), float(b[3] - b[1]), -1};
            const auto &c = configs[i];
            const float radius = 45 * c.style.geometry_scale * c.scale;
            s.center_x = s.box.x + s.box.w * 0.5f;
            if (first) {
                s.center_y = s.box.y + s.box.h * 0.5f - 8;
                gap = std::max(0.0f, 160 + player_config.offset_y -
                                         45 * player_config.style.geometry_scale * player_config.scale -
                                         (s.center_y + radius));
                first = false;
            } else
                s.center_y = previous_y - previous_radius - radius - gap;
            previous_y = s.center_y;
            previous_radius = radius;
        }
    }
}
extern "C" int khdays_ui_kh2_party_box(int slot, KhdaysUiElement *box)
{
    if (!khdays_ui_kh2_party_ready(slot) || !slots[std::size_t(slot - 1)].seen)
        return 0;
    *box = slots[std::size_t(slot - 1)].box;
    return 1;
}
extern "C" int khdays_ui_kh2_party_parent(int slot, KhdaysHudParent *out)
{
    KhdaysUiElement box;
    if (!khdays_ui_kh2_party_box(slot, &box))
        return 0;
    auto &s = slots[std::size_t(slot - 1)];
    auto &config = configs[std::size_t(slot - 1)];
    try {
        const auto f = gauge::evaluate_hp_gauge(asset->sequence, s.sample, config.style);
        if (!f.active)
            return 0;
        out->x = s.center_x + config.offset_x + config.portrait_offset_x +
                 (float(f.active->x) - config.style.source_origin_x + config.style.center_offset[0]) * config.scale;
        out->y = s.center_y + config.offset_y + config.portrait_offset_y +
                 (float(f.active->y) - config.style.source_origin_y + config.style.center_offset[1]) * config.scale;
        out->scale_x = f.active->scale_x;
        out->scale_y = f.active->scale_y;
        out->alpha = std::clamp(float(f.active->color >> 24) / 128, 0.0f, 1.0f);
        return 1;
    } catch (const std::exception &e) {
        log(e.what());
        config.enabled = s.valid = s.seen = false;
        s.program = {};
        khdays_ui_changed();
        return 0;
    }
}
extern "C" int khdays_ui_kh2_party_compose(int slot, const KhdaysUiElement *face, const KhdaysUiElement *hd)
{
    if (!khdays_ui_kh2_party_ready(slot))
        return 0;
    auto &s = slots[std::size_t(slot - 1)];
    const auto &config = configs[std::size_t(slot - 1)];
    try {
        KhdaysHudParent parent;
        if (!khdays_ui_kh2_party_parent(slot, &parent))
            return 0;
        const int scale = khdays_ui_scale();
        const float radius = 50 * config.style.geometry_scale * config.scale;
        const float length =
            gauge::hp_gauge_extent(s.sample.max_hp, config.style.geometry_scale).bar_length * config.scale;
        const int padding = int(std::ceil(config.canvas_padding));
        const int right = int(std::ceil(std::max(face->x + face->w, parent.x + radius))) + padding;
        const int top = int(std::floor(std::min(face->y, parent.y - radius))) - padding;
        int width = std::clamp(int(std::ceil(length + 2 * radius)) + 2 * padding, 64, 1024);
        int height = std::max(64, int(std::ceil(std::max(face->y + face->h, parent.y + radius))) - top + padding);
        if (s.image < 0 || width > s.width || height > s.height || scale != s.scale) {
            khdays_ui_image_put(s.image);
            s.width = std::max(width, s.width);
            s.height = std::max(height, s.height);
            s.image = khdays_ui_image_get(s.width * scale, s.height * scale);
            s.scale = scale;
        }
        if (s.image < 0)
            return 0;
        width = s.width;
        height = s.height;
        const int left = right - width;
        const auto frame = gauge::evaluate_hp_gauge(asset->sequence, s.sample, config.style);
        const float center_x = s.center_x + config.offset_x - left;
        const float center_y = s.center_y + config.offset_y - top;
        const render::SequenceView view{center_x * scale, center_y * scale, config.scale * scale};
        const float face_x = parent.x - face->w * 0.5f - left, face_y = parent.y - face->h * 0.5f - top;
        if (!reference) {
            auto *w = s.program.words;
            auto number = [w](unsigned p, float v) { std::memcpy(w + p, &v, 4); };
            w[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(s.image));
            w[KSI_ATLAS] = unsigned(atlas);
            w[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
            w[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
            w[KSI_WIDTH] = unsigned(width * scale);
            w[KSI_HEIGHT] = unsigned(height * scale);
            number(KSI_VIEW_X, view.x);
            number(KSI_VIEW_Y, view.y);
            number(KSI_SCALE, view.scale);
            w[KSI_FACE] = unsigned(khdays_ui_element_direct(face));
            w[KSI_HD_FACE] = hd ? unsigned(khdays_ui_element_direct(hd)) : 0xffffffffu;
            number(KSI_FACE_X, face_x * scale);
            number(KSI_FACE_Y, face_y * scale);
            number(KSI_FACE_W, face->w * scale);
            number(KSI_FACE_H, face->h * scale);
            render::encode_sequence_quads(s.program, frame.quads);
        } else {
            std::vector<std::uint32_t> pixels(std::size_t(width * scale) * std::size_t(height * scale));
            render::draw_sequence(frame.quads, asset->texture, asset->logical_size[0], asset->logical_size[1], pixels,
                                  width * scale, height * scale, view);
            for (int y = 0; y < int(face->h) * scale; ++y)
                for (int x = 0; x < int(face->w) * scale; ++x) {
                    const int dx = int(std::round(face_x * scale)) + x, dy = int(std::round(face_y * scale)) + y;
                    if (dx < 0 || dx >= width * scale || dy < 0 || dy >= height * scale)
                        continue;
                    auto &dst = pixels[std::size_t(dy) * std::size_t(width * scale) + std::size_t(dx)];
                    dst = render::source_over(dst, khdays_ui_element_rgba(face, x, y));
                    if (hd)
                        dst = render::source_over(dst, khdays_ui_element_rgba(hd, x, y));
                }
            auto *out = khdays_ui_image_begin(s.image);
            if (!out)
                return 0;
            std::copy(pixels.begin(), pixels.end(), out);
            khdays_ui_image_end(s.image);
        }
        auto e = *face;
        e.slot = KES_DIRECT | KES_PICTURE | KES_HUD | 3;
        e.x = float(left);
        e.y = float(top);
        e.w = float(width);
        e.h = float(height);
        e.image = s.image;
        khdays_ui_emit_extended(KHDAYS_UI_HUD, &e, &s.box);
        return 1;
    } catch (const std::exception &e) {
        log(e.what());
        configs[std::size_t(slot - 1)].enabled = false;
        s.valid = false;
        s.program.words[KSI_COUNT] = 0;
        return 0;
    }
}
extern "C" void khdays_ui_kh2_party_commands(KhdaysSequenceImage out[3])
{
    for (std::size_t i = 0; i < slots.size(); ++i)
        out[i] = slots[i].program;
}
