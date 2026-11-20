// An enemy set going, posed and moved a tick: its motions, its walls, its sounds and effects cast.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

// The element of KH2's keyblade's effects YS::WEAPON::locus starts: its swing's trail (W_EX010.a.us's 10: the swing
// trail's command 0xc8 between its two points, 0.65 and 3.7 times its bone 2's length out along the blade)
constexpr int kSwingTrail = 10;

// The motion's state ranges, each a bit of + 0x18c while the motion's time lies in it (OpenKH's ANB page: range 0
// grounded at bit 2, 1 in the air 3, 3 no gravity 5, 9 jumping or landing 9, 24 no gravity keeping its speed 6, 30 24
// ...): what the scripts read -- 1:60's "off the ground" is bits 5 and 24 (FUN_1403ba9a0: + 0x18c & 0x1000020), 2:5's
// and 1:78's bit 9. A Days hit's reaction holds bit 9 too.
constexpr std::pair<int, int> kStateRanges[] = {
    {0, 2},  {1, 3},   {2, 4},   {3, 5},   {9, 9},   {11, 10}, {12, 11}, {13, 15}, {14, 19}, {15, 16}, {21, 18},
    {24, 6}, {27, 22}, {29, 23}, {30, 24}, {31, 25}, {32, 26}, {34, 27}, {38, 28}, {39, 29}, {40, 30}, {41, 31}};
} // namespace

// The Days enemy whose shell answers an enemy's questions of Days's field (its room's collision, its players): its
// own; an ally's battle's
unsigned shell_of(const Enemy &e)
{
    return e.anchor != 0 ? e.anchor : e.days;
}

// a KH2 enemy or room object by its handle (its Days actor; kAllyIds and on for those not enemies); null gone
Enemy *enemy_or_ally(unsigned days)
{
    if (const auto e = enemies.find(days); e != enemies.end())
        return e->second.get();
    const auto a = allies.find(days);
    return a != allies.end() ? a->second.get() : nullptr;
}

// The Days player's object a script's player object stands for, 0 none
unsigned days_object(const Object &o)
{
    for (const auto &[object, mine] : players)
        if (mine.get() == &o)
            return object;
    return 0;
}

// A kind bound to its KH2 map (Kind::home) is made only in a room made from that map: whether the playing mission's
// room is one (khdays_missions_room_source: an added room's room.txt, a replaced room's change), its placement into
// `place` (the studio's: scale, the turns about x, y, z in degrees, the move); a kind bound to none, always (`place`
// left as it is). Told once a kind and room when not.
bool in_home(const Kind &kind, float place[7])
{
    if (kind.home.empty())
        return true;
    float at[7] = {1, 0, 0, 0, 0, 0, 0};
    char *source = khdays_missions_room_source(at);
    const bool known = source != nullptr;
    std::string want = "kh2:" + kind.home, have = known ? source : "";
    std::free(source);
    std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::transform(have.begin(), have.end(), have.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (known && have == want) {
        std::copy(at, at + 7, place);
        return true;
    }
    static std::set<std::string> told;
    if (told.insert(kind.name + "|" + have).second)
        khdays_log("kh2 enemies: %s fights in its own room, KH2's %s, and this room is %s: its stand-in plays",
                   kind.name.c_str(), kind.home.c_str(), known ? have.c_str() : "not made from a KH2 map");
    return false;
}
namespace {

// A point of a KH2 map's area (KH2's world, cm, y down: its spawns' places) in the field as the shell keeps it
// (to_kh2 of Days's place): the map's model axes (x, -y, -z), then the room's placement -- scale, the turns about x,
// y and z, the move (Days's units; the studio's placement_matrix) -- then to_kh2. A way (`way` true) is turned alone.
Vec3 map_point(const float place[7], const Vec3 &w, bool way = false)
{
    float p[3] = {w[0], -w[1], -w[2]};
    if (!way)
        for (float &v : p)
            v *= place[0];
    const float k = kPi / 180.0F;
    const float ax = place[1] * k, ay = place[2] * k, az = place[3] * k;
    float q[3] = {p[0], std::cos(ax) * p[1] - std::sin(ax) * p[2], std::sin(ax) * p[1] + std::cos(ax) * p[2]};
    p[0] = std::cos(ay) * q[0] + std::sin(ay) * q[2], p[1] = q[1], p[2] = -std::sin(ay) * q[0] + std::cos(ay) * q[2];
    q[0] = std::cos(az) * p[0] - std::sin(az) * p[1], q[1] = std::sin(az) * p[0] + std::cos(az) * p[1], q[2] = p[2];
    if (way)
        return days_way({q[0], q[1], q[2]});
    const float d[3] = {q[0] + place[4], q[1] + place[5], q[2] + place[6]};
    return to_kh2(d);
}
} // namespace

void forget(unsigned days)
{
    auto it = enemies.find(days);
    if (it == enemies.end())
        return;
    drop_attacks(days); // Days makes the next wave's enemy at its place
    for (const std::uint32_t record : {it->second->attack_record, it->second->command_record})
        if (record != 0)
            mod().memory.remove(record);
    it->second->attack_record = it->second->command_record = 0;
    std::vector<unsigned> holding;
    for (const auto &[object, h] : held)
        if (h.enemy == days)
            holding.push_back(object);
    if (playing && playing->enemy == days)
        end_event();
    for (const unsigned object : holding)
        let_go(object, "its enemy gone");
    Enemy &e = *it->second;
    if (e.dying && !e.released && e.seen) {
        static bool hooked = false;
        if (!hooked) {
            khdays_g3d_add_swap_hook(ghosts_close);
            hooked = true;
        }
        ghosts.push_back(std::move(it->second));
    } else {
        by_actor.erase(e.actor.get());
    }
    enemies.erase(it);
}

Enemy *start(unsigned days, KindFiles &k, const KhdaysKh2Shell &s, bool made)
{
    // a Days enemy set going again before the shell let its last go (its slot reused: Demyx's next water clone made
    // where a clone Wild Dance ended stood): that one let go first, as Days's own retiring would (its actor's entry
    // gone with it, not left to a freed enemy)
    if (enemies.count(days) != 0)
        forget(days);
    auto e = std::make_unique<Enemy>();
    e->days = days;
    e->kind = &k;
    e->motions = std::make_unique<SetMotions>(k.set, k.player_set);
    e->actor = std::make_unique<Actor>(k.script, mod().memory, mod().calls, k.name);
    e->player = std::make_unique<kh2::MotionPlayer>(*k.skeleton);
    e->slot = khdays_kh2_shell_attack_slot(days);
    Actor &a = *e->actor;
    a.world = &the_field();
    a.motions = e->motions.get();
    a.set_u32(field::kEntry, entry_record(k.name));
    a.set_place(to_kh2(s.place));
    e->on_floor_first = !made; // (set going by a wave: on its floor at its first tick)
    e->made = made;
    std::copy(s.place, s.place + 3, e->home);
    Enemy *raw = e.get();
    by_actor[&a] = raw;
    enemies[days] = std::move(e);
    if (Object *p = the_field().player(a)) {
        const Vec3 to = p->place(), at = a.place();
        if (to[0] != at[0] || to[2] != at[2])
            a.set_yaw(std::atan2(to[0] - at[0], to[2] - at[2]));
    }
    raw->battle_at = a.place();
    raw->battle_turn = a.yaw();
    if (tracing())
        a.states().entered = [days, raw](const std::string &name) {
            raw->state = name;
            khdays_log("kh2 enemies: %08x enters %s", days, name.c_str());
            shot_on_state(name);
            if (name == "leave") { // where it and the player it went for stood (it leaves when far from it)
                const Vec3 at = raw->actor->place();
                Object *p = the_field().player(*raw->actor);
                const Vec3 to = p != nullptr ? p->place() : Vec3{};
                khdays_log("kh2 enemies: %08x leaves at %.0f %.0f %.0f, the player at %.0f %.0f %.0f (KH2's cm)%s",
                           days, double(at[0]), double(at[1]), double(at[2]), double(to[0]), double(to[1]),
                           double(to[2]), the_field().on_screen(*raw->actor) ? ", on the screen" : "");
            }
        };
    // its body's size, KH2's: its body shapes (kind 1 -- Sora's a cylinder 35 round, the Soldier's 45; Cerberus's
    // spheres on its torso and legs, the torso's 2.4 m), the largest one's radius the Days enemy's collision sphere,
    // sitting on the ground: what keeps the players off it and what Days's hits find. Kind 0x300 (90 round for the
    // small ones, 5.5 m Cerberus's) is not its body: sized by it, Cerberus kept Roxas out of its paws' reach.
    float body = 0.0F;
    for (const Shape &sh : k.shapes)
        if (sh.kind == 0x0001)
            body = std::max(body, sh.radius / kKh2PerDays);
    if (body > 0.0F) {
        float was_radius = 0.0F, was_centre = 0.0F;
        khdays_kh2_shell_size(days, body, body, &was_radius, &was_centre);
        if (tracing())
            khdays_log("kh2 enemies: %08x's body %.2f round (its stand-in's %.2f, its centre %.2f up)", days,
                       double(body), double(was_radius), double(was_centre));
    }
    if (const std::uint32_t entry = a.u32(field::kEntry); entry != 0) {
        raw->large = (mod().memory.u32(entry + 4) & 0xffu) == 0xcu;
        // a pirate (its entry's + 0x48 bit 5) out of the moonlight, as every Days floor is (1:40): its parts 10 to 19
        // faded out, as YS::OBJ::exec_pirate leaves them at Place 0
        if ((mod().memory.u32(entry + 0x48) & 0x20u) != 0)
            a.faded_parts = 0xffc00u;
    }
    // its script set going: at once, or -- made by another's script (1:0) -- at its first tick, as KH2's object's
    // script first runs as the object is updated (FUN_1403e1240 makes its machine and runs nothing; FUN_1403e1c80 runs
    // it), after its maker has given it what it gives it (1:218: the Hydra's heads their necks, 11 to 13)
    // the first argument its KH2 battle spawn gives it (YS::OBJ's UnitArg[1], 1:39(self, 1)): Oogie Boogie's 1, without
    // which his script only idles; one another's script makes gets its maker's (1:218) instead
    if (const Kind *kind = find_kind(mod().kinds, k.name); kind != nullptr && !made)
        a.set_u32(field::kUnitArgs + 4, std::uint32_t(kind->unit_arg));
    {
        const float limit = 100.0F; // its karma's limit (YS::ENEMY's constructor; 2:76 sets another)
        std::uint32_t bits = 0;
        std::memcpy(&bits, &limit, 4);
        a.set_u32(field::kKarmaLimit, bits);
    }
    raw->started = !made;
    if (!made)
        a.start();
    // the effects its model starts as it is made (YS::OBJDATA::start_model_effect, FUN_1403e6470): Hades's hair's
    // flame, Scar's ghosts' own
    const int own = k.model_fx ? model_effects(a.u32(field::kEntry)) : 0;
    for (int n = 0; n < own; ++n)
        cast(*raw, n, 0, false, k.model_fx.get());
    if (tracing())
        khdays_log("kh2 enemies: %08x set going as %s, attack slot %d, %d effects of its model", days, k.name.c_str(),
                   raw->slot, own);
    // the objects KH2's area places with it that are no enemies (Kind::with: the Hydra's Phil, Pegasus and Phil's urn),
    // allies of its battle: where KH2 places them from it, turned as the way from it to its player is turned here
    // (its facing, set above) from KH2's; once a Days enemy and kind (it comes back after KH2's "leave" as itself)
    if (const Kind *kind = find_kind(mod().kinds, k.name);
        kind != nullptr && !kind->with.empty() && accompanied.insert({days, k.name}).second) {
        const float turn = a.yaw(), c = std::cos(turn), sn = std::sin(turn);
        const Vec3 at = a.place();
        for (const Companion &with : kind->with) {
            // (an enemy's kind is made as the Days enemy it stands in: none but the boss's own others, Final Xemnas's
            // hidden one, whose script hides it at once)
            KindFiles *f = enemy_entry(with.name) && with.name != k.name ? nullptr : files_of(with.name);
            if (f == nullptr) {
                khdays_log("kh2 enemies: %s's %s not made (not one of the mod's kinds, or an enemy)", k.name.c_str(),
                           with.name.c_str());
                continue;
            }
            const Vec3 to = {at[0] + with.x * c + with.z * sn, at[1] + with.y, at[2] - with.x * sn + with.z * c};
            start_ally(*f, to, with.yaw + turn, days, false, with.argument);
        }
    }
    // its room's objects (Kind::room: the ballroom's pillars and floor, Shadow Stalker's) where KH2 places them on its
    // map, the room made from it placing them as it places the map (map_point); once a Days enemy and kind
    float place[7] = {1, 0, 0, 0, 0, 0, 0};
    if (const Kind *kind = find_kind(mod().kinds, k.name); kind != nullptr && !kind->room.empty() &&
                                                           in_home(*kind, place) &&
                                                           accompanied.insert({days, k.name + "#room"}).second) {
        for (const Companion &piece : kind->room) {
            KindFiles *f = files_of(piece.name);
            if (f == nullptr) {
                khdays_log("kh2 enemies: %s's room object %s not made (not one of the mod's covered kinds)",
                           k.name.c_str(), piece.name.c_str());
                continue;
            }
            const Vec3 at = map_point(place, {piece.x, piece.y, piece.z});
            const Vec3 way = map_point(place, {std::sin(piece.yaw), 0.0F, std::cos(piece.yaw)}, true);
            Enemy *made_piece = start_ally(*f, at, std::atan2(way[0], way[2]), days, false, piece.argument);
            if (tracing())
                khdays_log("kh2 enemies: %08x's room object %s (%08x) at %.0f %.0f %.0f (KH2's %.0f %.0f %.0f)", days,
                           piece.name.c_str(), made_piece->days, double(at[0]), double(at[1]), double(at[2]),
                           double(piece.x), double(piece.y), double(piece.z));
        }
    }
    if (const Kind *kind = find_kind(mod().kinds, k.name); kind != nullptr && !kind->mission.empty() && !kh2_mission)
        start_mission(kind->mission);
    return raw;
}

// An ally (allies) of kind `k` made at `place` (KH2's world) turned by `yaw`, in the battle of the Days enemy
// `anchor`: its script set going at once, its model's own effects started as an enemy's
Enemy *start_ally(KindFiles &k, const Vec3 &place, float yaw, unsigned anchor, bool made, int argument)
{
    auto e = std::make_unique<Enemy>();
    e->days = kAllyIds + ++allies_made;
    e->anchor = anchor;
    e->kind = &k;
    e->motions = std::make_unique<SetMotions>(k.set, k.player_set);
    e->actor = std::make_unique<Actor>(k.script, mod().memory, mod().calls, k.name);
    e->player = std::make_unique<kh2::MotionPlayer>(*k.skeleton);
    Actor &a = *e->actor;
    a.world = &the_field();
    a.motions = e->motions.get();
    a.set_u32(field::kEntry, entry_record(k.name));
    a.set_place(place);
    a.set_yaw(yaw);
    to_days(place, e->place);
    std::copy(e->place, e->place + 3, e->home);
    e->placed = true;
    e->given = place;
    Enemy *raw = e.get();
    by_actor[&a] = raw;
    allies[raw->days] = std::move(e);
    static bool hooked = false;
    if (!hooked) {
        khdays_g3d_add_swap_hook(allies_close);
        hooked = true;
    }
    if (tracing()) {
        const unsigned id = raw->days;
        a.states().entered = [id, raw](const std::string &name) {
            raw->state = name;
            khdays_log("kh2 enemies: %08x enters %s", id, name.c_str());
        };
        khdays_log("kh2 enemies: %08x made, a %s with %08x's battle (not an enemy) at %.0f %.0f %.0f (KH2's cm)", id,
                   k.name.c_str(), anchor, double(place[0]), double(place[1]), double(place[2]));
    }
    // its spawn's first argument (YS::OBJ's UnitArg[1], which 1:39(self, 1) gives its script): a room object's own
    a.set_u32(field::kUnitArgs + 4, std::uint32_t(argument));
    raw->started = !made; // (as an enemy a script makes: start)
    if (!made)
        a.start();
    const int own = k.model_fx ? model_effects(a.u32(field::kEntry)) : 0;
    for (int n = 0; n < own; ++n)
        cast(*raw, n, 0, false, k.model_fx.get());
    friend_start(*raw); // (a party member: KH2's party AI moves it)
    return raw;
}

// A motion's pose at a time with its root's travel taken out (MotionPrototype0::getPosition's inverse first): the
// actor's place carries that travel (Actor::step), as KH2's actor does (model_kh2_motions.cpp takes it out the same
// way)
std::vector<kh2::Mat4> pose_at(kh2::MotionPlayer &player, const kh2::Motion &motion, float ticks)
{
    const kh2::Mat4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    player.evaluate(motion, ticks, identity, nullptr);
    std::vector<kh2::Mat4> out = player.joints();
    // column-major for column vectors lies as row-major for row vectors: "then" is the row vectors' product
    float taken[16];
    inverse_affine(motion.root_motion(ticks).data(), taken);
    for (kh2::Mat4 &j : out) {
        float moved[16];
        row_matrix_mul(j.data(), taken, moved);
        std::copy(moved, moved + 16, j.begin());
    }
    return out;
}

// The pose now, once a tick (MOTION::calc): the motion at its time; while a blend runs (the motion standing at its
// start frame), the pose shown when it started taken each tick's blend toward that frame -- blend_keep of it left
void pose(Enemy &e)
{
    if (const int c = e.motions->capture_id(); c >= 0 && e.actor->motion.id != c) {
        // its capture over: another motion of its own played (KH2's MOTION::start without the capturer's set)
        e.motions->end_capture();
        e.capture_weapon = nullptr;
        e.actor->set_bits(field::kStatus, bit::kOtherMotion, false);
    }
    const Actor &a = *e.actor;
    if (a.motion.started != e.posed_started) {
        e.posed_started = a.motion.started;
        e.blend_from = e.pose; // the pose shown the tick before (Context1)
    }
    const kh2::Motion *now = a.motion.id >= 0 ? e.motions->motion(a.motion.id) : nullptr;
    if (now == nullptr) {
        e.pose.clear();
        return;
    }
    e.pose = pose_at(*e.player, *now, a.motion.time);
    if (a.motion.blend <= 0.0F || e.blend_from.size() != e.pose.size())
        return;
    const float w = a.motion.blend_keep; // the pose before's share
    for (std::size_t j = 0; j < e.pose.size(); ++j)
        for (std::size_t c = 0; c < 16; ++c)
            e.pose[j][c] = e.pose[j][c] * (1.0F - w) + e.blend_from[j][c] * w;
}

// A point on a bone of the pose (KH2's model space, cm, y up) in Days's world. A shape on no bone is in the actor's
// own axes (KH2's world's: y down), which are the model's turned half round its x: its y and its z the other way (the
// Rabid Dog's rush hits 170 ahead of it, 55 up: (0, -55, -170); its aim points, 0x307, are all above it)
void days_point(const Enemy &e, const KhdaysKh2Shell &s, int bone, const Vec3 &at, float out[3])
{
    Vec3 p = {at[0], -at[1], -at[2]};
    if (bone != kNoBone && bone >= 0 && std::size_t(bone) < e.pose.size()) {
        const kh2::Mat4 &m = e.pose[std::size_t(bone)];
        p = {m[0] * at[0] + m[4] * at[1] + m[8] * at[2] + m[12], m[1] * at[0] + m[5] * at[1] + m[9] * at[2] + m[13],
             m[2] * at[0] + m[6] * at[1] + m[10] * at[2] + m[14]};
    }
    const Vec3 t = turned({p[0], 0.0F, p[2]}, days_turn(e.actor->yaw())); // (the model turned as Days draws it)
    out[0] = s.place[0] + t[0] / kKh2PerDays;
    out[1] = s.place[1] + p[1] / kKh2PerDays;
    out[2] = s.place[2] + t[2] / kKh2PerDays;
}

// A KH2 tick of the enemy: its script (not while a reaction plays), its motion and travel; KH2's gravity off the ground
// (the room's collision puts it back on it), a jump's landing
// KH2's sound `id` of the enemy's kind heard where the enemy was last drawn
void sound(const Enemy &e, int id)
{
    const auto it = e.kind->sounds.find(id);
    const bool heard = it != e.kind->sounds.end() && khdays_sound_shot_start(it->second, 1.0F, e.pan) != 0;
    if (tracing())
        khdays_log("kh2 enemies: %08x's sound %d%s", e.days, id,
                   heard ? "" : " (not heard: the mod has no kh2/se file)");
}

// Element `number` of the kind's effects started on the enemy (`player`: cast by the motion of its set that player
// plays in its hands, whose bones -- KH2's player's, his weapon's when `on_weapon` -- its element's are; `files` the
// effects it is of, null the kind's); its handle, 0 none
std::uint32_t cast(Enemy &e, int number, unsigned player, bool on_weapon, const KindFiles *files)
{
    static std::uint32_t next_handle = 0;
    const KindFiles &k = files != nullptr ? *files : *e.kind;
    const auto it = k.fx_elements.find(number);
    if (it == k.fx_elements.end() || it->second.effect >= k.fx.size() || it->second.effect >= k.fx_pages.size())
        return 0;
    try {
        Enemy::Running r;
        r.fx = std::make_unique<kh2::EffectInstance>(k.fx[it->second.effect], it->second.emitter, e.fx_seed++);
        r.element = &it->second;
        r.player = player;
        r.on_weapon = on_weapon;
        r.files = files;
        r.handle = ++next_handle != 0 ? next_handle : ++next_handle;
        const std::uint32_t handle = r.handle;
        e.fx.push_back(std::move(r));
        if (tracing())
            khdays_log("kh2 enemies: %08x casts %s's element %d (effect %zu, emitter %zu, bone %d, sound %d%s) as %08x",
                       e.days, k.name.c_str(), number, it->second.effect, it->second.emitter, it->second.bone,
                       it->second.sound,
                       on_weapon     ? ", on the player's weapon"
                       : player != 0 ? ", on the player"
                                     : "",
                       handle);
        return handle;
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: %s's effect %d cannot be played (%s)", k.name.c_str(), number, x.what());
    }
    return 0;
}

// A held player's weapon's swing (YS::WEAPON::locus, each tick of his weapon): its trail started while his weapon's
// motion swings (its range 12) and none runs, closed (ryj::EFFECT::loop_end: its layers end their loops) once it stops
// swinging; one that has ended forgotten
void weapon_trail(Enemy &e, unsigned object, Held &h, bool swing)
{
    if (h.trail == 0) {
        if (swing)
            h.trail = cast(e, kSwingTrail, object, true, &player_files().weapon_fx);
        return;
    }
    const auto r =
        std::find_if(e.fx.begin(), e.fx.end(), [&h](const Enemy::Running &x) { return x.handle == h.trail; });
    if (r != e.fx.end() && !r->fx->done()) {
        if (!swing) {
            r->fx->stop();
            h.trail = 0;
        }
    } else {
        h.trail = 0;
    }
}
namespace {

// KH2's ground class's events (FUN_1403b7920, FUN_1403b79d0, reached through tables KH2 builds running): trigger 13
// as an actor leaves the ground, 14 as it lands -- the Shadow's 13 its fall, the Neoshadow's 14 its leap's landing
// (203, then 204). + 0x120's bit 26 (13) or 27 (14) is cleared first; set again by the script (1:66 sets 27), its
// class does not act: true then, no landing motion (the Neoshadow's 204 stands)
bool ground_event(Enemy &e, int trigger)
{
    Actor &a = *e.actor;
    const std::uint32_t keep = trigger == 13 ? 1u << 26 : 1u << 27;
    a.set_bits(field::kFlags, keep, false);
    if (e.dying)
        return false;
    const int before = a.motion.id;
    a.states().event(a, trigger == 13 ? 1 : 2); // the state's own routine for it first (FUN_1403ca1d0)
    const bool handled = (a.u32(field::kFlags) & keep) == 0 && a.event(trigger);
    const bool kept = (a.u32(field::kFlags) & keep) != 0;
    if (tracing() && handled)
        khdays_log("kh2 enemies: %08x %s the ground (trigger %d) in motion %d%s", e.days,
                   trigger == 13 ? "leaves" : "lands on", trigger, before, kept ? ", its class kept from acting" : "");
    return kept;
}
} // namespace

// An effect's place in KH2's world now (FUN_1402be970: its instance's): its element's on the enemy's pose, the
// enemy's place and yaw (the effects' space is KH2's model space, y down, the model's turn about the up its yaw)
bool effect_place(const Enemy &e, std::uint32_t handle, Vec3 &out)
{
    for (const auto &r : e.fx)
        if (r.handle == handle && r.element != nullptr) {
            // one on a held player's bones: where it was last drawn
            const kh2::EffectMatrix m = r.player != 0 && r.placed ? r.last : element_world(e.pose, *r.element);
            const Vec3 place = e.actor->place();
            const Vec3 t = legacy(turned({m[12], 0.0F, m[14]}, days_turn(e.actor->yaw())));
            out = {place[0] + t[0], place[1] + m[13], place[2] + t[2]};
            return true;
        }
    return false;
}
namespace {

// The current motion's single-frame triggers its time passed (KH2's ANB triggers, YS::ANMATR::exec_trigger from
// add_time and start_motion): those after the time they were played up to, to the time now -- the start frame's own
// too when it began (exec_trigger(start, -1)), none while a blend runs, round a loop those up to its end and then
// those after its loop point. 1 an effect, 8 a sound, 9 to 12 bits 0 to 3 of + 0x2ad for this tick (FUN_14040dd70; its
// scripts read them the tick after: 1:151). `again`: a motion started after this tick's (its start frame's, as KH2
// plays them when it starts), its bits added to the tick's.
void cues(Enemy &e, bool again = false)
{
    Actor &a = *e.actor;
    // (a trigger's script event may start another motion: the windows are this one's, as they stood)
    const khdays::kh2_enemies::Playing m = a.motion;
    const kh2::MotionAttributes *attr = m.id >= 0 && m.blend <= 0.0F ? e.motions->attributes(m.id) : nullptr;
    const float length = m.id >= 0 && a.motions != nullptr ? a.motions->length(m.id) : 0.0F;
    const float loop = m.id >= 0 && a.motions != nullptr ? a.motions->loop_from(m.id) : 0.0F;
    const auto in = [&](float f) {
        const bool after = m.cue_start ? f >= m.cued : f > m.cued;
        return m.looped ? (after && f <= length) || (f > loop && f <= m.time) : after && f <= m.time;
    };
    std::uint32_t passed = 0;
    for (const auto &t : attr != nullptr ? attr->triggers : std::vector<kh2::MotionAttributes::Trigger>{}) {
        if (!in(float(t.frame)))
            continue;
        if (t.id == 0 && a.take_off) { // the take-off: the jump asked begins (FUN_14040dd70's event 0, + 0x648)
            a.jump = *a.take_off;
            a.take_off.reset();
            a.velocity = a.steered = a.jump.along;
            a.move_clock = 0.0F;
            a.set_bits(field::kFlags, bit::kInAir, true);
            if (tracing())
                khdays_log("kh2 enemies: %08x takes off (trigger 0) in motion %d at %d", e.days, a.motion.id,
                           int(t.frame));
        } else if (t.id == 1 && !t.params.empty() && a.muted_effects.count(std::int16_t(t.params[0])) != 0) {
            // kept from starting by its script (1:270; FUN_1403c7db0's list): nothing
        } else if (t.id == 1 && !t.params.empty()) {
            // its script told (FUN_14040dd70: trigger 16 with the actor, the element, the effect's handle -- the Hammer
            // Frame's shockwave attaches its attack to its element 8)
            const int number = std::int16_t(t.params[0]);
            const std::uint32_t handle = cast(e, number);
            const ai::Value told[3] = {ai::Value::of_address(a.address()), ai::Value::of_int(number),
                                       ai::Value::of_address(handle)};
            a.event(16, told);
        } else if (t.id == 7) { // a "game effect": trigger 17 with the actor and its two values (FUN_14040dd70)
            const ai::Value told[3] = {ai::Value::of_address(a.address()),
                                       ai::Value::of_int(t.params.size() > 0 ? std::int16_t(t.params[0]) : 0),
                                       ai::Value::of_int(t.params.size() > 1 ? std::int16_t(t.params[1]) : 0)};
            a.event(17, told);
        } else if (t.id == 28) {
            // its revenge (ANMATR::ExecTriggerCallback's 0x1c: + 0x588 bit 4, which every enemy's constructor sets,
            // and its karma at its limit -> ACT::request("revenge"), entered at once when it has one): a damage
            // motion's (Final Xemnas's 10 to 24 at their frame 20)
            float limit = 100.0F;
            const std::uint32_t bits = a.u32(field::kKarmaLimit);
            if (bits != 0)
                std::memcpy(&limit, &bits, 4);
            if (e.karma >= limit && a.states().enter(a, "revenge") && tracing())
                khdays_log("kh2 enemies: %08x's karma %.0f reaches %.0f: its revenge (trigger 28 of motion %d)", e.days,
                           double(e.karma), double(limit), a.motion.id);
        } else if (t.id >= 9 && t.id <= 12)
            passed |= 1u << (t.id - 9);
        else if (const int id = trigger_sound(t); id > 0)
            sound(e, id);
    }
    if (m.blend <= 0.0F && a.motion.started == m.started) {
        a.motion.cued = m.time;
        a.motion.cue_start = a.motion.looped = false;
    }
    const std::uint32_t marks = a.u32(field::kMotionMarks);
    a.set_u32(field::kMotionMarks, again ? marks | (passed << 8) : (marks & ~0xff00u) | (passed << 8));
}

// Where each of a script's threads stands (its next code word), its children after it
void threads_of(const ai::Thread &t, std::string &out)
{
    char at[24];
    std::snprintf(at, sizeof(at), " %#x", t.pc);
    out += at;
    if (!t.children.empty()) {
        out += " (";
        for (const auto &c : t.children)
            threads_of(*c, out);
        out += " )";
    }
}

void motion_states(Enemy &e)
{
    Actor &a = *e.actor;
    std::uint32_t on = 0, all = 0;
    for (const auto &[id, b] : kStateRanges)
        all |= 1u << b;
    // while the motion plays (a loop always does): one played to its end (held on its last frame) sets none -- a landing's
    // range 9 to its end ("65535") would hold it for ever after -- but range 13, read on its frame as KH2 reads them all:
    // what is put on it stays (F_NM080, Oogie Boogie's tower, holds its motion 0, range 13 over all of it)
    const bool loops = a.motion.id >= 0 && a.motions != nullptr && a.motions->has(a.motion.id) &&
                       a.motions->loop_from(a.motion.id) < a.motions->length(a.motion.id);
    const bool on_motion = !a.motion.ended || loops;
    if (const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.motions->attributes(a.motion.id) : nullptr)
        for (const auto &r : attr->ranges)
            for (const auto &[id, b] : kStateRanges)
                if (int(r.id) == id && (on_motion || id == 13) && a.motion.time >= float(r.start) &&
                    a.motion.time < float(r.end))
                    on |= 1u << b;
    if (e.reacting)
        on |= bit::kHeld;
    a.set_u32(field::kStatus, (a.u32(field::kStatus) & ~all) | on);
    // its range 22 (YS::ANMATR::CheckRangeCallback -> YS::BTLOBJ::turn_to_target_range, the PS3 build's): turned toward
    // its target while it is on -- the way to it along the ground, unless within 50 cm -- by its value in degrees, 0 its
    // Spec's turn speed (YS::OBJ::turn_to_dir: times the frame's time, 2.0 as 1:80 has it). Sephiroth's split flash
    // (204, frames 0 to 57) so faces the player till it dashes (a party member's turning is its own)
    if (const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.motions->attributes(a.motion.id) : nullptr;
        attr != nullptr && on_motion && the_field().friend_of(a) == nullptr)
        for (const auto &r : attr->ranges) {
            if (r.id != 22 || a.motion.time < float(r.start) || a.motion.time >= float(r.end))
                continue;
            const Object *target = the_field().player(a);
            if (target == nullptr)
                break;
            const Vec3 to = target->place(), at = a.place();
            const float dx = to[0] - at[0], dz = to[2] - at[2];
            if (std::sqrt(dx * dx + dz * dz) <= 50.0F)
                break;
            const float value = r.params.empty() ? 0.0F : float(std::uint16_t(r.params[0])) * kPi / 180.0F;
            const float step = (value == 0.0F ? a.f(field::kTurnSpeed) : value) * 2.0F;
            float d = std::atan2(dx, dz) - a.yaw();
            while (d > kPi)
                d -= 2.0F * kPi;
            while (d < -kPi)
                d += 2.0F * kPi;
            a.set_yaw(a.yaw() + std::clamp(d, -step, step));
            break;
        }
    // the motions asked to follow (1:13, 2:87's idle): the first taken as this one blends back (range 2, bit 4) or in a
    // tick it reaches its end (the ended bit, Actor::step's: a loop's as it goes round), the others after it
    if (!a.motion.queued.empty() && ((on & (1u << 4)) != 0 || (a.u32(field::kStatus) & bit::kMotionEnded) != 0)) {
        auto rest = a.motion.queued;
        const auto [next, blend] = rest.front();
        rest.pop_front();
        a.play(next, blend, 0.0F, true);
        a.motion.queued = std::move(rest);
    }
    // its ranges 16 to 19 ("AI" ranges: special movement, its combos): bits 0 to 3 of + 0x2ac (FUN_14040d810; 1:150)
    std::uint32_t ai_ranges = 0;
    // and its ranges 36 and 37: a damage pattern on and off while they last, the bit their parameter names (+ 0xd14
    // Pattern.Enable, + 0xd18 Pattern.Disable: FUN_14040d810's 0x24 and 0x25; patterns.h) -- Roxas's dashing combo
    // (motion 201, pattern 1 over frames 0 to 240) takes no flinch so, as in KH2
    std::uint32_t enable = 0, disable = 0;
    if (const kh2::MotionAttributes *attr =
            a.motion.id >= 0 && (!a.motion.ended || loops) ? e.motions->attributes(a.motion.id) : nullptr)
        for (const auto &r : attr->ranges) {
            if (a.motion.time < float(r.start) || a.motion.time >= float(r.end))
                continue;
            if (r.id >= 16 && r.id <= 19)
                ai_ranges |= 1u << (r.id - 16);
            else if ((r.id == 36 || r.id == 37) && !r.params.empty())
                (r.id == 36 ? enable : disable) |= 1u << (r.params[0] & 31u);
        }
    a.set_u32(field::kMotionMarks, (a.u32(field::kMotionMarks) & ~0xffu) | ai_ranges);
    a.set_u32(0xd14, enable);
    a.set_u32(0xd18, disable);
}

// A sphere of `radius` (KH2's cm) from `from` along `way` (KH2's) against the room's walls (khdays_kh2_shell_wall: Days's
// collision's wall list, which its ray cast never meets): how far along the way it meets one (0 to 1) and the wall's
// normal; whether it does. A wall whose front the way leaves (a sphere touching it going away or along it) is none:
// Days's casts count faces from their front alone, so only one it starts against looks that way
bool wall_cast(unsigned shell, const Vec3 &from, const Vec3 &way, float radius, float &share, Vec3 &normal)
{
    float f[3], t[3], h[3], n[3];
    to_days(from, f);
    to_days({from[0] + way[0], from[1] + way[1], from[2] + way[2]}, t);
    if (!khdays_kh2_shell_wall(shell, f, t, radius / kKh2PerDays, h, n))
        return false;
    const Vec3 at = to_kh2(h);
    const float length = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
    const float done = std::sqrt((at[0] - from[0]) * (at[0] - from[0]) + (at[1] - from[1]) * (at[1] - from[1]) +
                                 (at[2] - from[2]) * (at[2] - from[2]));
    share = length > 0.0F ? std::clamp(done / length, 0.0F, 1.0F) : 0.0F;
    normal = days_way({n[0], n[1], n[2]});
    return normal[0] * way[0] + normal[2] * way[2] < 0.0F;
}
} // namespace

// A way along the ground (`way`, KH2's cm) from `place` swept by a body's sphere of `radius` against the room's walls,
// as KH2's own collision moves a character (its body swept along the step, slid along what it meets): up to the first
// wall, the rest of the way along it (its part into the wall taken out), swept again and stopped at the next. The
// way made, in `way`; whether a wall stopped it. Days's collision pushes back only one standing in a wall, so a step
// longer than its radius went through one: the Lingering Will's knuckle rush (23 m in a second and a half) left the
// coliseum through its ring's and its stands' walls. Not `slide`: stopped at the first wall.
bool wall_sweep(unsigned shell, const Vec3 &place, float radius, Vec3 &way, bool slide)
{
    if (!(radius > 0.0F))
        radius = kWallRadius;
    const Vec3 start = {place[0], place[1] - radius - kWallStepOver, place[2]};
    Vec3 at = start, left = {way[0], 0.0F, way[2]};
    bool stopped = false;
    for (int pass = 0; pass < 2; ++pass) {
        const float length = std::hypot(left[0], left[2]);
        if (!(length > kWallGap))
            break;
        float share = 0.0F;
        Vec3 n{};
        if (!wall_cast(shell, at, left, radius, share, n)) {
            at[0] += left[0], at[2] += left[2];
            break;
        }
        stopped = true;
        const float kept = std::max(0.0F, share - kWallGap / length);
        at[0] += left[0] * kept, at[2] += left[2] * kept;
        if (pass == 1 || !slide)
            break; // its way along the wall met another (or it does not slide): it stops there
        left = {left[0] * (1.0F - kept), 0.0F, left[2] * (1.0F - kept)};
        const float nl = std::hypot(n[0], n[2]);
        if (!(nl > 0.0F))
            break;
        const float nx = n[0] / nl, nz = n[2] / nl, into = left[0] * nx + left[2] * nz;
        if (into < 0.0F)
            left[0] -= into * nx, left[2] -= into * nz;
    }
    way[0] = at[0] - start[0];
    way[2] = at[2] - start[2];
    return stopped;
}
namespace {

// Its way along the ground this tick (`travel`, KH2's cm, from `place`), on the ground or in the air, swept with its
// Days sphere's radius against the room's walls (wall_sweep); whether a wall stopped it. Days's collision keeps it that
// radius off them: a sphere any larger started in the wall it stood by, and every way, even away from it, was stopped
// (the Lingering Will stood against the coliseum's ring for good)
bool wall_stop(Enemy &e, const Vec3 &place, Vec3 &travel)
{
    if (!(std::hypot(travel[0], travel[2]) > 0.0F))
        return false;
    const Vec3 asked = travel;
    if (!wall_sweep(shell_of(e), place, e.radius * kKh2PerDays, travel, true))
        return false;
    if (trace_level() >= 2)
        khdays_log("kh2 enemies: %08x's way of %.1f %.1f stopped by a wall: %.1f %.1f made (KH2's cm)", e.days,
                   double(asked[0]), double(asked[2]), double(travel[0]), double(travel[2]));
    return true;
}

// The vacuums' pull on an object this tick (KH2's cm), as FUN_1403f9130 sums it for each object's move: none on one
// of no weight (its entry's + 0x50: the Hydra's 0), + 0x120 bit 2 or + 0x18c bit 25 set (FUN_1403ba7d0; its last
// test, FUN_1403da520, not read); each vacuum that does not let its kind be (+ 0x10's bit of its entry's kind, 0 for
// the player) and whose reach (+ 0x14, 0 none) it is within pulls it along the ground toward its point at weight /
// 100 x its speed, kept between its least and its most, times (distance - its near range) x its rate kept between -1
// and 1 -- toward it from beyond 2 m, away within 1 m with the near range of 100 and the rate of 0.01 -- and its
// turning (+ 0x24, 2:82) across that way. Demyx's water clones' pull Demyx (500) and each
// other (10), not the player (their kind 0 let be). Days's players are pulled by none here
Vec3 vacuum_pull(Enemy &e, const Vec3 &place)
{
    Vec3 pull{};
    const Actor &a = *e.actor;
    const std::uint32_t entry = a.u32(field::kEntry);
    if (vacuums.empty() || entry == 0)
        return pull;
    const auto f32 = [](std::uint32_t at) {
        const std::uint32_t bits = mod().memory.u32(at);
        float f = 0.0F;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    };
    const float weight = f32(entry + 0x50);
    if (weight == 0.0F || (a.u32(field::kFlags) & 4u) != 0 || ((a.u32(field::kStatus) >> 25) & 1u) != 0)
        return pull;
    const unsigned kind = ((a.u32(field::kFlags6c8) >> 7) & 1u) != 0 ? 0u : (mod().memory.u32(entry + 4) & 0xffu);
    for (const std::uint32_t v : vacuums) {
        if ((mod().memory.u32(v + 0x10) & (1u << (kind & 31u))) != 0)
            continue;
        const float dx = f32(v) - place[0], dz = f32(v + 8) - place[2];
        const float distance = std::sqrt(dx * dx + dz * dz), reach = f32(v + 0x14);
        if (reach != 0.0F && distance >= reach)
            continue;
        const float speed = weight / 100.0F * f32(v + 0x18), least = f32(v + 0x1c);
        const float s = least > speed ? least : std::min(f32(v + 0x20), speed);
        const float rate = std::clamp((distance - f32(v + 0x28)) * f32(v + 0x2c), -1.0F, 1.0F);
        if (distance > 0.0F) {
            pull[0] += dx / distance * s * rate;
            pull[2] += dz / distance * s * rate;
            // its turning (+ 0x24): the way to it turned a quarter about y (FUN_1403b7680 by DAT_140623ab0, pi / 2)
            // times that speed, about the point (the Lingering Will's; which way round a positive speed turns,
            // FUN_1401aad20's sign, not read)
            if (const float turn = f32(v + 0x24); turn != 0.0F) {
                pull[0] += dz / distance * turn;
                pull[2] += -dx / distance * turn;
            }
        }
    }
    return pull;
}
} // namespace

void one_tick(Enemy &e, const KhdaysKh2Shell &s, Vec3 &place)
{
    Actor &a = *e.actor;
    a.begin_tick();
    if (trace_level() >= 2 && ++e.ticks_traced % 60 == 0 && a.machine().main_thread() != nullptr) {
        std::string at;
        threads_of(*a.machine().main_thread(), at);
        khdays_log("kh2 enemies: %08x's threads at%s; motion %d at %.1f%s%s; %.3f above the ground, %.1f its fall; at "
                   "%.2f %.2f %.2f facing %.0f degrees (KH2's), status %08x, steered %.1f",
                   e.days, at.c_str(), a.motion.id, double(a.motion.time), a.jump.on ? ", in a jump" : "",
                   e.reacting ? ", reacting" : "", double(s.above), double(a.jump.on ? a.jump.up : e.fall),
                   double(s.place[0]), double(s.place[1]), double(s.place[2]), double(a.yaw() * 180.0F / kPi),
                   a.u32(field::kStatus), double(a.steer_speed));
    }
    if (!e.sent_state.empty() && (e.sent_after -= 1.0F) <= 0.0F) {
        if (a.states().shout(a, e.sent_state, e.sent_from) && tracing())
            khdays_log("kh2 enemies: %08x hears \"%s\" shouted", e.days, e.sent_state.c_str());
        e.sent_state.clear();
    }
    a.set_place(place); // Days's, its collision's push taken
    const unsigned started = a.motion.started;
    if (!e.started) { // made by another's script: its first tick (start)
        e.started = true;
        a.start();
    }
    if (!e.paused)
        a.tick();
    if (const Vec3 now = a.place(); now != place)
        place = now; // its script put it elsewhere (1:148, 1:153, 1:220)
    if (trace_level() >= 2 && a.motion.started != started && a.motion.blend > 0.0F)
        khdays_log("kh2 enemies: %08x tick %u: motion %d over a blend of %.1f, standing at %.1f", e.days,
                   e.ticks_traced, a.motion.id, double(a.motion.blend), double(a.motion.time));
    const bool blending = a.motion.blend > 0.0F;
    Vec3 travel = a.step(1.0F);
    const Vec3 pulled = vacuum_pull(e, place); // (KH2's update adds it to the motion's travel, FUN_1403b8ca2)
    travel[0] += pulled[0];
    travel[2] += pulled[2];
    if (trace_level() >= 2 && blending && a.motion.blend <= 0.0F)
        khdays_log("kh2 enemies: %08x tick %u: motion %d blended in, on from %.1f", e.days, e.ticks_traced, a.motion.id,
                   double(a.motion.time));
    cues(e);
    event_tick(e);
    motion_states(e);
    // no pull: a motion's ranges say so, or its class flies (class 5, 1:58 trap_obj_fly: its movement, FUN_140421c80,
    // keeps its own velocity and adds none) -- Zexion hovers so; before, a hit taken in the air let him fall, land and
    // rise again round and round, his class kept from acting each time ("leaves the ground", trigger 13)
    const bool no_gravity = (a.u32(field::kStatus) & kNoGravity) != 0 || a.dashing;
    // a motion in the air (its range 30: + 0x18c bit 24, 1:60's "off the ground"): the ground met again lands it
    const bool motion_air = (a.u32(field::kStatus) & kMotionAir) != 0;
    if (motion_air && e.floor_known && e.floor - place[1] > 0.01F * kKh2PerDays)
        e.aloft = true; // carried off the ground by its motion
    if (a.jump.on) {
        e.aloft = true;
        if (a.jump.up > 0.0F && e.floor_known && place[1] + travel[1] >= e.floor) {
            travel[1] = e.floor - place[1]; // on it, not through it
            e.aloft = false;
            if (ground_event(e, 14)) { // its script kept its class from acting: no landing motion
                a.jump = {};
                a.set_bits(field::kFlags, bit::kInAir, false);
            } else
                a.land();
        }
    } else if (!no_gravity && e.floor_known && e.floor - place[1] > 0.01F * kKh2PerDays) {
        if (e.fall == 0.0F) {
            e.fall_from = e.floor - place[1];
            if (!e.aloft && e.fall_from > kStep) { // the ground gone from under it (a ledge): its script told
                e.aloft = true;
                ground_event(e, 13);
            }
        }
        e.fall += kGravity;
        travel[1] += std::min(e.fall, e.floor - place[1]);
        a.set_bits(field::kFlags, bit::kInAir, true);
    } else if (e.fall > 0.0F) {
        // on the ground again: a fall from higher than a step (Cerberus's leap, its motion's root up; a ledge) lands
        // as KH2's falling class does, with its landing motion (FUN_1403cb940: + 0x11c, 5 by default); a step's
        // gap (Days's ground found a few centimetres off) does not
        const bool told = e.aloft && ground_event(e, 14);
        if (e.fall_from > kStep && !told) {
            if (tracing())
                khdays_log(
                    "kh2 enemies: %08x lands from %.1f (KH2's cm): its landing motion %d over its motion %d at %.1f",
                    e.days, double(e.fall_from), a.motion.landing, a.motion.id, double(a.motion.time));
            a.touch_down();
        }
        e.aloft = false;
        e.fall = 0.0F;
        a.set_bits(field::kFlags, bit::kInAir, false);
    }
    // never into the ground (KH2's ground class keeps it on it): a motion's root going down, or Days's ground found
    // above it, puts it back on the floor
    if (trace_level() >= 2 && std::fabs(travel[1]) > 0.5F && !a.jump.on)
        khdays_log("kh2 enemies: %08x's motion %d at %.1f moves it %.1f up (KH2's cm), %.1f above the ground", e.days,
                   a.motion.id, double(a.motion.time), double(-travel[1]),
                   double(e.floor_known ? e.floor - place[1] : 0.0F));
    if (!a.jump.on && e.floor_known && place[1] + travel[1] > e.floor)
        travel[1] = e.floor - place[1];
    // a motion in the air brought down onto the ground (the Neoshadow's leap, 203): landed
    if (e.aloft && motion_air && !a.jump.on && e.floor_known && place[1] + travel[1] >= e.floor - 0.01F) {
        e.aloft = false;
        ground_event(e, 14);
    }
    // the room's walls stop its way, on the ground or in the air; its slide and its steering end there (KH2's
    // FUN_140421450, FUN_1403b9420)
    if (wall_stop(e, place, travel)) {
        a.slide_speed = 0.0F;
        a.steer_speed = 0.0F;
    }
    // on the ground of its own (no jump, no motion in the air, not falling), its way along the ground does not take it
    // off a ledge (the ground more than a step below, or none): KH2's rooms wall their edges, Days's arenas and
    // rooms made from KH2's maps leave some open (the coliseum's ring: Hades slid off it by a hit's slide and could
    // not climb back), so it stops there as at a wall, its slide and its steering with it -- as a held player does
    const bool grounded = !a.jump.on && !motion_air && !e.aloft && !no_gravity && e.fall == 0.0F && e.floor_known &&
                          e.floor - place[1] <= 0.01F * kKh2PerDays;
    if (grounded && travel[0] * travel[0] + travel[2] * travel[2] > 0.0F) {
        const Vec3 to_place = {place[0] + travel[0], place[1], place[2] + travel[2]};
        float from[3], to[3], hit[3];
        to_days({to_place[0], to_place[1] - kGroundSearchUp, to_place[2]}, from);
        to_days({to_place[0], to_place[1] + kGroundSearchDown, to_place[2]}, to);
        const bool ground = khdays_kh2_shell_ray(shell_of(e), from, to, hit) != 0;
        const float drop = ground ? to_kh2(hit)[1] - place[1] : kGroundSearchDown;
        if (drop > kStep) {
            if (trace_level() >= 2)
                khdays_log("kh2 enemies: %08x kept off a ledge (%s %.1f below)", e.days,
                           ground ? "the ground" : "no ground within", double(drop));
            travel[0] = travel[2] = 0.0F;
            a.slide_speed = 0.0F;
            a.steer_speed = 0.0F;
        }
    }
    for (int i = 0; i < 3; ++i)
        place[i] += travel[i];
    a.set_place(place);
    // its reflected attack's motion over, as its damage motion's below
    if (e.reflect_motion >= 0 && (a.motion.ended || a.motion.id != e.reflect_motion)) {
        e.reflect_motion = -1;
        if (!e.reacting)
            a.damage_motion = false;
    }
    // its reaction over: its damage motion ended, or another (its idle pushed after it, or one its state played)
    if (e.reacting && (a.motion.ended || (a.motion.id != 10 && a.motion.id != 12))) {
        e.reacting = false;
        a.damage_motion = e.reflect_motion >= 0;
        a.set_bits(field::kStatus, bit::kHeld, false);
        if (e.paused)
            a.play(0, 8.0F, 0.0F); // its script picks its motion again otherwise
        e.paused = false;
    }
    // its karma less the frame time while its state can act (YS::ENEMY::_OVR_pre_exec)
    if (e.karma > 0.0F && a.states().can_control(a))
        e.karma = std::max(0.0F, e.karma - a.machine().tick_length);
    cues(e, true); // a motion started since (its landing): its start frame's triggers, as it starts
    if (e.dying && !e.released) {
        ++e.end_ticks;
        const bool faded = (a.u32(field::kFade) & bit::kFadeHeld) != 0 && a.fade <= 0.0F;
        if (faded || e.end_ticks >= kLongestEnd) {
            e.released = true; // Days's own defeat goes on: its tasks run again
            if (tracing())
                khdays_log("kh2 enemies: %08x's end over (%s): its Days defeat goes on", e.days,
                           faded ? "faded out" : "too long");
        }
    }
}

Enemy *find(unsigned days)
{
    const auto it = enemies.find(days);
    return it == enemies.end() ? nullptr : it->second.get();
}

} // namespace khdays::kh2_enemies::shell
