// Reaction commands and the scripts' command menus on Days's deck.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {
std::uint32_t menu_a_frame = ~0u; // the game frame whose A a script's command menu took from Days's deck

// Whether the menu a script gives is drawn as KH2's own, four slots (this VBlank or the one before)
bool top_menu_drawn()
{
    return top_offer.shown && khdays_link_frame() - top_offer.drawn <= 1u;
}
} // namespace

// KH2's reaction command zones: the shapes of kind 5 (their command at + 2) in the collision groups on now (below:
// the script's, the motion's ranges 4 and 5 (FUN_14040d810 on + 0x5a0), the shapes' default), offering their
// command to this game's own player while his place (+ 0x670) lies within a zone's radius of its point (the test KH2's
// own uses of a zone make, FUN_14054ae70: the place's distance, FUN_140140f50, under the shape's + 0x10). The Magnum
// Loader's eight along its rush (52A, group 5: Quick Blade), the Armored Knight's; the Soldier's and the Dusk's not
// (Kind::plays_player). Not while an enemy holds him, nor again in the motion one was taken in.
// The collision groups on now (YS::OBJ::is_enable_collision, FUN_1403ba550): one the script turned on or off (1:52,
// 1:53: + 0x5a8) is so; else one the motion's ranges turn on (4) or off (5) now (+ 0x5a0); else the shapes' default
// (their header's + 4, a bit a group)
std::set<int> groups_on(const Enemy &e)
{
    std::set<int> motion_on, motion_off, groups;
    const Actor &a = *e.actor;
    const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.motions->attributes(a.motion.id) : nullptr;
    if (attr != nullptr)
        for (const auto &r : attr->ranges)
            if ((r.id == 4 || r.id == 5) && !r.params.empty() && a.motion.time >= float(r.start) &&
                a.motion.time < float(r.end))
                (r.id == 4 ? motion_on : motion_off).insert(std::int16_t(r.params[0]));
    std::set<int> asked;
    for (const Shape &s : e.kind->shapes)
        asked.insert(s.group);
    asked.insert(motion_on.begin(), motion_on.end());
    for (const int g : asked) {
        const std::uint32_t at = field::kCollisionFlags + (std::uint32_t(g) >> 5) * 4, bit = 1u << (g & 31);
        bool on = false;
        if ((a.u32(at) & bit) != 0)
            on = true;
        else if ((a.u32(at + 4) & bit) != 0)
            on = false;
        else if (motion_on.count(g) != 0)
            on = true;
        else if (motion_off.count(g) != 0)
            on = false;
        else
            on = ((e.kind->default_groups >> (g & 31)) & 1u) != 0;
        if (on)
            groups.insert(g);
    }
    return groups;
}

// A large enemy's body as a Days enemy's one sphere holds it (Enemy::large): how far along the ground from its place its
// body's shapes (kind 1, in its groups on, in its pose now) reach at a player's height -- KH2 keeps its player's body
// (P_EX100's kind 1: a cylinder 85 high, 35 round) off each sphere it meets, so a sphere counts by its widest within
// that height over the ground; 0 none does. KH2's Hydra is many spheres on its legs, chest and necks: its largest
// alone (or one of a group off) left Roxas under it, all of them (the necks above him) kept him off its own reaction
// commands' zones
float footprint(Enemy &e, const KhdaysKh2Shell &s)
{
    constexpr float kPlayerBody = 85.0F / kKh2PerDays; // KH2's player's body's height (Days's units)
    const std::set<int> on = groups_on(e);
    if (e.pose.empty())
        pose(e);
    float reach = 0.0F;
    for (const Shape &sh : e.kind->shapes) {
        if (sh.kind != 0x0001 || on.count(sh.group) == 0)
            continue;
        float d[3];
        days_point(e, s, sh.bone, sh.at, d);
        const float r = sh.radius / kKh2PerDays, low = s.place[1], high = s.place[1] + kPlayerBody;
        const float off = d[1] < low ? low - d[1] : d[1] > high ? d[1] - high : 0.0F;
        if (off >= r)
            continue;
        const float dx = d[0] - s.place[0], dz = d[2] - s.place[2];
        reach = std::max(reach, std::sqrt(dx * dx + dz * dz) + std::sqrt(r * r - off * off));
    }
    return reach;
}

void zone_offer(Enemy &e)
{
    e.zone = -1;
    const Actor &a = *e.actor;
    if (a.motion.id != e.zone_taken_motion)
        e.zone_taken_motion = -1; // a motion after the one a command was taken in: its zones offer again
    const auto p = players.find(local_player);
    if (local_player == 0 || e.dying || !e.kind->plays_player || p == players.end() || !p->second || a.motion.id < 0 ||
        a.motion.id == e.zone_taken_motion)
        return;
    const std::set<int> groups = groups_on(e);
    if (groups.empty())
        return;
    KhdaysKh2Shell now{};
    to_days(a.place(), now.place);
    if (e.pose.empty())
        pose(e);
    const Vec3 player = p->second->place();
    for (const Shape &sh : e.kind->shapes) {
        // held, only a command he can take (command_usable: Roxas's grab holds him in AR20, whose range 26 is
        // "Reversal"'s 675, and the zone his own motion turns on offers it)
        if ((sh.kind & 0xff) != 5 || groups.count(sh.group) == 0 || sh.command <= 0 || sh.radius <= 0.0F ||
            !command_usable(sh.command))
            continue;
        float d[3];
        days_point(e, now, sh.bone, sh.at, d);
        const Vec3 at = to_kh2(d);
        const float dx = player[0] - at[0], dy = player[1] - at[1], dz = player[2] - at[2];
        const float depth = std::sqrt(dx * dx + dy * dy + dz * dz) / sh.radius;
        if (depth < 1.0F && (e.zone < 0 || depth < e.zone_depth)) {
            e.zone = sh.command;
            e.zone_depth = depth;
        }
    }
}

// KH2's attacks that carry a reaction command (2:51: the Hydra's Urninator on the urn Phil hands the player): offered
// while this game's own player stands within its reach of the attack, its owner the one a press sends it to -- the PS3
// build's YS::RC_SEARCHER::check_magic: each magic attack with a command (ATTACK::EachMagic, its + 0x118: + 0x11c
// here), a collision of its two sizes (+ 0x11c, + 0x120: + 0x120, + 0x124 here) at its place met by the player's,
// TARGET::set_attack to its owner. As a script's offer (1:165), this tick
void attack_offers(Enemy &e)
{
    const auto p = players.find(local_player);
    if (local_player == 0 || held.count(local_player) != 0 || p == players.end() || !p->second)
        return;
    const Vec3 me = p->second->place();
    const auto f32 = [](std::uint32_t at) {
        const std::uint32_t bits = mod().memory.u32(at);
        float f = 0.0F;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    };
    for (const auto &[at, o] : attacks) {
        if (o.enemy != e.days || o.ended || !o.placed)
            continue;
        const int command = std::int32_t(mod().memory.u32(at + 0x11c));
        if (command <= 0)
            continue;
        const float reach = f32(at + 0x120), height = f32(at + 0x124);
        const float dx = me[0] - o.at[0], dz = me[2] - o.at[2];
        if (dx * dx + dz * dz <= reach * reach && std::fabs(me[1] - o.at[1]) <= std::max(reach, height)) {
            if (tracing() && script_offer.command != command)
                khdays_log("kh2 enemies: %08x's attack %08x offers command %d", e.days, at, command);
            script_offer = {e.days, command, khdays_link_frame(), at};
        }
    }
}

// The command an enemy's script offers now (1:165 this frame or the one before), -1 none
int script_offered()
{
    return script_offer.command >= 0 && khdays_link_frame() - script_offer.frame <= 2u &&
                   enemies.count(script_offer.enemy) != 0
               ? script_offer.command
               : -1;
}

// Whether an enemy's script gives this game's own player's command menu now (1:237 this frame or the one before)
bool top_menu_standing()
{
    if (special_menu.enemy != 0 && enemies.count(special_menu.enemy) != 0) { // (10:6's: till 10:7)
        top_offer.enemy = special_menu.enemy;
        top_offer.commands = special_menu.commands;
        top_offer.frame = khdays_link_frame();
        return true;
    }
    return khdays_link_frame() - top_offer.frame <= 2u && enemies.count(top_offer.enemy) != 0;
}

// Whether this game's own player can take command `id` now, from his menu or a zone (KH2's FUN_1403ba070): free, any
// but one that asks its require (its flags' 0x40: YS::RC_SEARCHER::get_command_elem gives it only while the player's
// motion follows on from it, + 0x160 bit 21 and + 0x50c -- Days's own motions follow on from none) or one not given in
// battle (no 0x10, the field always fighting); held, one whose require the motion an enemy holds him in follows on from
// (its range 26: Roxas's duel, AR11, "The End"'s 672; his grab, AR20, "Reversal"'s 675; Final Xemnas's grab, AQ50,
// "Rescue"'s 657), no other -- KH2's is then in no state to act (no range 0 to 2 in his motion: the duel's "?", 104).
// Final Xemnas's Rescue zone stands on all the battle long: its 0x40 keeps it to his grab
bool command_usable(int id)
{
    const auto h = held.find(local_player);
    const Command *c = command(id);
    if (h == held.end())
        return c == nullptr || ((c->flags & 0x40u) == 0 && (c->flags & 0x10u) != 0);
    return c != nullptr && c->require != 0 && h->second.follows.count(c->require) != 0;
}
namespace {

// The command the deck's main row `row` (of `rows`) shows of the menu a script gives: its slot's, the slot past the
// deck's rows in the row chosen for it; of two there, the one the player can choose (the duel's "The End" in any row
// alike); 0 none
int deck_command(int row, int rows)
{
    int found = 0;
    for (int slot = 0; slot < int(top_offer.commands.size()); ++slot) {
        const int at = slot < rows ? slot : top_offer.extra_row;
        const int id = top_offer.commands[std::size_t(slot)];
        if (at != row || id <= 0)
            continue;
        if (found == 0 || (!command_usable(found) && command_usable(id)))
            found = id;
    }
    return found;
}
} // namespace

// The enemy whose zone offers this game's own player a command now, the deepest in; null none
Enemy *zone_offerer()
{
    Enemy *best = nullptr;
    for (auto &[days, e] : enemies)
        if (e->zone >= 0 && (best == nullptr || e->zone_depth < best->zone_depth))
            best = e.get();
    return best;
}

// A reaction command this game's own player takes (the pad's top button, KH2's triangle: the DS X), offered by the motion
// an enemy holds him in or by an enemy's zone: the command's argument sent to the enemy (its trigger 11, a message:
// Cerberus's 38, 39, 41 its steps' successes, the Magnum Loader's 309), once an offer
void take_command(Enemy &e)
{
    static std::uint32_t frame = ~0u;
    static std::uint16_t was = 0, pressed = 0;
    if (const std::uint32_t now = khdays_input_game_frames(); now != frame) {
        frame = now;
        const KhdaysInputSnapshot *s = khdays_input_snapshot();
        // the player's, and an input script's (a walk takes a command as a player does)
        const std::uint16_t buttons = std::uint16_t((s != nullptr ? s->ds_buttons : 0) | khdays_input_script_buttons());
        pressed = std::uint16_t(buttons & ~was);
        was = buttons;
    }
    // a press takes one command: the one a held player's motion offers, else a script's, else a zone's; one chosen in
    // the menu a script gives (A) is no reaction command's bar's
    const auto send = [&e](int id, const char *how, bool menu = false, std::uint32_t attack = 0) {
        pressed = std::uint16_t(pressed & ~(menu ? KHDAYS_KEY_A : KHDAYS_KEY_X));
        if (!menu) {
            ++local_takes;
            local_taken = id;
        }
        const Command *c = command(id);
        const int number = c != nullptr ? c->argument : id;
        const auto o = players.find(local_player);
        // KH2 sends the command's target record (FUN_1403a8b10 -> FUN_1403b5fa0: the manager's + 0x10, FUN_1403bee60's:
        // the object, its group, the attack -- TARGET::set_attack's, for a command an attack carries (2:51): 1:166
        // reads it -- or none, -1); the scripts take the object from its first word (the Dusk's Reversal orbits the
        // player round it)
        if (e.command_record == 0)
            e.command_record = mod().memory.add(16, e.actor->name() + "'s command record");
        const std::uint32_t record[4] = {e.actor->address(), 0u, attack, 0xffffffffu};
        mod().memory.write(e.command_record, record, sizeof record);
        const ai::Value values[3] = {ai::Value::of_address(o != players.end() && o->second ? o->second->address() : 0),
                                     ai::Value::of_int(number), ai::Value::of_address(e.command_record)};
        e.actor->event(11, values);
        if (tracing())
            khdays_log("kh2 enemies: player %08x takes reaction command %d %s %08x: message %d", local_player, id, how,
                       e.days, number);
    };
    for (auto &[object, h] : held) {
        if (h.enemy != e.days || h.offered < 0 || object != local_player || (pressed & KHDAYS_KEY_X) == 0)
            continue;
        const int id = h.offered;
        h.taken.insert(id);
        h.offered = -1;
        send(id, "offered by his motion in the hands of");
    }
    if (const int id = script_offered(); id >= 0 && (pressed & KHDAYS_KEY_X) != 0 && script_offer.enemy == e.days) {
        script_offer.command = -1;
        send(id, script_offer.attack != 0 ? "offered by an attack of" : "offered by the script of", false,
             script_offer.attack);
    }
    if (e.zone >= 0 && (pressed & KHDAYS_KEY_X) != 0 && zone_offerer() == &e) {
        const int id = e.zone;
        e.zone = -1;
        e.zone_taken_motion = e.actor->motion.id;
        send(id, "from the zone of");
    }
    // the command menu a script gives (1:237) on Days's deck: A the command its row shows -- sent to the enemy as a
    // reaction command is when the player can choose it now (command_usable), else nothing. Held, Days's own input does
    // not run: its cursor moved here as the deck's is (X the next row, Days's own steps and sound) and KH2's (Up,
    // Down). Free (Zexion's Lexicon, its illusion gone), Days's own input moves it, and Days's own use of that A is
    // none (khdays_kh2_enemies_menu_takes_a)
    const bool held_here = held.count(local_player) != 0;
    if (top_menu_standing() && top_offer.enemy == e.days && (held_here || top_offer.shown)) {
        if (held_here && (pressed & (KHDAYS_KEY_DOWN | KHDAYS_KEY_X)) != 0)
            khdays_deck_step(0);
        if (held_here && (pressed & KHDAYS_KEY_UP) != 0)
            khdays_deck_step(1);
        if (held_here)
            pressed = std::uint16_t(pressed & ~(KHDAYS_KEY_DOWN | KHDAYS_KEY_X | KHDAYS_KEY_UP));
        if ((pressed & KHDAYS_KEY_A) != 0) {
            // drawn as KH2's menu, the slot its own cursor is on; else the deck's row's
            const bool kh2_menu = top_menu_drawn();
            const int row = kh2_menu ? top_offer.cursor : khdays_deck_main_row();
            const int id = kh2_menu   ? top_offer.commands[std::size_t(top_offer.cursor)]
                           : row >= 0 ? deck_command(row, khdays_deck_main_rows())
                                      : 0;
            if (id > 0 && command_usable(id))
                send(id, "chosen on the deck as the command menu given by", true);
            else if (tracing())
                khdays_log(
                    "kh2 enemies: the player cannot choose command %d (the deck's row %d) of the menu %08x gives", id,
                    row, e.days);
            pressed = std::uint16_t(pressed & ~KHDAYS_KEY_A);
        }
    }
}

// The deck's rows as the command menu a script gives shows them (each frame, from khdays_kh2_enemies_holding), held or
// free -- KH2's 1:237 gives the player's menu whatever he does (Zexion's Lexicon's pages after his illusion let him
// go): as the menu comes, the deck back to its main rows (KH2 closes its submenus) and the row for its fourth slot
// chosen; their text drawn again whenever what they show changes, and once more as the menu goes (Days's own again)
void deck_sync()
{
    const bool wanted = top_menu_standing() && khdays_deck_main_row() != -2;
    if (wanted && !top_offer.shown && khdays_deck_main_row() == -1)
        khdays_deck_to_main();
    const int rows = wanted ? khdays_deck_main_rows() : 0;
    const bool show = rows > 0;
    std::array<int, 4> want{};
    if (show) {
        if (!top_offer.shown)
            top_offer.extra_row = int(khdays_link_frame() % unsigned(rows));
        for (int r = 0; r < rows && r < int(want.size()); ++r)
            want[std::size_t(r)] = deck_command(r, rows);
    }
    // KH2's own cursor over the four slots (the menu drawn as KH2's): on the deck's row as the menu comes, then a slot
    // on or back as Days's deck steps a row (its X, its D-pad step, the runtime's own while held), round the four as
    // KH2's goes round
    const int deck_row = show ? khdays_deck_main_row() : -1;
    if (show && !top_offer.shown)
        top_offer.cursor = std::max(0, deck_row);
    else if (show && deck_row >= 0 && top_offer.deck_row >= 0 && deck_row != top_offer.deck_row) {
        const int slots = int(top_offer.commands.size());
        const bool back = rows > 2 && (top_offer.deck_row - deck_row + rows) % rows == 1;
        top_offer.cursor = (top_offer.cursor + (back ? slots - 1 : 1)) % slots;
        if (tracing())
            khdays_log("kh2 enemies: the command menu's cursor on its slot %d (%d)", top_offer.cursor,
                       top_offer.commands[std::size_t(top_offer.cursor)]);
    }
    top_offer.deck_row = deck_row;
    if (show == top_offer.shown && want == top_offer.labels)
        return;
    top_offer.shown = show;
    top_offer.labels = want;
    khdays_deck_relabel();
}
} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

// The reaction command offered to this game's own player now (its name's KH2 text number in `text`), 0 none
extern "C" int khdays_kh2_enemies_offered(int *text)
{
    int id = -1;
    if (const auto h = held.find(local_player); h != held.end())
        id = h->second.offered;
    if (id < 0)
        id = script_offered();
    if (id < 0)
        if (const Enemy *z = zone_offerer()) // held, only one he can take (zone_offer)
            id = z->zone;
    if (id < 0)
        return 0;
    if (text != nullptr) {
        const Command *c = command(id);
        *text = c != nullptr ? c->text : 0;
    }
    return id;
}

// How many reaction commands this game's own player took; whether an enemy holds that player now (`held_now`)
extern "C" unsigned khdays_kh2_enemies_taken(int *held_now)
{
    if (held_now != nullptr)
        *held_now = held.count(local_player) != 0;
    return local_takes;
}

// Ov022_TranslateCommandToAction (prepare.py): whether a KH2 enemy's script took the players' control away (1:42 of
// type 0, YS::OBJ::ControlOff): each player locked as Days's own lock locks them, no buttons, no command, no way
extern "C" int khdays_kh2_enemies_control_off(void)
{
    return controls_off.count(0) != 0 ? 1 : 0;
}

// Ov002_CollectNearbySpots (prepare.py): whether a KH2 enemy's prize drain (1:187) takes every prize however far
extern "C" int khdays_kh2_enemies_prize_drain(void)
{
    return drain_frames > 0 ? 1 : 0;
}

// Ov022_UpdateCommandInput (prepare.py): A on the deck while it shows the command menu a script gives a free player
// (take_command sends its row's command): Days's own deck slot not used
extern "C" int khdays_kh2_enemies_menu_takes_a(int pressed)
{
    if ((pressed & KHDAYS_KEY_A) == 0 || !top_menu_standing() || !top_offer.shown || held.count(local_player) != 0)
        return 0;
    menu_a_frame = khdays_input_game_frames();
    return 1;
}

// ... and the command made of that press: none (kind 7, as the reaction button's own press of a KH2 command)
extern "C" int khdays_kh2_enemies_menu_commit(signed char *kind, unsigned short *arg)
{
    if (menu_a_frame != khdays_input_game_frames() || kind == nullptr || arg == nullptr)
        return 0;
    *kind = 7;
    *arg = 0;
    return 1;
}

extern "C" int khdays_kh2_enemies_command_image(char *archive, int archive_room, char *picture, int picture_room)
{
    const auto it = command_image_of != 0 ? enemies.find(command_image_of) : enemies.end();
    if (it == enemies.end() || it->second->dying || it->second->kind == nullptr || archive == nullptr ||
        picture == nullptr || archive_room <= 0 || picture_room <= 0)
        return 0;
    const KindFiles &k = *it->second->kind;
    const std::u8string &path = kind_archive(k);
    if (path.empty() || path.size() >= std::size_t(archive_room))
        return 0;
    std::memcpy(archive, path.data(), path.size());
    archive[path.size()] = '\0';
    const std::string stem = "kh2/fx/" + k.name + "/-" + std::to_string(k.fx_hd_pages);
    if (stem.size() >= std::size_t(picture_room))
        return 0;
    std::memcpy(picture, stem.data(), stem.size());
    picture[stem.size()] = '\0';
    return 1;
}
