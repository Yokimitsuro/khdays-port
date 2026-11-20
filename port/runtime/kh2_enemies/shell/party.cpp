// A party member (Riku): KH2's party AI, its weapon, its hurts.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {
const std::vector<FriendRow> kRikuActions = {{"hp_item", 0x18, FriendDo::HpItem, -1, 0, 0, nullptr},
                                             {"mp_item", 0x28, FriendDo::MpItem, -1, 0, 0, nullptr},
                                             {"idle", 0x201, FriendDo::Idle, -1, 0, 0, nullptr},
                                             {"move", 0x201, FriendDo::Move, -1, 0, 0, nullptr},
                                             {"normal_attack1", 2, FriendDo::Attack, 44, 0, 0, nullptr},
                                             {"normal_attack2", 2, FriendDo::Attack, 45, 0, 0, nullptr},
                                             {"jump_attack1", 2, FriendDo::Attack, 46, 0, 0, nullptr},
                                             {"jump_attack2", 2, FriendDo::Attack, 47, 0, 0, nullptr},
                                             {"cure", 0x10, FriendDo::Cure, 89, 46, 187, "cure"}};

// kn::VecSet from `from` to `to` (KH2's world): its height (to's y less from's: up is negative), its way along the
// ground and that way's length
struct FriendVec final {
    float height = 0.0F, along = 0.0F;
    Vec3 way{};
};

bool member_stun(Friend &f, Enemy &e);
bool member_hurting(Friend &f, Enemy &e);

// kn::Friend's constructor: a member's parameters' index by his entry's part (+ 0x4c)
int friend_index(int part, int weapon_part)
{
    switch (part) {
    case 2: return weapon_part == 0x12 ? 0xc : 0; // Donald
    case 3: return weapon_part == 0x15 ? 0xd : 1; // Goofy
    case 5: return 2;                             // Auron
    case 6: return 4;                             // Mulan
    case 7: return 5;                             // Aladdin
    case 8: return 6;                             // Sparrow
    case 9: return 7;                             // Beast
    case 10: return 8;                            // Jack
    case 11: return 9;                            // Simba
    case 12: return 10;                           // Tron
    case 13: return 0xb;                          // Riku
    case 15: return 3;                            // Pinocchio's
    case 200: return 0xe;                         // Stitch
    case 201: return 0xf;                         // Genie
    case 202: return 0x10;                        // Peter Pan
    case 203: return 0x11;                        // Tinker Bell
    case 204: return 0x12;                        // Chicken Little
    default: return 0;
    }
}

// The members' parameters read once from the mod (kh2/friend_param.bin)
const FriendParams *friend_params(int index)
{
    static std::vector<FriendParams> all;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> b = read_file(kh2_file("friend_param.bin"));
        const auto f32 = [&b](std::size_t at) {
            float v = 0.0F;
            if (at + 4 <= b.size())
                std::memcpy(&v, b.data() + at, 4);
            return v;
        };
        const auto u32 = [&b](std::size_t at) {
            std::uint32_t v = 0;
            if (at + 4 <= b.size())
                std::memcpy(&v, b.data() + at, 4);
            return v;
        };
        for (std::size_t i = 0; b.size() >= 19 * 4 && i < 19; ++i) {
            const std::size_t o = u32(4 * i);
            FriendParams p;
            if (o + 0xe0 > b.size()) {
                all.push_back(p);
                continue;
            }
            for (int m = 0; m < 4; ++m)
                for (int k = 0; k < 2; ++k) {
                    p.follow[m][k] = f32(o + std::size_t(8 * m + 4 * k));
                    p.battle_player[m][k] = f32(o + 0x20 + std::size_t(8 * m + 4 * k));
                    p.battle_enemy[m][k] = f32(o + 0x40 + std::size_t(8 * m + 4 * k));
                }
            const std::size_t bp = o + 0x60;
            p.search_range = f32(bp + 8), p.lost_distance = f32(bp + 0xc);
            p.idle_time[0] = f32(bp + 0x10), p.idle_time[1] = f32(bp + 0x14);
            p.move_time[0] = f32(bp + 0x18), p.move_time[1] = f32(bp + 0x1c);
            const std::uint32_t an = u32(o + 0xa0), ao = u32(o + 0xa4), hn = u32(o + 0xa8), ho = u32(o + 0xac);
            for (std::uint32_t k = 0; k < an && o + ao + 0x80 * (k + 1) <= b.size(); ++k) {
                const std::size_t a = o + ao + 0x80 * k;
                FriendAction x;
                std::memcpy(&x.id, b.data() + a, 4);
                std::memcpy(&x.motion, b.data() + a + 4, 4);
                std::memcpy(&x.need_mp, b.data() + a + 8, 4);
                x.flag = u32(a + 0xc);
                x.range_zx[0] = f32(a + 0x10), x.range_zx[1] = f32(a + 0x14);
                x.range_y[0] = f32(a + 0x18), x.range_y[1] = f32(a + 0x1c), x.jump = f32(a + 0x20);
                x.check_y[0] = f32(a + 0x2c), x.check_y[1] = f32(a + 0x30), x.disable_y_range = f32(a + 0x34);
                x.disable_y = f32(a + 0x38), x.cure = f32(a + 0x3c);
                std::memcpy(x.raw.data(), b.data() + a, 0x80);
                p.actions.push_back(x);
            }
            for (std::uint32_t k = 0; k < hn && o + ho + 0x20 * (k + 1) <= b.size(); ++k) {
                const std::size_t a = o + ho + 0x20 * k;
                p.hops.push_back({f32(a), f32(a + 4), f32(a + 8), f32(a + 0xc), f32(a + 0x10)});
            }
            all.push_back(std::move(p));
        }
        if (all.empty())
            khdays_log("kh2 enemies: no party AI's parameters (the mod's kh2/friend_param.bin): partners stand still");
    }
    return index >= 0 && std::size_t(index) < all.size() ? &all[std::size_t(index)] : nullptr;
}

// PREF::Party of a weapon part (03system.bin's pref "prty": a count, the records' offsets, PREF_PARTY 0x34 bytes)
PartySpec party_spec(int weapon_part)
{
    PartySpec spec;
    const std::vector<std::uint8_t> sys = read_file(kh2_file("03system.bin"));
    try {
        for (const kh2::BarEntry &e : kh2::bar_entries(sys.data(), sys.size())) {
            if (e.name != "pref")
                continue;
            const std::uint8_t *pref = sys.data() + e.offset;
            for (const kh2::BarEntry &s : kh2::bar_entries(pref, e.size)) {
                if (s.name != "prty" || s.size < 8)
                    continue;
                std::int32_t count = 0;
                std::memcpy(&count, pref + s.offset, 4);
                int n = weapon_part >= 0 && weapon_part < count ? weapon_part : 1;
                std::uint32_t at = 0;
                std::memcpy(&at, pref + s.offset + 4 + 4 * std::size_t(n), 4);
                if (at == 0)
                    std::memcpy(&at, pref + s.offset + 8, 4); // (none: the first's, KH2's 1)
                if (at == 0 || at + 0x34 > s.size)
                    continue;
                float r[13];
                std::memcpy(r, pref + s.offset + at, sizeof r);
                spec.walk = r[0], spec.run = r[1], spec.jump = r[2], spec.turn = r[3], spec.stun_time = r[6];
            }
        }
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: the mod's kh2/03system.bin cannot be read (%s)", x.what());
    }
    return spec;
}

// A party member's weapon read from the mod (its kind's Kind::weapon, weapon_set), on its hand's bone; none when its
// kind names none or its files are not there
std::shared_ptr<PartyWeapon> party_weapon(const Enemy &e, int skeleton)
{
    const Kind *kind = find_kind(mod().kinds, e.kind->name);
    if (kind == nullptr || kind->weapon.empty())
        return nullptr;
    try {
        const fs::path mdlx = kh2_file("obj/" + kind->weapon + ".mdlx", kind->folder);
        const auto model = read_file(mdlx);
        const auto mset = read_file(kh2_file("obj/" + kind->weapon_set + ".mset", kind->folder));
        if (model.empty() || mset.empty())
            throw std::runtime_error("its files are not in the mod");
        auto w = std::make_shared<PartyWeapon>();
        const std::u8string p8 = mdlx.u8string();
        w->mdlx.assign(p8.begin(), p8.end());
        w->set = std::make_shared<const kh2::MotionSet>(mset.data(), mset.size());
        w->skeleton =
            std::make_shared<const kh2::MotionSkeleton>(kh2::read_motion_skeleton(model.data(), model.size()));
        w->player = std::make_unique<kh2::MotionPlayer>(*w->skeleton);
        w->motions = std::make_unique<SetMotions>(w->set, mset.size() >= 16 && (mset[12] & 1) != 0);
        w->shapes = read_shapes(model);
        w->hand = weapon_bone(skeleton);
        if (tracing())
            khdays_log("kh2 enemies: %s's weapon %s (%s), on its bone %d, %zu shapes", e.kind->name.c_str(),
                       kind->weapon.c_str(), kind->weapon_set.c_str(), w->hand, w->shapes.size());
        return w->hand >= 0 ? w : nullptr;
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: %s's weapon %s cannot be read (%s)", e.kind->name.c_str(), kind->weapon.c_str(),
                   x.what());
    }
    return nullptr;
}
} // namespace

// An ally that is a party member (its entry's kind 1) made one of KH2's party AI's (Friend)
void friend_start(Enemy &e)
{
    const std::uint32_t record = e.actor->u32(field::kEntry);
    if (record == 0 || (mod().memory.u32(record + 4) & 0xff) != 1)
        return;
    const int part = int(mod().memory.u32(record + 0x4c) & 0xffff), weapon = int(mod().memory.u32(record + 0x4c) >> 16);
    Friend f;
    f.ally = e.days;
    f.character = part;
    f.hp = f.hp_most = character_health(part);
    f.params = friend_params(friend_index(part, weapon));
    f.spec = party_spec(weapon);
    f.table = part == 13 ? &kRikuActions : nullptr;
    f.weapon = party_weapon(e, int(mod().memory.u32(record + 4) >> 24));
    if (f.params == nullptr || f.table == nullptr) {
        khdays_log("kh2 enemies: %s's party AI not known (part %d): it stands where it is", e.kind->name.c_str(), part);
        return;
    }
    if (f.hp_most <= 0)
        khdays_log("kh2 enemies: %s's health not known (the mod's kh2/00battle.bin has no plrp record of character "
                   "%d): no enemy's attack reaches it",
                   e.kind->name.c_str(), part);
    if (tracing())
        khdays_log("kh2 enemies: %08x (%s) is KH2's party member %d (params %d): walk %.1f run %.1f, %zu actions, "
                   "health %d",
                   e.days, e.kind->name.c_str(), part, friend_index(part, weapon), double(f.spec.walk),
                   double(f.spec.run), f.params->actions.size(), f.hp_most);
    friends[e.days] = f;
}

const FriendAction *friend_action(const Friend &f, int id)
{
    for (const FriendAction &a : f.params->actions)
        if (a.id == id)
            return &a;
    return nullptr;
}
namespace {

FriendVec friend_vec(const Vec3 &from, const Vec3 &to)
{
    FriendVec v;
    v.height = to[1] - from[1];
    const float dx = to[0] - from[0], dz = to[2] - from[2];
    v.along = std::sqrt(dx * dx + dz * dz);
    v.way = v.along > 0.0F ? Vec3{dx / v.along, 0.0F, dz / v.along} : Vec3{};
    return v;
}

// Friend::isInsideRange: the target within the action's ranges along the ground and up (its flags' bit 0 none
// along the ground, bit 1 none up)
bool friend_in_range(const FriendAction &a, const FriendVec &v, float size)
{
    if ((a.flag & 1) == 0 && (v.along > a.range_zx[1] + size || v.along < a.range_zx[0] - size))
        return false;
    if ((a.flag & 2) == 0 && (v.height < -a.range_y[1] - size || v.height > size - a.range_y[0]))
        return false;
    return a.disable_y_range <= v.along || std::abs(v.height) <= a.disable_y;
}

// Friend::canAction for an attack: standing (its action idle's), its target in the action's ranges, in front of it
bool friend_can_attack(const Friend &f, const Enemy &self, const FriendAction &a, const FriendVec &v, float size)
{
    if (f.attacking || self.actor->jump.on || !friend_in_range(a, v, size))
        return false;
    const Vec3 face = facing_of(self.actor->yaw());
    return a.jump != 0.0F || face[0] * v.way[0] + face[2] * v.way[2] >= -1.0F;
}

// Friend::followMove: its move mode by the distance to where it goes (each mode's least and most: up a mode past
// the most, down below the least), its speed the mode's (none, walk, run), its way there
void friend_follow(Friend &f, const FriendVec &v, const float ranges[4][2])
{
    if (v.along > ranges[f.mode][1] && f.mode < 2)
        ++f.mode;
    else if (v.along < ranges[f.mode][0] && f.mode > 0)
        --f.mode;
    f.speed = f.mode == 0 ? 0.0F : f.mode == 1 ? f.spec.walk : f.spec.run;
    f.way = v.way;
}
} // namespace

// A KH2 tick of a party member (kn::Friend::normal_exec, battleAction, followMove; YS::ACTION_IDLE): its target (the
// nearest of the KH2 enemies within its search range, kept till it is farther than its lost distance), out of battle
// or without one the player followed, else an action its table's routines allow -- standing or moving a while (its
// idle and move times), an attack whose ranges reach (its motion played from the start, a jump toward the target
// for the jumping ones) -- picked by KH2's chance, not the same thrice; its speed and way turned into its steps
// (swept against the walls), its turn toward its way (its spec's turn a tick), its motion its stand's, walk's or
// run's (ACTION_IDLE: walking below half its walk and run speeds), 8 ticks blended
void friend_tick(Friend &f, Enemy &e, Vec3 &place)
{
    Actor &a = *e.actor;
    const FriendParams &p = *f.params;
    // its set's mode (YS::MOTION::id_to_num): 1 in battle, 2 out of it, turned over with its weapon
    e.motions->mode = (the_field().in_battle() ? 1 : 2) ^ (f.weapon != nullptr ? 1 : 0);
    // knocked out, his time runs out (YS::FRIEND::exec_stun, his pre_exec); the party AI waits while he lies (54: its
    // motion grants no control, MOTION's flags + 0x160 bits 2 to 4 and 30, kn::Friend::normal_exec) and while his
    // damage reaction plays out. A member whose set has no 54 (this Riku) fights on knocked out, as KH2's does
    const bool knocked = member_stun(f, e);
    if ((knocked && e.actor->motion.id == 54) || member_hurting(f, e)) {
        f.attacking = false;
        f.speed = 0.0F;
        f.action = -1;
        return;
    }
    // KH2's party AI stands aside while a script's thread holds him (YS::OBJ::is_hook: Final Xemnas's grab moves him
    // by the player's pad, 1:113 and 1:111) and while an event plays (YS::EVENT's ControlOff of the party): his motions
    // and place are the script's then
    if (a.hooked() || the_field().event_playing()) {
        f.attacking = false;
        f.speed = 0.0F;
        f.action = -1;
        return;
    }
    if (f.action_time > 0.0F)
        f.action_time -= 1.0F;
    Object *player = the_field().player(a);
    const Vec3 me = a.place();
    Enemy *t = nullptr;
    if (const auto it = enemies.find(f.target);
        it != enemies.end() && !it->second->dying && !it->second->released && it->second->started)
        t = it->second.get();
    const bool battle = the_field().in_battle();
    if (t != nullptr && friend_vec(me, t->actor->place()).along > p.lost_distance)
        t = nullptr;
    if (battle && t == nullptr) { // Friend::searchEnemy: the nearest within its search range
        float best = p.search_range;
        for (auto &[days, x] : enemies) {
            if (x->dying || x->released || !x->started || !x->actor)
                continue;
            const float d = friend_vec(me, x->actor->place()).along;
            if (d <= best)
                best = d, t = x.get();
        }
    }
    if (!battle)
        t = nullptr;
    const unsigned was = f.target;
    f.target = t != nullptr ? t->days : 0u;
    if (tracing() && was != f.target)
        khdays_log("kh2 enemies: %08x (partner) targets %08x", e.days, f.target);
    if (f.attacking) { // its attack's motion plays (its root moves it, its blows strike) till it ends
        if (a.motion.id == f.attack_motion && !a.motion.ended)
            return;
        f.attacking = false;
    }
    bool turn_to_target = false;
    if (t == nullptr) {
        if (player != nullptr)
            friend_follow(f, friend_vec(me, player->place()), battle ? p.battle_player : p.follow);
        else
            f.speed = 0.0F;
        f.action = -1;
    } else {
        const FriendVec v = friend_vec(me, t->actor->place());
        const float size = t->radius * kKh2PerDays;
        const auto attack_reaches = [&] {
            for (const FriendRow &r : *f.table)
                if (const FriendAction *x = r.does == FriendDo::Attack ? friend_action(f, r.param) : nullptr;
                    x != nullptr && friend_can_attack(f, e, *x, v, size))
                    return true;
            return false;
        };
        const bool going_on = f.action >= 0 && ((*f.table)[std::size_t(f.action)].attribute & 0x200) != 0 &&
                              f.action_time > 0.0F && !attack_reaches();
        if (!going_on) { // battleAction: the rows whose routine says yes, one picked
            std::vector<int> can;
            for (std::size_t i = 0; i < f.table->size(); ++i) {
                const FriendRow &r = (*f.table)[i];
                switch (r.does) {
                case FriendDo::Idle:
                case FriendDo::Move:
                    if (!attack_reaches())
                        can.push_back(int(i));
                    break;
                case FriendDo::Attack:
                    if (const FriendAction *x = friend_action(f, r.param);
                        x != nullptr && friend_can_attack(f, e, *x, v, size))
                        can.push_back(int(i));
                    break;
                default: break; // (its items and its cure: not yet)
                }
            }
            if (can.size() > 1 && f.history[0] == f.history[1] && f.history[1] == f.history[2])
                can.erase(std::remove(can.begin(), can.end(), f.history[0]), can.end());
            if (can.empty()) {
                f.action = -1;
            } else {
                f.action = can[std::size_t(kh2_chance() % std::uint32_t(can.size()))];
                f.history = {f.action, f.history[0], f.history[1]};
                const FriendRow &r = (*f.table)[std::size_t(f.action)];
                const auto timer = [](const float range[2]) {
                    return range[0] + float(kh2_chance() >> 8) / float(1u << 24) * (range[1] - range[0]);
                };
                if (r.does == FriendDo::Idle)
                    f.action_time = timer(p.idle_time);
                else if (r.does == FriendDo::Move)
                    f.action_time = timer(p.move_time);
                else if (const FriendAction *x = friend_action(f, r.param)) { // Friend::_OVR_attack
                    a.set_yaw(std::atan2(v.way[0], v.way[2]));
                    if (x->jump != 0.0F) { // a jump toward it: as high as its target over it, at its run's speed
                        const float up = std::clamp(x->jump - v.height, 0.0F, f.spec.jump);
                        const float ticks = std::sqrt(2.0F * up / kKh2Pull);
                        a.jump = {};
                        a.jump.on = true;
                        a.jump.arc = true;
                        a.jump.apex = ticks;
                        a.jump.rise = -up;
                        a.jump.along = {v.way[0] * f.spec.run, 0.0F, v.way[2] * f.spec.run};
                        a.velocity = a.steered = a.jump.along;
                        a.move_clock = 0.0F;
                        a.set_bits(field::kFlags, bit::kInAir, true);
                    }
                    a.play(x->motion, 2.0F, 0.0F);
                    a.motion.queued.clear();
                    a.motion.queued.push_back({x->jump != 0.0F ? 4 : 0, 2.0F});
                    f.attacking = true;
                    f.attack_motion = x->motion;
                    f.speed = 0.0F;
                    if (tracing())
                        khdays_log("kh2 enemies: %08x (partner) %s: motion %d at %.0f cm along, %.0f up", e.days,
                                   r.name, x->motion, double(v.along), double(-v.height));
                    return;
                }
                if (tracing())
                    khdays_log("kh2 enemies: %08x (partner) %s for %.0f ticks", e.days, r.name, double(f.action_time));
            }
        }
        const FriendRow *r = f.action >= 0 ? &(*f.table)[std::size_t(f.action)] : nullptr;
        if (r != nullptr && r->does == FriendDo::Idle) {
            f.speed = 0.0F;
            turn_to_target = true;
            f.way = v.way;
        } else { // battleMove: toward its target
            friend_follow(f, v, p.battle_enemy);
        }
    }
    // ACTION_IDLE: turned toward its way, its step along it, its motion its speed's
    if (f.own_turning && (f.speed > 0.0F || turn_to_target) && (f.way[0] != 0.0F || f.way[2] != 0.0F)) {
        float d = std::atan2(f.way[0], f.way[2]) - a.yaw();
        while (d > kPi)
            d -= 2.0F * kPi;
        while (d < -kPi)
            d += 2.0F * kPi;
        a.set_yaw(a.yaw() + std::clamp(d, -f.spec.turn, f.spec.turn));
    }
    if (f.speed > 0.0F && !a.jump.on) {
        Vec3 step = {f.way[0] * f.speed, 0.0F, f.way[2] * f.speed};
        wall_sweep(e.anchor, place, kWallRadius, step, true);
        place[0] += step[0];
        place[2] += step[2];
    }
    if (!a.jump.on) {
        const int want = f.speed <= 0.0F ? 0 : f.speed < (f.spec.walk + f.spec.run) * 0.5F ? 1 : 2;
        if (a.motion.id != want)
            a.play(want, 8.0F, 0.0F);
    }
}
namespace {

// Its weapon a KH2 tick: its motion its member's (the same number and time, his mode's), its bones on his hand's;
// its attack ranges' spheres (its own shapes, on its bones) strike the KH2 enemies they reach, each once a range, as
// the player's team's blows do (strike_enemies)
void weapon_tick(Friend &f, Enemy &e, const KhdaysKh2Shell &s)
{
    PartyWeapon *w = f.weapon.get();
    const Actor &a = *e.actor;
    if (w == nullptr)
        return;
    w->motions->mode = e.motions->mode;
    // its set hooked as his (1:75 on his weapon, YS::PARTY's + 0xaf8 object -- which the port's weapon is not, so
    // 1:234 finds none: the hook his own is given here, Final Xemnas's grab's 251)
    if (const int hook = e.motions->hook_id(); hook != w->motions->hook_id()) {
        if (hook < 0)
            w->motions->unhook();
        else
            w->motions->hook(hook);
    }
    // captured (1:29), its own part of the capturer's slot
    const bool captured = e.motions->capture_id() >= 0 && a.motion.id == e.motions->capture_id();
    const kh2::Motion *m = captured           ? (e.capture_weapon != nullptr ? e.capture_weapon->motion.get() : nullptr)
                           : a.motion.id >= 0 ? w->motions->motion(a.motion.id)
                                              : nullptr;
    if (m == nullptr || e.pose.empty() || std::size_t(w->hand) >= e.pose.size()) {
        w->pose.clear();
        return;
    }
    std::vector<kh2::Mat4> own;
    try {
        own = pose_at(*w->player, *m, a.motion.time);
    } catch (const std::exception &x) { // a motion made for another skeleton
        if (tracing())
            khdays_log("kh2 enemies: %s's weapon motion %d cannot be played (%s)", e.kind->name.c_str(), a.motion.id,
                       x.what());
        w->pose.clear();
        return;
    }
    w->pose.resize(own.size());
    for (std::size_t j = 0; j < own.size(); ++j)
        row_matrix_mul(own[j].data(), e.pose[std::size_t(w->hand)].data(), w->pose[j].data());
    if (a.motion.id != w->motion || a.motion.time < w->time)
        w->struck.clear();
    w->motion = a.motion.id;
    w->time = a.motion.time;
    const kh2::MotionAttributes *attr =
        captured ? (e.capture_weapon != nullptr ? e.capture_weapon->attributes.get() : nullptr)
                 : w->motions->attributes(a.motion.id);
    if (attr == nullptr)
        return;
    for (std::size_t r = 0; r < attr->ranges.size(); ++r) {
        const auto &range = attr->ranges[r];
        if (!attack_range_on(range, a.motion.time))
            continue;
        const int atkp = std::int16_t(range.params[0]), group = std::int16_t(range.params[1]);
        const int power = attack_power(atkp);
        for (const Shape &sh : w->shapes) {
            if (sh.group != group || (sh.kind & 0xff) != 6 || sh.bone < 0 || std::size_t(sh.bone) >= w->pose.size())
                continue;
            const kh2::Mat4 &b = w->pose[std::size_t(sh.bone)];
            const Vec3 p = {b[0] * sh.at[0] + b[4] * sh.at[1] + b[8] * sh.at[2] + b[12],
                            b[1] * sh.at[0] + b[5] * sh.at[1] + b[9] * sh.at[2] + b[13],
                            b[2] * sh.at[0] + b[6] * sh.at[1] + b[10] * sh.at[2] + b[14]};
            const Vec3 t = turned({p[0], 0.0F, p[2]}, days_turn(a.yaw()));
            const float c[3] = {s.place[0] + t[0] / kKh2PerDays, s.place[1] + p[1] / kKh2PerDays,
                                s.place[2] + t[2] / kKh2PerDays};
            const float radius = sh.radius / kKh2PerDays;
            for (auto &[days, other] : enemies) {
                Enemy &x = *other;
                if (x.released || x.dying || !x.started || w->struck[r].count(days) != 0)
                    continue;
                KhdaysKh2Shell ts;
                if (!khdays_kh2_shell_read(days, &ts) || !ts.standing)
                    continue;
                const float low = ts.place[1] + ts.radius,
                            high = std::max(low, ts.place[1] + kPlayerHeight - ts.radius);
                const float d[3] = {ts.place[0] - c[0], std::clamp(c[1], low, high) - c[1], ts.place[2] - c[2]};
                if (std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) >= radius + ts.radius)
                    continue;
                w->struck[r].insert(days);
                const int base = x.days_damage > 0 ? x.days_damage : last_days_damage > 0 ? last_days_damage : 1;
                x.blow += power > 0 ? std::max(1, base * power / 100) : 0;
                x.blow_blows += power > 0 ? double(power) / 100.0 : 0.0;
                x.blow_party = a.address(); // his, not the player's (the damage patterns' 2)
                blow_reaction(x, atkp);
                if (tracing())
                    khdays_log(
                        "kh2 enemies: %08x (partner)'s weapon, motion %d at %.1f (atkp %d, power %d), strikes %08x: "
                        "%d damage",
                        e.days, a.motion.id, double(a.motion.time), atkp, power, days, x.blow);
            }
        }
    }
}
} // namespace

// Its weapon drawn on its hand with its member's model's place (khdays_models_draw_posed: its bones given in his
// model's space)
void draw_weapon(const Friend &f, const float view[16], const float projection[16], unsigned alpha)
{
    const PartyWeapon *w = f.weapon.get();
    if (w == nullptr || w->pose.empty() || alpha == 0)
        return;
    std::vector<float> bones(w->pose.size() * 16);
    for (std::size_t b = 0; b < w->pose.size(); ++b)
        std::copy(w->pose[b].begin(), w->pose[b].end(), bones.begin() + std::ptrdiff_t(16 * b));
    khdays_models_draw_posed(w->mdlx.c_str(), view, projection, (alpha << 16) | (1u << 24), bones.data(),
                             w->pose.size(), 0);
}

// Its attack motions' blows (their ranges 10 and 33) on the KH2 enemies they reach: its team the player's
void friend_blows(Friend &f, Enemy &e, const KhdaysKh2Shell &s)
{
    weapon_tick(f, e, s);
    const Actor &a = *e.actor;
    if (a.motion.id != e.attack_motion || a.motion.time < e.attack_time)
        e.struck.clear();
    e.attack_motion = a.motion.id;
    e.attack_time = a.motion.time;
    const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.motions->attributes(a.motion.id) : nullptr;
    if (attr == nullptr || e.pose.empty())
        return;
    for (std::size_t r = 0; r < attr->ranges.size(); ++r) {
        const auto &range = attr->ranges[r];
        if (!attack_range_on(range, a.motion.time))
            continue;
        const std::size_t before = e.struck[r].size();
        strike_enemies(e, s, r, std::int16_t(range.params[1]), std::int16_t(range.params[0]));
        if (tracing() && e.struck[r].size() > before)
            khdays_log("kh2 enemies: %08x (partner)'s motion %d strikes (atkp %d)", e.days, a.motion.id,
                       int(std::int16_t(range.params[0])));
    }
    (void)f;
}
namespace {

// One of a party member's shapes of that type where it stands now (KH2's world: a sphere's centre, a cylinder's foot),
// false when it has none (its groups on)
bool member_shape(const Enemy &m, int type, Vec3 &at, const Shape **shape = nullptr, std::size_t nth = 0)
{
    if (!m.actor || m.pose.empty())
        return false;
    KhdaysKh2Shell s{};
    std::copy(m.place, m.place + 3, s.place);
    const std::set<int> on = groups_on(m);
    std::size_t n = 0;
    for (const Shape &sh : m.kind->shapes) {
        if ((sh.kind & 0xff) != type || on.count(sh.group) == 0 || sh.radius <= 0.0F || n++ != nth)
            continue;
        float d[3];
        days_point(m, s, sh.bone, sh.at, d);
        at = to_kh2(d);
        if (shape != nullptr)
            *shape = &sh;
        return true;
    }
    return false;
}
} // namespace

// A party member's collision of KH2's `type` (kTypeObj, kTypeHit) reached by an attack's sphere, or its cylinder `up`
// high from it (its centre in KH2's world: its foot, y down): YS::COLLISION::check -- a shape on a bone a sphere there
// (its height 0), one on none a cylinder its height up from its place
bool member_reached(const Enemy &m, const Vec3 &centre, float radius, float up, int type)
{
    Vec3 c{};
    const Shape *sh = nullptr;
    for (std::size_t nth = 0; member_shape(m, type, c, &sh, nth); ++nth) {
        const float top = c[1] - sh->height; // (KH2's y down)
        const float dx = centre[0] - c[0], dz = centre[2] - c[2];
        const float gap = std::max(0.0F, std::max(top, centre[1] - up) - std::min(c[1], centre[1]));
        if (std::sqrt(dx * dx + dz * dz + gap * gap) < radius + sh->radius)
            return true;
    }
    return false;
}
namespace {

// KH2's damage of an enemy's attack on a party member (YS::DAMAGE::set -> SHEET::calc_damage for an attacker with no
// sheet, from the current level's parameters: the attacker's battle entry's level). A cure's kinds: 5 its most health
// x the level's strength x the power over 10000, 6 strength x power over 100. The others: the level's strength less
// the member's defence (lvup at that level: KH2 takes Sora's, whom the port has not) kept to the level's most (and
// 99999, the sheet's) and its least (and 1), x the power x the member's element rates (100 and 100: no armour) over a
// million, rounded up; then its difficulty's (SHEET::adjust_game_level, the player's side: Beginner 50%, Proud and
// Critical 200% -- Days's story difficulty). The rest of calc_damage leaves an enemy's blow on him as it is: no
// ability of his, no combo of the attacker's, the debug configuration's rates 1
int member_damage(const Enemy &by, const Friend &f, const AttackParam &ap)
{
    const std::optional<LevelParam> lv = level_param(by.battle_level);
    if (!lv)
        return 0;
    if (ap.kind == 5)
        return int((std::int64_t(f.hp_most) * lv->strength * ap.power + 9999) / 10000);
    if (ap.kind == 6)
        return int((std::int64_t(lv->strength) * ap.power + 99) / 100);
    int v = lv->strength - character_defence(f.character, by.battle_level);
    v = std::min(v, std::min(lv->most, 99999));
    v = std::max(v, std::max(lv->least, 1));
    std::int64_t damage = (std::int64_t(v) * ap.power * 100 * 100 + 999999) / 1000000;
    if (damage > 0) {
        const int d = khdays_kh2_shell_difficulty();
        const int percent = d == 0 ? 50 : d >= 2 ? 200 : 100;
        damage = (damage * percent + 99) / 100;
    }
    return int(std::min<std::int64_t>(damage, 0x7fffffff));
}

// Health given to a party member (YS::PARTY::_OVR_add_hp -> BTLOBJ::_OVR_add_hp): knocked out, it gets up
// (YS::FRIEND::stun_recov: its getting up, motion 55, from its lying, 54; its idle after) with the health given x
// sstm + 0x54 (a half); else its health grows, to its most
void member_add_hp(Friend &f, Enemy &m, int amount)
{
    if (amount <= 0 || f.hp_most <= 0)
        return;
    if (f.knocked) {
        f.knocked = false;
        Actor &a = *m.actor;
        if (a.motion.id == 54 && a.play(55, 0.0F, 0.0F)) {
            a.motion.queued.clear();
            a.motion.queued.push_back({0, 0.0F});
            f.hurt_motion = 55;
            a.damage_motion = true;
        }
        amount = int(float(amount) * the_field().system_value(0x54, 0.5F));
    }
    f.hp = std::min(f.hp_most, f.hp + amount);
    if (tracing())
        khdays_log("kh2 enemies: %08x (partner) gets %d health: %d of %d", m.days, amount, f.hp, f.hp_most);
}

// YS::FRIEND::exec_stun (its pre_exec, a tick): knocked out, its time runs out -- 8 times as fast outside a battle
// (sstm + 0x5c: STATUS::GetMode 0) -- and it gets up with its most health x sstm + 0x58 (member_add_hp halves it
// again); lying (motion 54) as soon as it may move (not in its damage reaction, not in the air, not held by a script
// nor playing another's set's motion). Whether it lies
bool member_stun(Friend &f, Enemy &e)
{
    Actor &a = *e.actor;
    if (f.knocked) {
        f.knocked_for -= the_field().in_battle() ? 1.0F : the_field().system_value(0x5c, 8.0F);
        if (f.knocked_for <= 0.0F)
            member_add_hp(f, e, int(float(f.hp_most) * the_field().system_value(0x58, 0.5F)));
    }
    // knocked out and free (OBJ::can_control: no damage reaction of its, not attacking; not in the air, not held by a
    // script nor playing another's set's motion): its stars started (ryj::PAX::start, the system effects' 14) and its
    // lying asked (MOTION::change(0x36): nothing when its set has no 54 -- this Riku's -- and its AI goes on); else its
    // stars closed (EFFECT::loop_end) once its motion is not 54
    const bool free =
        f.knocked && !member_hurting(f, e) && !f.attacking && !a.jump.on && !a.hooked() && e.motions->capture_id() < 0;
    const auto stars = std::find_if(e.fx.begin(), e.fx.end(),
                                    [&f](const Enemy::Running &x) { return f.stars != 0 && x.handle == f.stars; });
    if (free) {
        if (stars == e.fx.end())
            f.stars = cast(e, 14, 0, false, &system_effects());
        if (a.motion.id != 54 && a.play(54, 0.0F, 0.0F)) {
            a.motion.queued.clear();
            a.damage_motion = true;
        }
    } else if (a.motion.id != 54 && f.stars != 0) {
        if (stars != e.fx.end())
            stars->fx->stop();
        f.stars = 0;
    }
    // its stars where its stun collision is (YS::EFFECT::pos: its TYPE_STUN shape's place, each tick)
    if (const auto r = std::find_if(e.fx.begin(), e.fx.end(),
                                    [&f](const Enemy::Running &x) { return f.stars != 0 && x.handle == f.stars; });
        r != e.fx.end()) {
        Vec3 at{};
        if (member_shape(e, kTypeStun, at)) {
            r->in_world = true;
            r->at = at;
        }
    }
    return f.knocked;
}

// Its damage reaction's motion still playing (YS::BTLOBJ's damage motions: the reaction's, its landing's after a
// blow) -- its states under priority 300 held meanwhile, as KH2's ACT holds an object's in a damage motion
bool member_hurting(Friend &f, Enemy &e)
{
    Actor &a = *e.actor;
    if (f.hurt_motion < 0)
        return false;
    const bool landing = a.motion.id == f.hurt_motion + 1 && (f.hurt_motion == 16 || f.hurt_motion == 18);
    if ((a.motion.id == f.hurt_motion || landing || a.jump.on) && !(a.motion.ended && !a.jump.on)) {
        a.damage_motion = true;
        return true;
    }
    f.hurt_motion = -1;
    a.damage_motion = f.knocked && a.motion.id == 54;
    return false;
}

// BTLOBJ::_OVR_exec_damage_reaction on a party member, by the reaction's kind (DAMAGE::GetReactionType): 1 a slide,
// 2 a small hit on the spot (10 from the front, 12 from behind, unturned), 3 a small one (the same and its slide; in
// the air 14 and 15, turned), 4 a blow (16 from the front facing it, 18 from behind turned away; thrown up by the
// attack's + 0xc and along by its + 0xe -- sstm + 0x28 and + 0x24 when both are 0 --, landing in the motion after),
// 14 a large one (23 from the front facing it, 24 from behind, its slide); the slides as far as the attack's hitback
// (+ 0xc; at 0x7fff sstm + 0x30, + 0x34 for a small one; half for reactions 12 and 13), a tenth of it a tick. Kind 5
// (a tornado) is not played. Its idle after each motion
void member_reaction(Enemy &by, Enemy &m, Friend &f, int reaction, const AttackParam &ap)
{
    Actor &a = *m.actor;
    int kind = reaction;
    if (reaction == 1 || reaction == 12)
        kind = 1;
    else if (reaction == 3 || reaction == 6 || reaction == 8 || reaction == 10 || reaction == 13 || reaction == 16 ||
             reaction == 18)
        kind = 3;
    else if (reaction == 4 || reaction == 7 || reaction == 9 || reaction == 11 || reaction == 15 || reaction == 17)
        kind = 4;
    if (kind <= 0 || kind == 5)
        return;
    // the hit's way (DAMAGE::set_dir): from the attacker to it along the ground
    const Vec3 to = a.place(), from = by.actor->place();
    Vec3 way = {to[0] - from[0], 0.0F, to[2] - from[2]};
    const float flat = std::sqrt(way[0] * way[0] + way[2] * way[2]);
    if (flat > 0.0F)
        way = {way[0] / flat, 0.0F, way[2] / flat};
    else
        way = facing_of(by.actor->yaw());
    const Vec3 face = facing_of(a.yaw());
    const bool back = way[0] * face[0] + way[2] * face[2] >= 0.0F; // its facing along the hit's: struck from behind
    const auto turn = [&] { a.set_yaw(back ? std::atan2(way[0], way[2]) : std::atan2(-way[0], -way[2])); };
    const auto own = [&a](int motion) { return a.motions != nullptr && !a.motions->has(motion) ? motion ^ 1 : motion; };
    const auto start = [&](int motion) {
        if (!a.play(motion, 0.0F, 0.0F))
            return;
        a.motion.queued.clear();
        a.motion.queued.push_back({0, 0.0F});
        f.hurt_motion = motion;
        a.damage_motion = true;
    };
    const auto slide = [&] {
        float cm = float(ap.hitback);
        if (ap.hitback == 0x7fff)
            cm = the_field().system_value(kind == 3 ? 0x34 : 0x30, 200.0F);
        if (reaction == 12 || reaction == 13)
            cm *= 0.5F;
        a.slide = way;
        a.slide_speed = cm * 0.1F;
    };
    const bool air = a.jump.on || (a.u32(field::kFlags) & bit::kInAir) != 0;
    if (kind == 1) {
        slide();
    } else if (kind == 2 || (kind == 3 && !air)) {
        start(own(back ? 12 : 10));
        if (kind == 3)
            slide();
    } else if (kind == 3) {
        turn();
        start(own(back ? 15 : 14));
    } else if (kind == 4) {
        float up = float(ap.hitback), along = float(ap.blow_along);
        if (ap.hitback == 0 && ap.blow_along == 0) {
            up = the_field().system_value(0x24, 0.0F);
            along = the_field().system_value(0x28, 0.0F);
        }
        turn();
        start(back ? 18 : 16);
        thrown(a, way[0], way[2], along, -up);
    } else if (kind == 14) {
        turn();
        start(back ? 24 : 23);
        slide();
    }
}
} // namespace

// An enemy's attack (its atkp) on a party member (kn::Friend::_OVR_exec_damage -> YS::FRIEND -> YS::PARTY ->
// BTLOBJ::add_damage, _OVR_exec_damage_reaction): none while it lies knocked out (but a cure's), its damage
// (member_damage) off its health -- a cure's on it --, its pinch told to its script on reaching a quarter (signal 9:
// trigger 10), its target the attacker; its reaction (member_reaction, after its damage patterns) unless a script
// holds it; at no health it is knocked out (FRIEND::_OVR__dead: its script told, signal 2; its time the spec's stun
// time, PREF_PARTY + 0x18, x its ability's 1)
void member_hurt(Enemy &by, Enemy &m, Friend &f, int atkp)
{
    if (f.hp_most <= 0 || !m.actor)
        return;
    Actor &a = *m.actor;
    const AttackParam &ap = attack_param(atkp);
    const bool cure = ap.kind == 5 || ap.kind == 6;
    if (f.knocked && !cure)
        return;
    const int damage = member_damage(by, f, ap);
    if (cure) {
        member_add_hp(f, m, damage);
        return;
    }
    const bool pinch = f.hp <= f.hp_most / 4;
    f.hp = std::max(0, f.hp - damage);
    if (!pinch && f.hp <= f.hp_most / 4) {
        const ai::Value told[2] = {ai::Value::of_int(9), ai::Value::of_int(0)};
        a.event(10, told);
    }
    if (!by.dying && !by.released && enemies.count(by.days) != 0)
        f.target = by.days;
    const int first = ap.reaction == 255 ? 0 : ap.reaction;
    const int reaction = hit_pattern_reaction(a, first, true);
    if (!a.hooked() && m.motions->capture_id() < 0)
        member_reaction(by, m, f, reaction, ap);
    if (tracing())
        khdays_log("kh2 enemies: %08x's attack (atkp %d, power %d, level %d) hits %08x (partner): %d damage, %d of %d "
                   "health, reaction %d",
                   by.days, atkp, ap.power, by.battle_level, m.days, damage, f.hp, f.hp_most, reaction);
    if (f.hp == 0) {
        f.knocked = true;
        f.knocked_for = f.spec.stun_time;
        const ai::Value told[2] = {ai::Value::of_int(2), ai::Value::of_int(0)};
        a.event(10, told);
        if (tracing())
            khdays_log("kh2 enemies: %08x (partner) knocked out for %.0f ticks", m.days, double(f.knocked_for));
    }
}

} // namespace khdays::kh2_enemies::shell
