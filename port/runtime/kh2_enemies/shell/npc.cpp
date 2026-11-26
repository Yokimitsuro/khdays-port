// KH2's battle NPCs (YS::BTLNPC, object entries of kind 9: the Land of Dragons' palace's gates, the Hydra's Megara):
// allies with a battle sheet of their own (2:9), which KH2's enemies' attacks strike as they strike the player.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

// The hit as its script reads it (YS::DAMAGE, as khdays_kh2_enemies_hit makes a Days enemy's), its attack's record
std::uint32_t npc_hit(Enemy &by, Enemy &npc, int atkp, int first, int reaction, int damage)
{
    static std::unique_ptr<Object> hit;
    static std::uint32_t params = 0;
    if (!hit)
        hit = std::make_unique<Object>(mod().memory, "a KH2 attack's hit on a battle NPC");
    if (params == 0)
        params = mod().memory.add(attack_record::kSize, "the attack of a hit on a battle NPC");
    const AttackParam &p = attack_param(atkp);
    std::uint8_t record[attack_record::kSize] = {};
    const auto put16 = [&record](std::uint32_t at, int v) {
        record[at] = std::uint8_t(v);
        record[at + 1] = std::uint8_t(v >> 8);
    };
    put16(attack_record::kNumber, atkp);
    record[attack_record::kKind] = std::uint8_t(p.kind);
    put16(attack_record::kPower, p.power);
    record[attack_record::kElement] = std::uint8_t(p.element);
    put16(attack_record::kHitback, p.hitback);
    put16(attack_record::kBlowAlong, p.blow_along);
    mod().memory.write(params, record, sizeof record);
    const Vec3 w = hit_way(*by.actor, *npc.actor);
    const float way[4] = {w[0], w[1], w[2], 0.0F};
    mod().memory.write(hit->address() + hit_record::kWay, way, sizeof way);
    hit->set_u32(hit_record::kAttacker, by.actor->address());
    hit->set_u32(hit_record::kTarget, npc.actor->address());
    hit->set_u32(hit_record::kFlag, 0);
    hit->set_u32(hit_record::kAttack, motion_attack(by, atkp));
    hit->set_u32(hit_record::kParams, params);
    hit->set_u32(hit_record::kKind, hit_record::reactions(reaction, first));
    hit->set_u32(hit_record::kDamage, std::uint32_t(damage));
    return hit->address();
}

// KH2's damage of an enemy's attack on it (YS::SHEET::calc_damage): the attacker's strength (none for a magic's) less
// its own defence, each its level's, kept between the least and the most its level and its battle row allow, times
// the attack's power and its rates for the attack's element and for every hit, rounded up
int npc_damage(const Enemy &by, const Enemy &npc, const AttackParam &ap)
{
    const std::optional<LevelParam> lv = level_param(battle_level(by)), own = level_param(battle_level(npc));
    if (!lv || !own)
        return 0;
    const std::int64_t strength = ap.magic ? 0 : lv->strength, whole = npc.health_most;
    if (const int cure = cure_amount(ap, whole, strength); cure >= 0)
        return cure;
    const BattleRow &row = battle_row(npc.battle_number);
    const std::int64_t most = std::min<std::int64_t>(lv->most, rounded_up(whole * row.most, kPerTenThousand));
    const std::int64_t least = std::max<std::int64_t>(lv->least, rounded_up(whole * row.least, kPerTenThousand));
    const std::int64_t value = std::max(std::min(strength - own->defence, most), least);
    const int rate = row.rates[std::size_t(std::clamp(ap.element, 0, kElements - 1))];
    const int every = row.rates[kEveryElement];
    const std::int64_t rates = std::int64_t(kPercent) * kPercent * kPercent; // power, rate and every: percent each
    return int(std::min<std::int64_t>(rounded_up(value * ap.power * rate * every, rates), 0x7fffffff));
}
} // namespace

// The level a KH2 enemy's battle sheet is of (SHEET::set): its battle entry's, else the area's -- the Days mission's
int battle_level(const Enemy &e)
{
    constexpr int kLeast = 1, kMost = 99;
    return std::clamp(e.battle_level > 0 ? e.battle_level : khdays_kh2_shell_mission_level(), kLeast, kMost);
}

// The battle NPCs its motion's attack of `team` reaches (its group's spheres on their collision's TYPE_HIT shapes),
// each once a range
void strike_npcs(Enemy &e, const KhdaysKh2Shell &s, std::size_t range, int group, int atkp, int team)
{
    each_npc(team, [&](Enemy &npc) {
        if (&npc == &e || e.struck[range].count(npc.days) != 0)
            return;
        for (const Shape &sh : e.kind->shapes) {
            if (sh.group != group || (sh.kind & 0xff) != kTypeAttack)
                continue;
            float c[3];
            days_point(e, s, sh.bone, sh.at, c);
            if (!member_reached(npc, to_kh2(c), sh.radius, 0.0F, kTypeHit))
                continue;
            e.struck[range].insert(npc.days);
            npc_hurt(e, npc, attack_key(*e.actor, atkp));
            return;
        }
    });
}

// An attack on a battle NPC (YS::PIERROT::_OVR_exec_damage): its damage off its health (kept to its least, one over
// while it withstands; a cure's on it), its state's routine for a hit, its trigger 6, its reaction unless its script
// carried it out (2:0), then its mission told -- only its mission while its pattern kMissionOnly is on
void npc_hurt(Enemy &by, Enemy &npc, int atkp)
{
    Actor &a = *npc.actor;
    const AttackParam &ap = attack_param(atkp);
    const std::uint32_t sheet = a.u32(field::kSheet);
    const bool cure = attack_record::is_cure(ap.kind);
    const int damage = npc_damage(by, npc, ap);
    const int first = ap.reaction == kNoReaction ? 0 : ap.reaction;
    if (pattern_on(a, pattern::kMissionOnly)) {
        mission_damage(a.address(), damage, by.actor->address(), ap.kind);
        return;
    }
    if (npc.dying || (a.u32(field::kFade) & bit::kDeadMark) != 0)
        return;
    a.set_u32(field::kAttacker, by.actor->address());
    const std::int64_t least = std::int32_t(mod().memory.u32(sheet + sheet::kLeast));
    std::int64_t taken = damage;
    if (!cure && npc.health - taken <= least && pattern_on(a, pattern::kWithstand))
        taken = std::max<std::int64_t>(0, npc.health - least - 1);
    npc.health = cure ? std::min(npc.health + damage, npc.health_most) : std::max(npc.health - taken, least);
    mod().memory.set_u32(sheet + sheet::kHealth, std::uint32_t(npc.health));
    if (npc.health <= 0) { // BTLOBJ::_OVR__dead, then its "dead" (PIERROT's)
        npc.dying = true;
        a.set_bits(field::kFade, bit::kDeadMark, true);
        a.states().unlock();
        if (!a.states().enter(a, "dead"))
            npc.released = true;
    }
    const int reaction = cure ? 0 : hit_pattern_reaction(a, first, false);
    const std::uint32_t hit = npc_hit(by, npc, atkp, first, reaction, damage);
    const ai::Value packet = ai::Value::of_address(hit);
    a.states().event(a, kEventStruck, {&packet, 1});
    a.event(kTriggerDamage, {&packet, 1});
    if (!npc.dying)
        the_field().damage_reaction(a, hit);
    mission_damage(a.address(), damage, by.actor->address(), ap.kind);
    if (tracing())
        khdays_log("kh2 enemies: %08x's attack (atkp %d, power %d, level %d) hits %08x (%s): %d damage, %lld of %lld "
                   "health, reaction %d",
                   by.days, atkp & 0xffff, ap.power, battle_level(by), npc.days, npc.kind->name.c_str(), damage,
                   static_cast<long long>(npc.health), static_cast<long long>(npc.health_most), reaction);
}

// Its damage motion over (its blow's landing too): its states go on
void npc_tick(Enemy &npc)
{
    if (npc.hurt_motion < 0)
        return;
    Actor &a = *npc.actor;
    const bool landing = a.motion.id == npc.hurt_motion + 1 &&
                         (npc.hurt_motion == damage_motion::kBlowFront || npc.hurt_motion == damage_motion::kBlowBack);
    if ((a.motion.id == npc.hurt_motion || landing || a.jump.on) && !(a.motion.ended && !a.jump.on))
        return;
    npc.hurt_motion = -1;
    a.damage_motion = false;
}

} // namespace khdays::kh2_enemies::shell
