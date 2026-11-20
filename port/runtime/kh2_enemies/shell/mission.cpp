// KH2's missions: their pieces, counts, gauges and timers.
#include "internal.h"

namespace khdays::kh2_enemies::shell {

// KH2's damage told to its mission (YS::UNITSCRIPT::DamageCallback, the PS3 build's: its trigger 6, given the object
// struck and the hit -- Luxord's time gauges drain there): the hit as YS::DAMAGE, its damage (+ 0x28, 2:46) in KH2's
// measure and its attack's record (+ 0x20) of type 0 (+ 4: no cure, 2:72)
void mission_damage(std::uint32_t object, std::int64_t damage)
{
    if (!kh2_mission || !kh2_mission->actor || kh2_mission->over || object == 0 || damage <= 0)
        return;
    static std::unique_ptr<Object> hit;
    static std::uint32_t params = 0;
    if (!hit)
        hit = std::make_unique<Object>(mod().memory, "a hit as a mission reads it");
    if (params == 0)
        params = mod().memory.add(48, "a hit's attack as a mission reads it");
    hit->set_u32(0x20, params);
    hit->set_u32(0x28, std::uint32_t(std::min<std::int64_t>(damage, 0x7fffffff)));
    const ai::Value values[2] = {ai::Value::of_address(object), ai::Value::of_address(hit->address())};
    kh2_mission->actor->event(6, values);
    if (trace_level() >= 2)
        khdays_log("kh2 enemies: mission %s told %08x's damage %lld", kh2_mission->name.c_str(), object,
                   static_cast<long long>(damage));
}

// The mission's piece of `kind` (4 a timer, 5 a counter) and number `index`, null when it has none
MissionPiece *mission_piece(Kh2Mission &m, int kind, int index)
{
    for (MissionPiece &p : m.pieces)
        if (p.kind == kind && p.index == index)
            return &p;
    return nullptr;
}

// The boss's KH2 mission (Kh2Mission) read from the mod (kh2/msn/<name>.bar) and set going: its timer and
// counter from its header, its script's main thread (trigger 3); none when it cannot be read
void start_mission(const std::string &name)
{
    const fs::path msn = kh2_file("msn/" + name + ".bar");
    const std::vector<std::uint8_t> bar = read_file(msn);
    const auto scripts = ai::scripts_in(bar.data(), bar.size());
    if (bar.empty() || scripts.empty()) {
        khdays_log("kh2 enemies: mission %s not read (the mod's kh2/msn/%s.bar, its script): none runs", name.c_str(),
                   name.c_str());
        return;
    }
    auto m = std::make_unique<Kh2Mission>();
    m->name = name;
    m->file = msn.string();
    const auto s32 = [&](std::size_t o) {
        std::int32_t v = 0;
        if (o + 4 <= bar.size())
            std::memcpy(&v, bar.data() + o, 4);
        return v;
    };
    for (const kh2::BarEntry &e : kh2::bar_entries(bar.data(), bar.size())) {
        if ((e.type & 0xffffu) != 2 || e.size < 0x1c)
            continue;
        std::size_t at = e.offset + 0x1c;
        while (at + 4 <= e.offset + e.size) {
            const int code = std::int16_t(bar[at] | (bar[at + 1] << 8)),
                      count = std::int16_t(bar[at + 2] | (bar[at + 3] << 8));
            if (code == 0 || count < 0)
                break;
            if ((code == 4 || code == 5 || code == 6) && count >= 3) {
                // MISSION2D::TABLE: its start, most and warning, then (YS::MISSION::init_mission2d) a byte its index,
                // one its place's side, a u16 its picture's entry + 1 (0 none), as the PS3 build reads them
                MissionPiece p;
                p.kind = code;
                const std::int32_t start = s32(at + 4), most = s32(at + 8);
                p.warning = s32(at + 12);
                p.start = start;
                if (count >= 4) {
                    const std::uint32_t picture = std::uint32_t(s32(at + 16));
                    p.index = int(picture >> 24);
                    p.align = int(picture >> 16 & 0xffu);
                    p.entry = int(picture & 0xffffu) - 1;
                }
                if (code == 4) { // dk::Timer: down from its start (setCountDown), else up to its most (setCountUp)
                    p.down = start > 0;
                    p.value = p.down ? start * 60 : 0;
                    p.most = (p.down ? start : most) * 60;
                } else { // MISSION2D::set_param: its count its start; its most's bit 30 shows "count / most"
                    p.with_most = (std::uint32_t(most) & 0x40000000u) != 0;
                    p.most = int(std::uint32_t(most) & ~0x40000000u);
                    p.value = start;
                    p.level = float(start); // a gauge's (MISSION_GAUGE::activate: its start as a float)
                }
                m->pieces.push_back(p);
            }
            at += 4 + 4 * std::size_t(count);
        }
        break;
    }
    m->script = std::make_shared<const ai::Script>(scripts.front().second);
    m->actor = std::make_unique<Actor>(m->script, mod().memory, mod().calls, "mission " + name);
    m->actor->world = &the_field();
    Kh2Mission &mm = *m;
    kh2_mission = std::move(m);
    ++kh2_mission_serial;
    mm.actor->machine().start_main(3);
    std::string pieces;
    for (const MissionPiece &p : mm.pieces)
        pieces += p.kind == 4   ? " (a timer of " + std::to_string(p.most / 60) + " s)"
                  : p.kind == 6 ? " (a gauge at " + std::to_string(p.value) + " of " + std::to_string(p.most) + ")"
                                : " (a count of " + std::to_string(p.value) + ")";
    khdays_log("kh2 enemies: mission %s set going%s", name.c_str(), pieces.c_str());
}
} // namespace khdays::kh2_enemies::shell

using namespace khdays::kh2_enemies;
using namespace khdays::kh2_enemies::shell;

bool khdays::kh2_enemies::mission_view(MissionView &out)
{
    if (!kh2_mission)
        return false;
    const Kh2Mission &m = *kh2_mission;
    out.file = m.file;
    out.serial = kh2_mission_serial;
    out.ticks = m.ticks;
    out.shown = m.shown && !m.over;
    out.pieces = m.pieces;
    return true;
}
