// Drawing: KH2 models and effects in Days's 3D, the players held, the ghosts of the fallen.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

// How the model whose draw begins is placed, from the matrices the game left (khdays_g3d.h): three ways are seen --
// the position matrix the model's place in the world, the camera folded into the projection (Days's enemies, Ms/:
// traced); after FlushP the position the identity, the place (the flush's base) and the camera folded into the
// projection; after Flush the position the camera times the place.
enum class Way { None, World, Folded, Camera };

struct DrawSpace final {
    float position[16], projection[16], base[16], camera[16];
    bool has_camera = false;
    int flush = 0; // khdays_g3d_base's
};
} // namespace

// How much of its model is drawn: its fade (1:19, 1:20, 1:28), a quarter of it in stealth (1:194); none hidden (1:133)
float drawn_fade(const Enemy &e)
{
    if (e.actor->hidden)
        return 0.0F;
    const float fade = std::clamp(e.actor->fade, 0.0F, 1.0F);
    return e.actor->stealth != 0 ? fade * 0.25F : fade;
}
namespace {

// The share of an effect's alpha its enemy's fade leaves it: an element bound to it whose flags say it fades with it
// (+ 0x10 bits 0 and 11: ryj::PAX::MANAGER::exec runs set_obj_fade then, the effect's colour's alpha the object's
// fade alpha each tick -- YS::OBJ::get_fade_alpha --, 0 while it is hidden, YS::OBJ::is_hide) its fade, every other 1.
// Final Xemnas's blades (elements 1, 2, 3, 5, 45): his double fades out but for its dance and its lasers, and they
// with it
float effect_object_alpha(const Enemy &e, const Enemy::Running &r)
{
    if ((r.element->flags & 0x801u) != 0x801u || r.in_world)
        return 1.0F;
    return e.actor->hidden ? 0.0F : std::clamp(e.actor->fade, 0.0F, 1.0F);
}
} // namespace

// How many of its PAX's elements, from the first, KH2 starts on an object's model as it is made
// (YS::OBJDATA::start_model_effect, the PC's FUN_1403e6470, each as 1:87 starts one: FUN_1402c6b30): by its object
// entry's + 0x4c (`record`, the copy at its actor's + 8) -- 0x3f8 and 0x138a element 0 (Hades's hair's flame, at the
// top of his head's bone 103), 0x1395 0 to 2, 0x13a2 0 to 8 (Scar's ghosts), 0x13ca 0 to 13 -- else by its number (+ 0): 0x888,
// 0x88a to 0x88c and 0x97c element 0, 0x889 0 to 3; none for any other
int model_effects(std::uint32_t record)
{
    if (record == 0)
        return 0;
    std::uint16_t key = 0;
    mod().memory.read(record + 0x4c, &key, 2);
    switch (key) {
    case 0x3f8:
    case 0x138a: return 1;
    case 0x1395: return 3;
    case 0x13a2: return 9;
    case 0x13ca: return 14;
    default: break;
    }
    const std::uint32_t number = mod().memory.u32(record);
    if (number == 0x888 || (number >= 0x88a && number <= 0x88c) || number == 0x97c)
        return 1;
    return number == 0x889 ? 4 : 0;
}

void row_matrix_mul(const float a[16], const float b[16], float out[16])
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float v = 0.0F;
            for (int k = 0; k < 4; ++k)
                v += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = v;
        }
}
namespace {

bool near_identity(const float m[16])
{
    for (int i = 0; i < 16; ++i)
        if (std::fabs(m[i] - (i % 5 == 0 ? 1.0F : 0.0F)) > 1e-4F)
            return false;
    return true;
}
} // namespace

// The inverse of a 3x3 and a translation (the last row; row vectors); the identity for a singular one
void inverse_affine(const float m[16], float out[16])
{
    const auto at = [&](int r, int c) { return m[r * 4 + c]; };
    const float det = at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
                      at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
                      at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
    std::fill(out, out + 16, 0.0F);
    out[15] = 1.0F;
    if (std::fabs(det) < 1e-12F) {
        out[0] = out[5] = out[10] = 1.0F;
        return;
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
        out[12 + c] = -(at(3, 0) * out[c] + at(3, 1) * out[4 + c] + at(3, 2) * out[8 + c]);
}
namespace {

// KH2's camera for an enemy's effects, its model drawn at `view` (its model to the view, Days's units) and
// `projection`: the effects run in KH2's axes (y down) and centimetres, at the enemy (their world the identity); their
// view the enemy's, its axes KH2's and shrunk by `units` (Days's units a KH2 unit, the camera's scale with it) so
// that their sprites keep KH2's size; their projection Days's, back in Days's axes and units. What they draw comes out
// of the clip space divided by `units` (kh2_fx::submit multiplies it back) -- as KH2's Dark Corridor's
// (runtime/mods/kh2_corridor_fit.h corridor_camera).
kh2::EffectCamera effect_camera(const float view_in[16], const float projection[16], float &units)
{
    static constexpr float model[3] = {1.0F, -1.0F, 1.0F}; // the effects' axes to the enemy's own (KH2's y down)
    static constexpr float turn[3] = {1.0F, -1.0F, -1.0F}; // Days's view's to KH2's
    float view[16];
    std::copy(view_in, view_in + 16, view);
    float length = 0.0F;
    for (int r = 0; r < 3; ++r)
        length += std::sqrt(view[r * 4] * view[r * 4] + view[r * 4 + 1] * view[r * 4 + 1] +
                            view[r * 4 + 2] * view[r * 4 + 2]) /
                  3.0F;
    if (!(length > 1e-6F) || !std::isfinite(length))
        length = 1.0F;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            view[r * 4 + k] /= length;
    units = length / kKh2PerDays;
    kh2::EffectCamera cam;
    cam.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    cam.view = {};
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            cam.view[std::size_t(r * 4 + k)] = model[r] * view[r * 4 + k] * turn[k];
    for (int k = 0; k < 3; ++k)
        cam.view[std::size_t(12 + k)] = view[12 + k] / units * turn[k];
    cam.view[15] = 1.0F;
    for (int k = 0; k < 4; ++k) {
        for (int r = 0; r < 3; ++r)
            cam.projection[std::size_t(r * 4 + k)] = turn[r] * projection[r * 4 + k];
        cam.projection[std::size_t(12 + k)] = projection[12 + k] / units;
    }
    return cam;
}
} // namespace

// A KH2 model to the world, in the draw's way (row vectors, Days's axes, y up): its axes turned by its yaw, or by its
// turns about all three (8:2) -- x, then y, then z, as an element's (element_world), KH2's y down making the x and z
// turns the other way round in Days's axes -- at `place` (Days's units)
void model_matrix(const Actor &a, const float place[3], float out[16])
{
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float yaw = days_turn(a.yaw());
    const Vec3 turns = a.turned_all ? Vec3{-a.turn_all[0], yaw, -a.turn_all[2]} : Vec3{0.0F, yaw, 0.0F};
    for (int axis = 0; axis < 3; ++axis) {
        const float angle = turns[std::size_t(axis)];
        if (angle == 0.0F)
            continue;
        const float c = std::cos(angle), n = std::sin(angle);
        std::array<float, 16> r{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const int i = (axis + 1) % 3, j = (axis + 2) % 3;
        r[std::size_t(i * 4 + i)] = c;
        r[std::size_t(i * 4 + j)] = n;
        r[std::size_t(j * 4 + i)] = -n;
        r[std::size_t(j * 4 + j)] = c;
        std::array<float, 16> t{};
        row_matrix_mul(m.data(), r.data(), t.data());
        m = t;
    }
    for (int k = 0; k < 3; ++k)
        m[std::size_t(12 + k)] = place[k];
    std::copy(m.begin(), m.end(), out);
}

// An element's place in its model's effects' space (KH2's axes, y down, cm; the model's root at 0, turned as it is):
// its size, its turn (x, then y, then z), its place, then the bone it follows (`pose`'s, KH2's model space y up)
kh2::EffectMatrix element_world(const std::vector<kh2::Mat4> &pose, const Element &el)
{
    const auto turn = [](int axis, float a) {
        const float c = std::cos(a), n = std::sin(a);
        std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const int i = (axis + 1) % 3, j = (axis + 2) % 3;
        m[std::size_t(i * 4 + i)] = c;
        m[std::size_t(i * 4 + j)] = n;
        m[std::size_t(j * 4 + i)] = -n;
        m[std::size_t(j * 4 + j)] = c;
        return m;
    };
    std::array<float, 16> m{el.size[0], 0, 0, 0, 0, el.size[1], 0, 0, 0, 0, el.size[2], 0, 0, 0, 0, 1};
    for (int axis = 0; axis < 3; ++axis) {
        if (el.turn[std::size_t(axis)] == 0.0F)
            continue;
        std::array<float, 16> t;
        row_matrix_mul(m.data(), turn(axis, el.turn[std::size_t(axis)]).data(), t.data());
        m = t;
    }
    for (int k = 0; k < 3; ++k)
        m[std::size_t(12 + k)] += el.place[std::size_t(k)];
    if (el.bone >= 0 && el.bone < kNoBone && std::size_t(el.bone) < pose.size()) {
        // the bone's own axes as they are, the space's y down: KH2 (FUN_1401a7af0) turns only the bone matrix's
        // columns -- its y and z by -1 into its world; here its y, the effects' space being the model's with y turned
        // (effect_camera) --, never the bone's frame, in which the element's place and turn are made. Turning the
        // frame too put Hades's flame's layers (8 to 12 along the bone's -y: back over his swept hair) at his forehead
        std::array<float, 16> bone;
        std::copy(pose[std::size_t(el.bone)].begin(), pose[std::size_t(el.bone)].end(), bone.begin());
        for (int r = 0; r < 4; ++r)
            bone[std::size_t(r * 4 + 1)] = -bone[std::size_t(r * 4 + 1)];
        std::array<float, 16> t;
        row_matrix_mul(m.data(), bone.data(), t.data());
        m = t;
    }
    kh2::EffectMatrix out;
    std::copy(m.begin(), m.end(), out.begin());
    return out;
}
namespace {

// A held player's pose now: his motion's (P_EX100's skeleton), empty without it
std::vector<kh2::Mat4> held_pose(const Held &h)
{
    const kh2::MotionSkeleton *skeleton = player_skeleton();
    if (skeleton == nullptr || h.body == nullptr || !h.body->motion)
        return {};
    static std::unique_ptr<kh2::MotionPlayer> player;
    if (!player)
        player = std::make_unique<kh2::MotionPlayer>(*skeleton);
    return pose_at(*player, *h.body->motion, h.time);
}

// A held player's weapon now, in his model's space (KH2's cm, y up, his root's travel taken out): the bones his KH2
// model drew it with this frame or the last (held_weapon_drawn: its weapon's in its own hand), else KH2's keyblade's
// bones (W_EX010's skeleton) in his weapon's motion's pose ('0011', in the keyblade's own axes: its grip barely moves)
// on his hand's bone (00objentry.bin and 03system.bin's sklt: P_EX100's 178), as KH2 attaches its weapon object; empty
// without the keyblade's files or a weapon motion
std::vector<kh2::Mat4> held_weapon_pose(const Held &h, unsigned object)
{
    if (const auto d = drawn_weapons.find(object);
        d != drawn_weapons.end() && !d->second.bones.empty() && khdays_link_frame() - d->second.frame <= 2u)
        return d->second.bones;
    const PlayerFiles &files = player_files();
    if (!files.weapon || files.hand < 0 || h.weapon == nullptr || !h.weapon->motion)
        return {};
    const std::vector<kh2::Mat4> body = held_pose(h);
    if (std::size_t(files.hand) >= body.size())
        return {};
    static std::unique_ptr<kh2::MotionPlayer> player;
    if (!player)
        player = std::make_unique<kh2::MotionPlayer>(*files.weapon);
    std::vector<kh2::Mat4> out;
    try {
        out = pose_at(*player, *h.weapon->motion, h.time);
    } catch (const std::exception &x) { // a weapon motion made for another skeleton
        if (tracing())
            khdays_log("kh2 enemies: the held player's weapon motion cannot be played on W_EX010 (%s)", x.what());
        return {};
    }
    const kh2::Mat4 &hand = body[std::size_t(files.hand)];
    for (kh2::Mat4 &bone : out) { // the hand's frame, then the bone's in it (row vectors: the bone's, then the hand's)
        float on[16];
        row_matrix_mul(bone.data(), hand.data(), on);
        std::copy(on, on + 16, bone.begin());
    }
    return out;
}

// An element's two points (Element::points: a swing trail's base and tip) in its model's effects' space (KH2's axes, y
// down, cm, as element_world's): each on its bone in `pose` (KH2's model space, y up), its `along` times the bone's
// length (its skeleton's scale w) out along the bone's x (YS::OBJ::get_bone_pos), with its offset turned by the bone when
// the element has them (+ 0x4c; ryj::PAX::MANAGER::get_obj_data); none when a bone is not the pose's
std::vector<kh2::EffectPoint> element_points(const std::vector<kh2::Mat4> &pose, const kh2::MotionSkeleton &skeleton,
                                             const Element &el)
{
    std::vector<kh2::EffectPoint> out;
    for (int i = 0; i < 2; ++i) {
        const int b = el.point_bone[std::size_t(i)];
        if (b < 0 || std::size_t(b) >= pose.size() || std::size_t(b) >= skeleton.joints.size())
            return {};
        const kh2::Mat4 &m = pose[std::size_t(b)]; // column-major: its x axis m[0..2], its place m[12..14]
        const float along = el.point_along[std::size_t(i)] * skeleton.joints[std::size_t(b)].scale[3];
        const std::array<float, 3> &offset = i == 0 ? el.place : el.turn;
        kh2::EffectPoint p;
        for (int k = 0; k < 3; ++k) {
            float v = m[std::size_t(k)] * along + m[std::size_t(12 + k)];
            if (el.point_offsets)
                v += m[std::size_t(k)] * offset[0] + m[std::size_t(4 + k)] * offset[1] +
                     m[std::size_t(8 + k)] * offset[2];
            p.place[std::size_t(k)] = k == 1 ? -v : v; // y up -> KH2's y down
        }
        out.push_back(p);
    }
    return out;
}

// A held player's model's effects' space (his model's axes, y down, cm; his feet at 0) to his enemy's: his model placed
// and turned where he stands from the enemy (both in KH2's world, y down; a model's y up, its turn Actor's), half a
// turn more about the up as his model is drawn (the enemy's motions for him are made in the enemy's frame: play_held
// turns them so; without it his effects stood mirrored on him, his keyblade's trail at his other hand). An affine map,
// row vectors (a point times it)
void held_map(const Enemy &e, const Held &h, float map[16])
{
    const Vec3 at = e.actor->place();
    const float yaw = e.actor->yaw();
    const auto to_enemy = [&](const Vec3 &r) {
        const Vec3 t = turned({-r[0], 0.0F, -r[2]}, days_turn(h.yaw));
        const Vec3 place = legacy(h.place), from = legacy(at);
        const Vec3 d = {place[0] + t[0] - from[0], place[1] + r[1] - from[1], place[2] + t[2] - from[2]};
        const Vec3 m = turned({d[0], 0.0F, d[2]}, -days_turn(yaw));
        return Vec3{m[0], d[1], m[2]};
    };
    std::fill(map, map + 16, 0.0F);
    const Vec3 origin = to_enemy({0.0F, 0.0F, 0.0F});
    for (int r = 0; r < 3; ++r) {
        Vec3 unit{};
        unit[std::size_t(r)] = 1.0F;
        const Vec3 v = to_enemy(unit);
        for (int c = 0; c < 3; ++c)
            map[r * 4 + c] = v[std::size_t(c)] - origin[std::size_t(c)];
    }
    for (int c = 0; c < 3; ++c)
        map[12 + c] = origin[std::size_t(c)];
    map[15] = 1.0F;
}

// An element a held player's motion cast, in its enemy's effects' space: on KH2's player's bone (P_EX100's skeleton) in
// the pose his motion has now -- his weapon's (KH2's keyblade's) for one on his weapon -- and its two points when it
// has them (`points`, left as they are otherwise). False without KH2's player's skeleton (or the keyblade's).
bool held_world(const Enemy &e, const Held &h, const Enemy::Running &r, kh2::EffectMatrix &out,
                std::vector<kh2::EffectPoint> *points)
{
    const std::vector<kh2::Mat4> pose = r.on_weapon ? held_weapon_pose(h, r.player) : held_pose(h);
    if (pose.empty())
        return false;
    float map[16];
    held_map(e, h, map);
    const kh2::EffectMatrix own = element_world(pose, *r.element);
    row_matrix_mul(own.data(), map, out.data());
    const kh2::MotionSkeleton *skeleton = r.on_weapon ? player_files().weapon.get() : player_skeleton();
    if (points != nullptr && r.element->points && skeleton != nullptr) {
        *points = element_points(pose, *skeleton, *r.element);
        for (kh2::EffectPoint &p : *points) {
            const std::array<float, 3> was = p.place;
            for (int c = 0; c < 3; ++c)
                p.place[std::size_t(c)] = was[0] * map[c] + was[1] * map[4 + c] + was[2] * map[8 + c] + map[12 + c];
        }
    }
    return true;
}

// Where KH2 puts the mark of a reaction command a held player is offered (Cerberus's: on Sora in its jaws), in Days's
// world: his body's middle, his body spheres' (P_EX100's shapes 0x302) on his bones in his pose now, placed as his
// root's travel is (Held's place and yaw); his feet without KH2's player's skeleton or spheres
void held_middle(const Held &h, float out[3])
{
    to_days(h.place, out);
    const std::vector<kh2::Mat4> pose = held_pose(h);
    Vec3 c{};
    int n = 0;
    for (const Shape &s : player_files().spheres) {
        if (s.bone < 0 || std::size_t(s.bone) >= pose.size())
            continue;
        const kh2::Mat4 &m = pose[std::size_t(s.bone)];
        for (int i = 0; i < 3; ++i)
            c[std::size_t(i)] += m[std::size_t(i)] * s.at[0] + m[std::size_t(4 + i)] * s.at[1] +
                                 m[std::size_t(8 + i)] * s.at[2] + m[std::size_t(12 + i)];
        ++n;
    }
    if (n == 0)
        return;
    for (float &v : c)
        v /= float(n);
    const Vec3 t = turned({c[0], 0.0F, c[2]}, days_turn(h.yaw));
    out[0] += t[0] / kKh2PerDays;
    out[1] += c[1] / kKh2PerDays;
    out[2] += t[2] / kKh2PerDays;
}

// Points a part of the way (0 to 1) from one draw's to the next: a pair (a swing trail's base and tip) the base along
// the line and the tip from it turned on the way round between the two (its length along between), as a blade swings;
// others along the line
std::vector<kh2::EffectPoint> points_between(const std::vector<kh2::EffectPoint> &from,
                                             const std::vector<kh2::EffectPoint> &to, float part)
{
    std::vector<kh2::EffectPoint> out = to;
    for (std::size_t i = 0; i < out.size(); ++i)
        for (int j = 0; j < 3; ++j)
            out[i].place[std::size_t(j)] =
                from[i].place[std::size_t(j)] + (to[i].place[std::size_t(j)] - from[i].place[std::size_t(j)]) * part;
    if (out.size() < 2)
        return out;
    Vec3 a{}, b{};
    for (int j = 0; j < 3; ++j) {
        a[std::size_t(j)] = from[1].place[std::size_t(j)] - from[0].place[std::size_t(j)];
        b[std::size_t(j)] = to[1].place[std::size_t(j)] - to[0].place[std::size_t(j)];
    }
    const float la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    const float lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (la <= 0.0F || lb <= 0.0F)
        return out;
    const float cosine = std::clamp((a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb), -1.0F, 1.0F);
    const float angle = std::acos(cosine), sine = std::sin(angle);
    if (sine < 1e-4F) // the same way (or turned right round: the line)
        return out;
    const float wa = std::sin((1.0F - part) * angle) / sine, wb = std::sin(part * angle) / sine;
    const float length = la + (lb - la) * part;
    for (int j = 0; j < 3; ++j)
        out[1].place[std::size_t(j)] =
            out[0].place[std::size_t(j)] + (a[std::size_t(j)] / la * wa + b[std::size_t(j)] / lb * wb) * length;
    return out;
}

// KHDAYS_KH2_HIDE_ELEMENTS=n,n...: a diagnostic -- the KH2 effect elements of those numbers (the trace's "casts ...'s
// element n") run but are not drawn, to find which one draws what
bool hidden_element(const KindFiles &files, const Element *el)
{
    static const std::set<int> hidden = [] {
        std::set<int> out;
        if (const char *v = std::getenv("KHDAYS_KH2_HIDE_ELEMENTS")) {
            std::istringstream in(v);
            for (std::string t; std::getline(in, t, ',');)
                out.insert(std::atoi(t.c_str()));
        }
        return out;
    }();
    if (hidden.empty())
        return false;
    for (const auto &[number, element] : files.fx_elements)
        if (&element == el)
            return hidden.count(number) != 0;
    return false;
}

// KHDAYS_TRACE_KH2_ENEMIES=2: an element's draws that cross the screen as slivers (a triangle longer than half the
// screen, its area under a fiftieth of its longest edge squared) or have corners behind the camera, told once a
// second an element -- what a picture "cracked" across the screen is drawn by
void trace_slivers(const Enemy &e, const KindFiles &files, const Enemy::Running &r)
{
    static std::map<std::pair<unsigned, const Element *>, unsigned> told;
    int slivers = 0, behind = 0, all = 0;
    float longest = 0.0F;
    for (const kh2::EffectDraw &d : r.draws)
        for (std::size_t t = 0; t + 2 < d.triangles.size(); t += 3) {
            ++all;
            float x[3], y[3];
            bool back = false;
            for (int k = 0; k < 3; ++k) {
                const kh2::EffectVertex &v = d.triangles[t + std::size_t(k)];
                back = back || v.w <= 1e-6F;
                x[k] = v.w > 1e-6F ? v.x / v.w : 0.0F;
                y[k] = v.w > 1e-6F ? v.y / v.w : 0.0F;
            }
            if (back) {
                ++behind;
                continue;
            }
            float edge = 0.0F;
            for (int k = 0; k < 3; ++k) {
                const float dx = x[(k + 1) % 3] - x[k], dy = y[(k + 1) % 3] - y[k];
                edge = std::max(edge, std::sqrt(dx * dx + dy * dy));
            }
            const float area = std::fabs((x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0])) * 0.5F;
            if (edge > 1.0F && area < edge * edge * 0.02F) {
                ++slivers;
                longest = std::max(longest, edge);
            }
        }
    if (slivers == 0 && behind == 0)
        return;
    unsigned &last = told[{e.days, r.element}];
    if (khdays_link_frame() - last < 60u && last != 0)
        return;
    last = khdays_link_frame();
    int number = -1;
    for (const auto &[n, element] : files.fx_elements)
        if (&element == r.element)
            number = n;
    khdays_log("kh2 enemies: %08x's element %d (effect %zu, age %.0f): %d slivers (longest %.2f of the screen's 2), "
               "%d with corners behind the camera, of %d triangles",
               e.days, number, r.element->effect, double(r.age), slivers, double(longest), behind, all);
}
} // namespace

// Its effects moved on by KH2's ticks since its last draw (the game's frames, 60 a second: the corridor's way), each
// at its element's place, its sound started when its time comes; the last tick's drawn at the camera now; those over
// let go. Where it is drawn gives its sounds' pan.
void draw_effects(Enemy &e, const float view[16], const float projection[16])
{
    const float across = std::sqrt(view[12] * view[12] + view[14] * view[14]);
    e.pan = across > 1e-6F && std::isfinite(across) ? std::clamp(view[12] / across, -1.0F, 1.0F) : 0.0F;
    if (e.fx.empty())
        return;
    const unsigned now = khdays_link_frame();
    const unsigned per_frame = unsigned(std::max(1L, std::lround(khdays_link_frame_seconds() * 60.0)));
    const unsigned ticks = e.fx_frame == 0 ? per_frame : now >= e.fx_frame ? (now - e.fx_frame) * per_frame : per_frame;
    e.fx_frame = now;
    float units = 1.0F;
    const kh2::EffectCamera cam = effect_camera(view, projection, units);
    kh2::EffectCamera placed = cam;
    for (Enemy::Running &r : e.fx) {
        // its two points (a swing trail's base and tip), where its element has them: on its bones now
        std::vector<kh2::EffectPoint> points;
        if (r.player != 0) {
            if (const auto h = held.find(r.player);
                h != held.end() && h->second.enemy == e.days && held_world(e, h->second, r, r.last, &points))
                r.placed = true;
            if (!r.placed) { // no KH2 player's skeleton: at the enemy's root
                Element at = *r.element;
                at.bone = kNoBone;
                r.last = element_world(e.pose, at);
                r.placed = true;
            }
            placed.world = r.last;
        } else if (r.in_world) { // its script's place in the world, in the enemy's effects' space
            Element at = *r.element;
            at.bone = kNoBone;
            const Vec3 from = legacy(e.actor->place()), there = legacy(r.at);
            const float yaw = days_turn(e.actor->yaw());
            const Vec3 t = turned({there[0] - from[0], 0.0F, there[2] - from[2]}, -yaw);
            at.place = {t[0], there[1] - from[1], t[2]};
            if (r.turned) // (its turns so: about the up days_turn's, about its z the other way)
                at.turn = {r.turn[0], days_turn(r.turn[1]) - yaw, -r.turn[2]};
            if (r.scaled)
                at.size = r.scale;
            placed.world = element_world(e.pose, at);
        } else if (r.scaled) { // its script's size (0:39)
            Element sized = *r.element;
            sized.size = r.scale;
            placed.world = element_world(e.pose, sized);
        } else {
            placed.world = element_world(e.pose, *r.element);
            if (r.element->points && e.kind->skeleton) // the Magnum Loader's blades', the Neoshadow's claws' trails
                points = element_points(e.pose, *e.kind->skeleton, *r.element);
        }
        // KH2 moves its bones every tick and a swing's trail takes a pair of points each tick: Days draws once in
        // `ticks` of them, so the ticks between take theirs on the way from the last draw's to these (else each pair
        // stood `ticks` times in the trail's history and its curve went in steps)
        std::vector<kh2::EffectPoint> before;
        if (!points.empty()) {
            before = r.points_before.size() == points.size() ? r.points_before : points;
            r.points_before = points;
        }
        if (r.dome != 0 && ticks > 0)
            give_dome(e, r);
        for (unsigned t = 0; t < (r.paused ? 0u : ticks); ++t) {
            const bool last = t + 1 == ticks;
            if (!points.empty())
                r.fx->set_points(last ? points : points_between(before, points, float(t + 1) / float(ticks)));
            if (last)
                r.draws.clear();
            if (!r.sounded && r.age >= r.element->start) {
                r.sounded = true;
                if (r.element->sound > 0)
                    sound(e, r.element->sound);
            }
            r.fx->tick(placed, last ? &r.draws : nullptr);
            r.age += 1.0F;
        }
        if (r.dome != 0 && ticks > 0)
            take_dome(r);
        if (r.coloured && ticks > 0) // its script's colour (0:67): each corner's times it, once a tick's draws
            for (kh2::EffectDraw &d : r.draws)
                for (kh2::EffectVertex &v : d.triangles) {
                    v.r *= r.colour[0];
                    v.g *= r.colour[1];
                    v.b *= r.colour[2];
                    v.a *= r.colour[3];
                }
        if (r.fade_step != 0.0F && ticks > 0) { // its fading (EFFECT::fade), held or not: its draws' alpha its share
            // (a held one's draws are the last tick's, already at the share before: those times the change)
            const float was = r.fade;
            r.fade = std::max(0.0F, r.fade + r.fade_step * float(ticks));
            const float by = !r.paused ? r.fade : was > 0.0F ? r.fade / was : 0.0F;
            for (kh2::EffectDraw &d : r.draws)
                for (kh2::EffectVertex &v : d.triangles)
                    v.a *= by;
        }
        const KindFiles &files = r.files != nullptr ? *r.files : *e.kind;
        if (trace_level() >= 2)
            trace_slivers(e, files, r);
        const float with_object = effect_object_alpha(e, r);
        if (r.element->effect < files.fx_pages.size() && !hidden_element(files, r.element) && with_object > 0.0F) {
            if (with_object >= 1.0F) {
                khdays::kh2_fx::submit(r.draws, units, *files.fx_pages[r.element->effect]);
            } else { // (a copy: a held effect's draws stand from tick to tick)
                std::vector<kh2::EffectDraw> faded = r.draws;
                for (kh2::EffectDraw &d : faded)
                    for (kh2::EffectVertex &v : d.triangles)
                        v.a *= with_object;
                khdays::kh2_fx::submit(faded, units, *files.fx_pages[r.element->effect]);
            }
        }
    }
    for (std::size_t i = e.fx.size(); i-- > 0;)
        if (e.fx[i].fx->done() || (e.fx[i].fade_step != 0.0F && e.fx[i].fade <= 0.0F)) {
            if (trace_level() >= 2)
                khdays_log("kh2 enemies: %08x's effect %08x over (%s)", e.days, e.fx[i].handle,
                           e.fx[i].fx->done() ? "ended" : "faded out");
            e.fx.erase(e.fx.begin() + std::ptrdiff_t(i));
        }
}

// The field's camera for a draw: the latest flush's, unless that one does not turn -- Days's enemies are drawn after a
// flush that folds the camera into the projection and leaves a camera of no turn, only moved back (traced: the Mole
// Driller's every draw) -- then the frame's own, its list's first flush's. With the identity the effects' view was the world:
// their sprites facing the screen (pppRyjMegaBirth's, pppDrawMatrixFront's) stood fixed in the world, edge on (the Mole
// Driller's dust stood up)
bool field_camera(float camera[16])
{
    if (khdays_g3d_camera(camera) == 0)
        return false;
    bool identity = true; // its turn the identity (a screen layer's: moved back 100 or 1024, not turned)
    for (int r = 0; r < 3 && identity; ++r)
        for (int c = 0; c < 3 && identity; ++c)
            identity = std::fabs(camera[r * 4 + c] - (r == c ? 1.0F : 0.0F)) < 1e-4F;
    float list[16];
    if (identity && khdays_g3d_list_camera(list) != 0)
        std::copy(list, list + 16, camera);
    return true;
}
namespace {

DrawSpace draw_space()
{
    DrawSpace d;
    float vector[16];
    khdays_g3d_matrices(d.position, vector, d.projection);
    d.flush = khdays_g3d_base(d.base);
    d.has_camera = field_camera(d.camera);
    return d;
}
} // namespace

// How far apart two places are (the sum of their coordinates' differences)
float apart(const float a[3], const float b[3])
{
    return std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]);
}
namespace {

// An enemy class's own model: in its file Ms/<class>.p (Ms/04.p the Soldier's, Ms/00.p the Shadow's), not the enemies'
// shared effects (Ms/SharedEffect.p2f): its class, -1 for another model
int enemy_model(const void *resource)
{
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    if (resource == nullptr || !khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run) ||
        std::strncmp(file, "Ms/", 3) != 0)
        return -1;
    const char *n = file + 3;
    int kind = 0;
    while (*n >= '0' && *n <= '9')
        kind = kind * 10 + (*n++ - '0');
    // the archive's name, then where in it the model lies ("Ms/04.p@200")
    return n != file + 3 && std::strncmp(n, ".p", 2) == 0 && (n[2] == 0 || n[2] == '@') ? kind : -1;
}

// How far the draw stands from `place` in the world, and the way it is placed: an enemy's model in the world way, its
// own model's place a few centimetres from its actor's (the Shadow's 6); the other ways exactly
float off_by(const DrawSpace &d, const float place[3], Way &way)
{
    way = Way::World;
    float best = apart(d.position + 12, place);
    if (d.flush == 2 && near_identity(d.position) && apart(d.base + 12, place) < best) {
        way = Way::Folded;
        best = apart(d.base + 12, place);
    }
    if (d.has_camera) {
        float inverse[16], world[16];
        inverse_affine(d.camera, inverse);
        row_matrix_mul(d.position, inverse, world);
        if (apart(world + 12, place) < best) {
            way = Way::Camera;
            best = apart(world + 12, place);
        }
    }
    return best;
}

// For a model drawn instead at `mine` (its model to the world): the position matrix it takes (`place`), and its
// model to the view and the projection alone (for its effects, whose sprites face the view)
void spaces_for(const DrawSpace &d, Way way, const float mine[16], float place[16], float view[16], float pure[16])
{
    row_matrix_mul(mine, d.camera, view);
    float inverse[16];
    switch (way) {
    case Way::World:
        std::copy(mine, mine + 16, place);
        inverse_affine(d.camera, inverse);
        row_matrix_mul(inverse, d.projection, pure);
        break;
    case Way::Folded: {
        float base_camera[16];
        inverse_affine(d.base, inverse);
        row_matrix_mul(mine, inverse, place);
        row_matrix_mul(d.base, d.camera, base_camera);
        inverse_affine(base_camera, inverse);
        row_matrix_mul(inverse, d.projection, pure);
        break;
    }
    default:
        std::copy(view, view + 16, place);
        std::copy(d.projection, d.projection + 16, pure);
        break;
    }
}
} // namespace

void ghosts_close()
{
    float camera[16];
    if (ghosts.empty() || !field_camera(camera))
        return;
    const unsigned now = khdays_link_frame();
    const unsigned per_frame = unsigned(std::max(1L, std::lround(khdays_link_frame_seconds() * 60.0)));
    for (auto &g : ghosts) {
        Enemy &e = *g;
        const unsigned ticks = e.ghost_frame == 0     ? per_frame
                               : now >= e.ghost_frame ? (now - e.ghost_frame) * per_frame
                                                      : per_frame;
        e.ghost_frame = now;
        for (unsigned t = 0; t < ticks && !e.released; ++t) {
            Actor &a = *e.actor;
            a.begin_tick();
            a.tick();
            Vec3 at = to_kh2(e.place);
            const Vec3 travel = a.step(1.0F);
            for (int i = 0; i < 3; ++i)
                at[i] += travel[i];
            to_days(at, e.place);
            pose(e);
            ++e.end_ticks;
            if (((a.u32(field::kFade) & bit::kFadeHeld) != 0 && a.fade <= 0.0F) || e.end_ticks >= kLongestEnd)
                e.released = true;
        }
        float mine[16];
        model_matrix(*e.actor, e.place, mine);
        float view[16];
        row_matrix_mul(mine, camera, view);
        draw_effects(e, view, e.pure);
        const unsigned alpha = unsigned(std::lround(drawn_fade(e) * 31.0F));
        if (alpha == 0 || e.pose.empty())
            continue;
        std::vector<float> bones(e.pose.size() * 16);
        for (std::size_t b = 0; b < e.pose.size(); ++b)
            std::copy(e.pose[b].begin(), e.pose[b].end(), bones.begin() + std::ptrdiff_t(16 * b));
        khdays_models_draw_posed(e.kind->mdlx.c_str(), view, e.pure, (alpha << 16) | (1u << 24), bones.data(),
                                 e.pose.size(), e.actor->drawn_hidden());
    }
    for (auto it = ghosts.begin(); it != ghosts.end();) {
        if ((*it)->released && (*it)->fx.empty()) {
            if (tracing())
                khdays_log("kh2 enemies: %08x's end over", (*it)->days);
            by_actor.erase((*it)->actor.get());
            it = ghosts.erase(it);
        } else {
            ++it;
        }
    }
}
namespace {

// Whether a model is a player character's (ba/ch/<code>/, the file it came from): never a shell's, though a player an
// enemy holds stands at its place (1:18, the enemy carried by the player: Cerberus's bite)
bool character_model(const void *resource)
{
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    return resource != nullptr && khdays_origin_of_ram(unsigned(KHDAYS_REF(resource)), &file, &offset, &run) &&
           file != nullptr && std::strncmp(file, "ba/ch/", 6) == 0;
}
} // namespace

} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

extern "C" int khdays_kh2_enemies_draw_begin(const void *resource, const void *render_obj)
{
    if (render_obj == nullptr || enemies.empty())
        return 0;
    const unsigned obj = unsigned(KHDAYS_REF(render_obj));
    const DrawSpace d = draw_space();
    if (!d.has_camera)
        return 0;
    Enemy *e = nullptr;
    KhdaysKh2Shell s;
    Way way = Way::None;
    const int model_kind = enemy_model(resource);
    const bool own_model = model_kind >= 0;
    if (!own_model && character_model(resource))
        return 0;
    const float near_enough = own_model ? kNearEnough : 1e-3F;
    if (const auto it = shell_draws.find(obj); it != shell_draws.end()) {
        Enemy *x = find(it->second);
        if (x != nullptr && khdays_kh2_shell_read(it->second, &s) && (!own_model || s.kind == model_kind) &&
            off_by(d, s.place, way) < near_enough)
            e = x;
        else
            shell_draws.erase(it);
    }
    if (e == nullptr) { // the nearest shell the draw stands at
        float best = near_enough;
        unsigned found = 0;
        for (auto &[days, x] : enemies) {
            KhdaysKh2Shell at;
            Way w = Way::None;
            if (!khdays_kh2_shell_read(days, &at) || (own_model && at.kind != model_kind))
                continue;
            const float off = off_by(d, at.place, w);
            if (off < best && (own_model || w != Way::World))
                best = off, found = days, way = w, s = at;
        }
        if (found != 0) {
            e = find(found);
            shell_draws[obj] = found;
        }
        if (e != nullptr && tracing()) {
            const char *file = nullptr;
            u32 offset = 0, run = 0;
            char model[160] = "?";
            if (resource != nullptr && khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run))
                std::snprintf(model, sizeof(model), "%s@%x", file, offset);
            khdays_log("kh2 enemies: %08x's model %s (render object %08x, placed the %s way) drawn as its KH2 enemy",
                       found, model, obj,
                       way == Way::World    ? "world"
                       : way == Way::Folded ? "folded"
                                            : "camera");
        }
    }
    if (e == nullptr && tracing()) { // a model drawn near a shell but not at its place (the link's check), once each
        static std::set<std::pair<unsigned, unsigned>> told;
        float inverse[16], world[16];
        inverse_affine(d.camera, inverse);
        row_matrix_mul(d.position, inverse, world);
        for (auto &[days, x] : enemies) {
            KhdaysKh2Shell near;
            if (!khdays_kh2_shell_read(days, &near))
                continue;
            const auto off = [&](const float *at) {
                return std::fabs(at[0] - near.place[0]) + std::fabs(at[1] - near.place[1]) +
                       std::fabs(at[2] - near.place[2]);
            };
            const float by_world = off(d.position + 12), by_base = off(d.base + 12), by_camera = off(world + 12);
            if (std::min({by_world, by_base, by_camera}) < 0.5F && told.insert({obj, days}).second) {
                const char *file = nullptr;
                u32 offset = 0, run = 0;
                char model[160] = "?";
                if (resource != nullptr && khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run))
                    std::snprintf(model, sizeof(model), "%s@%x", file, offset);
                khdays_log("kh2 enemies: %08x at %.3f %.3f %.3f; %s (render object %08x) drawn %.3f off as the world "
                           "way, %.3f as the folded, %.3f as the camera's",
                           days, double(near.place[0]), double(near.place[1]), double(near.place[2]), model, obj,
                           double(by_world), double(by_base), double(by_camera));
            }
        }
    }
    if (e == nullptr)
        return 0;
    // its stand-in's other model in the same file (Ms/04.p's at + 0xca00), or another shell's of its class standing
    // with it: left out, its KH2 model and effects drawn once a frame
    const unsigned frame = khdays_link_frame();
    if (e->drawn_frame == frame)
        return 1;
    e->drawn_frame = frame;
    if (e->pose.empty())
        pose(*e);
    // its model to the world (row vectors: its axes turned by its yaw, at its place), in the draw's way
    float mine[16];
    model_matrix(*e->actor, s.place, mine);
    float place[16], view[16], pure[16];
    spaces_for(d, way, mine, place, view, pure);
    std::copy(pure, pure + 16, e->pure);
    e->seen = true;
    // KH2's reaction command mark, projected as its model (its axes, then the draw's view and projection, row
    // vectors): on this game's own player it holds, or at its aim point (its 0x307 shape) when its zone offers him one
    float at[3];
    bool marked = false;
    if (const auto h = held.find(local_player); h != held.end() && h->second.enemy == e->days) {
        held_middle(h->second, at);
        marked = true;
    } else if (zone_offerer() == e || (script_offered() >= 0 && script_offer.enemy == e->days)) {
        for (const Shape &sh : e->kind->shapes)
            if (sh.kind == 0x307) {
                days_point(*e, s, sh.bone, sh.at, at);
                marked = true;
                break;
            }
    }
    if (marked) {
        // (the point in its model's axes: the world offset turned back, times the model's turn's transpose)
        const float off[3] = {at[0] - s.place[0], at[1] - s.place[1], at[2] - s.place[2]};
        float q[4] = {0.0F, 0.0F, 0.0F, 1.0F};
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                q[j] += off[k] * mine[j * 4 + k];
        float v[4] = {}, spot[4] = {};
        for (int col = 0; col < 4; ++col)
            for (int k = 0; k < 4; ++k)
                v[col] += q[k] * view[k * 4 + col];
        for (int col = 0; col < 4; ++col)
            for (int k = 0; k < 4; ++k)
                spot[col] += v[k] * pure[k * 4 + col];
        khdays_ui_reaction_mark_draw(spot);
    }
    draw_effects(*e, view, pure);
    const unsigned alpha = unsigned(std::lround(drawn_fade(*e) * 31.0F));
    if (alpha == 0 || e->pose.empty())
        return 1; // gone: nothing of it, nor of its stand-in
    std::vector<float> bones(e->pose.size() * 16);
    for (std::size_t b = 0; b < e->pose.size(); ++b)
        std::copy(e->pose[b].begin(), e->pose[b].end(), bones.begin() + std::ptrdiff_t(16 * b));
    const int drawn = khdays_models_draw_posed(e->kind->mdlx.c_str(), place, nullptr, (alpha << 16) | (1u << 24),
                                               bones.data(), e->pose.size(), e->actor->drawn_hidden());
    if (!drawn && !e->told_draw) {
        e->told_draw = true;
        khdays_log("kh2 enemies: %s's model cannot be drawn (the message above says why)", e->kind->name.c_str());
    }
    return 1;
}

bool khdays::kh2_enemies::held_motion(unsigned node, HeldMotion &out)
{
    const auto p = player_of_node.find(node);
    if (p == player_of_node.end())
        return false;
    const auto h = held.find(p->second);
    if (h == held.end() || h->second.body == nullptr || !h->second.body->motion)
        return false;
    out.motion = h->second.body->motion.get();
    out.weapon = h->second.weapon != nullptr ? h->second.weapon->motion.get() : nullptr;
    out.ticks = h->second.time;
    out.started = h->second.started;
    out.share = h->second.blend > 0.0F ? 1.0F - h->second.blend_keep : 1.0F;
    out.skeleton = player_skeleton();
    return out.skeleton != nullptr;
}

void khdays::kh2_enemies::held_weapon_drawn(unsigned node, const float *bones, std::size_t count)
{
    const auto p = player_of_node.find(node);
    if (p == player_of_node.end() || held.count(p->second) == 0)
        return;
    DrawnWeapon &d = drawn_weapons[p->second];
    d.bones.resize(count);
    for (std::size_t b = 0; b < count; ++b)
        std::copy(bones + 16 * b, bones + 16 * (b + 1), d.bones[b].begin());
    d.frame = khdays_link_frame();
}
