// gaug 0 -> 1, independent 0 -> 2 reveal, and 3 on closing; Days owns the message and glyphs.
#include "../host/app/hud_settings.h"
#include "hud_options.h"
#include "kh2_information.h"
#include "kh2_hp_config.h"
#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "parts.h"
#include "bg.h"
#include "situation.h"
#include "../runtime/menus/camp.h"
#include "../host/app/host.h"
#include "../host/screens/ui_text.h"
#include "../runtime/core/session_log.h"
#include "../runtime/core/rom.h"
#include "../runtime/mods/textures.h"
#include "../runtime/scripts/game_link.h"
#include "../runtime/display/frame_package.h"
int khdays_settings_game_language(void);
}
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <optional>
#include <stdexcept>

namespace {
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;
namespace ui = khdays::ui::kh2;
ui::InformationConfig config;
std::optional<render::SequenceAsset> asset;
std::vector<std::uint32_t> atlas_pixels;
int atlas = -1;
bool tried = false, available = false, enabled = false;
unsigned now = 0;
unsigned epoch = 1;
KhdaysSituationInputs context{};
bool context_allowed = false, suspended = false;
bool mode_known = false, single_mode = false;
struct Message {
    unsigned generation = 0, hash = 0;
    bool present = false, restored = false;
};
std::array<Message, 2> messages;
bool held = false;
float left = 0, top = 0, text_start_x = 0, clip_width = 0, readable_right = 0;
int width = 0, height = 0;
struct Window {
    KhdaysUiElement anchor{};
    KhdaysSequenceImage program{};
    seq::SequencePlayer background, mask;
    int image = -1, text = -1, scale = 0, text_w = 0, text_h = 0;
    unsigned revision = 0, message = 0, previous = 0, coverage = 0;
    float age = 0, close_age = 0, visible_age = 0, scroll_age = 0, ink_right = 0, first_x = 8, first_y = 3;
    bool active = false, closing = false, intro = false, seen = false, started = false, text_ready = false;
};
std::array<Window, 2> windows;
struct UpperArtCell {
    bool valid = false;
    unsigned map = 0, chars = 0, entry = 0;
    int cx = 0, cy = 0, bytes = 0;
    std::array<unsigned char, 64> pixels{};
};
std::array<std::array<UpperArtCell, 96>, 2> upper_art_cells;
bool upper_art_owned = false;
std::filesystem::path path(const std::string &s)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
void load()
{
    if (tried)
        return;
    tried = true;
    try {
        char relative[128], found[2048];
        const char *lang = khdays_language_code(khdays_settings_game_language());
        std::snprintf(relative, sizeof(relative), "layouts/gauge/%s/gauge.json", lang && *lang ? lang : "en");
        if (!khdays_mods_find(relative, found, sizeof(found)) &&
            !khdays_mods_find("layouts/gauge/en/gauge.json", found, sizeof(found)))
            return;
        config = ui::load_information_config(path(found));
        if (!config.enabled)
            return;
        const auto folder = path(found).parent_path();
        asset = render::load_sequence_asset(folder / path(config.archive), config.entry, folder / path(config.texture));
        const auto loop = seq::evaluate_sequence(asset->sequence, std::size_t(config.loop), 0);
        text_start_x = (float(loop.active.value_or(seq::SequenceActive{}).x) +
                        float(asset->sequence.clips.at(std::size_t(config.loop)).parameters[0]) +
                        float(asset->sequence.clips.at(std::size_t(config.intro)).parameters[0])) *
                           config.scale +
                       config.text_offset_x;
        clip_width = config.clip_width > 0 ? config.clip_width
                                           : float(asset->sequence.clips.at(std::size_t(config.loop)).parameters[4]);
        if (clip_width <= 0 || clip_width > 2048)
            throw std::runtime_error("information sequence needs a valid Ex parameter or explicit clip_width");
        readable_right = clip_width;
        const auto revealed =
            seq::evaluate_sequence(asset->sequence, std::size_t(config.reveal),
                                   float(seq::sequence_frame_count(asset->sequence, std::size_t(config.reveal)) - 1));
        // Stop the final glyph before the stationary right-hand fade, rather than under its opaque edge.
        for (const auto &q : revealed.quads) {
            float x0 = clip_width, x1 = 0;
            unsigned a0 = 255, a1 = 0;
            for (const auto &v : q.vertices) {
                x0 = std::min(x0, v.x);
                x1 = std::max(x1, v.x);
                a0 = std::min(a0, v.color >> 24);
                a1 = std::max(a1, v.color >> 24);
            }
            if (x0 >= clip_width / 2 && x1 >= clip_width && a0 == 0 && a1 > 0)
                for (const auto &v : q.vertices)
                    if ((v.color >> 24) == 0)
                        readable_right = std::min(readable_right, v.x);
        }
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        for (int clip : {config.intro, config.loop, config.reveal, config.outro}) {
            const int count = seq::sequence_frame_count(asset->sequence, std::size_t(clip));
            if (count <= 0 || count > 4096)
                throw std::runtime_error("information timeline exceeds 4096 frames");
            for (int t = 0; t < count; ++t) {
                const auto f = seq::evaluate_sequence(asset->sequence, std::size_t(clip), float(t));
                if (f.quads.size() > KSI_MAX_QUADS / 2)
                    throw std::runtime_error("too many information quads");
                for (const auto &q : f.quads)
                    for (const auto &v : q.vertices) {
                        x0 = std::min(x0, v.x);
                        x1 = std::max(x1, v.x);
                        y0 = std::min(y0, v.y);
                        y1 = std::max(y1, v.y);
                    }
            }
        }
        left = std::floor((x0 - config.source_origin_x) * config.scale) - 2;
        top = std::floor((y0 - config.source_origin_y) * config.scale) - 2;
        const float w = std::ceil((x1 - config.source_origin_x) * config.scale) - left + 2;
        const float h = std::ceil((y1 - config.source_origin_y) * config.scale) - top + 2;
        if (!std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0 || w > 2048 || h > 512)
            throw std::runtime_error("information canvas exceeds supported dimensions");
        width = int(w);
        height = int(h);
        atlas_pixels.resize(asset->texture.rgba.size() / 4);
        std::memcpy(atlas_pixels.data(), asset->texture.rgba.data(), asset->texture.rgba.size());
        atlas = khdays_textures_add_info("kh2:information:atlas", asset->texture.width, asset->texture.height,
                                         atlas_pixels.data(), KHDAYS_REPLACEMENT_NO_MIPS);
        if (atlas < 0)
            throw std::runtime_error("could not allocate information atlas");
    } catch (const std::exception &e) {
        asset.reset();
        khdays_log("kh2 information: %s; keeping the Days banner", e.what());
    }
}
void disable(const char *reason)
{
    available = enabled = false;
    asset.reset();
    for (auto &s : windows) {
        s.active = s.started = false;
        s.program = {};
    }
    upper_art_owned = false;
    for (auto &cells : upper_art_cells)
        cells.fill({});
    khdays_ui_changed();
    khdays_log("kh2 information: %s; keeping the Days banner", reason);
}
void close(Window &s)
{
    if (!s.active || s.closing)
        return;
    s.closing = true;
    s.close_age = 0;
    s.previous = now;
    s.background.select(asset->sequence, std::size_t(config.outro), true);
}
void open(Window &s, const Message &message, bool restore = false)
{
    const bool already_open = s.active && !s.closing;
    s.active = s.started = true;
    s.closing = false;
    s.intro = !already_open && !restore;
    s.age = s.close_age = s.visible_age = s.scroll_age = 0;
    s.previous = now;
    if (!restore)
        s.text_ready = false;
    s.background.select(asset->sequence, std::size_t(s.intro ? config.intro : config.loop), true);
    s.mask.select(asset->sequence, std::size_t(s.intro ? config.intro : config.reveal), true);
    if (restore)
        s.mask.seek(float(seq::sequence_frame_count(asset->sequence, std::size_t(config.reveal)) - 1));
    s.message = message.generation;
    khdays_log("kh2 information: engine %c %s, caption %08x", s.anchor.screen ? 'B' : 'A',
               restore   ? "restored"
               : s.intro ? "intro"
                         : "reveal",
               message.hash);
}
void observe(int engine, const unsigned short *text, bool force)
{
    auto &m = messages[std::size_t(engine)];
    unsigned hash = 2166136261u;
    if (text)
        for (int i = 0; i < 8192 && text[i]; ++i)
            hash = (hash ^ text[i]) * 16777619u;
    const bool present = text && text[0];
    if (force || present != m.present || (present && hash != m.hash)) {
        ++m.generation;
        if (present)
            m.hash = hash;
        m.present = present;
        m.restored = false;
        khdays_ui_changed();
    }
}
void draw(Window &s)
{
    const auto &d = asset->sequence;
    const bool paused = held || khdays_link_paused();
    const float delta = paused || now < s.previous ? 0 : float(now - s.previous) * config.frames_per_tick;
    s.previous = now;
    s.visible_age += delta;
    if (!s.intro && !s.closing)
        s.scroll_age += delta;
    const float text_scale = config.text_scale * config.scale / 0.65f;
    const float overflow =
        std::max(0.0f, text_start_x + (s.ink_right - s.first_x) * text_scale - readable_right * config.scale);
    const float scroll = std::min(overflow, std::max(0.0f, s.scroll_age - config.scroll_wait_frames) *
                                                config.scroll_speed * config.scale);
    const float reading_frames =
        overflow > 0 && config.scroll_speed > 0
            ? config.scroll_wait_frames + overflow / (config.scroll_speed * config.scale) + config.scroll_wait_frames
            : 0;
    if (!paused && s.anchor.screen == 1 && (khdays_host_graphics_flags() & KHDAYS_HOST_ONE_SCREEN) &&
        s.visible_age >= config.hold_frames * config.frames_per_tick && s.scroll_age >= reading_frames)
        close(s);
    if (s.closing) {
        s.close_age += delta;
        if (s.close_age >= seq::sequence_frame_count(d, std::size_t(config.outro))) {
            s.active = false;
            return;
        }
        s.background.seek(s.close_age);
    } else if (s.intro) {
        s.age += delta;
        const float count = float(seq::sequence_frame_count(d, std::size_t(config.intro)));
        if (s.age >= count) {
            s.intro = false;
            s.background.select(d, std::size_t(config.loop), true);
            s.mask.select(d, std::size_t(config.reveal), true);
            s.background.advance(d, s.age - count);
            s.mask.advance(d, s.age - count);
        } else {
            s.background.seek(s.age);
            s.mask.seek(s.age);
        }
    } else {
        s.background.advance(d, delta);
        s.mask.advance(d, delta);
    }
    // A paused caption must be readable even when its intro/reveal was frozen before opening.
    const bool pause_window = paused && !s.closing;
    const auto background_clip = pause_window ? std::size_t(config.loop) : s.background.clip();
    auto background = seq::evaluate_sequence(d, background_clip, pause_window ? 0 : s.background.frame());
    const unsigned split = unsigned(background.quads.size());
    if (!s.closing && !paused) {
        const auto mask = seq::evaluate_sequence(d, s.mask.clip(), s.mask.frame());
        background.quads.insert(background.quads.end(), mask.quads.begin(), mask.quads.end());
    }
    auto &p = s.program;
    p = {};
    auto *w = p.words;
    const auto number = [w](unsigned i, float v) { std::memcpy(w + i, &v, 4); };
    w[KSI_OUTPUT] = unsigned(khdays_ui_image_replacement(s.image));
    w[KSI_ATLAS] = unsigned(atlas);
    w[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
    w[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
    w[KSI_WIDTH] = unsigned(width * s.scale);
    w[KSI_HEIGHT] = unsigned(height * s.scale);
    const float vx = (-left - config.source_origin_x * config.scale) * s.scale;
    const float vy = (-top - config.source_origin_y * config.scale) * s.scale;
    number(KSI_VIEW_X, vx);
    number(KSI_VIEW_Y, vy);
    number(KSI_SCALE, config.scale * s.scale);
    w[KSI_CLEAR] = 1;
    w[KSI_ANCHORED] = 1;
    number(KSI_ANCHOR_X, 8);
    number(KSI_ANCHOR_Y, 4);
    render::encode_sequence_quads(p, background.quads);
    // SpriteMessage: active + clip parameters + message offsets. Days keeps its glyph metrics.
    const auto a = background.active.value_or(seq::SequenceActive{});
    const auto &params = d.clips[background_clip].parameters;
    const float tx = float(a.x) + float(params[0]) + float(d.clips[std::size_t(config.intro)].parameters[0]);
    const float ty = float(a.y) + float(params[1]);
    w[KSI_TEXT_INSERT] = split + 1;
    w[KSI_FACE] = !s.closing && s.text_ready && s.text >= 0 ? unsigned(khdays_ui_image_replacement(s.text)) : ~0u;
    w[KSI_HD_FACE] = ~0u;
    number(KSI_TEXT_SCALE, text_scale);
    number(KSI_FACE_X,
           vx + (tx * config.scale + config.text_offset_x - scroll) * s.scale - s.first_x * text_scale * s.scale);
    number(KSI_FACE_Y, vy + (ty * config.scale + config.text_offset_y) * s.scale - s.first_y * text_scale * s.scale);
    number(KSI_FACE_W, float(s.text_w) * text_scale);
    number(KSI_FACE_H, float(s.text_h) * text_scale);
    number(KSI_TEXT_CLIP_X, vx);
    number(KSI_TEXT_CLIP_Y, vy);
    number(KSI_TEXT_CLIP_W, clip_width * config.scale * s.scale);
    number(KSI_TEXT_CLIP_H, config.clip_height * config.scale * s.scale);
    number(KSI_TEXT_ALPHA, s.closing || (s.intro && !pause_window) ? 0.0f : float(a.color >> 24) / 128.0f);
    auto e = s.anchor;
    e.id = "hud.information";
    e.image = s.image;
    e.slot = KES_DIRECT | KES_FINE | KES_PICTURE | KES_NATIVE_FADE | 1;
    e.x = left + config.offset_x;
    e.y = top + config.offset_y;
    e.w = float(width);
    e.h = float(height);
    khdays_ui_emit_extended(KHDAYS_UI_TEXT, &e, &s.anchor);
}
} // namespace
extern "C" void khdays_ui_information_frame(unsigned frame)
{
    now = frame;
    khdays_situation_take(&context, frame);
    const auto overlay = [](int id) { return (context.overlays & (1ull << id)) != 0; };
    const bool event = context.event_status >= 0 && !(context.event_status & 8) &&
                       (khdays_situation_ran(&context, KHDAYS_UPD_Ov023_SceneRun) || context.pause_mode);
    const bool allowed = context.scene == 2 && overlay(2) && !context.movie && !context.tutorial &&
                         context.camp_screen == KHDAYS_CAMP_NONE && !overlay(8) && !overlay(25) && !overlay(26) &&
                         !overlay(27) && !event && !(context.mission_start >= 9 && context.mission_start <= 11) &&
                         (!held || context.pause_mode);
    if (allowed != context_allowed || (enabled && (windows[0].active || windows[1].active)))
        khdays_ui_changed();
    context_allowed = allowed;
}
extern "C" void khdays_ui_information_message(const unsigned short *text)
{
    observe(1, text, false);
}
extern "C" void khdays_ui_information_panel(const unsigned short *text)
{
    observe(0, text, true);
}
extern "C" void khdays_ui_information_crawl(const unsigned short *text)
{
    observe(0, text, false);
}
extern "C" void khdays_ui_information_reset(void)
{
    ++epoch;
    messages = {};
    held = suspended = false;
    mode_known = false;
    for (auto &s : windows) {
        s.active = s.started = s.closing = s.text_ready = false;
        s.program = {};
        s.message = 0;
        s.previous = now;
    }
    khdays_ui_changed();
    khdays_log("kh2 information: caption context reset (%u)", epoch);
}
extern "C" unsigned khdays_ui_information_epoch(void)
{
    return epoch;
}
extern "C" void khdays_ui_information_hold(int value, int leaving)
{
    held = value != 0;
    if (leaving)
        observe(0, nullptr, false);
    // Days shows its retained lower caption in pause, and briefly again on returning to play.
    if (!leaving && messages[1].present && windows[1].started) {
        ++messages[1].generation;
        messages[1].restored = true;
        khdays_ui_changed();
    }
}
extern "C" void khdays_ui_information_begin(void)
{
    load();
    available = (khdays_ui_information_policy() & 1u) && asset && khdays_textures_hd() &&
                (khdays_host_frame_wants() & (KHDAYS_WANT_2D | KHDAYS_WANT_3D));
    enabled = available && context_allowed;
    const bool single = (khdays_host_graphics_flags() & KHDAYS_HOST_ONE_SCREEN) != 0;
    if (mode_known && single_mode != single && messages[1].present && windows[1].started) {
        ++messages[1].generation;
        messages[1].restored = true;
    }
    mode_known = true;
    single_mode = single;
    if (suspended && enabled) {
        for (unsigned e = 0; e < messages.size(); ++e) {
            auto &m = messages[e];
            const bool unchanged = windows[e].started && windows[e].message == m.generation;
            if (e == 0 && unchanged && (context.mission_start < 1 || context.mission_start > 6)) {
                m.present = false;
                windows[e].active = windows[e].started = false;
                continue;
            }
            if (m.present && unchanged) {
                ++m.generation;
                m.restored = true;
            }
        }
    }
    suspended = !context_allowed;
    for (int engine = 0; engine < 2; ++engine) {
        auto &s = windows[std::size_t(engine)];
        const auto &m = messages[std::size_t(engine)];
        s.seen = false;
        s.coverage = 0;
        s.program = {};
        if (!enabled) {
            s.active = s.started = s.closing = s.text_ready = false;
            s.previous = now;
            continue;
        }
        // Scene_Leave restores the pre-pause VBlank count; this is not a new window.
        if (now < s.previous)
            s.previous = now;
        if (!m.present && !s.started)
            continue;
        // State 1 first retracts the lower panel. The upper window starts opening in state 2.
        if (engine == 0 && m.present && (!s.started || s.message != m.generation) && context.mission_start < 2) {
            s.previous = now;
            continue;
        }
        if (s.image < 0 || s.scale != khdays_ui_scale()) {
            khdays_ui_image_put(s.image);
            s.scale = khdays_ui_scale();
            s.image = khdays_ui_image_get(width * s.scale, height * s.scale);
        }
        if (s.image < 0)
            continue;
        if (m.present && (!s.started || s.message != m.generation)) {
            s.anchor = {"hud.info", engine, 1, 0, 0, 256, 24, s.image};
            open(s, m, m.restored);
            // The upper mission introduction hands the same line to the lower screen.
            if (engine == 1 && (khdays_host_graphics_flags() & KHDAYS_HOST_ONE_SCREEN) && !m.restored &&
                windows[0].started && m.hash == messages[0].hash)
                s.active = false;
        } else if (!m.present)
            close(s);
    }
}
extern "C" int khdays_ui_information_wants(void)
{
    return enabled;
}
extern "C" int khdays_ui_information_configured(void)
{
    load();
    return (khdays_ui_information_policy() & 1u) && asset && khdays_textures_hd();
}
extern "C" int khdays_ui_information_active(int engine)
{
    return enabled && engine >= 0 && engine < 2 && windows[std::size_t(engine)].started;
}
extern "C" int khdays_ui_information_text(const KhdaysUiElement *anchor, const unsigned *rgba, int w, int h,
                                          unsigned version, float first_x, float first_y, unsigned coverage)
{
    if (!enabled || !anchor || !rgba || anchor->screen < 0 || anchor->screen > 1 || w <= 0 || h <= 0)
        return 0;
    auto &s = windows[std::size_t(anchor->screen)];
    if (!s.started)
        return 0;
    if (s.seen && coverage <= s.coverage)
        return 1;
    try {
        const int scale = khdays_ui_scale();
        if (s.text < 0 || s.text_w != w || s.text_h != h) {
            khdays_ui_image_put(s.text);
            s.text = khdays_ui_image_get(w, h);
            s.revision = version - 1;
            s.text_w = w;
            s.text_h = h;
        }
        if (s.image < 0 || s.scale != scale) {
            khdays_ui_image_put(s.image);
            s.image = khdays_ui_image_get(width * scale, height * scale);
            s.scale = scale;
        }
        if (s.image < 0 || s.text < 0)
            return 0;
        if (s.revision != version) {
            auto *out = khdays_ui_image_begin(s.text);
            if (!out)
                return 0;
            std::copy_n(rgba, std::size_t(w) * std::size_t(h), out);
            khdays_ui_image_end(s.text);
            s.revision = version;
            s.ink_right = 0;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (rgba[std::size_t(y) * w + x] >> 24)
                        s.ink_right = std::max(s.ink_right, float(x + 1) / scale);
        }
        s.anchor = *anchor;
        s.first_x = first_x;
        s.first_y = first_y;
        s.text_ready = true;
        s.coverage = coverage;
        s.seen = true;
        return 1;
    } catch (const std::exception &e) {
        disable(e.what());
        return 0;
    }
}
extern "C" void khdays_ui_information_end(void)
{
    if (!enabled)
        return;
    for (int engine = 0; engine < 2; ++engine) {
        auto &s = windows[std::size_t(engine)];
        if (s.active && s.image >= 0) {
            try {
                draw(s);
            } catch (const std::exception &e) {
                disable(e.what());
                return;
            }
        }
        if (!s.active)
            s.previous = now;
        if (!s.started && !(engine == 0 && upper_art_owned))
            continue;
        bool upper_kept = false;
        // Remove only information artwork, never the enemy name or unrelated HUD tiles.
        for (int bg : {1, 3}) {
            KhdaysBgLayer l;
            if (!khdays_bg_text_layer(engine, bg, &l))
                continue;
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 32; ++x) {
                    const int cx = (l.hofs >> 3) + x, cy = (l.vofs >> 3) + y;
                    const unsigned entry = khdays_bg_map_entry(engine, &l, cx, cy);
                    const int bytes = l.colors256 ? 64 : 32;
                    const unsigned address = l.chars + (entry & 1023u) * unsigned(bytes);
                    const char *origin = khdays_bg_origin(engine, address);
                    bool upper_art = false;
                    if (engine == 0) {
                        auto &saved = upper_art_cells[bg == 3 ? 1 : 0][std::size_t(y * 32 + x)];
                        const bool art = origin && std::strstr(origin, "UI/btl/") && std::strstr(origin, "/main.p2");
                        const bool live = s.started && context.mission_start >= 1 && context.mission_start <= 8;
                        const auto *pixels = khdays_bg_memory(engine, address);
                        if (live) {
                            saved.valid = art && pixels;
                            if (saved.valid) {
                                saved.map = l.map;
                                saved.chars = l.chars;
                                saved.entry = entry;
                                saved.cx = cx % l.wt;
                                saved.cy = cy % l.ht;
                                saved.bytes = bytes;
                                std::copy_n(pixels, bytes, saved.pixels.begin());
                            }
                            upper_art = art;
                        } else if (saved.valid) {
                            // VRAM publication can outlive the panel state. Keep only identical old artwork hidden.
                            saved.valid = art && pixels && saved.map == l.map && saved.chars == l.chars &&
                                          saved.entry == entry && saved.cx == cx % l.wt && saved.cy == cy % l.ht &&
                                          saved.bytes == bytes && !std::memcmp(pixels, saved.pixels.data(), bytes);
                            upper_art = saved.valid;
                        }
                        upper_kept |= saved.valid;
                    }
                    if ((s.started && origin && std::strstr(origin, "/bl000.NCGR")) || upper_art)
                        khdays_ui_left_out_cell(KHDAYS_UI_TEXT, engine, bg, cx % l.wt, cy % l.ht);
                }
        }
        if (engine == 0)
            upper_art_owned = upper_kept;
    }
}
extern "C" void khdays_ui_information_commands(KhdaysSequenceImage out[2])
{
    for (int i = 0; i < 2; ++i)
        out[i] = windows[std::size_t(i)].program;
}
extern "C" unsigned khdays_ui_information_hold_frames(void)
{
    return enabled ? unsigned(std::ceil(config.hold_frames)) : 0;
}
extern "C" unsigned khdays_ui_information_state(void)
{
    unsigned state = available ? KHDAYS_INFORMATION_AVAILABLE : 0;
    if (enabled)
        for (unsigned e = 0; e < 2; ++e) {
            if (windows[e].started)
                state |= 1u << e;
            if (windows[e].program.words[KSI_CLEAR])
                state |= 1u << (8 + e);
        }
    return state;
}
