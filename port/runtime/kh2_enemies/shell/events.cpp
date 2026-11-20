// Events, cameras and the screen: event cameras, the scope, shakes, the white, lights, pictures.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

void event_point(const Playing &p, const std::array<float, 3> &k, float out[3]);
} // namespace

// The sequence playing ends (sa::EVENT::event_end_sub, event_cancel: dk::EventCamera::end(m_interpolate)): the game's
// camera told how to take up when it was this game's player's
void end_event()
{
    if (!playing)
        return;
    if (playing->player != 0 && playing->player == local_player) {
        event_end = {};
        event_end.how = playing->give_back && playing->shown ? 2 : 1;
        std::copy(playing->shown_eye, playing->shown_eye + 3, event_end.eye);
        std::copy(playing->shown_at, playing->shown_at + 3, event_end.at);
        event_end.half = playing->shown_half;
    }
    playing.reset();
}

void screen_white_tick()
{
    if (screen_white.step == 0.0F)
        return;
    screen_white.level = std::clamp(screen_white.level + screen_white.step, 0.0F, 1.0F);
    if (screen_white.level <= 0.0F || screen_white.level >= 1.0F)
        screen_white.step = 0.0F;
    if (trace_level() >= 2)
        khdays_log("kh2 enemies: the screen's white at %.2f", double(screen_white.level));
}

// ryj::SHAKE::exec, a KH2 tick: each wave's count on by the tick (back by its period), its size by its speed (kept
// within 0 and 16), the offset sin(count / period x 2 pi - pi) x size; its life down by the tick, nothing at 0
void screen_shake_tick()
{
    if (!screen_shake)
        return;
    ScreenShake &k = *screen_shake;
    if (k.life <= 0.0F) {
        screen_shake.reset();
        return;
    }
    for (std::size_t i = 0; i < 2; ++i) {
        if (k.period[i] > 0.0F) {
            k.count[i] += 1.0F;
            while (k.count[i] >= k.period[i])
                k.count[i] -= k.period[i];
        }
        k.size[i] = std::clamp(k.size[i] + k.speed[i], 0.0F, 16.0F);
        k.offset[i] = k.period[i] > 0.0F
                          ? std::trunc(std::sin(k.count[i] / k.period[i] * 6.2831853F - 3.1415927F) * k.size[i])
                          : 0.0F;
    }
    k.life = std::max(0.0F, k.life - 1.0F);
}

// 1:157: the sequence of that name (FUN_1403db2f0: its letters packed as the entries' names are) in the enemy's
// .a.us, played by the one event player. In KH2 its camera's keys are set from the event's own matrix (+ 0x60 of its
// object, FUN_1403ad2d0), the identity until a command 0x51 calls back FUN_1403ac630, which copies the target's matrix
// (+ 0x808, FUN_140406a90): Cerberus's have none, so KH2 plays them in its arena's own place. Days's rooms are not
// that arena: the port sets them from the target's place and turn as the sequence starts -- the call back's binding
// (the actor's own without a target).
void Field::event(Actor &a, const std::string &name, Object *target)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return;
    Enemy &e = *it->second;
    std::uint32_t key = 0;
    for (std::size_t i = 0; i < name.size() && i < 4; ++i)
        key |= std::uint32_t(std::uint8_t(name[i])) << (8 * i);
    const auto found = e.kind->events.find(key);
    if (found == e.kind->events.end()) {
        khdays_log("kh2 enemies: %08x has no event sequence \"%s\" (its .a.us's entries of type 0x16)", e.days,
                   name.c_str());
        return;
    }
    Playing p;
    p.event = found->second;
    p.enemy = e.days;
    p.player = e.took;
    for (const auto &[object, h] : held)
        if (h.enemy == e.days)
            p.player = object;
    const Object &from = target != nullptr ? *target : static_cast<const Object &>(a);
    to_days(from.place(), p.base);
    p.yaw = from.yaw();
    if (tracing()) {
        std::string unplayed;
        for (const int c : p.event->commands)
            if (c != 0 && c != 6 && c != 8 && c != 0xf && c != 0x10 && c != 0x13 && c != 0x17 && c != 0x27 &&
                c != 0x31 && c != 0x4c)
                unplayed += " " + std::to_string(c);
        for (const kh2::EventTimed &t : p.event->timed)
            if (t.command == 0x31 && t.objcamera != 0)
                unplayed += " 49 (+ 6 " + std::to_string(t.objcamera) + ")";
        khdays_log("kh2 enemies: %08x plays event sequence %s (\"%s\", %d frames) for player %08x%s%s", e.days,
                   p.event->name.c_str(), name.c_str(), p.event->length, p.player,
                   unplayed.empty() ? "" : "; commands not played:", unplayed.c_str());
    }
    // its camera from the game's (0x4c: dk::EventCamera::start, the game's camera's look-at, eye and field of view as
    // the springs' start and targets), else a cut to its keys
    float field_eye[3], field_at[3], field_half = 0.0F;
    if (p.event->start_camera && khdays_kh2_camera_view(field_eye, field_at) && khdays_kh2_camera_fov(&field_half) &&
        field_half > 0.0F) {
        Playing::Springs s;
        for (std::size_t k = 0; k < 3; ++k) {
            s.at[k] = field_at[k] * kKh2PerDays;
            s.eye[k] = field_eye[k] * kKh2PerDays;
        }
        s.fov = {2.0F * field_half, 0.0F};
        s.at_target = s.at_before = s.at;
        s.eye_target = s.eye_before = s.eye;
        s.fov_target = s.fov_before = s.fov;
        p.springs = s;
    }
    event_end = {}; // (a sequence after another: the game's camera does not take up between)
    playing = std::move(p);
}
namespace {

// A shake's offset this tick (FUN_140528920): each axis at random within half its amplitude either way, times the
// envelope at the ticks gone
std::array<float, 3> shake_offset(const Playing::Shake &s)
{
    static std::uint32_t seed = 0x2545f491u;
    const float envelope = kh2::event_shake_envelope(s.shake.shape, float(s.gone), float(s.shake.ticks));
    std::array<float, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float r = float(seed >> 8) / float(1u << 24);
        out[i] = (r * s.shake.amplitude[i] - s.shake.amplitude[i] * 0.5F) * envelope;
    }
    return out;
}

// dk::ObjCamera::springDamp(rate, 2.5, 0.5, length, ...): `current` moved toward `target` -- d its way from the target,
// current + d / |d| x rate x (2.5 (length - |d|) + 0.5 (d . (before - target) rate) / |d|), `before` the target before
template <std::size_t N>
std::array<float, N> spring_damp(const std::array<float, N> &current, const std::array<float, N> &target,
                                 const std::array<float, N> &before, float rate, float length)
{
    float away = 0.0F, along = 0.0F;
    for (std::size_t i = 0; i < N; ++i) {
        const float d = current[i] - target[i];
        away += d * d;
        along += d * (before[i] - target[i]) * rate;
    }
    away = std::sqrt(away);
    if (away == 0.0F)
        return current;
    const float force = 2.5F * (length - away) + 0.5F * (along / away);
    std::array<float, N> out{};
    for (std::size_t i = 0; i < N; ++i)
        out[i] = current[i] + (current[i] - target[i]) / away * rate * force;
    return out;
}
} // namespace

void event_tick(const Enemy &e)
{
    if (!playing || playing->enemy != e.days)
        return;
    Playing &p = *playing;
    const int frame = int((p.ticks + 1) / 2);
    while (p.next < p.event->timed.size() && p.event->timed[p.next].frame <= frame) {
        const kh2::EventTimed &c = p.event->timed[p.next++];
        if (c.command == 0x17 && c.trail.stop) {
            p.trail.reset(); // FUN_14015f170(0)
        } else if (c.command == 0x17) {
            Playing::Trail tr;
            tr.alpha = float(c.trail.alpha) / 128.0F;
            tr.size[0] = float(c.trail.width) / 512.0F; // of KH2's 512 x 416 screen (FUN_14015f1e0)
            tr.size[1] = float(c.trail.height) / 416.0F;
            tr.turn = c.trail.turn;
            tr.turn_ticks = c.trail.turn_ticks != 0 ? c.trail.turn_ticks : 1;
            p.trail = tr;
        } else if (c.command == 0x27) {
            p.shake = Playing::Shake{c.shake, 0, {}};
        } else if (c.command == 0x31 && c.objcamera == 0) {
            p.give_back = true; // sa::EVENT::set_bin_seq_objcamera: m_interpolate
        }
        if (tracing())
            khdays_log("kh2 enemies: event sequence %s plays command %#x at frame %d", p.event->name.c_str(), c.command,
                       frame);
    }
    if (p.trail && p.trail->turn != 0.0F && p.trail->turned < p.trail->turn_ticks) {
        p.trail->angle = std::remainder(p.trail->angle + p.trail->turn, 6.2831853F);
        ++p.trail->turned;
    }
    if (p.shake) {
        p.shake->offset = shake_offset(*p.shake);
        if (++p.shake->gone >= p.shake->shake.ticks)
            p.shake.reset();
    }
    if (trace_level() >= 2 && p.ticks % 30u == 0u) {
        float within_traced = 0.0F;
        if (const kh2::EventCamera *c = kh2::event_camera_at(*p.event, float(p.ticks) / 2.0F, within_traced)) {
            kh2::EventView view;
            kh2::event_view(*c, within_traced, view);
            float eye[3], at[3];
            event_point(p, view.eye, eye);
            event_point(p, view.at, at);
            float player[3] = {};
            if (const Object *o = Object::at(mod().memory, p.player))
                to_days(o->place(), player);
            khdays_log(
                "kh2 enemies: event sequence %s frame %u from %.2f %.2f %.2f turned %.2f: its camera at %.2f %.2f "
                "%.2f looking at %.2f %.2f %.2f, its player at %.2f %.2f %.2f (Days's)",
                p.event->name.c_str(), p.ticks / 2u, double(p.base[0]), double(p.base[1]), double(p.base[2]),
                double(p.yaw), double(eye[0]), double(eye[1]), double(eye[2]), double(at[0]), double(at[1]),
                double(at[2]), double(player[0]), double(player[1]), double(player[2]));
        }
    }
    // its camera's springs once a field update (sa::EVENT::camera_ctrl, KH2's 30 a second: every other tick) toward its
    // keys at this frame (dk::EventCamera::lookAt: the look-at at 0.1, the eye at 0.07, the field of view and the roll
    // at 0.07 about 0.001); no cut holding the frame, they stand
    float within = 0.0F;
    const kh2::EventCamera *cam =
        p.springs && p.ticks % 2u == 0u ? kh2::event_camera_at(*p.event, float(p.ticks) / 2.0F, within) : nullptr;
    if (cam != nullptr) {
        kh2::EventView view;
        kh2::event_view(*cam, within, view);
        float eye[3], at[3];
        event_point(p, view.eye, eye);
        event_point(p, view.at, at);
        Playing::Springs &s = *p.springs;
        s.at_before = s.at_target;
        s.eye_before = s.eye_target;
        s.fov_before = s.fov_target;
        for (std::size_t k = 0; k < 3; ++k) {
            s.at_target[k] = at[k] * kKh2PerDays;
            s.eye_target[k] = eye[k] * kKh2PerDays;
        }
        s.fov_target = {view.fov, view.roll};
        s.at = spring_damp(s.at, s.at_target, s.at_before, 0.1F, 1.0F);
        s.eye = spring_damp(s.eye, s.eye_target, s.eye_before, 0.07F, 1.0F);
        s.fov = spring_damp(s.fov, s.fov_target, s.fov_before, 0.07F, 0.001F);
        if (tracing())
            khdays_log(
                "kh2 enemies: event sequence %s frame %u: its camera's springs eye %.0f %.0f %.0f at %.0f %.0f %.0f "
                "fov %.3f, its keys eye %.0f %.0f %.0f at %.0f %.0f %.0f fov %.3f",
                p.event->name.c_str(), p.ticks / 2u, s.eye[0], s.eye[1], s.eye[2], s.at[0], s.at[1], s.at[2], s.fov[0],
                s.eye_target[0], s.eye_target[1], s.eye_target[2], s.at_target[0], s.at_target[1], s.at_target[2],
                s.fov_target[0]);
    }
    if (++p.ticks >= 2u * unsigned(std::max(p.event->length, 0))) {
        if (tracing())
            khdays_log("kh2 enemies: event sequence %s over%s", playing->event->name.c_str(),
                       playing->give_back ? ", the camera given back from its last view" : "");
        end_event();
        // YS::EVENT::phase_end: the control of the player and his party given back (YS::OBJ::ControlOn 0 and 1) -- a
        // script takes it away for an event (1:42) and leaves it to the event's end to give back (Xigbar's scope)
        controls_off.erase(0);
        controls_off.erase(1);
    }
}
namespace {

// A point of an event's camera (KH2's axes from its base: the model's turned half round x) in Days's world: as an
// actor's own axes on no bone (days_point)
void event_point(const Playing &p, const std::array<float, 3> &k, float out[3])
{
    const Vec3 t = turned({k[0], 0.0F, -k[2]}, days_turn(p.yaw));
    out[0] = p.base[0] + t[0] / kKh2PerDays;
    out[1] = p.base[1] - k[1] / kKh2PerDays;
    out[2] = p.base[2] + t[2] / kKh2PerDays;
}
} // namespace

} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

// KH2's reaction camera (runtime/camera/kh2_camera_hooks.c): while a KH2 enemy holds this game's player (YS::OBJ::
// can_control off) and the object its script watches (1:207) stands (TARGET::is_exist), his place and that object's
// aim point (TARGET::get_camera_pos), Days's world; 0 otherwise (dk::ReactionCamera::update returns the camera's mode)
extern "C" int khdays_kh2_enemies_reaction_camera(float player[3], float target[3])
{
    if (watched == 0 || local_player == 0)
        return 0;
    const auto h = held.find(local_player);
    Object *o = Object::at(mod().memory, watched);
    Vec3 aim{};
    if (h == held.end() || o == nullptr || !the_field().aim_point(*o, aim))
        return 0;
    to_days(h->second.place, player);
    to_days(aim, target);
    return 1;
}
namespace khdays::kh2_enemies::shell {

namespace {

// The camera of the event sequence playing, while it is this game's player's (runtime/camera/kh2_camera_hooks.c): its
// eye and look-at in Days's world, half its vertical field of view (0: the camera's own kept); 0 when none shows
// The scope camera's eye and look-at (Scope), Days's world; false when none stands (its object gone ends it)
bool scope_view(float eye[3], float at[3])
{
    if (!scope)
        return false;
    Object *o = Object::at(mod().memory, scope->object);
    if (o == nullptr) {
        scope.reset();
        return false;
    }
    const Vec3 place = o->place(), &t = scope->target;
    const Vec3 head = {place[0], place[1] - scope->height, place[2]}; // KH2's y down
    Vec3 near = {head[0] - t[0], head[1] - t[1], head[2] - t[2]};
    const float l = std::sqrt(near[0] * near[0] + near[1] * near[1] + near[2] * near[2]);
    for (std::size_t i = 0; i < 3; ++i)
        near[i] = t[i] + (l > 1e-6F ? near[i] / l : 0.0F) * scope->closeup;
    Vec3 e{};
    for (std::size_t i = 0; i < 3; ++i)
        e[i] = near[i] * scope->zoom + head[i] * (1.0F - scope->zoom);
    to_days(e, eye);
    to_days(t, at);
    if (static unsigned told = 0; tracing() && khdays_link_frame() - told >= 30u) {
        told = khdays_link_frame();
        khdays_log(
            "kh2 enemies: the scope camera: from %.0f %.0f %.0f (head %.0f %.0f %.0f) at %.0f %.0f %.0f, zoom %.2f, "
            "close-up %.0f",
            double(e[0]), double(e[1]), double(e[2]), double(head[0]), double(head[1]), double(head[2]), double(t[0]),
            double(t[1]), double(t[2]), double(scope->zoom), double(scope->closeup));
    }
    return true;
}

// The screen's shake (ScreenShake) on a camera of the KH2 enemies': its eye and look-at moved across the view so what
// it looks at moves by the offset's dots on KH2's 512 x 416 screen (its height the field of view's: `half` its half)
void shake_view(float eye[3], float at[3], float half)
{
    if (!screen_shake || (screen_shake->offset[0] == 0.0F && screen_shake->offset[1] == 0.0F) || half <= 0.0F)
        return;
    const float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const float distance = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    const float flat = std::sqrt(f[0] * f[0] + f[2] * f[2]);
    if (distance <= 1e-6F || flat <= 1e-6F)
        return;
    const float right[3] = {-f[2] / flat, 0.0F, f[0] / flat};
    const float tall = std::tan(half) * distance;
    const float across = screen_shake->offset[0] / 256.0F * tall * (4.0F / 3.0F);
    const float down = screen_shake->offset[1] / 208.0F * tall;
    for (int i = 0; i < 3; ++i) {
        const float by = -right[i] * across + (i == 1 ? down : 0.0F); // the picture right and down: the eye left, up
        eye[i] += by;
        at[i] += by;
    }
}
} // namespace

} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

extern "C" int khdays_kh2_enemies_event_camera(float eye[3], float at[3], float *fov_half)
{
    float camera_half = 0.0F;
    khdays_kh2_camera_fov(&camera_half);
    const kh2::EventCamera *cam = nullptr;
    float within = 0.0F;
    if (playing && playing->player != 0 && playing->player == local_player)
        cam = kh2::event_camera_at(*playing->event, float(playing->ticks) / 2.0F, within);
    if (cam == nullptr) {
        if (!scope_view(eye, at))
            return 0;
        *fov_half = 0.0F; // the camera's own
        shake_view(eye, at, camera_half);
        return 1;
    }
    kh2::EventView view;
    kh2::event_view(*cam, within, view);
    event_point(*playing, view.eye, eye);
    event_point(*playing, view.at, at);
    *fov_half = view.fov * 0.5F;
    if (const auto &s = playing->springs) { // from the game's camera (command 0x4c)
        for (std::size_t k = 0; k < 3; ++k) {
            eye[k] = s->eye[k] / kKh2PerDays;
            at[k] = s->at[k] / kKh2PerDays;
        }
        *fov_half = s->fov[0] * 0.5F;
    }
    playing->shown = true;
    std::copy(eye, eye + 3, playing->shown_eye);
    std::copy(at, at + 3, playing->shown_at);
    playing->shown_half = *fov_half;
    shake_view(eye, at, *fov_half);
    if (playing->shake) {
        // the eye moved in the camera's own axes: x and z turned with its yaw, y up (FUN_140528920)
        const float dx = at[0] - eye[0], dz = at[2] - eye[2], flat = std::sqrt(dx * dx + dz * dz);
        const float ax = flat > 1e-6F ? dx / flat : 0.0F, az = flat > 1e-6F ? dz / flat : 1.0F;
        const auto &o = playing->shake->offset;
        eye[0] += (o[0] * az + o[2] * ax) / kKh2PerDays;
        eye[1] += o[1] / kKh2PerDays;
        eye[2] += (-o[0] * ax + o[2] * az) / kKh2PerDays;
    }
    return 1;
}

extern "C" int khdays_kh2_enemies_event_end_view(float eye[3], float at[3], float *fov_half)
{
    const EventEnd e = event_end;
    event_end = {};
    if (e.how == 2) {
        std::copy(e.eye, e.eye + 3, eye);
        std::copy(e.at, e.at + 3, at);
        *fov_half = e.half;
    }
    return e.how;
}

// The screen's trail a sequence playing for this game's player has on (runtime/display/frame_package.h, `trail`): its
// alpha, its quad's width and height over the view's, its turn in radians; 0 when none
extern "C" int khdays_kh2_enemies_screen_trail(float out[4])
{
    if (!playing || !playing->trail || playing->player == 0 || playing->player != local_player)
        return 0;
    out[0] = playing->trail->alpha;
    out[1] = playing->trail->size[0];
    out[2] = playing->trail->size[1];
    out[3] = playing->trail->angle;
    return 1;
}

extern "C" float khdays_kh2_enemies_screen_white(void)
{
    return screen_white.level;
}

extern "C" int khdays_kh2_enemies_camera_warp(void)
{
    const bool warped = camera_warped;
    camera_warped = false;
    return warped ? 1 : 0;
}

// Whether the room's models are left out: a KH2 enemy's script hid the map (8:6) and has not shown it again (8:7); with
// none of its enemies left (its battle over before 8:7) it is shown
extern "C" int khdays_kh2_enemies_map_hidden(void)
{
    if (map_hidden && enemies.empty() && ghosts.empty())
        map_hidden = false;
    return map_hidden ? 1 : 0;
}

extern "C" int khdays_kh2_enemies_light(float room[3], float objects[3])
{
    if (lights.size() == 0)
        return 0;
    if (enemies.empty() && ghosts.empty()) {
        lights.clear(); // KH2 keeps a light till its script fades it; none here is left to
        return 0;
    }
    const Lights::Colours &c = lights.colours();
    for (std::size_t i = 0; i < 3; ++i) {
        room[i] = float(c.room[i]) / 128.0F;
        objects[i] = float(c.objects[i]) / 128.0F;
    }
    return c.any ? 1 : 0;
}

// KH2's object pictures shown (Picture), for ui/kh2_pictures.cpp: how many, those whose object is gone left out first
// (a dk::Layout outlives no object here: the port's choice, its script's 1:283 or layout 3 never coming then)
extern "C" int khdays_kh2_enemies_picture_count(void)
{
    if (pictures.empty())
        return 0;
    for (auto it = pictures.begin(); it != pictures.end();)
        if (Object::at(mod().memory, it->second.object) == nullptr)
            it = pictures.erase(it);
        else
            ++it;
    return int(pictures.size());
}

extern "C" int khdays_kh2_enemies_picture(int index, unsigned *handle, int *number, unsigned *changes, float place[2],
                                          char *archive, int archive_room, char *stem, int stem_room, int *first_page)
{
    if (index < 0 || std::size_t(index) >= pictures.size() || archive == nullptr || stem == nullptr ||
        archive_room <= 0 || stem_room <= 0)
        return 0;
    const auto &[key, p] = *std::next(pictures.begin(), index);
    if (p.kind == nullptr)
        return 0;
    const KindFiles &k = *p.kind;
    const std::u8string &path = kind_archive(k);
    const std::string pages = "kh2/fx/" + k.name + "/-";
    if (path.empty() || path.size() >= std::size_t(archive_room) || pages.size() >= std::size_t(stem_room))
        return 0;
    std::memcpy(archive, path.data(), path.size());
    archive[path.size()] = '\0';
    std::memcpy(stem, pages.data(), pages.size());
    stem[pages.size()] = '\0';
    *handle = key;
    *number = p.number;
    *changes = p.changes;
    place[0] = p.x;
    place[1] = p.y;
    *first_page = k.fx_hd_pages;
    return 1;
}

extern "C" void khdays_kh2_enemies_picture_over(unsigned handle)
{
    if (pictures.erase(handle) != 0 && tracing())
        khdays_log("kh2 enemies: picture %08x over (its layout played out)", handle);
}
