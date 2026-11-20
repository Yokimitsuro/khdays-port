// Final Xemnas's laser dome.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

// KH2's pick of one of n (ryj::LAST_XEMNUS_LASER's: its chance over (2^32 + n - 1) / n)
int laser_pick(int n)
{
    const std::uint64_t r = kh2_chance();
    return int(std::min<std::uint64_t>(r / ((0x100000000ull + std::uint64_t(n) - 1) / std::uint64_t(n)),
                                       std::uint64_t(n) - 1));
}

Enemy *dome_caster()
{
    if (!laser_dome)
        return nullptr;
    for (auto *list : {&enemies, &allies})
        if (const auto it = list->find(laser_dome->caster); it != list->end() && !it->second->released)
            return it->second.get();
    return nullptr;
}

// LAST_XEMNUS_LASER::setup: the rings, and the four effects on the caster (YS::OBJ::start_effect 14, 70, 69, 70)
void laser_setup(LaserDome &d, Enemy &caster)
{
    d.lasers.clear();
    float angle = 1.48353F, height = -700.0F;
    for (int ring = 0, count = 6; ring < 15; ++ring, count += 3, angle += -0.098902F, height += 46.6667F)
        for (int j = 0; j < count; ++j) {
            LaserDome::Laser l;
            const float a = float(j) * (6.28319F / float(count)); // kn::FMatrix::rotateY
            const Vec3 p = turned({0.0F, height, std::cos(angle) * 700.0F}, a);
            Vec3 w = {-p[0], -100.0F - p[1], -p[2]};
            const float n = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            w = {w[0] / n, w[1] / n, w[2] / n};
            l.start = l.at = p;
            l.start_way = l.way = w;
            d.lasers.push_back(l);
        }
    const std::array<int, 4> elements = {14, 70, 69, 70};
    for (std::size_t i = 0; i < elements.size() && i < d.fx.size(); ++i) {
        const std::uint32_t h = cast(caster, elements[i]);
        for (Enemy::Running &r : caster.fx)
            if (r.handle == h && h != 0)
                r.dome = int(i) + 1;
        d.fx[i] = h;
    }
    if (tracing())
        khdays_log("kh2 enemies: %08x's laser dome set up: %zu lasers, effects %08x %08x %08x %08x", caster.days,
                   d.lasers.size(), d.fx[0], d.fx[1], d.fx[2], d.fx[3]);
}

// a list's first free point (its alpha 0, which the effect clears as its sprite ends) taken for `at`
void dome_point(std::vector<kh2::EffectPoint> &list, const Vec3 &at, std::array<std::uint8_t, 4> bytes)
{
    for (kh2::EffectPoint &p : list)
        if (p.bytes[0] == 0) {
            p.place = {at[0], at[1], at[2]};
            p.bytes = bytes;
            return;
        }
}

// The dome's party member (LASER's FRIEND::Get(0): the first party member standing), none
Enemy *dome_member(Friend **f = nullptr)
{
    Enemy *found = nullptr;
    each_member([&](Enemy &m, Friend &x) {
        if (found == nullptr) {
            found = &m;
            if (f != nullptr)
                *f = &x;
        }
    });
    return found;
}

// The motion a dome's target plays in the caster's set (+ 0x154: a player's the caster's script gives him, the
// member's the one his set takes from it), -1 none
int dome_motion(Enemy &caster, Object *player, Enemy *member)
{
    if (member != nullptr)
        return member->motions->capture_id() >= 0 && member->actor->motion.id == member->motions->capture_id()
                   ? member->motions->capture_id()
                   : -1;
    if (player == nullptr)
        return -1;
    const auto h = held.find(days_object(*player));
    return h != held.end() && h->second.enemy == shell_of(caster) && !h->second.ended ? h->second.slot : -1;
}

// LASER::attack: the laser turned to its target's middle (OBJ::get_target_pos) -- the party member's while the
// player plays the dome's 270 or 271 (knocked about by it), the player's while the member does, else either by KH2's
// chance (its sign the player) -- flying. The player the caster's nearest; the member none when none stands (the
// player then)
void laser_fire(LaserDome &d, LaserDome::Laser &l, Enemy &caster)
{
    Vec3 target = l.at;
    Object *p = the_field().player(*caster.actor);
    Enemy *member = dome_member();
    const int mine = dome_motion(caster, p, nullptr), its = dome_motion(caster, nullptr, member);
    bool at_member = false;
    if (member != nullptr && p != nullptr) {
        if (mine == 270 || mine == 271)
            at_member = true;
        else if (its == 270 || its == 271)
            at_member = false;
        else
            at_member = (kh2_chance() & 0x80000000u) == 0;
    } else {
        at_member = member != nullptr;
    }
    Object *o = at_member ? static_cast<Object *>(member->actor.get()) : p;
    if (o != nullptr) {
        Vec3 aim{};
        target = the_field().aim_point(*o, aim) ? aim : o->place();
    }
    Vec3 w = {target[0] - l.at[0], target[1] - l.at[1], target[2] - l.at[2]};
    const float n = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (n > 0.0F)
        l.way = {w[0] / n, w[1] / n, w[2] / n};
    l.phase = 3;
    l.time = 0.0F;
    (void)d;
}

// LASER::check_obj_hit on each of Days's players: his body against the laser's 2 m sphere -- in a Reflect motion of
// his (the caster's set's 254 to 256, not past its loop) the laser bounces (a bounce point, sound 374065) -- then its
// 50 cm sphere: struck (atkp 1143 as the caster's attack, a hit point, sound 374047); either ends the laser
bool laser_hits(LaserDome &d, LaserDome::Laser &l, Enemy &caster)
{
    std::vector<KhdaysKh2Player> list(std::size_t(std::max(0, khdays_kh2_shell_players(shell_of(caster), nullptr, 0))));
    const int n = khdays_kh2_shell_players(shell_of(caster), list.data(), int(list.size()));
    float head[3];
    to_days(l.at, head);
    for (int i = 0; i < n && i < int(list.size()); ++i) {
        const KhdaysKh2Player &p = list[std::size_t(i)];
        const float low = p.place[1] + p.radius, high = std::max(low, p.place[1] + kPlayerHeight - p.radius);
        const float dx = p.place[0] - head[0], dy = std::clamp(head[1], low, high) - head[1], dz = p.place[2] - head[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < 200.0F / kKh2PerDays + p.radius) {
            const auto h = held.find(p.object);
            if (h != held.end() && h->second.enemy == shell_of(caster) && h->second.slot >= 254 &&
                h->second.slot <= 256 && !h->second.ended) {
                dome_point(d.bounces, l.at, {0x80, 0, 0, 0x1e});
                sound(caster, 374065);
                l.phase = 6;
                return true;
            }
        }
        if (dist < 50.0F / kKh2PerDays + p.radius) {
            float push[3];
            hitback_push(1143, dx, dz, push);
            const auto owner = enemies.find(shell_of(caster));
            if (owner != enemies.end() && hit_player(*owner->second, p.object, 1143, push))
                landed(caster, p.object, 1143);
            dome_point(d.hits, l.at, {0x80, 0, std::uint8_t(kh2_chance() >> 24), 0x1e});
            sound(caster, 374047);
            l.phase = 6;
            return true;
        }
    }
    // then the party member's (check_obj_hit(FRIEND::Get(0))): his body against the 2 m sphere in a Reflect of his --
    // the caster's set's 254 to 256 his set takes, not yet in its loop -- bounces it (374066: no player's flag,
    // + 0x588 bit 0x80), against the 50 cm one strikes him (LASER::strike: atkp 1143, the caster's)
    // (knocked out he stops it all the same, unhurt: PARTY::_OVR_exec_damage takes nothing then)
    Friend *f = nullptr;
    Enemy *m = dome_member(&f);
    if (m == nullptr)
        return false;
    const int motion = dome_motion(caster, nullptr, m);
    const float loop = motion >= 0 ? m->actor->motions->loop_from(motion) : 0.0F;
    const bool in_loop = loop > 0.0F && loop < m->actor->motions->length(motion) && m->actor->motion.time >= loop;
    if (motion >= 254 && motion <= 256 && !in_loop && member_reached(*m, l.at, 200.0F, 0.0F, kTypeObj)) {
        dome_point(d.bounces, l.at, {0x80, 0, 0, 0x1e});
        sound(caster, 374066);
        l.phase = 6;
        return true;
    }
    if (!member_reached(*m, l.at, 50.0F, 0.0F, kTypeObj))
        return false;
    member_hurt(caster, *m, *f, 1143);
    dome_point(d.hits, l.at, {0x80, 0, std::uint8_t(kh2_chance() >> 24), 0x1e});
    sound(caster, 374047);
    l.phase = 6;
    return true;
}

// LASER::exec
void laser_step(LaserDome &d, LaserDome::Laser &l, Enemy &caster)
{
    switch (l.phase) {
    case 1: // fading in, 10 an tick up to 128
        if (l.bytes[0] >= 0x80) {
            l.phase = 2;
            break;
        }
        l.bytes = {std::uint8_t(std::min(0x80, l.bytes[0] + 10)), 0, 0, 10};
        break;
    case 3:
    case 4: { // LASER::move: 40 cm a tick along its way
        for (int c = 0; c < 3; ++c)
            l.at[std::size_t(c)] += l.way[std::size_t(c)] * 40.0F;
        if (l.phase == 3 && laser_hits(d, l, caster))
            break;
        if (l.time >= (l.phase == 3 ? 60.0F : 30.0F)) {
            l.phase = 6;
            l.time = 0.0F;
        } else {
            l.time += 1.0F;
        }
        break;
    }
    case 5: // burst: hidden
        l.bytes = {};
        l.phase = 0;
        break;
    case 6: // LASER::exec_end: back on the dome, hidden, fading in again while the dome fires
        l.at = l.start;
        l.way = l.start_way;
        l.bytes = {};
        l.phase = d.phase == 4 || d.phase == 5 ? 1 : 0;
        break;
    default: break;
    }
}
} // namespace

// LAST_XEMNUS_LASER::exec, a KH2 tick (its task's priority, 20500, before the effects')
void laser_tick()
{
    if (!laser_dome)
        return;
    LaserDome &d = *laser_dome;
    Enemy *caster = dome_caster();
    if (caster == nullptr)
        d.phase = 7;
    switch (d.phase) {
    case 0:
        laser_setup(d, *caster);
        d.phase = 1;
        d.time = d.total = 0.0F;
        d.left = int(d.lasers.size());
        break;
    case 1:
        if (d.total < 140.0F) {
            d.time = 25.0F;
            break;
        }
        while (d.time >= 0.25F) {
            if (d.left <= 0) {
                d.phase = 2;
                d.time = d.total = 0.0F;
                break;
            }
            d.lasers[std::size_t(--d.left)].phase = 1;
            d.time -= 0.25F;
        }
        break;
    case 3:
        if (d.time >= 20.0F) {
            d.phase = 4;
            d.interval = d.burst_interval = 10.0F;
            d.burst_count = 0.0F;
            d.time = d.total = 0.0F;
        }
        break;
    case 4: { // exec_attack
        if (d.hum == 0)
            d.hum = the_field().sound(*caster->actor, 374040, true);
        const int n = int(d.lasers.size());
        while (d.interval <= d.time) {
            for (int k = 0; k < n; ++k) {
                LaserDome::Laser &l = d.lasers[std::size_t(laser_pick(n))];
                if (l.phase == 2) {
                    laser_fire(d, l, *caster);
                    break;
                }
            }
            d.time -= d.interval;
        }
        d.interval = std::max(d.interval - 0.04F, d.total >= 900.0F ? 0.15F : 0.5F);
        if (d.total > 1560.0F) {
            while (d.burst_interval <= d.burst_count) {
                for (int k = 0; k < n; ++k) {
                    LaserDome::Laser &l = d.lasers[std::size_t(laser_pick(n))];
                    if (l.phase >= 1 && l.phase <= 3) { // LASER::explosion
                        dome_point(d.bursts, l.at, {0x80, 0, std::uint8_t(kh2_chance() >> 24), 0x3c});
                        l.phase = 5;
                        l.time = 0.0F;
                        break;
                    }
                }
                d.burst_count -= d.burst_interval;
            }
            d.burst_count += 1.0F;
            d.burst_interval = std::clamp(d.burst_interval - 0.05F, 0.01F, 10.0F);
        }
        break;
    }
    case 5:
        if (std::all_of(d.lasers.begin(), d.lasers.end(), [](const LaserDome::Laser &l) { return l.phase == 2; })) {
            for (LaserDome::Laser &l : d.lasers)
                laser_fire(d, l, *caster);
            d.time = d.interval = 0.0F;
            d.phase = 6;
        }
        break;
    default: break;
    }
    if (d.phase == 7) { // its task's end (ExecTaskFinalizer): the sound faded over 2 s, the effects gone
        if (d.hum != 0)
            khdays_sound_shot_fade(d.hum, 2.0F);
        if (caster != nullptr)
            for (std::size_t i = caster->fx.size(); i-- > 0;)
                if (caster->fx[i].dome != 0)
                    caster->fx.erase(caster->fx.begin() + std::ptrdiff_t(i));
        if (tracing())
            khdays_log("kh2 enemies: the laser dome is over");
        laser_dome.reset();
        return;
    }
    for (LaserDome::Laser &l : d.lasers)
        laser_step(d, l, *caster);
    d.time += 1.0F;
    d.total += 1.0F;
}
namespace {

// The dome's lists given to one of its effects in the caster's effects' space (KH2's world from the caster, turned
// back by its turn, its y KH2's -- down -- as an effect's place in the world is given, Running::in_world) before it
// runs, and what it wrote back taken after
Vec3 to_effect_space(const Enemy &e, const Vec3 &w, bool way)
{
    const Vec3 from = e.actor->place();
    const Vec3 v = way ? w : Vec3{w[0] - from[0], w[1] - from[1], w[2] - from[2]};
    return turned(legacy(v), -days_turn(e.actor->yaw()));
}
} // namespace

void give_dome(const Enemy &e, Enemy::Running &r)
{
    if (!laser_dome)
        return;
    LaserDome &d = *laser_dome;
    if (r.dome == 1) {
        std::vector<kh2::EffectGameMatrix> m(d.lasers.size());
        for (std::size_t i = 0; i < d.lasers.size(); ++i) {
            const LaserDome::Laser &l = d.lasers[i];
            const Vec3 x = to_effect_space(e, {1.0F, 0.0F, 0.0F}, true),
                       y = to_effect_space(e, {0.0F, 1.0F, 0.0F}, true);
            const Vec3 z = to_effect_space(e, l.way, true), p = to_effect_space(e, l.at, false);
            m[i].matrix = {x[0], x[1], x[2], 0, y[0], y[1], y[2], 0, z[0], z[1], z[2], 0, p[0], p[1], p[2], 1};
            m[i].plain = false;
            m[i].bytes = l.bytes;
        }
        r.fx->set_matrices(std::move(m));
        return;
    }
    const std::vector<kh2::EffectPoint> &list = r.dome == 2 ? d.hits : r.dome == 3 ? d.bounces : d.bursts;
    std::vector<kh2::EffectPoint> local = list;
    for (kh2::EffectPoint &p : local) {
        const Vec3 at = to_effect_space(e, {p.place[0], p.place[1], p.place[2]}, false);
        p.place = {at[0], at[1], at[2]};
    }
    r.fx->set_points(std::move(local));
}

void take_dome(Enemy::Running &r)
{
    if (!laser_dome)
        return;
    LaserDome &d = *laser_dome;
    if (r.dome == 1) {
        const std::vector<kh2::EffectGameMatrix> &m = r.fx->matrices();
        for (std::size_t i = 0; i < m.size() && i < d.lasers.size(); ++i)
            d.lasers[i].bytes = m[i].bytes;
        return;
    }
    std::vector<kh2::EffectPoint> &list = r.dome == 2 ? d.hits : r.dome == 3 ? d.bounces : d.bursts;
    const std::vector<kh2::EffectPoint> &back = r.fx->points();
    for (std::size_t i = 0; i < back.size() && i < list.size(); ++i)
        list[i].bytes = back[i].bytes;
}

} // namespace khdays::kh2_enemies::shell
