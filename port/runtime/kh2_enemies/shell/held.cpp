// The players in hand: held in an enemy's motions, carried, given back.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

// The DS buttons this game's own player pressed this game frame (newly down) and holds, his own pad's or an input
// script's
struct FrameButtons final {
    std::uint32_t frame = ~0u;
    std::uint16_t held = 0, pressed = 0;
};
} // namespace

void let_go(unsigned object, const char *why)
{
    const auto h = held.find(object);
    if (h == held.end())
        return;
    for (auto &[days, e] : enemies) {
        if (e->carrier == object)
            e->carrier = 0;
        // his weapon's swing ends with the motion (his own has no range 12): its trail closes (loop_end)
        for (Enemy::Running &r : e->fx)
            if (r.on_weapon && r.player == object && r.handle == h->second.trail)
                r.fx->stop();
    }
    drawn_weapons.erase(object);
    if (const auto o = players.find(object); o != players.end() && o->second) {
        o->second->set_bits(field::kStatus, bit::kOtherMotion, false);
        o->second->set_bits(field::kStatus, bit::kKeepsCarried, false);
        // his controller's motion his own again (1:238, 1:15): the one asked for, else his idle -- Days plays the rest.
        // A thread waiting for it to change (Sephiroth's grab's, 770: the player falls when given back) goes on so
        const int own = h->second.own_queued ? h->second.own_motion : 0;
        o->second->set_u32(field::kMotion, std::uint32_t(own));
        o->second->set_u32(field::kMotionNow, std::uint32_t(own));
    }
    if (tracing())
        khdays_log("kh2 enemies: player %08x let go (%s)", object, why);
    held.erase(h);
}

// 1:29 (YS::trap_obj_motion_capture -> YS::MOTION::capture -> _capture): the motion of the enemy's set for the player,
// always from its first frame, `blend` its blend (YS::MOTION::start_motion(binarc, blend, 0); the PS3 build's): above
// 2 the motion stands while the blend counts from 2 up to it and his pose blends into its first frame (start_blend;
// add_time then plays its first frame's triggers, exec_trigger(start, -1)), else it starts with them (Rising Sun's 8:
// its AB50's twelve lights on his limbs at frame 8 were never reached when 8 was read as the frame to start from)
bool Field::player_motion(Object &o, Actor &by, int slot, float blend)
{
    const unsigned object = days_object(o);
    const auto it = by_actor.find(&by);
    if (object == 0 || it == by_actor.end() || slot < 0 || it->second->dying)
        return false; // (a defeated enemy lets him go: its thread on him takes him no more)
    const kh2::MotionSet &set = *it->second->kind->set;
    const kh2::MotionSet::Variant *body = set.variant(std::size_t(slot), "0010"); // KH2's player's body
    for (const auto &v : set.variants(std::size_t(slot)))
        if (body == nullptr && v.motion)
            body = &v;
    if (body == nullptr) {
        if (tracing())
            khdays_log("kh2 enemies: %08x's slot %d has no motion for a player", it->second->days, slot);
        return false;
    }
    const bool taken_now = held.count(object) == 0;
    Held &h = held[object];
    if (taken_now) {
        h.place = o.place();
        h.yaw = o.yaw();
        if (const auto w = waiting_threads.find(object); w != waiting_threads.end()) {
            h.machine = w->second.first;
            h.thread = w->second.second;
            waiting_threads.erase(w);
        }
    }
    h.enemy = it->second->days;
    h.slot = slot;
    h.body = body;
    h.own_queued = false; // a new motion: what was asked of the one before is not of this one
    h.steer = {};
    h.weapon = nullptr; // its weapon's ('0011' with data of its own: what it attacks with, its attributes)
    for (const auto &v : set.variants(std::size_t(slot)))
        if (h.weapon == nullptr && v.name == "0011" && v.own && (v.attributes || v.motion))
            h.weapon = &v;
    static unsigned motions_started = 0;
    h.started = ++motions_started;
    h.time = 0.0F;
    h.blend = blend > 2.0F ? blend : 0.0F;
    h.blend_count = h.blend > 0.0F ? 2.0F : 0.0F;
    h.blend_keep = h.blend > 0.0F ? 1.0F - 2.0F / h.blend : 0.0F;
    h.fresh = true;
    h.ended = false;
    h.offered = -1;
    h.taken.clear();
    h.struck.clear();
    h.cued = -1.0F; // its first frame's triggers too (exec_trigger(start, -1))
    o.set_u32(field::kMotion, std::uint32_t(slot));
    o.set_u32(field::kMotionNow, std::uint32_t(slot));
    o.set_bits(field::kStatus, bit::kMotionEnded, false);
    o.set_bits(field::kStatus, bit::kOtherMotion, true);
    if (tracing())
        khdays_log("kh2 enemies: %08x %s player %08x: %s's motion %d (%s, %s), blend %.1f, %.1f long", h.enemy,
                   taken_now ? "takes" : "keeps", object, it->second->kind->name.c_str(), slot,
                   set.name(std::size_t(slot)).c_str(), body->name.c_str(), double(blend),
                   double(body->motion->length_ticks()));
    return true;
}

// 1:11, 1:12, 1:61 on a player: his own motion started at once (start_motion, not the queue): he is given back now
void Field::player_own_motion_now(Object &o, int id)
{
    const unsigned object = days_object(o);
    o.set_u32(field::kMotion, std::uint32_t(id));
    o.set_u32(field::kMotionNow, std::uint32_t(id));
    if (held.count(object) != 0) {
        char why[48];
        std::snprintf(why, sizeof(why), "its own motion %d started", id);
        let_go(object, why);
    }
}

void Field::player_own_motion(Object &o, int id)
{
    const unsigned object = days_object(o);
    // KH2 queues it on the player's controller (1:13, FUN_1403c8310), taken once the motion of another's set he plays
    // gives him back: one still playing keeps him till it ends, or till a press of his acts out of it while its range
    // 2 (FLAG_CANCEL) is on (the Magnum Loader's thread asks for 0 as Quick Blade begins, the Armored Knight's as Rising
    // Sun's AB52 does, Roxas's as the Reversal's AR10 or AR22 does); one at its end (Cerberus's) lets him go now
    if (const auto h = held.find(object); h != held.end() && !h->second.ended) {
        h->second.own_queued = true;
        h->second.own_motion = id;
        if (tracing())
            khdays_log("kh2 enemies: player %08x's own motion %d queued till his motion ends or blends back to idle",
                       object, id);
        return;
    }
    o.set_u32(field::kMotion, std::uint32_t(id));
    o.set_u32(field::kMotionNow, std::uint32_t(id));
    if (held.count(object) != 0) {
        char why[48];
        std::snprintf(why, sizeof(why), "its own motion %d asked for", id);
        let_go(object, why);
    }
}
namespace {

// a thread found by its id in a thread's tree
ai::Thread *thread_of(ai::Thread *t, std::uint32_t id)
{
    if (t == nullptr)
        return nullptr;
    if (t->id == id)
        return t;
    for (auto &c : t->children)
        if (ai::Thread *found = thread_of(c.get(), id))
            return found;
    return nullptr;
}
} // namespace

void Field::player_thread(Object &o, ai::Machine &m, std::uint32_t id)
{
    const unsigned object = days_object(o);
    const auto h = held.find(object);
    if (h == held.end()) { // kept until the script takes the player in hand
        waiting_threads[object] = {&m, id};
        return;
    }
    if (h->second.machine == &m && h->second.thread != 0 && h->second.thread != id)
        if (ai::Thread *old = thread_of(m.main_thread(), h->second.thread))
            m.stop(old);
    h->second.machine = &m;
    h->second.thread = id;
}

void Field::carry(Actor &carried, Object &by, int bone, int flags)
{
    const auto it = by_actor.find(&carried);
    const unsigned object = days_object(by);
    if (it == by_actor.end())
        return;
    it->second->carry_bone = bone;
    it->second->carry_flags = flags;
    // a KH2 enemy or room object: it goes where that one is (at its bone), its floor there (Days's rooms hold no KH2
    // object's own collision)
    if (object == 0) {
        Actor *x = by.actor();
        const auto c = x != nullptr ? by_actor.find(x) : by_actor.end();
        if (c == by_actor.end()) {
            khdays_log("kh2 enemies: %08x is put on %s, which the port does not carry: it stays where it is",
                       it->second->days, by.name().c_str());
            return;
        }
        it->second->carrier = 0;
        it->second->carried_by = c->second->days;
        if (tracing())
            khdays_log("kh2 enemies: %08x carried by %08x, a %s (bone %#x, flags %#x)", it->second->days,
                       c->second->days, c->second->kind->name.c_str(), bone, flags);
        return;
    }
    // (a player before his enemy takes him in hand too: Scar is put on the player he pins, then plays its motion on
    // him; it is drawn on him while he is held)
    it->second->carrier = object;
    it->second->carried_by = 0;
    if (tracing())
        khdays_log("kh2 enemies: %08x carried by player %08x (bone %#x, flags %#x)", it->second->days, object, bone,
                   flags);
}

void Field::uncarry(Actor &carried)
{
    const auto it = by_actor.find(&carried);
    if (it == by_actor.end() || (it->second->carrier == 0 && it->second->carried_by == 0))
        return;
    if (tracing())
        khdays_log("kh2 enemies: %08x taken off %08x", it->second->days,
                   it->second->carrier != 0 ? it->second->carrier : it->second->carried_by);
    it->second->carrier = 0;
    it->second->carried_by = 0;
}

// It put where what carries it is (1:18): the Days player holding it, or the KH2 object carrying it at its bone
// (0x4000 its place), its floor that one's height; facing its way unless the carry's flag bit 1 keeps its own
void put_on_carrier(Enemy &e, Vec3 &place)
{
    Vec3 at{};
    float yaw = 0.0F;
    if (e.carrier != 0) {
        const auto h = held.find(e.carrier);
        if (h == held.end())
            return;
        at = h->second.place;
        yaw = h->second.yaw;
    } else if (e.carried_by != 0) {
        Enemy *c = enemy_or_ally(e.carried_by);
        if (c == nullptr || !c->actor)
            return;
        at = c->actor->place();
        if (e.carry_bone != 0x4000)
            if (Vec3 on{}; the_field().bone_point(*c->actor, e.carry_bone, {0.0F, 0.0F, 0.0F}, on))
                at = on;
        yaw = c->actor->yaw();
        e.floor = at[1];
        e.floor_known = true;
    } else {
        return;
    }
    place = at;
    e.actor->set_place(place);
    if ((e.carry_flags & 2) == 0)
        e.actor->set_yaw(yaw);
}
namespace {

const FrameButtons &frame_buttons()
{
    static FrameButtons b;
    if (const std::uint32_t now = khdays_input_game_frames(); now != b.frame) {
        b.frame = now;
        const KhdaysInputSnapshot *s = khdays_input_snapshot();
        const std::uint16_t buttons = std::uint16_t((s != nullptr ? s->ds_buttons : 0) | khdays_input_script_buttons());
        b.pressed = std::uint16_t(buttons & ~b.held);
        b.held = buttons;
    }
    return b;
}
} // namespace

std::uint16_t frame_pressed()
{
    return frame_buttons().pressed;
}

std::uint16_t frame_held()
{
    return frame_buttons().held;
}
namespace {

// Whether he pressed an action this game frame (A, B or Y: his attack, jump, guard)
bool action_pressed()
{
    return (frame_pressed() & (KHDAYS_KEY_A | KHDAYS_KEY_B | KHDAYS_KEY_Y)) != 0;
}
} // namespace

void hold_tick(Enemy &e, unsigned object, Held &h)
{
    const kh2::Motion &m = *h.body->motion;
    const float length = m.length_ticks();
    const bool loops = m.loop_from() < m.end() && m.loop_ticks() < length;
    const Vec3 was = h.place; // where this tick's moves start: the room's walls stop them (below)
    // 1:29's motion as YS::MOTION plays it (Playing, actor.h): at its first frame the tick it started; over a blend,
    // the count on a tick a tick (add_time while + 0x34 is set) with the motion standing, no travel and no triggers,
    // then on from its first frame by what the count went past, that frame's triggers played (exec_trigger(start, -1))
    bool still = false;
    bool round = false; // its loop went round this tick: the ended bit's tick (add_time's + 0x34 bit 1)
    float by = 1.0F;
    if (h.fresh) {
        h.fresh = false;
        still = true;
    } else if (h.blend > 0.0F) {
        h.blend_count += 1.0F;
        if (h.blend_count < h.blend) {
            h.blend_keep *= 1.0F - h.blend_count / h.blend;
            still = true;
        } else {
            by = h.blend_count - h.blend;
            h.blend = h.blend_count = h.blend_keep = 0.0F;
        }
    }
    const bool blending = h.blend > 0.0F;
    const float t0 = h.time;
    float t1 = t0 + by;
    const auto root = [&m](float at) {
        const kh2::Mat4 r = m.root_motion(at);
        return Vec3{r[12], r[13], r[14]};
    };
    Vec3 model{};
    if (!h.ended && !still) {
        const Vec3 a = root(t0), b = root(std::min(t1, length));
        for (int i = 0; i < 3; ++i)
            model[std::size_t(i)] = b[std::size_t(i)] - a[std::size_t(i)];
        if (t1 >= length) {
            if (loops) {
                const float loop = m.loop_ticks();
                const float over = std::fmod(t1 - length, length - loop);
                const Vec3 c = root(loop), d = root(loop + over);
                for (int i = 0; i < 3; ++i)
                    model[std::size_t(i)] += d[std::size_t(i)] - c[std::size_t(i)];
                t1 = loop + over;
                round = true;
            } else {
                t1 = length;
                h.ended = true;
                if (const auto o = players.find(object); o != players.end() && o->second)
                    o->second->set_bits(field::kStatus, bit::kMotionEnded, true);
            }
        }
        h.time = t1;
        const Vec3 along = turned({-model[0], 0.0F, model[2]}, h.yaw); // (as an actor's: Actor::update)
        h.place = {h.place[0] + along[0], h.place[1] - model[1], h.place[2] + along[2]};
    }
    // a loop's ended bit is the tick it goes round (Scar counts three rounds of the player pinned under him, 253,
    // before he throws him); one played to its end keeps it
    if (loops && !h.ended)
        if (const auto o = players.find(object); o != players.end() && o->second)
            o->second->set_bits(field::kStatus, bit::kMotionEnded, round);
    // in the air while his motion has a range 3, 24 or 30 (no pull: the air classes' movement adds none, FUN_1403cb100);
    // else KH2's ground class moves him: along the ground only (1:89's and 1:111's way flattened), on it (Days's room
    // collision under him) -- the Soldier's Cyclone swings him round it, its way down to the Soldier too
    bool air = false;
    if (const kh2::MotionAttributes *attr = h.body->attributes.get())
        for (const auto &r : attr->ranges)
            if ((r.id == 3 || r.id == 24 || r.id == 30) && h.time >= float(r.start) && h.time < float(r.end))
                air = true;
    const Vec3 asked = {h.move[0] + h.steer[0], h.move[1] + h.steer[1], h.move[2] + h.steer[2]};
    for (int i = 0; i < 3; ++i) // a script's move (1:89), this tick's, and its steering (1:111)
        if (air || i != 1)
            h.place[std::size_t(i)] += h.move[std::size_t(i)] + h.steer[std::size_t(i)];
    h.move = {};
    // the room's walls stop him as KH2's collision does: his body's sphere swept along his way from where he was, slid
    // along what it meets (the Soldier's Cyclone steps him 3.6 m aside; Days's collision's ray meets no wall)
    if (Vec3 way = {h.place[0] - was[0], 0.0F, h.place[2] - was[2]}; way[0] * way[0] + way[2] * way[2] > 0.0F) {
        if (wall_sweep(shell_of(e), was, kHeldRadius, way, true)) {
            h.place[0] = was[0] + way[0], h.place[2] = was[2] + way[2];
            if (trace_level() >= 2)
                khdays_log("kh2 enemies: held player %08x stopped by a wall: %.1f %.1f made (KH2's cm)", object,
                           double(way[0]), double(way[2]));
        }
    }
    if (!air) {
        // on the ground under him; a ledge (the ground more than a step below where he was) is not stepped off: KH2's
        // rooms wall their edges, Days's arenas leave some open (eh's platform), so he stays where he was
        float from[3], to[3], hit[3];
        to_days({h.place[0], h.place[1] - kGroundSearchUp, h.place[2]}, from);
        to_days({h.place[0], h.place[1] + kGroundSearchDown, h.place[2]}, to);
        if (khdays_kh2_shell_ray(shell_of(e), from, to, hit)) {
            const float ground = to_kh2(hit)[1];
            if (ground - was[1] > kStep) {
                if (trace_level() >= 2)
                    khdays_log("kh2 enemies: held player %08x kept off a ledge (ground %.1f below)", object,
                               double(ground - was[1]));
                h.place = was;
            } else {
                h.place[1] = ground;
            }
        } else {
            if (trace_level() >= 2)
                khdays_log("kh2 enemies: held player %08x finds no ground under %.1f %.1f %.1f", object,
                           double(h.place[0]), double(h.place[1]), double(h.place[2]));
            h.place = was; // no ground under it at all
        }
    }
    // put on one of KH2's actors (1:18): where it stands, turned as it is
    if (h.carried_by != 0)
        if (const Enemy *c = enemy_or_ally(h.carried_by); c != nullptr && c->actor) {
            h.place = c->actor->place();
            h.yaw = c->actor->yaw();
        }
    if (trace_level() >= 2)
        khdays_log("kh2 enemies: held player %08x at %.1f in %d%s: moved %.1f %.1f %.1f asked, from %.1f %.1f %.1f to "
                   "%.1f %.1f %.1f (KH2's cm)",
                   object, double(h.time), h.slot, air ? " (in the air)" : "", double(asked[0]), double(asked[1]),
                   double(asked[2]), double(was[0]), double(was[1]), double(was[2]), double(h.place[0]),
                   double(h.place[1]), double(h.place[2]));
    const auto in = [&h](const kh2::MotionAttributes::Range &r) {
        return h.time >= float(r.start) && h.time < float(r.end);
    };
    h.offered = -1;
    h.unhittable = false;
    h.idle = false;
    h.follows.clear();
    if (const kh2::MotionAttributes *attr = h.body->attributes.get()) {
        std::set<int> &chain = h.follows; // range 26: the commands this motion follows on from
        for (const auto &r : attr->ranges)
            if (r.id == 26 && !r.params.empty() && in(r))
                chain.insert(std::int16_t(r.params[0]));
        for (const auto &r : attr->ranges) {
            if (!in(r))
                continue;
            if (r.id == 27)
                h.unhittable = true;
            // range 2, FLAG_CANCEL (+ 0x34 bit 4: YS::MOTION's flags, the PS3 build's names): a press of his acts out
            // of it -- his own motion asked for, he is let go to Days's control on A, B or Y. It once let him go as it
            // began: the Reversal's AR22, 2 from its frame 0, ended in the tick it started
            if (r.id == 2 && h.own_queued && object == local_player && action_pressed())
                h.idle = true;
            if (r.id == 20 && !r.params.empty()) {
                const int id = std::int16_t(r.params[0]);
                const Command *c = command(id);
                if (h.taken.count(id) == 0 && (c == nullptr || c->require == 0 || chain.count(c->require) != 0))
                    h.offered = id;
            }
        }
        std::uint32_t passed = 0, ai_ranges = 0;
        for (const auto &tr : attr->triggers) {
            if (blending || float(tr.frame) <= h.cued || float(tr.frame) > h.time)
                continue;
            if (tr.id == 1 && !tr.params.empty())
                cast(e, std::int16_t(tr.params[0]), object);
            else if (tr.id >= 9 && tr.id <= 12)
                passed |= 1u << (tr.id - 9);
            else if (const int id = trigger_sound(tr); id > 0)
                sound(e, id);
        }
        bool keeps = false; // his range 13 (+ 0x18c bit 15): what is put on him with a carry's flag bit 0 stays
        for (const auto &r : attr->ranges) {
            if (r.id >= 16 && r.id <= 19 && in(r))
                ai_ranges |= 1u << (r.id - 16);
            if (r.id == 13 && in(r))
                keeps = true;
        }
        // his + 0x2ac and + 0x2ad as an enemy's (1:150, 1:151: the Magnum Loader's thread steps on his AB10's triggers
        // 9 and 10, frames 12 and 42)
        if (const auto o = players.find(object); o != players.end() && o->second) {
            o->second->set_u32(field::kMotionMarks,
                               (o->second->u32(field::kMotionMarks) & ~0xffffu) | ai_ranges | (passed << 8));
            o->second->set_bits(field::kStatus, bit::kKeepsCarried, keeps);
        }
    }
    if (h.weapon != nullptr && h.weapon->attributes)
        for (std::size_t r = 0; r < h.weapon->attributes->ranges.size(); ++r) {
            const auto &range = h.weapon->attributes->ranges[r];
            if (!attack_range(range) || !in(range) || !h.struck.insert(r).second)
                continue;
            // its weapon's attack on the enemy holding it (Cerberus's finish, atkp 598; the Magnum Loader's Quick Blade,
            // 657 and 656): Days's damage, the player's own last hit on it -- else on any KH2 enemy, else 1 -- times the
            // attack's power in KH2 (00battle.bin's atkp: 100 a plain hit's, 598's 300)
            const int atkp = std::int16_t(range.params[0]);
            const int power = attack_power(atkp);
            const int base = e.days_damage > 0 ? e.days_damage : last_days_damage > 0 ? last_days_damage : 1;
            e.blow += power > 0 ? std::max(1, base * power / 100) : 0;
            e.blow_blows += power > 0 ? double(power) / 100.0 : 0.0;
            blow_reaction(e, atkp);
            if (tracing())
                khdays_log(
                    "kh2 enemies: player %08x's weapon strikes %08x (atkp %d, power %d): %d damage (its last Days "
                    "hit's %d)",
                    object, e.days, atkp, power, e.blow, e.days_damage);
        }
    // his weapon's own triggers (8 its sounds: the keyblade's bank 500, Quick Blade's swish; 1 an effect of the enemy's
    // on the keyblade, as YS::MOTION::_Capture gives both parts the capturer's PAX) and its swing (range 12, + 0x20 bit
    // 11: its trail, YS::WEAPON::locus)
    bool swing = false;
    if (h.weapon != nullptr && h.weapon->attributes) {
        for (const auto &tr : h.weapon->attributes->triggers) {
            if (blending || float(tr.frame) <= h.cued || float(tr.frame) > h.time)
                continue;
            if (tr.id == 1 && !tr.params.empty())
                cast(e, std::int16_t(tr.params[0]), object, true);
            else if (const int id = trigger_sound(tr); id > 0)
                sound(e, id);
        }
        for (const auto &r : h.weapon->attributes->ranges)
            if (r.id == 12 && in(r))
                swing = true;
    }
    weapon_trail(e, object, h, swing && !blending);
    // his own motion asked for: taken at this one's end (Rising Sun's AB52), a loop's as it goes round --
    // YS::MOTION::add_time takes the queue's first at the end and goes round the loop only with none queued (a
    // Sephiroth's split flash that misses: its reaction command's AQ90, 252, a loop, gives the player back)
    if (h.own_queued && (h.ended || round))
        h.idle = true;
    if (!blending)
        h.cued = h.time;
    if (const auto o = players.find(object); o != players.end() && o->second) {
        o->second->set_place(h.place);
        o->second->set_yaw(h.yaw);
    }
}
} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

// A Days player's actor's tick begins (Ov022_ActorUpdate, prepare.py): whether a KH2 enemy holds it (its control
// stands still), put where the enemy's script and motion have it; this game's own player told apart
extern "C" int khdays_kh2_enemies_holding(void *actor)
{
    int local = 0;
    unsigned node = 0;
    const unsigned object = khdays_kh2_shell_player_object(unsigned(KHDAYS_REF(actor)), &local, &node);
    if (object == 0)
        return 0;
    if (local) {
        local_player = object;
        deck_sync();
    }
    if (node != 0)
        player_of_node[node] = object;
    player_actors[object] = unsigned(KHDAYS_REF(actor));
    // thrown back by a reflection of his blow (reflect_player)
    if (const auto r = recoils.find(object); r != recoils.end()) {
        khdays_kh2_shell_player_recoil(unsigned(KHDAYS_REF(actor)), r->second.data());
        recoils.erase(r);
    }
    // his model hidden or shown as a script asked (1:133, 1:134)
    if (const auto hp = hidden_players.find(object); hp != hidden_players.end() && hidden_told[object] != hp->second) {
        hidden_told[object] = hp->second;
        khdays_kh2_shell_player_hidden(unsigned(KHDAYS_REF(actor)), hp->second ? 1 : 0);
    }
    // its HP lost told to the KH2 mission (its damage trigger: KH2's player's damage drains Luxord's player's time
    // gauge), in KH2's measure as the mission's boss's health is measured against what Days shows of it -- Days has
    // no KH2 player's health to measure it by
    if (kh2_mission) {
        static std::map<unsigned, int> last_hp;
        static unsigned last_serial = 0;
        if (last_serial != kh2_mission_serial) { // (a mission's own: from its start)
            last_hp.clear();
            last_serial = kh2_mission_serial;
        }
        const int hp = khdays_kh2_shell_player_hp(unsigned(KHDAYS_REF(actor)));
        if (const auto was = last_hp.find(object); was != last_hp.end() && hp >= 0 && was->second > hp) {
            double per = 1.0;
            for (const auto &[id, g] : enemies)
                if (const Kind *k = g->kind != nullptr ? find_kind(mod().kinds, g->kind->name) : nullptr;
                    k != nullptr && k->mission == kh2_mission->name && g->health_most > 0 && g->shown_most > 0) {
                    per = double(g->health_most) / double(g->shown_most);
                    break;
                }
            auto &o = players[object];
            if (!o) {
                o = std::make_unique<Object>(mod().memory, "a player");
                o->set_bits(field::kFlags6c8, bit::kPlayer, true); // KH2's player (FUN_1403a7a40)
            }
            mission_damage(o->address(), std::max<std::int64_t>(1, std::llround(double(was->second - hp) * per)));
        }
        last_hp[object] = hp;
    }
    if (tracing()) { // its HP as it changes (the KH2 enemies' hits on it)
        static std::map<unsigned, int> seen;
        const int hp = khdays_kh2_shell_player_hp(unsigned(KHDAYS_REF(actor)));
        const auto was = seen.find(object);
        if (was == seen.end() || was->second != hp)
            khdays_log("kh2 enemies: player %08x's HP %d", object, hp);
        seen[object] = hp;
    }
    if (const auto c = captures.find(object); c != captures.end()) {
        // turned into a form (Capture): put where the form stands, his model hidden; back when its time is out and
        // it stands on the ground (Revert decided), or when the form is gone with its battle
        Capture &cap = c->second;
        cap.actor = unsigned(KHDAYS_REF(actor));
        const auto f = allies.find(cap.form);
        if (f == allies.end() || f->second->released) {
            end_capture(object, "its form gone with its battle");
            return 0;
        }
        const Enemy &form = *f->second;
        if (cap.time <= 0.0F && !(form.actor->jump.on || form.aloft)) {
            end_capture(object, "its time out");
            return 0;
        }
        // fallen (his HP out: the hits on the form are his): himself again where the form stood (he is held there
        // each frame), so that Days's fall and its game over show him, not a form left standing
        if (khdays_kh2_shell_player_hp(cap.actor) == 0) {
            end_capture(object, "he fell, his HP out");
            return 0;
        }
        if (!cap.hidden) {
            khdays_kh2_shell_player_hidden(cap.actor, 1);
            cap.hidden = true;
        }
        khdays_kh2_shell_put_player(cap.actor, form.place, days_turn(form.actor->yaw()));
        if (tracing() && khdays_link_frame() % 30u == 0u)
            khdays_log("kh2 enemies: player %08x as %s (action %#x): its time %.0f, way %s%.2f, at %.2f %.2f %.2f%s",
                       object, form.kind->name.c_str(), unsigned(cap.action), double(cap.time),
                       cap.heading ? "" : "none ", double(cap.heading.value_or(0.0F)), double(form.place[0]),
                       double(form.place[1]), double(form.place[2]), form.actor->jump.on ? ", in a jump" : "");
        return 0; // (Days's control goes on, locked: khdays_kh2_enemies_captured)
    }
    const auto h = held.find(object);
    if (h == held.end())
        return 0;
    float at[3];
    to_days(h->second.place, at);
    khdays_kh2_shell_put_player(unsigned(KHDAYS_REF(actor)), at, days_turn(h->second.yaw));
    return 1;
}
