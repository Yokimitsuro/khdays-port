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
        params = mod().memory.add(48, "the attack of a hit on a battle NPC");
    const AttackParam &p = attack_param(atkp);
    std::uint8_t record[48] = {};
    record[2] = std::uint8_t(atkp);
    record[3] = std::uint8_t(atkp >> 8);
    record[4] = std::uint8_t(p.kind);
    record[6] = std::uint8_t(p.power);
    record[7] = std::uint8_t(p.power >> 8);
    record[9] = std::uint8_t(p.element);
    record[0xc] = std::uint8_t(p.hitback);
    record[0xd] = std::uint8_t(p.hitback >> 8);
    record[0xe] = std::uint8_t(p.blow_along);
    record[0xf] = std::uint8_t(p.blow_along >> 8);
    mod().memory.write(params, record, sizeof record);
    const Vec3 w = hit_way(*by.actor, *npc.actor);
    const float way[4] = {w[0], w[1], w[2], 0.0F};
    mod().memory.write(hit->address(), way, sizeof way);
    hit->set_u32(0x10, by.actor->address());
    hit->set_u32(0x14, npc.actor->address());
    hit->set_u32(0x18, 0);
    hit->set_u32(0x1c, motion_attack(by, atkp));
    hit->set_u32(0x20, params);
    hit->set_u32(0x24, std::uint32_t(reaction & 0xff) << 16 | std::uint32_t(first & 0xff) << 24);
    hit->set_u32(0x28, std::uint32_t(damage));
    return hit->address();
}

// KH2's damage of an enemy's attack on it (YS::SHEET::calc_damage): the attacker's level's strength (none for a
// magic's) less its own level's defence, kept to the level's most and its own (its battle entry's share of its health)
// and to the least the same way, x the power x its rates over a million, rounded up; a cure's kinds 5 and 6 as a party
// member's (member_damage). The game level changes neither side's (SHEET::adjust_game_level: no PARTY's hit nor its
// own)
int npc_damage(const Enemy &by, const Enemy &npc, const AttackParam &ap)
{
    const std::optional<LevelParam> lv = level_param(battle_level(by)), own = level_param(battle_level(npc));
    if (!lv || !own)
        return 0;
    const std::int64_t strength = ap.magic ? 0 : lv->strength, whole = npc.health_most;
    if (ap.kind == 5)
        return int((whole * strength * ap.power + 9999) / 10000);
    if (ap.kind == 6)
        return int((strength * ap.power + 99) / 100);
    const BattleRow &row = battle_row(npc.battle_number);
    const std::int64_t most = std::min<std::int64_t>(lv->most, (whole * row.most + 9999) / 10000);
    const std::int64_t least = std::max<std::int64_t>(lv->least, (whole * row.least + 9999) / 10000);
    const std::int64_t value = std::max(std::min(strength - own->defence, most), least);
    const int rate = row.rates[std::size_t(std::clamp(ap.element, 0, 5))], every = row.rates[6];
    return int(std::min<std::int64_t>((value * ap.power * rate * every + 999999) / 1000000, 0x7fffffff));
}
} // namespace

// The level a KH2 enemy's battle sheet is of (SHEET::set: its battle entry's + 2, else YS::LEVELPARAM's current one,
// its area's): the mission's for the latter (Days's, its author's), 1 to 99
int battle_level(const Enemy &e)
{
    return std::clamp(e.battle_level > 0 ? e.battle_level : khdays_kh2_shell_mission_level(), 1, 99);
}

// The battle NPCs its motion's attack of `team` reaches (its group's spheres on their collision's TYPE_HIT shapes),
// each once a range
void strike_npcs(Enemy &e, const KhdaysKh2Shell &s, std::size_t range, int group, int atkp, int team)
{
    each_npc(team, [&](Enemy &npc) {
        if (&npc == &e || e.struck[range].count(npc.days) != 0)
            return;
        for (const Shape &sh : e.kind->shapes) {
            if (sh.group != group || (sh.kind & 0xff) != 6)
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

// An attack on it (YS::PIERROT::_OVR_exec_damage, a battle NPC's): none once it is dead, its last attacker told (2:88),
// its damage off its health (BTLOBJ::add_damage: kept to its least, 1:231, one over it while its pattern 9 withstands;
// a cure's on it), its reaction as its patterns change it, its state's routine for a hit (ACT::damage: event 3), its
// trigger 6, its reaction unless the script carried it out (2:0), its mission told last (UNITSCRIPT::DamageCallback) --
// alone with its pattern 13 on
void npc_hurt(Enemy &by, Enemy &npc, int atkp)
{
    Actor &a = *npc.actor;
    const AttackParam &ap = attack_param(atkp);
    const std::uint32_t sheet = a.u32(0x5c0), on = (a.u32(0xd10) | a.u32(0xd14)) & ~a.u32(0xd18);
    const bool cure = ap.kind == 5 || ap.kind == 6;
    const int damage = npc_damage(by, npc, ap);
    const int first = ap.reaction == 255 ? 0 : ap.reaction;
    if ((on & 1u << 13) != 0) {
        mission_damage(a.address(), damage, by.actor->address(), ap.kind);
        return;
    }
    if (npc.dying || (a.u32(field::kFade) & bit::kDeadMark) != 0)
        return;
    a.set_u32(field::kAttacker, by.actor->address());
    const std::int64_t least = std::int32_t(mod().memory.u32(sheet + 8));
    std::int64_t taken = damage;
    if (!cure && npc.health - taken <= least && (on & 1u << 9) != 0)
        taken = std::max<std::int64_t>(0, npc.health - least - 1);
    npc.health = cure ? std::min(npc.health + damage, npc.health_most) : std::max(npc.health - taken, least);
    mod().memory.set_u32(sheet, std::uint32_t(npc.health));
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
    a.states().event(a, 3, {&packet, 1});
    a.event(6, {&packet, 1});
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
    const bool landing = a.motion.id == npc.hurt_motion + 1 && (npc.hurt_motion == 16 || npc.hurt_motion == 18);
    if ((a.motion.id == npc.hurt_motion || landing || a.jump.on) && !(a.motion.ended && !a.jump.on))
        return;
    npc.hurt_motion = -1;
    a.damage_motion = false;
}

} // namespace khdays::kh2_enemies::shell
