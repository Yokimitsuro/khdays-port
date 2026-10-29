// Secondary motion for the mods' models (model_link.h). The solver's space is Days' world (y up)
// divided by the actor's world units per model unit, so a KH2 model simulates in its own metres
// (KH2 centimetres / 100) whatever the game's scale; the camera only converts, it never moves cloth.

#include "model_link.h"

#include "actor.h"
#include "days_environment.h"

#include "khdays/assets/model_import.h"

extern "C" {
#include "../../../gfx/g3d.h"
#include "../../core/events.h" // khdays_events_vblank_count
#include "../../core/window.h" // KHDAYS_W, khdays_game_ram
#include "../../core/session_log.h"
#include "../../scripts/game_link.h" // the game's frame count and rate
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <system_error>

namespace khdays::secondary {

namespace {

bool flag(const char *name)
{
    const char *v = std::getenv(name);
    return v != nullptr && *v != '\0' && *v != '0';
}

void say(const std::string &text)
{
    std::fprintf(stderr, "secondary: %s\n", text.c_str());
    khdays_log("secondary: %s", text.c_str());
}

std::filesystem::path u8(const std::string &path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

// The game's clock: its object frames (Obj_UpdateAll counts the unpaused ones) times the frame's
// length at the game's frame-rate mode; a jump backwards or of more than ten seconds is a new epoch.
struct GameClock {
    bool started = false;
    unsigned frame = 0;
    u32 vblank = 0;
    double seconds = 0.0;
    std::uint64_t epoch = 1;
    void update()
    {
        const u32 vb = khdays_events_vblank_count();
        if (started && vb == vblank)
            return;
        vblank = vb;
        const unsigned f = khdays_link_frame();
        const double period = khdays_link_frame_seconds();
        if (!started) {
            started = true;
        } else {
            const long long d = static_cast<long long>(static_cast<int>(f - frame));
            if (d < 0 || static_cast<double>(d) * period > 10.0) {
                ++epoch; // a save state, a reload: what was simulated no longer follows
                seconds += period;
            } else {
                seconds += static_cast<double>(d) * period;
            }
        }
        frame = f;
    }
};
GameClock clock_;

// A field actor's world matrix: Ov002_SubmitActorDraw flushes the camera, multiplies the actor's own
// MtxFx43 at actor +0x10c onto it (MTX_MULT_4x3) and draws the render object at actor +0x24, live
// (+0 bit 0) and registered (bit 3). Column-major, Days' world units; false when it is not one.
bool actor_world(const void *render_object, Mat4 &out)
{
    if (render_object == nullptr)
        return false;
    const KhdaysRef ref = KHDAYS_REF(render_object);
    if (ref < 0x24U)
        return false;
    const KhdaysRef actor = ref - 0x24U;
    if (!khdays_game_ram(actor) || !khdays_game_ram(actor + 0x10cU + 47U))
        return false;
    const auto *bytes = static_cast<const unsigned char *>(KHDAYS_W(actor));
    if ((bytes[0] & 9U) != 9U)
        return false;
    std::int32_t w[12];
    std::memcpy(w, bytes + 0x10c, sizeof(w));
    out = kIdentity4;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 3; ++r)
            out[static_cast<std::size_t>(c * 4 + r)] = static_cast<float>(w[c * 3 + r]) / 4096.0F;
    for (int c = 0; c < 3; ++c) { // a placement: each axis of a sane size, not degenerate
        const float l = length(Vec3{out[static_cast<std::size_t>(c * 4)], out[static_cast<std::size_t>(c * 4 + 1)],
                                    out[static_cast<std::size_t>(c * 4 + 2)]});
        if (!(l > 1e-3F && l < 1e3F))
            return false;
    }
    return std::isfinite(out[12]) && std::isfinite(out[13]) && std::isfinite(out[14]);
}
DaysEnvironment room_;
u32 room_vblank_ = 0;
bool room_read_ = false;

} // namespace

struct ModelSecondary {
    std::string name, file;
    SkeletonDesc skeleton;
    MeshDesc mesh;
    std::vector<int> joint_of_bone;
    std::vector<Mat4> inverse_bind, bind_inverse; // per bone
    std::shared_ptr<const Rig> rig;
    std::vector<std::filesystem::file_time_type> stamps;
    u32 checked = 0, swept = 0, reported = 0;
    bool debug = false, profile = false;
    bool said_placement = false, said_camera = false;
    struct ActorState {
        std::unique_ptr<Actor> actor;
        u32 seen = 0;
        float units = 0.0F;
    };
    std::map<const void *, ActorState> actors; // by render object; dropped when unseen (sweep)
    std::vector<Frame> pose, out;
    std::vector<std::uint8_t> written;
};

void ModelSecondaryDeleter::operator()(ModelSecondary *m) const
{
    delete m;
}

namespace {

std::shared_ptr<const Rig> cook_file(ModelSecondary &m)
{
    Diagnostics d;
    const Config config = read_config(u8(m.file), d);
    std::shared_ptr<Rig> rig;
    if (!has_errors(d)) {
        auto cooked = std::make_shared<Rig>(cook(config, m.skeleton, m.mesh, d));
        if (!has_errors(d)) {
            prepare_references(*cooked, &d);
            rig = std::move(cooked);
        }
    }
    for (const auto &x : d)
        say(m.name + ": " + format(x));
    m.stamps.clear();
    std::error_code ec;
    for (const auto &f : config.files)
        m.stamps.push_back(std::filesystem::last_write_time(f, ec));
    if (!rig) {
        say(m.name + ": " + m.file + " has errors; the model is drawn without secondary motion until it is fixed");
        return nullptr;
    }
    say(m.name + ": " + rig->summary);
    m.debug = rig->config.debug || flag("KHDAYS_SECONDARY_DEBUG");
    m.profile = rig->config.profile || flag("KHDAYS_SECONDARY_PROFILE");
    return rig;
}

// the configuration's files changed: cooked again; its actors start afresh on the new rig
void reload(ModelSecondary &m)
{
    if (!m.rig && m.stamps.empty())
        return;
    std::error_code ec;
    const Config *cfg = m.rig ? &m.rig->config : nullptr;
    bool changed = false;
    if (cfg != nullptr) {
        for (std::size_t i = 0; i < cfg->files.size() && i < m.stamps.size(); ++i)
            changed = changed || std::filesystem::last_write_time(cfg->files[i], ec) != m.stamps[i];
    } else {
        changed = !m.stamps.empty() && std::filesystem::last_write_time(u8(m.file), ec) != m.stamps[0];
    }
    if (!changed)
        return;
    say(m.name + ": " + m.file + " changed; reloading it");
    auto rig = cook_file(m);
    if (rig) {
        m.rig = std::move(rig);
        m.actors.clear();
    }
}

// --- The debug drawing: particles, links, anchors and colliders over the model ----------------------

struct Overlay {
    const Mat4 &view;
    const float *proj;
    float units;
    KhdaysG3dClipVertex clip(Vec3 sim) const
    {
        const Vec3 v = point(view, sim * units);
        KhdaysG3dClipVertex o{};
        o.x = proj[0] * v.x + proj[4] * v.y + proj[8] * v.z + proj[12];
        o.y = proj[1] * v.x + proj[5] * v.y + proj[9] * v.z + proj[13];
        o.z = proj[2] * v.x + proj[6] * v.y + proj[10] * v.z + proj[14];
        o.w = proj[3] * v.x + proj[7] * v.y + proj[11] * v.z + proj[15];
        o.z -= 0.02F * o.w; // drawn over the model
        o.a = 1.0F;
        return o;
    }
    void quad(KhdaysG3dClipVertex a, KhdaysG3dClipVertex b, Vec3 rgb, float width) const
    {
        if (a.w <= 0.0F || b.w <= 0.0F)
            return;
        // a band `width` (in the view's -1..1 units) across the screen direction of a-b
        const float ax = a.x / a.w, ay = a.y / a.w, bx = b.x / b.w, by = b.y / b.w;
        const float dx = bx - ax, dy = by - ay, l = std::sqrt(dx * dx + dy * dy);
        if (l < 1e-6F)
            return;
        const float nx = -dy / l * width, ny = dx / l * width;
        KhdaysG3dClipVertex v[4] = {a, b, b, a};
        v[0].x += nx * a.w;
        v[0].y += ny * a.w;
        v[1].x += nx * b.w;
        v[1].y += ny * b.w;
        v[2].x -= nx * b.w;
        v[2].y -= ny * b.w;
        v[3].x -= nx * a.w;
        v[3].y -= ny * a.w;
        for (auto &x : v) {
            x.r = rgb.x * 63.0F;
            x.g = rgb.y * 63.0F;
            x.b = rgb.z * 63.0F;
        }
        const std::uint32_t attr = (31U << 16) | 0xc0U; // opaque, both sides
        KhdaysG3dClipVertex t0[3] = {v[0], v[1], v[2]}, t1[3] = {v[0], v[2], v[3]};
        khdays_g3d_mod_polygon(t0, 3, attr, 0, -1, 0, 0);
        khdays_g3d_mod_polygon(t1, 3, attr, 0, -1, 0, 0);
    }
    void line(Vec3 a, Vec3 b, Vec3 rgb, float width = 0.004F) const
    {
        quad(clip(a), clip(b), rgb, width);
    }
    void dot(Vec3 p, Vec3 rgb, float size) const
    {
        const KhdaysG3dClipVertex c = clip(p);
        KhdaysG3dClipVertex a = c, b = c;
        a.x -= size * c.w;
        b.x += size * c.w;
        quad(a, b, rgb, size);
    }
};

void draw_overlay(const Actor &actor, const Mat4 &view, const float *proj, float units)
{
    const Overlay o{view, proj, units};
    const Simulation &sim = actor.simulation();
    const Rig &rig = actor.rig();
    const auto &x = sim.positions();
    for (const auto &d : rig.distances) {
        const Vec3 a = x[static_cast<std::size_t>(d.a)], b = x[static_cast<std::size_t>(d.b)];
        const float strain = length(b - a) / d.rest - 1.0F;
        Vec3 c = d.kind == Link::bend     ? Vec3{0.3F, 0.3F, 0.5F}
                 : d.kind == Link::anchor ? Vec3{1.0F, 0.6F, 0.0F}
                                          : Vec3{0.2F, 0.9F, 0.9F};
        if (strain > 0.1F && d.kind != Link::bend) // stretched: toward red
            c = lerp(c, Vec3{1.0F, 0.0F, 0.0F}, minf(1.0F, (strain - 0.1F) * 2.0F));
        if (d.kind != Link::bend)
            o.line(a, b, c);
    }
    for (std::size_t i = 0; i < x.size(); ++i)
        o.dot(x[i], rig.particles[i].dynamic ? Vec3{1.0F, 1.0F, 1.0F} : Vec3{1.0F, 0.2F, 0.2F}, 0.006F);
    for (const auto &c : sim.colliders()) { // the core and a ring at each end, across the view
        o.line(c.a, c.b, {0.9F, 0.9F, 0.2F}, 0.002F);
        for (const auto &[at, r] : {std::pair<Vec3, float>{c.a, c.ra}, std::pair<Vec3, float>{c.b, c.rb}}) {
            // the ring in the view's plane: its axes are the camera's (view's rows) in the world
            const Vec3 right = normalized(Vec3{view[0], view[4], view[8]}),
                       up = normalized(Vec3{view[1], view[5], view[9]});
            Vec3 prev = at + right * r;
            for (int k = 1; k <= 12; ++k) {
                const float a = 2.0F * kPi * static_cast<float>(k) / 12.0F;
                const Vec3 p = at + (right * std::cos(a) + up * std::sin(a)) * r;
                o.line(prev, p, {0.9F, 0.9F, 0.2F}, 0.002F);
                prev = p;
            }
        }
    }
}

void report(ModelSecondary &m, u32 now)
{
    if (!m.profile || now - m.reported < 600U)
        return;
    m.reported = now;
    for (const auto &[key, s] : m.actors) {
        Profile &p = s.actor->profile();
        if (p.updates == 0)
            continue;
        const PhaseTimes &ph = p.phases;
        const double n = static_cast<double>(std::max<std::uint64_t>(1, p.ticks));
        khdays_log(
            "secondary: %s actor %p: %llu updates, %llu ticks; per update p50 %.0f us, p95 %.0f us, p99 %.0f us; "
            "per tick: pose %.1f, solve %.1f, model collision %.1f, room gather %.1f, room queries %.1f, self %.1f, "
            "rebuild %.1f us; startup max %.0f us; %llu rebases, %llu restarts, %d recoveries, %zu room triangles",
            m.name.c_str(), key, static_cast<unsigned long long>(p.updates), static_cast<unsigned long long>(p.ticks),
            static_cast<double>(p.percentile(0.5F)), static_cast<double>(p.percentile(0.95F)),
            static_cast<double>(p.percentile(0.99F)), ph.pose / n, ph.solve / n, ph.model_collision / n,
            ph.environment_gather / n, ph.environment_query / n, ph.self_collision / n, ph.reconstruct / n,
            static_cast<double>(p.startup_max), static_cast<unsigned long long>(p.rebases),
            static_cast<unsigned long long>(p.restarts), s.actor->simulation().resets,
            s.actor->simulation().environment_triangles);
        p.phases.clear();
        p.ticks = 0;
    }
}

} // namespace

ModelSecondaryPtr load_model_secondary(const std::string &model_file, const std::string &name,
                                       const assets::ImportedModel &model, const std::vector<int> &joint_of_bone,
                                       const std::vector<Mat4> &inverse_bind)
{
    if (flag("KHDAYS_SECONDARY_OFF"))
        return nullptr;
    const auto path = u8(model_file);
    const auto file = path.parent_path() / (path.stem().u8string() + u8".secondary.txt");
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return nullptr;
    ModelSecondaryPtr m(new ModelSecondary);
    m->name = name;
    const auto f8 = file.u8string();
    m->file.assign(f8.begin(), f8.end());
    describe(model, m->skeleton, m->mesh);
    m->joint_of_bone = joint_of_bone;
    for (std::size_t b = 0; b < m->skeleton.names.size(); ++b) {
        const int j = b < joint_of_bone.size() ? joint_of_bone[b] : -1;
        const Mat4 ib = j >= 0 && static_cast<std::size_t>(j) < inverse_bind.size()
                            ? inverse_bind[static_cast<std::size_t>(j)]
                            : kIdentity4;
        m->inverse_bind.push_back(ib);
        m->bind_inverse.push_back(inverse_affine(ib));
    }
    const auto t0 = std::chrono::steady_clock::now();
    m->rig = cook_file(*m);
    if (m->rig)
        khdays_log("secondary: %s: cooked and prepared in %.1f ms", name.c_str(),
                   std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    const std::size_t n = m->skeleton.names.size();
    m->pose.assign(n, {});
    m->out.assign(n, {});
    m->written.assign(n, 0);
    return m;
}

void apply_model_secondary(ModelSecondary &m, const DrawInput &in, std::vector<Mat4> &skin)
{
    const u32 now = khdays_events_vblank_count();
    if (now - m.checked >= 60U) { // the configuration's files, once a second
        m.checked = now;
        reload(m);
    }
    if (!m.rig)
        return;
    const Rig &rig = *m.rig;
    // view space to the world: by the actor's own world matrix and the matrix the draw started from, so
    // the camera cancels out; else (not a field actor) by the latest flushed camera
    Mat4 view, world_of_view, placed;
    if (actor_world(in.actor, placed)) {
        world_of_view = mul(placed, inverse_affine(in.start));
        view = inverse_affine(world_of_view);
        if (!m.said_placement)
            say(m.name + ": placed in the world by its actor's matrix (camera-independent)");
        m.said_placement = true;
    } else {
        float camera[16];
        if (!khdays_g3d_camera(camera))
            return;
        std::copy(camera, camera + 16, view.begin());
        world_of_view = inverse_affine(view);
        if (!m.said_camera)
            say(m.name + ": placed in the world by the flushed camera (not a field actor's draw)");
        m.said_camera = true;
    }
    const Mat4 root = mul(world_of_view, in.root);
    const float units = length(Vec3{root[0], root[1], root[2]}); // world units a model unit
    if (!(units > 1e-6F) || !std::isfinite(units))
        return;
    clock_.update();
    if (rig.config.environment && (!room_read_ || now != room_vblank_)) {
        room_read_ = true;
        room_vblank_ = now;
        room_.refresh();
    }
    room_.set_scale(units);
    // the actor's state: made on its first draw, dropped once unseen for a while
    if (now - m.swept >= 60U) {
        m.swept = now;
        const auto idle = static_cast<u32>(rig.config.idle_reset * 60.0F) + 60U;
        for (auto it = m.actors.begin(); it != m.actors.end();)
            it = now - it->second.seen > idle ? m.actors.erase(it) : std::next(it);
    }
    auto &state = m.actors[in.actor];
    if (state.actor && std::fabs(state.units / units - 1.0F) > 0.02F)
        state.actor.reset(); // drawn at another scale: another actor in the same place
    if (!state.actor) {
        state.actor =
            std::make_unique<Actor>(m.rig, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(in.actor) >> 4));
        state.units = units;
    }
    state.seen = now;
    // the needed bones in the solver's space
    Mat4 to_sim = kIdentity4;
    to_sim[0] = to_sim[5] = to_sim[10] = 1.0F / units;
    for (const int b : rig.needed) {
        const auto i = static_cast<std::size_t>(b);
        const int j = m.joint_of_bone[i];
        if (j < 0 || static_cast<std::size_t>(j) >= skin.size())
            continue;
        m.pose[i] =
            frame_of(mul(to_sim, mul(world_of_view, mul(skin[static_cast<std::size_t>(j)], m.bind_inverse[i]))));
    }
    ActorInput a;
    a.seconds = clock_.seconds;
    a.epoch = clock_.epoch;
    a.pose = &m.pose;
    a.placement = frame_of(mul(to_sim, root));
    a.camera_distance = length(Vec3{in.root[12], in.root[13], in.root[14]}) / units;
    a.player = false;
    a.environment = rig.config.environment ? &room_ : nullptr;
    for (const auto &d : in.animations)
        a.animations.push_back({d.name, d.slot, d.section, d.frame / 30.0F, d.frames / 30.0F, d.weight});
    state.actor->update(a);
    std::fill(m.written.begin(), m.written.end(), std::uint8_t{0});
    for (const int b : rig.needed)
        m.out[static_cast<std::size_t>(b)] = m.pose[static_cast<std::size_t>(b)];
    state.actor->output(m.pose, m.out, m.written);
    Mat4 from_sim = kIdentity4;
    from_sim[0] = from_sim[5] = from_sim[10] = units;
    for (std::size_t b = 0; b < m.written.size(); ++b) {
        if (!m.written[b])
            continue;
        const int j = m.joint_of_bone[b];
        if (j < 0 || static_cast<std::size_t>(j) >= skin.size())
            continue;
        skin[static_cast<std::size_t>(j)] = mul(view, mul(from_sim, mul(matrix_of(m.out[b]), m.inverse_bind[b])));
    }
    if (m.debug && in.projection != nullptr && !state.actor->frozen())
        draw_overlay(*state.actor, view, in.projection, units);
    report(m, now);
}

} // namespace khdays::secondary
