// The scripts' world as Days's field answers it (Field, World's calls).
#include "internal.h"

namespace khdays::kh2_enemies::shell {

Field &the_field()
{
    static Field f;
    return f;
}

// 1:262 (YS::VOICE::request, then exec): its voice bank's sound `id` heard where it was last drawn, the one before
// ended, unless that one still plays and was asked with a higher priority
void Field::voice(Actor &a, int id, int priority)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return;
    Enemy &e = *it->second;
    if (e.voice_shot != 0 && khdays_sound_shot_heard(e.voice_shot) && priority < e.voice_priority)
        return;
    KindFiles &k = *e.kind;
    // its voices (kh2/voice/<name>/): its own mod's, else the first mod's that has them
    const std::string voices = kh2_mod_of("voice/" + k.name + "/volumes.txt", k.folder);
    const fs::path folder = u8(voices.empty() ? k.folder : voices) / "kh2" / "voice" / k.name;
    if (!k.voice_volumes_read) {
        k.voice_volumes_read = true;
        std::ifstream in(folder / "volumes.txt");
        int number = 0;
        float volume = 1.0F;
        while (in >> number >> volume)
            k.voice_volumes[number] = volume;
    }
    auto file = k.voices.find(id);
    if (file == k.voices.end()) {
        int made = -1;
        for (const char *kind : {".ogg", ".wav"}) {
            const fs::path p = folder / (std::to_string(id) + kind);
            std::error_code ec;
            if (made < 0 && fs::is_regular_file(p, ec)) {
                const std::u8string p8 = p.u8string();
                made = khdays_sound_shot_file(std::string(p8.begin(), p8.end()).c_str());
            }
        }
        file = k.voices.emplace(id, made).first;
    }
    const auto volume = k.voice_volumes.find(id);
    if (e.voice_shot != 0)
        khdays_sound_shot_end(e.voice_shot);
    e.voice_shot =
        file->second >= 0
            ? khdays_sound_shot_play(file->second, volume != k.voice_volumes.end() ? volume->second : 1.0F, e.pan)
            : 0;
    e.voice_priority = priority;
    if (tracing())
        khdays_log("kh2 enemies: %08x's voice %d (priority %d)%s", e.days, id, priority,
                   e.voice_shot != 0 ? "" : " (not heard: the mod has no kh2/voice file)");
}

// 1:318, 6:70: a sound its script plays (Zexion's book's pages, its loop): the enemy's kind's, the mod's
// kh2/se/<id>.wav read when first asked (its motions' and elements' are read as it loads), heard where the enemy was
// last drawn; looping until faded when `loop`
int Field::sound(Object &o, int id, bool loop)
{
    Actor *x = o.actor();
    const auto it = x != nullptr ? by_actor.find(x) : by_actor.end();
    if (it == by_actor.end())
        return 0;
    Enemy &e = *it->second;
    KindFiles &k = *e.kind;
    auto file = k.sounds.find(id);
    if (file == k.sounds.end()) {
        int made = -1;
        if (const fs::path wav = kh2_file("se/" + std::to_string(id) + ".wav", k.folder); !wav.empty()) {
            const std::u8string p8 = wav.u8string();
            made = khdays_sound_shot_file(std::string(p8.begin(), p8.end()).c_str());
        }
        file = k.sounds.emplace(id, made).first; // -1: none, asked once
    }
    if (file->second < 0) {
        if (tracing())
            khdays_log("kh2 enemies: %08x's sound %d (not heard: the mod has no kh2/se file)", e.days, id);
        return 0;
    }
    const int handle =
        loop ? khdays_sound_shot_loop(file->second, 1.0F, e.pan) : khdays_sound_shot_play(file->second, 1.0F, e.pan);
    if (tracing())
        khdays_log("kh2 enemies: %08x's sound %d%s", e.days, id, loop ? ", looping" : "");
    return handle;
}

// 1:0: an object of the mod's kinds made by its entry's number, as a Days enemy of its maker's class made where
// its maker stands (khdays_enemies_make: Days's own making -- hit, locked on, defeated, counted as Days's are),
// the kind's script set going on it at once; its maker's script then puts it where it wants it (the cubes, the
// Sorcerer where they are: 1:148, 1:79). An entry the mod has no kind for, or one whose script the port does not
// answer, makes nothing (0), as KH2 makes nothing of an entry it cannot load
std::uint32_t Field::appear(Actor &by, std::uint32_t entry)
{
    const auto maker = by_actor.find(&by);
    const std::string name = entry_name(entry);
    // (an object with no script of its own and no enemy, moved by its maker's threads -- Final Xemnas's Sora,
    // P_EX100_LAST -- is read as a form is: its empty script)
    const Kind *kind = name.empty() ? nullptr : find_kind(mod().kinds, name);
    const bool puppet = kind != nullptr && kind->calls == 0 && !enemy_entry(name);
    KindFiles *k = name.empty() || maker == by_actor.end() ? nullptr : files_of(name, puppet);
    if (k != nullptr && !enemy_entry(name)) { // no enemy: an ally of its maker's battle, where its maker stands
        Enemy *e = start_ally(*k, by.place(), by.yaw(), shell_of(*maker->second), true);
        if (tracing())
            khdays_log("kh2 enemies: %08x (%s) makes %08x, a %s (entry %u)", maker->second->days, by.kind().c_str(),
                       e->days, name.c_str(), unsigned(entry));
        return e->actor->address();
    }
    KhdaysKh2Shell s;
    if (k == nullptr || !khdays_kh2_shell_read(maker->second->days, &s)) {
        khdays_log("kh2 enemies: %s makes object %u (%s): not one of the mod's kinds, nothing made", by.kind().c_str(),
                   unsigned(entry), name.empty() ? "no entry" : name.c_str());
        return 0;
    }
    const char *why = nullptr;
    const unsigned days = khdays_enemies_make(s.kind, s.place, 0, 1.0, 1.0, &why);
    if (days == 0) {
        khdays_log("kh2 enemies: %s makes a %s: Days makes no enemy of class 0x%02x (%s)", by.kind().c_str(),
                   name.c_str(), unsigned(s.kind), why != nullptr ? why : "?");
        return 0;
    }
    kind_of[days] = name; // (its making named its maker's row's kind: Ov002_CreateActorFromMarker's join)
    appeared.insert(days);
    KhdaysKh2Shell made = s;
    if (khdays_kh2_shell_read(days, &made))
        std::copy(s.place, s.place + 3, made.place);
    Enemy *e = start(days, *k, made, true);
    if (tracing())
        khdays_log("kh2 enemies: %08x (%s) makes %08x, a %s (entry %u)", maker->second->days, by.kind().c_str(), days,
                   name.c_str(), unsigned(entry));
    return e->actor->address();
}

// 1:328: its Days enemy defeated as a player's hit of all its health defeats it (its prizes, its wave's count;
// seat 0's player its author), its script left to end it as it does (no "dead" entered: the cubes fade out)
void Field::dead_mark(Actor &a)
{
    const auto it = by_actor.find(&a);
    KhdaysKh2Shell s;
    if (it == by_actor.end() || it->second->dying || !khdays_kh2_shell_read(it->second->days, &s))
        return;
    Enemy &e = *it->second;
    e.dead_marked = true;
    const int took = khdays_enemies_hit(e.days, khdays_link_seat_actor(0), 0, std::max(1, s.hp), s.place, 0, 0, 1);
    if (tracing())
        khdays_log("kh2 enemies: %08x marks itself dead (1:328): %s", e.days,
                   took ? "its Days enemy defeated" : "Days took no hit");
}

// 1:177: its kind's walk path of that number (-1 the first), from where its battle began, turned as it then faced
// (its companions' frame: + z the way to its player)
bool Field::walk_path(Actor &a, int number, std::vector<Vec3> &out)
{
    const auto it = by_actor.find(&a);
    const Kind *kind = it != by_actor.end() ? find_kind(mod().kinds, it->second->kind->name) : nullptr;
    if (kind == nullptr)
        return false;
    for (const WalkPath &w : kind->paths) {
        if (number >= 0 && w.number != number)
            continue;
        const Enemy &e = *it->second;
        const float c = std::cos(e.battle_turn), s = std::sin(e.battle_turn);
        for (std::size_t i = 0; i + 2 < w.points.size(); i += 3) {
            const float x = w.points[i], y = w.points[i + 1], z = w.points[i + 2];
            out.push_back({e.battle_at[0] + x * c + z * s, e.battle_at[1] + y, e.battle_at[2] - x * s + z * c});
        }
        return !out.empty();
    }
    return false;
}

float Field::system_value(std::uint32_t offset, float fallback)
{
    const std::vector<std::uint8_t> &values = system_constants();
    if (std::size_t(offset) + 4 > values.size())
        return fallback;
    float v = 0.0F;
    std::memcpy(&v, values.data() + offset, 4);
    return v;
}

// 1:83: a point on one of its bones in the pose it has now (0x4000 the actor itself, in its own axes), KH2's world
bool Field::bone_point(Actor &a, int bone, const Vec3 &local, Vec3 &out)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return false;
    Enemy &e = *it->second;
    if (e.pose.empty())
        pose(e);
    KhdaysKh2Shell s{};
    to_days(a.place(), s.place);
    float d[3];
    days_point(e, s, bone, local, d);
    out = to_kh2(d);
    return true;
}

// 2:9: its health record given its health (now, at most), as KH2's record of it holds its own: its KH2 health in
// Days's scale once its battle entry is known (battle_entry), else Days's HP of it
void Field::health(Actor &a)
{
    const auto it = by_actor.find(&a);
    const std::uint32_t record = a.u32(0x5c0);
    KhdaysKh2Shell s;
    if (it == by_actor.end() || record == 0)
        return;
    const Enemy &e = *it->second;
    if (e.record_most > 0) { // KH2's numbers: its health's share of its entry's (before Days's first hit, its start
        // less the KH2 blows that landed since: its partner's, Riku's, on Final Xemnas)
        const double share =
            e.health >= 0
                ? double(e.health) / double(std::max<std::int64_t>(1, e.health_most))
                : std::max(0.0, e.start_share -
                                    (e.health_share > 0.0 ? e.pending_blows / (kSoldierBlows * e.health_share) : 0.0));
        const std::int64_t now =
            e.health == 0 ? 0 : std::max<std::int64_t>(1, std::llround(share * double(e.record_most)));
        a.memory().set_u32(record, std::uint32_t(std::min<std::int64_t>(now, 0x7fffffff)));
        a.memory().set_u32(record + 4, std::uint32_t(std::min<std::int64_t>(e.record_most, 0x7fffffff)));
        return;
    }
    if (e.health >= 0) {
        a.memory().set_u32(record, std::uint32_t(std::min<std::int64_t>(e.health, 0xffffffff)));
        a.memory().set_u32(record + 4, std::uint32_t(std::min<std::int64_t>(e.health_most, 0xffffffff)));
        return;
    }
    if (!khdays_kh2_shell_read(e.days, &s))
        return;
    a.memory().set_u32(record, std::uint32_t(std::max(0, s.hp)));
    a.memory().set_u32(record + 4, std::uint32_t(std::max(1, s.hp_max)));
}

// 2:9's battle entry: its health KH2's over KH2's Soldier's (the entries' first health values: KH2's level
// multiplier is the same for both; Hades of the cups' 1500 over 100: fifteen Soldiers), kept for Days's first hit
// on it (khdays_kh2_enemies_damage), which gives its health in Days's damage. Once
void Field::battle_entry(Actor &a, int entry)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end() || it->second->health_share > 0.0)
        return;
    Enemy &e = *it->second;
    e.battle_level = battle_row(entry).level;
    const int own = battle_health(entry), soldier = battle_health(kSoldierEntry);
    if (own <= 0 || soldier <= 0)
        return;
    e.health_share = double(own) / double(soldier);
    e.record_most = own;
    if (tracing())
        khdays_log("kh2 enemies: %08x's battle entry %d: %d health, %.2f KH2 Soldiers", e.days, entry, own,
                   e.health_share);
}

void Field::set_health(Actor &a, int hp)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end() || it->second->dying)
        return;
    Enemy &e = *it->second;
    if (e.record_most > 0) { // in the record's measure: that share of its health (before Days's first hit, kept)
        const double share = std::clamp(double(hp) / double(e.record_most), 0.0, 1.0);
        if (e.health < 0) {
            e.start_share = share;
            return;
        }
        e.health = std::clamp<std::int64_t>(std::llround(share * double(e.health_most)), 1, e.health_most);
        show_health(e);
        return;
    }
    if (e.health >= 0) { // in its own health's measure, kept between 1 and its most (Days's defeat by its hits)
        e.health = std::clamp<std::int64_t>(hp, 1, e.health_most);
        show_health(e);
        return;
    }
    khdays_kh2_shell_set_hp(e.days, hp);
}

// 1:98, 1:99: the ground Days last found under it (its place's height when none yet)
float Field::ground(Actor &a)
{
    const auto it = by_actor.find(&a);
    return it != by_actor.end() && it->second->floor_known ? it->second->floor : a.place()[1];
}

// 1:90: a state every enemy of the field enters, at once or each after a time at random below `delay` (KH2's
// chance, DAT_140783ca0 x 0x10dcd)
void Field::broadcast(Actor &a, const std::string &name, float delay)
{
    shouts.push_back({name, a.states().handle(), delay});
}

// 1:87: the actor's element `number` started (FUN_1403b6670 -> FUN_1402c6b30 on its effects); the handle the
// script gives back to 0:79 none
std::uint32_t Field::effect(Actor &a, int number, int, int)
{
    const auto it = by_actor.find(&a);
    return it != by_actor.end() ? cast(*it->second, number) : 0u; // its handle: 2:61 follows it
}

std::uint32_t Field::effect_on(Actor &a, int number, int, int, Object *target)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return 0;
    const unsigned on = target != nullptr ? days_object(*target) : 0; // a player: on his bones while it holds him
    return cast(*it->second, number, on);
}

void Field::attack_made(std::uint32_t at, Actor &by, Object *owner, int atkp, int group, int sub)
{
    const auto it = by_actor.find(&by);
    if (it == by_actor.end())
        return;
    AttackObject o;
    o.enemy = it->second->days;
    o.owner = owner != nullptr ? owner->address() : 0u;
    o.players = owner != nullptr && days_object(*owner) != 0;
    o.atkp = atkp;
    o.group = group;
    o.sub = sub;
    attacks[at] = o;
    mod().memory.set_u32(at + 0x18, std::uint32_t(atkp)); // (2:25, as a motion's attack: motion_attack)
    if (tracing())
        khdays_log("kh2 enemies: %08x makes an attack (atkp %d, group %d)%s", o.enemy, atkp, group,
                   o.players ? " of KH2's player" : "");
}

void Field::attack_size(std::uint32_t at, float radius, float height)
{
    if (const auto o = attacks.find(at); o != attacks.end())
        o->second.radius = radius, o->second.height = height;
}

void Field::attack_place(std::uint32_t at, const Vec3 &place)
{
    if (const auto o = attacks.find(at); o != attacks.end()) {
        o->second.before = o->second.placed ? o->second.at : place;
        o->second.at = place;
        o->second.placed = true;
    }
}

void Field::attack_grow(std::uint32_t at, float ticks, float radius, float height)
{
    if (const auto o = attacks.find(at); o != attacks.end()) {
        o->second.grow_for = ticks;
        o->second.grow_ticks = 0.0F;
        o->second.grow_radius = radius > 0.0F ? radius : o->second.radius;
        o->second.grow_height = height > 0.0F ? height : o->second.height;
    }
}

void Field::attack_follow(std::uint32_t at, std::uint32_t effect)
{
    if (const auto o = attacks.find(at); o != attacks.end())
        o->second.effect = effect;
}

void Field::attack_team(std::uint32_t at, int team)
{
    if (const auto o = attacks.find(at); o != attacks.end())
        o->second.team = team;
}

void Field::attack_strike(std::uint32_t at, Object *target)
{
    const auto o = attacks.find(at);
    if (o == attacks.end() || target == nullptr)
        return;
    strike(o->second, *target);
}

void Field::attack_end(std::uint32_t at)
{
    if (const auto o = attacks.find(at); o != attacks.end())
        o->second.ended = true;
}

bool Field::attack_finished(std::uint32_t at)
{
    // a motion's attack (the record trigger 15 gives: its range 10 lasting, KH2's YS::ATTACK of the motion) stands
    // while it is told -- before, 2:96 said it ended and Zexion never opened his book on the hit that lands it
    for (const auto &[days, e] : enemies)
        if (at != 0 && e->attack_record == at)
            return false;
    const auto o = attacks.find(at);
    return o == attacks.end() || o->second.ended || o->second.expired || o->second.landed;
}

bool Field::attack_expired(std::uint32_t at)
{
    const auto o = attacks.find(at);
    return o == attacks.end() || o->second.expired;
}

bool Field::attack_landed(std::uint32_t at)
{
    const auto o = attacks.find(at);
    return o != attacks.end() && o->second.landed;
}

std::uint32_t Field::attack_struck(std::uint32_t at)
{
    const auto o = attacks.find(at);
    return o != attacks.end() ? o->second.hit : 0u;
}

bool Field::attack_way(std::uint32_t at, Vec3 &out)
{
    const auto o = attacks.find(at);
    const auto e = o != attacks.end() ? enemies.find(o->second.enemy) : enemies.end();
    if (e == enemies.end())
        return false;
    const Vec3 from = e->second->actor->place();
    const Vec3 d = {o->second.at[0] - from[0], o->second.at[1] - from[1], o->second.at[2] - from[2]};
    const float n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (n <= 0.0F)
        return false;
    out = {d[0] / n, d[1] / n, d[2] / n};
    return true;
}

bool Field::attack_can_hit(std::uint32_t at, Object *target)
{
    const auto o = attacks.find(at);
    if (o == attacks.end() || target == nullptr)
        return false;
    const bool player = days_object(*target) != 0;
    return o->second.players ? !player && target->actor() != nullptr : player;
}

void Field::attack_line(std::uint32_t at, const Vec3 &now, const Vec3 &before)
{
    if (const auto o = attacks.find(at); o != attacks.end()) {
        o->second.at = now;
        o->second.before = before;
        o->second.placed = true;
    }
}

// An effect by its handle, an enemy's or a script-made object's (Roxas's keyblades')
Enemy::Running *Field::running_effect(std::uint32_t handle, Enemy **of)
{
    if (handle == 0)
        return nullptr;
    for (auto *list : {&enemies, &allies})
        for (auto &[days, e] : *list)
            for (Enemy::Running &r : e->fx)
                if (r.handle == handle) {
                    if (of != nullptr)
                        *of = e.get();
                    return &r;
                }
    return nullptr;
}

std::uint32_t Field::effect_in_world(Actor &a, int number, const Vec3 &place)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return 0;
    const std::uint32_t handle = cast(*it->second, number);
    if (Enemy::Running *r = running_effect(handle))
        r->in_world = true, r->at = place;
    return handle;
}

bool Field::effect_alive(std::uint32_t handle)
{
    return running_effect(handle) != nullptr;
}

bool Field::effect_where(std::uint32_t handle, Vec3 &out)
{
    Enemy *e = nullptr;
    Enemy::Running *r = running_effect(handle, &e);
    if (r == nullptr)
        return false;
    if (r->in_world) {
        out = r->at;
        return true;
    }
    return effect_place(*e, handle, out);
}

void Field::effect_unbind(std::uint32_t handle)
{
    Enemy *e = nullptr;
    Enemy::Running *r = running_effect(handle, &e);
    Vec3 at{};
    if (r == nullptr || r->in_world || !effect_where(handle, at))
        return;
    r->in_world = true;
    r->at = at;
    r->turned = true;
    r->turn = {0.0F, e->actor->yaw(), 0.0F}; // (facing the world's way it faced as it was let go)
}

void Field::effect_move(std::uint32_t handle, const Vec3 &place)
{
    if (Enemy::Running *r = running_effect(handle))
        r->in_world = true, r->at = place;
}

void Field::effect_turn(std::uint32_t handle, const Vec3 &angles)
{
    if (Enemy::Running *r = running_effect(handle))
        r->turned = true, r->turn = angles;
}

void Field::effect_fade(std::uint32_t handle, float ticks)
{
    if (Enemy::Running *r = running_effect(handle))
        r->fade_step = -r->fade / ticks;
}

void Field::effect_scale(std::uint32_t handle, const Vec3 &scale)
{
    if (Enemy::Running *r = running_effect(handle))
        r->scaled = true, r->scale = scale;
}

void Field::effect_colour(std::uint32_t handle, const std::array<float, 4> &colour)
{
    if (Enemy::Running *r = running_effect(handle))
        r->coloured = true, r->colour = colour;
}

void Field::sound_fade(int handle, float ticks)
{
    khdays_sound_shot_fade(handle, ticks / 60.0F);
}

void Field::control(int type, bool off)
{
    if (off)
        controls_off.insert(type);
    else
        controls_off.erase(type);
    if (tracing())
        khdays_log("kh2 enemies: the control of the objects of type %d %s", type, off ? "taken away" : "given back");
}

void Field::prize_drain(bool on)
{
    if (on)
        drain_frames = 2;
    if (on && tracing())
        khdays_log("kh2 enemies: the field's prizes drained to the player");
}

void Field::command_image(Object &of, bool on)
{
    Actor *a = of.actor();
    const auto it = a != nullptr ? by_actor.find(a) : by_actor.end();
    if (it == by_actor.end())
        return;
    const unsigned days = it->second->days;
    if (on)
        command_image_of = days;
    else if (command_image_of == days)
        command_image_of = 0;
    if (tracing())
        khdays_log("kh2 enemies: %08x %s", days,
                   on ? "draws the player's command menu in its own pictures" : "gives the command menu back");
}

bool Field::camera_view(Vec3 &eye, Vec3 &at)
{
    float e[3], t[3];
    if (!khdays_kh2_camera_view(e, t))
        return false;
    eye = to_kh2(e);
    at = to_kh2(t);
    return true;
}

void Field::scope_begin(Object &o, float height)
{
    scope = Scope{o.address(), height, 0.5F, 10.0F, {}};
    if (tracing())
        khdays_log("kh2 enemies: the scope camera from %08x, its eye %.0f up", o.address(), double(height));
}

void Field::scope_end()
{
    if (scope && tracing())
        khdays_log("kh2 enemies: the scope camera gone");
    scope.reset();
}

void Field::scope_zoom(float zoom)
{
    if (scope)
        scope->zoom = zoom;
}

void Field::scope_closeup(float distance)
{
    if (scope)
        scope->closeup = distance;
}

void Field::scope_target(const Vec3 &target)
{
    if (scope)
        scope->target = target;
}

bool Field::projection(const Vec3 &point, Vec3 &out)
{
    float e[3], t[3], half = 0.0F;
    if (!khdays_kh2_camera_view(e, t) || !khdays_kh2_camera_fov(&half) || half <= 0.0F)
        return false;
    float p[3];
    to_days(point, p);
    const auto unit = [](std::array<float, 3> v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 1e-9F)
            for (float &c : v)
                c /= l;
        return v;
    };
    // the view's axes: forward, right (forward x up), up
    const auto f = unit({t[0] - e[0], t[1] - e[1], t[2] - e[2]});
    const auto r = unit({-f[2], 0.0F, f[0]});
    const std::array<float, 3> u = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    const float d[3] = {p[0] - e[0], p[1] - e[1], p[2] - e[2]};
    const float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (z <= 1e-4F) {
        out = {1.0e6F, 1.0e6F, z * kKh2PerDays}; // behind the camera: on no part of the screen
        return true;
    }
    const float x = d[0] * r[0] + d[1] * r[1] + d[2] * r[2], y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
    // KH2's screen: 512 x 416 dots shown at 4:3, its projection's height the field of view's
    const float tall = std::tan(half);
    out = {x / (z * tall * (4.0F / 3.0F)) * 256.0F, -y / (z * tall) * 208.0F, z * kKh2PerDays};
    return true;
}

std::uint32_t Field::show_picture(Object &o, int number)
{
    Actor *a = o.actor();
    const auto it = a != nullptr ? by_actor.find(a) : by_actor.end();
    if (it == by_actor.end() || it->second->kind == nullptr)
        return 0;
    const std::uint32_t handle = next_picture++;
    pictures[handle] = Picture{o.address(), it->second->kind, number, 0, 0.0F, 0.0F};
    if (tracing())
        khdays_log("kh2 enemies: %08x shows its picture %08x, layout %d", it->second->days, handle, number);
    return handle;
}

void Field::picture_change(std::uint32_t handle, int number)
{
    const auto it = pictures.find(handle);
    if (it == pictures.end())
        return;
    it->second.number = number;
    ++it->second.changes;
    if (tracing())
        khdays_log("kh2 enemies: picture %08x to layout %d", handle, number);
}

void Field::picture_place(std::uint32_t handle, float x, float y)
{
    if (const auto it = pictures.find(handle); it != pictures.end())
        it->second.x = x, it->second.y = y;
}

// KH2's field pad's bits (YS::SoftPadTableField) as Days's buttons, by what they do: 8 the jump's CANCEL B, 9 and
// 26 square the guard's Y, 10 and 25 triangle the reaction command's X, 27 cross the attack's A, 24 circle B, 11
// R1 R, 23 L1 L, 14 to 16 and 28 the D-pad, 22 Select
bool Field::pad_held(int bit)
{
    static const std::map<int, int> keys = {{8, KHDAYS_KEY_B},     {9, KHDAYS_KEY_Y},      {10, KHDAYS_KEY_X},
                                            {11, KHDAYS_KEY_R},    {14, KHDAYS_KEY_UP},    {15, KHDAYS_KEY_DOWN},
                                            {16, KHDAYS_KEY_LEFT}, {28, KHDAYS_KEY_RIGHT}, {22, KHDAYS_KEY_SELECT},
                                            {23, KHDAYS_KEY_L},    {24, KHDAYS_KEY_B},     {25, KHDAYS_KEY_X},
                                            {26, KHDAYS_KEY_Y},    {27, KHDAYS_KEY_A}};
    const auto k = keys.find(bit);
    return k != keys.end() && (frame_held() & k->second) != 0;
}

// the way a Days player's D-pad holds (pad_ways): KH2's (sin yaw, 0, cos yaw), pushed all the way (a D-pad has no
// less)
bool Field::player_movement(Object &o, Vec3 &way, float &push)
{
    way = {};
    push = 0.0F;
    const unsigned object = days_object(o);
    const auto p = object != 0 ? pad_ways.find(object) : pad_ways.end();
    if (p == pad_ways.end())
        return false;
    if (p->second) {
        way = {std::sin(*p->second), 0.0F, std::cos(*p->second)};
        push = 1.0F;
    }
    return true;
}

// 1:354 / 1:356 KH2's 2D layers off and on: Days's HUD is its own (nothing done)
void Field::layers(bool on)
{
    if (tracing())
        khdays_log("kh2 enemies: KH2's 2D layers %s (1:35%c)", on ? "on" : "off", on ? '6' : '4');
}

bool Field::is_stopped(Object &o)
{
    Actor *a = o.actor();
    const auto it = a != nullptr ? by_actor.find(a) : by_actor.end();
    if (it == by_actor.end())
        return false;
    const Enemy &e = *it->second;
    return !e.stops.empty() || (e.days < kAllyIds && khdays_stop_held(KHDAYS_W(e.days)));
}

void Field::effect_pause(std::uint32_t handle, bool paused)
{
    if (Enemy::Running *r = running_effect(handle))
        r->paused = paused;
}

Actor *Field::xemnas(int n)
{
    for (Actor *x : objects()) {
        const std::uint32_t record = x->u32(field::kEntry);
        if (record == 0 || (mod().memory.u32(record + 0x4c) & 0xffffu) != 4007u)
            continue;
        const bool first = x->u32(field::kUnitArgs + 4) == 1u;
        if (first == (n == 0))
            return x;
    }
    return nullptr;
}

void Field::laser_start(Actor &caster)
{
    const auto it = by_actor.find(&caster);
    if (it == by_actor.end())
        return;
    laser_dome.emplace();
    laser_dome->caster = it->second->days;
    if (tracing())
        khdays_log("kh2 enemies: %08x starts the laser dome (8:3)", laser_dome->caster);
}

void Field::laser_attack()
{
    if (laser_dome) {
        laser_dome->phase = 3;
        laser_dome->time = 0.0F;
    }
    if (tracing())
        khdays_log("kh2 enemies: the laser dome fires (8:4)");
}

void Field::laser_end()
{
    if (laser_dome)
        laser_dome->phase = 7;
    if (tracing())
        khdays_log("kh2 enemies: the laser dome ends (8:5)");
}

void Field::laser_optimize(bool on)
{
    map_hidden = on;
    if (tracing())
        khdays_log("kh2 enemies: the room's map group 0, sky and glow %s (8:%c)", on ? "hidden" : "shown",
                   on ? '6' : '7');
}

bool Field::event_playing() const
{
    return playing.has_value();
}

bool Field::in_battle()
{
    return std::any_of(enemies.begin(), enemies.end(),
                       [](const auto &x) { return x.second->started && !x.second->dying && !x.second->released; });
}

Friend *Field::friend_of(Actor &a)
{
    const auto it = by_actor.find(&a);
    const auto f = it != by_actor.end() ? friends.find(it->second->days) : friends.end();
    return f != friends.end() ? &f->second : nullptr;
}

void Field::friend_target(Actor &a, int mode, Object *target)
{
    Friend *f = friend_of(a);
    Actor *t = target != nullptr ? target->actor() : nullptr;
    const auto it = t != nullptr ? by_actor.find(t) : by_actor.end();
    if (f != nullptr)
        f->target = it != by_actor.end() && enemies.count(it->second->days) != 0 ? it->second->days : 0u;
    if (tracing())
        khdays_log("kh2 enemies: its script sets the partner's target (10:14, mode %d)", mode);
}

void Field::friend_turning(Actor &a, bool own)
{
    if (Friend *f = friend_of(a))
        f->own_turning = own;
}

void Field::friend_action_clear(Actor &a)
{
    if (Friend *f = friend_of(a)) {
        f->action = -1;
        f->action_time = 0.0F;
    }
}

std::uint32_t Field::friend_action_param(Actor &a, int id)
{
    Friend *f = friend_of(a);
    const FriendAction *x = f != nullptr ? friend_action(*f, id) : nullptr;
    if (x == nullptr)
        return 0;
    static std::map<std::pair<const FriendParams *, int>, std::uint32_t> copies;
    std::uint32_t &at = copies[{f->params, id}];
    if (at == 0) {
        at = mod().memory.add(0x80, "a party member's action " + std::to_string(id));
        mod().memory.write(at, x->raw.data(), x->raw.size());
    }
    return at;
}

bool Field::friend_ability(Actor &, int)
{
    return true; // (the battle's partner has his own: none of Days's to ask)
}

float Field::magic_points(Object &o)
{
    Actor *x = o.actor();
    Friend *f = x != nullptr ? friend_of(*x) : nullptr;
    return f != nullptr ? f->mp : 0.0F;
}

void Field::magic_use(Object &o)
{
    Actor *x = o.actor();
    if (Friend *f = x != nullptr ? friend_of(*x) : nullptr)
        f->mp = std::max(0.0F, f->mp - float(f->use_mp));
}

void Field::magic_use_set(Object &o, int points)
{
    Actor *x = o.actor();
    if (Friend *f = x != nullptr ? friend_of(*x) : nullptr)
        f->use_mp = points;
}

void Field::friend_watch(Actor &a, std::uint32_t effect)
{
    if (Friend *f = friend_of(a))
        f->watched = effect;
}

bool Field::friend_watching(Actor &a, std::uint32_t effect)
{
    Friend *f = friend_of(a);
    return f != nullptr && f->watched == effect && running_effect(effect) != nullptr;
}

bool Field::hooked(Object &o)
{
    const Actor *x = o.actor();
    return x != nullptr && x->hooked();
}

void Field::special_command(Object &o, int number)
{
    Actor *a = o.actor();
    const auto it = a != nullptr ? by_actor.find(a) : by_actor.end();
    // SPECIAL_COMMAND::_check's tables by number (the US build's): those KH2's bosses here give
    static const std::map<int, std::array<int, 4>> tables = {{12, {454, 653, 652, -1}}};
    const auto t = tables.find(number);
    if (it == by_actor.end() || t == tables.end()) {
        khdays_log("kh2 enemies: %s's special command %d (10:6) not known: the player's menu stays his",
                   o.name().c_str(), number);
        return;
    }
    special_menu = {it->second->days, t->second};
    command_image_of = it->second->days; // in its own pictures (its "comd": STDOBJ::set_special_command)
    top_offer.enemy = special_menu.enemy;
    top_offer.commands = special_menu.commands;
    top_offer.frame = khdays_link_frame();
    deck_sync();
    if (tracing())
        khdays_log("kh2 enemies: %08x gives the player's special command menu %d (10:6): %d %d %d", special_menu.enemy,
                   number, t->second[0], t->second[1], t->second[2]);
}

void Field::special_command_off(Object &o)
{
    Actor *a = o.actor();
    const auto it = a != nullptr ? by_actor.find(a) : by_actor.end();
    if (it == by_actor.end() || special_menu.enemy != it->second->days)
        return;
    special_menu = {};
    if (command_image_of == it->second->days)
        command_image_of = 0;
    top_offer.frame = khdays_link_frame() - 3u; // (no longer standing)
    deck_sync();
    if (tracing())
        khdays_log("kh2 enemies: %08x takes its special command menu off (10:7)", it->second->days);
}

void Field::screen_shake(float life, float period_x, float period_y, float size_x, float size_y, float speed_x,
                         float speed_y)
{
    ScreenShake k;
    k.life = life;
    k.period = {period_x, period_y};
    k.size = {size_x, size_y};
    k.speed = {speed_x, speed_y};
    shell::screen_shake = k;
    if (tracing())
        khdays_log("kh2 enemies: the screen shakes for %.0f ticks (%.0f by %.0f dots)", double(life), double(size_x),
                   double(size_y));
}

void Field::screen_white(bool out, float frames)
{
    if (frames > 0.0F) {
        shell::screen_white.step = (out ? 1.0F : -1.0F) / frames;
    } else {
        shell::screen_white.level = out ? 1.0F : 0.0F;
        shell::screen_white.step = 0.0F;
    }
    if (tracing())
        khdays_log("kh2 enemies: the screen fades %s white over %.0f frames", out ? "to" : "back from", double(frames));
}

int Field::bg_cross(Actor &a, const Vec3 &from, const Vec3 &to, Vec3 &hit)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return -1;
    float f[3], t[3], h[3];
    unsigned face = 0;
    to_days(from, f);
    to_days(to, t);
    if (!khdays_kh2_shell_ray_face(shell_of(*it->second), f, t, h, &face))
        return -1;
    hit = to_kh2(h);
    // the face's record names it (the collision's own memory, below 0x80000000 as all the port's)
    return int(face & 0x7fffffffu);
}

bool Field::bg_normal(int polygon, Vec3 &normal)
{
    float n[3];
    if (polygon <= 0 || !khdays_kh2_shell_face_normal(unsigned(polygon), n))
        return false;
    normal = days_way({n[0], n[1], n[2]});
    return true;
}

void Field::camera_watch(Object &o)
{
    watched = o.address();
    if (tracing())
        khdays_log("kh2 enemies: the reaction camera watches %08x", watched);
}

void Field::camera_home()
{
    watched = 0;
}

void Field::camera_warp()
{
    camera_warped = true;
    if (tracing())
        khdays_log("kh2 enemies: the camera is put at its place at once");
}

std::uint32_t Field::light_create(int kind)
{
    const std::uint32_t h = lights.create(kind);
    if (tracing())
        khdays_log("kh2 enemies: light %u made, kind %d", h, kind);
    return h;
}

void Field::light_flag(std::uint32_t handle, int bit)
{
    lights.set_flag(handle, bit);
}

void Field::light_colour(std::uint32_t handle, int r, int g, int b, float ticks)
{
    lights.set_colour(handle, r, g, b, ticks);
    if (tracing())
        khdays_log("kh2 enemies: light %u to %d %d %d over %.0f ticks", handle, r, g, b, double(ticks));
}

void Field::light_fade_out(std::uint32_t handle, float ticks)
{
    lights.fade_out(handle, ticks);
    if (tracing())
        khdays_log("kh2 enemies: light %u fades out over %.0f ticks", handle, double(ticks));
}

void Field::light_fade_in(std::uint32_t handle, float ticks)
{
    lights.fade_in(handle, ticks);
    if (tracing())
        khdays_log("kh2 enemies: light %u fades in over %.0f ticks", handle, double(ticks));
}

bool Field::attack_on_floor(std::uint32_t at)
{
    const auto o = attacks.find(at);
    return o != attacks.end() && o->second.on_floor;
}

bool Field::ray(Actor &a, const Vec3 &from, const Vec3 &to, Vec3 &hit)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return false;
    float f[3], t[3], h[3];
    to_days(from, f);
    to_days(to, t);
    if (!khdays_kh2_shell_ray(shell_of(*it->second), f, t, h))
        return false;
    hit = to_kh2(h);
    return true;
}

bool Field::wall_sweep(Actor &a, const Vec3 &place, float radius, Vec3 &way, bool slide)
{
    const auto it = by_actor.find(&a);
    return it != by_actor.end() &&
           shell::wall_sweep(shell_of(*it->second), place, radius > 0.0F ? radius : it->second->radius * kKh2PerDays,
                             way, slide);
}

bool Field::record_distance(std::uint32_t record, const Vec3 &point, float &out)
{
    std::uint32_t r[4] = {};
    if (!mod().memory.read(record, r, sizeof r))
        return false;
    Vec3 at{};
    float radius = 0.0F;
    if (const auto o = attacks.find(r[2]); o != attacks.end()) { // an attack: its sphere
        at = o->second.at;
        radius = o->second.radius;
    } else if (Object *x = Object::at(mod().memory, r[0]); x == nullptr || !aim_point(*x, at)) {
        return false;
    }
    const float d[3] = {point[0] - at[0], point[1] - at[1], point[2] - at[2]};
    out = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - radius;
    return true;
}

void Field::attack_on_hit(std::uint32_t at, Actor &, std::uint32_t routine, std::uint32_t value)
{
    if (const auto o = attacks.find(at); o != attacks.end())
        o->second.on_hit = routine, o->second.on_hit_value = value;
}

void Field::attack_on_reflect(std::uint32_t at, Actor &, std::uint32_t routine, std::uint32_t value)
{
    if (const auto o = attacks.find(at); o != attacks.end())
        o->second.on_reflect = routine, o->second.on_reflect_value = value;
}

bool Field::aim_point(Object &o, Vec3 &out)
{
    const Vec3 p = o.place();
    if (days_object(o) != 0) { // a player: his middle (his height's half up; KH2's y down)
        out = {p[0], p[1] - kPlayerMiddle * kKh2PerDays, p[2]};
        return true;
    }
    Actor *x = o.actor();
    if (x == nullptr)
        return false;
    for (const ShapeAt &s : shapes(*x, 3)) {
        out = s.at;
        return true;
    }
    return false;
}

void Field::stop_effect(std::uint32_t handle)
{
    for (auto *list : {&enemies, &allies})
        for (auto &[days, e] : *list)
            for (auto r = e->fx.begin(); r != e->fx.end(); ++r)
                if (r->handle == handle) {
                    if (trace_level() >= 2)
                        khdays_log("kh2 enemies: effect %08x ended at once by a script", handle);
                    e->fx.erase(r);
                    return;
                }
}

void Field::effect_loop_end(std::uint32_t handle)
{
    if (Enemy::Running *r = running_effect(handle)) {
        if (trace_level() >= 2)
            khdays_log("kh2 enemies: effect %08x closed by a script (its loops end)", handle);
        r->fx->stop();
    }
}

void Field::offer(Actor &by, int command, Object *source)
{
    Actor *from = source != nullptr && source->actor() != nullptr ? source->actor() : &by;
    const auto it = by_actor.find(from);
    if (it == by_actor.end() || command < 0)
        return;
    script_offer = {it->second->days, command, khdays_link_frame()};
}

// KH2's pad's bits as Days's buttons, by what they do: the D-pad, L1 and R1 the shoulders, triangle the reaction
// command's X, cross the attack's A, circle the jump's B, square the guard's Y, Start, Select; L2, R2 and the
// sticks' presses none
bool Field::pad_pressed(int bit)
{
    static const std::map<int, int> keys = {{0, KHDAYS_KEY_SELECT}, {3, KHDAYS_KEY_START}, {4, KHDAYS_KEY_UP},
                                            {5, KHDAYS_KEY_RIGHT},  {6, KHDAYS_KEY_DOWN},  {7, KHDAYS_KEY_LEFT},
                                            {10, KHDAYS_KEY_L},     {11, KHDAYS_KEY_R},    {12, KHDAYS_KEY_X},
                                            {13, KHDAYS_KEY_B},     {14, KHDAYS_KEY_A},    {15, KHDAYS_KEY_Y}};
    const auto k = keys.find(bit);
    return k != keys.end() && (frame_pressed() & k->second) != 0;
}

void Field::override_top(Actor &by, const std::array<int, 4> &commands, Object *target)
{
    Actor *to = target != nullptr && target->actor() != nullptr ? target->actor() : &by;
    const auto it = by_actor.find(to);
    if (it == by_actor.end())
        return;
    const unsigned now = khdays_link_frame();
    if (!top_menu_standing()) {
        if (tracing())
            khdays_log("kh2 enemies: %08x gives the player's command menu %d %d %d %d", it->second->days, commands[0],
                       commands[1], commands[2], commands[3]);
    } else if (tracing() && commands != top_offer.commands)
        khdays_log("kh2 enemies: %08x's command menu now %d %d %d %d", it->second->days, commands[0], commands[1],
                   commands[2], commands[3]);
    const bool changed = !top_offer.shown || commands != top_offer.commands;
    top_offer.enemy = it->second->days;
    top_offer.commands = commands;
    top_offer.frame = now;
    if (changed) // on the deck at once ("The End" in its row the tick its range begins)
        deck_sync();
}

std::vector<Actor *> Field::actors()
{
    std::vector<Actor *> out;
    for (auto &[days, e] : enemies)
        if (!e->dying)
            out.push_back(e->actor.get());
    return out;
}

// 1:17: an ally goes (allies_close lets it go); an enemy's Days enemy stays, its script going on
void Field::leave(Actor &a)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return;
    if (it->second->anchor != 0)
        it->second->released = true;
    if (tracing())
        khdays_log("kh2 enemies: %08x made to leave (1:17)%s", it->second->days,
                   it->second->anchor != 0 ? ": it goes" : ": its Days enemy stays");
}

// 6:46, 6:47: every KH2 object of the field but those two held while the stop is on it (KH2's stop record's
// kinds' flags are not read: held whole, as Days's Stop holds them)
void Field::stop_all(int stop, Object *but, Object *nor)
{
    for (auto *list : {&enemies, &allies})
        for (auto &[days, e] : *list)
            if (e->actor.get() != but && e->actor.get() != nor)
                e->stops.insert(stop);
    if (tracing())
        khdays_log("kh2 enemies: stop %d put on the field's KH2 objects", stop);
}

void Field::signal(int id, int argument)
{
    signals.emplace_back(id, argument);
    if (tracing())
        khdays_log("kh2 enemies: signal %d (%d) sent", id, argument);
}

// MISSION_COUNT::set (the PS3 build's): counter `index` kept between 0 and its most; reaching its most from below
// sends 123, reaching 0 from above 14, with its index, while its pieces are up (the mission's script completes on
// 14)
void Field::set_count(Kh2Mission &m, int index, int n)
{
    MissionPiece *c = mission_piece(m, 5, index);
    if (c == nullptr)
        return;
    const int was = c->value;
    if (n >= c->most) {
        if (was < c->most && m.shown)
            signal(123, index);
        n = c->most;
    } else if (n <= 0) {
        if (was > 0 && m.shown)
            signal(14, index);
        n = 0;
    }
    check_warning(m, *c, n);
    c->value = n;
    khdays_log("kh2 enemies: mission %s's count %d", m.name.c_str(), n);
}

// YS::MISSION2D::check_warning (the PS3 build's), as a counter's or a gauge's value is set: with a warning above 0,
// on once the value falls to it -- or, when the piece starts at or below it, once the value reaches it -- else off
void Field::check_warning(const Kh2Mission &m, MissionPiece &p, int value)
{
    if (p.warning <= 0)
        return;
    const bool on = p.start <= p.warning ? value >= p.warning : value <= p.warning;
    if (on != p.alert) {
        p.alert = on;
        khdays_log("kh2 enemies: mission %s's %s %d warns%s", m.name.c_str(), p.kind == 6 ? "gauge" : "counter",
                   p.index, on ? "" : " no more");
    }
}

// YS::MISSION::WarningCount, WarningGauge (4:50, 4:58, 4:57): a counter's or a gauge's warning set or reset by its
// script, whatever its value
void Field::mission_warning(int kind, int index, bool on)
{
    if (!kh2_mission)
        return;
    MissionPiece *p = mission_piece(*kh2_mission, kind, index);
    if (p == nullptr || p->alert == on)
        return;
    p->alert = on;
    khdays_log("kh2 enemies: mission %s's %s %d warns%s (its script)", kh2_mission->name.c_str(),
               kind == 6 ? "gauge" : "counter", index, on ? "" : " no more");
}

// YS::MISSION_GAUGE::set (the PS3 build's): gauge `index` kept between 0 and its most; reaching 0 from above
// sends 136, its most from below 137, with its index, while its pieces are up
void Field::set_gauge(Kh2Mission &m, int index, float level)
{
    MissionPiece *g = mission_piece(m, 6, index);
    if (g == nullptr)
        return;
    const float was = g->level, most = float(g->most);
    if (level <= 0.0F) {
        if (was > 0.0F && m.shown)
            signal(136, index);
        level = 0.0F;
    } else if (level >= most) {
        if (was < most && m.shown)
            signal(137, index);
        level = most;
    }
    check_warning(m, *g, int(level));
    g->level = level;
    g->value = int(level);
    if (trace_level() >= 2)
        khdays_log("kh2 enemies: mission %s's gauge %d at %.2f", m.name.c_str(), index, double(level));
}

float Field::mission_gauge(unsigned number, int index, float value)
{
    if (!kh2_mission)
        return 0.0F;
    Kh2Mission &m = *kh2_mission;
    const MissionPiece *g = mission_piece(m, 6, index);
    if (g == nullptr)
        return 0.0F;
    if (number == 12) // GetGaugeRatio
        return g->most != 0 ? g->level / float(g->most) : 0.0F;
    if (number == 8) { // SetGauge
        set_gauge(m, index, value);
        return 0.0F;
    }
    // AddGauge: KH2 PC at 60 frames a second (its tick's length under 2, the port's 1) halves some missions' steps
    // (FUN_140433750): PO01_MS501's, TT12_WORK_BOX's and HB32_FM_VEX's; TR07_MS203's rises and TR04_MS104's falls;
    // then EH15_MS104's (Saïx's data battle) of -5 and above
    const auto in = [&](const char *name) { return m.name.find(name) != std::string::npos; };
    if (in("PO01_MS501") || in("TT12_WORK_BOX") || in("HB32_FM_VEX"))
        value *= 0.5F;
    if (in(value >= 0.0F ? "TR07_MS203" : "TR04_MS104"))
        value *= 0.5F;
    if (in("EH15_MS104") && value >= -5.0F)
        value *= 0.5F;
    set_gauge(m, index, g->level + value);
    return 0.0F;
}

Object *Field::attack_owner(std::uint32_t at)
{
    if (days_attack && at == days_attack->address())
        return days_attack_owner != 0 ? Object::at(mod().memory, days_attack_owner) : nullptr;
    for (const auto &[days, e] : enemies) // its motion's attack (motion_attack): the enemy
        if (at != 0 && e->attack_record == at && e->actor)
            return e->actor.get();
    const auto it = attacks.find(at);
    return it != attacks.end() && it->second.owner != 0 ? Object::at(mod().memory, it->second.owner) : nullptr;
}

bool Field::star(const Object &o)
{
    const auto h = held.find(days_object(o));
    return h != held.end() && h->second.unhittable;
}

std::uint32_t Field::player_sheet(const Object &o)
{
    const unsigned object = days_object(o);
    const auto actor = player_actors.find(object);
    if (object == 0 || actor == player_actors.end())
        return 0;
    PlayerSheet &s = player_sheets[object];
    if (s.sheet == 0) {
        s.sheet = mod().memory.add(0x270, "a player's sheet", true);
        s.part = mod().memory.add(0x20, "a player's PartRam", true);
    }
    const int hp = std::max(0, khdays_kh2_shell_player_hp(actor->second));
    const int most = std::max(1, khdays_kh2_shell_player_most_hp(actor->second));
    const int level = khdays_kh2_shell_player_level(actor->second);
    mod().memory.set_u32(s.sheet, std::uint32_t(hp));
    mod().memory.set_u32(s.sheet + 4, std::uint32_t(most));
    mod().memory.set_u32(s.sheet + 8, 0);
    mod().memory.set_u32(s.sheet + 0x244, s.part);
    mod().memory.set_u32(s.sheet + 0x268, o.address());
    // (KH2's levels end at 99: Marluxia's doom counts down from it in two figures -- a Days player at 100 began
    // at "00")
    mod().memory.set_u32(s.part + 0xc, std::uint32_t(std::clamp(level, 0, 99)) << 24);
    if (tracing() && (s.told_hp != hp || s.told_level != level)) {
        s.told_hp = hp, s.told_level = level;
        khdays_log("kh2 enemies: player %08x's sheet: health %d of %d, level %d", object, hp, most, level);
    }
    return s.sheet;
}

int Field::player_health(const Object &o)
{
    const auto actor = player_actors.find(days_object(o));
    return actor != player_actors.end() ? khdays_kh2_shell_player_hp(actor->second) : -1;
}

int Field::add_player_health(const Object &o, int amount, int bar)
{
    const unsigned object = days_object(o);
    const auto actor = player_actors.find(object);
    if (object == 0 || actor == player_actors.end() || bar != 0)
        return 0;
    const int hp = std::max(0, khdays_kh2_shell_player_hp(actor->second));
    const int most = std::max(1, khdays_kh2_shell_player_most_hp(actor->second));
    const int now = std::clamp(hp + amount, 0, most);
    khdays_kh2_shell_player_set_hp(actor->second, now);
    if (tracing())
        khdays_log("kh2 enemies: player %08x's health %d to %d (a script's %d)", object, hp, now, amount);
    return now;
}

int Field::mission(unsigned number, int first, int second)
{
    if (!kh2_mission)
        return 0;
    Kh2Mission &m = *kh2_mission;
    const auto count = [&](int index) {
        const MissionPiece *c = mission_piece(m, 5, index);
        return c != nullptr ? c->value : 0;
    };
    const auto end = [&](const char *how) {
        if (m.over)
            return;
        m.over = true;
        m.shown = false;
        m.end = how;
        khdays_log("kh2 enemies: mission %s %s", m.name.c_str(), how);
        const ai::Value none{};
        m.actor->machine().raise(2, {&none, 0});
    };
    switch (number) {
    case 3: end("completed"); break;
    case 11: end("failed"); break;
    case 16: end("over: its boss defeated"); break;
    case 22: end("left"); break;
    // (4:5 and 4:41 name the count first, then the counter: YS::MISSION::SetCount(n, index), AddCount(n, index))
    case 5: set_count(m, second, first); break;
    case 6: set_count(m, first, count(first) + 1); break;
    case 20: set_count(m, first, count(first) - 1); break;
    case 41: set_count(m, second, count(second) + first); break;
    case 29: return count(first);
    case 14: m.shown = !m.over; break;
    case 15: m.shown = false; break;
    case 21: return m.shown && !m.over ? 1 : 0;
    case 54: m.locked = true; break;
    case 55: return m.locked ? 1 : 0;
    default: break;
    }
    return 0;
}

void Field::vacuum(std::uint32_t record, bool alive)
{
    std::erase(vacuums, record);
    if (alive)
        vacuums.push_back(record);
}

void Field::stop_end_all(int stop)
{
    for (auto *list : {&enemies, &allies})
        for (auto &[days, e] : *list)
            e->stops.erase(stop);
    if (tracing())
        khdays_log("kh2 enemies: stop %d taken off", stop);
}

std::vector<Actor *> Field::objects()
{
    std::vector<Actor *> out = actors();
    for (auto &[id, e] : allies)
        out.push_back(e->actor.get());
    return out;
}

std::vector<World::ShapeAt> Field::shapes(Actor &a, int kind)
{
    std::vector<ShapeAt> out;
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return out;
    Enemy &e = *it->second;
    const std::set<int> on = groups_on(e);
    KhdaysKh2Shell now{};
    to_days(a.place(), now.place);
    if (e.pose.empty())
        pose(e);
    for (const Shape &sh : e.kind->shapes)
        if ((sh.kind & 0xff) == kind && on.count(sh.group) != 0) {
            float d[3];
            days_point(e, now, sh.bone, sh.at, d);
            out.push_back({sh.group, to_kh2(d), sh.radius});
        }
    return out;
}

// its model drawn by Days in the frame before or this one (whether Days leaves out what its camera does not see
// is not known: a drawn enemy counts as seen)
bool Field::takes_off_later(Actor &a)
{
    const auto it = by_actor.find(&a);
    const kh2::MotionAttributes *attr =
        it != by_actor.end() && a.motion.id >= 0 ? it->second->motions->attributes(a.motion.id) : nullptr;
    if (attr != nullptr)
        for (const auto &t : attr->triggers)
            if (t.id == 0 && float(t.frame) >= a.motion.time)
                return true;
    return false;
}

bool Field::on_screen(Actor &a)
{
    const auto it = by_actor.find(&a);
    return it != by_actor.end() && it->second->drawn_frame != ~0u &&
           khdays_link_frame() - it->second->drawn_frame <= 1u;
}

Object *Field::player(Actor &a)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return nullptr;
    std::vector<KhdaysKh2Player> list(
        std::size_t(std::max(0, khdays_kh2_shell_players(shell_of(*it->second), nullptr, 0))));
    const int n = khdays_kh2_shell_players(shell_of(*it->second), list.data(), int(list.size()));
    const Vec3 at = a.place();
    Object *best = nullptr;
    float best_d = 0.0F;
    for (int i = 0; i < n && i < int(list.size()); ++i) {
        auto &o = players[list[std::size_t(i)].object];
        if (!o) {
            o = std::make_unique<Object>(mod().memory, "a player");
            o->set_bits(field::kFlags6c8, bit::kPlayer, true); // KH2's player (FUN_1403a7a40)
        }
        if (const auto h = held.find(list[std::size_t(i)].object); h != held.end()) {
            o->set_place(h->second.place); // where the script and its motion put it
            o->set_yaw(h->second.yaw);
        } else {
            o->set_place(to_kh2(list[std::size_t(i)].place));
        }
        const Vec3 p = o->place();
        const float d = (p[0] - at[0]) * (p[0] - at[0]) + (p[2] - at[2]) * (p[2] - at[2]);
        if (best == nullptr || d < best_d)
            best = o.get(), best_d = d;
    }
    return best;
}

bool Field::capture_motion(Actor &target, Actor &by, int slot, float blend)
{
    const auto t = by_actor.find(&target), b = by_actor.find(&by);
    const std::uint32_t record = target.u32(field::kEntry);
    if (t == by_actor.end() || b == by_actor.end() || record == 0 || !t->second->motions || !b->second->motions)
        return false;
    const int character = int(mod().memory.u32(record + 4) >> 24); // (its weapon joint: sklt's character)
    char body[8], weapon[8];
    std::snprintf(body, sizeof body, "%03d0", character);
    std::snprintf(weapon, sizeof weapon, "%03d1", character);
    const kh2::MotionSet::Variant *part = b->second->motions->part(slot, body);
    if (part == nullptr)
        return false;
    Enemy &e = *t->second;
    e.motions->capture(slot, b->second->motions->set(), part);
    e.capture_weapon = b->second->motions->part(slot, weapon);
    target.set_bits(field::kStatus, bit::kOtherMotion, true);
    target.play(slot, blend, 0.0F);
    if (tracing())
        khdays_log("kh2 enemies: %08x captured in %08x's motion %d (its part %s%s)", e.days, b->second->days, slot,
                   body, e.capture_weapon != nullptr ? ", its weapon's too" : "");
    return true;
}

void Field::player_place(Object &o, const Vec3 &place)
{
    if (const auto h = held.find(days_object(o)); h != held.end())
        h->second.place = place;
}

void Field::player_move(Object &o, const Vec3 &way, float speed)
{
    if (const auto h = held.find(days_object(o)); h != held.end())
        h->second.move = {way[0] * speed, way[1] * speed, way[2] * speed};
}

void Field::player_carry(Object &o, Object *by)
{
    const unsigned object = days_object(o);
    const auto h = held.find(object);
    Actor *x = by != nullptr ? by->actor() : nullptr;
    const auto it = x != nullptr ? by_actor.find(x) : by_actor.end();
    if (h == held.end())
        return;
    h->second.carried_by = it != by_actor.end() ? it->second->days : 0u;
    if (tracing())
        khdays_log(h->second.carried_by != 0 ? "kh2 enemies: player %08x put on %08x (1:18)"
                                             : "kh2 enemies: player %08x let down (1:102)",
                   object, h->second.carried_by);
}

void Field::player_hide(Object &o, bool hidden)
{
    const unsigned object = days_object(o);
    hidden_players[object] = hidden;
    if (tracing())
        khdays_log("kh2 enemies: player %08x %s (1:%d)", object, hidden ? "hidden" : "shown", hidden ? 133 : 134);
}

void Field::player_steer(Object &o, const Vec3 &way, float speed)
{
    if (const auto h = held.find(days_object(o)); h != held.end())
        h->second.steer = {way[0] * speed, way[1] * speed, way[2] * speed};
}

void Field::player_yaw(Object &o, float yaw)
{
    if (const auto h = held.find(days_object(o)); h != held.end())
        h->second.yaw = yaw;
}

void Field::player_let_go(Object &o, const char *why)
{
    if (const unsigned object = days_object(o); held.count(object) != 0)
        let_go(object, why);
}

void Field::form_spec(int action, const std::vector<float> &numbers)
{
    (action == 0x2b ? dice_spec : card_spec) = numbers;
}

} // namespace khdays::kh2_enemies::shell
