// Ov002's counter: Days supplies values/events; replacement animation lives entirely in SEQD.
#include "../host/app/hud_settings.h"
#include "heart_points.h"
#include "kh2_hp_config.h"
#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../runtime/mods/textures.h"
#include "../runtime/scripts/game_link.h"
#include "../runtime/display/frame_package.h"
#include "../host/app/host.h"
#include "../host/screens/ui_text.h"
int khdays_settings_game_language(void);
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;
namespace ui = khdays::ui::kh2;
struct Track {
    seq::SequencePlayer player;
    bool active = false;
    int id = -1;
    bool pending = false;
};
struct Row {
    Track motion;
    unsigned value = 0;
    bool bonus = false;
    std::array<Track, 11> glyphs;
};
struct Canvas {
    unsigned token = ~0u;
    KhdaysSequenceImage commands{};
};
ui::HeartPointsConfig config;
std::unique_ptr<render::SequenceAsset> asset;
std::vector<std::uint32_t> pixels;
std::array<std::array<Canvas, KHDAYS_HEART_PROGRAMS>, 4> canvases;
std::array<Row, 10> rows;
Track heart, pickup, streak, bonus, chain, total, heart_grow, background;
std::array<Track, 11> total_glyphs;
std::array<Track, 3> particles;
const unsigned char *context = nullptr;
const void *previous_context = nullptr;
unsigned previous_frame = 0, current_frame = 0;
int previous_total = 0, heart_state = -1, chain_state = 0, atlas = -1;
bool tried = false, observing = false, body_seen = false, total_seen = false, chain_seen = false;
bool pickup_requested = false, chain_refreshed = false, chain_was_fading = false;
std::uint64_t chain_fade_stamp = 0;
std::vector<seq::SequenceQuad> quads;

int read(int at)
{
    int result;
    std::memcpy(&result, context + at, 4);
    return result;
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
        const auto *language = khdays_language_code(khdays_settings_game_language());
        std::snprintf(name, sizeof(name), "layouts/gauge/%s/gauge.json", language && *language ? language : "en");
        if (!khdays_mods_find(name, found, sizeof(found)) &&
            !khdays_mods_find("layouts/gauge/en/gauge.json", found, sizeof(found)))
            return;
        config = ui::load_heart_points_config(path(found));
        if (!config.enabled)
            return;
        const auto folder = path(found).parent_path();
        auto loaded = std::make_unique<render::SequenceAsset>(
            render::load_sequence_asset(folder / path(config.archive), config.entry, folder / path(config.texture)));
        for (const auto clip : config.animations) {
            if (clip < 0)
                continue;
            const int frames = seq::sequence_frame_count(loaded->sequence, std::size_t(clip));
            if (frames <= 0 || frames > 4096)
                throw std::runtime_error("heart counter timeline exceeds 4096 frames");
            for (int frame = 0; frame < frames; ++frame) {
                const auto evaluated = seq::evaluate_sequence(loaded->sequence, std::size_t(clip), float(frame));
                if (evaluated.quads.size() > 4)
                    throw std::runtime_error("heart counter clip exceeds four quads per frame");
                for (const auto &q : evaluated.quads)
                    for (const auto &v : q.vertices)
                        if (!std::isfinite(v.x) || !std::isfinite(v.y) || std::abs(v.x) > 4096 || std::abs(v.y) > 4096)
                            throw std::runtime_error("heart counter clip exceeds 4096 layout units");
            }
        }
        pixels.resize(loaded->texture.rgba.size() / 4);
        std::memcpy(pixels.data(), loaded->texture.rgba.data(), loaded->texture.rgba.size());
        atlas = khdays_textures_add_info("sequence:heart:atlas", loaded->texture.width, loaded->texture.height,
                                         pixels.data(), KHDAYS_REPLACEMENT_NO_MIPS);
        if (atlas < 0)
            throw std::runtime_error("could not allocate heart counter atlas");
        asset = std::move(loaded);
        khdays_log("heart counter: %s", found);
    } catch (const std::exception &e) {
        khdays_log("heart counter: %s; keeping the Days counter", e.what());
    }
}
void select(Track &t, int index)
{
    const int clip = config.animations[std::size_t(index)];
    t.id = index;
    t.pending = false;
    t.active = clip >= 0;
    if (t.active)
        t.player.select(asset->sequence, std::size_t(clip), true);
}
void advance(Track &t, float dt)
{
    if (t.active) {
        t.player.advance(asset->sequence, dt);
        t.active = t.player.playing();
        if (!t.active && t.pending)
            select(t, t.id);
    }
}
seq::SequencePose motion(const Track &t, bool hold = false)
{
    seq::SequencePose pose;
    if (t.id < 0 || (!t.active && !hold))
        return pose;
    const auto frame = seq::evaluate_sequence(asset->sequence, t.player.clip(), t.player.frame());
    if (frame.active) {
        const auto &a = *frame.active;
        pose.x = float(a.x);
        pose.y = float(a.y);
        pose.scale_x = a.scale_x;
        pose.scale_y = a.scale_y;
        for (unsigned i = 0; i < 4; ++i)
            pose.color_rate[i] = float((a.color >> (8 * i)) & 255u) / 128;
    }
    return pose;
}
void append(int clip, float frame, float x = 0, float y = 0, const seq::SequencePose &pose = {})
{
    if (clip < 0)
        return;
    auto evaluated = seq::evaluate_sequence(asset->sequence, std::size_t(clip), frame, pose);
    if (quads.size() + evaluated.quads.size() > KHDAYS_HEART_PROGRAMS * KSI_MAX_QUADS)
        throw std::runtime_error("heart counter exceeds GPU command capacity");
    for (auto &q : evaluated.quads) {
        for (auto &v : q.vertices) {
            // The generated DS atlas uses texel boundaries; SEQD otherwise adds its half-texel bias.
            v.x += x - .5f;
            v.y += y - .5f;
            v.u -= .5f;
            v.v -= .5f;
        }
        quads.push_back(q);
    }
}
void append(const Track &track)
{
    if (track.active)
        append(int(track.player.clip()), track.player.frame());
}
void number(unsigned value, bool small, bool sign, bool loss, std::array<Track, 11> &glyphs,
            const seq::SequencePose &pose = {})
{
    unsigned n = value, count = 1;
    while (n >= 10) {
        n /= 10;
        ++count;
    }
    for (unsigned i = 0; i < count; ++i) {
        const int index = (small ? 22 : 12) + int(value % 10);
        value /= 10;
        const float x = (small ? config.rows_x : config.total_x) +
                        float(count - i) * (small ? config.row_spacing : config.digit_spacing);
        const float y = small ? config.rows_y : config.total_y;
        auto &glyph = glyphs[i];
        const auto clip = config.animations[std::size_t(index)];
        if (clip >= 0) {
            if (glyph.id != index)
                select(glyph, index);
            append(clip, glyph.player.frame(), x / .65f, y / .65f, pose);
        }
    }
    if (sign) {
        auto &glyph = glyphs[count];
        const int index = loss ? 32 : 33;
        const int clip = config.animations[std::size_t(index)];
        if (clip >= 0) {
            if (glyph.id != index)
                select(glyph, index);
            append(clip, glyph.player.frame(), config.rows_x / .65f, config.rows_y / .65f, pose);
        }
    }
}
void submit()
{
    if (quads.empty())
        return;
    float left = 0, top = 0, right = 0, bottom = 0;
    for (const auto &q : quads)
        for (const auto &v : q.vertices) {
            left = std::min(left, (v.x - config.source_origin_x) * config.scale);
            top = std::min(top, (v.y - config.source_origin_y) * config.scale);
            right = std::max(right, (v.x - config.source_origin_x) * config.scale);
            bottom = std::max(bottom, (v.y - config.source_origin_y) * config.scale);
        }
    left = std::floor(left) - 1;
    top = std::floor(top) - 1;
    const float width = std::ceil(right) - left + 1, height = std::ceil(bottom) - top + 1;
    if (width > 8192 || height > 8192)
        throw std::runtime_error("heart counter canvas exceeds 8192 units");
    const int slot = khdays_g3d_build_slot();
    for (std::size_t first = 0, chunk = 0; first < quads.size(); first += KSI_MAX_QUADS, ++chunk) {
        auto &canvas = canvases[std::size_t(slot)][chunk];
        canvas.commands = {};
        canvas.token = unsigned((5 + chunk) * 4 + std::size_t(slot));
        auto &program = canvas.commands;
        auto *w = program.words;
        w[KSI_OUTPUT] = canvas.token;
        w[KSI_ATLAS] = unsigned(atlas);
        w[KSI_LOGICAL_W] = unsigned(asset->logical_size[0]);
        w[KSI_LOGICAL_H] = unsigned(asset->logical_size[1]);
        w[KSI_WIDTH] = unsigned(width);
        w[KSI_HEIGHT] = unsigned(height);
        w[KSI_HD_FACE] = ~0u;
        w[KSI_CLEAR] = 1;
        w[KSI_ANCHORED] = 1;
        const auto put = [w](unsigned at, float value) { std::memcpy(w + at, &value, 4); };
        put(KSI_VIEW_X, -left - config.source_origin_x * config.scale);
        put(KSI_VIEW_Y, -top - config.source_origin_y * config.scale);
        put(KSI_SCALE, config.scale);
        put(KSI_ANCHOR_X, 20);
        put(KSI_ANCHOR_Y, 24);
        render::encode_sequence_quads(
            program, std::span(quads).subspan(first, std::min<std::size_t>(KSI_MAX_QUADS, quads.size() - first)));
        const float x0 = (left + config.offset_x) / 128 - 1;
        const float x1 = (left + config.offset_x + width) / 128 - 1;
        const float y0 = 1 - (top + config.offset_y) / 96;
        const float y1 = 1 - (top + config.offset_y + height) / 96;
        // Explicit corners avoid an MSVC /O2 loop optimization that collapsed the Y extent.
        const KhdaysG3dClipVertex vertices[4]{{x0, y0, -.95f, 1, 63, 63, 63, 0, 0, 1},
                                              {x1, y0, -.95f, 1, 63, 63, 63, 128, 0, 1},
                                              {x1, y1, -.95f, 1, 63, 63, 63, 128, 128, 1},
                                              {x0, y1, -.95f, 1, 63, 63, 63, 0, 128, 1}};
        khdays_g3d_sequence_polygon(vertices, 4, 0xc0u | (31u << 16) | (0x3bu << 24), atlas, canvas.token);
    }
}
} // namespace

extern "C" void khdays_heart_begin(const void *owner)
{
    load();
    observing = khdays_hud_setting(KHDAYS_OPT_HEART_POINTS) && asset && khdays_textures_hd() &&
                (khdays_host_frame_wants() & KHDAYS_WANT_3D) && khdays_link_scene(nullptr) == 2;
    context = observing ? static_cast<const unsigned char *>(owner) : nullptr;
    if (!observing)
        return;
    current_frame = khdays_link_frame();
    const bool reset =
        owner != previous_context || current_frame < previous_frame || current_frame - previous_frame > 4;
    const float dt = reset || khdays_link_paused() ? 0 : float(current_frame - previous_frame) * config.frames_per_tick;
    if (reset) {
        heart = pickup = streak = bonus = chain = total = heart_grow = background = {};
        select(background, 3);
        particles = {};
        rows = {};
        total_glyphs = {};
        heart_state = -1;
        chain_state = 0;
        pickup_requested = false;
        chain_fade_stamp = 0;
        chain_was_fading = false;
        previous_total = read(0xd60);
        if (read(0x48) != 0)
            select(pickup, 2);
    }
    previous_context = owner;
    previous_frame = current_frame;
    // Pickups are armed after the native widget draw and first appear on the next update.
    if (pickup_requested) {
        select(pickup, 2);
        pickup_requested = false;
    } else if (pickup.active) {
        const auto &clip = asset->sequence.clips[pickup.player.clip()];
        const bool wrapped =
            pickup.player.frame() + dt >= float(seq::sequence_frame_count(asset->sequence, pickup.player.clip()));
        if (clip.loop == 0 && wrapped && read(0x48) == 0)
            pickup.active = false;
        else
            advance(pickup, dt);
    }
    chain_refreshed = read(0xcc) != 0;
    for (auto *track : {&heart, &streak, &bonus, &chain, &total, &heart_grow, &background})
        advance(*track, dt);
    for (auto &p : particles)
        advance(p, dt);
    for (auto &g : total_glyphs)
        advance(g, dt);
    for (auto &r : rows) {
        advance(r.motion, dt);
        for (auto &g : r.glyphs)
            advance(g, dt);
    }
    body_seen = total_seen = chain_seen = false;
}
extern "C" int khdays_heart_node(const void *node)
{
    if (!observing)
        return 0;
    // Match exact nodes, not resources: the countdown and world markers share the digit sheets.
    for (const int offset : {0xe8, 0x1f0, 0x2f8, 0x400, 0x508, 0x8cc, 0x9d4, 0xdc0, 0xec8})
        if (node == context + offset) {
            body_seen |= offset == 0xe8;
            total_seen |= offset == 0x8cc;
            chain_seen |= offset == 0xdc0;
            return 1;
        }
    return 0;
}
extern "C" void khdays_heart_event(int kind, int row)
{
    if (!observing)
        return;
    if (kind == 0)
        pickup_requested = true;
    else if (kind == 1 && row >= 0 && row < int(rows.size())) {
        auto &r = rows[std::size_t(row)];
        r.value = unsigned(std::max(0, read(0xd90 + row * 4)));
        r.bonus = read(0x7c + row * 4) != 0;
        r.glyphs = {};
        select(r.motion, r.bonus ? 35 : 34);
    } else if (kind == 2) {
        select(bonus, 11);
        const auto arm = [](Track &track, int index) {
            if (track.active)
                track.pending = true;
            else
                select(track, index);
        };
        if (read(0x7c4) <= 0)
            arm(streak, 4);
        for (int i = 0; i < 3; ++i) {
            short step;
            std::memcpy(&step, context + 0x8c4 + 2 * i, sizeof(step));
            if (step <= 0)
                arm(particles[std::size_t(i)], 5 + i);
        }
    }
}
extern "C" void khdays_heart_end(void)
{
    if (!observing)
        return;
    try {
        quads.clear();
        if (body_seen) {
            const int state = read(0xe4) != 0 ? 1 : 0;
            if (state != heart_state) {
                select(heart, state);
                heart_state = state;
            }
            append(background);
            append(streak);
            for (const auto &p : particles)
                append(p);
            if (heart.active)
                append(int(heart.player.clip()), heart.player.frame(), 0, 0, motion(heart_grow, true));
            append(pickup);
        }
        if (total_seen) {
            const int value = std::max(0, read(0xd60));
            if (value != previous_total) {
                const int change = value - previous_total;
                if (change > 0 && read(0xd58) == 1) {
                    const bool final = value == read(0xd64) && config.animations[41] >= 0;
                    select(total, final ? 41 : change == 1 ? 36 : change == 2 ? 38 : 39);
                    if (!heart_grow.active)
                        select(heart_grow, 40);
                } else {
                    select(total, 37);
                }
            }
            previous_total = value;
            number(unsigned(value), false, false, false, total_glyphs, motion(total));
            for (auto &r : rows)
                if (r.motion.active)
                    number(r.value, true, true, false, r.glyphs, motion(r.motion));
        }
        const bool fading = read(0xd0) != 0;
        std::uint64_t fade_stamp;
        std::memcpy(&fade_stamp, context + 0xfe0, sizeof(fade_stamp));
        const bool fade_restarted = chain_seen && fading && fade_stamp != chain_fade_stamp;
        bool hide_chain = false;
        if (chain_seen && fading) {
            if (chain_state != 5 || fade_restarted) {
                select(chain, config.animations[42] >= 0 ? 42 : 10);
                chain_state = 5;
                // A finished native fade draws zero before its restarted tween becomes visible.
                hide_chain = chain_was_fading && !chain_refreshed;
            }
        } else if (chain_seen && chain_state != 1 && chain_state != 2) {
            select(chain, 8);
            chain_state = 1;
        } else if (!chain_seen && (chain_state == 1 || chain_state == 2 || chain_state == 5)) {
            select(chain, 10);
            chain_state = 3;
        }
        if (!chain.active && chain_state == 1) {
            select(chain, chain_seen ? 9 : 10);
            chain_state = chain_seen ? 2 : 3;
        } else if (!chain.active && chain_state == 3)
            chain_state = 0;
        chain_fade_stamp = fade_stamp;
        chain_was_fading = fading;
        if (!hide_chain)
            append(chain);
        append(bonus);
        submit();
    } catch (const std::exception &e) {
        khdays_log("heart counter: %s; reverting to Days on the next frame", e.what());
        asset.reset();
    }
    observing = false;
    context = nullptr;
}
extern "C" void khdays_heart_commands(const KhdaysG3dList *list, KhdaysSequenceImage *out)
{
    for (int i = 0; i < KHDAYS_HEART_PROGRAMS; ++i) {
        out[i] = {};
        for (const auto &slot : canvases)
            for (int p = 0; p < list->npolys; ++p)
                if ((list->polys[p].flags & KHDAYS_G3D_SEQUENCE) && list->polys[p].pltt == slot[std::size_t(i)].token) {
                    out[i] = slot[std::size_t(i)].commands;
                    break;
                }
    }
}
