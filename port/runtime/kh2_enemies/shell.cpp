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
constexpr float kPi = 3.14159265358979F;
constexpr float kGravity = kKh2Pull; // a fall's pull a tick a tick: class 1's past its top (FUN_1403cb360; actor.h)
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

// `default_groups`, when given: the header's + 4, the groups on unless the motion or the script says (FUN_1403ba550's
// last test; 1 in every one read: the body's group 0)
std::vector<Shape> read_shapes(const std::vector<std::uint8_t> &mdlx, std::uint32_t *default_groups = nullptr)
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
        if (default_groups != nullptr)
            *default_groups = u32(at + 4);
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
    // its two points (flags 0x10000; KH2's ryj::PAX::MANAGER keeps them and get_obj_data sets them each tick, the
    // swing trails' base and tip): + 0x40 and + 0x42 their bones, + 0x44 and + 0x48 how far along each bone's x in its
    // lengths (YS::OBJ::get_bone_pos: the bone's length is its skeleton's scale w); + 0x4c set, the place (+ 0x1c) and
    // the turn (+ 0x28) are offsets added on those bones
    bool points = false, point_offsets = false;
    std::array<int, 2> point_bone{};
    std::array<float, 2> point_along{};
};

// An enemy kind's files, read once: its script (the .mdlx's AI, BAR entry 3), its motions and skeleton, its shapes.
struct KindFiles final {
    std::string name, mdlx;
    std::shared_ptr<const ai::Script> script;
    std::shared_ptr<const kh2::MotionSet> set;
    std::shared_ptr<const kh2::MotionSkeleton> skeleton;
    std::vector<Shape> shapes;
    std::uint32_t default_groups = 1; // the groups on unless the motion or the script says (the shapes' header's + 4)
    // whether its script plays the player's motions itself (a 1:29 in its walked code, the mod's list says: the Magnum
    // Loader's Quick Blade, Cerberus's commands): its zones' commands offered; those KH2's player's own code plays (the
    // Soldier's Cyclone, the Dusk's Reversal) are not yet
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
        if ((el.flags & 0x10000u) != 0) {
            el.points = true;
            el.point_bone = {int(u32(r + 0x40) & 0xffffu), int(u32(r + 0x40) >> 16)};
            el.point_along = {f32(r + 0x44), f32(r + 0x48)};
            el.point_offsets = (u32(r + 0x4c) & 0xffu) != 0;
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

// The kind's sounds (its motions' triggers 8 -- those it makes the player play too, his body's and his weapon's: the
// keyblade's bank 500 -- and its elements'), each the mod's kh2/se/<id>.wav, decoded once
void read_sounds(KindFiles &k, const fs::path &folder)
{
    std::set<int> ids;
    const auto add = [&ids](const kh2::MotionAttributes *attr) {
        if (attr != nullptr)
            for (const auto &t : attr->triggers)
                if (const int id = trigger_sound(t); id > 0)
                    ids.insert(id);
    };
    for (std::size_t slot = 0; slot < k.set->slots(); ++slot) {
        add(k.set->attributes(slot));
        for (const auto &v : k.set->variants(slot))
            add(v.attributes.get());
    }
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
            // (none for an object with no motion set of its own, the Thresholder's Possessor: its entry names none)
            const auto mset = read_file(obj / (kind->name + ".mset"));
            const auto scripts = ai::scripts_in(mdlx.data(), mdlx.size());
            if (scripts.empty())
                throw std::runtime_error("its .mdlx holds no script");
            k = std::make_unique<KindFiles>();
            k->name = kind->name;
            const std::u8string p8 = (obj / (kind->name + ".mdlx")).u8string();
            k->mdlx.assign(p8.begin(), p8.end());
            k->script = std::make_shared<const ai::Script>(scripts.front().second);
            k->set = mset.empty() ? std::make_shared<const kh2::MotionSet>()
                                  : std::make_shared<const kh2::MotionSet>(mset.data(), mset.size());
            // (no bones for an object with no model of its own: the Possessor, drawn by its effects alone)
            const auto bar = kh2::bar_entries(mdlx.data(), mdlx.size());
            const bool model = std::any_of(bar.begin(), bar.end(), [](const kh2::BarEntry &e) { return e.type == 4; });
            k->skeleton = std::make_shared<const kh2::MotionSkeleton>(
                model ? kh2::read_motion_skeleton(mdlx.data(), mdlx.size()) : kh2::MotionSkeleton{});
            k->shapes = read_shapes(mdlx, &k->default_groups);
            k->plays_player = kind->plays_player;
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
    std::unique_ptr<kh2::MotionPlayer> player;
    std::vector<kh2::Mat4> pose; // this frame's, its bones' model-space matrices (KH2's cm)
    // the pose shown when its motion started (a.motion.started then): a blend goes from it (MOTION::exec's Context1)
    std::vector<kh2::Mat4> blend_from;
    unsigned posed_started = 0;
    bool reacting = false;       // a Days hit's reaction playing (held: + 0x18c bit 9)
    bool paused = false;         // its script waits while it plays (a kind with no reaction state of its own)
    bool dying = false, released = false;
    bool down = false; // released and no longer standing: made again if its Days enemy is set going once more
    bool dead_marked = false; // counted as defeated by its own script (1:328): its Days defeat without "dead"
    // the place (KH2's) the shell gave its actor last: another script putting it elsewhere (1:148 on its handle, the
    // cubes' on the Sorcerer they made) is seen by it
    Vec3 given{};
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
        // on his weapon (the keyblade in his hand: its bones are its element's), and the files its element is of (none:
        // the enemy's kind's; KH2's keyblade's, W_EX010.a.us, for its swing's trail, YS::WEAPON::locus)
        bool on_weapon = false;
        const KindFiles *files = nullptr;
        std::uint32_t handle = 0; // what the scripts know it by (1:149, 0:44)
        kh2::EffectMatrix last{}; // that one's place when last drawn, kept once he is let go
        bool placed = false;
        // one its script placed in the world (1:22, 0:38: a shot), at a place of KH2's world, turned (0:41) when set
        bool in_world = false, turned = false;
        Vec3 at{}, turn{};
        float end_age = -1.0F; // ended once this old (0:52), -1 when not
    };
    std::vector<Running> fx;
    float pan = 0.0F; // where it was last drawn, left (-1) to right (1) of the camera: its sounds' pan
    unsigned fx_frame = 0;
    std::uint32_t fx_seed = 1;
    // where it was last drawn (Days's place) and the projection then, for its end once its Days enemy is gone
    float place[3] = {};
    float home[3] = {}; // where its wave placed it (Days's place): it comes back there after KH2's "leave"
    bool placed = false; // its place given to Days (e.place) at least once
    float fall_from = 0.0F; // how high above the ground its fall began (KH2's cm)
    bool aloft = false;     // off the ground as KH2's ground class sees it: a jump, a fall past a step, a motion's air
    // a state's name shouted to it with a delay (1:90; + 0x4c0 .. + 0x4d0, the last one shouted), heard once
    // `sent_after` ticks have gone: that state's shout routine run (StateMachine::shout)
    std::string sent_state;
    std::uint32_t sent_from = 0; // the shouter's handle
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
    std::uint32_t command_record = 0; // the target record a command taken is sent with (trigger 11's third)
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
std::set<unsigned> appeared;                        // those a script made (1:0): their kind theirs till they go
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
    Vec3 steer{};                 // a script's steering of him, each tick till another (1:111), KH2's cm
    bool own_queued = false;      // his own motion asked for (1:13): taken once this one ends or blends to idle
    // the blend 1:29 asked for (its fourth value: YS::MOTION::start_motion's blend, above 2 a blend), counted as an
    // enemy's motion's is (Playing, actor.h): its length (0 none or over), its count from 2, the pose before's share
    // left; `fresh` the motion started this tick (it stands at its first frame till the next)
    float blend = 0.0F, blend_count = 0.0F, blend_keep = 0.0F;
    bool fresh = false;
    unsigned started = 0;      // which motion this is (a new one each 1:29): his model blends from the pose before
    std::uint32_t trail = 0;   // his weapon's swing's trail running (its handle), 0 none
};
std::map<unsigned, Held> held; // by the Days player's object
// each held player's weapon as his KH2 model drew it (held_weapon_drawn: its bones, KH2's cm in his motion's space, y
// up) and the frame it did, by his object
struct DrawnWeapon final {
    std::vector<kh2::Mat4> bones;
    unsigned frame = 0;
};
std::map<unsigned, DrawnWeapon> drawn_weapons;
// the states' names shouted this frame (1:90; FUN_1403cacd0's list at DAT_142a23d50), each with its shouter's handle and
// its delay: sent to every enemy at the next frame's first tick (FUN_1403c9d40, once a frame)
struct Shout final {
    std::string name;
    std::uint32_t from = 0;
    float delay = 0.0F;
};
std::vector<Shout> shouts;
unsigned shouts_frame = ~0u;
// a thread a script started on a player before taking it in hand (Cerberus's 1:112 comes before its 1:29), by object
std::map<unsigned, std::pair<ai::Machine *, std::uint32_t>> waiting_threads;
unsigned local_player = 0;     // this game's own player's object (khdays_kh2_enemies_holding sees it)
unsigned local_takes = 0;      // the reaction commands this game's own player took (khdays_kh2_enemies_taken)
// KH2's attacks of their own (2:10, YS::ATTACK_MAGIC), by their address in the scripts' memory: the enemy whose
// script made one (its hits go through its shell), whether KH2's player owns it (its hits his blows), its atkp entry,
// its size and growth, its place and the one before (swept between), the effect it follows, the players it hit
struct AttackObject final {
    unsigned enemy = 0;
    bool players = false;
    int atkp = 0, group = -1, sub = 0, team = -1;
    float radius = 0.0F, height = 0.0F;
    float grow_ticks = 0.0F, grow_for = 0.0F, grow_radius = 0.0F, grow_height = 0.0F;
    Vec3 at{}, before{};
    bool placed = false;
    std::uint32_t effect = 0;
    std::set<unsigned> struck;
    bool ended = false;    // ended by its script (2:13): let go
    bool expired = false;  // grown, or its effect gone: no more hits, its state still asked (2:14, 2:29)
    bool landed = false;   // it hit (2:39)
    std::uint32_t hit = 0; // the object it hit last (2:44)
    std::uint32_t on_hit = 0, on_hit_value = 0; // its script's routine run when it hits (2:38), 0 none
};
std::map<std::uint32_t, AttackObject> attacks;

// a reaction command an enemy's script offered this game's own player (1:165: renewed each tick it stands) and the
// VBlank it last did
struct ScriptOffer final {
    unsigned enemy = 0;
    int command = -1;
    unsigned frame = 0;
};
ScriptOffer script_offer;
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

std::uint32_t cast(Enemy &e, int number, unsigned player = 0, bool on_weapon = false,
                   const KindFiles *files = nullptr);
kh2::EffectMatrix element_world(const std::vector<kh2::Mat4> &pose, const Element &el);
void sound(const Enemy &e, int id);

void pose(Enemy &e);
std::set<int> groups_on(const Enemy &e);
bool effect_place(const Enemy &e, std::uint32_t handle, Vec3 &out);
constexpr float kPlayerMiddle = 0.725F; // a player's middle over his feet (Days's units): half kPlayerHeight
void strike(AttackObject &o, Object &target);
void days_point(const Enemy &e, const KhdaysKh2Shell &s, int bone, const Vec3 &at, float out[3]);
Enemy *start(unsigned days, KindFiles &k, const KhdaysKh2Shell &s);
std::string entry_name(std::uint32_t number);

class Field final : public World {
public:
    // 1:0: an object of the mod's kinds made by its entry's number, as a Days enemy of its maker's class made where
    // its maker stands (khdays_enemies_make: Days's own making -- hit, locked on, defeated, counted as Days's are),
    // the kind's script set going on it at once; its maker's script then puts it where it wants it (the cubes, the
    // Sorcerer where they are: 1:148, 1:79). An entry the mod has no kind for, or one whose script the port does not
    // answer, makes nothing (0), as KH2 makes nothing of an entry it cannot load
    std::uint32_t appear(Actor &by, std::uint32_t entry) override
    {
        const auto maker = by_actor.find(&by);
        const std::string name = entry_name(entry);
        KindFiles *k = name.empty() || maker == by_actor.end() ? nullptr : files_of(name);
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
        Enemy *e = start(days, *k, made);
        if (tracing())
            khdays_log("kh2 enemies: %08x (%s) makes %08x, a %s (entry %u)", maker->second->days, by.kind().c_str(),
                       days, name.c_str(), unsigned(entry));
        return e->actor->address();
    }

    // 1:328: its Days enemy defeated as a player's hit of all its health defeats it (its prizes, its wave's count;
    // seat 0's player its author), its script left to end it as it does (no "dead" entered: the cubes fade out)
    void dead_mark(Actor &a) override
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

    void set_health(Actor &a, int hp) override
    {
        if (const auto it = by_actor.find(&a); it != by_actor.end() && !it->second->dying)
            khdays_kh2_shell_set_hp(it->second->days, hp);
    }

    // 1:98, 1:99: the ground Days last found under it (its place's height when none yet)
    float ground(Actor &a) override
    {
        const auto it = by_actor.find(&a);
        return it != by_actor.end() && it->second->floor_known ? it->second->floor : a.place()[1];
    }

    // 1:90: a state every enemy of the field enters, at once or each after a time at random below `delay` (KH2's
    // chance, DAT_140783ca0 x 0x10dcd)
    void broadcast(Actor &a, const std::string &name, float delay) override
    {
        shouts.push_back({name, a.states().handle(), delay});
    }

    // 1:87: the actor's element `number` started (FUN_1403b6670 -> FUN_1402c6b30 on its effects); the handle the
    // script gives back to 0:79 none
    std::uint32_t effect(Actor &a, int number, int, int) override
    {
        const auto it = by_actor.find(&a);
        return it != by_actor.end() ? cast(*it->second, number) : 0u; // its handle: 2:61 follows it
    }

    std::uint32_t effect_on(Actor &a, int number, int, int, Object *target) override
    {
        const auto it = by_actor.find(&a);
        if (it == by_actor.end())
            return 0;
        const unsigned on = target != nullptr ? days_object(*target) : 0; // a player: on his bones while it holds him
        return cast(*it->second, number, on);
    }
    void attack_made(std::uint32_t at, Actor &by, Object *owner, int atkp, int group, int sub) override
    {
        const auto it = by_actor.find(&by);
        if (it == by_actor.end())
            return;
        AttackObject o;
        o.enemy = it->second->days;
        o.players = owner != nullptr && days_object(*owner) != 0;
        o.atkp = atkp;
        o.group = group;
        o.sub = sub;
        attacks[at] = o;
        if (tracing())
            khdays_log("kh2 enemies: %08x makes an attack (atkp %d, group %d)%s", o.enemy, atkp, group,
                       o.players ? " of KH2's player" : "");
    }
    void attack_size(std::uint32_t at, float radius, float height) override
    {
        if (const auto o = attacks.find(at); o != attacks.end())
            o->second.radius = radius, o->second.height = height;
    }
    void attack_place(std::uint32_t at, const Vec3 &place) override
    {
        if (const auto o = attacks.find(at); o != attacks.end()) {
            o->second.before = o->second.placed ? o->second.at : place;
            o->second.at = place;
            o->second.placed = true;
        }
    }
    void attack_grow(std::uint32_t at, float ticks, float radius, float height) override
    {
        if (const auto o = attacks.find(at); o != attacks.end()) {
            o->second.grow_for = ticks;
            o->second.grow_ticks = 0.0F;
            o->second.grow_radius = radius > 0.0F ? radius : o->second.radius;
            o->second.grow_height = height > 0.0F ? height : o->second.height;
        }
    }
    void attack_follow(std::uint32_t at, std::uint32_t effect) override
    {
        if (const auto o = attacks.find(at); o != attacks.end())
            o->second.effect = effect;
    }
    void attack_team(std::uint32_t at, int team) override
    {
        if (const auto o = attacks.find(at); o != attacks.end())
            o->second.team = team;
    }
    void attack_strike(std::uint32_t at, Object *target) override
    {
        const auto o = attacks.find(at);
        if (o == attacks.end() || target == nullptr)
            return;
        strike(o->second, *target);
    }
    void attack_end(std::uint32_t at) override
    {
        if (const auto o = attacks.find(at); o != attacks.end())
            o->second.ended = true;
    }
    bool attack_finished(std::uint32_t at) override
    {
        const auto o = attacks.find(at);
        return o == attacks.end() || o->second.ended || o->second.expired || o->second.landed;
    }
    bool attack_expired(std::uint32_t at) override
    {
        const auto o = attacks.find(at);
        return o == attacks.end() || o->second.expired;
    }
    bool attack_landed(std::uint32_t at) override
    {
        const auto o = attacks.find(at);
        return o != attacks.end() && o->second.landed;
    }
    std::uint32_t attack_struck(std::uint32_t at) override
    {
        const auto o = attacks.find(at);
        return o != attacks.end() ? o->second.hit : 0u;
    }
    bool attack_way(std::uint32_t at, Vec3 &out) override
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
    bool attack_can_hit(std::uint32_t at, Object *target) override
    {
        const auto o = attacks.find(at);
        if (o == attacks.end() || target == nullptr)
            return false;
        const bool player = days_object(*target) != 0;
        return o->second.players ? !player && target->actor() != nullptr : player;
    }
    void attack_line(std::uint32_t at, const Vec3 &now, const Vec3 &before) override
    {
        if (const auto o = attacks.find(at); o != attacks.end()) {
            o->second.at = now;
            o->second.before = before;
            o->second.placed = true;
        }
    }

    Enemy::Running *running_effect(std::uint32_t handle, Enemy **of = nullptr)
    {
        if (handle == 0)
            return nullptr;
        for (auto &[days, e] : enemies)
            for (Enemy::Running &r : e->fx)
                if (r.handle == handle) {
                    if (of != nullptr)
                        *of = e.get();
                    return &r;
                }
        return nullptr;
    }
    std::uint32_t effect_in_world(Actor &a, int number, const Vec3 &place) override
    {
        const auto it = by_actor.find(&a);
        if (it == by_actor.end())
            return 0;
        const std::uint32_t handle = cast(*it->second, number);
        if (Enemy::Running *r = running_effect(handle))
            r->in_world = true, r->at = place;
        return handle;
    }
    bool effect_alive(std::uint32_t handle) override
    {
        return running_effect(handle) != nullptr;
    }
    bool effect_where(std::uint32_t handle, Vec3 &out) override
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
    void effect_move(std::uint32_t handle, const Vec3 &place) override
    {
        if (Enemy::Running *r = running_effect(handle))
            r->in_world = true, r->at = place;
    }
    void effect_turn(std::uint32_t handle, const Vec3 &angles) override
    {
        if (Enemy::Running *r = running_effect(handle))
            r->turned = true, r->turn = angles;
    }
    void effect_fade(std::uint32_t handle, float ticks) override
    {
        if (Enemy::Running *r = running_effect(handle))
            r->end_age = r->age + ticks;
    }
    bool ray(Actor &a, const Vec3 &from, const Vec3 &to, Vec3 &hit) override
    {
        const auto it = by_actor.find(&a);
        if (it == by_actor.end())
            return false;
        float f[3], t[3], h[3];
        to_days(from, f);
        to_days(to, t);
        if (!khdays_kh2_shell_ray(it->second->days, f, t, h))
            return false;
        hit = to_kh2(h);
        return true;
    }
    bool record_distance(std::uint32_t record, const Vec3 &point, float &out) override
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
    void attack_on_hit(std::uint32_t at, Actor &, std::uint32_t routine, std::uint32_t value) override
    {
        if (const auto o = attacks.find(at); o != attacks.end())
            o->second.on_hit = routine, o->second.on_hit_value = value;
    }
    bool aim_point(Object &o, Vec3 &out) override
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

    void stop_effect(std::uint32_t handle) override
    {
        for (auto &[days, e] : enemies)
            for (auto r = e->fx.begin(); r != e->fx.end(); ++r)
                if (r->handle == handle) {
                    e->fx.erase(r);
                    return;
                }
    }
    void offer(Actor &by, int command, Object *source) override
    {
        Actor *from = source != nullptr && source->actor() != nullptr ? source->actor() : &by;
        const auto it = by_actor.find(from);
        if (it == by_actor.end() || command < 0)
            return;
        script_offer = {it->second->days, command, khdays_link_frame()};
    }
    std::vector<Actor *> actors() override
    {
        std::vector<Actor *> out;
        for (auto &[days, e] : enemies)
            if (!e->dying)
                out.push_back(e->actor.get());
        return out;
    }
    std::vector<ShapeAt> shapes(Actor &a, int kind) override
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
                out.push_back({sh.group, to_kh2(d)});
            }
        return out;
    }

    // its model drawn by Days in the frame before or this one (whether Days leaves out what its camera does not see
    // is not known: a drawn enemy counts as seen)
    bool takes_off_later(Actor &a) override
    {
        const auto it = by_actor.find(&a);
        const kh2::MotionAttributes *attr =
            it != by_actor.end() && a.motion.id >= 0 ? it->second->kind->set->attributes(std::size_t(a.motion.id))
                                                     : nullptr;
        if (attr != nullptr)
            for (const auto &t : attr->triggers)
                if (t.id == 0 && float(t.frame) >= a.motion.time)
                    return true;
        return false;
    }

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

    bool player_motion(Object &o, Actor &by, int slot, float blend) override;
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
    void player_steer(Object &o, const Vec3 &way, float speed) override
    {
        if (const auto h = held.find(days_object(o)); h != held.end())
            h->second.steer = {way[0] * speed, way[1] * speed, way[2] * speed};
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

// An enemy's attacks of their own let go (its memory too): it is gone, or its field is
void drop_attacks(unsigned days)
{
    for (auto a = attacks.begin(); a != attacks.end();)
        if (days == 0 || a->second.enemy == days) {
            mod().memory.remove(a->first);
            a = attacks.erase(a);
        } else {
            ++a;
        }
}

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
std::uint32_t entry_record(const std::string &name);

// How much of its model is drawn: its fade (1:19, 1:20, 1:28), a quarter of it in stealth (1:194)
float drawn_fade(const Enemy &e)
{
    const float fade = std::clamp(e.actor->fade, 0.0F, 1.0F);
    return e.actor->stealth != 0 ? fade * 0.25F : fade;
}

Enemy *start(unsigned days, KindFiles &k, const KhdaysKh2Shell &s)
{
    auto e = std::make_unique<Enemy>();
    e->days = days;
    e->kind = &k;
    e->motions = std::make_unique<SetMotions>(k.set);
    e->actor = std::make_unique<Actor>(k.script, mod().memory, mod().calls, k.name);
    e->player = std::make_unique<kh2::MotionPlayer>(*k.skeleton);
    e->slot = khdays_kh2_shell_attack_slot(days);
    Actor &a = *e->actor;
    a.world = &the_field();
    a.motions = e->motions.get();
    a.set_u32(field::kEntry, entry_record(k.name));
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

// The pose now, once a tick (MOTION::calc): the motion at its time; while a blend runs (the motion standing at its
// start frame), the pose shown when it started taken each tick's blend toward that frame -- blend_keep of it left
void pose(Enemy &e)
{
    const Actor &a = *e.actor;
    if (a.motion.started != e.posed_started) {
        e.posed_started = a.motion.started;
        e.blend_from = e.pose; // the pose shown the tick before (Context1)
    }
    const kh2::Motion *now = a.motion.id >= 0 ? e.kind->set->motion(std::size_t(a.motion.id)) : nullptr;
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
    const Vec3 t = turned({p[0], 0.0F, p[2]}, e.actor->yaw());
    out[0] = s.place[0] + t[0] / kKh2PerDays;
    out[1] = s.place[1] + p[1] / kKh2PerDays;
    out[2] = s.place[2] + t[2] / kKh2PerDays;
}

// KH2's player as its enemies' attacks find him: Sora's shapes (P_EX100's: a cylinder 85 cm high, 35 round, and
// spheres on his bones up to his head) stand about 1.45 m high. Days's player's own sphere (half a metre round, on
// the ground) left the attacks KH2 aims at a standing Sora's chest and head above him (Cerberus's bites).
constexpr float kPlayerHeight = 1.45F;
// how far above and below a held player the ground is looked for (KH2's cm): a step up, a fall down
constexpr float kGroundSearchUp = 100.0F, kGroundSearchDown = 1000.0F;
constexpr float kHeldRadius = 30.0F; // a held player's body round, kept off the walls (KH2's cm)
constexpr float kStep = 30.0F;       // a fall from higher than this (KH2's cm) lands with the landing motion

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

// KH2's object entries (00objentry.bin: a u32 version and a u32 count, then 96-byte records -- + 0 the entry's
// number, + 4 its kind, 4 an enemy and 3 a boss, + 8 its name; OpenKH's numbers), each kind's record put in the scripts'
// memory once, at its actors' + 8 (YS::OBJ's Entry: 1:222 reads its number, 1:114 finds an actor by it); 0 when the
// mod's kh2/00objentry.bin has none for it
std::uint32_t entry_record(const std::string &name)
{
    static std::map<std::string, std::vector<std::uint8_t>> records;
    static std::map<std::string, std::uint32_t> made;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> file = read_file(u8(mod().folder) / "kh2" / "00objentry.bin");
        std::uint32_t count = 0;
        if (file.size() >= 8)
            std::memcpy(&count, file.data() + 4, 4);
        for (std::uint32_t i = 0; i < count && 8 + 96 * std::size_t(i + 1) <= file.size(); ++i) {
            const std::uint8_t *r = file.data() + 8 + 96 * std::size_t(i);
            const char *n = reinterpret_cast<const char *>(r + 8);
            records[std::string(n, strnlen(n, 32))] = std::vector<std::uint8_t>(r, r + 96);
        }
        if (records.empty())
            khdays_log("kh2 enemies: no KH2 object entries (the mod's kh2/00objentry.bin): scripts finding objects by "
                       "their entry find none");
    }
    if (const auto m = made.find(name); m != made.end())
        return m->second;
    const auto it = records.find(name);
    if (it == records.end())
        return made[name] = 0;
    const std::uint32_t at = mod().memory.add(96, name + "'s object entry");
    mod().memory.write(at, it->second.data(), 96);
    return made[name] = at;
}

// The name of KH2's object entry `number` (00objentry.bin's + 0, its name at + 8); "" none
std::string entry_name(std::uint32_t number)
{
    static std::map<std::uint32_t, std::string> names;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> file = read_file(u8(mod().folder) / "kh2" / "00objentry.bin");
        std::uint32_t count = 0;
        if (file.size() >= 8)
            std::memcpy(&count, file.data() + 4, 4);
        for (std::uint32_t i = 0; i < count && 8 + 96 * std::size_t(i + 1) <= file.size(); ++i) {
            const std::uint8_t *r = file.data() + 8 + 96 * std::size_t(i);
            std::uint32_t id = 0;
            std::memcpy(&id, r, 4);
            const char *n = reinterpret_cast<const char *>(r + 8);
            names.emplace(id, std::string(n, strnlen(n, 32)));
        }
    }
    const auto it = names.find(number);
    return it != names.end() ? it->second : std::string();
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
    // his keyblade (the mod's kh2/obj/W_EX010.mdlx and .a.us: the Kingdom Key): its skeleton, which the enemies' motions
    // for his weapon ('0011') are made for, in its own axes on his bone `hand`; its effects (its swing's trail)
    std::shared_ptr<const kh2::MotionSkeleton> weapon;
    int hand = -1;
    KindFiles weapon_fx;
};

// The bone KH2's player holds his weapon on: 00objentry.bin's P_EX100 record's + 7 (its weapon joint) names a
// character of 03system.bin's "sklt" (a u32 kind, a u32 count, then 8 bytes each: a u32 character, u16 the primary
// weapon's bone, u16 the second's: OpenKH's 03system page); his primary's bone (178), -1 when either file lacks it
int player_hand_bone()
{
    const fs::path kh2 = u8(mod().folder) / "kh2";
    const std::vector<std::uint8_t> entries = read_file(kh2 / "00objentry.bin");
    std::uint32_t count = 0;
    if (entries.size() >= 8)
        std::memcpy(&count, entries.data() + 4, 4);
    int character = -1;
    for (std::uint32_t i = 0; i < count && 8 + 96 * std::size_t(i + 1) <= entries.size() && character < 0; ++i) {
        const std::uint8_t *r = entries.data() + 8 + 96 * std::size_t(i);
        const char *n = reinterpret_cast<const char *>(r + 8);
        if (std::string(n, strnlen(n, 32)) == "P_EX100")
            character = r[7];
    }
    if (character < 0)
        return -1;
    const std::vector<std::uint8_t> sys = read_file(kh2 / "03system.bin");
    try {
        for (const kh2::BarEntry &e : kh2::bar_entries(sys.data(), sys.size()))
            if (e.name == "sklt" && e.size >= 8) {
                std::uint32_t n = 0;
                std::memcpy(&n, sys.data() + e.offset + 4, 4);
                for (std::uint32_t i = 0; i < n && 8 + 8 * std::size_t(i + 1) <= e.size; ++i) {
                    const std::uint8_t *s = sys.data() + e.offset + 8 + 8 * std::size_t(i);
                    std::uint32_t who = 0;
                    std::uint16_t bone = 0;
                    std::memcpy(&who, s, 4);
                    std::memcpy(&bone, s + 4, 2);
                    if (int(who) == character)
                        return bone != 0xffff ? int(bone) : -1;
                }
            }
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: the mod's kh2/03system.bin cannot be read (%s)", x.what());
    }
    return -1;
}

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
        const std::vector<std::uint8_t> weapon = read_file(u8(mod().folder) / "kh2" / "obj" / "W_EX010.mdlx");
        try {
            if (!weapon.empty())
                files.weapon =
                    std::make_shared<const kh2::MotionSkeleton>(kh2::read_motion_skeleton(weapon.data(), weapon.size()));
        } catch (const std::exception &x) {
            khdays_log("kh2 enemies: the mod's kh2/obj/W_EX010.mdlx cannot be read (%s)", x.what());
        }
        files.hand = player_hand_bone();
        files.weapon_fx.name = "W_EX010";
        if (files.weapon && files.skeleton && files.hand >= 0 && std::size_t(files.hand) < files.skeleton->joints.size())
            read_effects(files.weapon_fx, u8(mod().folder));
        else
            khdays_log("kh2 enemies: no KH2 keyblade (the mod's kh2/obj/W_EX010.mdlx, its 00objentry.bin and "
                       "03system.bin's weapon bone: %d): a held player's swings draw no trail",
                       files.hand);
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
    for (auto &[days, e] : enemies) {
        if (e->carrier == object)
            e->carrier = 0;
        // his weapon's swing ends with the motion (his own has no range 12): its trail closes (loop_end)
        for (Enemy::Running &r : e->fx)
            if (r.on_weapon && r.player == object && r.handle == h->second.trail)
                r.fx->stop();
    }
    drawn_weapons.erase(object);
    if (const auto o = players.find(object); o != players.end() && o->second)
        o->second->set_bits(field::kStatus, bit::kOtherMotion, false);
    if (tracing())
        khdays_log("kh2 enemies: player %08x let go (%s)", object, why);
    held.erase(h);
}

// 1:29 (YS::trap_obj_motion_capture -> YS::MOTION::capture -> _capture): the motion of the enemy's set for the player,
// always from its first frame, `blend` its blend (YS::MOTION::start_motion(binarc, blend, 0); the PS3 build's): above
// 2 the motion stands while the blend counts from 2 up to it and his pose blends into its first frame (start_blend;
// add_time then plays its first frame's triggers, exec_trigger(start, -1)), else it starts with them (Rising Sun's 8:
// its AB50's twelve lights on his limbs at frame 8 were never reached when 8 was read as the frame to start from)
bool Field::player_motion(Object &o, Actor &by, int slot, float blend)
{
    const unsigned object = days_object(o);
    const auto it = by_actor.find(&by);
    if (object == 0 || it == by_actor.end() || slot < 0 || it->second->dying)
        return false; // (a defeated enemy lets him go: its thread on him takes him no more)
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
    const bool taken_now = held.count(object) == 0;
    Held &h = held[object];
    if (taken_now) {
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
    h.own_queued = false; // a new motion: what was asked of the one before is not of this one
    h.steer = {};
    h.weapon = nullptr; // its weapon's ('0011' with data of its own: what it attacks with, its attributes)
    for (const auto &v : set.variants(std::size_t(slot)))
        if (h.weapon == nullptr && v.name == "0011" && v.own && (v.attributes || v.motion))
            h.weapon = &v;
    static unsigned motions_started = 0;
    h.started = ++motions_started;
    h.time = 0.0F;
    h.blend = blend > 2.0F ? blend : 0.0F;
    h.blend_count = h.blend > 0.0F ? 2.0F : 0.0F;
    h.blend_keep = h.blend > 0.0F ? 1.0F - 2.0F / h.blend : 0.0F;
    h.fresh = true;
    h.ended = false;
    h.offered = -1;
    h.taken.clear();
    h.struck.clear();
    h.cued = -1.0F; // its first frame's triggers too (exec_trigger(start, -1))
    o.set_u32(field::kMotion, std::uint32_t(slot));
    o.set_u32(field::kMotionNow, std::uint32_t(slot));
    o.set_bits(field::kStatus, bit::kMotionEnded, false);
    o.set_bits(field::kStatus, bit::kOtherMotion, true);
    if (tracing())
        khdays_log("kh2 enemies: %08x %s player %08x: %s's motion %d (%s, %s), blend %.1f, %.1f long", h.enemy,
                   taken_now ? "takes" : "keeps", object, it->second->kind->name.c_str(), slot,
                   set.name(std::size_t(slot)).c_str(),
                   body->name.c_str(), double(blend), double(body->motion->length_ticks()));
    return true;
}

void Field::player_own_motion(Object &o, int id)
{
    const unsigned object = days_object(o);
    // KH2 queues it on the player's controller (1:13, FUN_1403c8310), taken once the motion of another's set he plays
    // gives him back: one still playing keeps him till it blends back to his idle (range 2) or ends (the Magnum
    // Loader's thread asks for 0 as Quick Blade begins, the Armored Knight's as Rising Sun's AB52 does); one at its end
    // (Cerberus's) lets him go now
    if (const auto h = held.find(object); h != held.end() && !h->second.ended) {
        h->second.own_queued = true;
        if (tracing())
            khdays_log("kh2 enemies: player %08x's own motion %d queued till his motion ends or blends back to idle",
                       object, id);
        return;
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
// weapon's attacks, sounds and swing's trail.
void weapon_trail(Enemy &e, unsigned object, Held &h, bool swing);
void hold_tick(Enemy &e, unsigned object, Held &h)
{
    const kh2::Motion &m = *h.body->motion;
    const float length = m.length_ticks();
    const bool loops = m.loop_from() < m.end() && m.loop_ticks() < length;
    const Vec3 was = h.place; // where this tick's moves start: the room's walls stop them (below)
    // 1:29's motion as YS::MOTION plays it (Playing, actor.h): at its first frame the tick it started; over a blend,
    // the count on a tick a tick (add_time while + 0x34 is set) with the motion standing, no travel and no triggers,
    // then on from its first frame by what the count went past, that frame's triggers played (exec_trigger(start, -1))
    bool still = false;
    float by = 1.0F;
    if (h.fresh) {
        h.fresh = false;
        still = true;
    } else if (h.blend > 0.0F) {
        h.blend_count += 1.0F;
        if (h.blend_count < h.blend) {
            h.blend_keep *= 1.0F - h.blend_count / h.blend;
            still = true;
        } else {
            by = h.blend_count - h.blend;
            h.blend = h.blend_count = h.blend_keep = 0.0F;
        }
    }
    const bool blending = h.blend > 0.0F;
    const float t0 = h.time;
    float t1 = t0 + by;
    const auto root = [&m](float at) {
        const kh2::Mat4 r = m.root_motion(at);
        return Vec3{r[12], r[13], r[14]};
    };
    Vec3 model{};
    if (!h.ended && !still) {
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
    // in the air while his motion has a range 3, 24 or 30 (no pull: the air classes' movement adds none, FUN_1403cb100);
    // else KH2's ground class moves him: along the ground only (1:89's and 1:111's way flattened), on it (Days's room
    // collision under him) -- the Soldier's Cyclone swings him round it, its way down to the Soldier too
    bool air = false;
    if (const kh2::MotionAttributes *attr = h.body->attributes.get())
        for (const auto &r : attr->ranges)
            if ((r.id == 3 || r.id == 24 || r.id == 30) && h.time >= float(r.start) && h.time < float(r.end))
                air = true;
    const Vec3 asked = {h.move[0] + h.steer[0], h.move[1] + h.steer[1], h.move[2] + h.steer[2]};
    for (int i = 0; i < 3; ++i) // a script's move (1:89), this tick's, and its steering (1:111)
        if (air || i != 1)
            h.place[std::size_t(i)] += h.move[std::size_t(i)] + h.steer[std::size_t(i)];
    h.move = {};
    // the room's walls stop him as KH2's collision does: a cast at his middle from where he was to where he goes, the
    // way along the ground, him kept his radius short of what it meets (the Soldier's Cyclone steps him 3.6 m aside)
    if (const float dx = h.place[0] - was[0], dz = h.place[2] - was[2]; dx * dx + dz * dz > 0.0F) {
        const float went = std::sqrt(dx * dx + dz * dz), middle = was[1] - kPlayerMiddle * kKh2PerDays;
        const float reach = went + kHeldRadius;
        float from[3], to[3], hit[3];
        to_days({was[0], middle, was[2]}, from);
        to_days({was[0] + dx / went * reach, middle, was[2] + dz / went * reach}, to);
        if (khdays_kh2_shell_ray(e.days, from, to, hit)) {
            const Vec3 at = to_kh2(hit);
            const float d =
                std::max(0.0F, std::sqrt((at[0] - was[0]) * (at[0] - was[0]) + (at[2] - was[2]) * (at[2] - was[2])) -
                                   kHeldRadius);
            if (d < went) {
                h.place[0] = was[0] + dx / went * d, h.place[2] = was[2] + dz / went * d;
                if (trace_level() >= 2)
                    khdays_log("kh2 enemies: held player %08x stopped by a wall %.1f along (KH2's cm)", object,
                               double(d));
            }
        }
    }
    if (!air) {
        // on the ground under him; a ledge (the ground more than a step below where he was) is not stepped off: KH2's
        // rooms wall their edges, Days's arenas leave some open (eh's platform), so he stays where he was
        float from[3], to[3], hit[3];
        to_days({h.place[0], h.place[1] - kGroundSearchUp, h.place[2]}, from);
        to_days({h.place[0], h.place[1] + kGroundSearchDown, h.place[2]}, to);
        if (khdays_kh2_shell_ray(e.days, from, to, hit)) {
            const float ground = to_kh2(hit)[1];
            if (ground - was[1] > kStep) {
                if (trace_level() >= 2)
                    khdays_log("kh2 enemies: held player %08x kept off a ledge (ground %.1f below)", object,
                               double(ground - was[1]));
                h.place = was;
            } else {
                h.place[1] = ground;
            }
        } else {
            if (trace_level() >= 2)
                khdays_log("kh2 enemies: held player %08x finds no ground under %.1f %.1f %.1f", object,
                           double(h.place[0]), double(h.place[1]), double(h.place[2]));
            h.place = was; // no ground under it at all
        }
    }
    if (trace_level() >= 2)
        khdays_log("kh2 enemies: held player %08x at %.1f in %d%s: moved %.1f %.1f %.1f asked, from %.1f %.1f %.1f to "
                   "%.1f %.1f %.1f (KH2's cm)",
                   object, double(h.time), h.slot, air ? " (in the air)" : "", double(asked[0]), double(asked[1]),
                   double(asked[2]), double(was[0]), double(was[1]), double(was[2]), double(h.place[0]),
                   double(h.place[1]), double(h.place[2]));
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
            if (r.id == 2 && h.own_queued) // his own idle asked for: back to it (Quick Blade's frame 107)
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
            if (blending || float(tr.frame) <= h.cued || float(tr.frame) > h.time)
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
    // his weapon's own triggers (8 its sounds: the keyblade's bank 500, Quick Blade's swish; 1 an effect of the enemy's
    // on the keyblade, as YS::MOTION::_Capture gives both parts the capturer's PAX) and its swing (range 12, + 0x20 bit
    // 11: its trail, YS::WEAPON::locus)
    bool swing = false;
    if (h.weapon != nullptr && h.weapon->attributes) {
        for (const auto &tr : h.weapon->attributes->triggers) {
            if (blending || float(tr.frame) <= h.cued || float(tr.frame) > h.time)
                continue;
            if (tr.id == 1 && !tr.params.empty())
                cast(e, std::int16_t(tr.params[0]), object, true);
            else if (const int id = trigger_sound(tr); id > 0)
                sound(e, id);
        }
        for (const auto &r : h.weapon->attributes->ranges)
            if (r.id == 12 && in(r))
                swing = true;
    }
    weapon_trail(e, object, h, swing && !blending);
    if (h.own_queued && h.ended) // his own motion asked for: taken at this one's end (Rising Sun's AB52)
        h.idle = true;
    if (!blending)
        h.cued = h.time;
    if (const auto o = players.find(object); o != players.end() && o->second) {
        o->second->set_place(h.place);
        o->second->set_yaw(h.yaw);
    }
}

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
    const kh2::MotionAttributes *attr = a.motion.id >= 0 ? e.kind->set->attributes(std::size_t(a.motion.id)) : nullptr;
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
    const std::set<int> groups = groups_on(e);
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

// The command an enemy's script offers now (1:165 this frame or the one before), -1 none
int script_offered()
{
    return script_offer.command >= 0 && khdays_link_frame() - script_offer.frame <= 2u &&
                   enemies.count(script_offer.enemy) != 0
               ? script_offer.command
               : -1;
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
    // a press takes one command: the one a held player's motion offers, else a script's, else a zone's
    const auto send = [&e](int id, const char *how) {
        pressed = std::uint16_t(pressed & ~KHDAYS_KEY_X);
        ++local_takes;
        local_taken = id;
        const Command *c = command(id);
        const int number = c != nullptr ? c->argument : id;
        const auto o = players.find(local_player);
        // KH2 sends the command's target record (FUN_1403a8b10 -> FUN_1403b5fa0: the manager's + 0x10, FUN_1403bee60's:
        // the object, its group, none, -1); the scripts take the object from its first word (the Dusk's Reversal
        // orbits the player round it)
        if (e.command_record == 0)
            e.command_record = mod().memory.add(16, e.actor->name() + "'s command record");
        const std::uint32_t record[4] = {e.actor->address(), 0u, 0u, 0xffffffffu};
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
        send(id, "offered by the script of");
    }
    if (e.zone >= 0 && (pressed & KHDAYS_KEY_X) != 0 && held.count(local_player) == 0 && zone_offerer() == &e) {
        const int id = e.zone;
        e.zone = -1;
        e.zone_taken_motion = e.actor->motion.id;
        send(id, "from the zone of");
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
            khdays_log("kh2 enemies: %08x casts %s's element %d (effect %zu, emitter %zu, bone %d, sound %d%s)", e.days,
                       k.name.c_str(), number, it->second.effect, it->second.emitter, it->second.bone,
                       it->second.sound, on_weapon ? ", on the player's weapon" : player != 0 ? ", on the player" : "");
        return handle;
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: %s's effect %d cannot be played (%s)", k.name.c_str(), number, x.what());
    }
    return 0;
}

// The element of KH2's keyblade's effects YS::WEAPON::locus starts: its swing's trail (W_EX010.a.us's 10: the swing
// trail's command 0xc8 between its two points, 0.65 and 3.7 times its bone 2's length out along the blade)
constexpr int kSwingTrail = 10;

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
    const auto r = std::find_if(e.fx.begin(), e.fx.end(), [&h](const Enemy::Running &x) { return x.handle == h.trail; });
    if (r != e.fx.end() && !r->fx->done()) {
        if (!swing) {
            r->fx->stop();
            h.trail = 0;
        }
    } else {
        h.trail = 0;
    }
}

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

// An effect's place in KH2's world now (FUN_1402be970: its instance's): its element's on the enemy's pose, the
// enemy's place and yaw (the effects' space is KH2's model space, y down, the model's turn about the up its yaw)
bool effect_place(const Enemy &e, std::uint32_t handle, Vec3 &out)
{
    for (const auto &r : e.fx)
        if (r.handle == handle && r.element != nullptr) {
            // one on a held player's bones: where it was last drawn
            const kh2::EffectMatrix m = r.player != 0 && r.placed ? r.last : element_world(e.pose, *r.element);
            const Vec3 place = e.actor->place();
            const Vec3 t = turned({m[12], 0.0F, m[14]}, e.actor->yaw());
            out = {place[0] + t[0], place[1] + m[13], place[2] + t[2]};
            return true;
        }
    return false;
}

// A hit made with an attack on an object at once (2:17): a Days player its enemy's hit (KH2's own attack landing on
// him: trigger 15 too), a KH2 enemy KH2's player's blow when he owns it (the Neoshadow's strike: Days's damage the
// player's last hit times the attack's power)
void strike(AttackObject &o, Object &target)
{
    const auto owner = enemies.find(o.enemy);
    if (owner == enemies.end())
        return;
    Enemy &e = *owner->second;
    if (const unsigned player = days_object(target); player != 0) {
        if (o.players || !o.struck.insert(player).second)
            return;
        const float push[3] = {0.0F, 0.0F, 0.0F};
        o.landed = true; // KH2's hit lands whether Days's player takes it or not (his guard, his dodge)
        o.hit = target.address();
        if (khdays_kh2_shell_hit(e.days, player, e.slot, push))
            landed(e, player, o.atkp);
        if (o.on_hit != 0) { // its script's routine on the hit (2:38), its value given
            const ai::Value v = ai::Value::of_address(o.on_hit_value);
            e.actor->machine().start_routine(e.actor->machine().main_thread(), o.on_hit, {&v, 1});
        }
        return;
    }
    Actor *x = target.actor();
    const auto it = x != nullptr ? by_actor.find(x) : by_actor.end();
    if (!o.players || it == by_actor.end())
        return;
    Enemy &hit = *it->second;
    o.landed = true;
    o.hit = target.address();
    const int power = attack_power(o.atkp);
    const int base = hit.days_damage > 0 ? hit.days_damage : last_days_damage > 0 ? last_days_damage : 1;
    hit.blow += power > 0 ? std::max(1, base * power / 100) : 0;
    if (tracing())
        khdays_log("kh2 enemies: KH2's player's attack (atkp %d, power %d) struck on %08x: %d damage", o.atkp, power,
                   hit.days, hit.blow);
}

// An enemy's attacks of their own this tick (YS::ATTACK_MAGIC's virtual + 0x18, FUN_1404376b0): each following its
// effect (gone with it), growing (gone grown), swept from its place before to its place now in steps of its radius
// against Days's players -- a sphere when its height is its radius, else a cylinder that high from its place up -- each
// player hit once; those ended let go
void attack_objects(Enemy &e)
{
    std::vector<KhdaysKh2Player> list;
    for (auto it = attacks.begin(); it != attacks.end();) {
        AttackObject &o = it->second;
        if (o.enemy != e.days) {
            ++it;
            continue;
        }
        if (o.ended || e.dying) {
            mod().memory.remove(it->first);
            it = attacks.erase(it);
            continue;
        }
        if (o.expired) { // its script may still ask after it (2:14, 2:29) until it ends it
            ++it;
            continue;
        }
        if (o.effect != 0) {
            Vec3 place;
            if (!the_field().effect_where(o.effect, place))
                o.expired = true;
            else
                the_field().attack_place(it->first, place);
        }
        float radius = o.radius, height = o.height;
        if (o.grow_for > 0.0F) {
            if ((o.grow_ticks += 1.0F) > o.grow_for)
                o.expired = true;
            const float f = std::sin(std::min(o.grow_ticks / o.grow_for, 1.0F) * kPi * 0.5F);
            radius = o.grow_radius * (1.0F - f) + o.radius * f;
            height = o.grow_height * (1.0F - f) + o.height * f;
        }
        if (o.expired) {
            ++it;
            continue;
        }
        const float step = std::min(radius, height);
        if (o.placed && step > 0.0F && !o.players) {
            if (list.empty()) {
                list.resize(std::size_t(std::max(0, khdays_kh2_shell_players(e.days, nullptr, 0))));
                list.resize(std::size_t(std::max(0, khdays_kh2_shell_players(e.days, list.data(), int(list.size())))));
            }
            const Vec3 way = {o.at[0] - o.before[0], o.at[1] - o.before[1], o.at[2] - o.before[2]};
            const float length = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
            const int steps = std::max(1, int(std::ceil(length / step)));
            for (int k = steps; k >= 1; --k) {
                const float f = float(k) / float(steps);
                float c[3];
                to_days({o.before[0] + way[0] * f, o.before[1] + way[1] * f, o.before[2] + way[2] * f}, c);
                for (const KhdaysKh2Player &p : list) {
                    if (o.struck.count(p.object) != 0)
                        continue;
                    if (const auto h = held.find(p.object); h != held.end() && h->second.unhittable)
                        continue;
                    const float r = radius / kKh2PerDays, up = (radius == height ? 0.0F : height / kKh2PerDays);
                    // his body's middle line (feet a radius up to his top a radius down) against the sphere's centre or
                    // the cylinder's axis
                    const float low = p.place[1] + p.radius,
                                high = std::max(low, p.place[1] + kPlayerHeight - p.radius);
                    const float y = std::clamp(c[1] + up * 0.5F, low, high);
                    const float dy =
                        up > 0.0F ? std::max(0.0F, std::fabs(y - (c[1] + up * 0.5F)) - up * 0.5F) : y - c[1];
                    const float dx = p.place[0] - c[0], dz = p.place[2] - c[2];
                    if (std::sqrt(dx * dx + dz * dz + dy * dy) >= r + p.radius)
                        continue;
                    Object *who = nullptr;
                    if (const auto pl = players.find(p.object); pl != players.end())
                        who = pl->second.get();
                    if (who != nullptr) {
                        if (tracing())
                            khdays_log("kh2 enemies: %08x's attack (atkp %d) hits %08x", e.days, o.atkp, p.object);
                        strike(o, *who);
                    }
                }
            }
        }
        o.before = o.at;
        ++it;
    }
}

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
    const kh2::MotionAttributes *attr =
        m.id >= 0 && m.blend <= 0.0F ? e.kind->set->attributes(std::size_t(m.id)) : nullptr;
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

// The motion's state ranges, each a bit of + 0x18c while the motion's time lies in it (OpenKH's ANB page: range 0
// grounded at bit 2, 1 in the air 3, 3 no gravity 5, 9 jumping or landing 9, 24 no gravity keeping its speed 6, 30 24
// ...): what the scripts read -- 1:60's "off the ground" is bits 5 and 24 (FUN_1403ba9a0: + 0x18c & 0x1000020), 2:5's
// and 1:78's bit 9. A Days hit's reaction holds bit 9 too.
constexpr std::pair<int, int> kStateRanges[] = {{0, 2},   {1, 3},   {2, 4},   {3, 5},   {9, 9},   {11, 10},
                                                 {12, 11}, {13, 15}, {14, 19}, {15, 16}, {21, 18}, {24, 6},
                                                 {27, 22}, {29, 23}, {30, 24}, {31, 25}, {32, 26}, {34, 27},
                                                 {38, 28}, {39, 29}, {40, 30}, {41, 31}};
// ranges 3, 24 and 30: the motion alone carries it up and down (the air classes' movement, FUN_1403cb100 and
// FUN_1403cb500, adds no pull while + 0x18c has 0x1000020 or bit 6)
constexpr std::uint32_t kMotionAir = 1u << 24; // range 30 (FUN_14040d810), 1:60's "off the ground" with bit 5
constexpr std::uint32_t kNoGravity = (1u << 5) | (1u << 6) | kMotionAir;

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
    a.begin_tick();
    if (trace_level() >= 2 && ++e.ticks_traced % 60 == 0 && a.machine().main_thread() != nullptr) {
        std::string at;
        threads_of(*a.machine().main_thread(), at);
        khdays_log("kh2 enemies: %08x's threads at%s; motion %d at %.1f%s%s; %.3f above the ground, %.1f its fall", e.days,
                   at.c_str(), a.motion.id, double(a.motion.time), a.jump.on ? ", in a jump" : "",
                   e.reacting ? ", reacting" : "", double(s.above), double(a.jump.on ? a.jump.up : e.fall));
    }
    if (!e.sent_state.empty() && (e.sent_after -= 1.0F) <= 0.0F) {
        if (a.states().shout(a, e.sent_state, e.sent_from) && tracing())
            khdays_log("kh2 enemies: %08x hears \"%s\" shouted", e.days, e.sent_state.c_str());
        e.sent_state.clear();
    }
    a.set_place(place); // Days's, its collision's push taken
    const unsigned started = a.motion.started;
    if (!e.paused)
        a.tick();
    if (const Vec3 now = a.place(); now != place)
        place = now; // its script put it elsewhere (1:148, 1:153, 1:220)
    if (trace_level() >= 2 && a.motion.started != started && a.motion.blend > 0.0F)
        khdays_log("kh2 enemies: %08x tick %u: motion %d over a blend of %.1f, standing at %.1f", e.days,
                   e.ticks_traced, a.motion.id, double(a.motion.blend), double(a.motion.time));
    const bool blending = a.motion.blend > 0.0F;
    Vec3 travel = a.step(1.0F);
    if (trace_level() >= 2 && blending && a.motion.blend <= 0.0F)
        khdays_log("kh2 enemies: %08x tick %u: motion %d blended in, on from %.1f", e.days, e.ticks_traced,
                   a.motion.id, double(a.motion.time));
    cues(e);
    event_tick(e);
    motion_states(e);
    const bool no_gravity = (a.u32(field::kStatus) & kNoGravity) != 0;
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
        if (e.fall_from > kStep && !told)
            a.touch_down();
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

// A held player's weapon now, in his model's space (KH2's cm, y up, his root's travel taken out): the bones his KH2
// model drew it with this frame or the last (held_weapon_drawn: its weapon's in its own hand), else KH2's keyblade's
// bones (W_EX010's skeleton) in his weapon's motion's pose ('0011', in the keyblade's own axes: its grip barely moves)
// on his hand's bone (00objentry.bin and 03system.bin's sklt: P_EX100's 178), as KH2 attaches its weapon object; empty
// without the keyblade's files or a weapon motion
std::vector<kh2::Mat4> held_weapon_pose(const Held &h, unsigned object)
{
    if (const auto d = drawn_weapons.find(object);
        d != drawn_weapons.end() && !d->second.bones.empty() && khdays_link_frame() - d->second.frame <= 2u)
        return d->second.bones;
    const PlayerFiles &files = player_files();
    if (!files.weapon || files.hand < 0 || h.weapon == nullptr || !h.weapon->motion)
        return {};
    const std::vector<kh2::Mat4> body = held_pose(h);
    if (std::size_t(files.hand) >= body.size())
        return {};
    static std::unique_ptr<kh2::MotionPlayer> player;
    if (!player)
        player = std::make_unique<kh2::MotionPlayer>(*files.weapon);
    std::vector<kh2::Mat4> out;
    try {
        out = pose_at(*player, *h.weapon->motion, h.time);
    } catch (const std::exception &x) { // a weapon motion made for another skeleton
        if (tracing())
            khdays_log("kh2 enemies: the held player's weapon motion cannot be played on W_EX010 (%s)", x.what());
        return {};
    }
    const kh2::Mat4 &hand = body[std::size_t(files.hand)];
    for (kh2::Mat4 &bone : out) { // the hand's frame, then the bone's in it (row vectors: the bone's, then the hand's)
        float on[16];
        row_matrix_mul(bone.data(), hand.data(), on);
        std::copy(on, on + 16, bone.begin());
    }
    return out;
}

// An element's two points (Element::points: a swing trail's base and tip) in its model's effects' space (KH2's axes, y
// down, cm, as element_world's): each on its bone in `pose` (KH2's model space, y up), its `along` times the bone's
// length (its skeleton's scale w) out along the bone's x (YS::OBJ::get_bone_pos), with its offset turned by the bone when
// the element has them (+ 0x4c; ryj::PAX::MANAGER::get_obj_data); none when a bone is not the pose's
std::vector<kh2::EffectPoint> element_points(const std::vector<kh2::Mat4> &pose, const kh2::MotionSkeleton &skeleton,
                                             const Element &el)
{
    std::vector<kh2::EffectPoint> out;
    for (int i = 0; i < 2; ++i) {
        const int b = el.point_bone[std::size_t(i)];
        if (b < 0 || std::size_t(b) >= pose.size() || std::size_t(b) >= skeleton.joints.size())
            return {};
        const kh2::Mat4 &m = pose[std::size_t(b)]; // column-major: its x axis m[0..2], its place m[12..14]
        const float along = el.point_along[std::size_t(i)] * skeleton.joints[std::size_t(b)].scale[3];
        const std::array<float, 3> &offset = i == 0 ? el.place : el.turn;
        kh2::EffectPoint p;
        for (int k = 0; k < 3; ++k) {
            float v = m[std::size_t(k)] * along + m[std::size_t(12 + k)];
            if (el.point_offsets)
                v += m[std::size_t(k)] * offset[0] + m[std::size_t(4 + k)] * offset[1] + m[std::size_t(8 + k)] * offset[2];
            p.place[std::size_t(k)] = k == 1 ? -v : v; // y up -> KH2's y down
        }
        out.push_back(p);
    }
    return out;
}

// A held player's model's effects' space (his model's axes, y down, cm; his feet at 0) to his enemy's: his model placed
// and turned where he stands from the enemy (both in KH2's world, y down; a model's y up, its turn Actor's), half a
// turn more about the up as his model is drawn (the enemy's motions for him are made in the enemy's frame: play_held
// turns them so; without it his effects stood mirrored on him, his keyblade's trail at his other hand). An affine map,
// row vectors (a point times it)
void held_map(const Enemy &e, const Held &h, float map[16])
{
    const Vec3 at = e.actor->place();
    const float yaw = e.actor->yaw();
    const auto to_enemy = [&](const Vec3 &r) {
        const Vec3 t = turned({-r[0], 0.0F, -r[2]}, h.yaw);
        const Vec3 d = {h.place[0] + t[0] - at[0], h.place[1] + r[1] - at[1], h.place[2] + t[2] - at[2]};
        const Vec3 m = turned({d[0], 0.0F, d[2]}, -yaw);
        return Vec3{m[0], d[1], m[2]};
    };
    std::fill(map, map + 16, 0.0F);
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
}

// An element a held player's motion cast, in its enemy's effects' space: on KH2's player's bone (P_EX100's skeleton) in
// the pose his motion has now -- his weapon's (KH2's keyblade's) for one on his weapon -- and its two points when it
// has them (`points`, left as they are otherwise). False without KH2's player's skeleton (or the keyblade's).
bool held_world(const Enemy &e, const Held &h, const Enemy::Running &r, kh2::EffectMatrix &out,
                std::vector<kh2::EffectPoint> *points)
{
    const std::vector<kh2::Mat4> pose = r.on_weapon ? held_weapon_pose(h, r.player) : held_pose(h);
    if (pose.empty())
        return false;
    float map[16];
    held_map(e, h, map);
    const kh2::EffectMatrix own = element_world(pose, *r.element);
    row_matrix_mul(own.data(), map, out.data());
    const kh2::MotionSkeleton *skeleton = r.on_weapon ? player_files().weapon.get() : player_skeleton();
    if (points != nullptr && r.element->points && skeleton != nullptr) {
        *points = element_points(pose, *skeleton, *r.element);
        for (kh2::EffectPoint &p : *points) {
            const std::array<float, 3> was = p.place;
            for (int c = 0; c < 3; ++c)
                p.place[std::size_t(c)] =
                    was[0] * map[c] + was[1] * map[4 + c] + was[2] * map[8 + c] + map[12 + c];
        }
    }
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
    if (e.fx.empty())
        return;
    const unsigned now = khdays_link_frame();
    const unsigned per_frame = unsigned(std::max(1L, std::lround(khdays_link_frame_seconds() * 60.0)));
    const unsigned ticks = e.fx_frame == 0 ? per_frame : now >= e.fx_frame ? (now - e.fx_frame) * per_frame : per_frame;
    e.fx_frame = now;
    float units = 1.0F;
    const kh2::EffectCamera cam = effect_camera(view, projection, units);
    kh2::EffectCamera placed = cam;
    for (Enemy::Running &r : e.fx) {
        // its two points (a swing trail's base and tip), where its element has them: on its bones now
        std::vector<kh2::EffectPoint> points;
        if (r.player != 0) {
            if (const auto h = held.find(r.player);
                h != held.end() && h->second.enemy == e.days && held_world(e, h->second, r, r.last, &points))
                r.placed = true;
            if (!r.placed) { // no KH2 player's skeleton: at the enemy's root
                Element at = *r.element;
                at.bone = kNoBone;
                r.last = element_world(e.pose, at);
                r.placed = true;
            }
            placed.world = r.last;
        } else if (r.in_world) { // its script's place in the world, in the enemy's effects' space
            Element at = *r.element;
            at.bone = kNoBone;
            const Vec3 from = e.actor->place();
            const float yaw = e.actor->yaw();
            const Vec3 t = turned({r.at[0] - from[0], 0.0F, r.at[2] - from[2]}, -yaw);
            at.place = {t[0], r.at[1] - from[1], t[2]};
            if (r.turned)
                at.turn = {r.turn[0], r.turn[1] - yaw, r.turn[2]};
            placed.world = element_world(e.pose, at);
        } else {
            placed.world = element_world(e.pose, *r.element);
            if (r.element->points && e.kind->skeleton) // the Magnum Loader's blades', the Neoshadow's claws' trails
                points = element_points(e.pose, *e.kind->skeleton, *r.element);
        }
        if (!points.empty())
            r.fx->set_points(std::move(points));
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
        const KindFiles &files = r.files != nullptr ? *r.files : *e.kind;
        if (r.element->effect < files.fx_pages.size())
            khdays::kh2_fx::submit(r.draws, units, *files.fx_pages[r.element->effect]);
    }
    for (std::size_t i = e.fx.size(); i-- > 0;)
        if (e.fx[i].fx->done() || (e.fx[i].end_age >= 0.0F && e.fx[i].age >= e.fx[i].end_age))
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

// The field's camera for a draw: the latest flush's, unless that one does not turn -- Days's enemies are drawn after a
// flush that folds the camera into the projection and leaves a camera of no turn, only moved back (traced: the Mole
// Driller's every draw) -- then the frame's own, its list's first flush's. With the identity the effects' view was the world:
// their sprites facing the screen (pppRyjMegaBirth's, pppDrawMatrixFront's) stood fixed in the world, edge on (the Mole
// Driller's dust stood up)
bool field_camera(float camera[16])
{
    if (khdays_g3d_camera(camera) == 0)
        return false;
    bool identity = true; // its turn the identity (a screen layer's: moved back 100 or 1024, not turned)
    for (int r = 0; r < 3 && identity; ++r)
        for (int c = 0; c < 3 && identity; ++c)
            identity = std::fabs(camera[r * 4 + c] - (r == c ? 1.0F : 0.0F)) < 1e-4F;
    float list[16];
    if (identity && khdays_g3d_list_camera(list) != 0)
        std::copy(list, list + 16, camera);
    return true;
}

DrawSpace draw_space()
{
    DrawSpace d;
    float vector[16];
    khdays_g3d_matrices(d.position, vector, d.projection);
    d.flush = khdays_g3d_base(d.base);
    d.has_camera = field_camera(d.camera);
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
    if (ghosts.empty() || !field_camera(camera))
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
            a.begin_tick();
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
        const unsigned alpha = unsigned(std::lround(drawn_fade(e) * 31.0F));
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
    const unsigned days = unsigned(KHDAYS_REF(actor));
    forget(days);
    if (appeared.erase(days) != 0)
        kind_of.erase(days); // a script's (1:0): its Days enemy another's once reused
}

extern "C" void khdays_kh2_enemies_area_left(void)
{
    khdays_sound_shots_stop();
    made_row.clear();
    row_kinds.clear();
    kh2_rows = false;
    drop_attacks(0);
    enemies.clear();
    ghosts.clear();
    by_actor.clear();
    kind_of.clear();
    appeared.clear();
    forget_teamwork();
    held.clear();
    shouts.clear();
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

// FUN_1403c9d40, once a frame: the states' names shouted sent to every enemy -- with a delay each its own time at
// random below it (KH2's chance, DAT_140783ca0 x 0x10dcd), else its state of that name's shout routine run now
void send_shouts()
{
    static std::uint32_t chance = 1;
    const std::vector<Shout> sent = std::move(shouts);
    shouts.clear();
    for (const Shout &s : sent) {
        std::vector<unsigned> listening; // by their Days enemy: a routine run may end one
        for (const auto &[actor, e] : by_actor)
            listening.push_back(e->days);
        for (const unsigned days : listening) {
            Enemy *e = find(days);
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

extern "C" void khdays_kh2_enemies_tick(void *actor, int delta)
{
    const unsigned days = unsigned(KHDAYS_REF(actor));
    const auto kind = kind_of.find(days);
    if (kind == kind_of.end())
        return;
    // the frame before's KH2 ticks, each as KH2 orders its frame: its objects (each enemy's own ticks, done), its
    // loose threads, then the shouts told (once a frame here: after the first) -- the Sorcerer's turn-giving thread
    // moves the turn its shout gave on before its cubes hear the order, as KH2's must
    if (const unsigned frame = khdays_link_frame(); frame != shouts_frame) {
        shouts_frame = frame;
        const int steps = delta > 0 ? std::max(1, int(std::lround(2.0 * double(delta) / 4096.0))) : 0;
        for (int t = 0; t < steps; ++t) {
            tick_loose();
            if (t == 0 && !shouts.empty())
                send_shouts();
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
    // another script put it elsewhere since the last frame (1:148 on its handle: the cubes put the Sorcerer they
    // made where they are): that place, not Days's
    if (e->placed) {
        const Vec3 now = e->actor->place();
        if (std::fabs(now[0] - e->given[0]) + std::fabs(now[1] - e->given[1]) + std::fabs(now[2] - e->given[2]) > 0.01F)
            place = now;
    }
    // KH2 carries it up and down itself (its jumps, falls, motions in the air): Days lowers a stand-in in the air
    // between two steps (by 0.148 a frame, its own fall) and that is not KH2's -- the height it was given is kept then.
    // Days lifting it (out of the ground) is kept; its ground's distance (`above`) is from the place it was given
    if (e->placed && s.place[1] < e->place[1])
        place[1] = to_kh2(e->place)[1];
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
        attack_objects(*e);
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
    e->given = e->actor->place();
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
        if (e->dead_marked) // its script ends it (1:328: the cubes fade out, then their end)
            return;
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
    Object *striker = the_field().player(a);
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
    // struck to the enemy along the ground (2:1), its attacker and target (+ 0x10, + 0x14), its Flag (+ 0x18) clear, a
    // small reaction (Reaction, + 0x26: 2:7, 2:8), no first one (OrigReaction, + 0x27: none of Days's hits a finish)
    mod().memory.write(days_hit->address(), way, sizeof way);
    days_hit->set_u32(0x10, striker != nullptr ? striker->address() : 0u);
    days_hit->set_u32(0x14, a.address());
    days_hit->set_u32(0x18, 0);
    days_hit->set_u32(0x24, 2u << 16);
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
        a.take_off.reset();
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
    if (id < 0)
        id = script_offered();
    if (id < 0 && held.count(local_player) == 0)
        if (const Enemy *z = zone_offerer())
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

// Ov107_AiState_ApplyHit and Ov107_AiState_HitShapeOverlap (prepare.py's joins): its script's FLAG_NOHIT_ATTACK and
// FLAG_NOHIT_OBJ (+ 0x120 bits 3 and 1): no hit lands, nothing touches it (the Mole Driller under the ground)
extern "C" int khdays_kh2_enemies_untouched(void *actor)
{
    const Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    return e != nullptr && !e->dying && (e->actor->u32(field::kFlags) & bit::kNoHitAttack) != 0;
}

extern "C" int khdays_kh2_enemies_no_contact(void *actor)
{
    const Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    return e != nullptr && !e->dying && (e->actor->u32(field::kFlags) & bit::kNoHitObject) != 0;
}

// Ov107_AiState_ApplyHit (prepare.py's join): a hit's damage kept from taking the enemy under the least health its
// script set (1:231, YS::SHEET's MinHp: KH2's damage stops there -- the Phantom's)
extern "C" int khdays_kh2_enemies_damage(void *actor, int amount)
{
    const Enemy *e = find(unsigned(KHDAYS_REF(actor)));
    if (e == nullptr || e->dying || amount <= 0)
        return amount;
    const std::uint32_t record = e->actor->u32(0x5c0);
    KhdaysKh2Shell s;
    if (record == 0 || !khdays_kh2_shell_read(e->days, &s))
        return amount;
    const int least = std::int32_t(mod().memory.u32(record + 8));
    if (least <= 0)
        return amount;
    const int kept = std::max(0, std::min(amount, s.hp - least));
    if (kept != amount && tracing())
        khdays_log("kh2 enemies: %08x keeps %d health: %d of %d damage taken", e->days, least, kept, amount);
    return kept;
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
    } else if (zone_offerer() == e || (script_offered() >= 0 && script_offer.enemy == e->days)) {
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
    const unsigned alpha = unsigned(std::lround(drawn_fade(*e) * 31.0F));
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
    out.started = h->second.started;
    out.share = h->second.blend > 0.0F ? 1.0F - h->second.blend_keep : 1.0F;
    out.skeleton = player_skeleton();
    return out.skeleton != nullptr;
}

void khdays::kh2_enemies::held_weapon_drawn(unsigned node, const float *bones, std::size_t count)
{
    const auto p = player_of_node.find(node);
    if (p == player_of_node.end() || held.count(p->second) == 0)
        return;
    DrawnWeapon &d = drawn_weapons[p->second];
    d.bones.resize(count);
    for (std::size_t b = 0; b < count; ++b)
        std::copy(bones + 16 * b, bones + 16 * (b + 1), d.bones[b].begin());
    d.frame = khdays_link_frame();
}
