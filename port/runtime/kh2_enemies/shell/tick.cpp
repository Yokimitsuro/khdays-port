// The shell's life: the Days enemies a KH2 row makes, the area they stand in, each KH2 tick of theirs.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {
std::map<unsigned, int> made_row;     // every Days enemy made in this area -> its row
std::map<int, std::string> row_kinds; // the rows' KH2 enemies ("" none), asked once a row
bool kh2_rows = false;                // whether this area made any actor for a KH2 row
unsigned shouts_frame = ~0u;

// The KH2 enemy a mission's row places ("" none): khdays_missions_kh2_enemy makes the mission's plan each time
const std::string &row_kind(int row)
{
    auto it = row_kinds.find(row);
    if (it == row_kinds.end()) {
        char name[128];
        it = row_kinds.emplace(row, row >= 0 && khdays_missions_kh2_enemy(row, name, int(sizeof(name))) ? name : "")
                 .first;
    }
    return it->second;
}
} // namespace

// KHDAYS_TRACE_KH2_ENEMIES=3: every call an enemy makes, but those made every tick, with its arguments and the
// state it is in once the call is over
void trace_calls(ai::Calls &calls)
{
    for (const auto &[kind, number] : calls.handled()) {
        static const std::set<std::pair<unsigned, unsigned>> every_tick = {
            {0, 3},  {0, 4},  {0, 5},  {0, 16},  {0, 17},  {0, 18},  {1, 3},  {1, 4},  {1, 10}, {1, 14},
            {1, 15}, {1, 57}, {1, 60}, {1, 120}, {1, 147}, {1, 201}, {2, 23}, {1, 67}, {1, 68}, {1, 124}};
        if (every_tick.count({kind, number}) != 0)
            continue;
        const ai::Calls::Entry *entry = calls.find(kind, number);
        ai::Call inner = entry->fn;
        const unsigned k = kind, n = number;
        calls.set(kind, number, [inner, k, n](ai::Machine &machine, std::span<ai::Value> a) {
            std::string args;
            for (const ai::Value &v : a) {
                char one[40];
                if (v.tag == ai::Tag::Float)
                    std::snprintf(one, sizeof(one), " %g", double(v.f()));
                else
                    std::snprintf(one, sizeof(one), " %#x", v.bits);
                args += one;
            }
            const auto *who = static_cast<const Actor *>(machine.user());
            const auto it = who != nullptr ? by_actor.find(who) : by_actor.end();
            inner(machine, a);
            khdays_log("kh2 enemies: %08x in %s calls %u:%u%s", it != by_actor.end() ? it->second->days : 0u,
                       it != by_actor.end() ? it->second->state.c_str() : "?", k, n, args.c_str());
        });
    }
}

void shot_on_state(const std::string &name)
{
    static const std::string wanted = [] {
        const char *t = std::getenv("KHDAYS_SHOT_STATES");
        return t != nullptr && *t != '\0' ? "," + std::string(t) + "," : std::string();
    }();
    if (wanted.empty() || wanted.find("," + name + ",") == std::string::npos)
        return;
    const unsigned now = khdays_link_frame();
    for (const unsigned later : {0u, 15u, 30u, 60u, 120u, 240u})
        shot_frames.push_back(now + later);
    khdays_log("kh2 enemies: pictures asked from frame %u (%s)", now, name.c_str());
}
namespace {

void shots_due()
{
    const unsigned now = khdays_link_frame();
    for (auto it = shot_frames.begin(); it != shot_frames.end();)
        if (*it <= now) {
            khdays_host_shot_soon();
            it = shot_frames.erase(it);
        } else {
            ++it;
        }
}
} // namespace

// The allies moved on (KH2's 60 ticks a second, as the frames went) and drawn, in their battle's Days enemy's
// projection, as the 3D list closes; those whose battle is over (its Days enemy gone and its end played) go
void allies_close()
{
    for (auto it = allies.begin(); it != allies.end();) {
        Enemy &e = *it->second;
        const bool battle =
            enemies.count(e.anchor) != 0 ||
            std::any_of(ghosts.begin(), ghosts.end(), [&](const auto &g) { return g->days == e.anchor; });
        if (battle && !e.released) {
            ++it;
            continue;
        }
        if (tracing())
            khdays_log("kh2 enemies: %08x (%s) goes with its battle", e.days, e.kind->name.c_str());
        drop_attacks(e.days);
        std::vector<unsigned> holding;
        for (const auto &[object, h] : held)
            if (h.enemy == e.days)
                holding.push_back(object);
        for (const unsigned object : holding)
            let_go(object, "its ally gone");
        for (const std::uint32_t record : {e.attack_record, e.command_record})
            if (record != 0)
                mod().memory.remove(record);
        by_actor.erase(e.actor.get());
        friends.erase(e.days);
        it = allies.erase(it);
    }
    float camera[16];
    if (allies.empty() || !field_camera(camera))
        return;
    const unsigned now = khdays_link_frame();
    const unsigned per_frame = unsigned(std::max(1L, std::lround(khdays_link_frame_seconds() * 60.0)));
    for (auto &[id, g] : allies) {
        Enemy &e = *g;
        const unsigned ticks = e.ally_frame == 0     ? per_frame
                               : now >= e.ally_frame ? (now - e.ally_frame) * per_frame
                                                     : per_frame;
        e.ally_frame = now;
        const unsigned run = e.stops.empty() ? ticks : 0u; // held by a KH2 stop (6:46): drawn as it stands
        // what the shell knows of it: its place, the ground under it (its battle's room, from 2 m above it)
        KhdaysKh2Shell s{};
        std::copy(e.place, e.place + 3, s.place);
        s.above = -1.0F;
        s.standing = s.own = 1;
        s.hp = s.hp_max = 1;
        s.kind = -1;
        const float from[3] = {e.place[0], e.place[1] + 2.0F, e.place[2]};
        const float to[3] = {e.place[0], e.place[1] - kGroundSearchDown / kKh2PerDays, e.place[2]};
        float hit[3];
        if (khdays_kh2_shell_ray(e.anchor, from, to, hit))
            s.above = e.place[1] - hit[1];
        Vec3 place = e.actor->place(); // its own: nothing of Days's moves it
        if (s.above != -1.0F) {
            e.floor = place[1] + s.above * kKh2PerDays;
            e.floor_known = true;
        }
        put_on_carrier(e, place);
        Capture *turned = capture_of_form(e.days);
        const auto partner = friends.find(e.days);
        for (unsigned t = 0; t < run; ++t) {
            if (turned != nullptr)
                capture_tick(*turned, e, place); // a player's form: his pad moves it (KH2's action)
            if (partner != friends.end())
                friend_tick(partner->second, e, place); // a party member: KH2's party AI moves it
            one_tick(e, s, place);
            if (turned != nullptr) {
                keep_off_enemies(e, place); // the player's form: out of the enemies' bodies
                e.actor->set_place(place);
            }
            put_on_carrier(e, place); // on what carries it (1:18)
            pose(e);
            if (turned != nullptr)
                capture_blows(*turned, e, s); // its attack ranges' blows, his
            if (partner != friends.end())
                friend_blows(partner->second, e, s);
        }
        to_days(place, e.place);
        e.given = e.actor->place();
        const auto battle = enemies.find(e.anchor);
        if (battle != enemies.end() && battle->second->seen)
            std::copy(battle->second->pure, battle->second->pure + 16, e.pure);
        if (e.pure[15] == 0.0F && e.pure[11] == 0.0F)
            continue; // (no projection known yet: its battle's enemy not drawn)
        e.seen = true;
        e.drawn_frame = now;
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
        if (!khdays_models_draw_posed(e.kind->mdlx.c_str(), view, e.pure, (alpha << 16) | (1u << 24), bones.data(),
                                      e.pose.size(), e.actor->drawn_hidden()) &&
            !e.told_draw) {
            e.told_draw = true;
            khdays_log("kh2 enemies: %s's model cannot be drawn (the message above says why)", e.kind->name.c_str());
        }
        if (partner != friends.end())
            draw_weapon(partner->second, view, e.pure, alpha); // its weapon in its hand
    }
}
} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

extern "C" void khdays_kh2_enemies_made(int row, void *actor)
{
    const unsigned days = unsigned(KHDAYS_REF(actor));
    char name[128];
    forget(days);
    kind_of.erase(days);
    made_row[days] = row;
    if (!khdays_missions_kh2_enemy(row, name, int(sizeof(name))))
        return;
    kh2_rows = true;
    kind_of[days] = name;
    if (tracing())
        khdays_log("kh2 enemies: %08x made from row %d, a KH2 %s's stand-in", days, row, name);
}

extern "C" void khdays_kh2_enemies_gone(void *actor)
{
    const unsigned days = unsigned(KHDAYS_REF(actor));
    forget(days);
    if (appeared.erase(days) != 0)
        kind_of.erase(days); // a script's (1:0): its Days enemy another's once reused
}

extern "C" void khdays_kh2_enemies_area_left(void)
{
    reference_blow = 0; // the next area's first hit measures its KH2 enemies anew
    khdays_sound_shots_stop();
    khdays_kh2_shell_area_left();
    made_row.clear();
    row_kinds.clear();
    kh2_rows = false;
    drop_attacks(0);
    enemies.clear();
    ghosts.clear();
    allies.clear();
    allies_made = 0;
    accompanied.clear();
    for (const std::uint32_t v : vacuums)
        mod().memory.remove(v);
    vacuums.clear();
    signals.clear();
    kh2_mission.reset();
    by_actor.clear();
    kind_of.clear();
    appeared.clear();
    forget_teamwork();
    held.clear();
    for (auto &[object, hidden] : hidden_players) // (shown again as the next area holds them)
        hidden = false;
    // what the area's enemies offered or played for its players: the next area's objects may stand at the same
    // addresses (a mission played again), so none of it may carry over -- the deck's rows drawn as Days's own
    script_offer = {};
    top_offer = {};
    playing.reset();
    event_end = {};
    scope.reset();
    screen_shake.reset();
    screen_white = {};
    pictures.clear();
    drawn_weapons.clear();
    shell_draws.clear();
    local_player = 0;
    controls_off.clear();
    drain_frames = 0;
    command_image_of = 0;
    special_menu = {};
    last_days_damage = 0;
    local_taken = -1;
    shouts.clear();
    waiting_threads.clear();
    player_of_node.clear();
    players.clear();
    player_actors.clear();
    for (const auto &[object, s] : player_sheets) {
        mod().memory.remove(s.sheet);
        mod().memory.remove(s.part);
    }
    player_sheets.clear();
    recoils.clear();
    delivering = {};
    days_hit.reset();
    days_attack.reset();
    days_attack_owner = 0;
    watched = 0;
    camera_warped = false;
    lights.clear();
    map_hidden = false;
    the_field().force_leave = false;
}

extern "C" int khdays_kh2_enemies_held(void *actor)
{
    const Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    return e != nullptr && !e->released;
}
namespace khdays::kh2_enemies::shell {

namespace {

// FUN_1403c9d40, once a frame: the states' names shouted sent to every enemy -- with a delay each its own time at
// random below it (KH2's chance, DAT_140783ca0 x 0x10dcd), else its state of that name's shout routine run now
void send_shouts()
{
    static std::uint32_t chance = 1;
    const std::vector<Shout> sent = std::move(shouts);
    shouts.clear();
    for (const Shout &s : sent) {
        std::vector<unsigned> listening; // by their Days enemy (an ally's own number): a routine run may end one
        for (const auto &[actor, e] : by_actor)
            listening.push_back(e->days);
        for (const unsigned days : listening) {
            Enemy *e = find(days);
            if (e == nullptr) { // an ally: a boss's room objects hear it too (Shadow Stalker's floor, its swallow)
                const auto ally = allies.find(days);
                e = ally != allies.end() ? ally->second.get() : nullptr;
            }
            if (e == nullptr || e->dying)
                continue;
            if (s.delay > 0.0F) {
                chance *= 0x10dcdu;
                e->sent_state = s.name;
                e->sent_from = s.from;
                e->sent_after = float(chance) * (1.0F / 4294967296.0F) * s.delay;
            } else if (e->actor->states().shout(*e->actor, s.name, s.from) && tracing()) {
                khdays_log("kh2 enemies: %08x hears \"%s\" shouted", e->days, s.name.c_str());
            }
        }
    }
}

// The field's loose threads (2:20's), each once: KH2 runs them in a list of their own, apart from their objects'
// ticks (FUN_1403c5ec0). Not those of an enemy the Stop magic holds, nor of one whose end is over
void tick_loose()
{
    std::vector<const Actor *> actors;
    for (const auto &[a, e] : by_actor)
        if (e->actor->machine().has_loose())
            actors.push_back(a);
    for (const Actor *a : actors) {
        const auto it = by_actor.find(a); // (a thread run may have ended another)
        if (it != by_actor.end() && !it->second->released && !khdays_stop_held(KHDAYS_W(it->second->days)))
            it->second->actor->machine().tick_loose();
    }
}
} // namespace

} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

extern "C" void khdays_kh2_enemies_tick(void *actor, int delta)
{
    const unsigned days = unsigned(KHDAYS_REF(actor));
    const auto kind = kind_of.find(days);
    if (kind == kind_of.end())
        return;
    if (!shot_frames.empty())
        shots_due();
    // the frame before's KH2 ticks, each as KH2 orders its frame: its objects (each enemy's own ticks, done), its
    // loose threads, then the shouts told (once a frame here: after the first) -- the Sorcerer's turn-giving thread
    // moves the turn its shout gave on before its cubes hear the order, as KH2's must
    if (const unsigned frame = khdays_link_frame(); frame != shouts_frame) {
        shouts_frame = frame;
        if (drain_frames > 0)
            --drain_frames;
        const int steps = delta > 0 ? std::max(1, int(std::lround(2.0 * double(delta) / 4096.0))) : 0;
        if (!signals.empty()) { // told to every script, then the mission's
            const std::vector<std::pair<int, int>> sent = std::move(signals);
            signals.clear();
            for (const auto &[id, argument] : sent) {
                const ai::Value args[2] = {ai::Value::of_int(id), ai::Value::of_int(argument)};
                for (auto *list : {&enemies, &allies})
                    for (auto &[d, x] : *list)
                        if (!x->released)
                            x->actor->machine().raise(10, args);
                if (kh2_mission)
                    kh2_mission->actor->machine().raise(10, args);
            }
        }
        for (int t = 0; t < steps; ++t) {
            tick_loose();
            if (t == 0 && !shouts.empty())
                send_shouts();
            lights.tick();
            screen_shake_tick();
            screen_white_tick();
            laser_tick();
            if (kh2_mission) { // its script, its timer (KH2's 60 ticks a second); its pieces' time
                Kh2Mission &m = *kh2_mission;
                ++m.ticks;
                if (!m.over) {
                    m.actor->machine().tick();
                    // dk::timerTask: down to 0, or up to its most, while its pieces are up; its end tells the field
                    // 74 (the mission fails on it)
                    for (MissionPiece &p : m.pieces)
                        if (p.kind == 4 && m.shown && !m.time_up) {
                            p.value = p.down ? std::max(0, p.value - 1) : std::min(p.value + 1, std::max(p.most, 1));
                            if (p.down ? p.value == 0 : p.most > 0 && p.value >= p.most) {
                                m.time_up = true;
                                the_field().signal(74, 0);
                            }
                        }
                }
            }
        }
        if (steps == 0 && !shouts.empty())
            send_shouts();
    }
    KhdaysKh2Shell s;
    if (!khdays_kh2_shell_read(days, &s))
        return;
    Enemy *e = find(days);
    if (e != nullptr && e->released) {
        e->down = e->down || !s.standing;
        if (e->down && s.standing && s.hp > 0) {
            forget(days); // its Days enemy reused, set going again: a KH2 enemy anew
            e = nullptr;
        }
    }
    if (e == nullptr) {
        if (!s.standing || !s.own)
            return;
        float place[7];
        if (const Kind *bound = find_kind(mod().kinds, kind->second); bound != nullptr && !in_home(*bound, place)) {
            kind_of.erase(kind); // its stand-in plays, for good
            return;
        }
        KindFiles *k = files_of(kind->second);
        if (k == nullptr) {
            kind_of.erase(kind); // its stand-in plays, for good
            return;
        }
        e = start(days, *k, s);
    }
    if (e->released || khdays_stop_held(actor) || !e->stops.empty())
        return;
    // two of KH2's ticks to one of Days's field steps (0x1000 a 30th of a second)
    static bool told_step = false;
    if (!told_step && tracing()) {
        told_step = true;
        khdays_log("kh2 enemies: the field's step is %d", delta);
    }
    const int ticks = delta > 0 ? std::max(1, int(std::lround(2.0 * double(delta) / 4096.0))) : 0;
    // run into a wall: Days's collision moved it off the place it was given (by 1 cm or more). KH2 stops its
    // steering then (FUN_140421450: its wall's push at + 0xa78, + 0x1c 0): a lunge runs until a wall stops it.
    // The push a tick along the ground is its + 0xa78 (KH2's actor's update writes it, FUN_1403b9420; 6:24 reads it), and
    // a wall ends a hit's slide too (FUN_1403b9420 clears + 0xd24 .. + 0xd30).
    Vec3 pressed{};
    if (e->placed) {
        const float dx = s.place[0] - e->place[0], dz = s.place[2] - e->place[2];
        if (dx * dx + dz * dz >= 0.01F * 0.01F) {
            const Vec3 now = to_kh2(s.place), was = to_kh2(e->place);
            const float per = ticks > 0 ? 1.0F / float(ticks) : 1.0F;
            pressed = {(now[0] - was[0]) * per, 0.0F, (now[2] - was[2]) * per};
            e->actor->slide_speed = 0.0F;
            if (e->actor->steer_speed > 0.0F) {
                e->actor->steer_speed = 0.0F;
                if (trace_level() >= 2)
                    khdays_log("kh2 enemies: %08x pushed %.2f off its place: its steering stops", days,
                               double(std::sqrt(dx * dx + dz * dz)));
            }
        }
    }
    for (std::uint32_t i = 0; i < 3; ++i)
        e->actor->set_f(field::kPressed + 4 * i, pressed[i]);
    the_field().health(*e->actor);
    Vec3 place = to_kh2(s.place);
    // another script put it elsewhere since the last frame (1:148 on its handle: the cubes put the Sorcerer they
    // made where they are): that place, not Days's
    bool moved = false;
    if (e->placed) {
        const Vec3 now = e->actor->place();
        if (std::fabs(now[0] - e->given[0]) + std::fabs(now[1] - e->given[1]) + std::fabs(now[2] - e->given[2]) >
            0.01F) {
            place = now;
            moved = true;
        }
    }
    // KH2 carries it up and down itself (its jumps, falls, motions in the air); the height Days gives it is not KH2's:
    // between two steps Days lowers a stand-in in the air (by 0.148 a frame, its own fall), and Roxas's blows throw it
    // up. The height it was given is kept, but for Days lifting it out of the ground: up to the ground under the place
    // it was given, no higher (`above` is measured from there, below 0 sunk into it). Before, a KH2 enemy flying (class
    // 5, no fall of its own) stayed where the blows had thrown it: Vexen 4 m up against the coliseum's wall
    if (e->placed && !moved) {
        float height = e->place[1];
        if (s.place[1] > height && s.above < 0.0F && s.above != -1.0F)
            height = std::min(s.place[1], e->place[1] - s.above);
        if (trace_level() >= 2 && std::fabs(s.place[1] - height) > 0.05F)
            khdays_log("kh2 enemies: %08x kept at the height %.2f, Days's %.2f (%.2f above its ground)", days,
                       double(height), double(s.place[1]), double(s.above));
        const float kept[3] = {s.place[0], height, s.place[2]};
        place[1] = to_kh2(kept)[1];
    }
    if (s.above != -1.0F) { // (-1: no ground under it; below 0: sunk into it) -- from the place it was given
        e->floor = (e->placed ? to_kh2(e->place)[1] : place[1]) + s.above * kKh2PerDays;
        e->floor_known = true;
    }
    // set going by a wave: on its floor, as KH2's area spawns put their objects on their points on the floor --
    // Days's stand-in is set going above its own (half a metre: `above`), and the fall would play the landing's motion
    // in the place of its first (Demyx's data battle opens with 209, whose frame 132 calls his water clones)
    // (Days sets its stand-in going sunk into its ground -- `above` below 0, the floor read then half a metre up --
    // and lifts it out at its next frame: the first frame it stands out of it)
    if (e->on_floor_first && e->floor_known && s.above >= 0.0F) {
        if (place[1] < e->floor) {
            if (tracing())
                khdays_log("kh2 enemies: %08x set going %.1f cm above its floor: put on it", days,
                           double(e->floor - place[1]));
            place[1] = e->floor;
        }
        e->on_floor_first = false;
    }
    put_on_carrier(*e, place); // (its floor what carries it, not Days's ground under it)
    for (int t = 0; t < ticks; ++t) {
        one_tick(*e, s, place);
        for (auto &[object, h] : held)
            if (h.enemy == days)
                hold_tick(*e, object, h);
        for (auto h = held.begin(); h != held.end();) {
            const auto next = std::next(h);
            if (h->second.enemy == days && h->second.idle)
                let_go(h->first, "its motion back to its own idle (range 2)");
            h = next;
        }
        // a carry with its flag bit 0 ends as KH2's actor's update ends it (FUN_1403b4a12): its carrier gone or no
        // longer holding it (its + 0x18c bit 15, its motion's range 13) -- Scar, put on the player he pins, comes off
        // as he throws him; 1:161 then says so and his catch goes on
        if (e->actor->u32(field::kCarrier) != 0 && (e->carry_flags & 1) != 0) {
            bool keeps = false;
            if (e->carried_by != 0) { // a KH2 object: there, not dying, its own range 13 on -- or no motion of its
                // own yet: one made with the battle, where KH2's has stood since its area was loaded, its motion on
                const Enemy *c = enemy_or_ally(e->carried_by);
                keeps = c != nullptr && !c->dying &&
                        (c->actor->motion.id < 0 || (c->actor->u32(field::kStatus) & bit::kKeepsCarried) != 0);
            } else {
                const auto p = players.find(e->carrier);
                keeps = e->carrier != 0 && held.count(e->carrier) != 0 && p != players.end() && p->second &&
                        (p->second->u32(field::kStatus) & bit::kKeepsCarried) != 0;
            }
            if (!keeps) {
                if (tracing())
                    khdays_log("kh2 enemies: %08x comes off %08x (its carry's flag bit 0, its carrier's range 13 over)",
                               e->days, e->carried_by != 0 ? e->carried_by : e->carrier);
                e->actor->set_u32(field::kCarrier, 0);
                e->carrier = 0;
                e->carried_by = 0;
            }
        }
        put_on_carrier(*e, place); // on what carries it
        pose(*e);
        attack(*e, s);
        attack_objects(*e);
        attack_offers(*e);
    }
    zone_offer(*e);
    take_command(*e);
    // its own script entered its "dead" with health left (Zexion's illusion, its prisoner given back): KH2's end of it,
    // counted as defeated (1:328's way: its Days enemy defeated, its script left to end it) -- before, it stood faded
    // out for good, a wave's enemy the mission still waited on
    if (const StateEntry *now = e->actor->states().current();
        now != nullptr && now->name == "dead" && !e->dying && !e->dead_marked)
        the_field().dead_mark(*e->actor);
    // KH2's "leave" (the enemy far from the player: its scripts' 1:56 distances): it fades out and waits for KH2's
    // spawner to bring it back. Days's field has none, so once it has faded out it comes back where its wave placed
    // it, its script anew, its Days HP as it is.
    const StateEntry *state_now = e->actor->states().current();
    const bool left = state_now != nullptr && state_now->name == "leave" && !e->dying && e->actor->fade <= 0.0F &&
                      (e->actor->u32(field::kFade) & bit::kFadeHeld) != 0;
    if (left && e->made) {
        // a script's: gone for good -- its Days enemy sent away as a spawner sends one away (no defeat, no reward);
        // before, it came back as a wave's does, and Luxord's cards grew to dozens on the floor
        if (!e->sent_away) {
            e->sent_away = true;
            const int sent = khdays_enemies_remove(days);
            if (tracing())
                khdays_log("kh2 enemies: %08x (made by a script) left: %s", days,
                           sent ? "its Days enemy sent away" : "its Days enemy already leaving");
        }
    } else if (left) {
        KhdaysKh2Shell back = s;
        std::copy(e->home, e->home + 3, back.place);
        // where its wave placed it when the player is near it (10 m), else 4 m from the player on its way there: it
        // would only leave again (KH2's spawner brings its enemies back as the player comes near)
        if (Object *p = the_field().player(*e->actor)) {
            float at[3];
            to_days(p->place(), at);
            const float dx = e->home[0] - at[0], dz = e->home[2] - at[2], d = std::sqrt(dx * dx + dz * dz);
            if (d > 10.0F) {
                back.place[0] = at[0] + dx / d * 4.0F;
                back.place[1] = at[1];
                back.place[2] = at[2] + dz / d * 4.0F;
            }
        }
        KindFiles *k = e->kind;
        const std::int64_t health = e->health, health_most = e->health_most; // kept: its Days fields show its share
        const int shown_most = e->shown_most;
        const double health_share = e->health_share, start_share = e->start_share;
        const std::int64_t record_most = e->record_most;
        forget(days);
        e = start(days, *k, back);
        e->health_share = health_share;
        e->health = health;
        e->health_most = health_most;
        e->shown_most = shown_most;
        e->record_most = record_most;
        e->start_share = start_share;
        place = to_kh2(back.place);
        if (tracing())
            khdays_log("kh2 enemies: %08x comes back at %.2f %.2f %.2f", days, double(back.place[0]),
                       double(back.place[1]), double(back.place[2]));
    }
    to_days(place, e->place);
    e->placed = true;
    e->given = e->actor->place();
    e->radius = s.radius;
    if (e->large) {
        KhdaysKh2Shell now = s;
        std::copy(e->place, e->place + 3, now.place);
        const float body = footprint(*e, now);
        if (body > 0.0F && std::fabs(body - e->body) > 0.05F) {
            khdays_kh2_shell_size(days, body, body, nullptr, nullptr);
            if (tracing() && std::fabs(body - e->body) > 0.5F)
                khdays_log("kh2 enemies: %08x's body reaches %.2f (its sphere's radius)", days, double(body));
            e->body = body;
        }
    }
    khdays_kh2_shell_place(days, e->place, days_turn(e->actor->yaw()));
}

extern "C" unsigned khdays_kh2_enemies_lock_point(unsigned actor, unsigned point)
{
    Enemy *e = find(actor);
    KhdaysKh2Shell s;
    if (e == nullptr || e->dying || !khdays_kh2_shell_read(actor, &s))
        return point;
    for (const Shape &sh : e->kind->shapes)
        if ((sh.kind & 0xff) == 7) { // its aim point (0x307)
            if (e->pose.empty())
                pose(*e);
            float at[3];
            days_point(*e, s, sh.bone, sh.at, at);
            const unsigned put = khdays_kh2_shell_point(at);
            return put != 0 ? put : point;
        }
    return point;
}
namespace khdays::kh2_enemies::shell {

namespace {

// The Days sounds a stand-in makes of its own (its class's MON_ archive, the enemies' common appearing and defeat:
// docs/RUNTIME_MAP.md section 37): those KH2's own sounds take the place of
bool stand_ins_own(const char *identifier)
{
    return std::strncmp(identifier, "seqarc/MON_", 11) == 0 ||
           std::strncmp(identifier, "seqarc/COMMON_SYS/se_all_enemy_", 31) == 0;
}
} // namespace

} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

extern "C" int khdays_kh2_enemies_quiet(unsigned archive, unsigned entry, const void *place)
{
    if ((enemies.empty() && ghosts.empty()) || place == nullptr || khdays_sound_bridge_archive() == nullptr)
        return 0;
    char identifier[160];
    if (!khdays_sound_archive_identifier(khdays_sound_bridge_archive(), KHDAYS_SOUND_SEQUENCE_ARCHIVE_ENTRY,
                                         int(archive), int(entry), identifier, int(sizeof(identifier))) ||
        !stand_ins_own(identifier))
        return 0;
    std::int32_t fixed[3];
    std::memcpy(fixed, place, sizeof(fixed));
    const float at[3] = {float(fixed[0]) / 4096.0F, float(fixed[1]) / 4096.0F, float(fixed[2]) / 4096.0F};
    const Enemy *nearest = nullptr;
    float best = 0.0F;
    const auto look = [&](const Enemy &e) {
        const float d = std::hypot(at[0] - e.place[0], at[2] - e.place[2]);
        if (nearest == nullptr || d < best)
            nearest = &e, best = d;
    };
    for (const auto &[days, e] : enemies)
        look(*e);
    for (const auto &g : ghosts)
        look(*g);
    const bool quiet = nearest != nullptr && best <= std::max(kNearEnough, nearest->radius); // its body's
    if (tracing())
        khdays_log("kh2 enemies: %s at %.3f %.3f %.3f, %.3f from %08x: %s", identifier, double(at[0]), double(at[1]),
                   double(at[2]), double(best), nearest != nullptr ? nearest->days : 0u,
                   quiet ? "not heard (KH2's own sounds play)" : "heard");
    return quiet ? 1 : 0;
}

extern "C" int khdays_kh2_enemies_fits(unsigned spawner, int slot, unsigned actor)
{
    if (!kh2_rows)
        return 1; // no KH2 enemy in this area: Days's own choice
    const auto made = made_row.find(actor);
    const std::string &has = row_kind(made != made_row.end() ? made->second : -1);
    const std::string &want = row_kind(khdays_enemies_slot_row(spawner, slot));
    if (tracing() && has != want)
        khdays_log("kh2 enemies: %08x (%s) left for spawner %08x's slot %d (%s)", actor,
                   has.empty() ? "Days's" : has.c_str(), spawner, slot, want.empty() ? "Days's" : want.c_str());
    return has == want ? 1 : 0;
}

extern "C" int khdays_kh2_enemies_ready(const char *name)
{
    Mod &m = mod();
    const Kind *k = find_kind(m.kinds, name);
    return k != nullptr && k->covered;
}
