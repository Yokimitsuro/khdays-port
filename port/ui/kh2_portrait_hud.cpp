// The Days HUD adapter: one cached atlas per mapping and one GPU image per displayed slot.
#include "../host/app/hud_settings.h"
#include "kh2_portrait_hud.h"
#include "kh2_portrait.h"
#include "kh2_hp_hud.h"
#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "bg.h"
#include "elements.h"
#include "parts.h"
#include "../runtime/scripts/game_link.h"
#include "../runtime/mods/textures.h"
#include "../runtime/core/session_log.h"
#include "../runtime/core/rom.h"
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace {
namespace portrait = khdays::ui::kh2;
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;
struct Resource {
    bool tried = false;
    std::optional<render::SequenceAsset> asset;
    std::vector<std::uint32_t> texels;
    int atlas = -1;
};
struct Slot {
    bool valid = false, shown = false;
    int icon = -1, image = -1, width = 0, height = 0, scale = 0;
    portrait::PortraitPlayer player;
    KhdaysSequenceImage program{};
    KhdaysUiElement element{}, box{};
    std::vector<std::array<int, 2>> cells;
};
portrait::PortraitConfigs configs;
std::array<std::array<Resource, 2>, 21> resources;
std::array<Slot, 4> slots;
std::filesystem::path folder;
bool tried = false, loaded = false, reference = true;
unsigned previous_frame = 0;

std::filesystem::path path(std::string_view value)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
void message(const std::string &text)
{
    std::fprintf(stderr, "kh2 portraits: %s\n", text.c_str());
    khdays_log("kh2 portraits: %s", text.c_str());
}
void load()
{
    if (tried)
        return;
    tried = true;
    char found[2048];
    if (!khdays_mods_find("graphics/UI/btl/main.p2#1/chr/portraits.json", found, sizeof(found)))
        return;
    try {
        configs = portrait::load_portrait_config(path(found));
        folder = path(found).parent_path();
        loaded = true;
        message(std::string("loaded ") + found);
    } catch (const std::exception &e) {
        message(std::string(e.what()) + "; keeping the existing portraits");
    }
}
Resource &resource(int icon, int role)
{
    auto &r = resources[std::size_t(icon)][std::size_t(role)];
    if (r.tried)
        return r;
    r.tried = true;
    const auto &c = configs[std::size_t(icon)][std::size_t(role)];
    if (!c.enabled)
        return r;
    try {
        r.asset = render::load_sequence_asset(folder / path(c.archive), c.image_entry, c.sequence_entry,
                                              folder / path(c.texture));
        for (int clip : {c.normal, c.alert, c.dead, c.damage_small, c.damage_medium, c.damage_large, c.intro,
                         c.animation_override}) {
            if (clip < 0)
                continue;
            if (clip >= int(r.asset->sequence.clips.size()))
                throw std::runtime_error("animation index outside the archive");
            const auto &range = r.asset->sequence.clips[std::size_t(clip)].animations;
            const auto &s = r.asset->sequence;
            std::size_t quads = 0;
            for (std::size_t a = range.first; a < std::size_t(range.first) + range.count; ++a) {
                const auto &animation = s.animations[a];
                const auto sprite = s.sprites[animation.sprite];
                quads += sprite.count;
                for (std::size_t g = sprite.first; g < std::size_t(sprite.first) + sprite.count; ++g) {
                    const auto &part = s.parts[s.groups[g].part];
                    if (part.palette != 0 || part.scroll[0] != 0 || part.scroll[1] != 0)
                        throw std::runtime_error("palette selection/UV scrolling is not supported yet");
                }
            }
            if (quads > KSI_MAX_QUADS)
                throw std::runtime_error("too many portrait quads");
        }
        r.texels.resize(r.asset->texture.rgba.size() / 4);
        std::memcpy(r.texels.data(), r.asset->texture.rgba.data(), r.asset->texture.rgba.size());
        const auto name =
            std::string("kh2:portrait:") + portrait::portrait_names[std::size_t(icon)] + (role ? ":party" : ":player");
        r.atlas = khdays_textures_add_info(name.c_str(), r.asset->texture.width, r.asset->texture.height,
                                           r.texels.data(), KHDAYS_REPLACEMENT_NO_MIPS);
        if (r.atlas < 0)
            throw std::runtime_error("could not register portrait atlas");
        message(name + " from " + c.archive);
    } catch (const std::exception &e) {
        r.asset.reset();
        message(std::string(portrait::portrait_names[std::size_t(icon)]) + (role ? " party: " : " player: ") +
                e.what() + "; keeping the existing HD/DS portrait");
    }
    return r;
}

bool draw(Slot &slot, int index, const KhdaysBgLayer &layer)
{
    if (slot.cells.empty())
        return false;
    const auto &c = configs[std::size_t(slot.icon)][index == 0 ? 0 : 1];
    const auto &r = resources[std::size_t(slot.icon)][index == 0 ? 0 : 1];
    if (!r.asset)
        return false;
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
    seq::SequencePose pose;
    pose.alpha = parent.alpha;
    auto frame = slot.player.evaluate(r.asset->sequence, pose);
    float left = 0, top = 0, right = 0, bottom = 0;
    for (auto &q : frame.quads)
        for (auto &v : q.vertices) {
            v.x = (v.x - c.source_origin_x) * c.scale * parent.scale_x;
            v.y = (v.y - c.source_origin_y) * c.scale * parent.scale_y;
            left = std::min(left, v.x);
            top = std::min(top, v.y);
            right = std::max(right, v.x);
            bottom = std::max(bottom, v.y);
        }
    left = std::floor(left - c.canvas_padding);
    top = std::floor(top - c.canvas_padding);
    const float span_x = std::ceil(right + c.canvas_padding - left);
    const float span_y = std::ceil(bottom + c.canvas_padding - top);
    if (!std::isfinite(span_x) || !std::isfinite(span_y) || span_x > 512 || span_y > 512)
        throw std::runtime_error("portrait exceeds its 512-dot canvas limit");
    const int width = std::max(1, int(span_x));
    const int height = std::max(1, int(span_y));
    const int scale = khdays_ui_scale();
    if (slot.image < 0 || slot.scale != scale || width > slot.width || height > slot.height) {
        khdays_ui_image_put(slot.image);
        slot.width = std::max(width, slot.width);
        slot.height = std::max(height, slot.height);
        slot.scale = scale;
        slot.image = khdays_ui_image_get(slot.width * scale, slot.height * scale);
    }
    if (slot.image < 0)
        return false;
    const int w = slot.width * scale, h = slot.height * scale;
    const render::SequenceView view{-left * scale, -top * scale, float(scale)};
    if (reference) {
        std::vector<std::uint32_t> pixels(std::size_t(w) * std::size_t(h));
        render::draw_sequence(frame.quads, r.asset->texture, r.asset->logical_size[0], r.asset->logical_size[1], pixels,
                              w, h, view);
        auto *out = khdays_ui_image_begin(slot.image);
        if (!out)
            return false;
        std::copy(pixels.begin(), pixels.end(), out);
        khdays_ui_image_end(slot.image);
    } else {
        auto *words = slot.program.words;
        auto number = [words](unsigned at, float value) { std::memcpy(words + at, &value, 4); };
        words[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(slot.image));
        words[KSI_ATLAS] = unsigned(r.atlas);
        words[KSI_LOGICAL_W] = unsigned(r.asset->logical_size[0]);
        words[KSI_LOGICAL_H] = unsigned(r.asset->logical_size[1]);
        words[KSI_WIDTH] = unsigned(w);
        words[KSI_HEIGHT] = unsigned(h);
        number(KSI_VIEW_X, view.x);
        number(KSI_VIEW_Y, view.y);
        number(KSI_SCALE, view.scale);
        words[KSI_CLEAR] = 1;
        render::encode_sequence_quads(slot.program, frame.quads);
    }
    slot.element = slot.box;
    slot.element.slot = KES_DIRECT | KES_PICTURE | KES_HUD | 3;
    slot.element.x = parent.x + c.offset_x + left;
    slot.element.y = parent.y + c.offset_y + top;
    slot.element.w = float(slot.width);
    slot.element.h = float(slot.height);
    slot.element.image = slot.image;
    return true;
}
} // namespace

extern "C" void khdays_ui_kh2_portrait_frame(unsigned frame)
{
    load();
    if (!loaded)
        return;
    const float ticks = frame >= previous_frame && !khdays_link_paused() ? float(frame - previous_frame) : 0;
    for (int index = 0; index < 4; ++index) {
        auto &slot = slots[std::size_t(index)];
        const bool was = slot.valid;
        int icon, hp, maximum, low;
        slot.valid = false;
        if (khdays_hud_setting(KHDAYS_OPT_PARTY_HP) && khdays_textures_hd() &&
            khdays_ui_mode(KHDAYS_UI_HUD) == KHDAYS_UI_ON &&
            khdays_link_hud_portrait(index, &icon, &hp, &maximum, &low)) {
            auto &r = resource(icon, index == 0 ? 0 : 1);
            if (r.asset) {
                const bool reset = !was || slot.icon != icon || frame < previous_frame;
                slot.icon = icon;
                slot.player.update(r.asset->sequence, configs[std::size_t(icon)][index == 0 ? 0 : 1], hp, maximum,
                                   low != 0, ticks, reset);
                slot.valid = true;
            }
        }
        if (was || slot.valid)
            khdays_ui_changed();
    }
    previous_frame = frame;
}

extern "C" void khdays_ui_kh2_portrait_reference(int enabled)
{
    reference = enabled != 0;
    for (auto &slot : slots) {
        slot.program.words[KSI_COUNT] = 0;
        slot.program.words[KSI_CLEAR] = 0;
        slot.shown = false;
    }
}

extern "C" void khdays_ui_kh2_portrait_prepare(void)
{
    if (!loaded || khdays_ui_mode(KHDAYS_UI_HUD) != KHDAYS_UI_ON)
        return;
    if (std::none_of(slots.begin(), slots.end(), [](const Slot &s) { return s.valid; }))
        return;
    KhdaysBgLayer layer;
    if (!khdays_bg_text_layer(0, 3, &layer) || layer.colors256)
        return;
    for (auto &slot : slots)
        slot.cells.clear();
    for (int y = 0; y < 25; ++y)
        for (int x = 0; x < 33; ++x) {
            const int cx = ((layer.hofs >> 3) + x) % layer.wt, cy = ((layer.vofs >> 3) + y) % layer.ht;
            const unsigned entry = khdays_bg_map_entry(0, &layer, cx, cy);
            const int index = portrait::portrait_tile_slot((entry & 0x3ffu) * 32u);
            if (index < 0 || !slots[std::size_t(index)].valid || khdays_ui_cell_taken(0, 3, cx, cy))
                continue;
            const char *origin = khdays_bg_origin(0, layer.chars + (entry & 0x3ffu) * 32u);
            if (origin && std::strstr(origin, "UI/btl/") && std::strstr(origin, "main.p2"))
                slots[std::size_t(index)].cells.push_back({x, y});
        }
    for (int index = 0; index < 4; ++index) {
        auto &slot = slots[std::size_t(index)];
        if (!slot.valid)
            continue;
        try {
            slot.shown = draw(slot, index, layer);
        } catch (const std::exception &e) {
            message(std::string(e.what()) + "; keeping the existing HD/DS portrait");
            resources[std::size_t(slot.icon)][index == 0 ? 0 : 1].asset.reset();
            slot.valid = slot.shown = false;
            slot.program.words[KSI_COUNT] = slot.program.words[KSI_CLEAR] = 0;
        }
        if (slot.shown)
            for (auto cell : slot.cells)
                khdays_ui_left_out_cell(KHDAYS_UI_HUD, 0, 3, ((layer.hofs >> 3) + cell[0]) % layer.wt,
                                        ((layer.vofs >> 3) + cell[1]) % layer.ht);
    }
}

extern "C" void khdays_ui_kh2_portrait_emit(void)
{
    for (const auto &slot : slots)
        if (slot.shown)
            khdays_ui_emit_extended(KHDAYS_UI_HUD, &slot.element, &slot.box);
}

extern "C" void khdays_ui_kh2_portrait_commands(KhdaysSequenceImage out[4])
{
    for (std::size_t i = 0; i < slots.size(); ++i)
        out[i] = slots[i].program;
}
