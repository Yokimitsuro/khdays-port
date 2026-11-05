// A KH2 model's own motions: its .motions.txt read, and its slots played on the game's animations.

#include "model_parts.h"

namespace khdays::model_parts {

// A KH2 bone's frame (centimetres) in metres, as the model's joints are
Mat in_metres(Mat g)
{
    g[12] *= 0.01F;
    g[13] *= 0.01F;
    g[14] *= 0.01F;
    return g;
}

// A KH2 mset slot made for the model's own bones (an `own` mset's, or every slot without a `skeleton`)
bool own_slot(const Model &m, std::size_t slot)
{
    if (!m.kh2_retarget)
        return true;
    for (const auto &[from, to] : m.kh2_own_slots)
        if (slot >= from && slot < to)
            return true;
    return false;
}

// The skeleton a slot's motion plays on, and its pose given to the model's bones (retargeted unless its own)
const khdays::assets::kh2::MotionSkeleton &skeleton_of(const Model &m, std::size_t slot)
{
    return own_slot(m, slot) ? *m.kh2_own_skeleton : *m.kh2_skeleton;
}

void pose_on_model(const Model &m, std::size_t slot, const std::vector<khdays::assets::kh2::Mat4> &played,
                   std::vector<khdays::assets::kh2::Mat4> &out)
{
    if (own_slot(m, slot))
        out = played;
    else
        m.kh2_retarget->apply(played, out);
}

// A track's player for a slot, made anew when the slot plays on another skeleton than its last
void ready_player(const Model &m, Kh2Track &track, std::size_t slot)
{
    const khdays::assets::kh2::MotionSkeleton &skeleton = skeleton_of(m, slot);
    if (!track.player || track.skeleton != &skeleton) {
        track.player = std::make_unique<khdays::assets::kh2::MotionPlayer>(skeleton);
        track.skeleton = &skeleton;
    }
}

// A KH2 model's own motions: `<model>.motions.txt` beside the .mdlx (docs/KH2_MODELS.md, "Motions"): the mset, the
// sections of the game's animations that play its slots, and what its other lines ask for. A game animation without a
// line, or a slot with no motion, keeps the game's.
void load_kh2_motions(Model &model, const std::string &path)
{
    namespace kh2 = khdays::assets::kh2;
    const auto file = u8(path);
    if (file.extension() != ".mdlx" && file.extension() != ".MDLX")
        return;
    const auto list = file.parent_path() / (file.stem().u8string() + u8".motions.txt");
    std::ifstream in(list);
    if (!in)
        return;
    const auto say = [&](const std::string &what) {
        std::fprintf(stderr, "models: %s: %s\n", path.c_str(), what.c_str());
        khdays_log("models: %s: %s", path.c_str(), what.c_str());
    };
    std::string mset, line;
    std::map<std::string, std::vector<Kh2Section>> sections;
    std::vector<std::pair<std::string, int>> weapons;           // the game's joint, the KH2 bone
    std::map<std::string, std::array<float, 3>> weapon_degrees; // its `rotate`, by the game's joint
    std::vector<DsPoseRule> ds_rules;
    std::vector<HandRule> hand_rules;
    std::vector<HandPoseRule> hand_poses;
    std::vector<HandBodyRule> hand_body;
    std::string skeleton;                   // the mdlx the mset was made for, when not this one
    std::vector<std::pair<int, int>> bones; // this model's bone, the skeleton's
    std::vector<khdays::assets::kh2::Retarget::Limb> limbs;
    struct Extra {
        std::string file,
            entry; // `motions <file> [<entry>] at <slot> [own]`: an entry of a BAR of sets (GAMEOVER.mset)
        std::size_t at = 0;
        bool own = false; // made for this model's bones, not the `skeleton`'s
    };
    std::vector<Extra> extras;
    bool mset_own = false; // `mset <file> own`
    struct Carry {         // `carry <bone> <by bone> at <slot>`
        int bone = 0, by = 0;
        std::size_t at = 0;
    };
    std::vector<Carry> carries;
    std::string weapon_mset, weapon_mdlx; // `weapon_motions <mset> <mdlx> <bone>`
    int weapon_bone = -1;
    const auto number = [](const std::string &s, float &out) {
        char *end = nullptr;
        out = std::strtof(s.c_str(), &end);
        return end != s.c_str() && *end == '\0' && out >= 0.0F;
    };
    // a bone number or a range of them (`171-215`)
    const auto span = [](const std::string &s, int &from, int &to) {
        char *end = nullptr;
        const long a = std::strtol(s.c_str(), &end, 10);
        long b = a;
        if (end != s.c_str() && *end == '-') {
            const char *rest = end + 1;
            b = std::strtol(rest, &end, 10);
            if (end == rest)
                return false;
        }
        if (end == s.c_str() || *end != '\0' || a < 0 || b < a)
            return false;
        from = static_cast<int>(a);
        to = static_cast<int>(b);
        return true;
    };
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        std::istringstream words(line);
        std::string a, b;
        if (!(words >> a >> b))
            continue;
        // `walk|camp <game animation>[@<frame>] <slot> ...`: a line of its own, kept as the animation's under its own
        // name (`<animation>#walk`, `#camp`): played while the port's walk is on, or in the camp menu
        bool walk_line = false;
        if (a == "walk" || a == "camp") {
            std::string slot;
            if (!(words >> slot)) {
                say("\"" + line + "\": write `" + a + " <game animation> <slot> ...`; the line is ignored");
                continue;
            }
            walk_line = a == "walk";
            const auto at = b.find('@');
            a = b.substr(0, at) + "#" + a + (at != std::string::npos ? b.substr(at) : std::string());
            b = slot;
        }
        if (a == "mset") {
            std::string w;
            mset = b;
            mset_own = static_cast<bool>(words >> w) && w == "own";
            if (!w.empty() && w != "own")
                say("\"" + line + "\": write `mset <file> [own]`; read as `mset " + b + "`");
        } else if (a == "carry") { // carry <bone> <by bone> at <slot>
            Carry c;
            std::string by, w, at;
            int to = 0, slot = 0;
            const bool ok = static_cast<bool>(words >> by >> w >> at) && w == "at" && span(b, c.bone, to) &&
                            to == c.bone && span(by, c.by, to) && to == c.by && span(at, slot, to) && to == slot;
            c.at = static_cast<std::size_t>(slot);
            if (ok)
                carries.push_back(c);
            else
                say("\"" + line +
                    "\": write `carry <bone> <by bone> at <slot>` (the slot an `own` mset's); the line is ignored");
        } else if (a == "weapon_motions") { // weapon_motions <mset> <mdlx> <bone>
            std::string mdlx, bone;
            char *end = nullptr;
            if (!(words >> mdlx >> bone) || (weapon_bone = static_cast<int>(std::strtol(bone.c_str(), &end, 10)),
                                             end == bone.c_str() || *end || weapon_bone < 0)) {
                say("\"" + line +
                    "\": write `weapon_motions <mset> <mdlx> <bone>` (the bone: the weapon itself); "
                    "the line is ignored");
                weapon_bone = -1;
            } else {
                weapon_mset = b;
                weapon_mdlx = mdlx;
            }
        } else if (a == "motions") { // motions <file> [<entry>] at <slot> [own]
            Extra x{b, {}, 0, false};
            std::string w, v, o;
            if (words >> w && w != "at") {
                x.entry = w;
                words >> w;
            }
            char *end = nullptr;
            if (w != "at" || !(words >> v) || (x.at = std::strtoul(v.c_str(), &end, 10), end == v.c_str() || *end) ||
                (words >> o && o != "own"))
                say("\"" + line + "\": write `motions <file> [<entry>] at <first slot> [own]`; the line is ignored");
            else {
                x.own = o == "own";
                extras.push_back(x);
            }
        } else if (a == "root_motion") {
            model.kh2_root_motion = b == "on";
        } else if (a == "idle") { // idle <game animation>...
            std::string w;
            model.kh2_idle.push_back(b);
            while (words >> w)
                model.kh2_idle.push_back(w);
        } else if (a == "landing") {
            model.kh2_landing = b;
        } else if (a == "air") { // air <game animation> <slot> [land <slot>] [finish <slot>...] [blend <frames>]
            Kh2Section air, land;
            std::string slot, w, v;
            bool ok = static_cast<bool>(words >> slot);
            float number_of = 0.0F;
            ok = ok && number(slot, number_of) && number_of >= 0.0F;
            air.slot = static_cast<std::size_t>(number_of);
            air.realtime = true;
            bool has_land = false, finishing = false;
            while (ok && words >> w) {
                float n = 0.0F;
                if (w == "land" && words >> v && number(v, n) && n >= 0.0F) {
                    land.slot = static_cast<std::size_t>(n);
                    has_land = true;
                    finishing = false;
                } else if (w == "blend" && words >> v && number(v, n)) {
                    air.blend = land.blend = n;
                    finishing = false;
                } else if (w == "finish") {
                    land.finish = finishing = true;
                } else if (finishing && number(w, n) && n >= 0.0F) {
                    land.then.push_back(static_cast<std::size_t>(n));
                } else {
                    ok = false;
                }
            }
            if (!ok || (land.finish && !has_land)) {
                say("\"" + line +
                    "\": write `air <game animation> <slot> [land <slot>] [finish <slot>...] [blend <frames>]` "
                    "(`finish` after a `land`); the line is ignored");
                continue;
            }
            Kh2Air &entry = model.kh2_air[b];
            entry.air = {air};
            entry.land.clear();
            if (has_land)
                entry.land.push_back(land);
        } else if (a == "ik_adjust") {
            model.kh2_ik_adjust = b != "off";
        } else if (a == "skeleton") {
            skeleton = b;
        } else if (a == "bone") {
            std::string c;
            int t0 = 0, t1 = 0, s0 = 0, s1 = 0;
            if (!(words >> c) || !span(b, t0, t1) || !span(c, s0, s1) || t1 - t0 != s1 - s0) {
                say("\"" + line +
                    "\": write `bone <this model's bone> <the skeleton's bone>`, or two ranges as long (`bone 171-215 "
                    "115-159`); the line is ignored");
                continue;
            }
            for (int i = 0; i <= t1 - t0; ++i)
                bones.emplace_back(t0 + i, s0 + i);
        } else if (a == "ik") {
            std::string c, d, r;
            float u = 0.0F, l = 0.0F, e = 0.0F, reach = -1.0F;
            bool ok = (words >> c >> d) && number(b, u) && number(c, l) && number(d, e), lengths = false;
            while (ok && words >> r) // [<bone it reaches with>] [lengths]
                if (r == "lengths")
                    lengths = true;
                else
                    ok = reach < 0.0F && number(r, reach);
            if (!ok) {
                say("\"" + line +
                    "\": write `ik <upper bone> <lower bone> <end bone> [<bone it reaches with>] [lengths]`; the line "
                    "is ignored");
                continue;
            }
            limbs.push_back(
                {static_cast<int>(u), static_cast<int>(l), static_cast<int>(e), static_cast<int>(reach), lengths});
        } else if (a == "weapon") {
            std::string bone, r;
            float n = 0.0F;
            std::array<float, 3> by{};
            if (!(words >> bone) || !number(bone, n) ||
                (words >> r && !(r == "rotate" && words >> by[0] >> by[1] >> by[2]))) {
                say("\"" + line +
                    "\": write `weapon <the game's joint> <KH2 bone> [rotate <x> <y> <z>]`; the line is ignored");
                continue;
            }
            weapons.emplace_back(b, static_cast<int>(n));
            weapon_degrees[b] = by;
        } else if (a == "face") {
            std::string w;
            bool ok = true;
            if (b == "talk") {
                ok = static_cast<bool>(words >> model.face.talk);
            } else if (b == "speaker") {
                int from = 0, to = 0;
                while (ok && words >> w) {
                    ok = span(w, from, to);
                    for (int x = from; ok && x <= to; ++x)
                        model.face.speakers.push_back(x);
                }
            } else if (b == "ds") {
                int slot = 0;
                ok = words >> w >> slot && slot >= 0;
                if (ok)
                    model.face.ds.emplace_back(w, slot);
            } else if (b == "portrait") {
                FacePortrait shown;
                ok = static_cast<bool>(words >> shown.portrait);
                while (ok && words >> w) {
                    if (w == "talk") {
                        ok = words >> shown.talk && shown.talk >= 0;
                        continue;
                    }
                    char *end = nullptr;
                    const long n = std::strtol(w.c_str(), &end, 10);
                    ok = w == "last" || (end != w.c_str() && *end == '\0' && n >= 0);
                    shown.slots.push_back(w == "last" ? -2 : static_cast<int>(n));
                }
                ok = ok && (!shown.slots.empty() || shown.talk >= 0);
                if (ok)
                    model.face.portraits.push_back(std::move(shown));
            } else if (b == "closed") {
                int from = 0, to = 0;
                ok = false;
                while (words >> w) {
                    ok = span(w, from, to);
                    if (!ok)
                        break;
                    for (int x = from; x <= to; ++x)
                        model.face.closed.push_back(x);
                }
            } else if (b == "ranges") {
                ok = static_cast<bool>(words >> w) && (w == "on" || w == "off");
                model.face.ranges = w != "off";
            } else {
                ok = false;
            }
            if (!ok)
                say("\"" + line +
                    "\": write `face talk <slot>`, `face speaker <portrait>...`, `face ds <pattern animation> <slot>`, "
                    "`face portrait <portrait> [last|<slot>...] [talk <slot>]`, `face closed <slot>...` or `face ranges on|off`; the line is "
                    "ignored");
        } else if (a == "hand") {
            // hand <hand bone> <forearm bone> <finger bones> back <x> <y> <z>
            HandRule rule;
            int to = 0, from = 0, last = 0;
            std::string fingers, w;
            const bool ok = span(b, rule.hand, to) && to == rule.hand &&
                            static_cast<bool>(words >> rule.forearm >> fingers >> w) && w == "back" &&
                            static_cast<bool>(words >> rule.back[0] >> rule.back[1] >> rule.back[2]) &&
                            span(fingers, from, last);
            if (!ok) {
                say("\"" + line +
                    "\": write `hand <hand bone> <forearm bone> <finger bones> back <x> <y> <z>`; the line is ignored");
                continue;
            }
            for (int x = from; x <= last; ++x)
                rule.fingers.push_back(x);
            hand_rules.push_back(std::move(rule));
        } else if (a == "hand_pose") {
            // hand_pose relaxed|open|fist|flat|grip rotate <x> <y> <z> <bones>
            HandPoseRule rule;
            rule.pose = hand_pose_index(b);
            std::string w;
            bool ok = rule.pose < khdays::models::kHandPoses && static_cast<bool>(words >> w) && w == "rotate" &&
                      static_cast<bool>(words >> rule.degrees[0] >> rule.degrees[1] >> rule.degrees[2]);
            while (ok && words >> w) {
                int from = 0, to = 0;
                ok = span(w, from, to);
                for (int x = from; ok && x <= to; ++x)
                    rule.bones.push_back(x);
            }
            if (!ok || rule.bones.empty()) {
                say("\"" + line +
                    "\": write `hand_pose relaxed|open|fist|flat|grip rotate <x> <y> <z> <bones>`; the line is ignored");
                continue;
            }
            hand_poses.push_back(std::move(rule));
        } else if (a == "hand_bend" || a == "hand_fist") {
            // hand_bend <from degrees> <to degrees>: the open pose's share of the wrist's bend;
            // hand_fist <from> <to>: the fist's share of the hand's speed against the body
            std::string w;
            float from = 0.0F, to = 0.0F;
            if (number(b, from) && (words >> w) && number(w, to)) {
                (a == "hand_bend" ? model.hand_bend_from : model.hand_fist_from) = from;
                (a == "hand_bend" ? model.hand_bend_to : model.hand_fist_to) = to;
            } else {
                say("\"" + line + "\": write `" + a + " <from> <to>`; the line is ignored");
            }
        } else if (a == "hand_grip_fade" || a == "hand_lag" || a == "hand_finger_link") {
            if (!number(b, a == "hand_lag"           ? model.hand_lag
                           : a == "hand_finger_link" ? model.hand_finger_link
                                                     : model.hand_grip_fade))
                say("\"" + line + "\": write `" + a + " <number>`; the line is ignored");
        } else if (a == "hand_walk") {
            // hand_walk <body bone> <from> <to> <share>: the fist's share of how fast that bone travels
            int bone = 0, to_bone = 0;
            float from = 0.0F, to = 0.0F, share = 0.0F;
            if (span(b, bone, to_bone) && (words >> from >> to >> share)) {
                model.hand_walk_joint = bone;
                model.hand_walk_from = from;
                model.hand_walk_to = to;
                model.hand_walk_share = share;
            } else {
                say("\"" + line + "\": write `hand_walk <body bone> <from> <to> <share>`; the line is ignored");
            }
        } else if (a == "hand_contact") {
            // hand_contact <near> <far> <most speed>: the palm's distance to a surface for its flat pose, all to none
            std::string far, fast;
            float near = 0.0F, f = 0.0F, v = 0.0F;
            if (number(b, near) && (words >> far >> fast) && number(far, f) && number(fast, v)) {
                model.hand_contact_near = near;
                model.hand_contact_far = f;
                model.hand_contact_speed = v;
            } else {
                say("\"" + line + "\": write `hand_contact <near> <far> <most speed>`; the line is ignored");
            }
        } else if (a == "hand_body") {
            // hand_body <bones> axis <from bone> <to bone> [margin <cm>]
            HandBodyRule rule;
            std::string w = b;
            bool ok = true;
            do {
                if (w == "axis")
                    break;
                int from = 0, to = 0;
                ok = span(w, from, to);
                for (int x = from; ok && x <= to; ++x)
                    rule.bones.push_back(x);
            } while (ok && words >> w);
            ok = ok && w == "axis" && !rule.bones.empty() && static_cast<bool>(words >> rule.from >> rule.to);
            if (ok && words >> w)
                ok = w == "margin" && static_cast<bool>(words >> rule.margin);
            if (!ok) {
                say("\"" + line +
                    "\": write `hand_body <bones> axis <from bone> <to bone> [margin <cm>]`; the line is ignored");
                continue;
            }
            hand_body.push_back(std::move(rule));
        } else if (a == "hand_anim") {
            // hand_anim relaxed|open|fist|flat|grip left|right|both <game animation>...
            const std::size_t pose = hand_pose_index(b);
            std::string side, name;
            if (pose >= khdays::models::kHandPoses || !(words >> side) ||
                (side != "left" && side != "right" && side != "both") || !(words >> name)) {
                say("\"" + line +
                    "\": write `hand_anim relaxed|open|fist|flat|grip left|right|both <game animation>...`; the line is ignored");
                continue;
            }
            do {
                auto &sides = model.hand_anim.try_emplace(name, std::array<int, 2>{-1, -1}).first->second;
                if (side != "right")
                    sides[0] = static_cast<int>(pose);
                if (side != "left")
                    sides[1] = static_cast<int>(pose);
            } while (words >> name);
        } else if (a == "ds_pose") {
            // ds_pose <slot>[@<frame>] | rotate <x> <y> <z>, then bones (`135-156`), then [for <animation>...]
            DsPoseRule rule;
            bool ok = true;
            if (b == "rotate") {
                ok = static_cast<bool>(words >> rule.degrees[0] >> rule.degrees[1] >> rule.degrees[2]);
            } else {
                const auto at = b.find('@');
                float slot = 0.0F;
                ok = number(b.substr(0, at), slot) && (at == std::string::npos || number(b.substr(at + 1), rule.frame));
                rule.slot = static_cast<int>(slot);
            }
            bool names = false;
            std::string w;
            while (ok && words >> w) {
                int from = 0, to = 0;
                if (names)
                    rule.animations.push_back(w);
                else if (w == "for")
                    names = true;
                else if (span(w, from, to))
                    for (int x = from; x <= to; ++x)
                        rule.bones.push_back(x);
                else
                    ok = false;
            }
            if (!ok || rule.bones.empty() || (names && rule.animations.empty())) {
                say("\"" + line +
                    "\": write `ds_pose <mset slot>[@<frame>] <bones> [for <game animation>...]` or `ds_pose "
                    "rotate <x> <y> <z> <bones> [for ...]`; the line is ignored");
                continue;
            }
            ds_rules.push_back(std::move(rule));
        } else {
            Kh2Section section;
            const auto at = a.find('@');
            if (at != std::string::npos) {
                if (!number(a.substr(at + 1), section.from)) {
                    say("\"" + line +
                        "\": the frame after @ is not a number; write `<game animation>@<BCA frame> <slot>`; the line is ignored");
                    continue;
                }
                a.erase(at);
            }
            if (b == "blend") { // <game animation> blend <frames>: its fade into an idle, its own animation kept
                std::string v;
                float frames = 0.0F;
                if (at != std::string::npos || !(words >> v) || !number(v, frames) || frames < 0.0F) {
                    say("\"" + line + "\": write `<game animation> blend <frames>`; the line is ignored");
                    continue;
                }
                model.kh2_idle_blend[a] = frames;
                continue;
            }
            char *end = nullptr;
            section.slot = std::strtoul(b.c_str(), &end, 10);
            if (end == b.c_str() || *end != '\0') {
                say("\"" + line +
                    "\": the slot is not a number; write `<game animation> <mset slot>`; the line is ignored");
                continue;
            }
            std::string c, d;
            bool bad = false, to = false;
            std::vector<std::string> rest;
            while (words >> c)
                rest.push_back(c);
            std::istringstream options;
            for (std::size_t r = 0; r < rest.size() && !bad; ++r) {
                if (rest[r] != "finish")
                    continue;
                section.finish = true; // finish [<slot>...]: its slots taken out of the options
                std::size_t q = r + 1;
                for (; q < rest.size(); ++q) {
                    char *slot_end = nullptr;
                    const unsigned long slot = std::strtoul(rest[q].c_str(), &slot_end, 10);
                    if (slot_end == rest[q].c_str() || *slot_end != '\0')
                        break;
                    section.then.push_back(slot);
                }
                rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(r),
                           rest.begin() + static_cast<std::ptrdiff_t>(q));
                break;
            }
            {
                std::string joined;
                for (const std::string &w : rest)
                    joined += w + " ";
                options.str(joined);
            }
            while (!bad && options >> c) {
                if (c == "follow")
                    section.follow = true;
                else if (c == "cut")
                    section.cut = true;
                else if (c == "rotate") {
                    std::array<float, 3> degrees{};
                    bad = !(options >> degrees[0] >> degrees[1] >> degrees[2]);
                    section.turn = turn_of(degrees);
                } else if (c == "start_loop")
                    section.start_loop = true;
                else if (c == "loop_blend")
                    bad = !(options >> d && number(d, section.loop_blend));
                else if (c == "offset") {
                    bad = !(options >> section.offset[0] >> section.offset[1] >> section.offset[2]);
                    section.offset_to = section.offset;
                } else if (c == "to")
                    bad = !(options >> section.offset_to[0] >> section.offset_to[1] >> section.offset_to[2]) || to;
                else
                    bad = !(c == "blend" && options >> d && number(d, section.blend));
                to = to || c == "to";
            }
            if (bad) {
                say("\"" + line +
                    "\": after the slot write `blend <frames>`, `loop_blend <frames>`, `follow`, `start_loop`, `cut`, "
                    "`rotate <x> <y> <z>` (degrees), `finish [<slot>...]` or `offset <x> <y> <z> [to <x> <y> <z>]` (centimetres); the line is ignored");
                continue;
            }
            section.realtime = section.realtime || walk_line; // a walk at its own pace, not the game's slowed run
            auto &parts = sections[a];
            std::erase_if(parts, [&](const Kh2Section &s) { return s.from == section.from; });
            parts.push_back(section);
        }
    }
    // each joint's KH2 bone: the importer names them kh2_<bone>
    model.kh2_bone.assign(model.joints.size(), -1);
    for (std::size_t j = 0; j < model.joints.size(); ++j) {
        const std::string &n = model.joints[j].name;
        if (n.rfind("kh2_", 0) == 0)
            model.kh2_bone[j] = std::atoi(n.c_str() + 4);
    }
    // what the game attaches to a joint of its model (its weapons), on a KH2 bone's joint instead
    for (const auto &[node, bone] : weapons) {
        const auto g = std::find(model.node_names.begin(), model.node_names.end(), node);
        const auto j = std::find(model.kh2_bone.begin(), model.kh2_bone.end(), bone);
        if (g == model.node_names.end() || j == model.kh2_bone.end()) {
            say("weapon " + node + " " + std::to_string(bone) + ": " +
                (g == model.node_names.end() ? "the game's model has no joint " + node
                                             : "the model has no bone " + std::to_string(bone)) +
                "; check the `weapon` line; the game's place is kept");
            continue;
        }
        model.kh2_weapon.resize(model.node_names.size(), -1);
        model.kh2_weapon_turn.resize(model.node_names.size(), kIdentity);
        model.kh2_weapon[static_cast<std::size_t>(g - model.node_names.begin())] =
            static_cast<int>(j - model.kh2_bone.begin());
        model.kh2_weapon_turn[static_cast<std::size_t>(g - model.node_names.begin())] = turn_of(weapon_degrees[node]);
    }
    // a `place`d joint holding its own node's weapon: turned so the line's frame on it is the node's own (its place
    // already is), so a fade between a KH2 motion and the game's animation turns the weapon the short way
    for (std::size_t g = 0; g < model.kh2_weapon.size() && g < model.node_rest_inverse.size(); ++g) {
        if (model.kh2_weapon[g] < 0)
            continue;
        Joint &joint = model.joints[static_cast<std::size_t>(model.kh2_weapon[g])];
        if (joint.node != static_cast<int>(g) || !joint.placed)
            continue;
        // the pose turns it by the node's turn from rest (d) times this: d C W T = d N, N the node's rest turn
        const Mat rest = axes_of(mul(joint.bind, inverse(joint.inverse_bind)));
        const Mat node_rest = axes_of(inverse(model.node_rest_inverse[g]));
        joint.correction = mul(node_rest, inverse(mul(rest, model.kh2_weapon_turn[g])));
        joint.to_node = mul(inverse(axes_of(mul(joint.correction, rest))), node_rest);
    }
    if (mset.empty()) {
        if (!sections.empty())
            say("motions.txt names no mset (a line `mset <file>.mset`); the game's animations are played");
        resolve_ds_poses(model, ds_rules, say);
        resolve_hands(model, hand_rules, hand_poses, hand_body, say);
        return;
    }
    try {
        const auto read = [](const std::filesystem::path &p) {
            std::ifstream f(p, std::ios::binary);
            if (!f) {
                const auto name = p.filename().u8string();
                throw std::runtime_error("cannot open " + std::string(name.begin(), name.end()));
            }
            return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        };
        const auto mdlx = read(file);
        const auto set = read(file.parent_path() / u8(mset));
        auto own = std::make_shared<kh2::MotionSkeleton>(kh2::read_motion_skeleton(mdlx.data(), mdlx.size()));
        model.kh2_own_skeleton = own;
        model.kh2_own_slots.clear();
        auto motions = std::make_shared<kh2::MotionSet>(set.data(), set.size());
        if (mset_own)
            model.kh2_own_slots.emplace_back(0, motions->slots());
        // `motions`: another mset's slots from a slot on; with an entry, that entry of it, a set of its own
        for (const Extra &x : extras) {
            const auto bytes = read(file.parent_path() / u8(x.file));
            const std::size_t before = motions->slots();
            if (x.entry.empty()) {
                motions->add(x.at, kh2::MotionSet(bytes.data(), bytes.size()));
            } else {
                bool found = false;
                for (const kh2::BarEntry &e : kh2::bar_entries(bytes.data(), bytes.size()))
                    if (!found && e.name == x.entry && kh2::is_bar(bytes.data(), bytes.size(), e.offset)) {
                        motions->add(x.at, kh2::MotionSet(bytes.data() + e.offset, e.size));
                        found = true;
                    }
                if (!found)
                    throw std::runtime_error(x.file + " has no set named " + x.entry);
            }
            if (x.own && motions->slots() > before)
                model.kh2_own_slots.emplace_back(x.at, motions->slots());
        }
        if (skeleton.empty()) {
            model.kh2_skeleton = own;
            if (!carries.empty())
                say("`carry` places a bone in the `skeleton`'s motions, and there is no `skeleton` line; add one or take it off; ignored");
        } else { // the mset plays on its own model's bones, given to this one's
            const auto other = read(file.parent_path() / u8(skeleton));
            model.kh2_skeleton =
                std::make_shared<kh2::MotionSkeleton>(kh2::read_motion_skeleton(other.data(), other.size()));
            std::vector<int> source_of(own->joints.size(), -1);
            for (const auto &[t, s] : bones) {
                if (static_cast<std::size_t>(t) >= source_of.size())
                    throw std::runtime_error("bone " + std::to_string(t) + ": the model has " +
                                             std::to_string(own->joints.size()) + " bones");
                source_of[static_cast<std::size_t>(t)] = s;
            }
            auto retarget = std::make_shared<kh2::Retarget>(*model.kh2_skeleton, *own, source_of, limbs);
            // `carry`: the bone on the other where an `own` slot's first frame has it (both unscaled)
            for (const Carry &c : carries) {
                const kh2::Motion *motion = motions->motion(c.at);
                const bool in_own = std::any_of(model.kh2_own_slots.begin(), model.kh2_own_slots.end(),
                                                [&](const auto &r) { return c.at >= r.first && c.at < r.second; });
                const std::size_t n = own->joints.size();
                if (motion == nullptr || !in_own || static_cast<std::size_t>(c.bone) >= n ||
                    static_cast<std::size_t>(c.by) >= n) {
                    say("carry " + std::to_string(c.bone) + " " + std::to_string(c.by) + " at " + std::to_string(c.at) +
                        ": " +
                        (motion == nullptr || !in_own ? "no motion of an `own` mset in that slot"
                                                      : "the model has " + std::to_string(n) + " bones") +
                        "; check the line; it is ignored");
                    continue;
                }
                kh2::MotionPlayer player(*own);
                player.evaluate(*motion, 0.0F, kIdentity, nullptr);
                const auto unscaled = [](Mat m) {
                    for (std::size_t col = 0; col < 3; ++col) {
                        const float l = length({m[col * 4], m[col * 4 + 1], m[col * 4 + 2]});
                        for (std::size_t r = 0; r < 3 && l > 0.0F; ++r)
                            m[col * 4 + r] /= l;
                    }
                    return m;
                };
                const auto &g = player.joints();
                try {
                    retarget->carry(c.bone, c.by,
                                    mul(inverse(unscaled(g[static_cast<std::size_t>(c.by)])),
                                        unscaled(g[static_cast<std::size_t>(c.bone)])));
                } catch (const std::runtime_error &e) {
                    say(std::string(e.what()) + "; check its bones; the `carry` line is ignored");
                }
            }
            model.kh2_retarget = std::move(retarget);
        }
        model.kh2_motions = std::move(motions);
        // `weapon_motions`: the weapon's mset and skeleton, and its held pose (slot 0's first frame)
        if (!weapon_mset.empty()) {
            const auto wset = read(file.parent_path() / u8(weapon_mset));
            const auto wmdl = read(file.parent_path() / u8(weapon_mdlx));
            auto skeleton_of =
                std::make_shared<kh2::MotionSkeleton>(kh2::read_motion_skeleton(wmdl.data(), wmdl.size()));
            auto weapon = std::make_shared<kh2::MotionSet>(wset.data(), wset.size());
            if (static_cast<std::size_t>(weapon_bone) >= skeleton_of->joints.size())
                throw std::runtime_error(weapon_mdlx + " has " + std::to_string(skeleton_of->joints.size()) +
                                         " bones, no bone " + std::to_string(weapon_bone));
            if (weapon->motion(0) == nullptr)
                throw std::runtime_error(weapon_mset + " has no motion in slot 0 (the held pose)");
            kh2::MotionPlayer held(*skeleton_of);
            held.evaluate(*weapon->motion(0), 0.0F, kIdentity, nullptr);
            model.weapon_rest.clear();
            for (const auto &g : held.joints())
                model.weapon_rest.push_back(in_metres(g));
            model.weapon_motions = std::move(weapon);
            model.weapon_skeleton = std::move(skeleton_of);
            model.weapon_blade = weapon_bone;
        }
    } catch (const std::exception &e) {
        say(std::string("its motions cannot be read (") + e.what() +
            "); check the files motions.txt names; the game's animations are played");
        model.kh2_motions.reset();
        model.kh2_retarget.reset();
        model.kh2_own_slots.clear();
        model.weapon_motions.reset();
        resolve_ds_poses(model, ds_rules, say);
        resolve_hands(model, hand_rules, hand_poses, hand_body, say);
        return;
    }
    for (auto &[name, parts] : sections) {
        std::erase_if(parts, [&](const Kh2Section &s) {
            if (model.kh2_motions->motion(s.slot) != nullptr)
                return false;
            say(name + ": mset slot " + std::to_string(s.slot) +
                " holds no motion it plays; name another in motions.txt; the game's is kept");
            return true;
        });
        for (Kh2Section &s : parts) {
            std::erase_if(s.then, [&](std::size_t slot) {
                if (model.kh2_motions->motion(slot) != nullptr)
                    return false;
                say(name + ": `finish` slot " + std::to_string(slot) +
                    " holds no motion it plays; name another in motions.txt; left out");
                return true;
            });
            if (s.finish && model.kh2_idle.empty())
                say(name +
                    ": `finish` with no `idle` line (the game's idles it plays out into): it never plays out; add one");
        }
        if (parts.empty())
            continue;
        std::sort(parts.begin(), parts.end(), [](const Kh2Section &x, const Kh2Section &y) { return x.from < y.from; });
        if (parts.front().from > 0.0F)
            say(name + ": no section from frame 0; the game's animation plays before frame " +
                std::to_string(parts.front().from));
        model.kh2_sections[name] = std::move(parts);
    }
    for (auto it = model.kh2_air.begin(); it != model.kh2_air.end();) {
        Kh2Air &air = it->second;
        const auto plays = [&](std::size_t slot) { return model.kh2_motions->motion(slot) != nullptr; };
        if (!plays(air.air.front().slot)) {
            say("air " + it->first + ": mset slot " + std::to_string(air.air.front().slot) +
                " holds no motion it plays; name another; the line is ignored");
            it = model.kh2_air.erase(it);
            continue;
        }
        if (!air.land.empty() && !plays(air.land.front().slot)) {
            say("air " + it->first + ": its land slot " + std::to_string(air.land.front().slot) +
                " holds no motion it plays; name another; landing keeps the landing's");
            air.land.clear();
        }
        if (!air.land.empty())
            std::erase_if(air.land.front().then, [&](std::size_t slot) { return !plays(slot); });
        if (!air.land.empty() && model.kh2_landing.empty())
            say("air " + it->first +
                ": `land` with no `landing` line (the game's landing animation): never played; add one");
        ++it;
    }
    resolve_ds_poses(model, ds_rules, say);
    resolve_hands(model, hand_rules, hand_poses, hand_body, say);
    khdays_log("models: %s: %zu of the game's animations play KH2 motions from %s (root motion %s)", path.c_str(),
               model.kh2_sections.size(), mset.c_str(), model.kh2_root_motion ? "on" : "off");
}

// Says once that a motion cannot be played: its data is read at draw time, so a cut-short file throws only here
void say_bad_motion(const Model &m, const char *set, int slot, const char *what)
{
    static std::set<std::string> told;
    if (!told.insert(m.path + "/" + set + "/" + std::to_string(slot)).second)
        return;
    std::fprintf(stderr,
                 "models: %s: %s slot %d cannot be played (%s); replace the file or take the slot off motions.txt; the "
                 "game's pose is drawn for it\n",
                 m.path.c_str(), set, slot, what);
    khdays_log("models: %s: %s slot %d cannot be played (%s); replace the file or take the slot off motions.txt; the "
               "game's pose is drawn for it",
               m.path.c_str(), set, slot, what);
}

// A slot's pose at `ticks` on the model's bones (`track.joints`) and the root motion taken out of it; false when the
// motion cannot be played.
bool play_track(const Model &m, Kh2Track &track, std::size_t slot, const khdays::assets::kh2::Motion &motion,
                float ticks, const Floor &ground, Mat &taken)
{
    try {
        ready_player(m, track, slot);
        // IKAdjust's world: the model (KH2 centimetres, root motion taken out) to Days' world, y up
        taken = m.kh2_root_motion ? kIdentity : inverse(motion.root_motion(ticks));
        const Mat world = ground.on ? mul(ground.world, taken) : kIdentity;
        track.player->evaluate(motion, ticks, world, ground.on ? &ground.ray : nullptr);
        pose_on_model(m, slot, track.player->joints(), track.joints); // the mset's model's pose on this one
        return true;
    } catch (const std::exception &e) {
        say_bad_motion(m, "its mset", static_cast<int>(slot), e.what());
        return false;
    }
}

// A KH2 model whose game animation has a KH2 motion: its joints posed by the motion (on `base`, the game's root node,
// scaled), each motion of the game's cross-fade at the game's weight and the game's own pose (skin) at the rest of it. A
// section's clock starts when the game's frame enters it and moves as that frame moves (a followed section's is the
// frame itself); past the motion's end it loops from its loop point (retFrame) or holds.
void play_kh2(Model &m, const Mat &base)
{
    namespace kh2 = khdays::assets::kh2;
    namespace kh2_clock = khdays::kh2_clock;
    Kh2Actor &actor = actor_of(m);
    ++actor.draws;
    if (m.kh2_rest.size() != m.joints.size()) {
        m.kh2_rest.resize(m.joints.size());
        for (std::size_t j = 0; j < m.joints.size(); ++j)
            m.kh2_rest[j] = inverse(m.joints[j].inverse_bind);
    }
    struct Pose {
        float weight;
        const std::vector<kh2::Mat4> *joints; // the model's bones (retargeted when its mset is another's)
        const kh2::Motion *motion;
        float ticks;
        std::array<float, 3> offset; // KH2 centimetres
        FaceMotion face;
        Mat turn = kIdentity;  // `rotate`
        int slot = -1;         // its mset slot (the weapon's motion plays the same)
        Mat taken = kIdentity; // its root motion taken out
    };
    std::vector<Pose> poses;
    float total = 0.0F, game = 0.0F;
    const Floor ground = floor_of(m, base);
    // the game's frames this draw: its animations' largest forward step (a `finish` tail's clock), and whether one of
    // its `idle` animations plays
    const auto is_idle = [&](const std::string &name) {
        return std::find(m.kh2_idle.begin(), m.kh2_idle.end(), name) != m.kh2_idle.end();
    };
    float game_step = 0.0F;
    bool any_idle = false;
    for (const Playing &p : playing) {
        const auto was = actor.frames.find(p.name);
        if (was != actor.frames.end() && p.frame > was->second)
            game_step = std::fmax(game_step, p.frame - was->second);
        any_idle = any_idle || (p.ratio > 0.0F && is_idle(p.name));
    }
    actor.frames.clear();
    for (const Playing &p : playing)
        actor.frames[p.name] = p.frame;
    // the game's state for this actor (khdays_models_actor_state): damage in the air, or a landing straight out of it
    const auto known = actor_states.find(drawn_object != nullptr ? KHDAYS_REF(drawn_object) - 0x24U : 0U);
    const ActorState *game_state = known != actor_states.end() ? &known->second : nullptr;
    if (game_state != nullptr && !m.said_states && !m.kh2_air.empty()) {
        m.said_states = true;
        khdays_log("models: %s: the game's actor states reach its draws (state %d): its `air` lines play",
                   m.name.c_str(), game_state->state);
    }
    const bool in_air = game_state != nullptr && game_state->state == kAirDamageState;
    const bool landed_out = game_state != nullptr && khdays::kh2_moves::landed_out_of_air(
                                                         game_state->state == kLandingState, game_state->entered,
                                                         game_state->left_air, kLandedOutOf);
    // `air`: an animation playing its air slot keeps it while the game fades it out after (into the landing, or the
    // fall it recovers into); played anew (its weight rising) or gone, it is the ground's again
    const auto was = [&](const std::string &name) {
        const auto r = actor.ratios.find(name);
        return r != actor.ratios.end() ? r->second : 0.0F;
    };
    {
        std::set<std::string> airborne;
        for (const Playing &p : playing)
            if (p.ratio > 0.0F && m.kh2_air.contains(p.name) &&
                (in_air || (actor.in_air.contains(p.name) && p.ratio <= was(p.name) + 1e-4F)))
                airborne.insert(p.name);
        actor.in_air = std::move(airborne);
    }
    // `cut`: an animation the game fades into (its weight rising) takes the whole pose from its first draw; the
    // game's fade into it depends on the animation before (Ov002_GetModeBlendFrames: a cut from some, a fade from
    // others)
    Playing *cutting = nullptr;
    for (Playing &p : playing) {
        const auto found = m.kh2_sections.find(p.name);
        if (!(p.ratio > 0.0F) || p.ratio >= 0.999F || p.ratio < was(p.name) - 1e-4F || found == m.kh2_sections.end())
            continue;
        const Kh2Section *now = nullptr; // its section at the game's frame
        for (const Kh2Section &section : found->second)
            if (section.from <= p.frame)
                now = &section;
        if (now != nullptr && now->cut)
            cutting = &p;
    }
    // the game cutting into an animation other than its idles (its whole weight at its first draw: a hit, the death)
    // or `cut` taking the pose: a `finish` tail or an idle fade goes at once, as the game's pose does
    bool game_cut = cutting != nullptr;
    for (const Playing &p : playing)
        game_cut = game_cut || (p.ratio >= 0.999F && !(was(p.name) > 0.0F) && !is_idle(p.name));
    if (game_cut) {
        actor.tail.stop();
        actor.held_fade.stop();
    }
    // `walk` lines: the game's weight on its animations with one, at the last draw and now (the walk's mix below)
    float walk_was = 0.0F, walk_now = 0.0F;
    for (const Playing &p : playing)
        if (m.kh2_sections.contains(p.name + "#walk")) {
            walk_was = std::fmax(walk_was, was(p.name));
            walk_now = std::fmax(walk_now, p.ratio);
        }
    actor.ratios.clear();
    for (const Playing &p : playing)
        actor.ratios[p.name] = p.ratio;
    // KHDAYS_SECONDARY_TRACE: what this draw played, a line per draw (the game's weights, then what they became)
    const bool trace_kh2 = secondary_tracing();
    std::string traced;
    char part[192];
    if (trace_kh2)
        for (const Playing &p : playing) {
            std::snprintf(part, sizeof(part), " %s f%.1f w%.2f", p.name.c_str(), static_cast<double>(p.frame),
                          static_cast<double>(p.ratio));
            traced += part;
        }
    if (cutting != nullptr)
        for (Playing &p : playing)
            p.ratio = &p == cutting ? 1.0F : 0.0F;
    bool landing_plays = false;
    for (const Playing &p : playing)
        landing_plays = landing_plays || (p.ratio > 0.0F && p.name == m.kh2_landing);
    if (!landing_plays)
        actor.landing_from.clear();
    else if (landed_out && actor.landing_from.empty())
        actor.landing_from = actor.air_from; // kept while the landing plays, whatever state follows
    // an `air` slot's clock: the vblanks since the last draw (60 a second, as KH2's ticks), none while paused
    const u32 vblank = khdays_events_vblank_count();
    const float realtime = actor.vblank != 0 && !khdays_link_paused()
                               ? static_cast<float>(std::min<u32>(vblank - actor.vblank, 8U))
                               : 0.0F;
    actor.vblank = vblank;
    Kh2Finish finishing; // the `finish` section the game plays now, before going on to an idle
    float finishing_ratio = 0.0F;
    std::string main; // the animation the game plays most, and its fade into an idle (`blend`)
    float main_ratio = -1.0F, main_blend = 0.0F;
    // `camp` lines: while a camp menu draws the model (overlay 25, or its twin 8: runtime/menus/camp.c's camps)
    const bool in_camp = khdays_overlay_loaded(25) != 0 || khdays_overlay_loaded(8) != 0;
    // `walk` lines: the port's walk (the stick pushed short of running, controls_hooks.c) takes each animation with
    // one into its walk motion, eased in and out over the line's `blend` frames (a cut without)
    float walk_blend = 0.0F;
    bool any_walk = false;
    for (const Playing &p : playing)
        if (const auto w = m.kh2_sections.find(p.name + "#walk"); w != m.kh2_sections.end() && !w->second.empty()) {
            any_walk = true;
            walk_blend = std::fmax(walk_blend, w->second.front().blend);
        }
    const bool walks =
        any_walk && drawn_object != nullptr && khdays_controls_walk_share(KHDAYS_REF(drawn_object) - 0x24U) > 0;
    // eased only while the game plays the animation whole: fading it in (from the idle, a landing) the game's fade
    // is the change, and fading it out (no steer then) the mix is kept
    const float walk_step = walk_blend > 0.0F ? realtime * 0.5F / walk_blend : 1.0F; // game frames, 2 vblanks each
    const bool walk_fading = walk_now < walk_was - 1e-4F;
    if (!any_walk || !(walk_now > 0.0F))
        actor.walk = 0.0F;
    else if (!walk_fading && (walk_now < 0.999F || !(walk_was > 0.0F)))
        actor.walk = walks ? 1.0F : 0.0F;
    else if (!walk_fading)
        actor.walk = walks ? std::fmin(1.0F, actor.walk + walk_step) : std::fmax(0.0F, actor.walk - walk_step);
    struct Play {
        const Playing *p;
        float ratio;
        bool walk; // its `walk` line's sections
    };
    std::vector<Play> plays;
    for (const Playing &p : playing) {
        if (actor.walk > 0.0F && m.kh2_sections.contains(p.name + "#walk")) {
            plays.push_back({&p, p.ratio * (1.0F - actor.walk), false});
            plays.push_back({&p, p.ratio * actor.walk, true});
        } else {
            plays.push_back({&p, p.ratio, false});
        }
    }
    for (const Play &play : plays) {
        const Playing &p = *play.p;
        const float ratio = std::fmax(0.0F, std::fmin(1.0F, play.ratio));
        total += ratio;
        // its sections: `air`'s in the game's damage in the air, `land`'s landing straight out of it, else its own
        const std::vector<Kh2Section> *list = nullptr;
        std::string key = p.name;
        const auto air = m.kh2_air.find(p.name);
        const auto landing = m.kh2_air.find(actor.landing_from);
        const auto found = m.kh2_sections.find(p.name);
        const auto camp = in_camp ? m.kh2_sections.find(p.name + "#camp") : m.kh2_sections.end();
        if (play.walk) {
            list = &m.kh2_sections.find(p.name + "#walk")->second;
            key += "#walk";
        } else if (actor.in_air.contains(p.name) && air != m.kh2_air.end()) {
            list = &air->second.air;
            key += "#air";
            actor.air_from = p.name;
        } else if (p.name == m.kh2_landing && landing != m.kh2_air.end() && !landing->second.land.empty()) {
            list = &landing->second.land;
            key += "#land";
        } else if (camp != m.kh2_sections.end()) {
            list = &camp->second;
            key += "#camp";
        } else if (found != m.kh2_sections.end()) {
            list = &found->second;
        }
        // its section at the game's frame: the last one starting at or before it
        int now = -1;
        if (list != nullptr)
            for (std::size_t s = 0; s < list->size() && (*list)[s].from <= p.frame; ++s)
                now = static_cast<int>(s);
        if (ratio > main_ratio) {
            const auto own = m.kh2_idle_blend.find(p.name);
            main = p.name;
            main_ratio = ratio;
            // (a walk line's `blend` is its crossing with the run alone: the run fades into the idle with the game)
            main_blend = own != m.kh2_idle_blend.end() ? own->second
                         : now >= 0 && !play.walk      ? (*list)[static_cast<std::size_t>(now)].blend
                                                       : 0.0F;
        }
        if (now < 0) {
            game += ratio;
            if (trace_kh2) {
                std::snprintf(part, sizeof(part), " | %s game's w%.2f", p.name.c_str(), static_cast<double>(ratio));
                traced += part;
            }
            continue;
        }
        const std::vector<Kh2Section> &sections = *list;
        const Kh2Section &section = sections[static_cast<std::size_t>(now)];
        const kh2::Motion *motion = m.kh2_motions->motion(section.slot);
        Kh2Animation &a = actor.animations[key];
        const bool starts = a.seen + 1 != actor.draws;
        // the game set its frame back: counted on when the KH2 motion loops (it loops itself), else the section
        // starts over (a damage animation played again); a followed section's frame is its time either way
        const bool own_loop = motion->loop_ticks() >= 0.0F && motion->loop_ticks() < motion->length_ticks();
        const kh2_clock::FrameMove move =
            starts ? kh2_clock::FrameMove::on : kh2_clock::frame_move(a.last_frame, p.frame, own_loop);
        const bool back = move != kh2_clock::FrameMove::on;
        const bool enters = starts || now != a.section || (move == kh2_clock::FrameMove::restart && !section.follow);
        float step = 0.0F; // BCA frames since the last draw
        if (!starts) {
            step = p.frame - a.last_frame;
            if (step > 0.0F)
                a.step = step;
            else if (back)
                step = a.step; // one step on (the fall's loop goes back to 16)
        }
        const float before = a.last_frame;
        a.last_frame = p.frame;
        a.seen = actor.draws;
        // a cut (a new section, or a followed frame set back: the loop's blend): fade from the pose last shown
        const bool loops = !enters && back && section.follow;
        const float blend = loops && section.loop_blend >= 0.0F ? section.loop_blend : section.blend;
        const auto shown = a.tracks.find(a.section);
        if (starts) {
            a.fade.clear();
        } else if ((enters || loops) && blend > 0.0F && shown != a.tracks.end() && !shown->second.joints.empty() &&
                   static_cast<std::size_t>(a.section) < sections.size()) {
            a.fade = shown->second.joints;
            a.fade_slot = static_cast<int>(sections[static_cast<std::size_t>(a.section)].slot);
            a.fade_motion = m.kh2_motions->motion(static_cast<std::size_t>(a.fade_slot));
            a.fade_ticks = a.shown;
            a.fade_offset = a.shown_offset;
            a.fade_turn = a.shown_turn;
            a.fade_left = a.fade_frames = blend;
            a.looped_at = loops ? before : -1.0F;
        } else if (!a.fade.empty() && a.looped_at >= 0.0F && (enters || p.frame > a.looped_at)) {
            // the game let go of the loop (past where it set back): the loop's fade ends within `blend`
            const float weight = a.fade_left / a.fade_frames;
            a.fade_left = section.blend;
            a.fade_frames = section.blend / weight;
            a.looped_at = -1.0F;
            if (!(section.blend > 0.0F))
                a.fade.clear();
        }
        Kh2Track &track = a.tracks[now];
        // `start_loop`: from the motion's loop point (its start and loop are one motion; the game's are two). When
        // another game animation was playing this slot up to now (the start, cut or fading into this one), the
        // motion carries on from its time instead: the start is shorter than the motion's run up to its loop
        float start = section.start_loop ? std::fmax(0.0F, motion->loop_ticks()) : 0.0F;
        if (section.start_loop && enters && !section.follow)
            for (const auto &[name, other] : actor.animations) {
                const auto os = m.kh2_sections.find(name);
                if (name == key || other.seen + 1 < actor.draws || os == m.kh2_sections.end() ||
                    static_cast<std::size_t>(other.section) >= os->second.size() ||
                    os->second[static_cast<std::size_t>(other.section)].slot != section.slot)
                    continue;
                // seen last draw: one of its steps on; seen this draw (fading together): its time now
                start = other.shown + (other.seen == actor.draws ? 0.0F : other.step * 2.0F);
                break;
            }
        if (section.follow) { // the game's frame, the section stretched onto the motion
            const float to = static_cast<std::size_t>(now) + 1 < sections.size()
                                 ? sections[static_cast<std::size_t>(now) + 1].from
                                 : p.frames;
            track.ticks = start + (to > section.from ? (p.frame - section.from) / (to - section.from) *
                                                           (motion->length_ticks() - start)
                                                     : 0.0F);
        } else if (enters) { // its clock from the section's first frame (an `air` slot's from its start)
            track.ticks = start + (section.realtime ? 0.0F : (p.frame - section.from) * 2.0F);
        } else if (section.realtime) { // the game's frames: Days holds the damage on a frame while in the air
            track.ticks += realtime;
        } else {
            track.ticks += step * 2.0F; // BCA frames at 30 a second
        }
        a.section = now;
        kh2_played[p.name] = {section.slot, section.from};
        // a cut: the whole pose a new motion's (or one started over), nothing fading from the last
        if (enters && ratio >= 0.999F && a.fade.empty())
            kh2_pose_cut = true;
        float faded = 0.0F; // the weight of the pose fading out
        if (!a.fade.empty()) {
            a.fade_left -= section.realtime ? realtime * 0.5F : step; // BCA frames
            if (a.fade_left > 0.0F && a.fade_motion != nullptr)
                faded = a.fade_left / a.fade_frames;
            else
                a.fade.clear();
        }
        float ticks = track.ticks;
        const float end = motion->length_ticks(), from = motion->loop_ticks();
        if (ticks > end)
            ticks = from >= 0.0F && from < end ? from + std::fmod(ticks - from, end - from) : end;
        Mat taken = kIdentity;
        if (!play_track(m, track, section.slot, *motion, ticks, ground, taken)) { // the game's pose in its place
            game += ratio;
            kh2_played.erase(p.name);
            continue;
        }
        a.shown = ticks;
        if (trace_kh2) {
            std::snprintf(part, sizeof(part), " | %s slot %zu t%.1f w%.2f%s%s", key.c_str(), section.slot,
                          static_cast<double>(ticks), static_cast<double>(ratio), starts ? " starts" : "",
                          enters && !starts ? " enters" : "");
            traced += part;
        }
        const FaceMotion face{m.kh2_motions->attributes(section.slot), ticks, enters ? -1.0F : track.face_ticks};
        track.face_ticks = ticks;
        // `offset`, gliding to its `to` over the motion's length
        const float along = motion->length_ticks() > 0.0F ? std::fmin(1.0F, ticks / motion->length_ticks()) : 0.0F;
        for (std::size_t i = 0; i < 3; ++i)
            a.shown_offset[i] = section.offset[i] + (section.offset_to[i] - section.offset[i]) * along;
        a.shown_turn = section.turn;
        // KHDAYS_SECONDARY_TRACE: on a section's first draw, where the joints the game drives are in its pose and in
        // the motion's (cm, the model's root space), to tell which way each lies (`rotate`)
        if (trace_kh2 && enters) {
            std::string where;
            const Mat to_root = inverse(base);
            for (std::size_t j = 0; j < m.joints.size() && j < m.kh2_bone.size(); ++j) {
                const int bone = m.kh2_bone[j];
                if (m.joints[j].node < 0 || bone < 0 || static_cast<std::size_t>(bone) >= track.joints.size())
                    continue;
                const Mat g = mul(to_root, mul(skin[j], m.kh2_rest[j]));
                const Mat kh = mul(section.turn, mul(taken, track.joints[static_cast<std::size_t>(bone)]));
                std::snprintf(
                    part, sizeof(part), " %s game (%.0f %.0f %.0f) kh2 (%.0f %.0f %.0f);", m.joints[j].name.c_str(),
                    static_cast<double>(g[12] * 100.0F), static_cast<double>(g[13] * 100.0F),
                    static_cast<double>(g[14] * 100.0F), static_cast<double>(kh[12] + section.offset[0]),
                    static_cast<double>(kh[13] + section.offset[1]), static_cast<double>(kh[14] + section.offset[2]));
                where += part;
            }
            khdays_log("kh2 trace: %s %s starts slot %zu:%s", m.name.c_str(), key.c_str(), section.slot, where.c_str());
        }
        if (faded > 0.0F)
            poses.push_back(
                {ratio * faded, &a.fade, a.fade_motion, a.fade_ticks, a.fade_offset, {}, a.fade_turn, a.fade_slot});
        poses.push_back({ratio * (1.0F - faded), &track.joints, motion, ticks, a.shown_offset, face, a.shown_turn,
                         static_cast<int>(section.slot)});
        if (section.finish && !any_idle && !is_idle(p.name) && ratio > finishing_ratio) {
            finishing = {p.name, section.slot, section.then, section.blend, ticks};
            finishing_ratio = ratio;
        }
    }
    // `finish`: the game went on to its idles before the motion ended (and plays nothing else): the motion's rest
    // and the `finish` slots play out on the game's frames, then fade into the idle's; anything else drops them
    bool idle_only = true, from_seen = false;
    for (const Playing &p : playing) {
        const bool fading_out =
            actor.tail.active() && p.name == actor.tail_from && p.ratio <= actor.tail_from_ratio + 1e-4F;
        if (actor.tail.active() && p.name == actor.tail_from && p.ratio > 0.0F) {
            from_seen = fading_out;
            actor.tail_from_ratio = p.ratio;
        }
        idle_only = idle_only && (!(p.ratio > 0.0F) || is_idle(p.name) || p.name == actor.finish.name || fading_out);
    }
    if (!from_seen)
        actor.tail_from.clear();
    if (actor.tail.active()) {
        actor.tail.step(game_step, idle_only);
    } else if (any_idle && idle_only && !actor.finish.name.empty()) {
        std::vector<khdays::kh2_tail::Piece> pieces;
        const auto add = [&](std::size_t slot, float from) {
            if (const kh2::Motion *motion = m.kh2_motions->motion(slot))
                pieces.push_back({static_cast<int>(slot), from, motion->length_ticks()});
        };
        add(actor.finish.slot, actor.finish.ticks);
        for (const std::size_t slot : actor.finish.then)
            add(slot, 0.0F);
        actor.tail.start(std::move(pieces), actor.finish.blend);
        actor.tail_from = actor.finish.name;
        actor.tail_from_ratio = 1.0F;
        for (const Playing &p : playing)
            if (p.name == actor.tail_from)
                actor.tail_from_ratio = p.ratio;
        actor.tail_piece = 0;
        actor.tail_track = {};
        actor.tail_faded = {};
        actor.finish = {};
    }
    if (!finishing.name.empty())
        actor.finish = std::move(finishing);
    else if (!idle_only)
        actor.finish = {};
    if (actor.tail.active()) { // its pieces at their share, the rest of the pose at what is left
        if (actor.tail.piece() != actor.tail_piece) {
            actor.tail_faded = std::move(actor.tail_track);
            actor.tail_track = {};
            actor.tail_piece = actor.tail.piece();
        }
        const auto evaluate = [&](Kh2Track &t, int slot, float ticks) -> const kh2::Motion * {
            const kh2::Motion *motion = slot >= 0 ? m.kh2_motions->motion(static_cast<std::size_t>(slot)) : nullptr;
            if (motion == nullptr)
                return nullptr;
            ticks = std::fmin(ticks, motion->length_ticks());
            Mat taken = kIdentity;
            if (!play_track(m, t, static_cast<std::size_t>(slot), *motion, ticks, ground, taken))
                return nullptr;
            t.ticks = ticks;
            return motion;
        };
        const float share = actor.tail.weight(), under = actor.tail.faded_weight();
        const float whole = total > 0.0F ? total : 1.0F;
        for (Pose &pose : poses)
            pose.weight *= 1.0F - share;
        game *= 1.0F - share;
        if (const kh2::Motion *motion = evaluate(actor.tail_track, actor.tail.slot(), actor.tail.ticks())) {
            const int slot = actor.tail.slot();
            const FaceMotion face{m.kh2_motions->attributes(static_cast<std::size_t>(slot)), actor.tail_track.ticks,
                                  actor.tail_track.face_ticks};
            actor.tail_track.face_ticks = actor.tail_track.ticks;
            poses.push_back({share * (1.0F - under) * whole,
                             &actor.tail_track.joints,
                             motion,
                             actor.tail_track.ticks,
                             {},
                             face,
                             kIdentity,
                             slot});
        }
        if (under > 0.0F)
            if (const kh2::Motion *motion =
                    evaluate(actor.tail_faded, actor.tail.faded_slot(), actor.tail.faded_ticks()))
                poses.push_back({share * under * whole,
                                 &actor.tail_faded.joints,
                                 motion,
                                 actor.tail_faded.ticks,
                                 {},
                                 {},
                                 kIdentity,
                                 actor.tail.faded_slot()});
        total = game;
        for (const Pose &pose : poses)
            total += pose.weight;
    }
    // `blend` into an idle: when the game goes on to one of its idles from an animation with an idle fade (its own
    // `<animation> blend` line or its section's `blend`), the pose last shown fades into the game's over those frames
    // (the game's own fade not counted; a `finish` tail fades itself); anything else the game plays ends it
    const auto into_idle = [&]() {
        if (actor.held_fade.weight() > 0.0F) {
            bool other = actor.tail.active();
            for (const Playing &p : playing) {
                if (!(p.ratio > 0.0F) || is_idle(p.name))
                    continue;
                if (p.name == actor.held_from && p.ratio <= actor.held_from_ratio + 1e-4F) // fading out, not anew
                    actor.held_from_ratio = p.ratio;
                else
                    other = true;
            }
            actor.held_fade.step(game_step, other);
        } else if (actor.held_fade.starts(any_idle, actor.had_idle, actor.main.empty() || is_idle(actor.main),
                                          actor.main_blend, actor.tail.active()) &&
                   actor.shown.size() == skin.size()) {
            actor.held = actor.shown;
            actor.held_fade.start(actor.main_blend);
            actor.held_from = actor.main;
            actor.held_from_ratio = 1.0F;
            for (const Playing &p : playing)
                if (p.name == actor.held_from)
                    actor.held_from_ratio = p.ratio;
        }
        // the pose held relative to the model's root (where the game puts it now), mixed as frames
        const float w = actor.held_fade.weight();
        if (w > 0.0F && actor.held.size() == skin.size())
            for (std::size_t j = 0; j < skin.size(); ++j) {
                const Mat both[2] = {mul(base, actor.held[j]), mul(skin[j], m.kh2_rest[j])};
                const float share[2] = {w, 1.0F - w};
                skin[j] = mul(khdays::models::blend_frames(both, share, 2), m.joints[j].inverse_bind);
            }
        const Mat from_root = inverse(base);
        actor.shown.resize(skin.size());
        for (std::size_t j = 0; j < skin.size(); ++j)
            actor.shown[j] = mul(from_root, mul(skin[j], m.kh2_rest[j]));
        actor.main = main;
        actor.main_blend = main_blend;
        actor.had_idle = any_idle;
        if (trace_kh2)
            khdays_log("kh2 trace: %s v%u state %d;%s%s; idle fade %.2f, tail %.2f%s", m.name.c_str(), vblank,
                       game_state != nullptr ? game_state->state : -1, traced.c_str(),
                       cutting != nullptr ? " (cut)" : "", static_cast<double>(actor.held_fade.weight()),
                       static_cast<double>(actor.tail.active() ? actor.tail.weight() : 0.0F),
                       kh2_pose_cut ? " (a pose cut)" : "");
    };
    // each pose's root motion taken out unless it is kept; one that cannot say its own counts as the game's pose
    for (std::size_t i = 0; i < poses.size();) {
        try {
            poses[i].taken = m.kh2_root_motion ? kIdentity : inverse(poses[i].motion->root_motion(poses[i].ticks));
            ++i;
        } catch (const std::exception &e) {
            say_bad_motion(m, "its mset", poses[i].slot, e.what());
            game += poses[i].weight;
            poses.erase(poses.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    if (poses.empty()) {
        into_idle();
        return;
    }
    const Pose *most = &poses.front();
    for (const Pose &pose : poses)
        if (pose.weight > most->weight)
            most = &pose;
    if (most->weight >= game)
        face_motion = most->face;
    if (!(total > 0.0F)) {
        total = poses.back().weight = 1.0F;
        game = 0.0F;
    }
    kh2_game_share = game / total;
    // `weapon_motions`: each pose's slot played on the weapon at its time, mixed as the poses are; a slot the weapon
    // has no motion for counts as the game's own pose, held as Days holds it (the held pose, and its share)
    if (m.weapon_motions) {
        if (actor.weapon_skeleton != m.weapon_skeleton.get()) {
            actor.weapon_players.clear();
            actor.weapon_skeleton = m.weapon_skeleton.get();
        }
        std::vector<std::vector<Mat>> played;
        std::vector<float> shares;
        float days = game;
        for (const Pose &pose : poses) {
            const kh2::Motion *motion =
                pose.slot >= 0 ? m.weapon_motions->motion(static_cast<std::size_t>(pose.slot)) : nullptr;
            if (motion == nullptr) {
                days += pose.weight;
                continue;
            }
            auto &player = actor.weapon_players[pose.slot];
            if (!player)
                player = std::make_unique<kh2::MotionPlayer>(*m.weapon_skeleton);
            std::vector<Mat> bones;
            try {
                player->evaluate(*motion, std::fmin(pose.ticks, motion->length_ticks()), kIdentity, nullptr);
                for (const auto &g : player->joints())
                    bones.push_back(in_metres(g));
            } catch (const std::exception &e) { // held as Days holds it, as a slot with no motion
                say_bad_motion(m, "its weapon's mset", pose.slot, e.what());
                days += pose.weight;
                continue;
            }
            played.push_back(std::move(bones));
            shares.push_back(pose.weight / total);
        }
        kh2_weapon_share = days / total;
        kh2_weapon_pose.assign(m.weapon_rest.size(), kIdentity);
        std::vector<Mat> frames;
        std::vector<float> weights;
        for (std::size_t b = 0; b < m.weapon_rest.size(); ++b) {
            frames.assign(1, m.weapon_rest[b]);
            weights.assign(1, kh2_weapon_share);
            for (std::size_t p = 0; p < played.size(); ++p)
                if (b < played[p].size()) {
                    frames.push_back(played[p][b]);
                    weights.push_back(shares[p]);
                }
            kh2_weapon_pose[b] = khdays::models::blend_frames(frames.data(), weights.data(), frames.size());
        }
    }
    // each pose's joints in metres (KH2's centimetres)
    std::vector<std::vector<Mat>> placed;
    for (const Pose &pose : poses) {
        std::vector<Mat> joints;
        for (const auto &g : *pose.joints) {
            Mat w = mul(pose.turn, mul(pose.taken, g));
            w[12] = (w[12] + pose.offset[0]) * 0.01F;
            w[13] = (w[13] + pose.offset[1]) * 0.01F;
            w[14] = (w[14] + pose.offset[2]) * 0.01F;
            joints.push_back(mul(base, w));
        }
        placed.push_back(std::move(joints));
    }
    // each joint's frames mixed (turns the shorter way round), the game's own pose at its share, on its parent's mixed
    // frame as each pose holds it from its parent (docs/KH2_MODELS.md, "Into the idle")
    const std::size_t count = std::min(m.joints.size(), m.kh2_bone.size());
    std::vector<Mat> game_frame(count), mixed(count);
    std::vector<char> is_mixed(count, 0);
    for (std::size_t j = 0; j < count; ++j)
        game_frame[j] = mul(skin[j], m.kh2_rest[j]);
    std::vector<Mat> frames;
    std::vector<float> weights;
    for (std::size_t j = 0; j < count; ++j) {
        const int bone = m.kh2_bone[j];
        if (bone < 0)
            continue;
        const int parent = m.joints[j].parent;
        const bool on_parent = parent >= 0 && static_cast<std::size_t>(parent) < j && is_mixed[parent] != 0;
        const auto b = static_cast<std::size_t>(bone);
        const auto pb = on_parent ? static_cast<std::size_t>(m.kh2_bone[static_cast<std::size_t>(parent)]) : 0U;
        frames.assign(1, on_parent ? mul(inverse(game_frame[static_cast<std::size_t>(parent)]), game_frame[j])
                                   : game_frame[j]);
        weights.assign(1, game / total);
        for (std::size_t p = 0; p < poses.size(); ++p) {
            if (b >= placed[p].size() || pb >= placed[p].size())
                continue;
            frames.push_back(on_parent ? mul(inverse(placed[p][pb]), placed[p][b]) : placed[p][b]);
            weights.push_back(poses[p].weight / total);
        }
        const Mat frame = khdays::models::blend_frames(frames.data(), weights.data(), frames.size());
        mixed[j] = on_parent ? mul(mixed[static_cast<std::size_t>(parent)], frame) : frame;
        is_mixed[j] = 1;
        skin[j] = mul(mixed[j], m.joints[j].inverse_bind);
    }
    into_idle();
}

} // namespace khdays::model_parts
