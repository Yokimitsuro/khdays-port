// KINGDOM HEARTS II's enemies in Days's field (shell.h): the mod's files, each shell's KH2 actor ticked at KH2's 60
// a second, its travel put into the Days enemy's place, its attacks, its reactions and its end, its model drawn.
#include "shell.h"

#include "actor.h"
#include "calls.h"
#include "kinds.h"
#include "shell_days.h"

#include "khdays/assets/kh2_ai.h"
#include "khdays/assets/kh2_bar.h"
#include "khdays/assets/kh2_event.h"
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
#include <optional>
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
#include "../input/input.h"
}
#include "../../ui/reaction_command.h"
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
// reaction command's zone, its command at +2), its bone (+6, 0x4000 none), its offset on it (+8, +10, +12), its height
// (+14) and radius (+16); KH2's cm.
struct Shape final {
    int group = 0, kind = 0, bone = kNoBone, command = 0;
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
            s.command = u16(r + 2);
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
    // whether its script plays the player's motions itself (1:29: the Magnum Loader's Quick Blade, Cerberus's commands):
    // its zones' commands offered; those KH2's player's own code plays (the Soldier's Cyclone, the Dusk's Reversal: no
    // 1:29 in their scripts) are not yet
    bool plays_player = false;
    // its effects (its .a.us's PAX): the effects, the elements by their number, the pages of its pictures and of KH2's
    // resident ones (effect/texcommon.dpd)
    std::vector<std::shared_ptr<const kh2::EffectBlock>> fx;
    std::map<int, Element> fx_elements;
    // the sounds it plays (its motions' triggers 8, its elements'): KH2's sound id -> the mod's kh2/se/<id>.wav
    std::map<int, int> sounds;
    kh2::EffectBlock common;
    std::vector<std::unique_ptr<khdays::kh2_fx::Pages>> fx_pages; // by effect block: each its own pictures
    // its event sequences (its .a.us's entries of type 0x16: a reaction command's cameras), by their names' up to four
    // letters as FUN_1403db2f0 packs them (the first in the low byte)
    std::map<std::uint32_t, std::shared_ptr<const kh2::Event>> events;
};

// Whether a script calls the game's `kind`:`number` anywhere in its code: a call is a code word whose low four bits are
// 10, its table the word's top ten bits, its number the next word (the machine's case 10)
bool script_calls(const ai::Script &s, unsigned kind, unsigned number)
{
    for (std::size_t n = 0; n + 1 < s.words(); ++n) {
        const std::uint16_t w = s.word(n);
        if ((w & 0xfu) == 10u && (w >> 6) == kind && s.word(n + 1) == number)
            return true;
    }
    return false;
}

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
        // each block its own pages (its pictures and the resident ones), its HD pictures numbered on from the last
        // block's
        int first = 0;
        for (std::size_t b = 0; b < k.fx.size(); ++b) {
            auto pages = std::make_unique<khdays::kh2_fx::Pages>("kh2/obj/" + k.name + ".a.us#" + std::to_string(b));
            first += pages->add({k.fx[b].get()}, "kh2/fx/" + k.name, first);
            pages->add({&k.common}, "kh2/fx/texcommon");
            pages->tracing = tracing();
            k.fx_pages.push_back(std::move(pages));
        }
        khdays_log("kh2 enemies: %s's effects read (%zu effects, %zu elements%s)", k.name.c_str(), k.fx.size(),
                   k.fx_elements.size(), common.empty() ? "; no texcommon.dpd" : "");
    } catch (const std::exception &e) {
        khdays_log("kh2 enemies: %s's effects cannot be played (%s)", k.name.c_str(), e.what());
        k.fx.clear();
        k.fx_elements.clear();
        k.fx_pages.clear();
    }
}

// Its event sequences: every entry of type 0x16 in its .a.us, keyed by its name (docs/KH2_ENEMIES.md, "Event
// sequences")
void read_events(KindFiles &k, const fs::path &folder)
{
    const auto bar = read_file(folder / "kh2" / "obj" / (k.name + ".a.us"));
    const auto u32 = [&](std::size_t o) {
        return std::uint32_t(bar[o] | (bar[o + 1] << 8) | (bar[o + 2] << 16) | (std::uint32_t(bar[o + 3]) << 24));
    };
    if (bar.size() < 16 || std::memcmp(bar.data(), "BAR", 3) != 0)
        return;
    for (std::uint32_t i = 0; i < u32(4) && 16 + 16 * (i + 1) <= bar.size(); ++i) {
        const std::size_t e = 16 + 16 * std::size_t(i);
        const std::uint32_t at = u32(e + 8), size = u32(e + 12);
        if (std::uint16_t(bar[e] | (bar[e + 1] << 8)) != 0x16 || size == 0 || std::size_t(at) + size > bar.size())
            continue;
        try {
            k.events[u32(e + 4)] = std::make_shared<const kh2::Event>(kh2::read_event(bar.data() + at, size));
        } catch (const std::exception &x) {
            khdays_log("kh2 enemies: %s's event sequence %u cannot be played (%s)", k.name.c_str(), i, x.what());
        }
    }
    if (!k.events.empty())
        khdays_log("kh2 enemies: %s's %zu event sequences read", k.name.c_str(), k.events.size());
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
            k->plays_player = script_calls(*k->script, 1, 29);
            read_effects(*k, u8(m.folder));
            read_events(*k, u8(m.folder));
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
    unsigned took = 0; // the player its attack last landed on (its trigger 15)
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
        unsigned player = 0; // cast by the motion of its set a player it holds plays (his object): on his bones
        kh2::EffectMatrix last{}; // that one's place when last drawn, kept once he is let go
        bool placed = false;
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
    float fall_from = 0.0F; // how high above the ground its fall began (KH2's cm)
    std::string sent_state; // a state another enemy sent it (1:90), entered once `sent_after` ticks have gone
    float sent_after = 0.0F;
    float pure[16] = {};
    bool seen = false;
    unsigned ghost_frame = 0;
    unsigned drawn_frame = ~0u; // the frame its KH2 model was last drawn in: once a frame, whatever matched it
    unsigned ticks_traced = 0;  // KHDAYS_TRACE_KH2_ENEMIES=2: its ticks, its threads told once a second
    std::string state;          // the state it last entered (traced)
    // the Days player carrying it (1:18: Cerberus on the player in its reaction command), 0 none, and how
    unsigned carrier = 0;
    int carry_bone = 0, carry_flags = 0;
    std::uint32_t attack_record = 0; // trigger 15's attack (its + 0x18 the atkp entry), made on its first hit
    // a reaction command its zones offer this game's own player now (-1 none), how deep in (the distance over the
    // zone's radius), and the motion one was taken in (none offered again in it)
    int zone = -1;
    float zone_depth = 0.0F;
    int zone_taken_motion = -1;
    int days_damage = 0; // the last Days hit's damage on it: a KH2 player's blow is worked out from it
    int blow = 0;        // a KH2 player's blow on it to land (its Days damage), 0 none
    bool blow_landing = false; // that blow's hit going through Days now
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

// A Days player a KH2 enemy's script has taken in hand (KH2's player is an actor its enemies' scripts move and make
// play their motions, 1:29; Days's are Days's): from the first motion of the enemy's set it plays until the script
// gives it back (1:13, its idle) or the enemy is gone. Days's control of it stands still meanwhile
// (khdays_kh2_enemies_holding, from Ov022_ActorUpdate); the script puts and turns it, the motion's root moves it, its
// ranges offer reaction commands (20), keep it from being hit (27), its triggers play the enemy's sounds and effects.
struct Held final {
    unsigned enemy = 0; // the Days enemy whose set its motion is from
    int slot = -1;      // that slot (an entry of type 0x14: '0010' its body, '0011' its weapon)
    const kh2::MotionSet::Variant *body = nullptr, *weapon = nullptr;
    float time = 0.0F;
    bool ended = false;
    Vec3 place{}; // KH2's, its feet
    float yaw = 0.0F;
    int offered = -1;       // the reaction command its motion offers now (range 20), -1 none
    std::set<int> taken;    // the commands taken in this motion
    bool unhittable = false; // range 27
    ai::Machine *machine = nullptr; // its thread (1:112), in its enemy's machine
    std::uint32_t thread = 0;
    std::set<std::size_t> struck; // its weapon's attack ranges that hit its enemy
    float cued = -1.0F;           // its triggers looked at up to this time
    bool idle = false;            // its motion blends back to the player's own idle (range 2): let go
    Vec3 move{};                  // a script's move of him this tick (1:89), KH2's cm
};
std::map<unsigned, Held> held; // by the Days player's object
// a thread a script started on a player before taking it in hand (Cerberus's 1:112 comes before its 1:29), by object
std::map<unsigned, std::pair<ai::Machine *, std::uint32_t>> waiting_threads;
unsigned local_player = 0;     // this game's own player's object (khdays_kh2_enemies_holding sees it)
unsigned local_takes = 0;      // the reaction commands this game's own player took (khdays_kh2_enemies_taken)
int last_days_damage = 0;      // the last Days hit's damage on any KH2 enemy (a blow on one not hit yet)
int local_taken = -1;          // the last of them
std::map<unsigned, unsigned> player_of_node; // each player's model's scene node -> its object (the models' draws)

// KH2's one event player (FUN_1402cc4b0; a sequence started ends the one before): the sequence, the enemy whose script
// started it, the player whose camera it takes, its KH2 ticks since it started; the place (Days's) and the yaw (KH2's)
// its camera is set from
struct Playing final {
    std::shared_ptr<const kh2::Event> event;
    unsigned enemy = 0, player = 0, ticks = 0;
    float base[3] = {};
    float yaw = 0.0F;
    std::size_t next = 0; // its next command on a frame (KH2's loop waits on each in turn)
    // the screen's trail it started (command 0x17, FUN_14015f250 each tick): the picture before over the new at
    // `alpha`, its quad `size` of the screen, turned `angle` (on by its `turn` a tick for `turn_ticks`)
    struct Trail final {
        float alpha = 0.0F, size[2] = {1.0F, 1.0F}, angle = 0.0F, turn = 0.0F;
        int turn_ticks = 1, turned = 0;
    };
    std::optional<Trail> trail;
    // the camera shake it started (command 0x27, FUN_140528920 each tick): its ticks gone, the eye's offset now (cm,
    // the camera's x right, y up, z ahead)
    struct Shake final {
        kh2::EventShake shake;
        int gone = 0;
        std::array<float, 3> offset{};
    };
    std::optional<Shake> shake;
};
std::optional<Playing> playing;

// One of KH2's commands (03system.bin's "cmd", 0x30 bytes each: OpenKH's 03system page): what a reaction command
// sends when taken (its argument), the one it follows (require: a range 26 of that number must be on), its name's text
struct Command final {
    int argument = 0, require = 0, text = 0;
};
const Command *command(int id);

// The Days player's object a script's player object stands for, 0 none
unsigned days_object(const Object &o)
{
    for (const auto &[object, mine] : players)
        if (mine.get() == &o)
            return object;
    return 0;
}

void cast(Enemy &e, int number, unsigned player = 0);
void sound(const Enemy &e, int id);

void pose(Enemy &e);
void days_point(const Enemy &e, const KhdaysKh2Shell &s, int bone, const Vec3 &at, float out[3]);

class Field final : public World {
public:
    // 1:83: a point on one of its bones in the pose it has now (0x4000 the actor itself, in its own axes), KH2's world
    bool bone_point(Actor &a, int bone, const Vec3 &local, Vec3 &out) override
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

    // 2:9: its health record given Days's health of it (now, at most), as KH2's record of it holds its own
    void health(Actor &a) override
    {
        const auto it = by_actor.find(&a);
        const std::uint32_t record = a.u32(0x5c0);
        KhdaysKh2Shell s;
        if (it == by_actor.end() || record == 0 || !khdays_kh2_shell_read(it->second->days, &s))
            return;
        a.memory().set_u32(record, std::uint32_t(std::max(0, s.hp)));
        a.memory().set_u32(record + 4, std::uint32_t(std::max(1, s.hp_max)));
    }

    // 1:98, 1:99: the ground Days last found under it (its place's height when none yet)
    float ground(Actor &a) override
    {
        const auto it = by_actor.find(&a);
        return it != by_actor.end() && it->second->floor_known ? it->second->floor : a.place()[1];
    }

    // 1:90: a state every enemy of the field enters, at once or each after a time at random below `delay` (KH2's
    // chance, DAT_140783ca0 x 0x10dcd)
    void broadcast(Actor &, const std::string &name, float delay) override
    {
        static std::uint32_t chance = 1;
        for (auto &[actor, e] : by_actor) {
            if (e->dying)
                continue;
            if (delay > 0.0F) {
                chance *= 0x10dcdu;
                e->sent_state = name;
                e->sent_after = float(chance) * (1.0F / 4294967296.0F) * delay;
            } else {
                Actor &x = *e->actor;
                x.states().enter(x, name);
            }
        }
    }

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

    bool player_motion(Object &o, Actor &by, int slot, float frame) override;
    void player_own_motion(Object &o, int id) override;
    void player_place(Object &o, const Vec3 &place) override
    {
        if (const auto h = held.find(days_object(o)); h != held.end())
            h->second.place = place;
    }
    void player_move(Object &o, const Vec3 &way, float speed) override
    {
        if (const auto h = held.find(days_object(o)); h != held.end())
            h->second.move = {way[0] * speed, way[1] * speed, way[2] * speed};
    }
    void player_yaw(Object &o, float yaw) override
    {
        if (const auto h = held.find(days_object(o)); h != held.end())
            h->second.yaw = yaw;
    }
    void player_thread(Object &o, ai::Machine &m, std::uint32_t id) override;
    void carry(Actor &carried, Object &by, int bone, int flags) override;
    void uncarry(Actor &carried) override;
    void event(Actor &a, const std::string &name, Object *target) override;
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

void let_go(unsigned object, const char *why);

void forget(unsigned days)
{
    auto it = enemies.find(days);
    if (it == enemies.end())
        return;
    std::vector<unsigned> holding;
    for (const auto &[object, h] : held)
        if (h.enemy == days)
            holding.push_back(object);
    if (playing && playing->enemy == days)
        playing.reset();
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

// KH2's player as its enemies' attacks find him: Sora's shapes (P_EX100's: a cylinder 85 cm high, 35 round, and
// spheres on his bones up to his head) stand about 1.45 m high. Days's player's own sphere (half a metre round, on
// the ground) left the attacks KH2 aims at a standing Sora's chest and head above him (Cerberus's bites).
constexpr float kPlayerHeight = 1.45F;

// KH2's trigger 15 (FUN_1403d1730 -> the attacker's class virtual + 0xe0, FUN_1403d5500): an attack of the enemy's
// landed on a player -- the one hit and the attack (its + 0x18 its atkp entry, 2:25), the attack read first (fp + 0).
// Cerberus's bite (atkp 359) holding the player begins its reaction command.
void landed(Enemy &e, unsigned object, int atkp)
{
    Actor &a = *e.actor;
    the_field().player(a); // the players' objects as they stand now
    const auto it = players.find(object);
    if (it == players.end() || !it->second)
        return;
    e.took = object;
    if (e.attack_record == 0)
        e.attack_record = mod().memory.add(0x100, a.kind() + "'s attack", true);
    mod().memory.set_u32(e.attack_record + 0x18, std::uint32_t(atkp));
    mod().memory.set_u32(e.attack_record + 0x94, a.address()); // its owner
    const ai::Value values[2] = {ai::Value::of_address(it->second->address()), ai::Value::of_address(e.attack_record)};
    a.event(15, values);
}

// The current motion's attacks (its ranges 10 and 33: an atkp entry and a collision group) that cover its time: each
// of the group's attack spheres hits the players it reaches -- each one a body from his feet to KH2's player's
// height, his Days radius round -- each player once a range (KH2's hit's own "once")
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
                // the nearest point of his body's middle line (from his feet, a radius up, to his top, a radius down)
                const float low = p.place[1] + p.radius, high = std::max(low, p.place[1] + kPlayerHeight - p.radius);
                const float d[3] = {p.place[0] - c[0], std::clamp(c[1], low, high) - c[1], p.place[2] - c[2]};
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
                if (const auto h = held.find(p.object); h != held.end() && h->second.unhittable)
                    continue; // its motion's range 27
                e.struck[r].insert(p.object);
                const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
                const float push[3] = {flat > 0.0F ? d[0] / flat : 0.0F, 0.0F, flat > 0.0F ? d[2] / flat : 0.0F};
                const int took = khdays_kh2_shell_hit(e.days, p.object, e.slot, push);
                if (tracing())
                    khdays_log("kh2 enemies: %08x's motion %d at %.1f (atkp %d, group %d) hits %08x%s", e.days,
                               a.motion.id, double(a.motion.time), int(std::int16_t(range.params[0])), group, p.object,
                               took ? "" : " (not taken)");
                if (took)
                    landed(e, p.object, std::int16_t(range.params[0]));
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
        if (trace_level() >= 2 && e.windows_told.count(r) != 0 && nearest >= 0.0F)
            khdays_log("kh2 enemies: %08x's motion %d at %.1f still attacks (group %d): nearest player %.2f away, "
                       "reached within %.2f, the sphere %.2f up", e.days, a.motion.id, double(a.motion.time), group,
                       double(nearest), double(reach), double(centre[1] - s.place[1]));
        if (tracing() && e.windows_told.insert(r).second)
            khdays_log("kh2 enemies: %08x's motion %d attacks at %.1f (atkp %d, group %d): %d spheres, nearest player "
                       "%.2f away, reached within %.2f; the sphere at %.2f %.2f %.2f, the body at %.2f %.2f %.2f "
                       "facing the player by %.2f",
                       e.days, a.motion.id, double(a.motion.time), int(std::int16_t(range.params[0])), group, spheres,
                       double(nearest), double(reach), double(centre[0]), double(centre[1]), double(centre[2]),
                       double(s.place[0]), double(s.place[1]), double(s.place[2]), double(toward));
    }
}

// ------------------------------------------------------------------------------------------------- the players in hand

const Command *command(int id)
{
    static std::map<int, Command> table;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> sys = read_file(u8(mod().folder) / "kh2" / "03system.bin");
        try {
            for (const kh2::BarEntry &e : kh2::bar_entries(sys.data(), sys.size()))
                if (e.name == "cmd" && e.size >= 8) {
                    std::uint32_t count = 0;
                    std::memcpy(&count, sys.data() + e.offset + 4, 4);
                    for (std::uint32_t i = 0; i < count && 8 + 0x30 * (i + 1) <= e.size; ++i) {
                        const std::uint8_t *c = sys.data() + e.offset + 8 + 0x30 * i;
                        std::uint16_t number = 0, require = 0;
                        std::int16_t argument = 0;
                        std::int32_t text = 0;
                        std::memcpy(&number, c, 2);
                        std::memcpy(&argument, c + 4, 2);
                        std::memcpy(&text, c + 8, 4);
                        std::memcpy(&require, c + 34, 2);
                        table[number] = {argument, require, text};
                    }
                }
        } catch (const std::exception &x) {
            khdays_log("kh2 enemies: the mod's kh2/03system.bin cannot be read (%s)", x.what());
        }
        if (table.empty())
            khdays_log("kh2 enemies: no KH2 commands (the mod's kh2/03system.bin): a reaction command sends its own "
                       "number and follows none");
    }
    const auto it = table.find(id);
    return it != table.end() ? &it->second : nullptr;
}

// KH2's player's files (the mod's kh2/obj/P_EX100.mdlx, Sora's): his skeleton, which the enemies' motions for the
// player are made for (Roxas's P_EX110 has its bodies' rest places), and his body's spheres on his bones (his shapes of
// kind 0x302: his head, chest, waist and arms)
struct PlayerFiles final {
    std::shared_ptr<const kh2::MotionSkeleton> skeleton;
    std::vector<Shape> spheres;
};

const PlayerFiles &player_files()
{
    static PlayerFiles files;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> mdlx = read_file(u8(mod().folder) / "kh2" / "obj" / "P_EX100.mdlx");
        try {
            if (!mdlx.empty())
                files.skeleton =
                    std::make_shared<const kh2::MotionSkeleton>(kh2::read_motion_skeleton(mdlx.data(), mdlx.size()));
        } catch (const std::exception &x) {
            khdays_log("kh2 enemies: the mod's kh2/obj/P_EX100.mdlx cannot be read (%s)", x.what());
        }
        if (!files.skeleton)
            khdays_log("kh2 enemies: no KH2 player's skeleton (the mod's kh2/obj/P_EX100.mdlx): a held player's KH2 "
                       "model keeps the game's pose");
        for (const Shape &s : read_shapes(mdlx))
            if (s.kind == 0x302 && s.bone != kNoBone)
                files.spheres.push_back(s);
    }
    return files;
}

const kh2::MotionSkeleton *player_skeleton()
{
    return player_files().skeleton.get();
}

// An attack's power in KH2's battle table (00battle.bin's atkp, 48 bytes each: + 2 its number, + 6 its power, OpenKH's
// 00battle page; its first entry of that number), 0 when the mod has no kh2/00battle.bin or no such attack
int attack_power(int atkp)
{
    static std::map<int, int> power;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> battle = read_file(u8(mod().folder) / "kh2" / "00battle.bin");
        try {
            for (const kh2::BarEntry &e : kh2::bar_entries(battle.data(), battle.size()))
                if (e.name == "atkp" && e.size >= 8) {
                    std::uint32_t count = 0;
                    std::memcpy(&count, battle.data() + e.offset + 4, 4);
                    for (std::uint32_t i = 0; i < count && 8 + 48 * (i + 1) <= e.size; ++i) {
                        std::uint16_t number = 0, value = 0;
                        std::memcpy(&number, battle.data() + e.offset + 8 + 48 * i + 2, 2);
                        std::memcpy(&value, battle.data() + e.offset + 8 + 48 * i + 6, 2);
                        power.emplace(number, value);
                    }
                }
        } catch (const std::exception &x) {
            khdays_log("kh2 enemies: the mod's kh2/00battle.bin cannot be read (%s)", x.what());
        }
        if (power.empty())
            khdays_log("kh2 enemies: no KH2 attacks (the mod's kh2/00battle.bin): a player's blows in a reaction "
                       "command deal nothing");
    }
    const auto it = power.find(atkp);
    return it != power.end() ? it->second : 0;
}

void let_go(unsigned object, const char *why)
{
    const auto h = held.find(object);
    if (h == held.end())
        return;
    for (auto &[days, e] : enemies)
        if (e->carrier == object)
            e->carrier = 0;
    if (const auto o = players.find(object); o != players.end() && o->second)
        o->second->set_bits(field::kStatus, bit::kOtherMotion, false);
    if (tracing())
        khdays_log("kh2 enemies: player %08x let go (%s)", object, why);
    held.erase(h);
}

bool Field::player_motion(Object &o, Actor &by, int slot, float frame)
{
    const unsigned object = days_object(o);
    const auto it = by_actor.find(&by);
    if (object == 0 || it == by_actor.end() || slot < 0)
        return false;
    const kh2::MotionSet &set = *it->second->kind->set;
    const kh2::MotionSet::Variant *body = set.variant(std::size_t(slot), "0010"); // KH2's player's body
    for (const auto &v : set.variants(std::size_t(slot)))
        if (body == nullptr && v.motion)
            body = &v;
    if (body == nullptr) {
        if (tracing())
            khdays_log("kh2 enemies: %08x's slot %d has no motion for a player", it->second->days, slot);
        return false;
    }
    const bool fresh = held.count(object) == 0;
    Held &h = held[object];
    if (fresh) {
        h.place = o.place();
        h.yaw = o.yaw();
        if (const auto w = waiting_threads.find(object); w != waiting_threads.end()) {
            h.machine = w->second.first;
            h.thread = w->second.second;
            waiting_threads.erase(w);
        }
    }
    h.enemy = it->second->days;
    h.slot = slot;
    h.body = body;
    h.weapon = nullptr; // its weapon's ('0011' with data of its own: what it attacks with, its attributes)
    for (const auto &v : set.variants(std::size_t(slot)))
        if (h.weapon == nullptr && v.name == "0011" && v.own && (v.attributes || v.motion))
            h.weapon = &v;
    h.time = std::clamp(frame, 0.0F, body->motion->length_ticks());
    h.ended = false;
    h.offered = -1;
    h.taken.clear();
    h.struck.clear();
    h.cued = h.time > 0.0F ? h.time : -1.0F;
    o.set_u32(field::kMotion, std::uint32_t(slot));
    o.set_u32(field::kMotionNow, std::uint32_t(slot));
    o.set_bits(field::kStatus, bit::kMotionEnded, false);
    o.set_bits(field::kStatus, bit::kOtherMotion, true);
    if (tracing())
        khdays_log("kh2 enemies: %08x %s player %08x: %s's motion %d (%s, %s) from %.1f, %.1f long", h.enemy,
                   fresh ? "takes" : "keeps", object, it->second->kind->name.c_str(), slot, set.name(std::size_t(slot)).c_str(),
                   body->name.c_str(), double(frame), double(body->motion->length_ticks()));
    return true;
}

void Field::player_own_motion(Object &o, int id)
{
    const unsigned object = days_object(o);
    // KH2 queues it on the player's controller (1:13, FUN_1403c8310), taken once the motion of another's set he plays
    // gives him back: a motion blending back to his idle later (range 2) keeps him till then (the Magnum Loader's
    // thread asks for 0 as soon as Quick Blade begins); otherwise (Cerberus's, at its end) he is let go now
    if (const auto h = held.find(object); h != held.end() && !h->second.ended && h->second.body->attributes) {
        for (const auto &r : h->second.body->attributes->ranges)
            if (r.id == 2 && float(r.start) > h->second.time) {
                if (tracing())
                    khdays_log("kh2 enemies: player %08x's own motion %d queued till his motion blends back to idle "
                               "(%d)",
                               object, id, r.start);
                return;
            }
    }
    o.set_u32(field::kMotion, std::uint32_t(id));
    o.set_u32(field::kMotionNow, std::uint32_t(id));
    if (held.count(object) != 0) {
        char why[48];
        std::snprintf(why, sizeof(why), "its own motion %d asked for", id);
        let_go(object, why);
    }
}

// a thread found by its id in a thread's tree
ai::Thread *thread_of(ai::Thread *t, std::uint32_t id)
{
    if (t == nullptr)
        return nullptr;
    if (t->id == id)
        return t;
    for (auto &c : t->children)
        if (ai::Thread *found = thread_of(c.get(), id))
            return found;
    return nullptr;
}

void Field::player_thread(Object &o, ai::Machine &m, std::uint32_t id)
{
    const unsigned object = days_object(o);
    const auto h = held.find(object);
    if (h == held.end()) { // kept until the script takes the player in hand
        waiting_threads[object] = {&m, id};
        return;
    }
    if (h->second.machine == &m && h->second.thread != 0 && h->second.thread != id)
        if (ai::Thread *old = thread_of(m.main_thread(), h->second.thread))
            m.stop(old);
    h->second.machine = &m;
    h->second.thread = id;
}

void Field::carry(Actor &carried, Object &by, int bone, int flags)
{
    const auto it = by_actor.find(&carried);
    const unsigned object = days_object(by);
    if (it == by_actor.end())
        return;
    if (object == 0 || held.count(object) == 0) {
        khdays_log("kh2 enemies: %08x is put on %s, which the port does not carry yet: it stays where it is",
                   it->second->days, by.name().c_str());
        return;
    }
    it->second->carrier = object;
    it->second->carry_bone = bone;
    it->second->carry_flags = flags;
    if (tracing())
        khdays_log("kh2 enemies: %08x carried by player %08x (bone %#x, flags %#x)", it->second->days, object, bone,
                   flags);
}

void Field::uncarry(Actor &carried)
{
    const auto it = by_actor.find(&carried);
    if (it == by_actor.end() || it->second->carrier == 0)
        return;
    if (tracing())
        khdays_log("kh2 enemies: %08x taken off player %08x", it->second->days, it->second->carrier);
    it->second->carrier = 0;
}

// 1:157: the sequence of that name (FUN_1403db2f0: its letters packed as the entries' names are) in the enemy's
// .a.us, played by the one event player. In KH2 its camera's keys are set from the event's own matrix (+ 0x60 of its
// object, FUN_1403ad2d0), the identity until a command 0x51 calls back FUN_1403ac630, which copies the target's matrix
// (+ 0x808, FUN_140406a90): Cerberus's have none, so KH2 plays them in its arena's own place. Days's rooms are not
// that arena: the port sets them from the target's place and turn as the sequence starts -- the call back's binding
// (the actor's own without a target).
void Field::event(Actor &a, const std::string &name, Object *target)
{
    const auto it = by_actor.find(&a);
    if (it == by_actor.end())
        return;
    Enemy &e = *it->second;
    std::uint32_t key = 0;
    for (std::size_t i = 0; i < name.size() && i < 4; ++i)
        key |= std::uint32_t(std::uint8_t(name[i])) << (8 * i);
    const auto found = e.kind->events.find(key);
    if (found == e.kind->events.end()) {
        khdays_log("kh2 enemies: %08x has no event sequence \"%s\" (its .a.us's entries of type 0x16)", e.days,
                   name.c_str());
        return;
    }
    Playing p;
    p.event = found->second;
    p.enemy = e.days;
    p.player = e.took;
    for (const auto &[object, h] : held)
        if (h.enemy == e.days)
            p.player = object;
    const Object &from = target != nullptr ? *target : static_cast<const Object &>(a);
    to_days(from.place(), p.base);
    p.yaw = from.yaw();
    if (tracing()) {
        std::string unplayed;
        for (const int c : p.event->commands)
            if (c != 0 && c != 6 && c != 8 && c != 0xf && c != 0x10 && c != 0x13)
                unplayed += " " + std::to_string(c);
        khdays_log("kh2 enemies: %08x plays event sequence %s (\"%s\", %d frames) for player %08x%s%s", e.days,
                   p.event->name.c_str(), name.c_str(), p.event->length, p.player,
                   unplayed.empty() ? "" : "; commands not played:", unplayed.c_str());
    }
    playing = std::move(p);
}

// A shake's offset this tick (FUN_140528920): each axis at random within half its amplitude either way, times the
// envelope at the ticks gone
std::array<float, 3> shake_offset(const Playing::Shake &s)
{
    static std::uint32_t seed = 0x2545f491u;
    const float envelope = kh2::event_shake_envelope(s.shake.shape, float(s.gone), float(s.shake.ticks));
    std::array<float, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float r = float(seed >> 8) / float(1u << 24);
        out[i] = (r * s.shake.amplitude[i] - s.shake.amplitude[i] * 0.5F) * envelope;
    }
    return out;
}

// The event player's tick with its enemy's (KH2's 60 a second; its frames 30): its commands on frames played as the
// frame reaches each, its trail turned and its shake moved on; over at its length
void event_tick(const Enemy &e)
{
    if (!playing || playing->enemy != e.days)
        return;
    Playing &p = *playing;
    const int frame = int((p.ticks + 1) / 2);
    while (p.next < p.event->timed.size() && p.event->timed[p.next].frame <= frame) {
        const kh2::EventTimed &c = p.event->timed[p.next++];
        if (c.command == 0x17 && c.trail.stop) {
            p.trail.reset(); // FUN_14015f170(0)
        } else if (c.command == 0x17) {
            Playing::Trail tr;
            tr.alpha = float(c.trail.alpha) / 128.0F;
            tr.size[0] = float(c.trail.width) / 512.0F; // of KH2's 512 x 416 screen (FUN_14015f1e0)
            tr.size[1] = float(c.trail.height) / 416.0F;
            tr.turn = c.trail.turn;
            tr.turn_ticks = c.trail.turn_ticks != 0 ? c.trail.turn_ticks : 1;
            p.trail = tr;
        } else if (c.command == 0x27) {
            p.shake = Playing::Shake{c.shake, 0, {}};
        }
        if (tracing())
            khdays_log("kh2 enemies: event sequence %s plays command %#x at frame %d", p.event->name.c_str(), c.command,
                       frame);
    }
    if (p.trail && p.trail->turn != 0.0F && p.trail->turned < p.trail->turn_ticks) {
        p.trail->angle = std::remainder(p.trail->angle + p.trail->turn, 6.2831853F);
        ++p.trail->turned;
    }
    if (p.shake) {
        p.shake->offset = shake_offset(*p.shake);
        if (++p.shake->gone >= p.shake->shake.ticks)
            p.shake.reset();
    }
    if (++p.ticks >= 2u * unsigned(std::max(p.event->length, 0))) {
        if (tracing())
            khdays_log("kh2 enemies: event sequence %s over", playing->event->name.c_str());
        playing.reset();
    }
}

// A point of an event's camera (KH2's axes from its base: the model's turned half round x) in Days's world: as an
// actor's own axes on no bone (days_point)
void event_point(const Playing &p, const std::array<float, 3> &k, float out[3])
{
    const Vec3 t = turned({k[0], 0.0F, -k[2]}, p.yaw);
    out[0] = p.base[0] + t[0] / kKh2PerDays;
    out[1] = p.base[1] - k[1] / kKh2PerDays;
    out[2] = p.base[2] + t[2] / kKh2PerDays;
}

// A KH2 tick of a player its enemy holds: its motion on (its root's travel turned by its yaw, as Actor::step), held at
// its end (the ended bit its script waits on) or looped; its ranges now (20 a reaction command offered when the one
// it follows is on -- range 26's -- 27 not to be hit), its triggers passed (1 the enemy's effect, 8 its sound), its
// weapon's attacks.
void hold_tick(Enemy &e, unsigned object, Held &h)
{
    const kh2::Motion &m = *h.body->motion;
    const float length = m.length_ticks();
    const bool loops = m.loop_from() < m.end() && m.loop_ticks() < length;
    const float t0 = h.time;
    float t1 = t0 + 1.0F;
    const auto root = [&m](float at) {
        const kh2::Mat4 r = m.root_motion(at);
        return Vec3{r[12], r[13], r[14]};
    };
    Vec3 model{};
    if (!h.ended) {
        const Vec3 a = root(t0), b = root(std::min(t1, length));
        for (int i = 0; i < 3; ++i)
            model[std::size_t(i)] = b[std::size_t(i)] - a[std::size_t(i)];
        if (t1 >= length) {
            if (loops) {
                const float loop = m.loop_ticks();
                const float over = std::fmod(t1 - length, length - loop);
                const Vec3 c = root(loop), d = root(loop + over);
                for (int i = 0; i < 3; ++i)
                    model[std::size_t(i)] += d[std::size_t(i)] - c[std::size_t(i)];
                t1 = loop + over;
            } else {
                t1 = length;
                h.ended = true;
                if (const auto o = players.find(object); o != players.end() && o->second)
                    o->second->set_bits(field::kStatus, bit::kMotionEnded, true);
            }
        }
        h.time = t1;
        const Vec3 along = turned({model[0], 0.0F, model[2]}, h.yaw);
        h.place = {h.place[0] + along[0], h.place[1] - model[1], h.place[2] + along[2]};
    }
    for (int i = 0; i < 3; ++i) // a script's move (1:89), this tick's
        h.place[std::size_t(i)] += h.move[std::size_t(i)];
    h.move = {};
    const auto in = [&h](const kh2::MotionAttributes::Range &r) {
        return h.time >= float(r.start) && h.time < float(r.end);
    };
    h.offered = -1;
    h.unhittable = false;
    h.idle = false;
    if (const kh2::MotionAttributes *attr = h.body->attributes.get()) {
        std::set<int> chain; // range 26: the commands this motion follows on from
        for (const auto &r : attr->ranges)
            if (r.id == 26 && !r.params.empty() && in(r))
                chain.insert(std::int16_t(r.params[0]));
        for (const auto &r : attr->ranges) {
            if (!in(r))
                continue;
            if (r.id == 27)
                h.unhittable = true;
            if (r.id == 2) // the Magnum Loader's Quick Blade: the player back to his own from its frame 107
                h.idle = true;
            if (r.id == 20 && !r.params.empty()) {
                const int id = std::int16_t(r.params[0]);
                const Command *c = command(id);
                if (h.taken.count(id) == 0 && (c == nullptr || c->require == 0 || chain.count(c->require) != 0))
                    h.offered = id;
            }
        }
        std::uint32_t passed = 0, ai_ranges = 0;
        for (const auto &tr : attr->triggers) {
            if (float(tr.frame) <= h.cued || float(tr.frame) > h.time)
                continue;
            if (tr.id == 1 && !tr.params.empty())
                cast(e, std::int16_t(tr.params[0]), object);
            else if (tr.id >= 9 && tr.id <= 12)
                passed |= 1u << (tr.id - 9);
            else if (const int id = trigger_sound(tr); id > 0)
                sound(e, id);
        }
        for (const auto &r : attr->ranges)
            if (r.id >= 16 && r.id <= 19 && in(r))
                ai_ranges |= 1u << (r.id - 16);
        // his + 0x2ac and + 0x2ad as an enemy's (1:150, 1:151: the Magnum Loader's thread steps on his AB10's triggers
        // 9 and 10, frames 12 and 42)
        if (const auto o = players.find(object); o != players.end() && o->second)
            o->second->set_u32(field::kMotionMarks,
                               (o->second->u32(field::kMotionMarks) & ~0xffffu) | ai_ranges | (passed << 8));
    }
    if (h.weapon != nullptr && h.weapon->attributes)
        for (std::size_t r = 0; r < h.weapon->attributes->ranges.size(); ++r) {
            const auto &range = h.weapon->attributes->ranges[r];
            if ((range.id != 10 && range.id != 33) || range.params.size() < 2 || !in(range) || !h.struck.insert(r).second)
                continue;
            // its weapon's attack on the enemy holding it (Cerberus's finish, atkp 598; the Magnum Loader's Quick Blade,
            // 657 and 656): Days's damage, the player's own last hit on it -- else on any KH2 enemy, else 1 -- times the
            // attack's power in KH2 (00battle.bin's atkp: 100 a plain hit's, 598's 300)
            const int atkp = std::int16_t(range.params[0]);
            const int power = attack_power(atkp);
            const int base = e.days_damage > 0 ? e.days_damage : last_days_damage > 0 ? last_days_damage : 1;
            e.blow += power > 0 ? std::max(1, base * power / 100) : 0;
            if (tracing())
                khdays_log("kh2 enemies: player %08x's weapon strikes %08x (atkp %d, power %d): %d damage (its last Days "
                           "hit's %d)",
                           object, e.days, atkp, power, e.blow, e.days_damage);
        }
    h.cued = h.time;
    if (const auto o = players.find(object); o != players.end() && o->second) {
        o->second->set_place(h.place);
        o->second->set_yaw(h.yaw);
    }
}

// KH2's reaction command zones: the shapes of kind 5 (their command at + 2) in the collision groups the motion's
// ranges 4 turn on now (FUN_14040d810: range 4 FUN_1403f1fa0 on + 0x5a0, range 5 the group off), offering their
// command to this game's own player while his place (+ 0x670) lies within a zone's radius of its point (the test KH2's
// own uses of a zone make, FUN_14054ae70: the place's distance, FUN_140140f50, under the shape's + 0x10). The Magnum
// Loader's eight along its rush (52A, group 5: Quick Blade), the Armored Knight's; the Soldier's and the Dusk's not
// (KindFiles::plays_player). Not while an enemy holds him, nor again in the motion one was taken in.
void zone_offer(Enemy &e)
{
    e.zone = -1;
    const Actor &a = *e.actor;
    if (a.motion.id != e.zone_taken_motion)
        e.zone_taken_motion = -1; // a motion after the one a command was taken in: its zones offer again
    const auto p = players.find(local_player);
    if (local_player == 0 || held.count(local_player) != 0 || e.dying || !e.kind->plays_player || p == players.end() ||
        !p->second || a.motion.id < 0 || a.motion.id == e.zone_taken_motion)
        return;
    const kh2::MotionAttributes *attr = e.kind->set->attributes(std::size_t(a.motion.id));
    if (attr == nullptr)
        return;
    std::set<int> groups;
    for (const auto &r : attr->ranges)
        if (r.id == 4 && !r.params.empty() && a.motion.time >= float(r.start) && a.motion.time < float(r.end))
            groups.insert(std::int16_t(r.params[0]));
    for (const auto &r : attr->ranges)
        if (r.id == 5 && !r.params.empty() && a.motion.time >= float(r.start) && a.motion.time < float(r.end))
            groups.erase(std::int16_t(r.params[0]));
    if (groups.empty())
        return;
    KhdaysKh2Shell now{};
    to_days(a.place(), now.place);
    if (e.pose.empty())
        pose(e);
    const Vec3 player = p->second->place();
    for (const Shape &sh : e.kind->shapes) {
        if ((sh.kind & 0xff) != 5 || groups.count(sh.group) == 0 || sh.command <= 0 || sh.radius <= 0.0F)
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
        const std::uint16_t buttons = s != nullptr ? s->ds_buttons : 0;
        pressed = std::uint16_t(buttons & ~was);
        was = buttons;
    }
    for (auto &[object, h] : held) {
        if (h.enemy != e.days || h.offered < 0 || object != local_player || (pressed & KHDAYS_KEY_X) == 0)
            continue;
        const int id = h.offered;
        h.taken.insert(id);
        h.offered = -1;
        ++local_takes;
        local_taken = id;
        const Command *c = command(id);
        const int number = c != nullptr ? c->argument : id;
        const auto o = players.find(object);
        const ai::Value values[3] = {ai::Value::of_address(o != players.end() && o->second ? o->second->address() : 0),
                                     ai::Value::of_int(number), ai::Value::of_address(e.actor->address())};
        e.actor->event(11, values);
        if (tracing())
            khdays_log("kh2 enemies: player %08x takes reaction command %d: message %d to %08x", object, id, number,
                       e.days);
    }
    if (e.zone >= 0 && (pressed & KHDAYS_KEY_X) != 0 && held.count(local_player) == 0 && zone_offerer() == &e) {
        const int id = e.zone;
        e.zone = -1;
        e.zone_taken_motion = e.actor->motion.id;
        ++local_takes;
        local_taken = id;
        const Command *c = command(id);
        const int number = c != nullptr ? c->argument : id;
        const auto o = players.find(local_player);
        const ai::Value values[3] = {ai::Value::of_address(o != players.end() && o->second ? o->second->address() : 0),
                                     ai::Value::of_int(number), ai::Value::of_address(e.actor->address())};
        e.actor->event(11, values);
        if (tracing())
            khdays_log("kh2 enemies: player %08x takes reaction command %d from %08x's zone: message %d", local_player,
                       id, e.days, number);
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

// Element `number` of the kind's effects started on the enemy (`player`: cast by the motion of its set that player
// plays in its hands, whose bones -- KH2's player's -- its element's are)
void cast(Enemy &e, int number, unsigned player)
{
    const KindFiles &k = *e.kind;
    const auto it = k.fx_elements.find(number);
    if (it == k.fx_elements.end() || it->second.effect >= k.fx.size())
        return;
    try {
        Enemy::Running r;
        r.fx = std::make_unique<kh2::EffectInstance>(k.fx[it->second.effect], it->second.emitter, e.fx_seed++);
        r.element = &it->second;
        r.player = player;
        e.fx.push_back(std::move(r));
        if (tracing())
            khdays_log("kh2 enemies: %08x casts element %d (effect %zu, emitter %zu, bone %d, sound %d)", e.days,
                       number, it->second.effect, it->second.emitter, it->second.bone, it->second.sound);
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: %s's effect %d cannot be played (%s)", k.name.c_str(), number, x.what());
    }
}

// The current motion's single-frame triggers its time passed this tick (KH2's ANB triggers): 1 an effect, 8 a sound,
// 9 to 12 bits 0 to 3 of + 0x2ad for this tick (FUN_14040dd70; its scripts read them the tick after: 1:151)
void cues(Enemy &e)
{
    Actor &a = *e.actor;
    const float from = a.motion.id == e.cue_motion && a.motion.time >= e.cue_time ? e.cue_time : -1.0F;
    e.cue_motion = a.motion.id;
    e.cue_time = a.motion.time;
    const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.kind->set->attributes(std::size_t(a.motion.id)) : nullptr;
    std::uint32_t passed = 0;
    for (const auto &t : attr != nullptr ? attr->triggers : std::vector<kh2::MotionAttributes::Trigger>{}) {
        if (float(t.frame) <= from || float(t.frame) > a.motion.time)
            continue;
        if (t.id == 1 && !t.params.empty())
            cast(e, std::int16_t(t.params[0]));
        else if (t.id >= 9 && t.id <= 12)
            passed |= 1u << (t.id - 9);
        else if (const int id = trigger_sound(t); id > 0)
            sound(e, id);
    }
    a.set_u32(field::kMotionMarks, (a.u32(field::kMotionMarks) & ~0xff00u) | (passed << 8));
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

// The motion's state ranges, each a bit of + 0x18c while the motion's time lies in it (OpenKH's ANB page: range 0
// grounded at bit 2, 1 in the air 3, 3 no gravity 5, 9 jumping or landing 9, 24 no gravity keeping its speed 6, 30 24
// ...): what the scripts read -- 1:60's "off the ground" is bits 5 and 24 (FUN_1403ba9a0: + 0x18c & 0x1000020), 2:5's
// and 1:78's bit 9. A Days hit's reaction holds bit 9 too.
constexpr std::pair<int, int> kStateRanges[] = {{0, 2},   {1, 3},   {2, 4},   {3, 5},   {9, 9},   {11, 10},
                                                 {12, 11}, {13, 15}, {14, 19}, {15, 16}, {21, 18}, {24, 6},
                                                 {27, 22}, {29, 23}, {30, 24}, {31, 25}, {32, 26}, {34, 27},
                                                 {38, 28}, {39, 29}, {40, 30}, {41, 31}};
constexpr std::uint32_t kNoGravity = (1u << 5) | (1u << 6); // ranges 3 and 24
constexpr float kStep = 30.0F; // a fall from higher than this (KH2's cm) lands with the landing motion

void motion_states(Enemy &e)
{
    Actor &a = *e.actor;
    std::uint32_t on = 0, all = 0;
    for (const auto &[id, b] : kStateRanges)
        all |= 1u << b;
    // while the motion plays (a loop always does): one played to its end (held on its last frame) sets none -- a landing's
    // range 9 to its end ("65535") would hold it for ever after
    const bool loops = a.motion.id >= 0 && a.motions != nullptr && a.motions->has(a.motion.id) &&
                       a.motions->loop_from(a.motion.id) < a.motions->length(a.motion.id);
    if (const kh2::MotionAttributes *attr = a.motion.id >= 0 && (!a.motion.ended || loops)
                                                ? e.kind->set->attributes(std::size_t(a.motion.id))
                                                : nullptr)
        for (const auto &r : attr->ranges)
            for (const auto &[id, b] : kStateRanges)
                if (int(r.id) == id && a.motion.time >= float(r.start) && a.motion.time < float(r.end))
                    on |= 1u << b;
    if (e.reacting)
        on |= bit::kHeld;
    a.set_u32(field::kStatus, (a.u32(field::kStatus) & ~all) | on);
    // its ranges 16 to 19 ("AI" ranges: special movement, its combos): bits 0 to 3 of + 0x2ac (FUN_14040d810; 1:150)
    std::uint32_t ai_ranges = 0;
    if (const kh2::MotionAttributes *attr = a.motion.id >= 0 && (!a.motion.ended || loops)
                                                ? e.kind->set->attributes(std::size_t(a.motion.id))
                                                : nullptr)
        for (const auto &r : attr->ranges)
            if (r.id >= 16 && r.id <= 19 && a.motion.time >= float(r.start) && a.motion.time < float(r.end))
                ai_ranges |= 1u << (r.id - 16);
    a.set_u32(field::kMotionMarks, (a.u32(field::kMotionMarks) & ~0xffu) | ai_ranges);
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
    if (!e.sent_state.empty() && (e.sent_after -= 1.0F) <= 0.0F) {
        a.states().enter(a, e.sent_state);
        e.sent_state.clear();
    }
    if (!e.paused)
        a.tick();
    Vec3 travel = a.step(1.0F);
    cues(e);
    event_tick(e);
    motion_states(e);
    const bool no_gravity = (a.u32(field::kStatus) & kNoGravity) != 0;
    if (a.jump.on) {
        if (a.jump.up > 0.0F && e.floor_known && place[1] + travel[1] >= e.floor) {
            travel[1] = e.floor - place[1]; // on it, not through it
            a.land();
        }
    } else if (!no_gravity && e.floor_known && e.floor - place[1] > 0.01F * kKh2PerDays) {
        if (e.fall == 0.0F)
            e.fall_from = e.floor - place[1];
        e.fall += kGravity;
        travel[1] += std::min(e.fall, e.floor - place[1]);
        a.set_bits(field::kFlags, bit::kInAir, true);
    } else if (e.fall > 0.0F) {
        // on the ground again: a fall from higher than a step (Cerberus's leap, its motion's root up; a ledge) lands
        // as KH2's falling class does, with its landing motion (FUN_1403cb940: + 0x11c, 5 by default); a step's
        // gap (Days's ground found a few centimetres off) does not
        if (e.fall_from > kStep)
            a.touch_down();
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

// An element's place in its model's effects' space (KH2's axes, y down, cm; the model's root at 0, turned as it is):
// its size, its turn (x, then y, then z), its place, then the bone it follows (`pose`'s, KH2's model space y up)
kh2::EffectMatrix element_world(const std::vector<kh2::Mat4> &pose, const Element &el)
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
    if (el.bone >= 0 && el.bone < kNoBone && std::size_t(el.bone) < pose.size()) {
        std::array<float, 16> bone;
        std::copy(pose[std::size_t(el.bone)].begin(), pose[std::size_t(el.bone)].end(), bone.begin());
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

// An element a held player's motion cast, in its enemy's effects' space: on KH2's player's bone (P_EX100's skeleton) in
// the pose his motion has now, his model placed and turned where he stands from the enemy (both in KH2's world, y
// down; a model's y up, its turn Actor's). False without KH2's player's skeleton.
// A held player's pose now: his motion's (P_EX100's skeleton), empty without it
std::vector<kh2::Mat4> held_pose(const Held &h)
{
    const kh2::MotionSkeleton *skeleton = player_skeleton();
    if (skeleton == nullptr || h.body == nullptr || !h.body->motion)
        return {};
    static std::unique_ptr<kh2::MotionPlayer> player;
    if (!player)
        player = std::make_unique<kh2::MotionPlayer>(*skeleton);
    return pose_at(*player, *h.body->motion, h.time);
}

bool held_world(const Enemy &e, const Held &h, const Element &el, kh2::EffectMatrix &out)
{
    const std::vector<kh2::Mat4> pose = held_pose(h);
    if (pose.empty())
        return false;
    const kh2::EffectMatrix own = element_world(pose, el);
    // his effects' axes (his model's, y down) to the enemy's, a point at a time: the map is affine
    const Vec3 at = e.actor->place();
    const float yaw = e.actor->yaw();
    const auto to_enemy = [&](const Vec3 &r) {
        const Vec3 t = turned({r[0], 0.0F, r[2]}, h.yaw);
        const Vec3 d = {h.place[0] + t[0] - at[0], h.place[1] + r[1] - at[1], h.place[2] + t[2] - at[2]};
        const Vec3 m = turned({d[0], 0.0F, d[2]}, -yaw);
        return Vec3{m[0], d[1], m[2]};
    };
    float map[16] = {};
    const Vec3 origin = to_enemy({0.0F, 0.0F, 0.0F});
    for (int r = 0; r < 3; ++r) {
        Vec3 unit{};
        unit[std::size_t(r)] = 1.0F;
        const Vec3 v = to_enemy(unit);
        for (int c = 0; c < 3; ++c)
            map[r * 4 + c] = v[std::size_t(c)] - origin[std::size_t(c)];
    }
    for (int c = 0; c < 3; ++c)
        map[12 + c] = origin[std::size_t(c)];
    map[15] = 1.0F;
    row_matrix_mul(own.data(), map, out.data());
    return true;
}

// Where KH2 puts the mark of a reaction command a held player is offered (Cerberus's: on Sora in its jaws), in Days's
// world: his body's middle, his body spheres' (P_EX100's shapes 0x302) on his bones in his pose now, placed as his
// root's travel is (Held's place and yaw); his feet without KH2's player's skeleton or spheres
void held_middle(const Held &h, float out[3])
{
    to_days(h.place, out);
    const std::vector<kh2::Mat4> pose = held_pose(h);
    Vec3 c{};
    int n = 0;
    for (const Shape &s : player_files().spheres) {
        if (s.bone < 0 || std::size_t(s.bone) >= pose.size())
            continue;
        const kh2::Mat4 &m = pose[std::size_t(s.bone)];
        for (int i = 0; i < 3; ++i)
            c[std::size_t(i)] += m[std::size_t(i)] * s.at[0] + m[std::size_t(4 + i)] * s.at[1] +
                                 m[std::size_t(8 + i)] * s.at[2] + m[std::size_t(12 + i)];
        ++n;
    }
    if (n == 0)
        return;
    for (float &v : c)
        v /= float(n);
    const Vec3 t = turned({c[0], 0.0F, c[2]}, h.yaw);
    out[0] += t[0] / kKh2PerDays;
    out[1] += c[1] / kKh2PerDays;
    out[2] += t[2] / kKh2PerDays;
}

// Its effects moved on by KH2's ticks since its last draw (the game's frames, 60 a second: the corridor's way), each
// at its element's place, its sound started when its time comes; the last tick's drawn at the camera now; those over
// let go. Where it is drawn gives its sounds' pan.
void draw_effects(Enemy &e, const float view[16], const float projection[16])
{
    const float across = std::sqrt(view[12] * view[12] + view[14] * view[14]);
    e.pan = across > 1e-6F && std::isfinite(across) ? std::clamp(view[12] / across, -1.0F, 1.0F) : 0.0F;
    if (e.fx.empty() || e.kind->fx_pages.empty())
        return;
    const unsigned now = khdays_link_frame();
    const unsigned per_frame = unsigned(std::max(1L, std::lround(khdays_link_frame_seconds() * 60.0)));
    const unsigned ticks = e.fx_frame == 0 ? per_frame : now >= e.fx_frame ? (now - e.fx_frame) * per_frame : per_frame;
    e.fx_frame = now;
    float units = 1.0F;
    const kh2::EffectCamera cam = effect_camera(view, projection, units);
    kh2::EffectCamera placed = cam;
    for (Enemy::Running &r : e.fx) {
        if (r.player != 0) {
            if (const auto h = held.find(r.player);
                h != held.end() && h->second.enemy == e.days && held_world(e, h->second, *r.element, r.last))
                r.placed = true;
            if (!r.placed) { // no KH2 player's skeleton: at the enemy's root
                Element at = *r.element;
                at.bone = kNoBone;
                r.last = element_world(e.pose, at);
                r.placed = true;
            }
            placed.world = r.last;
        } else {
            placed.world = element_world(e.pose, *r.element);
        }
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
        if (r.element->effect < e.kind->fx_pages.size())
            khdays::kh2_fx::submit(r.draws, units, *e.kind->fx_pages[r.element->effect]);
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
    held.clear();
    waiting_threads.clear();
    player_of_node.clear();
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
    the_field().health(*e->actor);
    Vec3 place = to_kh2(s.place);
    if (s.above != -1.0F) { // (-1: no ground under it; below 0: sunk into it)
        e->floor = place[1] + s.above * kKh2PerDays;
        e->floor_known = true;
    }
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
        if (e->carrier != 0)
            if (const auto h = held.find(e->carrier); h != held.end()) { // on the player carrying it
                place = h->second.place;
                e->actor->set_place(place);
                if ((e->carry_flags & 2) == 0)
                    e->actor->set_yaw(h->second.yaw);
            }
        pose(*e);
        attack(*e, s);
    }
    zone_offer(*e);
    take_command(*e);
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
    if (!e->blow_landing)
        e->days_damage = last_days_damage = hp_before - s.hp; // a Days hit's (a blow's own is not one)
    e->blow_landing = false;
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
    if (!days_hit) {
        days_hit = std::make_unique<Object>(mod().memory, "a Days hit");
        days_hit->set_u32(0x24, 2u << 16); // + 0x26: a small reaction
    }
    const int motion = front ? 10 : 12;
    // KH2's hit (FUN_140411060): the reaction applied -- its motion asked, the actor held while it plays
    // (FUN_1403d4650; a set without the motion, a boss's, plays nothing and goes on), the queue cleared -- and the
    // reaction state the attack names entered by name: a small hit's "revenge" (every enemy script has it: its
    // reaction, then its revenge mode's attacks; Cerberus's leaves its opening leap so). A kind with the motion and
    // without "revenge" waits on the motion, its script paused.
    const bool moves = a.motions->has(motion);
    a.states().clear(a);
    if (moves) {
        a.jump = {};
        a.play(motion, 2.0F, 0.0F);
        a.set_bits(field::kStatus, bit::kHeld, true);
        e->reacting = true;
    }
    StateEntry reaction;
    const bool revenge = a.states().find(a, "revenge", reaction) && a.states().enter(a, "revenge");
    e->paused = moves && !revenge;
    // then trigger 6, the hit told to the script (FUN_140410d60, the hit its argument: a small one): KH2's
    // enemies turn to their revenge mode on it when the player struck them (1:309 of 2:88's), which begins once the
    // reaction is over (the state waits while the actor is held)
    const ai::Value packet = ai::Value::of_address(days_hit->address());
    a.event(6, {&packet, 1});
    if (tracing())
        khdays_log("kh2 enemies: %08x hit (%d to %d), %s%s", days, hp_before, s.hp,
                   moves ? (motion == 10 ? "motion 10" : "motion 12") : "no reaction motion of its own",
                   e->paused ? ", its script waiting" : "");
}

// The camera of the event sequence playing, while it is this game's player's (runtime/camera/kh2_camera_hooks.c): its
// eye and look-at in Days's world, half its vertical field of view (0: the camera's own kept); 0 when none shows
extern "C" int khdays_kh2_enemies_event_camera(float eye[3], float at[3], float *fov_half)
{
    if (!playing || playing->player == 0 || playing->player != local_player)
        return 0;
    float within = 0.0F;
    const kh2::EventCamera *cam = kh2::event_camera_at(*playing->event, float(playing->ticks) / 2.0F, within);
    if (cam == nullptr)
        return 0;
    kh2::EventView view;
    kh2::event_view(*cam, within, view);
    event_point(*playing, view.eye, eye);
    event_point(*playing, view.at, at);
    *fov_half = view.fov * 0.5F;
    if (playing->shake) {
        // the eye moved in the camera's own axes: x and z turned with its yaw, y up (FUN_140528920)
        const float dx = at[0] - eye[0], dz = at[2] - eye[2], flat = std::sqrt(dx * dx + dz * dz);
        const float ax = flat > 1e-6F ? dx / flat : 0.0F, az = flat > 1e-6F ? dz / flat : 1.0F;
        const auto &o = playing->shake->offset;
        eye[0] += (o[0] * az + o[2] * ax) / kKh2PerDays;
        eye[1] += o[1] / kKh2PerDays;
        eye[2] += (-o[0] * ax + o[2] * az) / kKh2PerDays;
    }
    return 1;
}

// The screen's trail a sequence playing for this game's player has on (runtime/display/frame_package.h, `trail`): its
// alpha, its quad's width and height over the view's, its turn in radians; 0 when none
extern "C" int khdays_kh2_enemies_screen_trail(float out[4])
{
    if (!playing || !playing->trail || playing->player == 0 || playing->player != local_player)
        return 0;
    out[0] = playing->trail->alpha;
    out[1] = playing->trail->size[0];
    out[2] = playing->trail->size[1];
    out[3] = playing->trail->angle;
    return 1;
}

// A Days player's actor's tick begins (Ov022_ActorUpdate, prepare.py): whether a KH2 enemy holds it (its control
// stands still), put where the enemy's script and motion have it; this game's own player told apart
extern "C" int khdays_kh2_enemies_holding(void *actor)
{
    int local = 0;
    unsigned node = 0;
    const unsigned object = khdays_kh2_shell_player_object(unsigned(KHDAYS_REF(actor)), &local, &node);
    if (object == 0)
        return 0;
    if (local)
        local_player = object;
    if (node != 0)
        player_of_node[node] = object;
    if (tracing()) { // its HP as it changes (the KH2 enemies' hits on it)
        static std::map<unsigned, int> seen;
        const int hp = khdays_kh2_shell_player_hp(unsigned(KHDAYS_REF(actor)));
        const auto was = seen.find(object);
        if (was == seen.end() || was->second != hp)
            khdays_log("kh2 enemies: player %08x's HP %d", object, hp);
        seen[object] = hp;
    }
    const auto h = held.find(object);
    if (h == held.end())
        return 0;
    float at[3];
    to_days(h->second.place, at);
    khdays_kh2_shell_put_player(unsigned(KHDAYS_REF(actor)), at, h->second.yaw);
    return 1;
}

// The reaction command offered to this game's own player now (its name's KH2 text number in `text`), 0 none
extern "C" int khdays_kh2_enemies_offered(int *text)
{
    int id = -1;
    if (const auto h = held.find(local_player); h != held.end())
        id = h->second.offered;
    else if (const Enemy *z = zone_offerer())
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

// KH2's text as its menus' international encoding has it (msg/<language>/sys.bar; our reading of the files): 01 a
// space, 2e-47 A-Z, 9a-b3 a-z, 90-99 the digits, b7-cb and d0-e4 the accented small and capital letters (a grave,
// acute, circumflex, diaeresis ... u diaeresis, each set in that order), b5 oe, b6 sharp s, e5 and e6 the opening
// exclamation and question marks, punctuation; the codes below 0x20 (OpenKH's MSG page: a colour, a scale, a pause,
// each with its bytes) and characters of other tables left out
static std::u16string kh2_text(const std::uint8_t *b, std::size_t n)
{
    static const char16_t small[] = {0x00e0, 0x00e1, 0x00e2, 0x00e4, 0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f6, 0x00f9, 0x00fa, 0x00fb, 0x00fc};
    static const char16_t capital[] = {0x00c0, 0x00c1, 0x00c2, 0x00c4, 0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d6, 0x00d9, 0x00da, 0x00db, 0x00dc};
    // each control code's length with its own byte (0 none of its own: 1)
    static const std::uint8_t length[0x20] = {1, 1, 1, 1, 1, 7, 1, 5, 4, 2, 2, 2, 2, 1, 2, 6,
                                              1, 5, 3, 5, 3, 3, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2};
    std::u16string out;
    for (std::size_t i = 0; i < n && b[i] != 0;) {
        const std::uint8_t c = b[i];
        if (c < 0x20) {
            if (c == 0x01)
                out += u' ';
            else if (c == 0x02)
                out += u' ';
            i += length[c];
            continue;
        }
        ++i;
        if (c >= 0x2e && c <= 0x47)
            out += char16_t(u'A' + (c - 0x2e));
        else if (c >= 0x9a && c <= 0xb3)
            out += char16_t(u'a' + (c - 0x9a));
        else if (c >= 0x90 && c <= 0x99)
            out += char16_t(u'0' + (c - 0x90));
        else if (c >= 0xb7 && c <= 0xcb)
            out += small[c - 0xb7];
        else if (c >= 0xd0 && c <= 0xe4)
            out += capital[c - 0xd0];
        else
            switch (c) {
            case 0x48: out += u'!'; break;
            case 0x49: out += u'?'; break;
            case 0x4f: out += u'.'; break;
            case 0x50: out += u','; break;
            case 0x52: out += u':'; break;
            case 0x5a: out += u'('; break;
            case 0x5b: out += u')'; break;
            case 0x66: out += u'-'; break;
            case 0x2b: out += u'+'; break;
            case 0xb5: out += char16_t(0x0153); break;
            case 0xb6: out += char16_t(0x00df); break;
            case 0xcc: out += char16_t(0x00ba); break;
            case 0xe5: out += char16_t(0x00a1); break;
            case 0xe6: out += char16_t(0x00bf); break;
            case 0xec:
            case 0xee: out += u'\''; break;
            case 0xed: out += u'"'; break;
            default: break;
            }
    }
    return out;
}

// A command's name (its text's number, 03system.bin's; its top bit cleared it is sys.bar's message) in the game's
// language, from the mod's kh2/msg/<language>/sys.bar; empty when there is none
static std::u16string command_name(int text)
{
    static const char *const folders[] = {"us", "us", "fr", "gr", "it", "sp"}; // the game's 1 English .. 5 Spanish
    static std::map<int, std::vector<std::uint8_t>> read; // each language's sys.bar
    const int language = khdays_link_language();
    const int l = language >= 1 && language <= 5 ? language : 1;
    auto it = read.find(l);
    if (it == read.end())
        it = read.emplace(l, read_file(fs::path(std::u8string(mod().folder.begin(), mod().folder.end())) / "kh2" / "msg" / folders[l] / "sys.bar")).first;
    const std::vector<std::uint8_t> &bar = it->second;
    const std::uint32_t id = std::uint32_t(text) & 0x7fffu;
    try {
        for (const kh2::BarEntry &e : kh2::bar_entries(bar.data(), bar.size())) {
            if (e.name != "sys" || e.size < 8)
                continue;
            std::uint32_t count = 0;
            std::memcpy(&count, bar.data() + e.offset + 4, 4);
            for (std::uint32_t i = 0; i < count && 8 + 8 * (i + 1) <= e.size; ++i) {
                std::uint32_t mid = 0, at = 0;
                std::memcpy(&mid, bar.data() + e.offset + 8 + 8 * i, 4);
                std::memcpy(&at, bar.data() + e.offset + 12 + 8 * i, 4);
                if (mid == id && at < e.size)
                    return kh2_text(bar.data() + e.offset + at, e.size - at);
            }
        }
    } catch (const std::exception &) {
    }
    return {};
}

namespace {
int put_name(int text, unsigned short *out, int room)
{
    if (text == 0 || out == nullptr || room <= 0)
        return 0;
    const std::u16string name = command_name(text);
    const int n = std::min(int(name.size()), room);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<unsigned short>(name[std::size_t(i)]);
    return n;
}
} // namespace

extern "C" int khdays_kh2_enemies_offered_name(unsigned short *out, int room)
{
    int text = 0;
    return khdays_kh2_enemies_offered(&text) != 0 ? put_name(text, out, room) : 0;
}

// The name of the last reaction command this game's own player took and its id in `id` (0 none)
extern "C" int khdays_kh2_enemies_taken_name(unsigned short *out, int room, int *id)
{
    const Command *c = local_taken >= 0 ? command(local_taken) : nullptr;
    *id = c != nullptr ? local_taken : 0;
    return c != nullptr ? put_name(c->text, out, room) : 0;
}

// Ov107_ProcessObjectTick (prepare.py's join): a KH2 player's blow on the enemy to land now as one Days hit (its
// damage in `amount`), the hit's own way through Days's hit and the shell's
extern "C" int khdays_kh2_enemies_blow(void *actor, int *amount)
{
    Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    if (e == nullptr || e->blow <= 0 || e->dying)
        return 0;
    *amount = e->blow;
    e->blow = 0;
    e->blow_landing = true;
    return 1;
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

namespace {
// Whether a model is a player character's (ba/ch/<code>/, the file it came from): never a shell's, though a player an
// enemy holds stands at its place (1:18, the enemy carried by the player: Cerberus's bite)
bool character_model(const void *resource)
{
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    return resource != nullptr && khdays_origin_of_ram(unsigned(KHDAYS_REF(resource)), &file, &offset, &run) &&
           file != nullptr && std::strncmp(file, "ba/ch/", 6) == 0;
}
} // namespace

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
    if (!own_model && character_model(resource))
        return 0;
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
    // KH2's reaction command mark, projected as its model (its axes, then the draw's view and projection, row
    // vectors): on this game's own player it holds, or at its aim point (its 0x307 shape) when its zone offers him one
    float at[3];
    bool marked = false;
    if (const auto h = held.find(local_player); h != held.end() && h->second.enemy == e->days) {
        held_middle(h->second, at);
        marked = true;
    } else if (zone_offerer() == e) {
        for (const Shape &sh : e->kind->shapes)
            if (sh.kind == 0x307) {
                days_point(*e, s, sh.bone, sh.at, at);
                marked = true;
                break;
            }
    }
    if (marked) {
        const float dx = at[0] - s.place[0], dy = at[1] - s.place[1], dz = at[2] - s.place[2];
        const float q[4] = {c * dx - sn * dz, dy, sn * dx + c * dz, 1.0F};
        float v[4] = {}, spot[4] = {};
        for (int col = 0; col < 4; ++col)
            for (int k = 0; k < 4; ++k)
                v[col] += q[k] * view[k * 4 + col];
        for (int col = 0; col < 4; ++col)
            for (int k = 0; k < 4; ++k)
                spot[col] += v[k] * pure[k * 4 + col];
        khdays_ui_reaction_mark_draw(spot);
    }
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

bool khdays::kh2_enemies::held_motion(unsigned node, HeldMotion &out)
{
    const auto p = player_of_node.find(node);
    if (p == player_of_node.end())
        return false;
    const auto h = held.find(p->second);
    if (h == held.end() || h->second.body == nullptr || !h->second.body->motion)
        return false;
    out.motion = h->second.body->motion.get();
    out.weapon = h->second.weapon != nullptr ? h->second.weapon->motion.get() : nullptr;
    out.ticks = h->second.time;
    out.skeleton = player_skeleton();
    return out.skeleton != nullptr;
}
