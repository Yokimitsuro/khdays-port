// A player turned into a KH2 object (Luxord's die and card).
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {
constexpr int kCaptureForm = 12;          // YS::PLAYER + 0xb50's form 12: a capture
constexpr float kDriveBarTime = 600.0F;   // sstm + 0x68: a drive bar's time (ticks)
constexpr float kLandedBlowDrive = 15.0F; // sstm + 0x74
constexpr float kFormRunSpeed = 8.0F;     // prty 55 and 56 (the die's and the card's entries' + 0x4e): RunSpeed
} // namespace

// The capture whose form is the ally `form`, null none
Capture *capture_of_form(unsigned form)
{
    for (auto &[object, c] : captures)
        if (c.form == form)
            return &c;
    return nullptr;
}

// A turned player given back (CHANGEFORM::CaptureEnd, a script's 1:243, or his form's time out): his form's ally goes
// (released: allies_close lets it go), his Days model is drawn again where the form stood
void end_capture(unsigned object, const char *why)
{
    const auto c = captures.find(object);
    if (c == captures.end())
        return;
    if (const auto f = allies.find(c->second.form); f != allies.end())
        f->second->released = true;
    if (c->second.actor != 0 && c->second.hidden)
        khdays_kh2_shell_player_hidden(c->second.actor, 0);
    if (tracing())
        khdays_log("kh2 enemies: player %08x turned back (%s)", object, why);
    captures.erase(c);
}

void Field::capture_form(Actor &by, int entry)
{
    Object *p = player(by);
    const auto maker = by_actor.find(&by);
    const unsigned object = p != nullptr ? days_object(*p) : 0u;
    if (object == 0 || maker == by_actor.end())
        return;
    end_capture(object, "turned into another form"); // (a CHANGEFORM takes the form there was)
    const std::string name = entry_name(std::uint32_t(entry));
    KindFiles *k = name.empty() ? nullptr : files_of(name, true);
    if (k == nullptr) {
        khdays_log("kh2 enemies: the player's form %s (object entry %#x) is not in the mod: he stays himself",
                   name.empty() ? "?" : name.c_str(), unsigned(entry));
        return;
    }
    const Enemy &m = *maker->second;
    Capture c;
    c.anchor = m.anchor != 0 ? m.anchor : m.days;
    c.entry = entry;
    c.time = 2.0F * kDriveBarTime;
    Enemy *f = start_ally(*k, p->place(), p->yaw(), c.anchor);
    c.form = f->days;
    f->actor->play(0, 0.0F, 0.0F); // its idle (motion 0, as KH2's player's)
    captures[object] = c;
    if (tracing())
        khdays_log("kh2 enemies: player %08x turned into %s (%08x) by %08x's script", object, name.c_str(), c.form,
                   m.days);
    // out of the hands that held him: CHANGEFORM makes KH2's player anew, the grip on the one before gone with it --
    // the card game's hold (Luxord's motions 252-254) was left on him past the card, his own self held there after it
    // (the die's script lets him go itself, 1:11; the card's does not)
    if (held.count(object) != 0)
        let_go(object, "turned into a form");
}

void Field::capture_end(Actor &by)
{
    if (Object *p = player(by))
        end_capture(days_object(*p), "a script turned him back");
}

int Field::player_form(Actor &by)
{
    Object *p = player(by);
    return p != nullptr && captures.count(days_object(*p)) != 0 ? kCaptureForm : 0;
}

void Field::form_action(Actor &by, int action)
{
    Object *p = player(by);
    const auto c = p != nullptr ? captures.find(days_object(*p)) : captures.end();
    if (c == captures.end())
        return;
    c->second.action = action;
    c->second.inertia = {};                                            // (Ts::ACTION_CARD::start: its inertia made 0)
    if (const auto f = allies.find(c->second.form); f != allies.end()) // the card floats (MainteHeightFromFloor)
        f->second->actor->set_bits(field::kStatus, kNoGravity, action == 0x2e);
}

// A turned player's form moved on a tick (KH2's actions as the PS3 build has them): his pad's way, his jump and his
// command. The card (Ts::ACTION_CARD): turned toward the way held by its turn a tick, its velocity eased toward the
// way at its run speed by its kept share (YS::OBJ::calc_inertia: lerp; none held, times 1 less its loss), swept
// against the room's walls, kept its height over the floor at its climb a tick (Ts::ACTION_LIB::MainteHeightFromFloor);
// its command its spin (motion 201, whose range 10 strikes with atkp 1310). The die (Ts::ACTION_DICE, an idle): turned
// toward the way, moved along it only by its share (0 for Luxord's: it moves by jumping alone); its jump
// (Ts::ACTION_DICE::jump -> ACTION_DICEJUMP, KH2's jump at its height) along the way at the form's run speed, its
// command the same jump that far again times its party jump's number (ACTION_DICE::party_jump: Dice Attack); its jump
// and fall motions (3, 4) strike with atkp 1309. KH2's prepare_jump arc is not read: a plain throw to the height, its
// way kept in the air. Its time down a tick; out on the ground, he turns back
void capture_tick(Capture &c, Enemy &e, Vec3 &place)
{
    Actor &a = *e.actor;
    const bool aloft = a.jump.on || e.aloft;
    c.time -= 1.0F;
    const auto turn_to = [&](float most) {
        if (!c.heading)
            return;
        float d = *c.heading - a.yaw();
        while (d > kPi)
            d -= 2.0F * kPi;
        while (d < -kPi)
            d += 2.0F * kPi;
        a.set_yaw(a.yaw() + std::clamp(d, -most, most));
    };
    const Vec3 way = c.heading ? Vec3{std::sin(*c.heading), 0.0F, std::cos(*c.heading)} : Vec3{};
    if (c.action == 0x2e) {
        turn_to(card_spec[2]);
        const float speed = c.heading ? card_spec[1] : 0.0F;
        for (int i = 0; i < 3; i += 2)
            c.inertia[std::size_t(i)] =
                speed > 0.0F ? c.inertia[std::size_t(i)] +
                                   (way[std::size_t(i)] * speed - c.inertia[std::size_t(i)]) * card_spec[3]
                             : c.inertia[std::size_t(i)] * (1.0F - card_spec[4]);
        Vec3 step = c.inertia;
        wall_sweep(c.anchor, place, kWallRadius, step, true);
        place[0] += step[0];
        place[2] += step[2];
        if (e.floor_known) {
            const float off = (e.floor - place[1]) - card_spec[5]; // over its height (KH2's y points down)
            if (std::abs(off) > card_spec[6])
                place[1] += off >= 0.0F ? card_spec[6] : -card_spec[6];
        }
        if (c.command && a.motion.id != 201)
            a.play(201, 2.0F, 0.0F);
        else if (a.motion.id == 201 && a.motion.ended)
            a.play(0, 2.0F, 0.0F);
    } else if (c.action == 0x2b) {
        if (!aloft) {
            turn_to(dice_spec[1]);
            Vec3 step = {way[0] * kFormRunSpeed * dice_spec[0], 0.0F, way[2] * kFormRunSpeed * dice_spec[0]};
            if (std::hypot(step[0], step[2]) > 0.0F) {
                wall_sweep(c.anchor, place, kWallRadius, step, true);
                place[0] += step[0];
                place[2] += step[2];
            }
            if (c.jump || c.command) {
                const float far = kFormRunSpeed * (c.command ? dice_spec[3] : 1.0F);
                if (tracing())
                    khdays_log("kh2 enemies: the die %08x jumps (%s), %.0f cm a tick along", e.days,
                               c.command ? "its command: party jump" : "the jump button", double(far));
                a.jump = {};
                a.jump.on = true;
                a.jump.up = -std::sqrt(2.0F * kKh2Pull * dice_spec[2]);
                a.jump.along = {way[0] * far, 0.0F, way[2] * far};
                // its way kept through the air: the travel along the ground is its velocity (Actor::step), never
                // slowed (its air evaluation keeps `keep` of it steered, `slow` of it not -- all of it here)
                a.jump.keep = a.jump.slow = 1.0F;
                a.velocity = a.steered = a.jump.along;
                a.move_clock = 0.0F;
                a.set_bits(field::kFlags, bit::kInAir, true);
                a.play(3, 2.0F, 0.0F);
                a.motion.after = 4;
                a.motion.landing = 5;
            } else if (a.motion.id != 0 && a.motion.ended)
                a.play(0, 2.0F, 0.0F);
        } else if (a.jump.on) { // its way in the air swept against the walls: stopped at one
            Vec3 along = a.velocity;
            if (wall_sweep(c.anchor, place, kWallRadius, along, false))
                a.jump.along = a.velocity = a.steered = {};
        }
    }
    c.jump = c.command = false; // (each press taken once)
}

// A turned player's form's attack ranges this tick (10 and 33, as attack reads an enemy's): his blows on the KH2
// enemies they reach (strike_enemies) -- the form is KH2's player, so its attacks of team 0, its own (atkp 1309, 1310:
// power 25, the wiki's x0.25), are his -- and each new blow's AddDrive x sstm + 0x74 off its time
// (PLAYER::_OVR_attack_callback, his drive kind 2)
void capture_blows(Capture &c, Enemy &e, const KhdaysKh2Shell &s)
{
    const Actor &a = *e.actor;
    if (a.motion.id != e.attack_motion || a.motion.time < e.attack_time) {
        e.struck.clear(); // another motion, or this one over again
        c.struck.clear();
    }
    e.attack_motion = a.motion.id;
    e.attack_time = a.motion.time;
    const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.motions->attributes(a.motion.id) : nullptr;
    if (attr == nullptr || e.pose.empty())
        return;
    for (std::size_t r = 0; r < attr->ranges.size(); ++r) {
        const auto &range = attr->ranges[r];
        if (!attack_range_on(range, a.motion.time))
            continue;
        const int atkp = std::int16_t(range.params[0]);
        strike_enemies(e, s, r, std::int16_t(range.params[1]), atkp);
        const std::size_t hit = e.struck[r].size();
        std::size_t &told = c.struck[unsigned(r)];
        if (hit > told)
            c.time -= float(hit - told) * float(attack_param(atkp).add_drive) * kLandedBlowDrive;
        told = hit;
    }
}

// A turned player's form kept out of the KH2 enemies' bodies: the player's own body (kWallRadius, the one its walls
// keep off) never inside an enemy's Days collision sphere, pushed out along the ground the shortest way, as Days's
// collision keeps its players off its enemies (the form stands where the player is, the player held there: Days
// moves neither). The card game puts the player in front of Luxord, his die made there; without it Luxord's body and
// the die were drawn one in the other
void keep_off_enemies(const Enemy &form, Vec3 &place)
{
    for (const auto &[id, g] : enemies) {
        const Enemy &e = *g;
        if (e.dying || e.released || e.down || !e.actor || e.radius <= 0.0F)
            continue;
        const Vec3 at = e.actor->place();
        const float apart_most = e.radius * kKh2PerDays + kWallRadius;
        const float dx = place[0] - at[0], dz = place[2] - at[2], d = std::hypot(dx, dz);
        if (d >= apart_most)
            continue;
        // the way out: from its middle, or (standing on it) back along the form's facing
        const float ux = d > 0.01F ? dx / d : -std::sin(form.actor->yaw());
        const float uz = d > 0.01F ? dz / d : -std::cos(form.actor->yaw());
        Vec3 out = {ux * (apart_most - d), 0.0F, uz * (apart_most - d)};
        wall_sweep(form.anchor, place, kWallRadius, out, true);
        place[0] += out[0];
        place[2] += out[2];
    }
}
} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

// Ov022_TranslateCommandToAction (prepare.py), before its lock: whether this player is turned into a form (Capture),
// locked then as Days's own lock locks a player -- no Days command, no Days way; his pad this frame first taken for his
// form (its buttons pressed, + 0x18: 1 the command, 0x800 the jump; the way his D-pad holds, from the camera's turn)
extern "C" int khdays_kh2_enemies_captured(void *actor)
{
    const unsigned at = unsigned(KHDAYS_REF(actor));
    const unsigned object = khdays_kh2_shell_player_object(at, nullptr, nullptr);
    unsigned pressed = 0;
    int heading = -1;
    const bool read = object != 0 && khdays_kh2_shell_player_pad(at, &pressed, &heading);
    // Days's heading a moves him along (-sin a, -cos a), KH2's (-sin a, cos a) (days_way): KH2's yaw (sin y, cos y) of
    // the same way is -a
    const std::optional<float> way =
        heading >= 0 ? std::optional<float>(-float(heading) / 65536.0F * 2.0F * kPi) : std::nullopt;
    if (read)
        pad_ways[object] = way; // (1:113's, World::player_movement)
    const auto c = object != 0 ? captures.find(object) : captures.end();
    if (c == captures.end())
        return 0;
    if (read) {
        c->second.heading = way;
        c->second.jump |= (pressed & 0x800u) != 0;
        c->second.command |= (pressed & 1u) != 0;
    }
    return 1;
}

// func_ov022_0209c9fc (prepare.py), before it draws the actor's two weapon slots: whether this player is turned into a
// form, his model hidden -- his weapons too then (his node's undrawn flag keeps his body from being drawn, not the
// slots, which follow his hand: his keyblade was left in the air beside the die)
extern "C" int khdays_kh2_enemies_turned(void *actor)
{
    const unsigned object = khdays_kh2_shell_player_object(unsigned(KHDAYS_REF(actor)), nullptr, nullptr);
    const auto c = object != 0 ? captures.find(object) : captures.end();
    return c != captures.end() && c->second.hidden ? 1 : 0;
}
