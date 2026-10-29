// Animated field notifications share GPU composition and active-track text anchors.
#include "../host/app/hud_settings.h"
#include "kh2_obtained.h"
#include "kh2_hp_config.h"
#include "kh2_information.h"
#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "parts.h"
#include "bg.h"
#include "situation.h"
#include "../runtime/menus/camp.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../runtime/mods/textures.h"
#include "../runtime/scripts/game_link.h"
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
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;
namespace ui = khdays::ui::kh2;
struct Region {
    int x = 0, y = 0, w = 0, h = 0;
} regions[4] = {{0, 7, 13, 3}};
// Native clears are deferred: retain only the exact resident cells we replaced.
struct NativeCell {
    bool valid = false;
    unsigned map = 0, chars = 0, entry = 0;
    std::array<unsigned char, 32> pixels{};
};
struct Notification {
    const char *name;
    int kind;
    Notification(const char *value, int index) : name(value), kind(index) {}
    ui::ObtainedConfig config;
    std::optional<render::SequenceAsset> asset;
    std::vector<unsigned> atlas_pixels;
    bool tried = false, allowed = false, pending = false, active = false, started = false, native = false;
    bool retiring = false;
    std::array<NativeCell, 2 * 32 * 24> native_cells{};
    unsigned previous = 0, revision = 0;
    unsigned context_epoch = 0;
    float age = 0, left = 0, top = 0;
    float ink_left = 0, ink_right = 0;
    int atlas = -1, image = -1, text = -1, width = 0, height = 0, image_scale = 0, text_w = 0, text_h = 0;
    int frames = 0;
    KhdaysUiElement anchor{};
    KhdaysSequenceImage program{};
    static std::filesystem::path path(const std::string &s)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
    }
    void disable(const char *why)
    {
        asset.reset();
        native_cells.fill({});
        retiring = false;
        pending = active = started = native = false;
        program = {};
        khdays_ui_changed();
        khdays_log("kh2 %s: %s; keeping the Days notification", name, why);
    }
    void load()
    {
        if (tried)
            return;
        tried = true;
        try {
            char relative[128], found[2048];
            const char *language = khdays_language_code(khdays_settings_game_language());
            std::snprintf(relative, sizeof(relative), "layouts/gauge/%s/gauge.json",
                          language && *language ? language : "en");
            if (!khdays_mods_find(relative, found, sizeof(found)) &&
                !khdays_mods_find("layouts/gauge/en/gauge.json", found, sizeof(found)))
                return;
            config = ui::load_notification_config(path(found), name);
            if (!config.enabled)
                return;
            if (config.animation < 0)
                throw std::runtime_error("select an animation before enabling the replacement");
            const auto folder = path(found).parent_path();
            asset =
                render::load_sequence_asset(folder / path(config.archive), config.entry, folder / path(config.texture));
            frames = seq::sequence_frame_count(asset->sequence, std::size_t(config.animation));
            if (frames <= 0 || frames > 4096)
                throw std::runtime_error("animation must contain 1 to 4096 frames");
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            bool has_anchor = false;
            for (int t = 0; t < frames; ++t) {
                const auto f = seq::evaluate_sequence(asset->sequence, std::size_t(config.animation), float(t));
                has_anchor |= f.active.has_value();
                if (f.quads.size() > KSI_MAX_QUADS)
                    throw std::runtime_error("animation exceeds GPU quad capacity");
                for (const auto &q : f.quads)
                    for (const auto &v : q.vertices) {
                        x0 = std::min(x0, (v.x - config.source_origin_x) * config.scale);
                        x1 = std::max(x1, (v.x - config.source_origin_x) * config.scale);
                        y0 = std::min(y0, (v.y - config.source_origin_y) * config.scale);
                        y1 = std::max(y1, (v.y - config.source_origin_y) * config.scale);
                    }
            }
            if (!has_anchor && kind != 3)
                throw std::runtime_error("animation needs an active text anchor");
            left = std::floor(x0) - 2;
            top = std::floor(y0) - 2;
            const float w = std::ceil(x1) - left + 2, h = std::ceil(y1) - top + 2;
            if (!std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0 || w > 2048 || h > 512)
                throw std::runtime_error("animation exceeds supported canvas dimensions");
            width = int(w);
            height = int(h);
            atlas_pixels.resize(asset->texture.rgba.size() / 4);
            std::memcpy(atlas_pixels.data(), asset->texture.rgba.data(), asset->texture.rgba.size());
            const std::string id = std::string("kh2:") + name + ":atlas";
            atlas = khdays_textures_add_info(id.c_str(), asset->texture.width, asset->texture.height,
                                             atlas_pixels.data(), KHDAYS_REPLACEMENT_NO_MIPS);
            if (atlas < 0)
                throw std::runtime_error("could not register notification atlas");
            khdays_log("kh2 %s: %s, animation %d", name, found, config.animation);
        } catch (const std::exception &e) {
            disable(e.what());
        }
    }
    int configured()
    {
        load();
        return asset && khdays_textures_hd() && khdays_ui_mode(KHDAYS_UI_TEXT) == KHDAYS_UI_ON;
    }
    int enabled()
    {
        return khdays_hud_setting(kind == 3 ? KHDAYS_OPT_LOCKED_ON : KHDAYS_OPT_NOTIFICATIONS) && configured();
    }
    bool suppress(int bg, int x, int y)
    {
        if ((!pending && !started && !retiring) || (bg != 1 && bg != 3) || (kind == 3 && bg != 3) || x < 0 || x >= 32 ||
            y < 0 || y >= 24 || !allowed)
            return false;
        const auto &r = regions[kind];
        const bool live = (pending || started) && native && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
        auto &cell = native_cells[(bg == 3 ? 32 * 24 : 0) + y * 32 + x];
        if ((!live && !cell.valid) || !enabled())
            return false;
        KhdaysBgLayer layer{};
        if (!khdays_bg_text_layer(0, bg, &layer) || layer.colors256)
            return live;
        const unsigned entry = khdays_bg_map_entry(0, &layer, x, y);
        const unsigned at = layer.chars + (entry & 1023u) * 32u;
        std::array<unsigned char, 32> pixels{};
        for (unsigned i = 0; i < pixels.size(); ++i) {
            const auto *p = khdays_bg_memory(0, at + i);
            if (!p) {
                cell.valid = false;
                return live;
            }
            pixels[i] = *p;
        }
        if (live) {
            cell.valid = std::any_of(pixels.begin(), pixels.end(), [](unsigned char v) { return v != 0; });
            cell.map = layer.map;
            cell.chars = layer.chars;
            cell.entry = entry;
            cell.pixels = pixels;
            return true;
        }
        if (cell.map != layer.map || cell.chars != layer.chars || cell.entry != entry || cell.pixels != pixels)
            cell.valid = false;
        return cell.valid;
    }
    void retain_native()
    {
        if (!owns())
            return;
        const auto &r = regions[kind];
        for (int bg = 1; bg <= 3; bg += 2)
            for (int y = std::max(r.y, 0); y < std::min(r.y + r.h, 24); ++y)
                for (int x = std::max(r.x, 0); x < std::min(r.x + r.w, 32); ++x)
                    suppress(bg, x, y);
        retiring = std::any_of(native_cells.begin(), native_cells.end(), [](const auto &c) { return c.valid; });
    }
    void request(int announcement)
    {
        if (!configured())
            return;
        if (!announcement)
            retain_native();
        context_epoch = khdays_ui_information_epoch();
        native = announcement != 0;
        if (announcement)
            pending = true;
        else {
            pending = false;
            if (config.loop) {
                active = started = false;
                program = {};
            }
        }
        khdays_ui_changed();
    }
    void frame(unsigned frame)
    {
        KhdaysSituationInputs context{};
        khdays_situation_take(&context, frame);
        const auto overlay = [&](int id) { return (context.overlays & (1ull << id)) != 0; };
        const bool event = context.event_status >= 0 && !(context.event_status & 8) &&
                           (khdays_situation_ran(&context, KHDAYS_UPD_Ov023_SceneRun) || context.pause_mode);
        const bool field = context.scene == 2 && overlay(2) && context.camp_screen == KHDAYS_CAMP_NONE && !overlay(8) &&
                           !overlay(25) && !overlay(26) && !overlay(27);
        const bool was_allowed = allowed;
        allowed = field && !context.movie && !context.tutorial && !event;
        // The existing native-panel reset hook advances this epoch across map/HUD recreation.
        const unsigned epoch = khdays_ui_information_epoch();
        if (!field || !configured() || frame < previous || context_epoch != epoch) {
            if (active || pending || started)
                khdays_ui_changed();
            if (started || retiring)
                native_cells.fill({});
            pending = active = started = native = false;
            retiring = false;
            program = {};
        }
        if (!enabled()) {
            pending = native;
            if (started || retiring)
                native_cells.fill({});
            active = started = retiring = false;
            program = {};
        }
        // Hiding the HUD does not dismiss its native notification or repeat its request on return.
        if (!allowed) {
            program = {};
            if (was_allowed)
                khdays_ui_changed();
        }
        context_epoch = epoch;
        if (active && !khdays_link_paused() && frame >= previous)
            age += float(frame - previous) * config.frames_per_tick;
        if (active && config.loop && age >= frames)
            age = float(std::min(asset->sequence.clips[std::size_t(config.animation)].restart, unsigned(frames - 1)));
        previous = frame;
        if (retiring && !native) {
            for (unsigned i = 0; i < native_cells.size(); ++i)
                if (native_cells[i].valid) {
                    const unsigned xy = i % (32 * 24);
                    suppress(i >= 32 * 24 ? 3 : 1, int(xy % 32), int(xy / 32));
                }
            khdays_ui_changed();
            retiring = std::any_of(native_cells.begin(), native_cells.end(), [](const auto &c) { return c.valid; });
        }
        if (enabled() && (active || pending))
            khdays_ui_changed();
    }
    void begin()
    {
        program = {};
    }
    int pixels(const KhdaysUiElement *box, const unsigned *rgba, int w, int h, unsigned version)
    {
        if (!allowed || !enabled() || !box || !rgba || w <= 0 || h <= 0 || w > 4096 || h > 512)
            return 0;
        try {
            const int scale = khdays_ui_scale();
            if (text < 0 || w != text_w || h != text_h) {
                khdays_ui_image_put(text);
                text = khdays_ui_image_get(w, h);
                text_w = w;
                text_h = h;
                revision = version - 1;
            }
            if (image < 0 || scale != image_scale) {
                khdays_ui_image_put(image);
                image = khdays_ui_image_get(width * scale, height * scale);
                image_scale = scale;
            }
            if (text < 0 || image < 0)
                return 0;
            if (revision != version) {
                auto *out = khdays_ui_image_begin(text);
                if (!out)
                    return 0;
                std::copy_n(rgba, std::size_t(w) * h, out);
                khdays_ui_image_end(text);
                revision = version;
                ink_left = float(w) / scale;
                ink_right = 0;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        if (rgba[std::size_t(y) * w + x] >> 24) {
                            ink_left = std::min(ink_left, float(x) / scale);
                            ink_right = std::max(ink_right, float(x + 1) / scale);
                        }
                if (ink_right == 0)
                    ink_left = 0;
            }
            anchor = *box;
            if (pending) {
                age = 0;
                pending = false;
                active = started = true;
            }
            return owns();
        } catch (const std::exception &e) {
            disable(e.what());
            return 0;
        }
    }
    int owns()
    {
        return allowed && (pending || started) && native && enabled();
    }
    void end()
    {
        if (!allowed || !active || !enabled() || image < 0 || text < 0)
            return;
        try {
            if (!config.loop && age >= frames) {
                active = false;
                return;
            }
            const auto f = seq::evaluate_sequence(asset->sequence, std::size_t(config.animation), age);
            auto *w = program.words;
            const auto number = [w](unsigned i, float v) { std::memcpy(w + i, &v, 4); };
            w[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(image));
            w[KSI_ATLAS] = unsigned(atlas);
            w[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
            w[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
            w[KSI_WIDTH] = unsigned(width * image_scale);
            w[KSI_HEIGHT] = unsigned(height * image_scale);
            const float vx = (-left - config.source_origin_x * config.scale) * image_scale;
            const float vy = (-top - config.source_origin_y * config.scale) * image_scale;
            number(KSI_VIEW_X, vx);
            number(KSI_VIEW_Y, vy);
            number(KSI_SCALE, config.scale * image_scale);
            w[KSI_CLEAR] = w[KSI_ANCHORED] = 1;
            number(KSI_ANCHOR_X, anchor.x);
            number(KSI_ANCHOR_Y, anchor.y);
            render::encode_sequence_quads(program, f.quads);
            const auto &params = asset->sequence.clips[std::size_t(config.animation)].parameters;
            const auto a = f.active.value_or(seq::SequenceActive{});
            const float text_scale = config.text_scale * config.scale / 0.65f;
            // TREASURE_INFO::update selects ALIGN_CENTER when the clip's Ex parameter is one.
            const float align_x = params[4] == 1 ? (ink_left + ink_right) * .5f * text_scale * image_scale : 0;
            w[KSI_TEXT_INSERT] = unsigned(f.quads.size()) + 1;
            w[KSI_FACE] = f.active ? unsigned(khdays_ui_image_replacement(text)) : ~0u;
            w[KSI_HD_FACE] = ~0u;
            number(KSI_TEXT_SCALE, text_scale);
            number(KSI_FACE_X,
                   vx + ((float(a.x) + float(params[0])) * config.scale + config.text_offset_x) * image_scale -
                       align_x);
            number(KSI_FACE_Y,
                   vy + ((float(a.y) + float(params[1])) * config.scale + config.text_offset_y) * image_scale -
                       3 * text_scale * image_scale);
            number(KSI_FACE_W, text_w * text_scale);
            number(KSI_FACE_H, text_h * text_scale);
            number(KSI_TEXT_CLIP_X, 0);
            number(KSI_TEXT_CLIP_Y, 0);
            number(KSI_TEXT_CLIP_W, float(width * image_scale));
            number(KSI_TEXT_CLIP_H, float(height * image_scale));
            number(KSI_TEXT_ALPHA, f.active ? std::clamp(float(a.color >> 24) / 128, 0.0f, 1.0f) : 0);
            auto e = anchor;
            e.id = kind == 0   ? "hud.obtained"
                   : kind == 1 ? "hud.backpack_full"
                   : kind == 2 ? "hud.breakthrough"
                               : "hud.locked_on";
            e.image = image;
            e.slot = KES_DIRECT | KES_FINE | KES_PICTURE | KES_HUD | KES_NATIVE_FADE | (kind == 3 ? 3 : 1);
            e.x += config.offset_x - 2;
            e.y += config.offset_y - 2;
            e.w = float(width);
            e.h = float(height);
            khdays_ui_emit_extended(KHDAYS_UI_TEXT, &e, &anchor);
        } catch (const std::exception &e) {
            disable(e.what());
        }
    }
};
Notification notices[4] = {{"obtained", 0}, {"backpack_full", 1}, {"breakthrough", 2}, {"locked_on", 3}};
bool valid_kind(int kind)
{
    return kind >= 1 && kind <= 3;
}
} // namespace
extern "C" int khdays_ui_obtained_enabled(void)
{
    return notices[0].enabled();
}
extern "C" int khdays_ui_obtained_configured(void)
{
    return notices[0].configured();
}
extern "C" void khdays_ui_obtained_request(int a)
{
    notices[0].request(a);
}
extern "C" void khdays_ui_obtained_frame(unsigned f)
{
    for (auto &n : notices)
        n.frame(f);
}
extern "C" void khdays_ui_obtained_begin(void)
{
    for (auto &n : notices)
        n.begin();
    auto &n = notices[3];
    if (n.native && n.allowed && n.enabled()) {
        const unsigned clear = 0;
        n.pixels(&n.anchor, &clear, 1, 1, 1);
    }
}
extern "C" int khdays_ui_obtained_text(const KhdaysUiElement *b, const unsigned *p, int w, int h, unsigned v)
{
    return notices[0].pixels(b, p, w, h, v);
}
extern "C" int khdays_ui_obtained_owns(void)
{
    return notices[0].owns();
}
extern "C" void khdays_ui_obtained_end(void)
{
    for (auto &n : notices)
        n.end();
}
extern "C" void khdays_ui_obtained_commands(KhdaysSequenceImage *out)
{
    *out = notices[0].program;
}
extern "C" void khdays_ui_notification_commands(KhdaysSequenceImage *out)
{
    for (int k = 1; k <= 3; ++k)
        out[k - 1] = notices[k].program;
}
extern "C" int khdays_ui_notification_enabled(int kind)
{
    return valid_kind(kind) && notices[kind].enabled();
}
extern "C" int khdays_ui_notification_configured(int kind)
{
    return valid_kind(kind) && notices[kind].configured();
}
extern "C" void khdays_ui_notification_request(int kind, int present)
{
    if (valid_kind(kind))
        notices[kind].request(present);
}
extern "C" int khdays_ui_notification_owns(int kind)
{
    return valid_kind(kind) && notices[kind].owns();
}
extern "C" int khdays_ui_notification_text(int kind, const KhdaysUiElement *b, const unsigned *p, int w, int h,
                                           unsigned v)
{
    return valid_kind(kind) && notices[kind].pixels(b, p, w, h, v);
}
extern "C" int khdays_ui_notification_box(int kind, KhdaysUiElement *out)
{
    if (!valid_kind(kind) || !notices[kind].allowed || !notices[kind].native || !notices[kind].enabled())
        return 0;
    *out = notices[kind].anchor;
    return 1;
}
extern "C" int khdays_ui_notification_cell(int engine, int bg, int x, int y)
{
    if (engine || (bg != 1 && bg != 3))
        return 0;
    for (auto &n : notices)
        if (n.suppress(bg, x, y))
            return 1;
    return 0;
}
// Observe the native layout callback; Days still copies and clears its own maps.
extern "C" void khdays_ui_notification_layout(const void *request, int shown)
{
    if (!request)
        return;
    const auto *p = static_cast<const unsigned char *>(request);
    auto word = [p](int at) {
        short v;
        std::memcpy(&v, p + at, 2);
        return int(v);
    };
    int destination;
    std::memcpy(&destination, p + 16, 4);
    if (destination != 11)
        return; // main BG3 in the field layout
    const int tag = word(0);
    int k = tag == 78 || tag == 93 ? 1 : tag == 72 ? 2 : tag == 60 || tag == 61 ? 3 : 0;
    if (!k || !notices[k].configured())
        return;
    auto &n = notices[k];
    if (!shown) {
        n.request(0);
        return;
    }
    const int w = word(10), h = word(12);
    if (w <= 0 || w > 32 || h <= 0 || h > 24)
        return;
    regions[k] = {word(2), word(4), w, h};
    n.anchor = {"hud.notification", 0, 3, float(word(2) * 8), float(word(4) * 8), float(w * 8), float(h * 8), -1};
    if (!n.native)
        n.request(1);
}
