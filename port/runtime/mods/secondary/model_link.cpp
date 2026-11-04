// Secondary motion for the mods' models (model_link.h). The solver's space is Days' world (y up)
// divided by the actor's world units per model unit, so a KH2 model simulates in its own metres
// (KH2 centimetres / 100) whatever the game's scale; the camera only converts, it never moves cloth.

#include "model_link.h"

#include "actor.h"
#include "days_environment.h"

#include "khdays/assets/kh2_mdlx.h"
#include "khdays/assets/model_import.h"
#include "khdays/vfs/filesystem.h"

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
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>

namespace khdays::secondary {

namespace {

bool flag(const char *name)
{
    const char *v = std::getenv(name);
    return v != nullptr && *v != '\0' && *v != '0';
}

// F12 (a session log mark): where an actor stands in the world (world units) and the room faces that pushed its
// cloth in the last tick, most first: a floor steep enough to stand for a wall shows here
void log_room_contacts(const std::string &name, u32 mark, const void *actor, const Mat4 &root, float units,
                       const Simulation &sim, const std::string &collision)
{
    const float facing = std::atan2(root[8], root[10]) / kDegrees;
    const auto &room = sim.room();
    const auto &push = sim.room_push();
    std::vector<std::size_t> order;
    for (std::size_t k = 0; k < room.size() && k < push.size(); ++k)
        if (push[k] > 0.0F)
            order.push_back(k);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return push[a] > push[b]; });
    khdays_log("secondary: %s: mark %u: actor %p at (%.2f %.2f %.2f), facing %.0f degrees; collision of %s; %zu room "
               "triangles near, %zu pushed its cloth",
               name.c_str(), mark, actor, static_cast<double>(root[12]), static_cast<double>(root[13]),
               static_cast<double>(root[14]), static_cast<double>(facing), collision.c_str(), room.size(),
               order.size());
    static const char *const kKinds[] = {"ground", "wall", "ceiling", "object"};
    for (std::size_t n = 0; n < order.size() && n < 8; ++n) {
        const Triangle &t = room[order[n]];
        const Vec3 c = (t.a + t.b + t.c) * (units / 3.0F);
        const float steep = std::acos(std::fmax(-1.0F, std::fmin(1.0F, t.n.y))) / kDegrees;
        khdays_log("secondary: %s: mark %u:   %s %.0f degrees steep pushed %.1f cm; centre (%.2f %.2f %.2f), normal "
                   "(%.2f %.2f %.2f)",
                   name.c_str(), mark, kKinds[static_cast<int>(t.kind) & 3], static_cast<double>(steep),
                   static_cast<double>(push[order[n]] * 100.0F), static_cast<double>(c.x), static_cast<double>(c.y),
                   static_cast<double>(c.z), static_cast<double>(t.n.x), static_cast<double>(t.n.y),
                   static_cast<double>(t.n.z));
    }
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
// a camera cut: the camera jumping further than this in one draw (m, model units; degrees)
constexpr float kCutDistance = 0.75F;
constexpr float kCutDegrees = 25.0F;
u32 room_vblank_ = 0;
bool room_read_ = false;

} // namespace

void gather_room(const Aabb &box, std::vector<Triangle> &out)
{
    const u32 now = khdays_events_vblank_count();
    if (!room_read_ || now != room_vblank_) {
        room_read_ = true;
        room_vblank_ = now;
        room_.refresh();
    }
    room_.set_scale(1.0F); // each actor's cloth sets its own before it gathers
    room_.gather(box, out);
}

struct ModelSecondary {
    std::string name, file;
    SkeletonDesc skeleton;
    MeshDesc mesh;
    std::vector<int> joint_of_bone;
    std::vector<Mat4> inverse_bind, bind_inverse; // per bone
    std::shared_ptr<const Rig> rig;
    ConfigWatch watch; // its configuration's files, read again when one changes
    u32 checked = 0, swept = 0, reported = 0;
    bool debug = false, profile = false, trace = false;
    bool said_placement = false, said_base = false, said_camera = false, said_shared = false;
    struct ActorState {
        std::unique_ptr<Actor> actor;
        u32 seen = 0;
        float units = 0.0F;
        std::vector<Vec3> game_targets; // `game_shape` this draw (the solver's space)
        std::vector<float> game_weight;
        std::vector<Frame> last_pose; // KHDAYS_SECONDARY_TRACE: the last draw's, for its steps
        std::vector<Vec3> last_cloth;
        std::uint64_t last_generation = 0; // KHDAYS_SECONDARY_TRACE: the actor's starts so far
        const char *placement = "";        // ... and how this draw found its place in the world
        // the root node in its placement's space as held (hold_root), and the last draw's difference from it
        Mat4 root_held = kIdentity4;
        bool has_root = false;
        float root_off = 0.0F, root_turn = 0.0F; // m, degrees
        // the camera at the last draw (its place in the world, and from the actor's root) and how far it jumped to
        // this one: in the world (m, degrees), round the actor (degrees) and nearer or further from it (m)
        Mat4 camera_world = kIdentity4;
        Vec3 camera_from{};
        bool has_camera = false;
        float camera_step = 0.0F, camera_turn = 0.0F, camera_swing = 0.0F, camera_reach = 0.0F;
        u32 marked = 0; // the F12 mark last logged (khdays_log_marks)
    };
    std::map<const void *, ActorState> actors; // by render object; dropped when unseen (sweep)
    // set_game_mesh: the coat locked on the game model's (`game_shape`): its locked strips each laid down paths of
    // the game coat's edges, the strips behind its sides on outward arcs between two of them; the game model
    // kept for a reload
    struct GameChain {
        std::vector<int> particles;          // top to bottom
        int anchor = -1;                     // the fixed particle above the first (-1: none)
        std::vector<float> lengths;          // at rest: from the anchor to the first, then each from the one before
        std::vector<std::vector<int>> paths; // two sections, each game vertices top to bottom
        std::vector<std::size_t> ends;       // each section's last particle (in `particles`)
        // the front's middle: its points from the second to its first section's last (the fork) halfway between
        // these two chains' (the strips at either side of it), not on its path (-1: none)
        int between_a = -1, between_b = -1;
        // a slit's panel: the strip beside it, whose bow (over the knee) its points between the fork and the corner
        // take onto the straight slit edge (-1: none)
        int sibling = -1;
    };
    struct GameArc {
        std::vector<int> particles;
        int a = -1, b = -1;        // its sides' chains (game_chains)
        std::vector<float> t, bow; // per row: its share of the way from a to b, its outward bow per gap
    };
    std::vector<GameChain> game_chains;
    std::vector<GameArc> game_arcs;
    std::vector<std::array<int, 3>> game_shell; // `cutscene_sim`: the shell over the locked places (particles)
    std::vector<std::uint8_t> game_shell_open;  // per shell face: across the slit (not pushed out of the legs)
    GameMesh game;
    bool has_game = false;
    std::vector<Frame> pose, out;
    std::vector<std::uint8_t> written;
    std::vector<ClipVolume> clip; // `clip_world`'s volumes, made for `clip_rig`
    const Rig *clip_rig = nullptr;
};

void ModelSecondaryDeleter::operator()(ModelSecondary *m) const
{
    delete m;
}

namespace {

// Rigs cooked and prepared ahead on a thread of their own (prewarm), by their .secondary.txt: a model loaded
// later (a cutscene's, mid-scene) takes its rig from here instead of stalling the game ~0.1-0.25 s to cook it
struct Prepared {
    std::shared_ptr<const Rig> rig;
    Diagnostics diagnostics;
    ConfigWatch watch;
    std::size_t bones = 0, vertices = 0; // the model it was cooked against
};
std::mutex prepared_lock;
std::condition_variable prepared_done;
std::map<std::string, Prepared> prepared;
std::set<std::string> preparing;

// a file's key there: its full path, normalised (the game names its files with mixed slashes)
std::string prepared_key(const std::filesystem::path &file)
{
    std::error_code ec;
    std::filesystem::path full = std::filesystem::weakly_canonical(file, ec);
    if (ec)
        full = file.lexically_normal();
    full.make_preferred();
    const auto f8 = full.u8string();
    return {f8.begin(), f8.end()};
}

struct Prewarm {
    std::thread worker;
    ~Prewarm()
    {
        if (worker.joinable())
            worker.join();
    }
} prewarm;

// Every mod model with secondary motion (mods/<Mod>/graphics/.../<name>.secondary.txt beside <name>.mdlx), but
// `skip` (cooked already), cooked and prepared on a thread of its own
void prewarm_model_secondary(const std::string &skip)
{
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> work; // .secondary.txt, .mdlx
    std::error_code ec;
    for (const auto &mod : std::filesystem::directory_iterator(khdays::vfs::mods_root(), ec)) {
        const auto graphics = mod.path() / "graphics";
        if (!std::filesystem::is_directory(graphics, ec))
            continue;
        for (const auto &e : std::filesystem::recursive_directory_iterator(graphics, ec)) {
            const std::u8string fname = e.path().filename().u8string(); // any name a mod's file may have
            const std::u8string tail = u8".secondary.txt";
            if (fname.size() <= tail.size() || fname.compare(fname.size() - tail.size(), tail.size(), tail) != 0)
                continue;
            const auto model = e.path().parent_path() / (fname.substr(0, fname.size() - tail.size()) + u8".mdlx");
            const std::string key = prepared_key(e.path());
            if (key == prepared_key(u8(skip)) || !std::filesystem::exists(model, ec))
                continue;
            work.emplace_back(e.path(), model);
            std::lock_guard<std::mutex> lock(prepared_lock);
            preparing.insert(key);
        }
    }
    if (work.empty())
        return;
    prewarm.worker = std::thread([work] {
        for (const auto &[file, model] : work) {
            const std::string key = prepared_key(file);
            Prepared p;
            try {
                const auto imported = assets::kh2::import_mdlx(model);
                SkeletonDesc skeleton;
                MeshDesc mesh;
                describe(imported, skeleton, mesh);
                p.bones = skeleton.names.size();
                p.vertices = mesh.vertices.size();
                const Config config = read_config(file, p.diagnostics);
                if (!has_errors(p.diagnostics)) {
                    auto cooked = std::make_shared<Rig>(cook(config, skeleton, mesh, p.diagnostics));
                    if (!has_errors(p.diagnostics)) {
                        prepare_references(*cooked, &p.diagnostics);
                        p.rig = std::move(cooked);
                    }
                }
                p.watch.record(config.files);
            } catch (...) {
                p.rig.reset(); // cooked again when loaded, which reports why
            }
            std::lock_guard<std::mutex> lock(prepared_lock);
            if (p.rig)
                prepared[key] = std::move(p);
            preparing.erase(key);
            prepared_done.notify_all();
        }
    });
}

// The rig prepared ahead for this model's file, when its files are as they were then (waits while it is being
// prepared); null to cook it here
std::shared_ptr<const Rig> take_prepared(ModelSecondary &m)
{
    const std::string key = prepared_key(u8(m.file));
    std::unique_lock<std::mutex> lock(prepared_lock);
    prepared_done.wait(lock, [&] { return preparing.count(key) == 0; });
    const auto it = prepared.find(key);
    if (it == prepared.end())
        return nullptr;
    const Prepared &p = it->second;
    if (p.bones != m.skeleton.names.size() || p.vertices != m.mesh.vertices.size() || p.watch.changed())
        return nullptr;
    for (const auto &x : p.diagnostics)
        say(m.name + ": " + format(x));
    m.watch = p.watch;
    return p.rig;
}

std::shared_ptr<const Rig> cook_file(ModelSecondary &m)
{
    if (auto ready = take_prepared(m)) {
        say(m.name + ": " + ready->summary + " (prepared ahead)");
        m.debug = ready->config.debug || flag("KHDAYS_SECONDARY_DEBUG");
        m.profile = ready->config.profile || flag("KHDAYS_SECONDARY_PROFILE");
        m.trace = flag("KHDAYS_SECONDARY_TRACE");
        return ready;
    }
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
    m.watch.record(config.files);
    if (!rig) {
        say(m.name + ": " + m.file +
            (m.rig ? " has errors; the model keeps its last good setup until it is fixed"
                   : " has errors; the model is drawn without secondary motion until it is fixed"));
        return nullptr;
    }
    say(m.name + ": " + rig->summary);
    m.debug = rig->config.debug || flag("KHDAYS_SECONDARY_DEBUG");
    m.profile = rig->config.profile || flag("KHDAYS_SECONDARY_PROFILE");
    m.trace = flag("KHDAYS_SECONDARY_TRACE");
    return rig;
}

// the configuration's files changed: cooked again; its actors start afresh on the new rig
void reload(ModelSecondary &m)
{
    if (!m.watch.changed())
        return;
    say(m.name + ": " + m.file + " changed; reloading it");
    auto rig = cook_file(m);
    if (rig) {
        m.rig = std::move(rig);
        m.actors.clear();
        if (m.has_game)
            set_game_mesh(m, m.game);
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

namespace {

// A place on a path (a polyline): its segment and how far along it
struct PathAt {
    std::size_t seg = 0;
    float f = 0.0F;
};

Vec3 path_point(const std::vector<Vec3> &path, PathAt at)
{
    return path[at.seg] + (path[at.seg + 1] - path[at.seg]) * at.f;
}

// The first place at or past `at` whose distance from `from` reaches `r` (false: the path ends first)
bool step_along(const std::vector<Vec3> &path, Vec3 from, float r, PathAt &at)
{
    for (std::size_t i = at.seg; i + 1 < path.size(); ++i) {
        const float f0 = i == at.seg ? at.f : 0.0F;
        const Vec3 a = path[i], ab = path[i + 1] - a, ac = a - from;
        const float qa = dot(ab, ab);
        if (qa < 1e-12F || length(path[i + 1] - from) < r)
            continue;
        const float qb = 2.0F * dot(ab, ac), qc = dot(ac, ac) - r * r;
        const float x = (-qb + std::sqrt(maxf(qb * qb - 4.0F * qa * qc, 0.0F))) / (2.0F * qa);
        at = {i, clampf(maxf(x, f0), 0.0F, 1.0F)};
        return true;
    }
    at = {path.size() - 2, 1.0F};
    return false;
}

// Bones of these lengths laid down the path from `from`, each from the last one's end to the first place on
// the path its length away, all lengthened or shortened by one share so the last ends on the path's end
std::vector<Vec3> lay_along(const std::vector<Vec3> &path, Vec3 from, const std::vector<float> &lengths)
{
    std::vector<Vec3> out(lengths.size(), path.empty() ? from : path.back());
    if (path.size() < 2 || lengths.empty())
        return out;
    const auto run = [&](float share, bool keep) { // true: the last ends short of the path's end
        Vec3 c = from;
        PathAt at{};
        for (std::size_t q = 0; q < lengths.size(); ++q) {
            if (!step_along(path, c, lengths[q] * share, at))
                return false;
            c = path_point(path, at);
            if (keep)
                out[q] = c;
        }
        return !(at.seg + 2 == path.size() && at.f >= 1.0F - 1e-6F);
    };
    float lo = 0.05F, hi = 8.0F;
    for (int i = 0; i < 40; ++i) {
        const float mid = 0.5F * (lo + hi);
        (run(mid, false) ? lo : hi) = mid;
    }
    run(hi, true);
    out.back() = path.back();
    return out;
}

} // namespace

void set_game_mesh(ModelSecondary &m, const GameMesh &game)
{
    constexpr float kSeam = 0.02F; // m: game coat vertices this close are one place (its pieces' seams)
    constexpr float kBand = 0.05F; // m: a top or knee vertex within this of the coat's top or the game's knee
    constexpr float kHem = 0.06F;  // m: a bottom vertex within this of the coat's bottom
    if (&game != &m.game)
        m.game = game;
    m.has_game = true;
    m.game_chains.clear();
    m.game_arcs.clear();
    m.game_shell.clear();
    m.game_shell_open.clear();
    if (!m.rig)
        return;
    const Rig &rig = *m.rig;
    const std::size_t nodes = game.node_parent.size(), bones = rig.skeleton.parent.size();
    const float sc = game.scale;
    const auto node_of = [&](int bone) {
        return bone >= 0 && static_cast<std::size_t>(bone) < game.node_of_bone.size()
                   ? game.node_of_bone[static_cast<std::size_t>(bone)]
                   : -1;
    };
    const auto up = [&](int node) {
        const int p = game.node_parent[static_cast<std::size_t>(node)];
        return p == node ? -1 : p;
    };
    const auto head = [&](int bone) { return frame_of(rig.skeleton.rest[static_cast<std::size_t>(bone)]).t * sc; };
    const auto dominant = [&](std::uint32_t v) { return v < game.weights.size() ? game.weights[v][0].first : -1; };
    // each bone's vertices' middle: the side of the body a strip is on (two panels can start on one line)
    std::vector<Vec3> middle(bones);
    std::vector<int> counted(bones, 0);
    for (const auto &v : m.mesh.vertices)
        for (std::size_t k = 0; k < 4; ++k)
            if (v.bones[k] >= 0 && static_cast<std::size_t>(v.bones[k]) < bones && v.weights[k] > 0.5F) {
                middle[static_cast<std::size_t>(v.bones[k])] += v.position * sc;
                ++counted[static_cast<std::size_t>(v.bones[k])];
            }
    const auto rest_of = [&](int i) {
        const Particle &p = rig.particles[static_cast<std::size_t>(i)];
        return apply(frame_of(rig.skeleton.rest[static_cast<std::size_t>(p.bone)]), p.offset) * sc;
    };
    const auto side_of = [&](int i) { // its strip's place across the body (its bone's vertices' middle)
        const auto b = static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(i)].bone);
        return counted[b] >= 3 ? middle[b] * (1.0F / static_cast<float>(counted[b])) : rest_of(i);
    };
    std::vector<std::uint8_t> has_next(rig.particles.size(), 0);
    for (const Particle &p : rig.particles)
        if (p.dynamic && p.anchor >= 0 && rig.particles[static_cast<std::size_t>(p.anchor)].dynamic)
            has_next[static_cast<std::size_t>(p.anchor)] = 1;
    for (std::size_t g = 0; g < rig.groups.size(); ++g) {
        if (!group_follows_game(rig, g)) // `game_shape` and `cutscene_sim` alone ask for it
            continue;
        // the legs carrying it (its follow bones) and its reference bone's node (the hips)
        std::vector<int> follow;
        for (const Particle &p : rig.particles)
            if (static_cast<std::size_t>(p.group) == g && p.blend >= 0) {
                const ReferenceBlend &b = rig.blends[static_cast<std::size_t>(p.blend)];
                for (int k = 1; k < b.count; ++k)
                    if (std::find(follow.begin(), follow.end(), b.bone[static_cast<std::size_t>(k)]) == follow.end())
                        follow.push_back(b.bone[static_cast<std::size_t>(k)]);
            }
        const int ref = node_of(rig.groups[g].reference_bone);
        if (follow.empty() || ref < 0)
            continue;
        // the game coat's nodes: the hips' and those above them and below them (the waist, the thighs), the follow
        // bones' and their children (the calves); and the knees' heights, the mod's and the game's
        std::vector<std::uint8_t> allowed(nodes, 0), hips(nodes, 0);
        for (int a = ref; a >= 0; a = up(a))
            allowed[static_cast<std::size_t>(a)] = hips[static_cast<std::size_t>(a)] = 1;
        for (std::size_t c = 0; c < nodes; ++c)
            if (up(static_cast<int>(c)) == ref)
                allowed[c] = 1;
        float knee_own = 0.0F, knee_game = 0.0F;
        int knees = 0;
        for (const int f : follow) {
            const int fn = node_of(f);
            if (fn < 0)
                continue;
            allowed[static_cast<std::size_t>(fn)] = 1;
            int own = -1, its = -1;
            float own_d = 0.0F, its_d = 0.0F;
            for (std::size_t c = 0; c < bones; ++c)
                if (rig.skeleton.parent[c] == f && length(head(static_cast<int>(c)) - head(f)) > own_d) {
                    own = static_cast<int>(c);
                    own_d = length(head(own) - head(f));
                }
            for (std::size_t c = 0; c < nodes; ++c)
                if (up(static_cast<int>(c)) == fn) {
                    allowed[c] = 1;
                    const float dist = length(game.node_place[c] - game.node_place[static_cast<std::size_t>(fn)]);
                    if (dist > its_d) {
                        its = static_cast<int>(c);
                        its_d = dist;
                    }
                }
            if (own >= 0 && its >= 0) {
                knee_own += head(own).y;
                knee_game += game.node_place[static_cast<std::size_t>(its)].y;
                ++knees;
            }
        }
        if (knees == 0)
            continue;
        knee_own /= static_cast<float>(knees);
        knee_game /= static_cast<float>(knees);
        // its surface: the triangles all on those nodes, in pieces (corners welded by place); the coat the pieces
        // with a corner on the hips or above (not the game's own legs)
        std::vector<std::array<std::uint32_t, 3>> surface;
        for (std::size_t t = 0; t + 2 < game.triangles.size(); t += 3) {
            const std::array<std::uint32_t, 3> v{game.triangles[t], game.triangles[t + 1], game.triangles[t + 2]};
            bool in = true;
            for (const std::uint32_t c : v) {
                const int node = dominant(c);
                in = in && node >= 0 && static_cast<std::size_t>(node) < nodes &&
                     allowed[static_cast<std::size_t>(node)];
            }
            if (in)
                surface.push_back(v);
        }
        std::map<std::array<long, 3>, int> weld;
        std::vector<int> piece;
        const auto welded = [&](std::uint32_t v) {
            const Vec3 &p = game.positions[v];
            const std::array<long, 3> key{std::lround(p.x * 1e4F), std::lround(p.y * 1e4F), std::lround(p.z * 1e4F)};
            const auto [it, added] = weld.emplace(key, static_cast<int>(piece.size()));
            if (added)
                piece.push_back(it->second);
            return it->second;
        };
        const auto find = [&](int a) {
            while (piece[static_cast<std::size_t>(a)] != a)
                a = piece[static_cast<std::size_t>(a)] =
                    piece[static_cast<std::size_t>(piece[static_cast<std::size_t>(a)])];
            return a;
        };
        for (const auto &v : surface) {
            const int a = welded(v[0]), b = welded(v[1]), c = welded(v[2]);
            piece[static_cast<std::size_t>(find(b))] = find(a);
            piece[static_cast<std::size_t>(find(c))] = find(a);
        }
        std::vector<std::uint8_t> on_hips(piece.size(), 0);
        for (const auto &v : surface)
            for (const std::uint32_t c : v)
                if (hips[static_cast<std::size_t>(dominant(c))])
                    on_hips[static_cast<std::size_t>(find(welded(c)))] = 1;
        // the coat's places (its vertices, one per kSeam) and the edges between them, with how many triangles
        // each edge is on (one: an open edge, the coat's top, bottom and slit)
        std::vector<std::uint32_t> place_vertex; // a game vertex at each place
        std::vector<int> place_of_vertex(game.positions.size(), -1);
        std::map<std::pair<int, int>, int> edges;
        float top = -1e30F, bottom = 1e30F;
        for (const auto &v : surface) {
            if (!on_hips[static_cast<std::size_t>(find(welded(v[0])))])
                continue;
            std::array<int, 3> at{};
            for (std::size_t e = 0; e < 3; ++e) {
                const std::uint32_t c = v[e];
                if (place_of_vertex[c] < 0) {
                    for (std::size_t q = 0; q < place_vertex.size() && place_of_vertex[c] < 0; ++q)
                        if (length(game.positions[place_vertex[q]] - game.positions[c]) < kSeam)
                            place_of_vertex[c] = static_cast<int>(q);
                    if (place_of_vertex[c] < 0) {
                        place_of_vertex[c] = static_cast<int>(place_vertex.size());
                        place_vertex.push_back(c);
                    }
                }
                at[e] = place_of_vertex[c];
                top = std::fmax(top, game.positions[c].y);
                bottom = std::fmin(bottom, game.positions[c].y);
            }
            for (std::size_t e = 0; e < 3; ++e)
                if (at[e] != at[(e + 1) % 3])
                    ++edges[{std::min(at[e], at[(e + 1) % 3]), std::max(at[e], at[(e + 1) % 3])}];
        }
        const std::size_t places = place_vertex.size();
        if (places == 0)
            continue;
        const auto place_at = [&](int q) { return game.positions[place_vertex[static_cast<std::size_t>(q)]]; };
        std::vector<std::vector<std::pair<int, float>>> next(places);
        std::vector<std::vector<int>> open(places);
        for (const auto &[e, count] : edges) {
            const float l = length(place_at(e.first) - place_at(e.second));
            next[static_cast<std::size_t>(e.first)].push_back({e.second, l});
            next[static_cast<std::size_t>(e.second)].push_back({e.first, l});
            if (count == 1) {
                open[static_cast<std::size_t>(e.first)].push_back(e.second);
                open[static_cast<std::size_t>(e.second)].push_back(e.first);
            }
        }
        const auto shortest = [&](int from, int to) { // the coat's edges from one place to another (Dijkstra)
            std::vector<float> dist(places, 1e30F);
            std::vector<int> prev(places, -1);
            std::vector<std::uint8_t> done(places, 0);
            dist[static_cast<std::size_t>(from)] = 0.0F;
            for (;;) {
                int u = -1;
                for (std::size_t q = 0; q < places; ++q)
                    if (!done[q] && dist[q] < 1e29F && (u < 0 || dist[q] < dist[static_cast<std::size_t>(u)]))
                        u = static_cast<int>(q);
                if (u < 0 || u == to)
                    break;
                done[static_cast<std::size_t>(u)] = 1;
                for (const auto &[w, l] : next[static_cast<std::size_t>(u)])
                    if (dist[static_cast<std::size_t>(u)] + l < dist[static_cast<std::size_t>(w)]) {
                        dist[static_cast<std::size_t>(w)] = dist[static_cast<std::size_t>(u)] + l;
                        prev[static_cast<std::size_t>(w)] = u;
                    }
            }
            std::vector<int> path;
            for (int q = to; q >= 0; q = q == from ? -1 : prev[static_cast<std::size_t>(q)])
                path.insert(path.begin(), static_cast<int>(place_vertex[static_cast<std::size_t>(q)]));
            return path.size() >= 2 && path.front() == static_cast<int>(place_vertex[static_cast<std::size_t>(from)])
                       ? path
                       : std::vector<int>{};
        };
        // around the body: from the hips' node, front the way the coat's shared (forked) points face, left across
        const Vec3 centre = game.node_place[static_cast<std::size_t>(ref)];
        const auto across = [&](Vec3 v) {
            const float x = v.x - centre.x, z = v.z - centre.z, l = std::sqrt(x * x + z * z);
            return l > 1e-6F ? std::array<float, 2>{x / l, z / l} : std::array<float, 2>{0.0F, 0.0F};
        };
        std::vector<std::vector<int>> strips;
        std::map<int, int> shared;
        for (std::size_t last = 0; last < rig.particles.size(); ++last) {
            const Particle &lp = rig.particles[last];
            if (static_cast<std::size_t>(lp.group) != g || !lp.dynamic || has_next[last])
                continue;
            std::vector<int> strip;
            for (int c = static_cast<int>(last); c >= 0 && rig.particles[static_cast<std::size_t>(c)].dynamic;
                 c = rig.particles[static_cast<std::size_t>(c)].anchor)
                strip.insert(strip.begin(), c);
            for (const int i : strip)
                ++shared[i];
            if (strip.size() >= 3)
                strips.push_back(std::move(strip));
        }
        std::vector<std::size_t> forked, plain;
        for (std::size_t s = 0; s < strips.size(); ++s)
            (shared[strips[s][1]] > 1 || shared[strips[s][0]] > 1 ? forked : plain).push_back(s);
        const std::string group = m.name + ": group " + rig.groups[g].name + ": game_shape: ";
        if (forked.size() != 2 || plain.size() < 3) {
            say(group + "its strips are not a coat with one forked front strip and others around it; not locked");
            continue;
        }
        const auto knee_row = [&](const std::vector<int> &s) {
            std::size_t kr = 1;
            for (std::size_t q = 2; q + 1 < s.size(); ++q)
                if (std::fabs(rest_of(s[q]).y - knee_own) < std::fabs(rest_of(s[kr]).y - knee_own))
                    kr = q;
            return kr;
        };
        const auto way_of = [&](const std::vector<int> &s) { return across(side_of(s[knee_row(s)])); };
        const std::array<float, 2> front = across(side_of(strips[forked[0]][0]));
        const std::array<float, 2> left{front[1], -front[0]};
        const auto on = [](std::array<float, 2> a, std::array<float, 2> b) { return a[0] * b[0] + a[1] * b[1]; };
        std::size_t back = plain[0];
        for (const std::size_t s : plain)
            if (on(way_of(strips[s]), front) < on(way_of(strips[back]), front))
                back = s;
        // each side's strips back to front: the middle one the side, those before it in front locked too, those
        // behind on arcs between it and the back
        std::array<std::size_t, 2> lateral{};
        std::vector<std::size_t> front_sides, behind;
        bool sides = true;
        for (int sgn = 1; sgn >= -1; sgn -= 2) {
            std::vector<std::size_t> row;
            for (const std::size_t s : plain)
                if (s != back && static_cast<float>(sgn) * on(way_of(strips[s]), left) > 0.0F)
                    row.push_back(s);
            std::sort(row.begin(), row.end(), [&](std::size_t a, std::size_t b) {
                return on(way_of(strips[a]), front) < on(way_of(strips[b]), front);
            });
            if (row.empty()) {
                sides = false;
                break;
            }
            const std::size_t mid = row.size() / 2;
            lateral[sgn > 0 ? 0 : 1] = row[mid];
            for (std::size_t q = 0; q < row.size(); ++q)
                if (q != mid)
                    (q < mid ? behind : front_sides).push_back(row[q]);
        }
        if (!sides) {
            say(group + "no strips at one of its sides; not locked");
            continue;
        }
        const std::size_t chains_before = m.game_chains.size(), arcs_before = m.game_arcs.size();
        // the key places, each the nearest by its way around and taken once: the slit's top (the highest open
        // place in front, under the top) and its corners (down its open edges while they fall steeper than they
        // run), the front's top; then each locked strip's top, knee and bottom
        std::vector<std::uint8_t> taken(places, 0);
        const auto nearest = [&](std::array<float, 2> way, float lo, float hi) {
            int best = -1;
            float best_on = -2.0F;
            for (std::size_t q = 0; q < places; ++q) {
                const Vec3 p = place_at(static_cast<int>(q));
                const float o = on(across(p), way);
                if (!taken[q] && p.y >= lo && p.y <= hi && o > best_on) {
                    best = static_cast<int>(q);
                    best_on = o;
                }
            }
            if (best >= 0)
                taken[static_cast<std::size_t>(best)] = 1;
            return best;
        };
        int slit = -1;
        for (std::size_t q = 0; q < places; ++q) {
            const Vec3 p = place_at(static_cast<int>(q));
            if (!open[q].empty() && p.y < top - 0.1F && on(across(p), front) > 0.7F &&
                (slit < 0 || p.y > place_at(slit).y))
                slit = static_cast<int>(q);
        }
        if (slit < 0) {
            say(group + "the game coat has no slit in front (an open edge under its top); not locked");
            continue;
        }
        taken[static_cast<std::size_t>(slit)] = 1;
        std::array<std::vector<int>, 2> corner; // left, right: game vertices from the slit's top down its edge
        for (const int first : open[static_cast<std::size_t>(slit)]) {
            std::vector<int> walk{slit, first};
            for (;;) {
                int nx = -1;
                for (const int w : open[static_cast<std::size_t>(walk.back())])
                    if (w != walk[walk.size() - 2])
                        nx = w;
                if (nx < 0 || std::find(walk.begin(), walk.end(), nx) != walk.end())
                    break;
                const Vec3 a = place_at(walk.back()), b = place_at(nx);
                if (a.y - b.y < std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z)))
                    break;
                walk.push_back(nx);
            }
            std::vector<int> path;
            for (const int q : walk) {
                taken[static_cast<std::size_t>(q)] = 1;
                path.push_back(static_cast<int>(place_vertex[static_cast<std::size_t>(q)]));
            }
            corner[on(across(place_at(first)), left) > 0.0F ? 0 : 1] = std::move(path);
        }
        const int front_top = nearest(front, top - kBand, 1e30F);
        if (corner[0].empty() || corner[1].empty() || front_top < 0) {
            say(group + "the game coat's slit has no two edges down from its top; not locked");
            continue;
        }
        const std::vector<int> trunk = shortest(front_top, slit);
        const auto lengths_of = [&](const std::vector<int> &s) {
            std::vector<float> l;
            const int anchor = rig.particles[static_cast<std::size_t>(s[0])].anchor;
            Vec3 at = anchor >= 0 ? rest_of(anchor) : rest_of(s[0]);
            for (const int i : s) {
                l.push_back(length(rest_of(i) - at));
                at = rest_of(i);
            }
            return l;
        };
        const auto chain_of = [&](const std::vector<int> &s) {
            ModelSecondary::GameChain c;
            c.particles = s;
            c.anchor = rig.particles[static_cast<std::size_t>(s[0])].anchor;
            c.lengths = lengths_of(s);
            return c;
        };
        std::array<int, 7> chain_at{}; // back, left side, right side, left front side.., the front's left, right
        const auto locked = [&](std::size_t s) {
            const std::array<float, 2> way = way_of(strips[s]);
            const int t = nearest(way, top - kBand, 1e30F), k = nearest(way, knee_game - kBand, knee_game + kBand),
                      b = nearest(way, -1e30F, bottom + kHem);
            ModelSecondary::GameChain c = chain_of(strips[s]);
            if (t >= 0 && k >= 0 && b >= 0) {
                c.paths = {shortest(t, k), shortest(k, b)};
                c.ends = {knee_row(strips[s]), strips[s].size() - 1};
            }
            m.game_chains.push_back(std::move(c));
            return static_cast<int>(m.game_chains.size()) - 1;
        };
        chain_at[0] = locked(back);
        chain_at[1] = locked(lateral[0]);
        chain_at[2] = locked(lateral[1]);
        std::array<int, 2> beside{-1, -1}; // each side's front strip nearest the front: the front's middle between them
        std::array<float, 2> nearest_front{-2.0F, -2.0F};
        for (const std::size_t s : front_sides) {
            const int c = locked(s);
            const std::size_t at = on(way_of(strips[s]), left) > 0.0F ? 0 : 1;
            if (on(way_of(strips[s]), front) > nearest_front[at]) {
                nearest_front[at] = on(way_of(strips[s]), front);
                beside[at] = c;
            }
        }
        for (const std::size_t f : forked) {
            const std::vector<int> &s = strips[f];
            ModelSecondary::GameChain c = chain_of(s);
            std::size_t fork = 0;
            for (std::size_t q = 0; q < s.size(); ++q)
                if (shared[s[q]] > 1)
                    fork = q;
            c.paths = {trunk, corner[on(across(side_of(s.back())), left) > 0.0F ? 0 : 1]};
            c.ends = {fork, s.size() - 1};
            if (beside[0] >= 0 && beside[1] >= 0) {
                c.between_a = beside[0];
                c.between_b = beside[1];
            }
            c.sibling = beside[on(across(side_of(s.back())), left) > 0.0F ? 0 : 1];
            m.game_chains.push_back(std::move(c));
        }
        // the strips behind the sides: on an arc between their side and the back, by their way around at rest
        for (const std::size_t s : behind) {
            const std::array<float, 2> way = way_of(strips[s]);
            const std::size_t lat = on(way, left) > 0.0F ? 1 : 2;
            ModelSecondary::GameArc arc;
            arc.particles = strips[s];
            arc.a = chain_at[lat];
            arc.b = chain_at[0];
            const auto &pa = m.game_chains[static_cast<std::size_t>(arc.a)].particles;
            const auto &pb = m.game_chains[static_cast<std::size_t>(arc.b)].particles;
            for (std::size_t r = 0; r < arc.particles.size() && r < pa.size() && r < pb.size(); ++r) {
                const auto aw = across(side_of(pa[r])), bw = across(side_of(pb[r])),
                           mw = across(side_of(arc.particles[r]));
                const float span = std::atan2(aw[0] * bw[1] - aw[1] * bw[0], on(aw, bw)),
                            part = std::atan2(aw[0] * mw[1] - aw[1] * mw[0], on(aw, mw));
                arc.t.push_back(std::fabs(span) > 1e-6F ? clampf(part / span, 0.0F, 1.0F) : 0.5F);
                arc.bow.push_back(0.5F * std::tan(0.25F * std::fabs(span)));
            }
            m.game_arcs.push_back(std::move(arc));
        }
        // a key place or path missing: the group is not locked
        bool whole = true;
        for (std::size_t c = chains_before; c < m.game_chains.size(); ++c) {
            whole = whole && m.game_chains[c].paths.size() == 2;
            for (const auto &path : m.game_chains[c].paths)
                whole = whole && path.size() >= 2;
        }
        std::string said = group + "locked:";
        for (std::size_t c = chains_before; c < m.game_chains.size(); ++c) {
            const auto &ch = m.game_chains[c];
            if (ch.paths.size() != 2 || ch.paths[0].empty() || ch.paths[1].empty())
                continue;
            const Vec3 k = game.positions[static_cast<std::size_t>(ch.paths[0].back())] * 100.0F,
                       b = game.positions[static_cast<std::size_t>(ch.paths[1].back())] * 100.0F;
            char line[192];
            std::snprintf(
                line, sizeof line, " %s (row %zu on %.0f %.0f %.0f, last on %.0f %.0f %.0f)",
                rig.skeleton
                    .names[static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(ch.particles[0])].bone)]
                    .c_str(),
                ch.ends[0], k.x, k.y, k.z, b.x, b.y, b.z);
            said += line;
        }
        say(said + "; " + std::to_string(m.game_arcs.size() - arcs_before) + " on arcs (cm, the game's rest)");
        if (!whole) {
            say(group + "a strip's top, knee or bottom place or the coat's path to it is missing; not locked");
            m.game_chains.resize(chains_before);
            m.game_arcs.resize(arcs_before);
            continue;
        }
        // `cutscene_sim`'s shell: each strip (its fixed point, then its points down) beside the next round the
        // body, closed all round (across the slit too); its faces wound alike, all turned outward when most point
        // inward at rest (the slit's are near flat there)
        const std::size_t shell_before = m.game_shell.size();
        std::vector<std::vector<int>> columns;
        const auto column = [&](const std::vector<int> &particles) {
            std::vector<int> col;
            const int anchor = rig.particles[static_cast<std::size_t>(particles[0])].anchor;
            if (anchor >= 0)
                col.push_back(anchor);
            col.insert(col.end(), particles.begin(), particles.end());
            columns.push_back(std::move(col));
        };
        for (std::size_t c = chains_before; c < m.game_chains.size(); ++c)
            column(m.game_chains[c].particles);
        for (std::size_t c = arcs_before; c < m.game_arcs.size(); ++c)
            column(m.game_arcs[c].particles);
        const auto angle = [&](const std::vector<int> &col) {
            const auto w = across(side_of(col[std::min<std::size_t>(3, col.size() - 1)]));
            return std::atan2(w[1], w[0]);
        };
        std::sort(columns.begin(), columns.end(),
                  [&](const std::vector<int> &a, const std::vector<int> &b) { return angle(a) < angle(b); });
        for (std::size_t c = 0; c < columns.size(); ++c) {
            const auto &l = columns[c], &r = columns[(c + 1) % columns.size()];
            for (std::size_t q = 0; q + 1 < l.size() && q + 1 < r.size(); ++q)
                for (const std::array<int, 3> f :
                     {std::array<int, 3>{l[q], l[q + 1], r[q]}, std::array<int, 3>{r[q], l[q + 1], r[q + 1]}}) {
                    if (f[0] == f[1] || f[1] == f[2] || f[0] == f[2])
                        continue; // the front above its fork: one strip
                    m.game_shell.push_back(f);
                    // between the front's two panels: across the slit, where the legs are seen anyway
                    const auto forked_column = [&](const std::vector<int> &col) {
                        for (const int i : col)
                            if (shared.count(i) != 0 && shared.at(i) > 1)
                                return true;
                        return false;
                    };
                    m.game_shell_open.push_back(forked_column(l) && forked_column(r) ? 1 : 0);
                }
        }
        float outward = 0.0F;
        for (std::size_t q = shell_before; q < m.game_shell.size(); ++q) {
            const auto &f = m.game_shell[q];
            const Vec3 a = rest_of(f[0]), b = rest_of(f[1]), cc = rest_of(f[2]);
            const Vec3 mid = (a + b + cc) * (1.0F / 3.0F);
            outward += dot(cross(b - a, cc - a), Vec3{mid.x - centre.x, 0.0F, mid.z - centre.z});
        }
        if (outward < 0.0F)
            for (std::size_t q = shell_before; q < m.game_shell.size(); ++q)
                std::swap(m.game_shell[q][1], m.game_shell[q][2]);
    }
}

// `clip_world`'s volumes (rig.cpp clip_volumes_of) from the particles' places at bind, said in the session log
void build_clip(ModelSecondary &m)
{
    m.clip.clear();
    m.clip_rig = m.rig.get();
    if (!m.rig)
        return;
    const Rig &rig = *m.rig;
    std::vector<Vec3> rest(rig.particles.size());
    for (std::size_t i = 0; i < rest.size(); ++i)
        rest[i] = point(m.bind_inverse[static_cast<std::size_t>(rig.particles[i].bone)], rig.particles[i].offset);
    for (ClipVolume &cv : clip_volumes_of(rig, rest)) {
        if (cv.faces.empty()) {
            say(m.name + ": group " + cv.group + ": `clip_world` needs three strips round the body; no volume");
            continue;
        }
        say(m.name + ": group " + cv.group + ": `clip_world` volume of " + std::to_string(cv.columns) + " columns, " +
            std::to_string(cv.faces.size()) + " faces" +
            (cv.open_edges == 0 ? std::string(", closed")
                                : ", " + std::to_string(cv.open_edges) + " edges not closed"));
        m.clip.push_back(std::move(cv));
    }
}

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
    // the first model with secondary motion: every other one prepared ahead, so a model loaded mid-scene does
    // not stall the game to cook it
    static bool prewarmed = false;
    if (!prewarmed && !flag("KHDAYS_SECONDARY_NO_PREWARM")) {
        prewarmed = true;
        prewarm_model_secondary(m->file);
    }
    const std::size_t n = m->skeleton.names.size();
    m->pose.assign(n, {});
    m->out.assign(n, {});
    m->written.assign(n, 0);
    return m;
}

// KHDAYS_SECONDARY_TRACE: each draw's animations, how far the body and the cloth moved since the last
// draw (cm, the model's own units), to find what jumps when the game does something (a loop, a cut)
static void trace_draw(ModelSecondary &m, ModelSecondary::ActorState &state, const ActorInput &a, u32 now)
{
    std::string anims;
    char part[160];
    for (const auto &x : a.animations) {
        std::snprintf(part, sizeof(part), " %s slot %d f%.1f/%.1f w%.2f", x.name.c_str(), x.slot,
                      static_cast<double>(x.seconds * 30.0F), static_cast<double>(x.length * 30.0F),
                      static_cast<double>(x.weight));
        anims += part;
    }
    const Rig &rig = *m.rig;
    float hips = 0.0F, body = 0.0F, cloth = 0.0F;
    // the first group's reference bone in the world (the solver's space, metres): where the model stands
    const Vec3 at = rig.groups.empty() ? Vec3{} : m.pose[static_cast<std::size_t>(rig.groups.front().reference_bone)].t;
    int body_bone = -1, cloth_bone = -1;
    if (state.last_pose.size() == m.pose.size()) {
        if (!rig.groups.empty())
            hips = length(m.pose[static_cast<std::size_t>(rig.groups.front().reference_bone)].t -
                          state.last_pose[static_cast<std::size_t>(rig.groups.front().reference_bone)].t);
        for (const int b : rig.needed) {
            const auto i = static_cast<std::size_t>(b);
            const float d = length(m.pose[i].t - state.last_pose[i].t);
            if (d > body) {
                body = d;
                body_bone = b;
            }
        }
    }
    const auto &x = state.actor->simulation().positions();
    if (state.last_cloth.size() == x.size())
        for (std::size_t i = 0; i < x.size(); ++i) {
            if (!rig.particles[i].dynamic)
                continue;
            const float d = length(x[i] - state.last_cloth[i]);
            if (d > cloth) {
                cloth = d;
                cloth_bone = rig.particles[i].bone;
            }
        }
    const auto name = [&](int b) {
        return b >= 0 ? rig.skeleton.names[static_cast<std::size_t>(b)] : std::string("-");
    };
    khdays_log("secondary trace: %s v%u t%.3f epoch %llu;%s; hips %.2f cm, body %.1f cm (%s), cloth %.1f cm (%s); "
               "root %.2f mm %.3f deg off held; camera %.2f m %.1f deg (round it %.1f deg, %.2f m nearer or further); "
               "placed by %s; at %.4f %.4f %.4f%s",
               m.name.c_str(), now, a.seconds, static_cast<unsigned long long>(a.epoch), anims.c_str(),
               static_cast<double>(hips * 100.0F), static_cast<double>(body * 100.0F), name(body_bone).c_str(),
               static_cast<double>(cloth * 100.0F), name(cloth_bone).c_str(),
               static_cast<double>(state.root_off * 1000.0F), static_cast<double>(state.root_turn),
               static_cast<double>(state.camera_step), static_cast<double>(state.camera_turn),
               static_cast<double>(state.camera_swing), static_cast<double>(state.camera_reach), state.placement,
               static_cast<double>(at.x), static_cast<double>(at.y), static_cast<double>(at.z),
               a.cut        ? " (a cut)"
               : a.pose_cut ? " (a pose cut)"
                            : "");
    if (state.actor->generation() != state.last_generation)
        khdays_log("secondary trace: %s v%u: the cloth started afresh", m.name.c_str(), now);
    state.last_generation = state.actor->generation();
    state.last_pose = m.pose;
    state.last_cloth = x;
}

namespace {

// Whether two matrices agree to the fixed-point rounding of a few GE products (entries in 1/4096ths,
// relative for large ones)
bool fx_close(const Mat4 &a, const Mat4 &b)
{
    for (std::size_t i = 0; i < 16; ++i)
        if (std::fabs(a[i] - b[i]) > 16.0F / 4096.0F + 0.002F * std::fabs(b[i]))
            return false;
    return true;
}

// The root node in its placement's space, inverse(start) x root, is the game's own (camera-free) node
// matrix, but rebuilt from the GE's fixed-point products of the camera with it: every turn of the camera
// changes its rounding, and the body seen in the world shook by a fraction of a millimetre, which hair and
// cords felt as acceleration. It is held while it moves less than that (Days' roxas_R never moves at
// all); past 1 cm or 1 degree it is taken anew. Returns the root to use.
Mat4 hold_root(ModelSecondary::ActorState &state, const Mat4 &now)
{
    const Frame a = frame_of(state.root_held), b = frame_of(now);
    const float per = length(Vec3{now[0], now[1], now[2]}); // its units a model unit (a model unit: 1 m)
    state.root_off = per > 1e-9F ? length(b.t - a.t) / per : 0.0F;
    state.root_turn = angle_between(a.q, b.q) / kDegrees;
    const bool scaled = std::fabs(length(b.s) / std::fmax(length(a.s), 1e-9F) - 1.0F) > 0.01F;
    if (!state.has_root || state.root_off > 0.01F || state.root_turn > 1.0F || scaled) {
        state.root_held = now;
        state.has_root = true;
    }
    return state.root_held;
}

} // namespace

// The view's way to the world as a draw this frame found it from its own place (its start exactly the camera times
// its actor's matrix, or the base transform), with the flushed camera it went with: the same for every model drawn
// under that camera, for one that cannot find its own (a weapon, drawn from the hand's saved matrix)
struct SharedView {
    Mat4 world_of_view = kIdentity4, camera = kIdentity4;
    u32 vblank = 0;
    bool valid = false;
};
SharedView shared_view;

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
    // view space to the world: by the model's place without the camera (a field actor's world matrix, or
    // the base transform the 3D engine's flush multiplied onto the camera, when the draw started from
    // exactly that) and the matrix the draw started from, so the camera cancels out; else by the latest
    // flushed camera (that cancels it only up to the GE's rounding, and the cloth feels the camera turn)
    Mat4 world_of_view, placed;
    bool camera_free = true, verified = false; // verified: its start is exactly its place under the camera
    const char *placed_by = "the camera";
    const bool own = actor_world(in.actor, placed);
    const bool shared =
        shared_view.valid && shared_view.vblank == now && in.has_camera && fx_close(shared_view.camera, in.camera);
    if (own && in.has_camera && fx_close(mul(in.camera, placed), in.start)) { // its start is its own place
        verified = true;
        placed_by = "its actor";
        if (!m.said_placement)
            say(m.name + ": placed in the world by its actor's matrix (camera-independent)");
        m.said_placement = true;
    } else if (!(in.base_kind == 1 && in.has_camera && fx_close(mul(in.camera, in.base), in.start)) &&
               !(in.base_kind == 2 && fx_close(kIdentity4, in.start)) && shared) {
        // drawn from a matrix not its own place (a weapon at the hand): this frame's view as another model found it
        camera_free = false;
        world_of_view = shared_view.world_of_view;
        placed_by = "another model";
        if (!m.said_shared)
            say(m.name + ": placed in the world by another model's place this frame (camera-independent)");
        m.said_shared = true;
    } else if (own) {
        placed_by = "its actor (unchecked)";
        if (!m.said_placement)
            say(m.name + ": placed in the world by its actor's matrix (camera-independent)");
        m.said_placement = true;
    } else if ((in.base_kind == 1 && in.has_camera && fx_close(mul(in.camera, in.base), in.start)) ||
               (in.base_kind == 2 && fx_close(kIdentity4, in.start))) {
        placed = in.base;
        verified = true;
        placed_by = "the base";
        if (!m.said_base)
            say(m.name + ": placed in the world by the 3D engine's base transform (camera-independent)");
        m.said_base = true;
    } else {
        if (!in.has_camera)
            return;
        camera_free = false;
        world_of_view = inverse_affine(in.camera);
        if (!m.said_camera)
            say(m.name + ": placed in the world by the flushed camera (its draw starts from neither an actor's "
                         "matrix nor the base transform); turning the camera can move its cloth");
        m.said_camera = true;
    }
    if (camera_free)
        world_of_view = mul(placed, inverse_affine(in.start));
    const Mat4 root = mul(world_of_view, in.root);
    const float measured = length(Vec3{root[0], root[1], root[2]}); // world units a model unit, this draw's root's
    if (!(measured > 1e-6F) || !std::isfinite(measured))
        return;
    clock_.update();
    if (rig.config.environment && (!room_read_ || now != room_vblank_)) {
        room_read_ = true;
        room_vblank_ = now;
        room_.refresh();
    }
    // the actor's state: made on its first draw, dropped once unseen for a while
    if (now - m.swept >= 60U) {
        m.swept = now;
        const auto idle = static_cast<u32>(rig.config.idle_reset * 60.0F) + 60U;
        for (auto it = m.actors.begin(); it != m.actors.end();)
            it = now - it->second.seen > idle ? m.actors.erase(it) : std::next(it);
    }
    auto &state = m.actors[in.actor];
    state.placement = placed_by;
    if (state.actor && std::fabs(state.units / measured - 1.0F) > 0.02F)
        state.actor.reset(); // drawn at another scale: another actor in the same place
    if (!state.actor) {
        state.actor =
            std::make_unique<Actor>(m.rig, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(in.actor) >> 4));
        state.units = measured;
        state.marked = khdays_log_marks();
        if (m.trace)
            khdays_log("secondary trace: %s: a new actor for render object %p (%zu actors)", m.name.c_str(), in.actor,
                       m.actors.size());
    }
    state.seen = now;
    // world units a model unit as the actor started (another scale past 2 % starts a new one), not this draw's: it
    // divides the whole world position, and the root's fixed-point rounding would shake the cloth far from the origin
    const float units = state.units;
    room_.set_scale(units);
    if (camera_free) // the root node held through the GE's rounding (hold_root)
        world_of_view =
            mul(placed, mul(hold_root(state, mul(inverse_affine(in.start), in.root)), inverse_affine(in.root)));
    // the view's way to the world for this frame's other draws under this camera, as held: the same as this
    // model's own, so a weapon drawn from its hand's matrix lies in the world where the hand does
    if (verified && in.has_camera)
        shared_view = {world_of_view, in.camera, now, true};
    const Mat4 view = inverse_affine(world_of_view);
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
    a.environment = rig.config.environment ? &room_ : nullptr;
    // a camera cut: the camera's view of the actor jumped further in one draw than a camera moves (a new shot;
    // cutscenes re-stage their actors across one, and the cloth would take that as a fling). Judged about the
    // actor, not in the world: a field camera swinging round a running actor moves far in the world each frame
    if (in.has_camera) {
        const Mat4 camera_world = inverse_affine(in.camera);
        const Vec3 from =
            (Vec3{camera_world[12], camera_world[13], camera_world[14]} - Vec3{root[12], root[13], root[14]}) *
            (1.0F / units); // the camera's place from the actor's root (world axes, model units)
        if (state.has_camera) {
            const Frame was = frame_of(state.camera_world), is = frame_of(camera_world);
            state.camera_step = length(is.t - was.t) / units;
            state.camera_turn = angle_between(was.q, is.q) / kDegrees;
            const float near = length(state.camera_from), far = length(from);
            state.camera_swing =
                near > 1e-4F && far > 1e-4F
                    ? std::acos(clampf(dot(state.camera_from, from) / (near * far), -1.0F, 1.0F)) / kDegrees
                    : 0.0F;
            state.camera_reach = std::fabs(far - near);
            a.cut = state.camera_turn > kCutDegrees || state.camera_swing > kCutDegrees ||
                    state.camera_reach > kCutDistance;
        }
        state.camera_world = camera_world;
        state.camera_from = from;
        state.has_camera = true;
    }
    a.pose_cut = in.pose_cut;
    for (const auto &d : in.animations)
        a.animations.push_back({d.name, d.slot, d.section, d.frame / 30.0F, d.frames / 30.0F, d.weight});
    // `game_shape`: the coat locked on the game model's coat now: each locked strip from its fixed point down its
    // two paths (the game's vertices as its nodes carry them), each section's bones scaled alike to end on the
    // section's end; the strips behind the sides on arcs between their side and the back, bowed outward by the gap
    const bool coat_locked = in.game_nodes != nullptr && !m.game_chains.empty();
    if (coat_locked) {
        const std::size_t n = rig.particles.size();
        state.game_targets.assign(n, {});
        state.game_weight.assign(n, 0.0F);
        const Mat4 view_to_sim = mul(to_sim, world_of_view);
        const auto game_vertex = [&](int v) {
            Vec3 at{};
            const Vec3 r = m.game.positions[static_cast<std::size_t>(v)];
            for (const auto &[node, w] : m.game.weights[static_cast<std::size_t>(v)]) {
                if (node < 0 || w <= 0.0F || static_cast<std::size_t>(node) >= in.game_nodes->size())
                    continue;
                const Mat4 &dn = (*in.game_nodes)[static_cast<std::size_t>(node)];
                at += Vec3{dn[0] * r.x + dn[4] * r.y + dn[8] * r.z + dn[12],
                           dn[1] * r.x + dn[5] * r.y + dn[9] * r.z + dn[13],
                           dn[2] * r.x + dn[6] * r.y + dn[10] * r.z + dn[14]} *
                      w;
            }
            return Vec3{view_to_sim[0] * at.x + view_to_sim[4] * at.y + view_to_sim[8] * at.z + view_to_sim[12],
                        view_to_sim[1] * at.x + view_to_sim[5] * at.y + view_to_sim[9] * at.z + view_to_sim[13],
                        view_to_sim[2] * at.x + view_to_sim[6] * at.y + view_to_sim[10] * at.z + view_to_sim[14]};
        };
        const auto fixed_at = [&](int i) { // a fixed particle where its bone is now
            const Particle &p = rig.particles[static_cast<std::size_t>(i)];
            const int j = m.joint_of_bone[static_cast<std::size_t>(p.bone)];
            if (j < 0 || static_cast<std::size_t>(j) >= skin.size())
                return Vec3{};
            return apply(frame_of(mul(view_to_sim, mul(skin[static_cast<std::size_t>(j)],
                                                       m.bind_inverse[static_cast<std::size_t>(p.bone)]))),
                         p.offset);
        };
        constexpr float kBetween = 0.5F; // the front's middle: this far from its left neighbour to its right
        std::vector<Vec3> locked(n);
        std::vector<std::uint8_t> done(n, 0);
        std::vector<std::vector<Vec3>> rows(m.game_chains.size());
        for (std::size_t c = 0; c < m.game_chains.size(); ++c) {
            const auto &chain = m.game_chains[c];
            std::size_t first = 0;
            for (std::size_t sec = 0; sec < 2; ++sec) {
                std::vector<Vec3> path;
                for (const int v : chain.paths[sec])
                    path.push_back(game_vertex(v));
                const Vec3 from = first == 0 ? (chain.anchor >= 0 ? fixed_at(chain.anchor) : path.front())
                                             : locked[static_cast<std::size_t>(chain.particles[first - 1])];
                const std::vector<float> lengths(chain.lengths.begin() + static_cast<std::ptrdiff_t>(first),
                                                 chain.lengths.begin() +
                                                     static_cast<std::ptrdiff_t>(chain.ends[sec] + 1));
                const std::vector<Vec3> pts = lay_along(path, from, lengths);
                for (std::size_t q = 0; q < pts.size(); ++q) {
                    const auto i = static_cast<std::size_t>(chain.particles[first + q]);
                    locked[i] = pts[q];
                    done[i] = 1;
                }
                // the front's middle (not its top, not the slit's panels) halfway between the strips beside it
                if (sec == 0 && chain.between_a >= 0)
                    for (std::size_t q = 1; q <= chain.ends[0]; ++q) {
                        const auto &ra = rows[static_cast<std::size_t>(chain.between_a)],
                                   &rb = rows[static_cast<std::size_t>(chain.between_b)];
                        if (q < ra.size() && q < rb.size())
                            locked[static_cast<std::size_t>(chain.particles[q])] = lerp(ra[q], rb[q], kBetween);
                    }
                first = chain.ends[sec] + 1;
            }
            // a slit's panel: each point between the fork and the corner on the line from the one above to the one
            // below (its share of the way at rest), out by as much as the strip beside it stands out of its own
            // such line there: the panel bows over the knee as its neighbour does
            if (chain.sibling >= 0) {
                const auto &sib = m.game_chains[static_cast<std::size_t>(chain.sibling)];
                const auto &sr = rows[static_cast<std::size_t>(chain.sibling)];
                const auto share = [](const std::vector<float> &l, std::size_t q) {
                    const float span = l[q] + l[q + 1];
                    return span > 1e-6F ? l[q] / span : 0.5F;
                };
                for (std::size_t q = chain.ends[0] + 1; q + 1 < chain.particles.size(); ++q) {
                    if (q + 1 >= sr.size() || q + 1 >= sib.lengths.size())
                        break;
                    const Vec3 above = locked[static_cast<std::size_t>(chain.particles[q - 1])],
                               below = locked[static_cast<std::size_t>(chain.particles[q + 1])];
                    const Vec3 bow = sr[q] - lerp(sr[q - 1], sr[q + 1], share(sib.lengths, q));
                    locked[static_cast<std::size_t>(chain.particles[q])] =
                        lerp(above, below, share(chain.lengths, q)) + bow;
                }
            }
            for (const int i : chain.particles)
                rows[c].push_back(locked[static_cast<std::size_t>(i)]);
        }
        for (const auto &arc : m.game_arcs)
            for (std::size_t r = 0; r < arc.t.size(); ++r) {
                const Vec3 pa = rows[static_cast<std::size_t>(arc.a)][r], pb = rows[static_cast<std::size_t>(arc.b)][r];
                Vec3 centre{};
                float count = 0.0F;
                for (const auto &row : rows)
                    if (r < row.size()) {
                        centre += row[r];
                        count += 1.0F;
                    }
                centre = centre * (1.0F / count);
                const Vec3 chord = pb - pa;
                const float lc = length(chord), t = arc.t[r];
                Vec3 out = (pa + pb) * 0.5F - centre;
                if (lc > 1e-6F)
                    out = out - chord * (dot(out, chord) / (lc * lc));
                const float lo = length(out);
                const auto i = static_cast<std::size_t>(arc.particles[r]);
                locked[i] =
                    pa + chord * t + (lo > 1e-6F ? out * (arc.bow[r] * lc * 4.0F * t * (1.0F - t) / lo) : Vec3{});
                done[i] = 1;
            }
        for (std::size_t i = 0; i < n; ++i)
            if (done[i]) {
                state.game_targets[i] = locked[i];
                state.game_weight[i] = 1.0F;
            }
        a.game_targets = &state.game_targets;
        a.game_weight = &state.game_weight;
        if (!m.game_shell.empty()) {
            a.game_shell = &m.game_shell;
            a.game_shell_open = &m.game_shell_open;
        }
    }
    state.actor->update(a);
    if (const u32 mark = khdays_log_marks(); mark != state.marked) {
        state.marked = mark;
        log_room_contacts(m.name, mark, in.actor, root, units, state.actor->simulation(), room_.describe());
    }
    std::fill(m.written.begin(), m.written.end(), std::uint8_t{0});
    for (const int b : rig.needed)
        m.out[static_cast<std::size_t>(b)] = m.pose[static_cast<std::size_t>(b)];
    state.actor->output(m.pose, m.out, m.written);
    // the front's middle turned halfway between the bones beside it too (the slit's top between its neighbours),
    // its tail kept on its point
    if (coat_locked)
        for (const auto &chain : m.game_chains) {
            if (chain.between_a < 0)
                continue;
            const auto &ca = m.game_chains[static_cast<std::size_t>(chain.between_a)];
            const auto &cb = m.game_chains[static_cast<std::size_t>(chain.between_b)];
            for (std::size_t q = 1; q <= chain.ends[0] && q < ca.particles.size() && q < cb.particles.size(); ++q) {
                const Particle &p = rig.particles[static_cast<std::size_t>(chain.particles[q])];
                const auto b = static_cast<std::size_t>(p.bone);
                const auto ba = static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(ca.particles[q])].bone),
                           bb = static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(cb.particles[q])].bone);
                if (!m.written[b] || !m.written[ba] || !m.written[bb])
                    continue;
                const Vec3 tail = apply(m.out[b], p.offset);
                m.out[b].q = nlerp(m.out[ba].q, m.out[bb].q, 0.5F);
                m.out[b].t = Vec3{};
                m.out[b].t = tail - apply(m.out[b], p.offset);
            }
        }
    if (m.trace)
        trace_draw(m, state, a, now);
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

void clip_volumes(ModelSecondary &m, const std::vector<Mat4> &skin, std::vector<std::array<Vec3, 3>> &out)
{
    out.clear();
    if (m.clip_rig != m.rig.get())
        build_clip(m);
    if (!m.rig)
        return;
    const Rig &rig = *m.rig;
    std::vector<Vec3> at, normal;
    for (const ClipVolume &cv : m.clip) {
        const std::size_t n = cv.particles.size();
        at.assign(n + 2, {});
        normal.assign(n + 2, {});
        float scale = 1.0F; // view units a model unit
        bool whole = true;
        for (std::size_t k = 0; k < n && whole; ++k) {
            const Particle &p = rig.particles[static_cast<std::size_t>(cv.particles[k])];
            const auto b = static_cast<std::size_t>(p.bone);
            const int j = b < m.joint_of_bone.size() ? m.joint_of_bone[b] : -1;
            whole = j >= 0 && static_cast<std::size_t>(j) < skin.size();
            if (!whole)
                break;
            const Mat4 &s = skin[static_cast<std::size_t>(j)];
            at[k] = point(s, point(m.bind_inverse[b], p.offset));
            scale = length(Vec3{s[0], s[1], s[2]});
        }
        if (!whole)
            continue;
        for (auto [ring, middle] : {std::pair{&cv.top, n}, std::pair{&cv.bottom, n + 1}})
            for (const int k : *ring)
                at[middle] += at[static_cast<std::size_t>(k)] * (1.0F / static_cast<float>(ring->size()));
        // that far inside the drawn controls: each corner in by its side faces' normals (area-weighted; the caps'
        // turned the hem's in and up, and the floor showed through the coat's last 1.5 cm)
        for (const auto &f : cv.faces) {
            if (static_cast<std::size_t>(f[0]) >= n || static_cast<std::size_t>(f[1]) >= n ||
                static_cast<std::size_t>(f[2]) >= n)
                continue;
            const Vec3 nrm = cross(at[static_cast<std::size_t>(f[1])] - at[static_cast<std::size_t>(f[0])],
                                   at[static_cast<std::size_t>(f[2])] - at[static_cast<std::size_t>(f[0])]);
            for (const int k : f)
                normal[static_cast<std::size_t>(k)] += nrm;
        }
        for (std::size_t k = 0; k < n; ++k)
            at[k] -= normalized(normal[k], {}) * (cv.inset * scale);
        for (const auto &f : cv.faces)
            out.push_back({at[static_cast<std::size_t>(f[0])], at[static_cast<std::size_t>(f[1])],
                           at[static_cast<std::size_t>(f[2])]});
    }
}

} // namespace khdays::secondary
