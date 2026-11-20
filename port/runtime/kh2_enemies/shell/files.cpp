// The mod's files: a kind's model, motions, effects, sounds and events; KH2's system tables.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

std::string motion_set_name(const std::string &name);
} // namespace

std::vector<std::uint8_t> read_file(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

fs::path u8(const std::string &s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

// A file of KINGDOM HEARTS II's in a mod's kh2/ folder (`rel` under it): in `first` (a kind's own mod) when it is
// there, else in the first mod in load order that has it -- the installer's "Days - KH2 Enemies" or any mod that
// carries its own; empty when none has it
fs::path kh2_file(const std::string &rel, const std::string &first)
{
    std::error_code ec;
    if (!first.empty() && fs::is_regular_file(u8(first) / "kh2" / u8(rel), ec))
        return u8(first) / "kh2" / u8(rel);
    char path[1024];
    if (khdays_mods_find(("kh2/" + rel).c_str(), path, int(sizeof(path))))
        return u8(path);
    return {};
}

// The mod folder holding kh2/`rel` as kh2_file finds it, "" none
std::string kh2_mod_of(const std::string &rel, const std::string &first)
{
    const fs::path p = kh2_file(rel, first);
    if (p.empty())
        return {};
    fs::path mod = p;
    for (const auto &part : u8(rel)) {
        (void)part;
        mod = mod.parent_path();
    }
    const std::u8string m8 = mod.parent_path().u8string();
    return {m8.begin(), m8.end()};
}

// `default_groups`, when given: the header's + 4, the groups on unless the motion or the script says (FUN_1403ba550's
// last test; 1 in every one read: the body's group 0)
std::vector<Shape> read_shapes(const std::vector<std::uint8_t> &mdlx, std::uint32_t *default_groups)
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
namespace {

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
} // namespace

// A motion's trigger 8, a sound: (bank, number, ...) -- KH2's sound id bank x 1000 + number (FUN_1401de150); 0 none
int trigger_sound(const kh2::MotionAttributes::Trigger &t)
{
    if (t.id != 8 || t.params.size() < 2 || std::int16_t(t.params[0]) < 0 || std::int16_t(t.params[1]) < 0)
        return 0;
    return std::int16_t(t.params[0]) * 1000 + std::int16_t(t.params[1]);
}
namespace {

// The kind's sounds (its motions' triggers 8 -- those it makes the player play too, his body's and his weapon's: the
// keyblade's bank 500 -- and its elements'), each the mod's kh2/se/<id>.wav, decoded once
void read_sounds(KindFiles &k, const std::string &folder)
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
        const fs::path wav = kh2_file("se/" + std::to_string(id) + ".wav", folder);
        if (wav.empty())
            continue;
        const std::u8string p8 = wav.u8string();
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

void read_effects(KindFiles &k, const std::string &folder)
{
    const auto pax = bar_entry(read_file(kh2_file("obj/" + k.name + ".a.us", folder)), 0x12);
    if (pax.empty()) {
        khdays_log("kh2 enemies: %s has no effects (kh2/obj/%s.a.us)", k.name.c_str(), k.name.c_str());
        return;
    }
    try {
        for (auto &b : kh2::read_effect(pax.data(), pax.size()))
            k.fx.push_back(std::make_shared<const kh2::EffectBlock>(std::move(b)));
        k.fx_elements = pax_elements(pax);
        const auto common = read_file(kh2_file("effect/texcommon.dpd", folder));
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
        k.fx_hd_pages = first;
        khdays_log("kh2 enemies: %s's effects read (%zu effects, %zu elements%s)", k.name.c_str(), k.fx.size(),
                   k.fx_elements.size(), common.empty() ? "; no texcommon.dpd" : "");
    } catch (const std::exception &e) {
        khdays_log("kh2 enemies: %s's effects cannot be played (%s)", k.name.c_str(), e.what());
        k.fx.clear();
        k.fx_elements.clear();
        k.fx_pages.clear();
    }
}
} // namespace

// KH2's system effects (YS::EFFECT::Init: 00effect.bar's PAX, its entry of type 0x12, "syst" -- YS::SystemPax, which
// YS::EFFECT::GetSystemPax gives: a party member's knock-out stars, its element 14), read once from the mod's
// kh2/00effect.bar; no HD pictures of their own (KH2 has none), the resident ones cut as any effect's. Empty without
const KindFiles &system_effects()
{
    static KindFiles k;
    static bool read = false;
    if (read)
        return k;
    read = true;
    k.name = "00effect";
    const auto pax = bar_entry(read_file(kh2_file("00effect.bar")), 0x12);
    if (pax.empty()) {
        khdays_log("kh2 enemies: no KH2 system effects (the mod's kh2/00effect.bar): a party member knocked out shows "
                   "no stars");
        return k;
    }
    try {
        for (auto &b : kh2::read_effect(pax.data(), pax.size()))
            k.fx.push_back(std::make_shared<const kh2::EffectBlock>(std::move(b)));
        k.fx_elements = pax_elements(pax);
        const auto common = read_file(kh2_file("effect/texcommon.dpd"));
        if (!common.empty())
            k.common = kh2::read_effect_block(common.data(), common.size(), 0);
        for (std::size_t b = 0; b < k.fx.size(); ++b) {
            auto pages = std::make_unique<khdays::kh2_fx::Pages>("kh2/00effect.bar#" + std::to_string(b));
            pages->add({k.fx[b].get()}, "", 0);
            pages->add({&k.common}, "kh2/fx/texcommon");
            pages->tracing = tracing();
            k.fx_pages.push_back(std::move(pages));
        }
        khdays_log("kh2 enemies: KH2's system effects read (%zu effects, %zu elements)", k.fx.size(),
                   k.fx_elements.size());
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: KH2's system effects cannot be played (%s)", x.what());
        k.fx.clear();
        k.fx_elements.clear();
        k.fx_pages.clear();
    }
    return k;
}
namespace {

// Its model's own effects (the .mdlx's PAX: BAR entry 0x12, Hades's "hade", his hair's flame) read as its .a.us's,
// their HD pictures in the model's own folder after its textures' (a model file's pictures run through its entries
// in BAR order: its texture container's, then its PAX's)
void read_model_effects(KindFiles &k, const std::string &folder)
{
    const std::vector<std::uint8_t> mdlx = read_file(kh2_file("obj/" + k.name + ".mdlx", folder));
    const std::vector<std::uint8_t> pax = bar_entry(mdlx, 0x12);
    if (pax.empty())
        return;
    auto own = std::make_unique<KindFiles>();
    own->name = k.name;
    try {
        int first = kh2::mdlx_texture_pictures(mdlx.data(), mdlx.size());
        for (auto &b : kh2::read_effect(pax.data(), pax.size()))
            own->fx.push_back(std::make_shared<const kh2::EffectBlock>(std::move(b)));
        own->fx_elements = pax_elements(pax);
        own->common = k.common;
        for (std::size_t b = 0; b < own->fx.size(); ++b) {
            auto pages = std::make_unique<khdays::kh2_fx::Pages>("kh2/obj/" + k.name + ".mdlx#" + std::to_string(b));
            first += pages->add({own->fx[b].get()}, "kh2/obj/" + k.name, first);
            pages->add({&own->common}, "kh2/fx/texcommon");
            pages->tracing = tracing();
            own->fx_pages.push_back(std::move(pages));
        }
        khdays_log("kh2 enemies: %s's model's own effects read (%zu effects, %zu elements)", k.name.c_str(),
                   own->fx.size(), own->fx_elements.size());
        k.model_fx = std::move(own);
    } catch (const std::exception &e) {
        khdays_log("kh2 enemies: %s's model's own effects cannot be played (%s)", k.name.c_str(), e.what());
    }
}

// Its event sequences: every entry of type 0x16 in its .a.us, keyed by its name (docs/KH2_ENEMIES.md, "Event
// sequences")
void read_events(KindFiles &k, const std::string &folder)
{
    const auto bar = read_file(kh2_file("obj/" + k.name + ".a.us", folder));
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
} // namespace

Mod &mod()
{
    static Mod m;
    if (m.looked)
        return m;
    m.looked = true;
    std::error_code ec;
    for (int i = 0; i < khdays_mods_started_count(); ++i) {
        const char *started = khdays_mods_started_path(i);
        if (started == nullptr || !fs::is_regular_file(u8(started) / "kh2_enemies.txt", ec))
            continue;
        std::size_t added = 0;
        for (Kind &k : load_kinds(started, [](const std::string &why) { khdays_log("%s", why.c_str()); }))
            if (find_kind(m.kinds, k.name) == nullptr) {
                m.kinds.push_back(std::move(k));
                ++added;
            }
        khdays_log("kh2 enemies: %s's list: %zu kinds", started, added);
        if (m.folder.empty())
            m.folder = started;
    }
    if (m.folder.empty()) {
        khdays_log(
            "kh2 enemies: no mod holds kh2_enemies.txt (the installer's \"Days - KH2 Enemies\", or a mod's own): "
            "the missions' KH2 enemies play their stand-ins");
        return m;
    }
    const std::vector<std::uint8_t> shapes = read_file(kh2_file("calls.txt"));
    const int read = khdays::kh2_enemies::read_shapes(m.calls, std::string(shapes.begin(), shapes.end()));
    if (read == 0) {
        khdays_log("kh2 enemies: no mod has kh2/calls.txt (the calls' shapes the mod tool reads from the player's "
                   "KINGDOM HEARTS II): the missions' KH2 enemies play their stand-ins");
        m.folder.clear();
        m.kinds.clear();
        return m;
    }
    add_all(m.calls);
    if (trace_level() >= 3)
        trace_calls(m.calls);
    m.memory.report = [](const std::string &s) {
        if (tracing())
            khdays_log("kh2 enemies: %s", s.c_str());
    };
    khdays_log("kh2 enemies: %zu kinds, %d calls' shapes", m.kinds.size(), read);
    return m;
}

// A kind's files, read once. `form`: a form a script turns the player into (1:242: Luxord's card and die) or an object a
// script makes to move it itself (Final Xemnas's Sora), which has no script of its own -- read whatever its coverage,
// with an empty script
KindFiles *files_of(const std::string &name, bool form)
{
    Mod &m = mod();
    auto it = m.files.find(name);
    if (it != m.files.end())
        return it->second.get();
    std::unique_ptr<KindFiles> k;
    const Kind *kind = find_kind(m.kinds, name);
    if (kind == nullptr) {
        khdays_log("kh2 enemies: %s is in no mod's list: its stand-in plays", name.c_str());
    } else if (!kind->covered && !form) {
        khdays_log("kh2 enemies: %s's script makes calls the port does not answer yet (%s): its stand-in plays",
                   name.c_str(), kind->missing.c_str());
    } else {
        try {
            const fs::path obj = u8(kind->folder) / "kh2" / "obj";
            const auto mdlx = read_file(obj / (kind->name + ".mdlx"));
            // the motion set its object entry names (Hades of the Coliseum's cups moves by B_HE030.mset); none for an
            // object with no motion set of its own (the Thresholder's Possessor: its entry names none)
            const auto mset = read_file(kh2_file("obj/" + motion_set_name(kind->name), kind->folder));
            const auto scripts = ai::scripts_in(mdlx.data(), mdlx.size());
            if (scripts.empty() && !form)
                throw std::runtime_error("its .mdlx holds no script");
            k = std::make_unique<KindFiles>();
            k->name = kind->name;
            k->folder = kind->folder;
            const std::u8string p8 = (obj / (kind->name + ".mdlx")).u8string();
            k->mdlx.assign(p8.begin(), p8.end());
            k->script = scripts.empty() ? std::make_shared<const ai::Script>()
                                        : std::make_shared<const ai::Script>(scripts.front().second);
            k->set = mset.empty() ? std::make_shared<const kh2::MotionSet>()
                                  : std::make_shared<const kh2::MotionSet>(mset.data(), mset.size());
            k->player_set = mset.size() >= 16 && (mset[12] & 1) != 0;
            // (no bones for an object with no model of its own: the Possessor, drawn by its effects alone)
            const auto bar = kh2::bar_entries(mdlx.data(), mdlx.size());
            const bool model = std::any_of(bar.begin(), bar.end(), [](const kh2::BarEntry &e) { return e.type == 4; });
            k->skeleton = std::make_shared<const kh2::MotionSkeleton>(
                model ? kh2::read_motion_skeleton(mdlx.data(), mdlx.size()) : kh2::MotionSkeleton{});
            k->shapes = read_shapes(mdlx, &k->default_groups);
            k->plays_player = kind->plays_player;
            read_effects(*k, kind->folder);
            read_model_effects(*k, kind->folder);
            read_events(*k, kind->folder);
            read_sounds(*k, kind->folder);
            khdays_log("kh2 enemies: %s read (%zu motions' slots, %zu bones, %zu shapes)", name.c_str(),
                       k->set->slots(), k->skeleton->joints.size(), k->shapes.size());
        } catch (const std::exception &e) {
            khdays_log("kh2 enemies: %s cannot be read (%s): its stand-in plays", name.c_str(), e.what());
            k.reset();
        }
    }
    return m.files.emplace(name, std::move(k)).first->second.get();
}

namespace {

using EntryRecord = std::array<std::uint8_t, 96>;

// KH2's object entries (the mod's kh2/00objentry.bin: a u32 version and a u32 count, then 96-byte records -- + 0 the
// entry's number, + 4 its kind, 4 an enemy and 3 a boss, + 7 its weapon joint, + 8 its name, + 0x28 its motion set;
// OpenKH's numbers), read once
const std::vector<EntryRecord> &object_entries()
{
    static const std::vector<EntryRecord> entries = [] {
        std::vector<EntryRecord> out;
        const std::vector<std::uint8_t> file = read_file(kh2_file("00objentry.bin"));
        std::uint32_t count = 0;
        if (file.size() >= 8)
            std::memcpy(&count, file.data() + 4, 4);
        for (std::uint32_t i = 0; i < count && 8 + 96 * std::size_t(i + 1) <= file.size(); ++i)
            std::memcpy(out.emplace_back().data(), file.data() + 8 + 96 * std::size_t(i), 96);
        if (out.empty())
            khdays_log("kh2 enemies: no KH2 object entries (the mod's kh2/00objentry.bin): scripts finding objects by "
                       "their entry find none");
        return out;
    }();
    return entries;
}

// A record's name (+ 8) or motion set (+ 0x28): 32 bytes at most
std::string entry_text(const EntryRecord &r, std::size_t at)
{
    const char *n = reinterpret_cast<const char *>(r.data() + at);
    return {n, strnlen(n, 32)};
}
} // namespace

// The object entry of the kind `name` (its last record of that name) put in the scripts' memory once, at its actors'
// + 8 (YS::OBJ's Entry: 1:222 reads its number, 1:114 finds an actor by it); 0 when the mod has none for it
std::uint32_t entry_record(const std::string &name)
{
    static std::map<std::string, std::uint32_t> made;
    if (const auto m = made.find(name); m != made.end())
        return m->second;
    const EntryRecord *record = nullptr;
    for (const EntryRecord &r : object_entries())
        if (entry_text(r, 8) == name)
            record = &r;
    if (record == nullptr)
        return made[name] = 0;
    const std::uint32_t at = mod().memory.add(96, name + "'s object entry");
    mod().memory.write(at, record->data(), 96);
    return made[name] = at;
}

// The name of KH2's object entry `number` (its first record's); "" none
std::string entry_name(std::uint32_t number)
{
    static const std::map<std::uint32_t, std::string> names = [] {
        std::map<std::uint32_t, std::string> out;
        for (const EntryRecord &r : object_entries()) {
            std::uint32_t id = 0;
            std::memcpy(&id, r.data(), 4);
            out.emplace(id, entry_text(r, 8));
        }
        return out;
    }();
    const auto it = names.find(number);
    return it != names.end() ? it->second : std::string();
}

// Whether the object entry of `name` is an enemy's (its + 4, its kind: 3 a boss, 4 an enemy, 0xc a large one -- the
// Hydra) -- a Days enemy stands in for it -- or not (9 a friend in battle, 0x17 a gimmick: an ally, allies); an
// enemy's when the mod has no entry for it
bool enemy_entry(const std::string &name)
{
    const std::uint32_t at = entry_record(name);
    if (at == 0)
        return true;
    const unsigned kind = mod().memory.u32(at + 4) & 0xffu;
    return kind == 3 || kind == 4 || kind == 0xc;
}
namespace {

// The motion set the first object entry of the model `name` names (+ 0x28, as the mod's tool copies it:
// tools/mods/kh2_enemies.py's motion_set_of), `name`.mset when none does
std::string motion_set_name(const std::string &name)
{
    for (const EntryRecord &r : object_entries())
        if (entry_text(r, 8) == name)
            if (std::string set = entry_text(r, 0x28); !set.empty())
                return set;
    return name + ".mset";
}
} // namespace

const std::u8string &kind_archive(const KindFiles &k)
{
    static std::map<std::pair<std::string, std::string>, std::u8string> found; // looked for once (asked every frame)
    auto at = found.find({k.name, k.folder});
    if (at == found.end())
        at = found.emplace(std::make_pair(k.name, k.folder), kh2_file("obj/" + k.name + ".a.us", k.folder).u8string())
                 .first;
    return at->second;
}

const Command *command(int id)
{
    static std::map<int, Command> table;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> sys = read_file(kh2_file("03system.bin"));
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
                        std::uint32_t flags = 0;
                        std::memcpy(&number, c, 2);
                        std::memcpy(&argument, c + 4, 2);
                        std::memcpy(&text, c + 8, 4);
                        std::memcpy(&flags, c + 12, 4);
                        std::memcpy(&require, c + 34, 2);
                        table[number] = {argument, require, text, flags};
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

// KH2's battle constants (World::system_value): 03system.bin's "pref" entry is a BAR of its own whose "sstm" record
// holds them from its + 4's offset on (FUN_1403e9360 keeps that place: + 0x30 a hit's push back, 200); empty when the
// mod's file lacks them
const std::vector<std::uint8_t> &system_constants()
{
    static std::vector<std::uint8_t> values;
    static bool read = false;
    if (read)
        return values;
    read = true;
    const std::vector<std::uint8_t> sys = read_file(kh2_file("03system.bin"));
    try {
        for (const kh2::BarEntry &e : kh2::bar_entries(sys.data(), sys.size())) {
            if (e.name != "pref")
                continue;
            const std::uint8_t *pref = sys.data() + e.offset;
            for (const kh2::BarEntry &s : kh2::bar_entries(pref, e.size)) {
                if (s.name != "sstm" || s.size < 8)
                    continue;
                std::int32_t from = 0;
                std::memcpy(&from, pref + s.offset + 4, 4);
                if (from >= 0 && std::size_t(from) < s.size)
                    values.assign(pref + s.offset + from, pref + s.offset + s.size);
            }
        }
    } catch (const std::exception &x) {
        khdays_log("kh2 enemies: the mod's kh2/03system.bin cannot be read (%s)", x.what());
    }
    return values;
}

// The bone a character holds its weapon on: 03system.bin's "sklt" (a u32 kind, a u32 count, then 8 bytes each: a u32
// character -- an object entry's + 7, its weapon joint --, u16 the primary weapon's bone, u16 the second's: OpenKH's
// 03system page); -1 when it lacks it
int weapon_bone(int character)
{
    const std::vector<std::uint8_t> sys = read_file(kh2_file("03system.bin"));
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
namespace {

// The bone KH2's player holds his weapon on: his object entry's (P_EX100's) weapon joint on weapon_bone; his
// primary's bone (178), -1 when either file lacks it
int player_hand_bone()
{
    for (const EntryRecord &r : object_entries())
        if (entry_text(r, 8) == "P_EX100")
            return weapon_bone(r[7]);
    return -1;
}
} // namespace

const PlayerFiles &player_files()
{
    static PlayerFiles files;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> mdlx = read_file(kh2_file("obj/P_EX100.mdlx"));
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
        const std::vector<std::uint8_t> weapon = read_file(kh2_file("obj/W_EX010.mdlx"));
        try {
            if (!weapon.empty())
                files.weapon = std::make_shared<const kh2::MotionSkeleton>(
                    kh2::read_motion_skeleton(weapon.data(), weapon.size()));
        } catch (const std::exception &x) {
            khdays_log("kh2 enemies: the mod's kh2/obj/W_EX010.mdlx cannot be read (%s)", x.what());
        }
        files.hand = player_hand_bone();
        files.weapon_fx.name = "W_EX010";
        if (files.weapon && files.skeleton && files.hand >= 0 &&
            std::size_t(files.hand) < files.skeleton->joints.size())
            read_effects(files.weapon_fx, kh2_mod_of("obj/W_EX010.mdlx"));
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

const AttackParam &attack_param(int atkp)
{
    static std::map<int, AttackParam> power;
    static std::map<int, AttackParam> variants; // by number | sub-number << 16, those of a sub-number but 0
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> battle = read_file(kh2_file("00battle.bin"));
        try {
            for (const kh2::BarEntry &e : kh2::bar_entries(battle.data(), battle.size()))
                if (e.name == "atkp" && e.size >= 8) {
                    std::uint32_t count = 0;
                    std::memcpy(&count, battle.data() + e.offset + 4, 4);
                    for (std::uint32_t i = 0; i < count && 8 + 48 * (i + 1) <= e.size; ++i) {
                        const std::size_t at = e.offset + 8 + 48 * std::size_t(i);
                        const std::uint8_t *r = battle.data() + at;
                        std::uint16_t number = 0, value = 0;
                        std::int16_t back = 0, along = 0, reflect_back = 0;
                        std::uint32_t act = 0;
                        std::memcpy(&number, r + 2, 2);
                        std::memcpy(&value, r + 6, 2);
                        std::memcpy(&back, r + 0xc, 2);
                        std::memcpy(&along, r + 0xe, 2);
                        std::memcpy(&reflect_back, r + 0x16, 2);
                        std::memcpy(&act, r + 0x18, 4);
                        AttackParam p;
                        p.power = value;
                        p.team = r[8];
                        p.reaction = r[10];
                        p.hitback = back;
                        p.add_drive = r[0x29];
                        p.revenge = r[0x2a];
                        p.kind = r[4];
                        p.element = r[9];
                        p.blow_along = along;
                        p.refact_self = r[0x13];
                        p.refact_other = r[0x14];
                        p.reflect_motion = r[0x15];
                        p.reflect_hitback = reflect_back;
                        if (act != 0 && at + act < battle.size()) {
                            const char *name = reinterpret_cast<const char *>(battle.data() + at + act);
                            p.reflect_act.assign(name, strnlen(name, battle.size() - (at + act)));
                        }
                        p.reflect_range = r[0x22];
                        p.reflect_angle = std::int8_t(r[0x23]);
                        std::uint16_t sub = 0;
                        std::memcpy(&sub, r, 2);
                        if (sub != 0)
                            variants.emplace(int(number) | int(sub) << 16, p);
                        power.emplace(number, std::move(p));
                    }
                }
        } catch (const std::exception &x) {
            khdays_log("kh2 enemies: the mod's kh2/00battle.bin cannot be read (%s)", x.what());
        }
        if (power.empty())
            khdays_log("kh2 enemies: no KH2 attacks (the mod's kh2/00battle.bin): a player's blows in a reaction "
                       "command deal nothing");
    }
    static const AttackParam none;
    if ((atkp >> 16) != 0) // a variant (attack_key): its own record, else the number's first
        if (const auto v = variants.find(atkp); v != variants.end())
            return v->second;
    const auto it = power.find(atkp & 0xffff);
    return it != power.end() ? it->second : none;
}

// An attack of an enemy's motion as its owner's attack level makes it (YS::ATTACK_OBJ's constructor: its sheet's
// + 0x24c, 1:358, given ATTACKPARAM::Get as the sub-number, which finds the record of id << 16 | sub << 8 -- the
// record's + 0, a short, its sub-number times 256): the entry and its level's record as one key for attack_param, its
// number in the low 16 bits. Lexaeus raises his from 0 to 3, his strikes' 50 power to 600 and their reaction to a blow
// (atkp 1843's records 0, 256, 512, 768) -- Days's player takes his stand-in's damage all the same: the reaction and
// its push alone change
int attack_key(const Actor &a, int atkp)
{
    const std::uint32_t sheet = a.u32(0x5c0);
    const int level = sheet != 0 ? int(mod().memory.u32(sheet + 0x24c) & 0xffu) : 0;
    return level > 0 ? (atkp & 0xffff) | (level << 8) << 16 : atkp;
}

int attack_power(int atkp)
{
    return attack_param(atkp).power;
}

int attack_team(int atkp)
{
    return attack_param(atkp).team;
}

// Whether an attack gives what it strikes a reaction (its + 10: 0 and 255 none)
bool attack_reacts(int atkp)
{
    const int r = attack_param(atkp).reaction;
    return r != 0 && r != 255;
}

// KH2's damage patterns (00battle.bin's patn: patterns.h), read once from the mod's kh2/00battle.bin
const std::vector<DamagePattern> &damage_patterns()
{
    static std::vector<DamagePattern> patterns;
    static bool read = false;
    if (!read) {
        read = true;
        const std::vector<std::uint8_t> battle = read_file(kh2_file("00battle.bin"));
        try {
            for (const kh2::BarEntry &e : kh2::bar_entries(battle.data(), battle.size()))
                if (e.name == "patn")
                    patterns = parse_damage_patterns(battle.data() + e.offset, e.size);
        } catch (const std::exception &x) {
            khdays_log("kh2 enemies: the mod's kh2/00battle.bin cannot be read (%s)", x.what());
        }
        if (patterns.empty())
            khdays_log("kh2 enemies: no KH2 damage patterns (the mod's kh2/00battle.bin): every hit's reaction kept");
    }
    return patterns;
}

} // namespace khdays::kh2_enemies::shell
