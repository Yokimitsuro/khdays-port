// Attacks: an enemy's own, what they strike, those reflected.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {
constexpr int kAttackType = 0, kGuardType = 2;
} // namespace

// An enemy's attacks of their own let go (its memory too): it is gone, or its field is
void drop_attacks(unsigned days)
{
    for (auto a = attacks.begin(); a != attacks.end();)
        if (days == 0 || a->second.enemy == days) {
            mod().memory.remove(a->first);
            a = attacks.erase(a);
        } else {
            ++a;
        }
}

// KH2's trigger 15 (FUN_1403d1730 -> the attacker's class virtual + 0xe0, FUN_1403d5500): an attack of the enemy's
// landed on a player -- the one hit and the attack (its + 0x18 its atkp entry, 2:25), the attack read first (fp + 0).
// Cerberus's bite (atkp 359) holding the player begins its reaction command. On a player invincible then (`star`: his
// held motion's range 27, YS::MOTION's FLAG_STAR) it lands all the same, with no damage nor reaction: Sephiroth's
// trigger 15 asks 1:225 of the player his attacks land on, and his split flash landing on the player in his reaction
// command 664 (AQ90, range 27 throughout) is what enters his rc_success
void landed(Enemy &e, unsigned object, int atkp, bool star)
{
    Actor &a = *e.actor;
    the_field().player(a); // the players' objects as they stand now
    const auto it = players.find(object);
    if (it == players.end() || !it->second)
        return;
    e.took = object;
    const ai::Value values[2] = {ai::Value::of_address(it->second->address()),
                                 ai::Value::of_address(motion_attack(e, atkp))};
    a.event(15, values);
    // held in another's motion (1:29: YS::MOTION::capture, a motion of his own started), struck by an attack with a
    // reaction: his damage reaction's motion takes its place, as KH2's MOTION::change does, and the holder's script
    // sees it (1:238) -- Vexen's freeze ends with its shatter (2:17, atkp 1816)
    if (!star && held.count(object) != 0 && attack_reacts(atkp))
        let_go(object, "struck: his damage reaction");
}

// Its motion's attack as KH2's scripts know it (trigger 15's, 19's): one record of the enemy's, + 0x18 its atkp entry
// (2:25: its number, not its level's record -- attack_key), + 0x94 its owner, + 4 its flags (0x40 its reflection's
// motion played: 2:26); made at its first use
std::uint32_t motion_attack(Enemy &e, int atkp)
{
    if (e.attack_record == 0)
        e.attack_record = mod().memory.add(0x100, e.actor->kind() + "'s attack", true);
    mod().memory.set_u32(e.attack_record + 0x18, std::uint32_t(atkp & 0xffff));
    mod().memory.set_u32(e.attack_record + 0x94, e.actor->address());
    return e.attack_record;
}

// The current motion's attacks (its ranges 10 and 33: an atkp entry and a collision group) that cover its time: each
// of the group's attack spheres hits the players it reaches -- each one a body from his feet to KH2's player's
// height, his Days radius round -- each player once a range (KH2's hit's own "once")
void attack(Enemy &e, const KhdaysKh2Shell &s)
{
    const Actor &a = *e.actor;
    if (a.motion.id != e.attack_motion || a.motion.time < e.attack_time) {
        e.struck.clear(); // another motion, or this one over again
        e.windows_told.clear();
        if (e.attack_record != 0) // its attacks anew (+ 4: 0x40 its reflection's motion played)
            mod().memory.set_u32(e.attack_record + 4, 0);
    }
    e.attack_motion = a.motion.id;
    e.attack_time = a.motion.time;
    const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.motions->attributes(a.motion.id) : nullptr;
    if (attr == nullptr || e.pose.empty() || e.dying)
        return;
    std::vector<KhdaysKh2Player> list(std::size_t(std::max(0, khdays_kh2_shell_players(shell_of(e), nullptr, 0))));
    const int n = khdays_kh2_shell_players(shell_of(e), list.data(), int(list.size()));
    for (std::size_t r = 0; r < attr->ranges.size(); ++r) {
        const auto &range = attr->ranges[r];
        if (!attack_range_on(range, a.motion.time))
            continue;
        const int group = std::int16_t(range.params[1]);
        // an attack of the player's team (its atkp's team 1: YS::ATTACK's constructor makes the player its owner, and
        // it strikes every team but his): the enemies it reaches, as his blow -- Demyx's water clone swung in Wild
        // Dance (atkp 1326 to 1328)
        if (const int atkp = std::int16_t(range.params[0]); attack_team(atkp) == 1) {
            strike_enemies(e, s, r, group, atkp);
            continue;
        }
        float nearest = -1.0F, reach = 0.0F, centre[3] = {}, toward = 0.0F;
        int spheres = 0;
        for (const Shape &sh : e.kind->shapes) {
            if (sh.group != group || (sh.kind & 0xff) != 6)
                continue;
            float c[3];
            ++spheres;
            days_point(e, s, sh.bone, sh.at, c);
            const float radius = sh.radius / kKh2PerDays;
            for (int i = 0; i < n && i < int(list.size()); ++i) {
                const KhdaysKh2Player &p = list[std::size_t(i)];
                // the nearest point of his body's middle line (from his feet, a radius up, to his top, a radius down)
                const float low = p.place[1] + p.radius, high = std::max(low, p.place[1] + kPlayerHeight - p.radius);
                const float d[3] = {p.place[0] - c[0], std::clamp(c[1], low, high) - c[1], p.place[2] - c[2]};
                const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (nearest < 0.0F || dist - radius - p.radius < nearest - reach) {
                    nearest = dist;
                    reach = radius + p.radius;
                    std::copy(c, c + 3, centre);
                    const Vec3 f = days_facing(a.yaw()); // (Days's places)
                    const float tx = p.place[0] - s.place[0], tz = p.place[2] - s.place[2];
                    const float tl = std::sqrt(tx * tx + tz * tz);
                    toward = tl > 0.0F ? (tx * f[0] + tz * f[2]) / tl : 1.0F;
                }
                if (dist >= radius + p.radius || e.struck[r].count(p.object) != 0)
                    continue;
                e.struck[r].insert(p.object);
                const int key = attack_key(a, std::int16_t(range.params[0])); // its owner's level's (1:358)
                if (const auto h = held.find(p.object); h != held.end() && h->second.unhittable) {
                    // invincible (his motion's range 27): landed with no damage (landed's star)
                    if (tracing())
                        khdays_log("kh2 enemies: %08x's motion %d at %.1f (atkp %d) lands on %08x, invincible", e.days,
                                   a.motion.id, double(a.motion.time), key & 0xffff, p.object);
                    landed(e, p.object, key, true);
                    if (a.motion.id != e.attack_motion)
                        return;
                    continue;
                }
                float push[3]; // from the sphere to him, as far as the attack throws (hitback_push)
                hitback_push(key, d[0], d[2], push);
                const int took = hit_player(e, p.object, key, push);
                if (tracing())
                    khdays_log("kh2 enemies: %08x's motion %d at %.1f (atkp %d, group %d) hits %08x%s", e.days,
                               a.motion.id, double(a.motion.time), int(std::int16_t(range.params[0])), group, p.object,
                               took ? "" : " (not taken)");
                if (took)
                    landed(e, p.object, key);
                if (a.motion.id != e.attack_motion)
                    return; // its attack reflected by his block, its own motion played (hit_player)
            }
            // the party members it reaches (the player's team's: KH2's attack strikes every team but its owner's)
            each_member([&](Enemy &m, Friend &f) {
                if (e.struck[r].count(m.days) != 0 || !member_reached(m, to_kh2(c), sh.radius, 0.0F, kTypeHit))
                    return;
                e.struck[r].insert(m.days);
                member_hurt(e, m, f, attack_key(a, std::int16_t(range.params[0])));
            });
        }
        if (tracing() && e.windows_told.count(r) == 0) {
            // every attack sphere's place along its facing then (forward, up), what each group reaches
            const Vec3 f = days_facing(a.yaw()); // (Days's places)
            std::string where;
            for (const Shape &sh : e.kind->shapes) {
                if ((sh.kind & 0xff) != 6)
                    continue;
                float c[3];
                days_point(e, s, sh.bone, sh.at, c);
                char one[64];
                std::snprintf(one, sizeof(one), " [group %d: %.2f ahead, %.2f up]", sh.group,
                              double((c[0] - s.place[0]) * f[0] + (c[2] - s.place[2]) * f[2]),
                              double(c[1] - s.place[1]));
                where += one;
            }
            khdays_log("kh2 enemies: %08x's attack spheres:%s", e.days, where.c_str());
        }
        if (trace_level() >= 2 && e.windows_told.count(r) != 0 && nearest >= 0.0F)
            khdays_log("kh2 enemies: %08x's motion %d at %.1f still attacks (group %d): nearest player %.2f away, "
                       "reached within %.2f, the sphere %.2f up",
                       e.days, a.motion.id, double(a.motion.time), group, double(nearest), double(reach),
                       double(centre[1] - s.place[1]));
        if (tracing() && e.windows_told.insert(r).second)
            khdays_log("kh2 enemies: %08x's motion %d attacks at %.1f (atkp %d, group %d): %d spheres, nearest player "
                       "%.2f away, reached within %.2f; the sphere at %.2f %.2f %.2f, the body at %.2f %.2f %.2f "
                       "facing the player by %.2f",
                       e.days, a.motion.id, double(a.motion.time), int(std::int16_t(range.params[0])), group, spheres,
                       double(nearest), double(reach), double(centre[0]), double(centre[1]), double(centre[2]),
                       double(s.place[0]), double(s.place[1]), double(s.place[2]), double(toward));
    }
}

// A hit's reaction on `a` as its damage patterns on change its first one (patterns.h): those on are BTLOBJ's
// Pattern, (Default | Enable) & ~Disable (YS::PIERROT::_OVR_exec_damage): + 0xd10 Default (1:211 sets a bit, 1:212
// and 1:324 clear it), + 0xd14 Enable and + 0xd18 Disable (its motion's ranges 36 and 37 while they last,
// motion_states); in the air as 1:60 tells it
int hit_pattern_reaction(const Actor &a, int first, bool not_player)
{
    const bool air = (a.u32(field::kStatus) & 0x1000020u) != 0 || (a.u32(field::kFlags) & bit::kInAir) != 0 ||
                     (a.u32(field::kStatus) & 0x40u) != 0;
    const std::uint32_t on = (a.u32(0xd10) | a.u32(0xd14)) & ~a.u32(0xd18);
    return pattern_reaction(damage_patterns(), on, air, first, not_player);
}

// A KH2 player's blow with attack `atkp` on `t`: its reaction kept with the blow's, the strongest
void blow_reaction(Enemy &t, int atkp)
{
    const int r = attack_param(atkp).reaction;
    t.blow_reaction = std::max(t.blow_reaction, r == 255 ? 0 : r);
    t.blow_karma += attack_param(atkp).revenge;
}

// A motion's attack of the player's team (attack, above): each of its group's spheres strikes the other KH2 enemies
// it reaches, each once a range -- a body as a player's (its Days sphere's radius round, from its feet to KH2's player's
// height) -- a blow of the player's on each: Days's damage, its last Days hit's (else any KH2 enemy's, else 1) times
// the attack's power over 100, as his weapon's (held_tick)
void strike_enemies(Enemy &e, const KhdaysKh2Shell &s, std::size_t r, int group, int atkp)
{
    const int power = attack_power(atkp);
    for (const Shape &sh : e.kind->shapes) {
        if (sh.group != group || (sh.kind & 0xff) != 6)
            continue;
        float c[3];
        days_point(e, s, sh.bone, sh.at, c);
        const float radius = sh.radius / kKh2PerDays;
        for (auto &[days, other] : enemies) {
            Enemy &t = *other;
            if (&t == &e || t.released || t.dying || !t.started || e.struck[r].count(days) != 0)
                continue;
            KhdaysKh2Shell ts;
            if (!khdays_kh2_shell_read(days, &ts) || !ts.standing)
                continue;
            const float low = ts.place[1] + ts.radius, high = std::max(low, ts.place[1] + kPlayerHeight - ts.radius);
            const float d[3] = {ts.place[0] - c[0], std::clamp(c[1], low, high) - c[1], ts.place[2] - c[2]};
            if (std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) >= radius + ts.radius)
                continue;
            e.struck[r].insert(days);
            const int base = t.days_damage > 0 ? t.days_damage : last_days_damage > 0 ? last_days_damage : 1;
            t.blow += power > 0 ? std::max(1, base * power / 100) : 0;
            t.blow_blows += power > 0 ? double(power) / 100.0 : 0.0;
            blow_reaction(t, atkp);
            if (tracing())
                khdays_log("kh2 enemies: %08x's motion %d at %.1f (atkp %d, the player's team, power %d) strikes %08x: "
                           "%d damage",
                           e.days, e.actor->motion.id, double(e.actor->motion.time), atkp, power, days, t.blow);
        }
    }
}
namespace {

// How far a KH2 attack throws back what it strikes, in Days's units (KH2's YS::DAMAGE::get_hitback, as 2:86 reads
// it): the attack's own hitback (atkp + 0xc, KH2's cm) or, at 0x7fff, KH2's battle constant for its reaction
// (03system.bin's sstm + 0x34 for a small one, else + 0x30); none for an attack that gives no reaction
float hitback_of(int atkp)
{
    const AttackParam &p = attack_param(atkp);
    if (p.reaction == 0 || p.reaction == 255)
        return 0.0F;
    float cm = float(p.hitback);
    if (p.hitback == 0x7fff) {
        const int r = p.reaction;
        const bool small = r == 3 || r == 6 || r == 8 || r == 10 || r == 13 || r == 16 || r == 18;
        const std::vector<std::uint8_t> &values = system_constants();
        const std::size_t offset = small ? 0x34u : 0x30u;
        cm = 200.0F;
        if (offset + 4 <= values.size())
            std::memcpy(&cm, values.data() + offset, 4);
    }
    return cm > 0.0F ? cm / kKh2PerDays : 0.0F;
}
} // namespace

// The push a KH2 hit gives a Days player (Ov107_InvokeHitCallback's), along the ground way (dx, dz) from what struck to
// him: Days's recoil moves him twice the push it is given (Ov022_IntegrateRecoilVec adds it and halves it each
// frame), so half the attack's hitback. Days's own enemies give 0.25 to 0.375
void hitback_push(int atkp, float dx, float dz, float push[3])
{
    push[0] = push[1] = push[2] = 0.0F;
    const float flat = std::sqrt(dx * dx + dz * dz), half = hitback_of(atkp) * 0.5F;
    if (!(flat > 0.0F) || !(half > 0.0F))
        return;
    push[0] = dx / flat * half;
    push[2] = dz / flat * half;
}

// A hit made with an attack on an object at once (2:17): a Days player its enemy's hit (KH2's own attack landing on
// him: trigger 15 too), a KH2 enemy KH2's player's blow when he owns it (the Neoshadow's strike: Days's damage the
// player's last hit times the attack's power)
void strike(AttackObject &o, Object &target)
{
    const auto owner = enemies.find(o.enemy);
    if (owner == enemies.end())
        return;
    Enemy &e = *owner->second;
    if (const unsigned player = days_object(target); player != 0) {
        if (o.players || !o.struck.insert(player).second)
            return;
        float push[3], at[3], to[3]; // from the enemy to him: its attack of its own's way
        to_days(e.actor->place(), at);
        to_days(target.place(), to);
        hitback_push(o.atkp, to[0] - at[0], to[2] - at[2], push);
        o.landed = true; // KH2's hit lands whether Days's player takes it or not (his guard, his dodge)
        o.hit = target.address();
        if (hit_player(e, player, o.atkp, push, &o))
            landed(e, player, o.atkp);
        if (o.on_hit != 0) { // its script's routine on the hit (2:38), its value given
            const ai::Value v = ai::Value::of_address(o.on_hit_value);
            e.actor->machine().start_routine(e.actor->machine().main_thread(), o.on_hit, {&v, 1});
        }
        return;
    }
    Actor *x = target.actor();
    const auto it = x != nullptr ? by_actor.find(x) : by_actor.end();
    if (!o.players || it == by_actor.end())
        return;
    Enemy &hit = *it->second;
    o.landed = true;
    o.hit = target.address();
    const int power = attack_power(o.atkp);
    const int base = hit.days_damage > 0 ? hit.days_damage : last_days_damage > 0 ? last_days_damage : 1;
    hit.blow += power > 0 ? std::max(1, base * power / 100) : 0;
    hit.blow_blows += power > 0 ? double(power) / 100.0 : 0.0;
    blow_reaction(hit, o.atkp);
    if (tracing())
        khdays_log("kh2 enemies: KH2's player's attack (atkp %d, power %d) struck on %08x: %d damage", o.atkp, power,
                   hit.days, hit.blow);
}
namespace {

// KH2's reflection of attacks (YS::ATTACK_WORK_MANAGER::check_reflect, the PS3 build's): two attacks of other teams
// meet, each can still hit the other's owner, and the table IsReflectTable says whether their types reflect each other
// (types: 0 ATTACK, 1 S_ATTACK, 2 GUARD, 3 S_GUARD, 4 SPECIAL, 5 CURE, 6 C_CURE) -- an ATTACK reflects an ATTACK, a
// GUARD and an S_GUARD; an S_ATTACK an S_GUARD alone. A Days player's blow stands for KH2's player's plain attack (an
// ATTACK reflecting itself REFLECT, from any way) and his block for his guard (a GUARD reflecting itself NULL)
bool reflects(int a, int b)
{
    static constexpr std::uint8_t table[7][7] = {{1, 0, 1, 1, 0, 0, 0}, {0, 0, 0, 1, 0, 0, 0}, {1, 0, 0, 0, 0, 0, 0},
                                                 {1, 1, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0},
                                                 {0, 0, 0, 0, 0, 0, 0}};
    return a >= 0 && a < 7 && b >= 0 && b < 7 && table[a][b] != 0;
}

// Whether the attack's owner reflects what comes from the other's owner (ATTACK_WORK::is_reflect_range): its + 0x22 0,
// from any way; else the way from the other's owner to its own along the ground (ATTACK::get_dir) against its owner's
// facing turned by twice its + 0x23 (degrees: VectorRotY), within + 0x22 degrees of coming straight at it
bool in_reflect_range(const AttackParam &p, const Actor &owner, const Object &other)
{
    if (p.reflect_range == 0)
        return true;
    constexpr float kDegree = kPi / 180.0F;
    const Vec3 dir = facing_of(owner.yaw() + float(p.reflect_angle * 2) * kDegree);
    const Vec3 to = owner.place(), from = other.place();
    const float wx = to[0] - from[0], wz = to[2] - from[2], l = std::sqrt(wx * wx + wz * wz);
    const float along = l > 0.0F ? (wx * dir[0] + wz * dir[2]) / l : 0.0F;
    return along < -std::cos(float(p.reflect_range) * kDegree);
}
} // namespace

// The reflected attack's own motion (BTLOBJ::exec_reflect_default): its + 0x15 started at once, its idle after (4, its
// fall, in the air); on the ground slid back against its facing by its + 0x16 (under 0 50, its sheet's hitback
// addition added: 1:348) a tenth of it a tick, unless its + 0x120 bit 7 holds it (OBJ::can_hitback). In the air KH2
// throws it back up (its class's blow, 50 up): not made
void reflect_motion(Enemy &e, const AttackParam &p)
{
    Actor &a = *e.actor;
    const bool air = a.jump.on || (a.u32(field::kFlags) & bit::kInAir) != 0;
    if (a.play(p.reflect_motion, 0.0F, 0.0F, true)) {
        a.motion.queued.clear();
        a.motion.queued.push_back({air ? 4 : 0, 0.0F});
        e.reflect_motion = p.reflect_motion;
        e.reacting = false;
        e.paused = false;
        a.damage_motion = true;
    }
    float cm = p.reflect_hitback < 0 ? 50.0F : float(p.reflect_hitback);
    if (air || cm <= 0.0F || (a.u32(field::kFlags) & (1u << 7)) != 0)
        return;
    if (const std::uint32_t sheet = a.u32(0x5c0); sheet != 0) {
        const std::uint32_t bits = mod().memory.u32(sheet + 0x1c8);
        float more = 0.0F;
        std::memcpy(&more, &bits, 4);
        cm += more;
    }
    const Vec3 f = facing_of(a.yaw());
    a.slide = {-f[0], 0.0F, -f[2]};
    a.slide_speed = cm * 0.1F;
}
namespace {

// The enemy's side of a reflection (ATTACK_WORK::reflect on its attack, then YS::PIERROT::_OVR_exec_reflect): its own
// attack reflected unless it reflects itself NULL (+ 4's 0x200 and the other's owner at + 0x84 -- the port's 2:39 and
// 2:44 on an attack of its own); its state's routine for it (ACT::reflect: event 7) and its trigger 19, both given its
// attack and the other (the other read first: Lexaeus's asks whether the other's owner is a player in play and then
// sets his own attack's 0x40, 2:26, so he plays no motion of it); then, unless its attack's 0x40 is set or it reflects
// itself NULL, its reflect motion (its state left: ACT::clear) and its reflect state (+ 0x18: ACT::start), its 0x40
// set; its script's routine for it last (2:74: its value, its attack, the other)
void reflect_enemy(Enemy &e, std::uint32_t mine, AttackObject *object, const AttackParam &p, Object &by)
{
    Actor &a = *e.actor;
    if (object != nullptr && p.refact_self != 2) {
        object->landed = true;
        object->hit = by.address();
    }
    // the other: the Days blow's (or block's) attack, its owner the player (2:24)
    if (!days_attack)
        days_attack = std::make_unique<Object>(mod().memory, "a Days hit's attack");
    days_attack->set_u32(0xc, by.address());
    days_attack_owner = by.address();
    const ai::Value values[2] = {ai::Value::of_address(mine), ai::Value::of_address(days_attack->address())};
    a.states().event(a, 7, values);
    a.event(19, values);
    const bool own = (mod().memory.u32(mine + 4) & 0x40u) == 0 && p.refact_self != 2;
    if (own) {
        if (p.reflect_motion != 0) {
            reflect_motion(e, p);
            a.states().clear(a);
        }
        if (!p.reflect_act.empty())
            a.states().enter(a, p.reflect_act);
        mod().memory.set_u32(mine + 4, mod().memory.u32(mine + 4) | 0x40u);
    }
    if (object != nullptr && object->on_reflect != 0) {
        const ai::Value v[3] = {ai::Value::of_address(object->on_reflect_value), ai::Value::of_address(mine),
                                ai::Value::of_address(days_attack->address())};
        a.machine().start_routine(a.machine().main_thread(), object->on_reflect, v);
    }
    if (tracing())
        khdays_log("kh2 enemies: %08x's attack (atkp %d) reflected: %s", e.days,
                   std::int32_t(mod().memory.u32(mine + 0x18)),
                   !own ? (p.refact_self == 2 ? "it reflects itself NULL" : "its script played its own (0x40)")
                   : p.reflect_motion != 0  ? "its motion played"
                   : !p.reflect_act.empty() ? "its state entered"
                                            : "nothing of its own");
}

// A Days player's side of his blow's reflection (YS::PARTY::_OVR_exec_reflect, his blow reflecting itself REFLECT):
// the attack it met makes NULL of it (+ 0x14 2) -- a spark (the system effects' 13) and no more; GUARD (1) -- the spark
// 12, pushed 200 cm away from the attack (BTLOBJ::set_hitback; KH2's player's motion 42); REFLECT (0) -- the spark 13,
// thrown back 50 cm (exec_reflect_default; his motion 41). Days's player staggers with no damage instead of KH2's
// motions (khdays_kh2_shell_player_recoil), pushed as a Days hit pushes him (hitback_push: half the way, Days's drift
// moving him twice it). The spark on the attack's sphere toward him (ATTACK_WORK::draw_refmark: on the blow's, which
// Days's blow has not)
void reflect_player(Enemy &e, const AttackParam &p, unsigned player, const float centre[3], float radius,
                    const float body[3])
{
    float dx = body[0] - centre[0], dy = body[1] - centre[1], dz = body[2] - centre[2];
    const float l = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (l > 0.0F) {
        const float spark[3] = {centre[0] + dx / l * radius, centre[1] + dy / l * radius, centre[2] + dz / l * radius};
        if (const std::uint32_t h = cast(e, p.refact_other == 1 ? 12 : 13, 0, false, &system_effects()); h != 0)
            for (Enemy::Running &r : e.fx)
                if (r.handle == h) {
                    r.in_world = true;
                    r.at = to_kh2(spark);
                }
    }
    const float cm = p.refact_other == 2 ? 0.0F : p.refact_other == 1 ? 200.0F : 50.0F;
    const float flat = std::sqrt(dx * dx + dz * dz);
    if (cm <= 0.0F || flat <= 0.0F)
        return;
    const float half = cm / kKh2PerDays * 0.5F;
    recoils[player] = {dx / flat * half, 0.0F, dz / flat * half};
}

// How far a point lies from a segment (Days's world)
float segment_distance(const float a[3], const float b[3], const float p[3])
{
    const float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    const float len = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    const float t = len > 0.0F ? std::clamp((ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / len, 0.0F, 1.0F) : 0.0F;
    const float d[3] = {ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}
} // namespace

// A Days player's blow landing on a KH2 enemy met by an attack of its that stands now, as check_reflect meets two
// attacks: an attack it can still hit him with (each player once a range: attack(), its struck), of a type that
// reflects KH2's player's ATTACK, within its cone, one of its spheres reaching his blow -- Days's blow has no shape of
// KH2's, so the line from his middle to the enemy's sphere's centre, as thick as his body. Its motion's attacks first
// (ranges 10 and 33 now), then its own (2:10, a sphere at its place). Reflected: neither hits the other (his blow
// answered with no damage, the attack's range struck him), each side's reflection played; whether it was
bool clashed(Enemy &e, unsigned player, Object &who)
{
    KhdaysKh2Shell s;
    if (!e.actor || !e.motions || e.pose.empty() || !khdays_kh2_shell_read(e.days, &s))
        return false;
    std::vector<KhdaysKh2Player> list(std::size_t(std::max(0, khdays_kh2_shell_players(shell_of(e), nullptr, 0))));
    const int n = khdays_kh2_shell_players(shell_of(e), list.data(), int(list.size()));
    const KhdaysKh2Player *p = nullptr;
    for (int i = 0; i < n && i < int(list.size()); ++i)
        if (list[std::size_t(i)].object == player)
            p = &list[std::size_t(i)];
    if (p == nullptr)
        return false;
    const float body[3] = {p->place[0], p->place[1] + kPlayerMiddle, p->place[2]};
    const float to[3] = {s.place[0], s.place[1] + s.radius, s.place[2]};
    Actor &a = *e.actor;
    const auto clash = [&](std::uint32_t mine, AttackObject *object, const AttackParam &ap, const float c[3],
                           float radius) {
        if (tracing())
            khdays_log("kh2 enemies: player %08x's blow on %08x meets its attack (atkp %d, type %d): reflected (it "
                       "reflects itself %d, the blow %d)",
                       player, e.days, std::int32_t(mod().memory.u32(mine + 0x18)), ap.kind, ap.refact_self,
                       ap.refact_other);
        reflect_enemy(e, mine, object, ap, who);
        reflect_player(e, ap, player, c, radius, body);
    };
    const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.motions->attributes(a.motion.id) : nullptr;
    // its struck as attack() keeps them: of this motion and on, else none yet (made so, attack() goes on with them)
    if (a.motion.id != e.attack_motion || a.motion.time < e.attack_time) {
        e.struck.clear();
        e.windows_told.clear();
        if (e.attack_record != 0)
            mod().memory.set_u32(e.attack_record + 4, 0);
        e.attack_motion = a.motion.id;
        e.attack_time = a.motion.time;
    }
    for (std::size_t r = 0; attr != nullptr && r < attr->ranges.size(); ++r) {
        const auto &range = attr->ranges[r];
        if (!attack_range_on(range, a.motion.time))
            continue;
        const int atkp = attack_key(a, std::int16_t(range.params[0])), group = std::int16_t(range.params[1]);
        const AttackParam &ap = attack_param(atkp);
        const char *why = ap.team == 1                      ? "its player's team's"
                          : !reflects(ap.kind, kAttackType) ? "a type no blow reflects with"
                          : e.struck[r].count(player) != 0  ? "it struck him already"
                          : !in_reflect_range(ap, a, who)   ? "out of its cone"
                                                            : nullptr;
        if (why != nullptr) {
            if (trace_level() >= 2)
                khdays_log("kh2 enemies: player %08x's blow on %08x in its motion %d at %.1f (atkp %d, type %d): not "
                           "reflected, %s",
                           player, e.days, a.motion.id, double(a.motion.time), atkp & 0xffff, ap.kind, why);
            continue;
        }
        float nearest = std::numeric_limits<float>::max();
        for (const Shape &sh : e.kind->shapes) {
            if (sh.group != group || (sh.kind & 0xff) != 6)
                continue;
            float c[3];
            days_point(e, s, sh.bone, sh.at, c);
            const float radius = sh.radius / kKh2PerDays, apart = segment_distance(body, to, c) - radius - p->radius;
            nearest = std::min(nearest, apart);
            if (apart >= 0.0F)
                continue;
            e.struck[r].insert(player);
            clash(motion_attack(e, atkp), nullptr, ap, c, radius);
            return true;
        }
        if (trace_level() >= 2)
            khdays_log("kh2 enemies: player %08x's blow on %08x in its motion %d at %.1f (atkp %d): not reflected, its "
                       "spheres %.2f off his blow",
                       player, e.days, a.motion.id, double(a.motion.time), atkp & 0xffff, double(nearest));
    }
    for (auto &[at, o] : attacks) {
        if (o.enemy != e.days || o.players || o.ended || o.expired || !o.placed || o.radius <= 0.0F ||
            o.struck.count(player) != 0)
            continue;
        const AttackParam &ap = attack_param(o.atkp);
        if (ap.team == 1 || !reflects(ap.kind, kAttackType) || !in_reflect_range(ap, a, who))
            continue;
        float c[3];
        to_days(o.at, c);
        const float radius = o.radius / kKh2PerDays;
        if (segment_distance(body, to, c) >= radius + p->radius)
            continue;
        o.struck.insert(player);
        clash(at, &o, ap, c, radius);
        return true;
    }
    return false;
}

// A Days player struck by a KH2 enemy's attack (Ov107_InvokeHitCallback through the shell): KH2's guard reflects an
// ATTACK alone (IsReflectTable), so his block lets an S_ATTACK or a SPECIAL through (Sephiroth's split flash and
// Heartless Angel, KH2's unguardable attacks); his block of an ATTACK is KH2's guard reflecting it -- the enemy's side
// played once Days's hit is over (reflect_enemy; Days's block plays his own). Whether the hit was taken
int hit_player(Enemy &e, unsigned player, int atkp, const float push[3], AttackObject *object)
{
    const AttackParam &p = attack_param(atkp);
    std::uint32_t record = 0;
    if (object != nullptr) {
        for (auto &[at, o] : attacks)
            if (&o == object)
                record = at;
    } else {
        record = motion_attack(e, atkp);
    }
    const Delivering before = delivering;
    delivering = {e.days, atkp, record, object, 0};
    const int took = khdays_kh2_shell_hit(e.days, player, e.slot, push, p.kind == 1 || p.kind == 4 ? 1 : 0);
    const Delivering now = delivering;
    delivering = before;
    if (now.guarded_by != 0 && record != 0 && e.actor && !e.dying) {
        Object *who = nullptr;
        if (const auto it = players.find(now.guarded_by); it != players.end())
            who = it->second.get();
        if (who != nullptr && reflects(p.kind, kGuardType) && in_reflect_range(p, *e.actor, *who)) {
            if (tracing())
                khdays_log("kh2 enemies: player %08x's block reflects %08x's attack (atkp %d)", now.guarded_by, e.days,
                           atkp);
            reflect_enemy(e, record, object, p, *who);
        }
    }
    return took;
}

// An enemy's attacks of their own this tick (YS::ATTACK_MAGIC's virtual + 0x18, FUN_1404376b0): each following its
// effect (gone with it), growing (gone grown), swept from its place before to its place now in steps of its radius
// against Days's players -- a sphere when its height is its radius, else a cylinder that high from its place up -- each
// player hit once; those ended let go
void attack_objects(Enemy &e)
{
    std::vector<KhdaysKh2Player> list;
    for (auto it = attacks.begin(); it != attacks.end();) {
        AttackObject &o = it->second;
        if (o.enemy != e.days) {
            ++it;
            continue;
        }
        if (o.ended || e.dying) {
            mod().memory.remove(it->first);
            it = attacks.erase(it);
            continue;
        }
        if (o.expired) { // its script may still ask after it (2:14, 2:29) until it ends it
            ++it;
            continue;
        }
        if (o.effect != 0) {
            Vec3 place;
            if (!the_field().effect_where(o.effect, place))
                o.expired = true;
            else
                the_field().attack_place(it->first, place);
        }
        float radius = o.radius, height = o.height;
        if (o.grow_for > 0.0F) {
            if ((o.grow_ticks += 1.0F) > o.grow_for)
                o.expired = true;
            const float f = std::sin(std::min(o.grow_ticks / o.grow_for, 1.0F) * kPi * 0.5F);
            radius = o.grow_radius * (1.0F - f) + o.radius * f;
            height = o.grow_height * (1.0F - f) + o.height * f;
        }
        if (o.expired) {
            ++it;
            continue;
        }
        // the room's floor met (2:41, + 8 bit 4: Hades's fire shots end on it): a ray from its middle down by its size
        // (KH2's y down) strikes the ground
        if (o.placed && !o.on_floor) {
            const float reach = std::max(std::max(radius, height * 0.5F), 1.0F);
            float from[3], to[3], hit[3];
            to_days(o.at, from);
            to_days({o.at[0], o.at[1] + reach, o.at[2]}, to);
            o.on_floor = khdays_kh2_shell_ray(shell_of(e), from, to, hit) != 0;
            if (o.on_floor) { // where (2:42: + 0xb4)
                const Vec3 at = to_kh2(hit);
                const float met[4] = {at[0], at[1], at[2], 1.0F};
                mod().memory.write(it->first + 0xb4, met, sizeof met);
            }
        }
        // (a height of 0 a sphere: 2:11 sizes Demyx's notes 150 by 0)
        const float step = height > 0.0F ? std::min(radius, height) : radius;
        if (o.placed && step > 0.0F && !o.players) {
            if (list.empty()) {
                list.resize(std::size_t(std::max(0, khdays_kh2_shell_players(shell_of(e), nullptr, 0))));
                list.resize(
                    std::size_t(std::max(0, khdays_kh2_shell_players(shell_of(e), list.data(), int(list.size())))));
            }
            const Vec3 way = {o.at[0] - o.before[0], o.at[1] - o.before[1], o.at[2] - o.before[2]};
            const float length = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
            const int steps = std::max(1, int(std::ceil(length / step)));
            for (int k = steps; k >= 1; --k) {
                const float f = float(k) / float(steps);
                float c[3];
                to_days({o.before[0] + way[0] * f, o.before[1] + way[1] * f, o.before[2] + way[2] * f}, c);
                for (const KhdaysKh2Player &p : list) {
                    if (o.struck.count(p.object) != 0)
                        continue;
                    const float r = radius / kKh2PerDays, up = (radius == height ? 0.0F : height / kKh2PerDays);
                    // his body's middle line (feet a radius up to his top a radius down) against the sphere's centre or
                    // the cylinder's axis
                    const float low = p.place[1] + p.radius,
                                high = std::max(low, p.place[1] + kPlayerHeight - p.radius);
                    const float y = std::clamp(c[1] + up * 0.5F, low, high);
                    const float dy =
                        up > 0.0F ? std::max(0.0F, std::fabs(y - (c[1] + up * 0.5F)) - up * 0.5F) : y - c[1];
                    const float dx = p.place[0] - c[0], dz = p.place[2] - c[2];
                    if (std::sqrt(dx * dx + dz * dz + dy * dy) >= r + p.radius)
                        continue;
                    Object *who = nullptr;
                    if (const auto pl = players.find(p.object); pl != players.end())
                        who = pl->second.get();
                    if (who != nullptr) {
                        if (const auto h = held.find(p.object); h != held.end() && h->second.unhittable) {
                            // invincible (his held motion's range 27): landed with no damage (landed's star)
                            o.struck.insert(p.object);
                            o.landed = true;
                            o.hit = who->address();
                            if (tracing())
                                khdays_log("kh2 enemies: %08x's attack (atkp %d) lands on %08x, invincible", e.days,
                                           o.atkp, p.object);
                            landed(e, p.object, o.atkp, true);
                            continue;
                        }
                        if (tracing())
                            khdays_log("kh2 enemies: %08x's attack (atkp %d) hits %08x", e.days, o.atkp, p.object);
                        strike(o, *who);
                    }
                }
                // the party members (their bodies against its sphere, or its cylinder from its place up)
                const Vec3 centre = to_kh2(c);
                each_member([&](Enemy &m, Friend &f) {
                    if (o.struck.count(m.days) != 0 ||
                        !member_reached(m, centre, radius, radius == height ? 0.0F : height, kTypeHit))
                        return;
                    o.struck.insert(m.days);
                    member_hurt(e, m, f, o.atkp);
                });
            }
        }
        o.before = o.at;
        ++it;
    }
}

} // namespace khdays::kh2_enemies::shell
