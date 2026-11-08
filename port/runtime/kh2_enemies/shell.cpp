// KINGDOM HEARTS II's enemies in Days's field (shell.h): the mod's files, each shell's KH2 actor ticked at KH2's 60
// a second, its travel put into the Days enemy's place, its attacks, its reactions and its end, its model drawn.
#include "shell.h"

#include "actor.h"
#include "calls.h"
#include "kinds.h"
#include "shell_days.h"

#include "khdays/assets/kh2_ai.h"
#include "khdays/assets/kh2_motion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>

#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "../scripts/game_link.h"
#include "../../gfx/g3d.h"
#include "../battle/stop_magic.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../core/origin.h"
#include "../core/window.h"
#include "../missions/missions.h"
#include "../missions/registry.h"
#include "../scripts/enemies_link.h"
#include "../mods/textures.h"
#include "../sound/sound_bridge.h"
#include "../sound/sound_shots.h"
#include "../../sound/archive.h"
}
#include "../mods/kh2_fx_draw.h"
#include "../mods/models.h"

namespace {

namespace fs = std::filesystem;
namespace kh2 = khdays::assets::kh2;
namespace ai = khdays::assets::kh2::ai;
using namespace khdays::kh2_enemies;

// KH2's units are centimetres, Days's about metres (docs/KH2_MODELS.md: a KH2 character comes out at a Days
// character's size by x 0.01), and KH2's world's y points down: a Days place (x, y, z) is KH2's (100 x, -100 y, 100 z)
constexpr float kKh2PerDays = 100.0F;
constexpr float kGravity = 1.0F / 0.408163F; // KH2's, a tick a tick (actor.cpp)
constexpr int kNoBone = 0x4000;
// how long an enemy's end may take before its Days defeat goes on without it (a script that never fades out): ticks
constexpr unsigned kLongestEnd = 60u * 10u;

// KHDAYS_TRACE_KH2_ENEMIES: 1 what each enemy does; 2 also where each of its script's threads stands, once a second;
// 3 also every call its script makes (trace_calls)
int trace_level()
{
    static int level = -1;
    if (level < 0) {
        const char *v = std::getenv("KHDAYS_TRACE_KH2_ENEMIES");
        level = v != nullptr && *v >= '0' && *v <= '9' ? *v - '0' : v != nullptr && *v != '\0' ? 1 : 0;
    }
    return level;
}

bool tracing()
{
    return trace_level() > 0;
}

Vec3 to_kh2(const float d[3])
{
    return {d[0] * kKh2PerDays, -d[1] * kKh2PerDays, d[2] * kKh2PerDays};
}

void to_days(const Vec3 &k, float out[3])
{
    out[0] = k[0] / kKh2PerDays;
    out[1] = -k[1] / kKh2PerDays;
    out[2] = k[2] / kKh2PerDays;
}

std::vector<std::uint8_t> read_file(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

fs::path u8(const std::string &s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

// ------------------------------------------------------------------------------------------------- the mod's files

// One of an .mdlx's collision shapes (its BAR entry 0x17: a 64-byte header, its count first, then 20-byte records):
// its group (+0), its kind (+4: 0x300 the body's cylinder, 0x302 a sphere on a bone, 0x306 an attack's sphere, 5 a
// box), its bone (+6, 0x4000 none), its offset on it (+8, +10, +12), its height (+14) and radius (+16); KH2's cm.
struct Shape final {
    int group = 0, kind = 0, bone = kNoBone;
    Vec3 at{};
    float height = 0.0F, radius = 0.0F;
};

std::vector<Shape> read_shapes(const std::vector<std::uint8_t> &mdlx)
{
    std::vector<Shape> out;
    const auto u16 = [&](std::size_t o) { return std::uint16_t(mdlx[o] | (mdlx[o + 1] << 8)); };
    const auto u32 = [&](std::size_t o) { return std::uint32_t(u16(o) | (std::uint32_t(u16(o + 2)) << 16)); };
    if (mdlx.size() < 16 || std::memcmp(mdlx.data(), "BAR", 3) != 0)
        return out;
    const std::uint32_t entries = u32(4);
    for (std::uint32_t i = 0; i < entries && 16 + 16 * (i + 1) <= mdlx.size(); ++i) {
        const std::size_t e = 16 + 16 * i;
        const std::uint32_t at = u32(e + 8), size = u32(e + 12);
        if (u16(e) != 0x17 || std::size_t(at) + size > mdlx.size() || size < 64)
            continue;
        const std::uint32_t count = u32(at);
        for (std::uint32_t k = 0; k < count && 64 + 20 * (k + 1) <= size; ++k) {
            const std::size_t r = at + 64 + 20 * k;
            Shape s;
            s.group = mdlx[r];
            s.kind = u16(r + 4);
            s.bone = u16(r + 6);
            s.at = {float(std::int16_t(u16(r + 8))), float(std::int16_t(u16(r + 10))),
                    float(std::int16_t(u16(r + 12)))};
            s.height = float(std::int16_t(u16(r + 14)));
            s.radius = float(std::int16_t(u16(r + 16)));
            out.push_back(s);
        }
    }
    return out;
}

// An effect's element, as KH2FM.exe's FUN_1402c6b30 starts one (the PAX's records of 0x50 bytes from + 0x10, their
// count at + 8): u16 its effect (the 0x82 directory's entry of that number), u16 its number (what a motion's trigger 1
// and 1:87 name), + 6 u16 the bone it follows (0x4000 none: the actor itself; FUN_1403b50f0), + 0x10 flags, + 0x14
// when its sound starts (its time, ticks), + 0x18 its sound (KH2's id, played above 0: FUN_1402c58f0), + 0x1c its
// place, + 0x28 its turn (radians), + 0x34 its size -- KH2's axes (y down), centimetres
struct Element final {
    std::size_t effect = 0, emitter = 0; // in KindFiles::fx
    int bone = kNoBone;
    std::uint32_t flags = 0;
    float start = 0.0F;
    int sound = 0;
    std::array<float, 3> place{}, turn{}, size{1.0F, 1.0F, 1.0F};
};

// An enemy kind's files, read once: its script (the .mdlx's AI, BAR entry 3), its motions and skeleton, its shapes.
struct KindFiles final {
    std::string name, mdlx;
    std::shared_ptr<const ai::Script> script;
    std::shared_ptr<const kh2::MotionSet> set;
    std::shared_ptr<const kh2::MotionSkeleton> skeleton;
    std::vector<Shape> shapes;
    // its effects (its .a.us's PAX): the effects, the elements by their number, the pages of its pictures and of KH2's
    // resident ones (effect/texcommon.dpd)
    std::vector<std::shared_ptr<const kh2::EffectBlock>> fx;
    std::map<int, Element> fx_elements;
    // the sounds it plays (its motions' triggers 8, its elements'): KH2's sound id -> the mod's kh2/se/<id>.wav
    std::map<int, int> sounds;
    kh2::EffectBlock common;
    std::unique_ptr<khdays::kh2_fx::Pages> fx_pages;
};

// A PAX's elements (docs/FORMATS.md, "KH2's effects"): its 0x82 directory's 32-byte entries, each the offset of its
// effect from the directory (+0), its emitter (+4) and its number (+8), the effects listed after them by offset; then
// the records before the directory (Element), each naming a directory entry by its number. A PAX without records: the
// directory's entries themselves, by their numbers.
std::map<int, Element> pax_elements(const std::vector<std::uint8_t> &pax)
{
    const auto u32 = [&](std::size_t o) {
        return o + 4 <= pax.size()
                   ? std::uint32_t(pax[o] | (pax[o + 1] << 8) | (pax[o + 2] << 16) | (std::uint32_t(pax[o + 3]) << 24))
                   : 0u;
    };
    const auto f32 = [&](std::size_t o) {
        const std::uint32_t b = u32(o);
        float f;
        std::memcpy(&f, &b, 4);
        return std::isfinite(f) ? f : 0.0F;
    };
    const std::size_t d = u32(0xc);
    const std::uint32_t n = u32(d + 12);
    const std::size_t list = d + 16 + std::size_t(32) * n;
    const std::uint32_t effects = u32(list);
    std::map<std::uint32_t, std::size_t> index_of;
    for (std::uint32_t i = 0; i < effects; ++i)
        index_of[u32(list + 4 + 4 * i)] = i;
    std::map<int, std::pair<std::size_t, std::size_t>> directory; // by number: the effect and its emitter
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::size_t e = d + 16 + 32 * std::size_t(i);
        const auto it = index_of.find(u32(e));
        if (it != index_of.end())
            directory[int(u32(e + 8))] = {it->second, u32(e + 4)};
    }
    std::map<int, Element> out;
    const std::uint32_t records = u32(8);
    for (std::uint32_t i = 0; i < records && 0x10 + 0x50 * std::size_t(i + 1) <= d; ++i) {
        const std::size_t r = 0x10 + 0x50 * std::size_t(i);
        const auto it = directory.find(int(u32(r) & 0xffffu));
        if (it == directory.end())
            continue;
        Element el;
        el.effect = it->second.first;
        el.emitter = it->second.second;
        el.bone = int(u32(r + 4) >> 16);
        el.flags = u32(r + 0x10);
        el.start = f32(r + 0x14);
        el.sound = int(std::int32_t(u32(r + 0x18)));
        for (int k = 0; k < 3; ++k) {
            el.place[std::size_t(k)] = f32(r + 0x1c + 4 * std::size_t(k));
            el.turn[std::size_t(k)] = f32(r + 0x28 + 4 * std::size_t(k));
            el.size[std::size_t(k)] = f32(r + 0x34 + 4 * std::size_t(k));
        }
        out[int(u32(r) >> 16)] = el;
    }
    if (records == 0)
        for (const auto &[number, at] : directory) {
            Element el;
            el.effect = at.first;
            el.emitter = at.second;
            out[number] = el;
        }
    return out;
}

// A motion's trigger 8, a sound: (bank, number, ...) -- KH2's sound id bank x 1000 + number (FUN_1401de150); 0 none
int trigger_sound(const kh2::MotionAttributes::Trigger &t)
{
    if (t.id != 8 || t.params.size() < 2 || std::int16_t(t.params[0]) < 0 || std::int16_t(t.params[1]) < 0)
        return 0;
    return std::int16_t(t.params[0]) * 1000 + std::int16_t(t.params[1]);
}

// The kind's sounds (its motions' triggers 8, its elements'), each the mod's kh2/se/<id>.wav, decoded once
void read_sounds(KindFiles &k, const fs::path &folder)
{
    std::set<int> ids;
    for (std::size_t slot = 0; slot < k.set->slots(); ++slot)
        if (const kh2::MotionAttributes *attr = k.set->attributes(slot))
            for (const auto &t : attr->triggers)
                if (const int id = trigger_sound(t); id > 0)
                    ids.insert(id);
    for (const auto &[number, el] : k.fx_elements)
        if (el.sound > 0)
            ids.insert(el.sound);
    for (const int id : ids) {
        const std::u8string p8 = (folder / "kh2" / "se" / (std::to_string(id) + ".wav")).u8string();
        const int file = khdays_sound_shot_file(std::string(p8.begin(), p8.end()).c_str());
        if (file >= 0)
            k.sounds[id] = file;
    }
    khdays_log("kh2 enemies: %s's sounds: %zu of %zu (kh2/se/<id>.wav)", k.name.c_str(), k.sounds.size(), ids.size());
}

// The BAR entry of `type` in `bar` (its bytes), none: empty
std::vector<std::uint8_t> bar_entry(const std::vector<std::uint8_t> &bar, std::uint16_t type)
{
    const auto u32 = [&](std::size_t o) {
        return std::uint32_t(bar[o] | (bar[o + 1] << 8) | (bar[o + 2] << 16) | (std::uint32_t(bar[o + 3]) << 24));
    };
    if (bar.size() < 16 || std::memcmp(bar.data(), "BAR", 3) != 0)
        return {};
    for (std::uint32_t i = 0; i < u32(4) && 16 + 16 * (i + 1) <= bar.size(); ++i) {
        const std::size_t e = 16 + 16 * std::size_t(i);
        const std::uint32_t at = u32(e + 8), size = u32(e + 12);
        if (std::uint16_t(bar[e] | (bar[e + 1] << 8)) == type && std::size_t(at) + size <= bar.size())
            return {bar.begin() + at, bar.begin() + at + size};
    }
    return {};
}

void read_effects(KindFiles &k, const fs::path &folder)
{
    const auto pax = bar_entry(read_file(folder / "kh2" / "obj" / (k.name + ".a.us")), 0x12);
    if (pax.empty()) {
        khdays_log("kh2 enemies: %s has no effects (kh2/obj/%s.a.us)", k.name.c_str(), k.name.c_str());
        return;
    }
    try {
        for (auto &b : kh2::read_effect(pax.data(), pax.size()))
            k.fx.push_back(std::make_shared<const kh2::EffectBlock>(std::move(b)));
        k.fx_elements = pax_elements(pax);
        const auto common = read_file(folder / "kh2" / "effect" / "texcommon.dpd");
        if (!common.empty())
            k.common = kh2::read_effect_block(common.data(), common.size(), 0);
        k.fx_pages = std::make_unique<khdays::kh2_fx::Pages>("kh2/obj/" + k.name + ".a.us");
        std::vector<const kh2::EffectBlock *> blocks;
        for (const auto &b : k.fx)
            blocks.push_back(b.get());
        k.fx_pages->add(blocks, "kh2/fx/" + k.name);
        k.fx_pages->add({&k.common}, "kh2/fx/texcommon");
        k.fx_pages->tracing = tracing();
        khdays_log("kh2 enemies: %s's effects read (%zu effects, %zu elements%s)", k.name.c_str(), k.fx.size(),
                   k.fx_elements.size(), common.empty() ? "; no texcommon.dpd" : "");
    } catch (const std::exception &e) {
        khdays_log("kh2 enemies: %s's effects cannot be played (%s)", k.name.c_str(), e.what());
        k.fx.clear();
        k.fx_elements.clear();
    }
}

// A motion set as an actor's Motions: motion N is slot N (an enemy's mset maps them in order: OpenKH's MSET page)
class SetMotions final : public Motions {
public:
    explicit SetMotions(std::shared_ptr<const kh2::MotionSet> set) : set_(std::move(set)) {}
    bool has(int id) const override
    {
        return id >= 0 && set_->motion(std::size_t(id)) != nullptr;
    }
    float length(int id) const override
    {
        return has(id) ? set_->motion(std::size_t(id))->length_ticks() : 0.0F;
    }
    float loop_from(int id) const override
    {
        if (!has(id))
            return 0.0F;
        const kh2::Motion *m = set_->motion(std::size_t(id));
        return m->loop_from() < m->end() ? m->loop_ticks() : m->length_ticks();
    }
    Vec3 root(int id, float ticks) const override
    {
        if (!has(id))
            return {};
        const kh2::Mat4 r = set_->motion(std::size_t(id))->root_motion(ticks);
        return {r[12], r[13], r[14]};
    }

private:
    std::shared_ptr<const kh2::MotionSet> set_;
};

struct Mod final {
    bool looked = false;
    std::string folder; // empty: none
    std::vector<Kind> kinds;
    ai::Calls calls;
    ai::Memory memory; // every KH2 enemy's and player's in one address space, as KH2's are
    std::map<std::string, std::unique_ptr<KindFiles>> files; // by name; null: it cannot be read
};

void trace_calls(ai::Calls &calls);

Mod &mod()
{
    static Mod m;
    if (m.looked)
        return m;
    m.looked = true;
    char path[1024];
    if (!khdays_mods_find("kh2_enemies.txt", path, int(sizeof(path)))) {
        khdays_log("kh2 enemies: no mod holds kh2_enemies.txt (the installer's \"Days - KH2 Enemies\"): the missions' "
                   "KH2 enemies play their stand-ins");
        return m;
    }
    const fs::path folder = u8(path).parent_path();
    const std::u8string f8 = folder.u8string();
    m.folder.assign(f8.begin(), f8.end());
    m.kinds = load_kinds(m.folder, [](const std::string &why) { khdays_log("%s", why.c_str()); });
    const std::vector<std::uint8_t> shapes = read_file(folder / "kh2" / "calls.txt");
    const int read = khdays::kh2_enemies::read_shapes(m.calls, std::string(shapes.begin(), shapes.end()));
    if (read == 0) {
        khdays_log("kh2 enemies: %s has no kh2/calls.txt (the calls' shapes the mod tool reads from the player's "
                   "KINGDOM HEARTS II): the missions' KH2 enemies play their stand-ins",
                   m.folder.c_str());
        m.folder.clear();
        return m;
    }
    add_all(m.calls);
    if (trace_level() >= 3)
        trace_calls(m.calls);
    m.memory.report = [](const std::string &s) {
        if (tracing())
            khdays_log("kh2 enemies: %s", s.c_str());
    };
    khdays_log("kh2 enemies: %s, %zu kinds, %d calls' shapes", m.folder.c_str(), m.kinds.size(), read);
    return m;
}

KindFiles *files_of(const std::string &name)
{
    Mod &m = mod();
    auto it = m.files.find(name);
    if (it != m.files.end())
        return it->second.get();
    std::unique_ptr<KindFiles> k;
    const Kind *kind = find_kind(m.kinds, name);
    if (m.folder.empty() || kind == nullptr) {
        khdays_log("kh2 enemies: %s is not in the mod's list: its stand-in plays", name.c_str());
    } else if (!kind->covered) {
        khdays_log("kh2 enemies: %s's script makes calls the port does not answer yet (%s): its stand-in plays",
                   name.c_str(), kind->missing.c_str());
    } else {
        try {
            const fs::path obj = u8(m.folder) / "kh2" / "obj";
            const auto mdlx = read_file(obj / (kind->name + ".mdlx"));
            const auto mset = read_file(obj / (kind->name + ".mset"));
            const auto scripts = ai::scripts_in(mdlx.data(), mdlx.size());
            if (scripts.empty())
                throw std::runtime_error("its .mdlx holds no script");
            k = std::make_unique<KindFiles>();
            k->name = kind->name;
            const std::u8string p8 = (obj / (kind->name + ".mdlx")).u8string();
            k->mdlx.assign(p8.begin(), p8.end());
            k->script = std::make_shared<const ai::Script>(scripts.front().second);
            k->set = std::make_shared<const kh2::MotionSet>(mset.data(), mset.size());
            k->skeleton =
                std::make_shared<const kh2::MotionSkeleton>(kh2::read_motion_skeleton(mdlx.data(), mdlx.size()));
            k->shapes = read_shapes(mdlx);
            read_effects(*k, u8(m.folder));
            read_sounds(*k, u8(m.folder));
            khdays_log("kh2 enemies: %s read (%zu motions' slots, %zu bones, %zu shapes)", name.c_str(),
                       k->set->slots(), k->skeleton->joints.size(), k->shapes.size());
        } catch (const std::exception &e) {
            khdays_log("kh2 enemies: %s cannot be read (%s): its stand-in plays", name.c_str(), e.what());
            k.reset();
        }
    }
    return m.files.emplace(name, std::move(k)).first->second.get();
}

// ------------------------------------------------------------------------------------------------- the enemies

struct Enemy final {
    unsigned days = 0; // the Days enemy (a DS address)
    KindFiles *kind = nullptr;
    std::unique_ptr<SetMotions> motions;
    std::unique_ptr<Actor> actor;
    std::unique_ptr<kh2::MotionPlayer> player, from_player;
    std::vector<kh2::Mat4> pose; // this frame's, its bones' model-space matrices (KH2's cm)
    bool reacting = false;       // a Days hit's reaction playing (held: + 0x18c bit 9)
    bool paused = false;         // its script waits while it plays (a kind with no reaction state of its own)
    bool dying = false, released = false;
    bool down = false; // released and no longer standing: made again if its Days enemy is set going once more
    unsigned end_ticks = 0;
    float fall = 0.0F;      // its fall's speed off the ground (KH2's units a tick)
    // the ground under it, KH2's y (down), from the last frame Days found one: a jump or a fall stops there (Days's
    // collision lets a fast one through: the Wight Knight's 5 m leap fell on under the room)
    float floor = 0.0F;
    bool floor_known = false;
    float radius = 0.0F;    // its Days enemy's collision sphere's, last read (Days's units)
    int attack_motion = -1; // the motion its attacks' hits below were made in, and its time then
    float attack_time = 0.0F;
    std::map<std::size_t, std::set<unsigned>> struck; // by attack range: the players it hit (each once)
    std::set<std::size_t> windows_told;                // traced: the attack ranges of this motion told
    int slot = 0;                                     // its stand-in's attack slot its hits use
    bool told_draw = false;
    // its effects running (started by its motions' trigger 1 and its script's 1:87), moved on as it is drawn (KH2's
    // 60 a second): each its element, its ticks run, whether its sound has started
    struct Running final {
        std::unique_ptr<kh2::EffectInstance> fx;
        std::vector<kh2::EffectDraw> draws;
        const Element *element = nullptr;
        float age = 0.0F;
        bool sounded = false;
    };
    std::vector<Running> fx;
    float pan = 0.0F; // where it was last drawn, left (-1) to right (1) of the camera: its sounds' pan
    unsigned fx_frame = 0;
    std::uint32_t fx_seed = 1;
    int cue_motion = -1; // the motion its triggers were last looked at in, and its time then
    float cue_time = 0.0F;
    // where it was last drawn (Days's place) and the projection then, for its end once its Days enemy is gone
    float place[3] = {};
    float home[3] = {}; // where its wave placed it (Days's place): it comes back there after KH2's "leave"
    bool placed = false; // its place given to Days (e.place) at least once
    float pure[16] = {};
    bool seen = false;
    unsigned ghost_frame = 0;
    unsigned drawn_frame = ~0u; // the frame its KH2 model was last drawn in: once a frame, whatever matched it
    unsigned ticks_traced = 0;  // KHDAYS_TRACE_KH2_ENEMIES=2: its ticks, its threads told once a second
    std::string state;          // the state it last entered (traced)
};

std::map<unsigned, std::string> kind_of;            // a Days enemy made from a KH2 row -> its KH2 kind
std::map<unsigned, int> made_row;                   // every Days enemy made in this area -> its row
std::map<int, std::string> row_kinds;               // the rows' KH2 enemies ("" none), asked once a row
bool kh2_rows = false;                              // whether this area made any actor for a KH2 row

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
std::map<unsigned, std::unique_ptr<Enemy>> enemies; // those set going, by their Days actor
std::map<const Actor *, Enemy *> by_actor;

// KHDAYS_TRACE_KH2_ENEMIES=3: every call an enemy makes, but those made every tick, with its arguments and the
// state it is in once the call is over
void trace_calls(ai::Calls &calls)
{
    for (const auto &[kind, number] : calls.handled()) {
        static const std::set<std::pair<unsigned, unsigned>> every_tick = {
            {0, 3}, {0, 4}, {0, 5}, {0, 16}, {0, 17}, {0, 18}, {1, 3}, {1, 4}, {1, 10}, {1, 14}, {1, 15},
            {1, 57}, {1, 60}, {1, 120}, {1, 147}, {1, 201}, {2, 23}, {1, 67}, {1, 68}, {1, 124}};
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

std::map<unsigned, std::unique_ptr<Object>> players; // each of Days's players as KH2's scripts see them
// A Days hit as KH2's scripts read it, trigger 6's argument: + 0x26 its reaction's kind, 2 a small one (2:8 asks
// whether it is past 1, FUN_1403ce980; the Strafer's and the cubes' revenge turn on it)
std::unique_ptr<Object> days_hit;

void cast(Enemy &e, int number);

class Field final : public World {
public:
    // 1:87: the actor's element `number` started (FUN_1403b6670 -> FUN_1402c6b30 on its effects); the handle the
    // script gives back to 0:79 none
    std::uint32_t effect(Actor &a, int number, int, int) override
    {
        const auto it = by_actor.find(&a);
        if (it != by_actor.end())
            cast(*it->second, number);
        return 0;
    }

    // its model drawn by Days in the frame before or this one (whether Days leaves out what its camera does not see
    // is not known: a drawn enemy counts as seen)
    bool on_screen(Actor &a) override
    {
        const auto it = by_actor.find(&a);
        return it != by_actor.end() && it->second->drawn_frame != ~0u &&
               khdays_link_frame() - it->second->drawn_frame <= 1u;
    }

    Object *player(Actor &a) override
    {
        const auto it = by_actor.find(&a);
        if (it == by_actor.end())
            return nullptr;
        std::vector<KhdaysKh2Player> list(
            std::size_t(std::max(0, khdays_kh2_shell_players(it->second->days, nullptr, 0))));
        const int n = khdays_kh2_shell_players(it->second->days, list.data(), int(list.size()));
        const Vec3 at = a.place();
        Object *best = nullptr;
        float best_d = 0.0F;
        for (int i = 0; i < n && i < int(list.size()); ++i) {
            auto &o = players[list[std::size_t(i)].object];
            if (!o) {
                o = std::make_unique<Object>(mod().memory, "a player");
                o->set_bits(field::kFlags6c8, bit::kPlayer, true); // KH2's player (FUN_1403a7a40)
            }
            o->set_place(to_kh2(list[std::size_t(i)].place));
            const Vec3 p = o->place();
            const float d = (p[0] - at[0]) * (p[0] - at[0]) + (p[2] - at[2]) * (p[2] - at[2]);
            if (best == nullptr || d < best_d)
                best = o.get(), best_d = d;
        }
        return best;
    }
};

Field &the_field()
{
    static Field f;
    return f;
}

// Enemies whose end ("dead": its effect, its fading) goes on once its Days enemy is gone -- Days defeats it at once and
// reuses it for the next wave -- moved on and drawn where it was as the 3D list closes, until it has faded out
std::vector<std::unique_ptr<Enemy>> ghosts;
void ghosts_close();

void forget(unsigned days)
{
    auto it = enemies.find(days);
    if (it == enemies.end())
        return;
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

// The kind's script on a new actor at the shell's place, facing the nearest player
Enemy *start(unsigned days, KindFiles &k, const KhdaysKh2Shell &s)
{
    auto e = std::make_unique<Enemy>();
    e->days = days;
    e->kind = &k;
    e->motions = std::make_unique<SetMotions>(k.set);
    e->actor = std::make_unique<Actor>(k.script, mod().memory, mod().calls, k.name);
    e->player = std::make_unique<kh2::MotionPlayer>(*k.skeleton);
    e->from_player = std::make_unique<kh2::MotionPlayer>(*k.skeleton);
    e->slot = khdays_kh2_shell_attack_slot(days);
    Actor &a = *e->actor;
    a.world = &the_field();
    a.motions = e->motions.get();
    a.set_place(to_kh2(s.place));
    std::copy(s.place, s.place + 3, e->home);
    Enemy *raw = e.get();
    by_actor[&a] = raw;
    enemies[days] = std::move(e);
    if (Object *p = the_field().player(a)) {
        const Vec3 to = p->place(), at = a.place();
        if (to[0] != at[0] || to[2] != at[2])
            a.set_yaw(std::atan2(to[0] - at[0], to[2] - at[2]));
    }
    if (tracing())
        a.states().entered = [days, raw](const std::string &name) {
            raw->state = name;
            khdays_log("kh2 enemies: %08x enters %s", days, name.c_str());
            if (name == "leave") { // where it and the player it went for stood (it leaves when far from it)
                const Vec3 at = raw->actor->place();
                Object *p = the_field().player(*raw->actor);
                const Vec3 to = p != nullptr ? p->place() : Vec3{};
                khdays_log("kh2 enemies: %08x leaves at %.0f %.0f %.0f, the player at %.0f %.0f %.0f (KH2's cm)%s", days,
                           double(at[0]), double(at[1]), double(at[2]), double(to[0]), double(to[1]), double(to[2]),
                           the_field().on_screen(*raw->actor) ? ", on the screen" : "");
            }
        };
    a.start();
    if (tracing())
        khdays_log("kh2 enemies: %08x set going as %s, attack slot %d", days, k.name.c_str(), raw->slot);
    return raw;
}

// The pose now: the motion at its time, blended from the one before over its blend
void row_matrix_mul(const float a[16], const float b[16], float out[16]);
void inverse_affine(const float m[16], float out[16]);

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

void pose(Enemy &e)
{
    const Actor &a = *e.actor;
    const kh2::Motion *now = a.motion.id >= 0 ? e.kind->set->motion(std::size_t(a.motion.id)) : nullptr;
    if (now == nullptr) {
        e.pose.clear();
        return;
    }
    e.pose = pose_at(*e.player, *now, a.motion.time);
    const kh2::Motion *from = a.motion.from_id >= 0 ? e.kind->set->motion(std::size_t(a.motion.from_id)) : nullptr;
    if (from == nullptr || a.motion.blend <= 0.0F || a.motion.blend_left <= 0.0F)
        return;
    const auto old = pose_at(*e.from_player, *from, a.motion.from_time);
    const float w = a.motion.blend_left / a.motion.blend; // the old pose's share
    for (std::size_t j = 0; j < e.pose.size() && j < old.size(); ++j)
        for (std::size_t c = 0; c < 16; ++c)
            e.pose[j][c] = e.pose[j][c] * (1.0F - w) + old[j][c] * w;
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
    const Vec3 t = turned({p[0], 0.0F, p[2]}, e.actor->yaw());
    out[0] = s.place[0] + t[0] / kKh2PerDays;
    out[1] = s.place[1] + p[1] / kKh2PerDays;
    out[2] = s.place[2] + t[2] / kKh2PerDays;
}

// The current motion's attacks (its ranges 10 and 33: an atkp entry and a collision group) that cover its time: each
// of the group's attack spheres hits the players it reaches, each player once a range (KH2's hit's own "once")
void attack(Enemy &e, const KhdaysKh2Shell &s)
{
    const Actor &a = *e.actor;
    if (a.motion.id != e.attack_motion || a.motion.time < e.attack_time) {
        e.struck.clear(); // another motion, or this one over again
        e.windows_told.clear();
    }
    e.attack_motion = a.motion.id;
    e.attack_time = a.motion.time;
    const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.kind->set->attributes(std::size_t(a.motion.id)) : nullptr;
    if (attr == nullptr || e.pose.empty() || e.dying)
        return;
    std::vector<KhdaysKh2Player> list(std::size_t(std::max(0, khdays_kh2_shell_players(e.days, nullptr, 0))));
    const int n = khdays_kh2_shell_players(e.days, list.data(), int(list.size()));
    for (std::size_t r = 0; r < attr->ranges.size(); ++r) {
        const auto &range = attr->ranges[r];
        if ((range.id != 10 && range.id != 33) || range.params.size() < 2 || a.motion.time < float(range.start) ||
            a.motion.time >= float(range.end))
            continue;
        const int group = std::int16_t(range.params[1]);
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
                const float d[3] = {p.place[0] - c[0], p.place[1] + p.radius - c[1], p.place[2] - c[2]};
                const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (nearest < 0.0F || dist - radius - p.radius < nearest - reach) {
                    nearest = dist;
                    reach = radius + p.radius;
                    std::copy(c, c + 3, centre);
                    const Vec3 f = facing_of(a.yaw());
                    const float tx = p.place[0] - s.place[0], tz = p.place[2] - s.place[2];
                    const float tl = std::sqrt(tx * tx + tz * tz);
                    toward = tl > 0.0F ? (tx * f[0] + tz * f[2]) / tl : 1.0F;
                }
                if (dist >= radius + p.radius || e.struck[r].count(p.object) != 0)
                    continue;
                e.struck[r].insert(p.object);
                const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
                const float push[3] = {flat > 0.0F ? d[0] / flat : 0.0F, 0.0F, flat > 0.0F ? d[2] / flat : 0.0F};
                const int took = khdays_kh2_shell_hit(e.days, p.object, e.slot, push);
                if (tracing())
                    khdays_log("kh2 enemies: %08x's motion %d at %.1f (atkp %d, group %d) hits %08x%s", e.days,
                               a.motion.id, double(a.motion.time), int(std::int16_t(range.params[0])), group, p.object,
                               took ? "" : " (not taken)");
            }
        }
        if (tracing() && e.windows_told.count(r) == 0) {
            // every attack sphere's place along its facing then (forward, up), what each group reaches
            const Vec3 f = facing_of(a.yaw());
            std::string where;
            for (const Shape &sh : e.kind->shapes) {
                if ((sh.kind & 0xff) != 6)
                    continue;
                float c[3];
                days_point(e, s, sh.bone, sh.at, c);
                char one[64];
                std::snprintf(one, sizeof(one), " [group %d: %.2f ahead, %.2f up]", sh.group,
                              double((c[0] - s.place[0]) * f[0] + (c[2] - s.place[2]) * f[2]), double(c[1] - s.place[1]));
                where += one;
            }
            khdays_log("kh2 enemies: %08x's attack spheres:%s", e.days, where.c_str());
        }
        if (tracing() && e.windows_told.insert(r).second)
            khdays_log("kh2 enemies: %08x's motion %d attacks at %.1f (atkp %d, group %d): %d spheres, nearest player "
                       "%.2f away, reached within %.2f; the sphere at %.2f %.2f %.2f, the body at %.2f %.2f %.2f "
                       "facing the player by %.2f",
                       e.days, a.motion.id, double(a.motion.time), int(std::int16_t(range.params[0])), group, spheres,
                       double(nearest), double(reach), double(centre[0]), double(centre[1]), double(centre[2]),
                       double(s.place[0]), double(s.place[1]), double(s.place[2]), double(toward));
    }
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

// Element `number` of the kind's effects started on the enemy
void cast(Enemy &e, int number)
{
    const KindFiles &k = *e.kind;
    const auto it = k.fx_elements.find(number);
    if (it == k.fx_elements.end() || it->second.effect >= k.fx.size())
        return;
    try {
        Enemy::Running r;
        r.fx = std::make_unique<kh2::EffectInstance>(k.fx[it->second.effect], it->second.emitter, e.fx_seed++);
        r.element = &it->second;
        e.fx.push_back(std::move(r));
        if (tracing())
            khdays_log("kh2 enemies: %08x casts element %d (effect %zu, emitter %zu, bone %d, sound %d)", e.days,
                       number, it->second.effect, it->second.emitter, it->second.bone, it->second.sound);
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: %s's effect %d cannot be played (%s)", k.name.c_str(), number, x.what());
    }
}

// The current motion's single-frame triggers its time passed this tick (KH2's ANB triggers): 1 an effect, 8 a sound
void cues(Enemy &e)
{
    const Actor &a = *e.actor;
    const float from = a.motion.id == e.cue_motion && a.motion.time >= e.cue_time ? e.cue_time : -1.0F;
    e.cue_motion = a.motion.id;
    e.cue_time = a.motion.time;
    const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.kind->set->attributes(std::size_t(a.motion.id)) : nullptr;
    if (attr == nullptr)
        return;
    for (const auto &t : attr->triggers) {
        if (float(t.frame) <= from || float(t.frame) > a.motion.time)
            continue;
        if (t.id == 1 && !t.params.empty())
            cast(e, std::int16_t(t.params[0]));
        else if (const int id = trigger_sound(t); id > 0)
            sound(e, id);
    }
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

void one_tick(Enemy &e, const KhdaysKh2Shell &s, Vec3 &place)
{
    Actor &a = *e.actor;
    if (trace_level() >= 2 && ++e.ticks_traced % 60 == 0 && a.machine().main_thread() != nullptr) {
        std::string at;
        threads_of(*a.machine().main_thread(), at);
        khdays_log("kh2 enemies: %08x's threads at%s; motion %d at %.1f%s%s; %.3f above the ground, %.1f its fall", e.days,
                   at.c_str(), a.motion.id, double(a.motion.time), a.jump.on ? ", in a jump" : "",
                   e.reacting ? ", reacting" : "", double(s.above), double(a.jump.on ? a.jump.up : e.fall));
    }
    if (!e.paused)
        a.tick();
    Vec3 travel = a.step(1.0F);
    cues(e);
    if (a.jump.on) {
        if (a.jump.up > 0.0F && e.floor_known && place[1] + travel[1] >= e.floor) {
            travel[1] = e.floor - place[1]; // on it, not through it
            a.land();
        }
    } else if (e.floor_known && e.floor - place[1] > 0.01F * kKh2PerDays) {
        e.fall += kGravity;
        travel[1] += std::min(e.fall, e.floor - place[1]);
        a.set_bits(field::kFlags, bit::kInAir, true);
    } else if (e.fall > 0.0F) {
        e.fall = 0.0F;
        a.set_bits(field::kFlags, bit::kInAir, false);
    }
    for (int i = 0; i < 3; ++i)
        place[i] += travel[i];
    a.set_place(place);
    if (e.reacting && a.motion.ended) {
        e.reacting = false;
        a.set_bits(field::kStatus, bit::kHeld, false);
        if (e.paused)
            a.play(0, 8.0F, 0.0F); // its script picks its motion again otherwise
        e.paused = false;
    }
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

void row_matrix_mul(const float a[16], const float b[16], float out[16])
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float v = 0.0F;
            for (int k = 0; k < 4; ++k)
                v += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = v;
        }
}

bool near_identity(const float m[16])
{
    for (int i = 0; i < 16; ++i)
        if (std::fabs(m[i] - (i % 5 == 0 ? 1.0F : 0.0F)) > 1e-4F)
            return false;
    return true;
}

// The inverse of a 3x3 and a translation (the last row; row vectors); the identity for a singular one
void inverse_affine(const float m[16], float out[16])
{
    const auto at = [&](int r, int c) { return m[r * 4 + c]; };
    const float det = at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
                      at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
                      at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
    std::fill(out, out + 16, 0.0F);
    out[15] = 1.0F;
    if (std::fabs(det) < 1e-12F) {
        out[0] = out[5] = out[10] = 1.0F;
        return;
    }
    const float d = 1.0F / det;
    out[0] = (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) * d;
    out[1] = (at(0, 2) * at(2, 1) - at(0, 1) * at(2, 2)) * d;
    out[2] = (at(0, 1) * at(1, 2) - at(0, 2) * at(1, 1)) * d;
    out[4] = (at(1, 2) * at(2, 0) - at(1, 0) * at(2, 2)) * d;
    out[5] = (at(0, 0) * at(2, 2) - at(0, 2) * at(2, 0)) * d;
    out[6] = (at(0, 2) * at(1, 0) - at(0, 0) * at(1, 2)) * d;
    out[8] = (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0)) * d;
    out[9] = (at(0, 1) * at(2, 0) - at(0, 0) * at(2, 1)) * d;
    out[10] = (at(0, 0) * at(1, 1) - at(0, 1) * at(1, 0)) * d;
    for (int c = 0; c < 3; ++c)
        out[12 + c] = -(at(3, 0) * out[c] + at(3, 1) * out[4 + c] + at(3, 2) * out[8 + c]);
}

// KH2's camera for an enemy's effects, its model drawn at `view` (its model to the view, Days's units) and
// `projection`: the effects run in KH2's axes (y down) and centimetres, at the enemy (their world the identity); their
// view the enemy's, its axes KH2's and shrunk by `units` (Days's units a KH2 unit, the camera's scale with it) so
// that their sprites keep KH2's size; their projection Days's, back in Days's axes and units. What they draw comes out
// of the clip space divided by `units` (kh2_fx::submit multiplies it back) -- as KH2's Dark Corridor's
// (runtime/mods/kh2_corridor_fit.h corridor_camera).
kh2::EffectCamera effect_camera(const float view_in[16], const float projection[16], float &units)
{
    static constexpr float model[3] = {1.0F, -1.0F, 1.0F}; // the effects' axes to the enemy's own (KH2's y down)
    static constexpr float turn[3] = {1.0F, -1.0F, -1.0F}; // Days's view's to KH2's
    float view[16];
    std::copy(view_in, view_in + 16, view);
    float length = 0.0F;
    for (int r = 0; r < 3; ++r)
        length += std::sqrt(view[r * 4] * view[r * 4] + view[r * 4 + 1] * view[r * 4 + 1] +
                            view[r * 4 + 2] * view[r * 4 + 2]) /
                  3.0F;
    if (!(length > 1e-6F) || !std::isfinite(length))
        length = 1.0F;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            view[r * 4 + k] /= length;
    units = length / kKh2PerDays;
    kh2::EffectCamera cam;
    cam.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    cam.view = {};
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            cam.view[std::size_t(r * 4 + k)] = model[r] * view[r * 4 + k] * turn[k];
    for (int k = 0; k < 3; ++k)
        cam.view[std::size_t(12 + k)] = view[12 + k] / units * turn[k];
    cam.view[15] = 1.0F;
    for (int k = 0; k < 4; ++k) {
        for (int r = 0; r < 3; ++r)
            cam.projection[std::size_t(r * 4 + k)] = turn[r] * projection[r * 4 + k];
        cam.projection[std::size_t(12 + k)] = projection[12 + k] / units;
    }
    return cam;
}

// An element's place in its enemy's effects' space (KH2's axes, y down, cm; the enemy's root at 0, turned as it is):
// its size, its turn (x, then y, then z), its place, then the bone it follows (the pose's, KH2's model space y up)
kh2::EffectMatrix element_world(const Enemy &e, const Element &el)
{
    const auto turn = [](int axis, float a) {
        const float c = std::cos(a), n = std::sin(a);
        std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const int i = (axis + 1) % 3, j = (axis + 2) % 3;
        m[std::size_t(i * 4 + i)] = c;
        m[std::size_t(i * 4 + j)] = n;
        m[std::size_t(j * 4 + i)] = -n;
        m[std::size_t(j * 4 + j)] = c;
        return m;
    };
    std::array<float, 16> m{el.size[0], 0, 0, 0, 0, el.size[1], 0, 0, 0, 0, el.size[2], 0, 0, 0, 0, 1};
    for (int axis = 0; axis < 3; ++axis) {
        if (el.turn[std::size_t(axis)] == 0.0F)
            continue;
        std::array<float, 16> t;
        row_matrix_mul(m.data(), turn(axis, el.turn[std::size_t(axis)]).data(), t.data());
        m = t;
    }
    for (int k = 0; k < 3; ++k)
        m[std::size_t(12 + k)] += el.place[std::size_t(k)];
    if (el.bone >= 0 && el.bone < kNoBone && std::size_t(el.bone) < e.pose.size()) {
        std::array<float, 16> bone;
        std::copy(e.pose[std::size_t(el.bone)].begin(), e.pose[std::size_t(el.bone)].end(), bone.begin());
        static constexpr float flip[3] = {1.0F, -1.0F, 1.0F}; // y down <-> y up
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 3; ++c)
                bone[std::size_t(r * 4 + c)] *= (r < 3 ? flip[r] : 1.0F) * flip[c];
        std::array<float, 16> t;
        row_matrix_mul(m.data(), bone.data(), t.data());
        m = t;
    }
    kh2::EffectMatrix out;
    std::copy(m.begin(), m.end(), out.begin());
    return out;
}

// Its effects moved on by KH2's ticks since its last draw (the game's frames, 60 a second: the corridor's way), each
// at its element's place, its sound started when its time comes; the last tick's drawn at the camera now; those over
// let go. Where it is drawn gives its sounds' pan.
void draw_effects(Enemy &e, const float view[16], const float projection[16])
{
    const float across = std::sqrt(view[12] * view[12] + view[14] * view[14]);
    e.pan = across > 1e-6F && std::isfinite(across) ? std::clamp(view[12] / across, -1.0F, 1.0F) : 0.0F;
    if (e.fx.empty() || !e.kind->fx_pages)
        return;
    const unsigned now = khdays_link_frame();
    const unsigned per_frame = unsigned(std::max(1L, std::lround(khdays_link_frame_seconds() * 60.0)));
    const unsigned ticks = e.fx_frame == 0 ? per_frame : now >= e.fx_frame ? (now - e.fx_frame) * per_frame : per_frame;
    e.fx_frame = now;
    float units = 1.0F;
    const kh2::EffectCamera cam = effect_camera(view, projection, units);
    kh2::EffectCamera placed = cam;
    for (Enemy::Running &r : e.fx) {
        placed.world = element_world(e, *r.element);
        for (unsigned t = 0; t < ticks; ++t) {
            const bool last = t + 1 == ticks;
            if (last)
                r.draws.clear();
            if (!r.sounded && r.age >= r.element->start) {
                r.sounded = true;
                if (r.element->sound > 0)
                    sound(e, r.element->sound);
            }
            r.fx->tick(placed, last ? &r.draws : nullptr);
            r.age += 1.0F;
        }
        khdays::kh2_fx::submit(r.draws, units, *e.kind->fx_pages);
    }
    for (std::size_t i = e.fx.size(); i-- > 0;)
        if (e.fx[i].fx->done())
            e.fx.erase(e.fx.begin() + std::ptrdiff_t(i));
}

// How the model whose draw begins is placed, from the matrices the game left (khdays_g3d.h): three ways are seen --
// the position matrix the model's place in the world, the camera folded into the projection (Days's enemies, Ms/:
// traced); after FlushP the position the identity, the place (the flush's base) and the camera folded into the
// projection; after Flush the position the camera times the place.
enum class Way { None, World, Folded, Camera };

struct DrawSpace final {
    float position[16], projection[16], base[16], camera[16];
    bool has_camera = false;
    int flush = 0; // khdays_g3d_base's
};

DrawSpace draw_space()
{
    DrawSpace d;
    float vector[16];
    khdays_g3d_matrices(d.position, vector, d.projection);
    d.flush = khdays_g3d_base(d.base);
    d.has_camera = khdays_g3d_camera(d.camera) != 0;
    return d;
}

// How far apart two places are (the sum of their coordinates' differences)
float apart(const float a[3], const float b[3])
{
    return std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]);
}

// An enemy class's own model: in its file Ms/<class>.p (Ms/04.p the Soldier's, Ms/00.p the Shadow's), not the enemies'
// shared effects (Ms/SharedEffect.p2f): its class, -1 for another model
int enemy_model(const void *resource)
{
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    if (resource == nullptr || !khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run) ||
        std::strncmp(file, "Ms/", 3) != 0)
        return -1;
    const char *n = file + 3;
    int kind = 0;
    while (*n >= '0' && *n <= '9')
        kind = kind * 10 + (*n++ - '0');
    // the archive's name, then where in it the model lies ("Ms/04.p@200")
    return n != file + 3 && std::strncmp(n, ".p", 2) == 0 && (n[2] == 0 || n[2] == '@') ? kind : -1;
}

// How far the draw stands from `place` in the world, and the way it is placed: an enemy's model in the world way, its
// own model's place a few centimetres from its actor's (the Shadow's 6); the other ways exactly
float off_by(const DrawSpace &d, const float place[3], Way &way)
{
    way = Way::World;
    float best = apart(d.position + 12, place);
    if (d.flush == 2 && near_identity(d.position) && apart(d.base + 12, place) < best) {
        way = Way::Folded;
        best = apart(d.base + 12, place);
    }
    if (d.has_camera) {
        float inverse[16], world[16];
        inverse_affine(d.camera, inverse);
        row_matrix_mul(d.position, inverse, world);
        if (apart(world + 12, place) < best) {
            way = Way::Camera;
            best = apart(world + 12, place);
        }
    }
    return best;
}
constexpr float kNearEnough = 0.25F; // Days's units

// For a model drawn instead at `mine` (its model to the world): the position matrix it takes (`place`), and its
// model to the view and the projection alone (for its effects, whose sprites face the view)
void spaces_for(const DrawSpace &d, Way way, const float mine[16], float place[16], float view[16], float pure[16])
{
    row_matrix_mul(mine, d.camera, view);
    float inverse[16];
    switch (way) {
    case Way::World:
        std::copy(mine, mine + 16, place);
        inverse_affine(d.camera, inverse);
        row_matrix_mul(inverse, d.projection, pure);
        break;
    case Way::Folded: {
        float base_camera[16];
        inverse_affine(d.base, inverse);
        row_matrix_mul(mine, inverse, place);
        row_matrix_mul(d.base, d.camera, base_camera);
        inverse_affine(base_camera, inverse);
        row_matrix_mul(inverse, d.projection, pure);
        break;
    }
    default:
        std::copy(view, view + 16, place);
        std::copy(d.projection, d.projection + 16, pure);
        break;
    }
}

// The shells' own draws, by render object: a Days enemy's model has no link of its own to its actor that the port
// reads (its node's + 0x24, a battle actor's, is not one: traced), so a draw is its shell's when it is its class's own
// model drawn near the shell's place (kNearEnough; the nearest shell of that class); the room's models and the
// enemies' shared effects are other files. Kept while it still stands there.
std::map<unsigned, unsigned> shell_draws;

void ghosts_close()
{
    float camera[16];
    if (ghosts.empty() || !khdays_g3d_camera(camera))
        return;
    const unsigned now = khdays_link_frame();
    const unsigned per_frame = unsigned(std::max(1L, std::lround(khdays_link_frame_seconds() * 60.0)));
    for (auto &g : ghosts) {
        Enemy &e = *g;
        const unsigned ticks = e.ghost_frame == 0     ? per_frame
                               : now >= e.ghost_frame ? (now - e.ghost_frame) * per_frame
                                                      : per_frame;
        e.ghost_frame = now;
        for (unsigned t = 0; t < ticks && !e.released; ++t) {
            Actor &a = *e.actor;
            a.tick();
            Vec3 at = to_kh2(e.place);
            const Vec3 travel = a.step(1.0F);
            for (int i = 0; i < 3; ++i)
                at[i] += travel[i];
            to_days(at, e.place);
            pose(e);
            ++e.end_ticks;
            if (((a.u32(field::kFade) & bit::kFadeHeld) != 0 && a.fade <= 0.0F) || e.end_ticks >= kLongestEnd)
                e.released = true;
        }
        const float c = std::cos(e.actor->yaw()), sn = std::sin(e.actor->yaw());
        const float mine[16] = {c, 0, -sn, 0, 0, 1, 0, 0, sn, 0, c, 0, e.place[0], e.place[1], e.place[2], 1};
        float view[16];
        row_matrix_mul(mine, camera, view);
        draw_effects(e, view, e.pure);
        const unsigned alpha = unsigned(std::lround(std::clamp(e.actor->fade, 0.0F, 1.0F) * 31.0F));
        if (alpha == 0 || e.pose.empty())
            continue;
        std::vector<float> bones(e.pose.size() * 16);
        for (std::size_t b = 0; b < e.pose.size(); ++b)
            std::copy(e.pose[b].begin(), e.pose[b].end(), bones.begin() + std::ptrdiff_t(16 * b));
        khdays_models_draw_posed(e.kind->mdlx.c_str(), view, e.pure, (alpha << 16) | (1u << 24), bones.data(),
                                 e.pose.size());
    }
    for (auto it = ghosts.begin(); it != ghosts.end();) {
        if ((*it)->released && (*it)->fx.empty()) {
            if (tracing())
                khdays_log("kh2 enemies: %08x's end over", (*it)->days);
            by_actor.erase((*it)->actor.get());
            it = ghosts.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

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
    forget(unsigned(KHDAYS_REF(actor)));
}

extern "C" void khdays_kh2_enemies_area_left(void)
{
    khdays_sound_shots_stop();
    made_row.clear();
    row_kinds.clear();
    kh2_rows = false;
    enemies.clear();
    ghosts.clear();
    by_actor.clear();
    kind_of.clear();
    players.clear();
    days_hit.reset();
}

extern "C" int khdays_kh2_enemies_held(void *actor)
{
    const Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    return e != nullptr && !e->released;
}

extern "C" void khdays_kh2_enemies_tick(void *actor, int delta)
{
    const unsigned days = unsigned(KHDAYS_REF(actor));
    const auto kind = kind_of.find(days);
    if (kind == kind_of.end())
        return;
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
        KindFiles *k = files_of(kind->second);
        if (k == nullptr) {
            kind_of.erase(kind); // its stand-in plays, for good
            return;
        }
        e = start(days, *k, s);
    }
    if (e->released || khdays_stop_held(actor))
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
    if (e->placed) {
        const float dx = s.place[0] - e->place[0], dz = s.place[2] - e->place[2];
        if (dx * dx + dz * dz >= 0.01F * 0.01F && e->actor->steer_speed > 0.0F) {
            e->actor->steer_speed = 0.0F;
            if (trace_level() >= 2)
                khdays_log("kh2 enemies: %08x pushed %.2f off its place: its steering stops", days,
                           double(std::sqrt(dx * dx + dz * dz)));
        }
    }
    Vec3 place = to_kh2(s.place);
    if (s.above != -1.0F) { // (-1: no ground under it; below 0: sunk into it)
        e->floor = place[1] + s.above * kKh2PerDays;
        e->floor_known = true;
    }
    for (int t = 0; t < ticks; ++t) {
        one_tick(*e, s, place);
        pose(*e);
        attack(*e, s);
    }
    // KH2's "leave" (the enemy far from the player: its scripts' 1:56 distances): it fades out and waits for KH2's
    // spawner to bring it back. Days's field has none, so once it has faded out it comes back where its wave placed
    // it, its script anew, its Days HP as it is.
    if (const StateEntry *now = e->actor->states().current();
        now != nullptr && now->name == "leave" && !e->dying && e->actor->fade <= 0.0F &&
        (e->actor->u32(field::kFade) & bit::kFadeHeld) != 0) {
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
        forget(days);
        e = start(days, *k, back);
        place = to_kh2(back.place);
        if (tracing())
            khdays_log("kh2 enemies: %08x comes back at %.2f %.2f %.2f", days, double(back.place[0]),
                       double(back.place[1]), double(back.place[2]));
    }
    to_days(place, e->place);
    e->placed = true;
    e->radius = s.radius;
    khdays_kh2_shell_place(days, e->place, e->actor->yaw());
}

extern "C" void khdays_kh2_enemies_hit(void *actor, int hp_before)
{
    const unsigned days = unsigned(KHDAYS_REF(actor));
    Enemy *e = find(days);
    KhdaysKh2Shell s;
    if (e == nullptr || e->dying || !khdays_kh2_shell_read(days, &s) || s.hp >= hp_before)
        return;
    Actor &a = *e->actor;
    if (s.hp <= 0) { // its end: the script's "dead" (its effect, its fade); Days's defeat once it is over
        e->dying = true;
        e->reacting = false;
        e->paused = false;
        a.set_bits(field::kStatus, bit::kHeld, false);
        a.states().unlock();
        if (!a.states().enter(a, "dead"))
            e->released = true;
        if (tracing())
            khdays_log("kh2 enemies: %08x defeated%s", days, e->released ? " (no \"dead\" state)" : "");
        return;
    }
    if (Object *p = the_field().player(a))
        a.set_u32(field::kAttacker, p->address()); // the last to hit it (2:88): the nearest player
    const StateEntry *now = a.states().current();
    if (now != nullptr && (now->flags & 2) != 0) {
        if (tracing())
            khdays_log("kh2 enemies: %08x hit (%d to %d) in a locked state", days, hp_before, s.hp);
        return; // a locked state (its leaving, an event) plays on
    }
    // the reaction: a small one, from the front or the back as the nearest player stands (KH2's motions 10 and 12,
    // DAMAGE_S_FRONT_HIGH and DAMAGE_S_BACK_HIGH)
    bool front = true;
    if (Object *p = the_field().player(a)) {
        const Vec3 to = p->place(), at = a.place(), f = facing_of(a.yaw());
        front = (to[0] - at[0]) * f[0] + (to[2] - at[2]) * f[2] >= 0.0F;
    }
    const int motion = front ? 10 : 12;
    if (!a.motions->has(motion)) {
        if (tracing())
            khdays_log("kh2 enemies: %08x hit (%d to %d), no motion %d", days, hp_before, s.hp, motion);
        return;
    }
    // KH2's hit (FUN_140410700): the reaction applied -- its motion, the actor held (FUN_1403d4650) -- the queue
    // cleared and the reaction state the attack names entered by name: a small hit's "revenge" (every enemy script
    // has it: its reaction, then its revenge mode's attacks). A kind without one waits on the motion, its script paused.
    a.states().clear(a);
    a.jump = {};
    a.play(motion, 2.0F, 0.0F);
    a.set_bits(field::kStatus, bit::kHeld, true);
    e->reacting = true;
    StateEntry reaction;
    e->paused = !a.states().find(a, "revenge", reaction) || !a.states().enter(a, "revenge");
    // then trigger 6, the hit told to the script (FUN_140410d60, the hit its argument: a small one): KH2's
    // enemies turn to their revenge mode on it when the player struck them (1:309 of 2:88's), which begins once the
    // reaction is over (the state waits while the actor is held)
    if (!days_hit) {
        days_hit = std::make_unique<Object>(mod().memory, "a Days hit");
        days_hit->set_u32(0x24, 2u << 16); // + 0x26: a small reaction
    }
    const ai::Value packet = ai::Value::of_address(days_hit->address());
    a.event(6, {&packet, 1});
    if (tracing())
        khdays_log("kh2 enemies: %08x hit (%d to %d), motion %d%s", days, hp_before, s.hp, motion,
                   e->paused ? ", its script waiting" : "");
}

extern "C" int khdays_kh2_enemies_draw_begin(const void *resource, const void *render_obj)
{
    if (render_obj == nullptr || enemies.empty())
        return 0;
    const unsigned obj = unsigned(KHDAYS_REF(render_obj));
    const DrawSpace d = draw_space();
    if (!d.has_camera)
        return 0;
    Enemy *e = nullptr;
    KhdaysKh2Shell s;
    Way way = Way::None;
    const int model_kind = enemy_model(resource);
    const bool own_model = model_kind >= 0;
    const float near_enough = own_model ? kNearEnough : 1e-3F;
    if (const auto it = shell_draws.find(obj); it != shell_draws.end()) {
        Enemy *x = find(it->second);
        if (x != nullptr && khdays_kh2_shell_read(it->second, &s) && (!own_model || s.kind == model_kind) &&
            off_by(d, s.place, way) < near_enough)
            e = x;
        else
            shell_draws.erase(it);
    }
    if (e == nullptr) { // the nearest shell the draw stands at
        float best = near_enough;
        unsigned found = 0;
        for (auto &[days, x] : enemies) {
            KhdaysKh2Shell at;
            Way w = Way::None;
            if (!khdays_kh2_shell_read(days, &at) || (own_model && at.kind != model_kind))
                continue;
            const float off = off_by(d, at.place, w);
            if (off < best && (own_model || w != Way::World))
                best = off, found = days, way = w, s = at;
        }
        if (found != 0) {
            e = find(found);
            shell_draws[obj] = found;
        }
        if (e != nullptr && tracing()) {
            const char *file = nullptr;
            u32 offset = 0, run = 0;
            char model[160] = "?";
            if (resource != nullptr && khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run))
                std::snprintf(model, sizeof(model), "%s@%x", file, offset);
            khdays_log("kh2 enemies: %08x's model %s (render object %08x, placed the %s way) drawn as its KH2 enemy",
                       found, model, obj,
                       way == Way::World    ? "world"
                       : way == Way::Folded ? "folded"
                                            : "camera");
        }
    }
    if (e == nullptr && tracing()) { // a model drawn near a shell but not at its place (the link's check), once each
        static std::set<std::pair<unsigned, unsigned>> told;
        float inverse[16], world[16];
        inverse_affine(d.camera, inverse);
        row_matrix_mul(d.position, inverse, world);
        for (auto &[days, x] : enemies) {
            KhdaysKh2Shell near;
            if (!khdays_kh2_shell_read(days, &near))
                continue;
            const auto off = [&](const float *at) {
                return std::fabs(at[0] - near.place[0]) + std::fabs(at[1] - near.place[1]) +
                       std::fabs(at[2] - near.place[2]);
            };
            const float by_world = off(d.position + 12), by_base = off(d.base + 12), by_camera = off(world + 12);
            if (std::min({by_world, by_base, by_camera}) < 0.5F && told.insert({obj, days}).second) {
                const char *file = nullptr;
                u32 offset = 0, run = 0;
                char model[160] = "?";
                if (resource != nullptr && khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run))
                    std::snprintf(model, sizeof(model), "%s@%x", file, offset);
                khdays_log("kh2 enemies: %08x at %.3f %.3f %.3f; %s (render object %08x) drawn %.3f off as the world "
                           "way, %.3f as the folded, %.3f as the camera's",
                           days, double(near.place[0]), double(near.place[1]), double(near.place[2]), model, obj,
                           double(by_world), double(by_base), double(by_camera));
            }
        }
    }
    if (e == nullptr)
        return 0;
    // its stand-in's other model in the same file (Ms/04.p's at + 0xca00), or another shell's of its class standing
    // with it: left out, its KH2 model and effects drawn once a frame
    const unsigned frame = khdays_link_frame();
    if (e->drawn_frame == frame)
        return 1;
    e->drawn_frame = frame;
    if (e->pose.empty())
        pose(*e);
    // its model to the world (row vectors: its axes turned by its yaw, at its place), in the draw's way
    const float c = std::cos(e->actor->yaw()), sn = std::sin(e->actor->yaw());
    const float mine[16] = {c, 0, -sn, 0, 0, 1, 0, 0, sn, 0, c, 0, s.place[0], s.place[1], s.place[2], 1};
    float place[16], view[16], pure[16];
    spaces_for(d, way, mine, place, view, pure);
    std::copy(pure, pure + 16, e->pure);
    e->seen = true;
    draw_effects(*e, view, pure);
    const float fade = std::clamp(e->actor->fade, 0.0F, 1.0F);
    const unsigned alpha = unsigned(std::lround(fade * 31.0F));
    if (alpha == 0 || e->pose.empty())
        return 1; // gone: nothing of it, nor of its stand-in
    std::vector<float> bones(e->pose.size() * 16);
    for (std::size_t b = 0; b < e->pose.size(); ++b)
        std::copy(e->pose[b].begin(), e->pose[b].end(), bones.begin() + std::ptrdiff_t(16 * b));
    const int drawn = khdays_models_draw_posed(e->kind->mdlx.c_str(), place, nullptr, (alpha << 16) | (1u << 24),
                                               bones.data(), e->pose.size());
    if (!drawn && !e->told_draw) {
        e->told_draw = true;
        khdays_log("kh2 enemies: %s's model cannot be drawn (the message above says why)", e->kind->name.c_str());
    }
    return 1;
}

// The Days sounds a stand-in makes of its own (its class's MON_ archive, the enemies' common appearing and defeat:
// docs/RUNTIME_MAP.md section 37): those KH2's own sounds take the place of
bool stand_ins_own(const char *identifier)
{
    return std::strncmp(identifier, "seqarc/MON_", 11) == 0 ||
           std::strncmp(identifier, "seqarc/COMMON_SYS/se_all_enemy_", 31) == 0;
}

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
    const Kind *k = m.folder.empty() ? nullptr : find_kind(m.kinds, name);
    return k != nullptr && k->covered;
}
