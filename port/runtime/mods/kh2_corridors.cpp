// KH2's Dark Corridor in place of Days's standing ovals (kh2_corridors.h).
#include "kh2_corridors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "khdays/assets/dds.h"
#include "khdays/assets/kh2_effect.h"

extern "C" {
#include "../../gfx/g3d.h"
#include "../../host/app/hud_settings.h"
#include "../core/origin.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../scripts/game_link.h"
#include "textures.h"
}
#include "kh2_corridor_fit.h"
#include "model_parts.h" // read_playing: the animations a render object plays
#include "names.h"

namespace {

namespace kh2 = khdays::assets::kh2;
using khdays::kh2_corridor::kClosingClip;
using khdays::kh2_corridor::kDaysOvalTop;
using khdays::kh2_corridor::Matrix;

// The files the KH2 Prizes mod copies from the player's KH2 (tools/mods/kh2_prizes.py): the dark corridor is
// event/effect/tt_event_904.pax's first effect (its block 0, emitter 0: the same block is in the other scenes that
// show it); texcommon.dpd holds the pictures KH2 keeps resident, which its shapes cut. Their HD pictures: one a
// page, in the order of the pages (the pictures' tbp, ascending), as kh2fx/<file's stem>/-<n>.dds.
constexpr const char *kEffect = "kh2fx/tt_event_904.pax";
constexpr const char *kCommon = "kh2fx/texcommon.dpd";

// How many of the game's frames the oval's closing clip has left; -1 when it does not play it.
float closing_left(const void *render_obj)
{
    if (render_obj == nullptr)
        return -1.0F;
    khdays::model_parts::read_playing(render_obj);
    for (const auto &p : khdays::model_parts::playing)
        if (p.name == kClosingClip)
            return std::max(0.0F, p.frames - p.frame);
    return -1.0F;
}

unsigned ticks_per_frame() // KH2 runs 60 times a second; the game's frames are 1, 2 or 3 V-blanks (game_link.h)
{
    return static_cast<unsigned>(std::max(1L, std::lround(khdays_link_frame_seconds() * 60.0)));
}

int trace_level()
{
    static const int level = [] {
        const char *v = std::getenv("KHDAYS_TRACE_CORRIDORS");
        return v != nullptr && *v != '\0' && *v != '0' ? std::max(1, std::atoi(v)) : 0;
    }();
    return level;
}

bool tracing()
{
    return trace_level() > 0;
}

// --------------------------------------------------------------------------------------------- the effect's files
struct HdPage final {
    std::uint16_t tbp = 0, cbp = 0;
    std::string relative; // kh2fx/<stem>/-<n>.dds
};

struct Effect final {
    std::shared_ptr<const kh2::EffectBlock> block;
    kh2::EffectBlock common;
    std::vector<const kh2::EffectPicture *> pictures; // the effect's and the resident ones: the pages' views
    std::vector<HdPage> hd;
    std::map<std::pair<std::uint64_t, bool>, int> pages; // (TEX0, HD) -> the texture added, -1 none
    std::vector<std::unique_ptr<std::vector<std::uint8_t>>> texels; // theirs: textures.h keeps them, never copied
    std::uint32_t close_ticks = 0; // how long KH2's corridor takes to close once told (its run measured at load)
};

std::unique_ptr<Effect> effect;
bool effect_tried = false;

std::vector<std::uint8_t> read_mod_file(const char *relative, std::string &why)
{
    char path[512];
    if (!khdays_mods_find(relative, path, static_cast<int>(sizeof(path)))) {
        why = std::string("no mod has ") + relative;
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty())
        why = std::string(path) + " cannot be read";
    return bytes;
}

// The HD pictures of `pictures`' pages: one a page (a distinct tbp), in ascending order, -<n>.dds under `stem`.
void add_hd_pages(const std::vector<kh2::EffectPicture> &pictures, const std::string &stem, std::vector<HdPage> &out)
{
    std::map<std::uint16_t, std::uint16_t> palette_of; // tbp -> the cbp of its first picture
    for (const auto &p : pictures)
        palette_of.emplace(p.tbp, p.cbp);
    int n = 0;
    for (const auto &[tbp, cbp] : palette_of)
        out.push_back({tbp, cbp, stem + "/-" + std::to_string(n++) + ".dds"});
}

bool load()
{
    if (effect_tried)
        return effect != nullptr;
    effect_tried = true;
    std::string why;
    const auto bytes = read_mod_file(kEffect, why);
    const auto common = bytes.empty() ? std::vector<std::uint8_t>{} : read_mod_file(kCommon, why);
    if (bytes.empty() || common.empty()) {
        khdays_log("kh2 corridors: %s; Days's ovals drawn as they are", why.c_str());
        return false;
    }
    try {
        auto e = std::make_unique<Effect>();
        auto blocks = kh2::read_effect(bytes.data(), bytes.size());
        e->block = std::make_shared<const kh2::EffectBlock>(std::move(blocks.front()));
        e->common = kh2::read_effect_block(common.data(), common.size(), 0);
        kh2::EffectInstance probe(e->block, 0, 0); // its commands all known, or it says which is not
        for (const auto &p : e->block->pictures)
            e->pictures.push_back(&p);
        for (const auto &p : e->common.pictures)
            e->pictures.push_back(&p);
        add_hd_pages(e->block->pictures, "kh2fx/tt_event_904", e->hd);
        add_hd_pages(e->common.pictures, "kh2fx/texcommon", e->hd);
        e->close_ticks = khdays::kh2_corridor::closing_ticks(e->block);
        effect = std::move(e);
        khdays_log("kh2 corridors: KH2's dark corridor read (%zu layers, %zu models, %zu pictures; it closes in %u "
                   "ticks)",
                   effect->block->emitters.front().layers.size(), effect->block->models.size(),
                   effect->pictures.size(), effect->close_ticks);
        return true;
    } catch (const std::exception &e) {
        khdays_log("kh2 corridors: %s cannot be played (%s); Days's ovals drawn as they are", kEffect, e.what());
        return false;
    }
}

// A draw's picture: its TEX0's view of the pages as a texture of the port's; the HD picture of its page when HD
// graphics are on and the mod has one that covers the same view with the same palette; -1 none.
int page(std::uint64_t tex0, bool hd)
{
    const auto key = std::make_pair(tex0, hd);
    if (const auto it = effect->pages.find(key); it != effect->pages.end())
        return it->second;
    const int tbp = static_cast<int>(tex0 & 0x3fffu), cbp = static_cast<int>((tex0 >> 37) & 0x3fffu);
    const int width = 1 << ((tex0 >> 26) & 15u), height = 1 << ((tex0 >> 30) & 15u);
    char name[96];
    std::snprintf(name, sizeof(name), "kh2fx/tt_event_904@%llx%s", static_cast<unsigned long long>(tex0),
                  hd ? "/hd" : "");
    int id = -1;
    for (const HdPage &h : effect->hd) {
        if (!hd || h.tbp != tbp || h.cbp != cbp)
            continue;
        char path[512];
        if (!khdays_mods_find(h.relative.c_str(), path, static_cast<int>(sizeof(path))))
            break;
        try {
            auto t = khdays::assets::read_dds(path);
            // the same view, larger: its fractions name the same places
            if (static_cast<long long>(t.width) * height == static_cast<long long>(t.height) * width) {
                effect->texels.push_back(std::make_unique<std::vector<std::uint8_t>>(std::move(t.rgba)));
                id = khdays_textures_add(name, t.width, t.height, effect->texels.back()->data());
            } else
                khdays_log("kh2 corridors: %s is %d x %d, not the shape of its page's %d x %d view: the original drawn",
                           path, t.width, t.height, width, height);
        } catch (const std::exception &e) {
            khdays_log("kh2 corridors: %s cannot be read (%s): the original drawn", path, e.what());
        }
        break;
    }
    const bool hd_made = id >= 0;
    if (id < 0) {
        kh2::EffectPage p = kh2::effect_page(effect->pictures, tex0);
        effect->texels.push_back(std::make_unique<std::vector<std::uint8_t>>(std::move(p.rgba)));
        id = khdays_textures_add(name, p.width, p.height, effect->texels.back()->data());
    }
    if (tracing())
        khdays_log("kh2 corridors: page %04x (palette %04x, %d x %d) %s", tbp, cbp, width, height,
                   hd_made ? "from its HD picture" : hd ? "original (no HD picture of this view)" : "original");
    effect->pages.emplace(key, id);
    return id;
}

// ------------------------------------------------------------------------------------------------- the matrices
Matrix multiply(const Matrix &a, const Matrix &b) // a, then b
{
    Matrix m{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k)
                sum += a[static_cast<std::size_t>(r * 4 + k)] * b[static_cast<std::size_t>(k * 4 + c)];
            m[static_cast<std::size_t>(r * 4 + c)] = sum;
        }
    return m;
}

Matrix inverse_affine(const Matrix &m) // a 3x3 and a translation (the last row)
{
    const auto at = [&](int r, int c) { return m[static_cast<std::size_t>(r * 4 + c)]; };
    const float det = at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
                      at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
                      at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
    Matrix out{};
    if (std::fabs(det) < 1e-12F) {
        out[0] = out[5] = out[10] = out[15] = 1.0F;
        return out;
    }
    const float d = 1.0F / det;
    out[0] = (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) * d;
    out[1] = (at(0, 2) * at(2, 1) - at(0, 1) * at(2, 2)) * d;
    out[2] = (at(0, 1) * at(1, 2) - at(0, 2) * at(1, 1)) * d;
    out[4] = (at(1, 2) * at(2, 0) - at(1, 0) * at(2, 2)) * d;
    out[5] = (at(0, 0) * at(2, 2) - at(0, 2) * at(2, 0)) * d;
    out[6] = (at(0, 2) * at(1, 0) - at(0, 0) * at(1, 2)) * d;
    out[8] = (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0)) * d;
    out[9] = (at(0, 1) * at(2, 0) - at(0, 0) * at(2, 1)) * d;
    out[10] = (at(0, 0) * at(1, 1) - at(0, 1) * at(1, 0)) * d;
    for (int c = 0; c < 3; ++c)
        out[static_cast<std::size_t>(12 + c)] =
            -(at(3, 0) * out[static_cast<std::size_t>(c)] + at(3, 1) * out[static_cast<std::size_t>(4 + c)] +
              at(3, 2) * out[static_cast<std::size_t>(8 + c)]);
    out[15] = 1.0F;
    return out;
}

bool near_identity(const Matrix &m)
{
    for (int i = 0; i < 16; ++i)
        if (std::fabs(m[static_cast<std::size_t>(i)] - (i % 5 == 0 ? 1.0F : 0.0F)) > 1e-4F)
            return false;
    return true;
}

// Where a corridor is drawn: the oval's model to the view, and the view to the clip space.
struct Place final {
    Matrix view{}, projection{};
};

// The matrices as a model's draw starts: after the 3D engine's Flush the position matrix is the camera times the
// model's place and the projection the projection's; after FlushP the position is the identity and the camera and
// the place are folded into the projection (g3d.h khdays_g3d_base), taken out again here.
Place place_now()
{
    float position[16], vector[16], projection[16], base[16], camera[16];
    khdays_g3d_matrices(position, vector, projection);
    Place p;
    std::copy(position, position + 16, p.view.begin());
    std::copy(projection, projection + 16, p.projection.begin());
    if (khdays_g3d_base(base) == 2 && khdays_g3d_camera(camera) && near_identity(p.view)) {
        Matrix b, c;
        std::copy(base, base + 16, b.begin());
        std::copy(camera, camera + 16, c.begin());
        p.view = multiply(b, c);
        p.projection = multiply(inverse_affine(p.view), p.projection);
    }
    return p;
}

// ------------------------------------------------------------------------------------------------- the corridors
struct Corridor final {
    explicit Corridor(std::uint32_t seed) : fx(effect->block, 0, seed) {}
    kh2::EffectInstance fx;
    std::string name;
    Place place;                 // the oval's last
    Matrix world{};              // the oval's model to the world, for its closing (the camera may move)
    bool in_world = false;       // `world` known (a camera flushed)
    std::vector<kh2::EffectDraw> draws; // its last tick's
    unsigned frame = 0;          // the game's frame of its last tick
    bool started = false;
    unsigned generation = 0;     // the 3D list it was last drawn in
    bool closing = false;        // told to close (its oval's closing clip, or the oval gone)
    float scale = 0.0F;          // the oval's own (traces)
};

Corridor *drawing = nullptr; // the corridor whose oval's draw is under way (its nodes traced)
std::unordered_map<const void *, std::unique_ptr<Corridor>> open; // by the oval's render object
std::vector<std::unique_ptr<Corridor>> closing;                   // their ovals gone: drawn where they were
unsigned generation = 1;                                          // the 3D list being built
std::uint32_t seeds = 0;

// KH2 runs an effect 60 times a second: as many times as the game's frames since the last, each its 1, 2 or 3
// V-blanks (game_link.h). None in the same frame (drawn twice, or the game paused): its last draws again.
void advance(Corridor &c, const kh2::EffectCamera &camera)
{
    const unsigned now = khdays_link_frame();
    const unsigned per_frame = ticks_per_frame();
    unsigned ticks = 1;
    if (c.started)
        ticks = now >= c.frame ? (now - c.frame) * per_frame : per_frame; // the count started again: one frame
    c.started = true;
    c.frame = now;
    for (unsigned i = 0; i < ticks; ++i) {
        const bool last = i + 1 == ticks;
        if (last)
            c.draws.clear();
        c.fx.tick(camera, last ? &c.draws : nullptr);
    }
}

void submit(const Corridor &c, float units)
{
    const bool hd = khdays_textures_hd() != 0;
    for (const kh2::EffectDraw &d : c.draws) {
        if (d.screen) // the screen behind as its picture: not drawn by the port yet
            continue;
        const int id = d.tex0 != 0 ? page(d.tex0, hd) : -1;
        if (d.tex0 != 0 && id < 0)
            continue; // its picture not made: not drawn as bare colour
        const uint32_t flags = (d.depth_write ? KHDAYS_G3D_EFFECT_DEPTH_WRITE : 0u) |
                               (d.depth_only ? KHDAYS_G3D_EFFECT_DEPTH_ONLY : 0u) |
                               (d.depth_test ? 0u : KHDAYS_G3D_EFFECT_NO_DEPTH_TEST);
        for (std::size_t t = 0; t + 2 < d.triangles.size(); t += 3) {
            KhdaysG3dClipVertex tri[3];
            for (std::size_t k = 0; k < 3; ++k) {
                const kh2::EffectVertex &e = d.triangles[t + k];
                KhdaysG3dClipVertex &o = tri[k];
                o.x = e.x * units; // corridor_camera's clip space back to Days's
                o.y = e.y * units;
                o.z = e.z * units;
                o.w = e.w * units;
                o.r = e.r * 63.0F;
                o.g = e.g * 63.0F;
                o.b = e.b * 63.0F;
                o.a = e.a;
                o.s = e.u * 16384.0F; // the page as a mod model's 1024 x 1024 texture
                o.t = e.v * 16384.0F;
            }
            khdays_g3d_effect_polygon(tri, 3, id, d.alpha, flags);
        }
    }
}

void draw_at(Corridor &c, Place p)
{
    khdays::kh2_corridor::face_camera(p.view); // seen from the front, as KH2's
    const float units = khdays::kh2_corridor::corridor_units(p.view);
    const kh2::EffectCamera camera = khdays::kh2_corridor::corridor_camera(p.view, p.projection, units);
    if (trace_level() >= 3 && khdays_link_frame() % 20u == 0u) {
        // KH2's camera as the effect sees it (view, then projection, row vectors), to draw the effect with a
        // reference player from the game's own point of view
        std::string m;
        char one[24];
        for (float v : camera.view) {
            std::snprintf(one, sizeof(one), " %.6g", static_cast<double>(v));
            m += one;
        }
        m += " |";
        for (float v : camera.projection) {
            std::snprintf(one, sizeof(one), " %.6g", static_cast<double>(v));
            m += one;
        }
        khdays_log("kh2 corridors: %s at frame %u: camera units %.6g view+projection%s", c.name.c_str(),
                   khdays_link_frame(), static_cast<double>(units), m.c_str());
    }
    advance(c, camera);
    if (c.fx.done())
        c.draws.clear(); // closed before its oval's end: nothing (the oval stays left out)
    submit(c, units);
}

// The 3D list closes: the corridors whose oval it did not draw close, drawn where it was under the camera now.
void list_closing()
{
    for (auto it = open.begin(); it != open.end();) {
        if (it->second->generation == generation) {
            ++it;
            continue;
        }
        it->second->fx.stop();
        it->second->closing = true;
        if (tracing())
            khdays_log("kh2 corridors: %s gone at frame %u: KH2's corridor closes", it->second->name.c_str(),
                       khdays_link_frame());
        closing.push_back(std::move(it->second));
        it = open.erase(it);
    }
    float camera[16];
    const bool has_camera = khdays_g3d_camera(camera) != 0;
    Matrix now{};
    std::copy(camera, camera + 16, now.begin());
    for (auto it = closing.begin(); it != closing.end();) {
        Corridor &c = **it;
        Place p = c.place;
        if (c.in_world && has_camera)
            p.view = multiply(c.world, now);
        draw_at(c, p);
        if (c.fx.done()) {
            if (tracing())
                khdays_log("kh2 corridors: %s's corridor ended at frame %u", c.name.c_str(), khdays_link_frame());
            it = closing.erase(it);
        } else {
            ++it;
        }
    }
    ++generation;
}

// Whether the model at `resource` is one of the ovals; its name into `name`. By its file and place in it, as the
// models are known (models.cpp), each looked up once.
bool oval(const void *resource, std::string &name)
{
    static std::unordered_map<std::string, std::string> known; // file@offset -> the oval's name, "" another
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    if (!khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run))
        return false;
    char key[200];
    std::snprintf(key, sizeof(key), "%s@%x", file, offset);
    auto it = known.find(key);
    if (it == known.end()) {
        char model[160];
        std::string is;
        if (std::strncmp(file, "mi/ob/", 6) == 0 && khdays_names_model(file, offset, model, sizeof(model))) {
            const char *leaf = std::strrchr(model, '/');
            leaf = leaf != nullptr ? leaf + 1 : model;
            if (std::strcmp(leaf, "E000_xx_000") == 0 || std::strcmp(leaf, "E_100_xx_000") == 0)
                is = model;
        }
        it = known.emplace(key, is).first;
    }
    name = it->second;
    return !name.empty();
}

} // namespace

extern "C" int khdays_kh2_corridors_begin(const void *resource, const void *render_obj)
{
    std::string name;
    if (trace_level() >= 3 && resource != nullptr && khdays_link_frame() % 40u == 0u) {
        // every model's place in the world now and then: where the ground is beside an oval (the characters' feet)
        const char *file = nullptr;
        u32 offset = 0, run = 0;
        float camera[16];
        if (khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run) && khdays_g3d_camera(camera)) {
            Matrix cam;
            std::copy(camera, camera + 16, cam.begin());
            const Matrix w = multiply(place_now().view, inverse_affine(cam));
            khdays_log("kh2 corridors: a model of %s@%x at frame %u, at %.4f %.4f %.4f in the world", file, offset,
                       khdays_link_frame(), static_cast<double>(w[12]), static_cast<double>(w[13]),
                       static_cast<double>(w[14]));
        }
    }
    if (resource == nullptr || !oval(resource, name))
        return 0;
    const void *object = render_obj != nullptr ? render_obj : resource;
    const float left = closing_left(render_obj);
    auto it = open.find(object);
    if (it == open.end()) {
        if (left >= 0.0F || !khdays_hud_setting(KHDAYS_OPT_CORRIDORS) || !load())
            return 0; // Days's own (one already closing when the setting came on stays Days's)
        static bool hooked = false;
        if (!hooked) {
            khdays_g3d_set_swap_hook(list_closing);
            hooked = true;
        }
        auto c = std::make_unique<Corridor>(++seeds);
        c->name = name;
        it = open.emplace(object, std::move(c)).first;
        if (tracing())
            khdays_log("kh2 corridors: %s shown at frame %u: KH2's corridor opens", name.c_str(),
                       khdays_link_frame());
    }
    Corridor &c = *it->second;
    c.generation = generation;
    if (!c.closing && khdays::kh2_corridor::closes_now(left, ticks_per_frame(), effect->close_ticks)) {
        c.closing = true; // as long left of the oval's closing clip as KH2's closing takes: both end together
        c.fx.stop();
        if (tracing())
            khdays_log("kh2 corridors: %s's closing clip has %.0f frames left at frame %u: KH2's corridor closes",
                       name.c_str(), static_cast<double>(left), khdays_link_frame());
    }
    if (trace_level() >= 2 && render_obj != nullptr) {
        std::string now, which; // with the frames; the names alone (said when they change) -- read_playing's
        char one[160];
        for (const auto &p : khdays::model_parts::playing) {
            std::snprintf(one, sizeof(one), " joints %s frame %.1f of %.0f", p.name.c_str(), static_cast<double>(p.frame),
                          static_cast<double>(p.frames));
            now += one;
            which += " " + p.name;
        }
        for (const auto &m : khdays::model_parts::materials_playing) {
            now += " materials " + m;
            which += " " + m;
        }
        static std::string said;
        if (which != said || khdays_link_frame() % 10u == 0u)
            khdays_log("kh2 corridors: %s at frame %u plays%s", name.c_str(), khdays_link_frame(), now.c_str());
        said = which;
    }
    c.place = place_now();
    float camera[16];
    if (khdays_g3d_camera(camera)) {
        Matrix cam;
        std::copy(camera, camera + 16, cam.begin());
        c.world = multiply(c.place.view, inverse_affine(cam));
        c.in_world = true;
    }
    if (tracing()) {
        Place p = c.place;
        const float scale = khdays::kh2_corridor::corridor_units(p.view) / khdays::kh2_corridor::kDaysPerKh2;
        if (std::fabs(scale - c.scale) > 0.01F * std::max(scale, c.scale) ||
            (trace_level() >= 2 && khdays_link_frame() % 10u == 0u)) {
            // where the oval's floor and its model's top fall on the screen (-1..1, y up): what its fit is checked
            // against on a capture
            const Matrix to_clip = multiply(c.place.view, c.place.projection);
            const auto screen = [&](float x, float y) {
                const float w = x * to_clip[3] + y * to_clip[7] + to_clip[15];
                return std::array<float, 2>{(x * to_clip[0] + y * to_clip[4] + to_clip[12]) / w,
                                            (x * to_clip[1] + y * to_clip[5] + to_clip[13]) / w};
            };
            const auto floor = screen(0, 0), top = screen(0, kDaysOvalTop), side = screen(1, 0);
            if (trace_level() >= 2) {
                std::string heights;
                char one[48];
                for (int y = 0; y <= 80; y += 5) {
                    std::snprintf(one, sizeof(one), " %.1f:%.4f", y / 10.0, static_cast<double>(screen(0, y / 10.0F)[1]));
                    heights += one;
                }
                khdays_log("kh2 corridors: %s on the screen at its heights (model y: screen y)%s", name.c_str(),
                           heights.c_str());
            }
            khdays_log("kh2 corridors: %s at frame %u: drawn at scale %.4f, place %.3f %.3f %.3f; on the screen its "
                       "floor %.4f %.4f, its top %.4f %.4f, a unit across %.4f",
                       name.c_str(), khdays_link_frame(), static_cast<double>(scale), static_cast<double>(c.world[12]),
                       static_cast<double>(c.world[13]), static_cast<double>(c.world[14]),
                       static_cast<double>(floor[0]), static_cast<double>(floor[1]), static_cast<double>(top[0]),
                       static_cast<double>(top[1]), static_cast<double>(side[0] - floor[0]));
        }
        c.scale = scale;
    }
    draw_at(c, c.place);
    drawing = &c;
    return 1;
}

extern "C" void khdays_kh2_corridors_shape(void)
{
    if (trace_level() < 2 || drawing == nullptr)
        return;
    KhdaysG3dMaterial m;
    khdays_g3d_material(&m);
    static unsigned said_frame = 0, said_alpha = 99;
    const unsigned frame = khdays_link_frame(), alpha = (m.attr >> 16) & 31u;
    if (frame != said_frame || alpha != said_alpha)
        khdays_log("kh2 corridors: %s at frame %u: a shape at alpha %u", drawing->name.c_str(), frame, alpha);
    said_frame = frame;
    said_alpha = alpha;
}

extern "C" void khdays_kh2_corridors_node(unsigned node)
{
    if (trace_level() < 2 || drawing == nullptr || open.empty())
        return;
    const unsigned frame = khdays_link_frame();
    float position[16], vector[16], projection[16];
    khdays_g3d_matrices(position, vector, projection);
    Matrix now;
    std::copy(position, position + 16, now.begin());
    const Matrix in_oval = multiply(now, inverse_affine(drawing->place.view)); // the node's place under the oval's
    {
        // what changes as Days hides its oval: each node's size and the material's alpha, said when they change
        KhdaysG3dMaterial m;
        khdays_g3d_material(&m);
        float size = 0.0F;
        for (int r = 0; r < 3; ++r)
            size += std::sqrt(in_oval[static_cast<std::size_t>(r * 4)] * in_oval[static_cast<std::size_t>(r * 4)] +
                              in_oval[static_cast<std::size_t>(r * 4 + 1)] * in_oval[static_cast<std::size_t>(r * 4 + 1)] +
                              in_oval[static_cast<std::size_t>(r * 4 + 2)] * in_oval[static_cast<std::size_t>(r * 4 + 2)]) /
                    3.0F;
        static std::map<unsigned, std::pair<int, unsigned>> said; // node -> size in 1/100, alpha
        const std::pair<int, unsigned> now_said{static_cast<int>(std::lround(size * 100.0F)), (m.attr >> 16) & 31u};
        if (said[node] != now_said)
            khdays_log("kh2 corridors: %s at frame %u: node %u size %.2f, material alpha %u", drawing->name.c_str(),
                       frame, node, static_cast<double>(size), now_said.second);
        said[node] = now_said;
    }
    if (frame % 30u != 0u)
        return;
    std::string row;
    char one[24];
    for (int i = 0; i < 16; ++i) {
        if (i % 4 == 3)
            continue;
        std::snprintf(one, sizeof(one), " %.4f", static_cast<double>(in_oval[static_cast<std::size_t>(i)]));
        row += one;
    }
    khdays_log("kh2 corridors: %s at frame %u: node %u%s", drawing->name.c_str(), frame, node, row.c_str());
}
