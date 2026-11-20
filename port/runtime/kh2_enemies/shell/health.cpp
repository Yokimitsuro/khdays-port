// Health: KH2's numbers shown in Days's fields, the hits an enemy takes.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {
// Days's HP fields are signed 16 bits: the most of a KH2 enemy's health's share they show
constexpr int kDaysShownMost = 32767;

int kept_damage(void *actor, int amount, int kinds);

// Days's hit kinds (the packet's high flags, a bit a kind: Ov022_SendSweepHit's 1 << the attack's kind) as KH2's
// elements (atkp's + 9: 0 physical, 1 fire, 2 blizzard, 3 thunder). Ov107_AiState_ApplyHit names four by the statuses
// they give: bit 7 burning (Fire: Roxas's Fire seen `08 00 80 00`), bit 3 frozen (Blizzard), bit 11 jolted (Thunder),
// bit 2 tossed (Aero: no element of KH2's, physical); his attack's 0x4000 and the rest physical
int kh2_element_of(unsigned kinds)
{
    if ((kinds & 0x80u) != 0)
        return 1;
    if ((kinds & 0x8u) != 0)
        return 2;
    if ((kinds & 0x800u) != 0)
        return 3;
    return 0;
}
} // namespace

const BattleRow &battle_row(int entry)
{
    static std::map<int, BattleRow> rows;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> file = read_file(kh2_file("00battle.bin"));
        try {
            for (const kh2::BarEntry &e : kh2::bar_entries(file.data(), file.size())) {
                if (e.name != "enmp" || e.size < 8)
                    continue;
                std::uint32_t count = 0;
                std::memcpy(&count, file.data() + e.offset + 4, 4);
                for (std::uint32_t i = 0; i < count && 8 + 92 * std::size_t(i + 1) <= e.size; ++i) {
                    const std::uint8_t *r = file.data() + e.offset + 8 + 92 * std::size_t(i);
                    rows.emplace(int(r[0] | (r[1] << 8)), BattleRow{int(r[4] | (r[5] << 8)), int(r[2] | (r[3] << 8))});
                }
            }
        } catch (const std::exception &) {
        }
    }
    static const BattleRow none;
    const auto it = rows.find(entry);
    return it != rows.end() ? it->second : none;
}

int battle_health(int entry)
{
    return battle_row(entry).health;
}
namespace {

// One of 00battle.bin's tables (its BAR entry of that name) out of the mod's file: empty when it has none
std::vector<std::uint8_t> battle_table(const char *name)
{
    const std::vector<std::uint8_t> file = read_file(kh2_file("00battle.bin"));
    try {
        for (const kh2::BarEntry &e : kh2::bar_entries(file.data(), file.size()))
            if (e.name == name)
                return {file.begin() + std::ptrdiff_t(e.offset), file.begin() + std::ptrdiff_t(e.offset + e.size)};
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: the mod's kh2/00battle.bin cannot be read (%s)", x.what());
    }
    return {};
}
} // namespace

std::optional<LevelParam> level_param(int level)
{
    static const std::vector<std::uint8_t> table = battle_table("lvpm");
    if (level < 1 || std::size_t(level) * 12 > table.size())
        return std::nullopt;
    const std::uint8_t *r = table.data() + 12 * std::size_t(level - 1);
    const auto u16 = [r](int o) { return int(r[o] | (r[o + 1] << 8)); };
    return LevelParam{u16(0), u16(2), u16(4), u16(6), u16(8)};
}

// A party member's health as a new game starts it (00battle.bin's plrp: a u32 version and count, then 0x80 bytes a
// record -- + 2 its character, Sora 1 to Riku 13, the party members' part numbers; + 3 its health --, the first of
// that character: the record YS::SHEET::AllocFriend loads it from); 0 none
int character_health(int character)
{
    static const std::vector<std::uint8_t> table = battle_table("plrp");
    std::uint32_t count = 0;
    if (table.size() >= 8)
        std::memcpy(&count, table.data() + 4, 4);
    for (std::uint32_t i = 0; i < count && 8 + 0x80 * std::size_t(i + 1) <= table.size(); ++i) {
        const std::uint8_t *r = table.data() + 8 + 0x80 * std::size_t(i);
        if (r[2] == character && r[3] != 0)
            return r[3];
    }
    return 0;
}

// A party member's defence at a level (00battle.bin's lvup, YS::LEVELUP::Get: a u32 version and count, a word, then
// each character's table's offset in words, Sora's first; a table a u32 count of levels, then 16 bytes a level from
// 1: + 4 strength, + 5 magic, + 6 defence, + 7 AP); its last level's past them, 0 none
int character_defence(int character, int level)
{
    static const std::vector<std::uint8_t> table = battle_table("lvup");
    std::uint32_t count = 0, at = 0, levels = 0;
    if (table.size() >= 12)
        std::memcpy(&count, table.data() + 4, 4);
    if (character < 1 || std::uint32_t(character) > count || 12 + 4 * std::size_t(character) > table.size())
        return 0;
    std::memcpy(&at, table.data() + 8 + 4 * std::size_t(character), 4);
    const std::size_t from = 4 * std::size_t(at);
    if (at == 0 || from + 4 > table.size())
        return 0;
    std::memcpy(&levels, table.data() + from, 4);
    if (levels == 0)
        return 0;
    const std::size_t row = std::size_t(std::clamp(level, 1, int(levels)) - 1);
    return from + 4 + 16 * (row + 1) <= table.size() ? table[from + 4 + 16 * row + 6] : 0;
}
namespace {

// The share of its health Days's fields show: up to shown_most, at least 1 while it has any (Days defeats it at 0)
int shown_health(const Enemy &e)
{
    if (e.health <= 0 || e.health_most <= 0)
        return 0;
    return int(std::max<std::int64_t>(1, (e.health * e.shown_most + e.health_most - 1) / e.health_most));
}
} // namespace

void show_health(const Enemy &e)
{
    khdays_kh2_shell_set_health(e.days, shown_health(e), e.shown_most);
}
} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

extern "C" void khdays_kh2_enemies_hit(void *actor, int hp_before)
{
    const unsigned days = unsigned(KHDAYS_REF(actor));
    Enemy *e = find(days);
    KhdaysKh2Shell s;
    if (e != nullptr)
        e->dealt = -1; // (its class has taken it)
    if (e == nullptr || e->dying || !khdays_kh2_shell_read(days, &s))
        return;
    // the hit's damage: on its own health when it has one (Days's fields show a share of it, which a small hit may not
    // move), else on Days's HP
    const std::int64_t damage = e->health >= 0 ? e->health_taken : std::int64_t(hp_before - s.hp);
    e->health_taken = 0;
    const bool blow = e->blow_landing; // a KH2 player's blow's hit, not a Days hit
    e->blow_landing = false;
    // a party member's blow (Riku's): him the striker, and the patterns that ask for one not the player's on
    Object *party = blow && e->landing_party != 0 ? Object::at(mod().memory, e->landing_party) : nullptr;
    e->landing_party = 0;
    // its reaction: a Days hit's a small one (REACTION_SMALL, 3: most of KH2's player's attacks' in atkp); a KH2
    // player's blow's its attacks' (atkp + 10): "The End"'s and Cerberus's finish's none (0) -- the enemy then goes on
    // as it was (Roxas's rc_duelbaut on to his second phase); then as the enemy's damage patterns on change it
    const int first_reaction = blow ? e->landing_reaction : 3;
    const int hit_reaction = hit_pattern_reaction(*e->actor, first_reaction, party != nullptr);
    // a hit its least health kept from taking any (KH2's YS::SHEET MinHp) is a hit all the same: its reaction and its
    // trigger 6 (the Prison Keeper's spits a captive out and lowers its least on it)
    const bool at_least = e->held_at_least;
    e->held_at_least = false;
    if (damage <= 0 && !at_least)
        return;
    if (!blow && damage > 0)
        e->days_damage = last_days_damage = int(std::min<std::int64_t>(damage, 0x7fffffff));
    Actor &a = *e->actor;
    mission_damage(a.address(), damage); // its mission told (its last blow too)
    if (s.hp <= 0) { // its end: the script's "dead" (its effect, its fade); Days's defeat once it is over
        e->dying = true;
        // counted defeated (+ 0x9b8 bit 2, FUN_1403d4a40 as its death tells its group): what 1:272 reads -- Zexion
        // asks it of his books
        a.set_bits(field::kFade, bit::kDeadMark, true);
        // an enemy's (its entry's kind 4: the PS3 build's YS::ZAKO::_OVR__dead) tells the field: signal 75 with its
        // serial (the water clones' end counted by Demyx and his mission)
        if (const std::uint32_t entry = a.u32(field::kEntry); entry != 0 && (mod().memory.u32(entry + 4) & 0xffu) == 4u)
            the_field().signal(75, int(e->days & 0x7fffffffu));
        e->reacting = false;
        e->paused = false;
        a.damage_motion = false;
        a.set_bits(field::kStatus, bit::kHeld, false);
        if (e->dead_marked) // its script ends it (1:328: the cubes fade out, then their end)
            return;
        a.states().unlock();
        if (!a.states().enter(a, "dead"))
            e->released = true;
        if (tracing())
            khdays_log("kh2 enemies: %08x defeated%s", days, e->released ? " (no \"dead\" state)" : "");
        return;
    }
    // the last to hit it (2:88): the party member whose blow it is, else the nearest player
    Object *striker = party != nullptr ? party : the_field().player(a);
    if (striker != nullptr)
        a.set_u32(field::kAttacker, striker->address());
    // the reaction: a small one, from the front or the back as the striker stands (KH2's motions 10 and 12,
    // DAMAGE_S_FRONT_HIGH and DAMAGE_S_BACK_HIGH)
    bool front = true;
    float way[4] = {};
    if (striker != nullptr) {
        const Vec3 to = striker->place(), at = a.place(), f = facing_of(a.yaw());
        front = (to[0] - at[0]) * f[0] + (to[2] - at[2]) * f[2] >= 0.0F;
        const float dx = at[0] - to[0], dz = at[2] - to[2], flat = std::sqrt(dx * dx + dz * dz);
        if (flat > 0.0F)
            way[0] = dx / flat, way[2] = dz / flat;
    }
    if (!days_hit)
        days_hit = std::make_unique<Object>(mod().memory, "a Days hit");
    // the hit as KH2's YS::DAMAGE, at the offsets KH2FM.exe's calls read: its way (Dir, + 0) from the player who
    // struck to the enemy along the ground (2:1), its attacker and target (+ 0x10, + 0x14), its Flag (+ 0x18) clear, its
    // reaction (Reaction, + 0x26: 2:7, 2:8) and the one it had before the patterns (OrigReaction, + 0x27), its damage
    // (Damage, + 0x28: 2:46), in its health's measure
    mod().memory.write(days_hit->address(), way, sizeof way);
    days_hit->set_u32(0x10, striker != nullptr ? striker->address() : 0u);
    days_hit->set_u32(0x14, a.address());
    days_hit->set_u32(0x18, 0);
    days_hit->set_u32(0x24, std::uint32_t(hit_reaction & 0xff) << 16 | std::uint32_t(first_reaction & 0xff) << 24);
    days_hit->set_u32(0x28, std::uint32_t(std::min<std::int64_t>(std::max<std::int64_t>(damage, 0), 0x7fffffff)));
    // its attack's parameters (+ 0x20, an atkp record of 48 bytes): its element (+ 9, 2:85) the Days hit's
    // and no push back of its own (+ 0xc, 0x7fff: 2:86 gives KH2's battle constants' then)
    static std::uint32_t params = 0;
    if (params == 0) {
        params = mod().memory.add(48, "a Days hit's attack parameters");
        const std::uint8_t none[2] = {0xff, 0x7f};
        mod().memory.write(params + 0xc, none, 2);
    }
    const std::uint8_t element = std::uint8_t(kh2_element_of(e->hit_kinds));
    mod().memory.write(params + 9, &element, 1);
    days_hit->set_u32(0x20, params);
    // the hit's attack (its + 0xc its owner, 2:24: the player who struck). A hit is KH2's damage (the PS3 build's
    // YS::ENEMY::_OVR_exec_damage, PIERROT::_OVR_exec_damage), not its reflect: the state's + 0x18 routine (event 7)
    // and trigger 19 are a reflected attack's (PIERROT::_OVR_exec_reflect, the PC's FUN_140411060: its ACT::clear and
    // its reflect state, "revenge", are not a hit's)
    if (!days_attack)
        days_attack = std::make_unique<Object>(mod().memory, "a Days hit's attack");
    days_attack->set_u32(0xc, striker != nullptr ? striker->address() : 0u);
    days_attack_owner = striker != nullptr ? striker->address() : 0u;
    // its karma: the attack's RevengeDamage (a KH2 blow's atkp + 0x2a; a Days hit KH2's player's small hit's, 10)
    e->karma += float(blow ? e->landing_karma : 10);
    e->landing_karma = 0;
    const StateEntry *now = a.states().current();
    if (now != nullptr && (now->flags & 2) != 0) {
        if (tracing())
            khdays_log("kh2 enemies: %08x hit (%d to %d) in a locked state", days, hp_before, s.hp);
        return; // a locked state (its leaving, an event) plays on
    }
    if (hit_reaction == 0) { // no reaction: no motion, no "revenge"; trigger 6 told all the same
        const ai::Value packet = ai::Value::of_address(days_hit->address());
        a.states().event(a, 3, {&packet, 1});
        a.event(6, {&packet, 1});
        if (tracing())
            khdays_log("kh2 enemies: %08x hit (%d to %d) with no reaction (%s)", days, hp_before, s.hp,
                       first_reaction == 0 ? "a KH2 player's blow" : "its damage patterns");
        return;
    }
    const int motion = front ? 10 : 12;
    // KH2's hit (FUN_140411060): the reaction applied -- its motion asked, the actor held while it plays
    // (FUN_1403d4650; a set without the motion, a boss's, plays nothing and goes on), the queue cleared -- and the
    // (the PS3 build's YS::BTLOBJ::_OVR_exec_damage_reaction -> exec_damage_small_spot: the motion started and its
    // idle pushed after it; no state is cleared or entered: the state goes on, a state under priority 300 held while
    // the damage motion plays -- ACT::exec -- and its "revenge" starts from the motion's trigger 28 once its karma
    // reaches its limit, cues)
    const bool moves = a.motions->has(motion);
    if (moves) {
        a.jump = {};
        a.take_off.reset();
        a.play(motion, 2.0F, 0.0F);
        a.motion.queued.clear();
        a.motion.queued.push_back({0, 2.0F});
        a.set_bits(field::kStatus, bit::kHeld, true);
        e->reacting = true;
        a.damage_motion = true;
    }
    e->paused = false;
    // then the current state's own routine for it (ACT::damage: callback 3) and trigger 6, the hit its argument
    // (PIERROT::_OVR_exec_damage): KH2's enemies turn to their revenge mode on it when the player struck them (1:309
    // of 2:88's)
    const ai::Value packet = ai::Value::of_address(days_hit->address());
    a.states().event(a, 3, {&packet, 1});
    a.event(6, {&packet, 1});
    if (tracing())
        khdays_log("kh2 enemies: %08x hit (%d to %d), %s, karma %.0f", days, hp_before, s.hp,
                   moves ? (motion == 10 ? "motion 10" : "motion 12") : "no reaction motion of its own",
                   double(e->karma));
}

// Ov107_AiState_ApplyHit and Ov107_AiState_HitShapeOverlap (prepare.py's joins): its script's FLAG_NOHIT_ATTACK and
// FLAG_NOHIT_OBJ (+ 0x120 bits 3 and 1): no hit lands, nothing touches it (the Mole Driller under the ground) -- but
// the hit that counts it defeated (dead_mark: Zexion's illusion ends with its hits off)
extern "C" int khdays_kh2_enemies_untouched(void *actor)
{
    const Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    return e != nullptr && !e->dying && !e->dead_marked && (e->actor->u32(field::kFlags) & bit::kNoHitAttack) != 0;
}

// Ov107_AiState_ApplyHit (prepare.py's join): no hit on a KH2 enemy from a player KH2 lets make none now -- turned into
// a form (Capture: CHANGEFORM puts a new object in his place, so a swing his Days self began lands not) or his control
// taken (1:42 of type 0, YS::OBJ::ControlOff: Days's lock stops his next command, not a combo under way). Luxord's die:
// Roxas's swing begun before landed two frames after he turned, Luxord's revenge cut the routine before its 1:43 and the
// players' control stayed taken
extern "C" int khdays_kh2_enemies_disarmed(void *enemy, void *source)
{
    const Enemy *e = find(unsigned(KHDAYS_REF(enemy)));
    if (source == nullptr || e == nullptr || e->blow_landing) // (a KH2 blow of his landing: his form's own)
        return 0;
    const unsigned from = unsigned(KHDAYS_REF(source));
    const bool disarmed = captures.count(from) != 0 || (controls_off.count(0) != 0 && players.count(from) != 0);
    if (disarmed && tracing())
        khdays_log("kh2 enemies: %08x's hit from player %08x lands not (%s)", unsigned(KHDAYS_REF(enemy)), from,
                   captures.count(from) != 0 ? "turned into a form" : "his control taken");
    return disarmed ? 1 : 0;
}

// Ov107_AiState_ApplyHit (prepare.py's join): a Days player's hit on a KH2 enemy -- KH2's enemies alone; Days's own
// go on as they always have -- reflected by an attack of its (clashed): 1, Days's hit answered with no damage (as an
// enemy's barrier answers it, + 0x1c4's 0x10). His block packet (Ov022_TryBlockHit's, its low flags 0x11) of the
// attack he is struck with marks that attack guarded (hit_player reflects it once Days's hit is over): 0, Days goes
// on. Not a KH2 player's blow (a reaction command's, a party member's) nor a player's not in the field's list
extern "C" int khdays_kh2_enemies_reflected(void *actor, void *source, int flags)
{
    Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    if (source == nullptr || e == nullptr || e->dying || e->blow_landing || !e->actor)
        return 0;
    const unsigned from = unsigned(KHDAYS_REF(source));
    the_field().player(*e->actor); // the players' objects as they stand now
    const auto it = players.find(from);
    if (it == players.end() || !it->second)
        return 0;
    if ((flags & 0xff) == 0x11) {
        if (delivering.enemy == e->days)
            delivering.guarded_by = from;
        return 0;
    }
    return clashed(*e, from, *it->second) ? 1 : 0;
}

extern "C" int khdays_kh2_enemies_no_contact(void *actor)
{
    const Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    return e != nullptr && !e->dying && (e->actor->u32(field::kFlags) & bit::kNoHitObject) != 0;
}

// Ov107_AiState_ApplyHit (prepare.py's join): a hit's damage kept from taking the enemy under the least health its
// script set (1:231, YS::SHEET's MinHp: KH2's damage stops there -- the Phantom's), or one over it while it withstands
// (pattern 9); what Days's fields then lose kept for the enemy's class (khdays_kh2_enemies_dealt)
extern "C" int khdays_kh2_enemies_damage(void *actor, int amount, int kinds)
{
    const int kept = kept_damage(actor, amount, kinds);
    if (Enemy *e = find(unsigned(KHDAYS_REF(actor)))) {
        e->dealt = kept;
        e->dealt_frame = khdays_link_frame();
    }
    return kept;
}

// Ov107_CalcHitDamage (prepare.py's join): the damage a KH2 enemy's Days class works out again in its own hit
// handler (each class's OnHit takes its HP off so, never the amount Ov107_AiState_ApplyHit kept) -- the kept one,
// once, in the frame it was kept: else Luxord, kept at 1 while his time lasts, fell to the hit all the same
extern "C" int khdays_kh2_enemies_dealt(void *actor, int amount)
{
    Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    if (e == nullptr || e->dealt < 0 || e->dealt_frame != khdays_link_frame())
        return amount;
    const int kept = e->dealt;
    e->dealt = -1;
    return kept;
}
namespace khdays::kh2_enemies::shell {

namespace {

int kept_damage(void *actor, int amount, int kinds)
{
    Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    if (e == nullptr || e->dying || amount <= 0)
        return amount;
    e->hit_kinds = unsigned(kinds);
    // a KH2 player's blow (a reaction command's strike) on one with a health of its own: its power in the area's
    // measured blows -- before any Days hit measured one, kept for its health once it is measured
    if (e->blow_landing && e->health_share > 0.0) {
        const double blows = e->blow_blows;
        e->blow_blows = 0.0;
        if (reference_blow <= 0) {
            e->pending_blows += blows;
            // its least health (1:231, YS::SHEET's MinHp) or its withstanding (pattern 9) keep it from falling as they
            // keep a measured hit (below): the blows stop where its health would reach its least, one over it while it
            // withstands -- Final Xemnas, kept at 1 for his laser dome, fell to his partner's blows alone otherwise
            const std::uint32_t sheet = e->actor->u32(0x5c0);
            const int least = sheet != 0 ? std::int32_t(mod().memory.u32(sheet + 8)) : 0;
            const Actor &a = *e->actor;
            const bool withstands = (((a.u32(0xd10) | a.u32(0xd14)) & ~a.u32(0xd18)) & 0x200u) != 0;
            const double whole = kSoldierBlows * e->health_share * e->start_share;
            if (least > 0 || withstands) {
                const double share = least > 0 && e->record_most > 0 ? double(least) / double(e->record_most) : 0.0;
                const double most = std::max(0.0, whole - kSoldierBlows * e->health_share * share) - 1e-3;
                if (e->pending_blows >= most) {
                    e->pending_blows = std::max(0.0, most);
                    e->held_at_least = true;
                    if (tracing())
                        khdays_log("kh2 enemies: %08x takes %.2f of KH2's blows before a Days hit measured one: held "
                                   "at its least",
                                   e->days, blows);
                    return 0;
                }
            }
            // enough of them to end it in KH2's own measure (its health in KH2's Soldier blows, as much as is left of
            // it): its end, the Days hit its whole Days health -- Demyx's water clones (a quarter of a blow) fall to the
            // first swing of Wild Dance, as in KH2, whether Roxas struck anything before or not
            if (KhdaysKh2Shell s; e->pending_blows >= whole && khdays_kh2_shell_read(e->days, &s) && s.hp > 0) {
                if (tracing())
                    khdays_log("kh2 enemies: %08x takes %.2f of KH2's blows before a Days hit measured one: its "
                               "health's %.2f, its end",
                               e->days, e->pending_blows, kSoldierBlows * e->health_share * e->start_share);
                return s.hp;
            }
            if (tracing())
                khdays_log("kh2 enemies: %08x takes %.2f of KH2's blows before a Days hit measured one: kept", e->days,
                           blows);
            return 0;
        }
        amount = int(std::clamp(std::round(blows * double(reference_blow)), 1.0, 2147483647.0));
    }
    // its health in Days's damage at the first hit on it: kSoldierBlows x its share of KH2's Soldier blows of the area's
    // first hit's size -- as many of Roxas's blows as Sora's it stands in KH2 (Hades of the cups: 60), whatever Roxas's
    // level and gear
    if (e->health < 0 && e->health_share > 0.0) {
        if (reference_blow <= 0)
            reference_blow = amount;
        e->health_most =
            std::max<std::int64_t>(1, std::llround(kSoldierBlows * e->health_share * double(reference_blow)));
        e->health = std::clamp<std::int64_t>(std::llround(e->start_share * double(e->health_most)), 1, e->health_most);
        if (e->pending_blows > 0.0) { // KH2's blows that landed before
            e->health = std::max<std::int64_t>(1, e->health - std::llround(e->pending_blows * double(reference_blow)));
            e->pending_blows = 0.0;
        }
        e->shown_most = int(std::min<std::int64_t>(e->health_most, kDaysShownMost));
        show_health(*e);
        if (tracing())
            khdays_log("kh2 enemies: %08x's health %lld: %.0f blows of %d (%.2f KH2 Soldiers of %.0f), shown as %d",
                       e->days, static_cast<long long>(e->health_most), kSoldierBlows * e->health_share, reference_blow,
                       e->health_share, kSoldierBlows, e->shown_most);
    }
    // its share of that element (1:311: the Volcanic Lord takes no fire)
    const int element = kh2_element_of(unsigned(kinds));
    if (trace_level() >= 2)
        khdays_log("kh2 enemies: %08x struck for %d, Days's kinds %#x (KH2's element %d)", e->days, amount,
                   unsigned(kinds), element);
    const int rate = e->actor->element_rate[std::size_t(element)];
    if (rate != 100) {
        const int shared = int((std::int64_t(amount) * rate + 99) / 100);
        if (tracing())
            khdays_log("kh2 enemies: %08x takes %d of %d (element %d at %d%%; Days's kinds %#x)", e->days, shared,
                       amount, element, rate, unsigned(kinds));
        amount = shared;
        if (amount <= 0)
            return 0;
    }
    const std::uint32_t record = e->actor->u32(0x5c0);
    KhdaysKh2Shell s;
    if (!khdays_kh2_shell_read(e->days, &s))
        return amount;
    int least = record != 0 ? std::int32_t(mod().memory.u32(record + 8)) : 0;
    if (least > 0 && e->record_most > 0 && e->health >= 0) // the record's measure to its health's
        least = int(std::min<std::int64_t>(0x7fffffff, (std::int64_t(least) * e->health_most + e->record_most - 1) /
                                                           e->record_most));
    // withstanding (its damage pattern 9 on: YS::BTLOBJ::is_withstand, the PS3 build's): a hit that would take it to
    // its least or below leaves it one over (BTLOBJ::add_damage), so it cannot fall -- Luxord, till his time gauge
    // runs out (his script takes the pattern off then, 1:324, and leaves him 1)
    if (const Actor &a = *e->actor; (((a.u32(0xd10) | a.u32(0xd14)) & ~a.u32(0xd18)) & 0x200u) != 0)
        least = std::max(least, 0) + 1;
    if (e->health >= 0) {
        // its own health takes the damage (Days's damage is in its stand-in's measure, as its health is); Days's
        // fields lose what their share does
        const std::int64_t kept =
            least > 0 ? std::clamp<std::int64_t>(amount, 0, std::max<std::int64_t>(0, e->health - least))
                      : std::int64_t(amount);
        e->health = std::max<std::int64_t>(0, e->health - kept);
        e->health_taken = kept;
        e->held_at_least = kept < amount;
        const int shown = shown_health(*e), taken = std::max(0, s.hp - shown);
        if (trace_level() >= 2 || (kept != amount && tracing()))
            khdays_log("kh2 enemies: %08x takes %lld of %d damage: %lld of %lld health left, %d of %d shown", e->days,
                       static_cast<long long>(kept), amount, static_cast<long long>(e->health),
                       static_cast<long long>(e->health_most), shown, e->shown_most);
        return taken;
    }
    if (least <= 0)
        return amount;
    const int kept = std::max(0, std::min(amount, s.hp - least));
    if (kept != amount && tracing())
        khdays_log("kh2 enemies: %08x keeps %d health: %d of %d damage taken", e->days, least, kept, amount);
    return kept;
}
} // namespace

} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

// Ov107_ProcessObjectTick (prepare.py's join): a KH2 player's blow on the enemy to land now as one Days hit (its
// damage in `amount`), the hit's own way through Days's hit and the shell's
extern "C" int khdays_kh2_enemies_blow(void *actor, int *amount)
{
    Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    if (e == nullptr || e->blow <= 0 || e->dying)
        return 0;
    *amount = e->blow;
    // one with a health of its own: its power in the area's measured blows (khdays_kh2_enemies_damage takes it so), in
    // the measure Days's fields show -- Days's class takes the packet's amount off them too -- and 1 before a Days hit
    // measured a blow
    if (e->health_share > 0.0) {
        const double health = reference_blow > 0 ? e->blow_blows * double(reference_blow) : 1.0;
        const double shown = e->health_most > 0 ? health * double(e->shown_most) / double(e->health_most) : health;
        *amount = int(std::clamp(std::ceil(shown), 1.0, 2147483647.0));
        // before then, blows enough to end it in KH2's own measure (khdays_kh2_enemies_damage): its whole Days health
        if (KhdaysKh2Shell s; reference_blow <= 0 &&
                              e->pending_blows + e->blow_blows >= kSoldierBlows * e->health_share * e->start_share &&
                              khdays_kh2_shell_read(e->days, &s) && s.hp > 0)
            *amount = s.hp;
    }
    e->blow = 0;
    e->blow_landing = true;
    e->landing_reaction = e->blow_reaction;
    e->blow_reaction = 0;
    e->landing_party = e->blow_party;
    e->blow_party = 0;
    e->landing_karma = e->blow_karma;
    e->blow_karma = 0;
    return 1;
}
