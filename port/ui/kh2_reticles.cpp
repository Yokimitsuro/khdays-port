// Ov022's screen-projected selection markers, drawn through the existing GPU 3D layer.
#include "../host/app/hud_settings.h"
#include "kh2_reticles.h"
#include "kh2_hp_config.h"
#include "../gfx/kh2_sequence.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "../runtime/core/window.h"
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
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>

namespace {
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;
namespace ui = khdays::ui::kh2;
struct Resource {
    render::SequenceAsset asset;
    std::vector<std::uint32_t> pixels;
    int atlas = -1;
};
struct Canvas {
    int token = -1;
    KhdaysSequenceImage commands{};
};
struct Marker {
    ui::ReticleConfig config;
    std::shared_ptr<Resource> resource;
    seq::SequencePlayer player;
    std::array<Canvas, 4> canvases;
    float left = 0, top = 0;
    int width = 0, height = 0;
    float x = 0, y = 0, depth = -1, exit_frame = 0;
    std::array<int, 3> world{};
    unsigned identity = 0, poly = 0, alpha = 31;
    bool seen = false, active = false, leaving = false, restart = false;
    bool has_world = false, visible = false;
};
std::array<Marker, KHDAYS_RETICLE_COUNT> markers;
// The texture registry retains pixel pointers, including atlases whose clips fail validation.
std::map<std::tuple<std::string, std::string, std::string>, std::shared_ptr<Resource>> resources;
bool tried = false, observing = false;
std::array<int, 3> target_world{};
bool have_target = false;
unsigned frame = 0, previous_frame = 0;
constexpr const char *names[]{"soft_lock", "hard_lock", "interact", "chain", "error"};
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
        const auto configs = ui::load_reticle_configs(path(found));
        const auto folder = path(found).parent_path();
        for (int i = 0; i < KHDAYS_RETICLE_COUNT; ++i) {
            auto &m = markers[i];
            m.config = configs[i];
            const auto &c = m.config;
            if (!c.enabled || c.animation < 0)
                continue;
            try {
                const auto key = std::tuple{c.archive, c.entry, c.texture};
                auto &r = resources[key];
                if (!r) {
                    auto loaded = std::make_shared<Resource>();
                    loaded->asset =
                        render::load_sequence_asset(folder / path(c.archive), c.entry, folder / path(c.texture));
                    loaded->pixels.resize(loaded->asset.texture.rgba.size() / 4);
                    std::memcpy(loaded->pixels.data(), loaded->asset.texture.rgba.data(),
                                loaded->asset.texture.rgba.size());
                    const auto id = "kh2:reticle:atlas:" + std::to_string(i);
                    loaded->atlas =
                        khdays_textures_add_info(id.c_str(), loaded->asset.texture.width, loaded->asset.texture.height,
                                                 loaded->pixels.data(), KHDAYS_REPLACEMENT_NO_MIPS);
                    if (loaded->atlas < 0)
                        throw std::runtime_error("could not allocate reticle atlas");
                    r = std::move(loaded);
                }
                float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                for (const int clip : {c.animation, c.end_animation}) {
                    if (clip < 0)
                        continue;
                    const int count = seq::sequence_frame_count(r->asset.sequence, std::size_t(clip));
                    if (count <= 0 || count > 4096)
                        throw std::runtime_error("reticle timeline exceeds 4096 frames");
                    // Fixed bounds include the intro, every loop frame, and the disappearance.
                    for (int t = 0; t < count; ++t) {
                        const auto evaluated = seq::evaluate_sequence(r->asset.sequence, std::size_t(clip), float(t));
                        if (evaluated.quads.size() > KSI_MAX_QUADS)
                            throw std::runtime_error("reticle exceeds the GPU command capacity");
                        for (const auto &q : evaluated.quads)
                            for (const auto &v : q.vertices) {
                                x0 = std::min(x0, (v.x - c.source_origin_x) * c.scale);
                                y0 = std::min(y0, (v.y - c.source_origin_y) * c.scale);
                                x1 = std::max(x1, (v.x - c.source_origin_x) * c.scale);
                                y1 = std::max(y1, (v.y - c.source_origin_y) * c.scale);
                            }
                    }
                }
                m.left = std::floor(x0) - 2;
                m.top = std::floor(y0) - 2;
                const float width = std::ceil(x1) - m.left + 2, height = std::ceil(y1) - m.top + 2;
                if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 || width > 512 ||
                    height > 512)
                    throw std::runtime_error("reticle canvas exceeds 512 screen units");
                m.width = int(width);
                m.height = int(height);
                m.resource = r;
                khdays_log("kh2 reticle %s: %s, clips %d/%d", names[i], found, c.animation, c.end_animation);
            } catch (const std::exception &e) {
                khdays_log("kh2 reticle %s: %s; keeping the Days marker", names[i], e.what());
            }
        }
    } catch (const std::exception &e) {
        khdays_log("kh2 reticles: %s; keeping the Days markers", e.what());
    }
}

bool available()
{
    // The reference DS renderer deliberately excludes mod polygons, as for mod models.
    return khdays_hud_setting(KHDAYS_OPT_RETICLES) && khdays_textures_hd() &&
           (khdays_host_frame_wants() & KHDAYS_WANT_3D) && khdays_link_scene(nullptr) == 2;
}

void draw(Marker &m)
{
    auto &out = m.canvases[std::size_t(khdays_g3d_build_slot())];
    out.token = int(&m - markers.data()) * 4 + khdays_g3d_build_slot();
    const auto &c = m.config;
    const auto evaluated = seq::evaluate_sequence(m.resource->asset.sequence, m.player.clip(), m.player.frame());
    if (evaluated.quads.size() > KSI_MAX_QUADS)
        throw std::runtime_error("reticle exceeds the GPU command capacity");
    auto &program = out.commands;
    auto *w = program.words;
    w[KSI_OUTPUT] = unsigned(out.token);
    w[KSI_ATLAS] = unsigned(m.resource->atlas);
    w[KSI_LOGICAL_W] = unsigned(m.resource->asset.logical_size[0]);
    w[KSI_LOGICAL_H] = unsigned(m.resource->asset.logical_size[1]);
    w[KSI_WIDTH] = unsigned(m.width);
    w[KSI_HEIGHT] = unsigned(m.height);
    const auto number = [w](unsigned at, float v) { std::memcpy(w + at, &v, 4); };
    number(KSI_VIEW_X, -m.left - c.source_origin_x * c.scale);
    number(KSI_VIEW_Y, -m.top - c.source_origin_y * c.scale);
    number(KSI_SCALE, c.scale);
    w[KSI_HD_FACE] = ~0u;
    w[KSI_CLEAR] = 1;
    render::encode_sequence_quads(program, evaluated.quads);
    // The 8x8 UV rectangle maps the retained program's bounds, independent of internal resolution.
    KhdaysG3dClipVertex vertices[4]{};
    constexpr int corners[]{0, 1, 3, 2};
    for (int i = 0; i < 4; ++i) {
        const int corner = corners[i];
        auto &v = vertices[i];
        const float x = m.x + c.offset_x + m.left + float((corner & 1) ? m.width : 0);
        const float y = m.y + c.offset_y + m.top + float((corner & 2) ? m.height : 0);
        v.x = (x / 128 - 1) * 1024;
        v.y = (1 - y / 96) * 1024;
        v.z = m.depth * 1024;
        v.w = 1024;
        v.r = v.g = v.b = 63;
        v.a = 1;
        v.s = (corner & 1) ? 128.0f : 0;
        v.t = (corner & 2) ? 128.0f : 0;
    }
    const unsigned attr = 0xc0u | (m.alpha << 16) | (m.poly << 24);
    khdays_g3d_sequence_polygon(vertices, 4, attr, m.resource->atlas, unsigned(out.token));
}
} // namespace

extern "C" void khdays_reticles_frame(unsigned now)
{
    frame = now;
}
extern "C" void khdays_reticles_begin(void)
{
    khdays_g3d_set_marker(0);
    load();
    observing = available();
    have_target = false;
    for (auto &m : markers) {
        m.seen = false;
        m.restart = false;
        m.visible = false;
        if (!observing || frame < previous_frame || frame - previous_frame > 4)
            m.active = m.leaving = false;
    }
}
extern "C" void khdays_reticles_target(const int *world)
{
    if (observing && world) {
        std::copy_n(world, 3, target_world.begin());
        have_target = true;
    }
}
extern "C" int khdays_reticles_saved_world(int kind, int *world)
{
    if (!observing || !world || kind < 0 || kind >= KHDAYS_RETICLE_COUNT)
        return 0;
    const auto &m = markers[std::size_t(kind)];
    if (!m.active || !m.has_world)
        return 0;
    std::copy(m.world.begin(), m.world.end(), world);
    return 1;
}
extern "C" void khdays_reticles_projected(int kind, int visible, int x, int y)
{
    if (!observing || kind < 0 || kind >= KHDAYS_RETICLE_COUNT)
        return;
    auto &m = markers[std::size_t(kind)];
    m.visible = visible != 0;
    if (m.visible) {
        m.x = float(x) / 4096;
        m.y = float(y) / 4096;
    }
}
extern "C" int khdays_reticles_mark(int kind, unsigned identity, unsigned sprite, int x, int y)
{
    // The existing draw hook also surrounds the PNG/DS fallback when SEQD is unavailable.
    khdays_g3d_set_marker(1);
    if (!observing || kind < 0 || kind >= KHDAYS_RETICLE_COUNT || !khdays_game_ram(sprite) ||
        !khdays_game_ram(sprite + 0x2f))
        return 0;
    auto &m = markers[std::size_t(kind)];
    if (!m.resource)
        return 0;
    const auto *record = static_cast<const unsigned char *>(KHDAYS_W(sprite));
    int depth;
    std::memcpy(&depth, record + 0x20, 4);
    // Ov002_ResetViewToDefault: ortho near=0, far=3, eye at 1024; DrawSpriteQuad clamps depth at 1.
    m.depth = 2 * (float(std::min(depth, 4096)) / 4096) / 3 - 1;
    m.poly = record[0x24] & 63u;
    m.alpha = record[0x2a] & 31u;
    m.restart = !m.active || m.leaving || m.identity != identity;
    m.identity = identity;
    m.x = float(x) / 4096;
    m.y = float(y) / 4096;
    m.has_world = have_target;
    if (have_target)
        m.world = target_world;
    m.visible = true;
    m.seen = true;
    return 1;
}
extern "C" void khdays_reticles_end(int cancel)
{
    khdays_g3d_set_marker(0);
    if (!observing)
        return;
    khdays_g3d_set_marker(1); // Includes exit clips when no native marker was drawn this frame.
    const float ticks = khdays_link_paused() || frame < previous_frame ? 0 : float(frame - previous_frame);
    previous_frame = frame;
    for (int i = 0; i < KHDAYS_RETICLE_COUNT; ++i) {
        auto &m = markers[i];
        if (cancel) {
            m.active = m.leaving = false;
            continue;
        }
        if (!m.resource)
            continue;
        try {
            const auto &d = m.resource->asset.sequence;
            const float delta = ticks * m.config.frames_per_tick;
            if (m.seen) {
                if (m.restart)
                    m.player.select(d, std::size_t(m.config.animation), true);
                else
                    m.player.advance(d, delta);
                m.active = true;
                m.leaving = false;
            } else if (m.active && !m.leaving) {
                if (m.config.end_animation < 0) {
                    m.active = false;
                    continue;
                }
                m.player.select(d, std::size_t(m.config.end_animation), true);
                m.exit_frame = 0;
                m.leaving = true;
            } else if (m.leaving) {
                m.exit_frame += delta;
                if (m.exit_frame >= float(seq::sequence_frame_count(d, m.player.clip()))) {
                    m.active = m.leaving = false;
                    continue;
                }
                m.player.seek(m.exit_frame);
            }
            if (m.active && m.visible)
                draw(m);
        } catch (const std::exception &e) {
            m.active = m.leaving = false;
            m.resource.reset();
            khdays_log("kh2 reticle %s: %s; keeping the Days marker", names[i], e.what());
        }
    }
    observing = false;
    khdays_g3d_set_marker(0);
}
extern "C" void khdays_reticles_commands(const KhdaysG3dList *list, KhdaysSequenceImage *out)
{
    for (int i = 0; i < KHDAYS_RETICLE_COUNT; ++i) {
        out[i] = {};
        for (const auto &c : markers[i].canvases) {
            if (c.token < 0)
                continue;
            for (int p = 0; p < list->npolys; ++p)
                if ((list->polys[p].flags & KHDAYS_G3D_SEQUENCE) && list->polys[p].pltt == unsigned(c.token)) {
                    out[i] = c.commands;
                    break;
                }
        }
    }
}
