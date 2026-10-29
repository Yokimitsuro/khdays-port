// Reading `<model>.secondary.txt` (config.h, docs/SECONDARY_MOTION.md).

#include "config.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace khdays::secondary {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

const ParamInfo kParams[kParamCount] = {
    {"mass", 0.05F, 1e-4F, 100.0F, Unit::none},          {"stretch_compliance", 0.0F, 0.0F, 1.0F, Unit::none},
    {"shear_compliance", 1e-5F, 0.0F, 1.0F, Unit::none}, {"bend_compliance", 1e-3F, 0.0F, 1.0F, Unit::none},
    {"damping", 1.0F, 0.0F, 200.0F, Unit::none},         {"follow_frequency", 2.0F, 0.0F, 60.0F, Unit::none},
    {"follow_damping", 0.6F, 0.0F, 10.0F, Unit::none},   {"gravity_scale", 1.0F, -10.0F, 10.0F, Unit::none},
    {"max_distance", 0.0F, 0.0F, 10.0F, Unit::length},   {"tether_slack", 0.05F, 0.0F, 2.0F, Unit::none},
    {"swing_limit", kPi, 0.0F, kPi, Unit::angle},        {"twist_limit", kPi, 0.0F, kPi, Unit::angle},
    {"inertia", 1.0F, 0.0F, 2.0F, Unit::none},           {"guided_inertia", 0.5F, 0.0F, 2.0F, Unit::none},
    {"max_speed", 20.0F, 0.01F, 1000.0F, Unit::none},    {"thickness", 0.01F, 0.0F, 1.0F, Unit::length},
    {"friction", 0.2F, 0.0F, 1.0F, Unit::none},          {"wind_drag", 1.0F, 0.0F, 100.0F, Unit::none},
    {"wind_lift", 0.5F, 0.0F, 1.0F, Unit::none},         {"row_compliance", 0.0F, 0.0F, 1.0F, Unit::none},
    {"max_strain", 0.08F, 0.0F, 4.0F, Unit::none},       {"row_max_strain", 0.08F, 0.0F, 8.0F, Unit::none},
    {"smoothing", 0.0F, 0.0F, 0.5F, Unit::none},         {"follow_bone_scale", 1.0F, 0.0F, 1.0F, Unit::none},
};

struct Line {
    std::vector<std::string> words;
    int number = 0;
};

std::vector<Line> lines_of(const std::string &text)
{
    std::vector<Line> out;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        std::istringstream words(line);
        Line l;
        l.number = n;
        std::string w;
        while (words >> w)
            l.words.push_back(w);
        if (!l.words.empty())
            out.push_back(std::move(l));
    }
    return out;
}

bool number(const std::string &s, float &out)
{
    if (s.empty())
        return false;
    char *end = nullptr;
    errno = 0;
    const float v = std::strtof(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

bool integer(const std::string &s, int &out)
{
    char *end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || v < -1000000 || v > 1000000)
        return false;
    out = static_cast<int>(v);
    return true;
}

class Reader {
public:
    Reader(Config &config, Diagnostics &out, std::string file, std::filesystem::path folder)
        : c_(config), out_(out), file_(std::move(file)), folder_(std::move(folder))
    {
    }

    void run(const std::vector<Line> &lines, int depth);

private:
    void error(int line, const std::string &text)
    {
        out_.push_back({true, file_, line, text});
    }
    void warn(int line, const std::string &text)
    {
        out_.push_back({false, file_, line, text});
    }
    // a length in the file's units, in metres
    float length(float v) const
    {
        return v * c_.units;
    }
    bool param_line(const Line &l, ParamSet &set);
    bool bone_list(const Line &l, std::size_t from, std::vector<BoneRef> &out);
    bool wind_line(const Line &l, Wind &w);
    bool vec3(const Line &l, std::size_t at, Vec3 &v);
    bool mode_line(const Line &l, ModeSetting &m, PolicySetting *p);
    void group(const std::vector<Line> &lines, std::size_t &i);
    void preset(const std::vector<Line> &lines, std::size_t &i);
    void control(const std::vector<Line> &lines, std::size_t &i);
    void collider(const Line &l);
    void lod(const Line &l);

    Config &c_;
    Diagnostics &out_;
    std::string file_;
    std::filesystem::path folder_;
};

bool Reader::vec3(const Line &l, std::size_t at, Vec3 &v)
{
    if (l.words.size() < at + 3)
        return false;
    return number(l.words[at], v.x) && number(l.words[at + 1], v.y) && number(l.words[at + 2], v.z);
}

// `<param> <value> [hem <value>]`
bool Reader::param_line(const Line &l, ParamSet &set)
{
    Param p;
    if (!param_named(l.words[0], p))
        return false;
    const ParamInfo &info = param_info(p);
    const auto convert = [&](float v) {
        return info.unit == Unit::length ? length(v) : info.unit == Unit::angle ? v * kDegrees : v;
    };
    float v = 0.0F;
    if (l.words.size() < 2 || !number(l.words[1], v)) {
        error(l.number, std::string("write `") + info.name + " <number>`");
        return true;
    }
    const auto index = static_cast<std::size_t>(p);
    const auto check = [&](float x) {
        if (x < info.lo || x > info.hi) {
            error(l.number, std::string(info.name) + " " + l.words[1] + " is out of range (" +
                                std::to_string(info.unit == Unit::angle ? info.lo / kDegrees : info.lo) + " to " +
                                std::to_string(info.unit == Unit::angle ? info.hi / kDegrees : info.hi) +
                                (info.unit == Unit::length  ? " metres"
                                 : info.unit == Unit::angle ? " degrees"
                                                            : "") +
                                ")");
            return false;
        }
        return true;
    };
    const float iv = convert(v);
    if (!check(iv))
        return true;
    set.value[index] = iv;
    set.given.set(index);
    if (l.words.size() >= 4 && l.words[2] == "hem") {
        float h = 0.0F, c = 1.0F;
        if (!number(l.words[3], h) || (l.words.size() != 4 && (l.words.size() != 6 || l.words[4] != "curve" ||
                                                               !number(l.words[5], c) || c <= 0.0F || c > 8.0F))) {
            error(l.number, std::string("write `") + info.name + " <value> hem <value> [curve <0-8>]`");
            return true;
        }
        if (check(convert(h))) {
            set.hem[index] = convert(h);
            set.curve[index] = c;
            set.hem_given.set(index);
        }
    } else if (l.words.size() > 2) {
        error(l.number, std::string("after `") + info.name + " <value>` only `hem <value> [curve <n>]` may follow");
    }
    return true;
}

bool Reader::bone_list(const Line &l, std::size_t from, std::vector<BoneRef> &out)
{
    if (l.words.size() <= from) {
        error(l.number, "`" + l.words[0] + "` names no bones");
        return false;
    }
    for (std::size_t i = from; i < l.words.size(); ++i) {
        // a range of KH2 bones: `26-32` is 26 27 ... 32
        const std::string &w = l.words[i];
        const auto dash = w.find('-');
        int a = 0, b = 0;
        if (dash != std::string::npos && dash > 0 && integer(w.substr(0, dash), a) && integer(w.substr(dash + 1), b)) {
            if (b < a || b - a > 512) {
                error(l.number, "the range " + w + " is empty or too long");
                return false;
            }
            for (int n = a; n <= b; ++n)
                out.push_back({std::to_string(n), l.number});
            continue;
        }
        out.push_back({w, l.number});
    }
    return true;
}

// `wind <world|actor|bone <bone>> <x y z> speed <m/s> [gust <f>] [frequency <Hz>] [turbulence <f>]`
bool Reader::wind_line(const Line &l, Wind &w)
{
    std::size_t i = 1;
    if (l.words.size() < 2) {
        error(l.number, "write `wind <world|actor|bone <bone>> <x y z> speed <m/s> ...`");
        return false;
    }
    if (l.words[i] == "off") {
        w = Wind{};
        w.given = true;
        return true;
    }
    if (l.words[i] == "world")
        w.space = WindSpace::world;
    else if (l.words[i] == "actor")
        w.space = WindSpace::actor;
    else if (l.words[i] == "bone" && l.words.size() > i + 1) {
        w.space = WindSpace::bone;
        w.bone = {l.words[++i], l.number};
    } else {
        error(l.number, "wind: the direction's space is world, actor or bone <bone>");
        return false;
    }
    ++i;
    Vec3 d;
    if (!vec3(l, i, d) || secondary::length(d) < 1e-6F) {
        error(l.number, "wind: give a direction x y z that is not zero");
        return false;
    }
    w.direction = normalized(d);
    i += 3;
    for (; i + 1 < l.words.size(); i += 2) {
        float v = 0.0F;
        if (!number(l.words[i + 1], v) || v < 0.0F) {
            error(l.number, "wind: " + l.words[i] + " takes a number of at least 0");
            return false;
        }
        if (l.words[i] == "speed")
            w.speed = v;
        else if (l.words[i] == "gust")
            w.gust = v;
        else if (l.words[i] == "frequency")
            w.frequency = v;
        else if (l.words[i] == "turbulence")
            w.turbulence = v;
        else {
            error(l.number, "wind: " + l.words[i] + " is not speed, gust, frequency or turbulence");
            return false;
        }
    }
    if (i != l.words.size()) {
        error(l.number, "wind: " + l.words.back() + " has no value");
        return false;
    }
    w.given = true;
    return true;
}

bool Reader::mode_line(const Line &l, ModeSetting &m, PolicySetting *p)
{
    if (l.words[0] == "mode") {
        if (l.words.size() != 2) {
            error(l.number, "write `mode simulated|guided|authored`");
            return true;
        }
        if (l.words[1] == "simulated")
            m.mode = Mode::simulated;
        else if (l.words[1] == "guided")
            m.mode = Mode::guided;
        else if (l.words[1] == "authored")
            m.mode = Mode::authored;
        else {
            error(l.number, "mode " + l.words[1] + " is not simulated, guided or authored");
            return true;
        }
        m.given = true;
        return true;
    }
    if (l.words[0] == "authored_policy" && p != nullptr) {
        if (l.words.size() == 2 && l.words[1] == "exact")
            p->policy = AuthoredPolicy::exact;
        else if (l.words.size() == 2 && l.words[1] == "collision_corrected")
            p->policy = AuthoredPolicy::collision_corrected;
        else {
            error(l.number, "write `authored_policy exact|collision_corrected`");
            return true;
        }
        p->given = true;
        return true;
    }
    return false;
}

void Reader::preset(const std::vector<Line> &lines, std::size_t &i)
{
    const Line &head = lines[i];
    PresetConfig p;
    p.line = head.number;
    p.file = file_;
    if (head.words.size() == 2 || (head.words.size() == 4 && head.words[2] == "extends")) {
        p.name = head.words[1];
        if (head.words.size() == 4)
            p.extends = head.words[3];
    } else {
        error(head.number, "write `preset <name> [extends <preset>]`");
    }
    for (++i; i < lines.size(); ++i) {
        const Line &l = lines[i];
        if (l.words[0] == "end")
            break;
        if (param_line(l, p.params) || mode_line(l, p.mode, &p.policy))
            continue;
        if (l.words[0] == "collide") {
            p.collide.assign(l.words.begin() + 1, l.words.end());
            p.collide_given = true;
        } else if (l.words[0] == "self_collision" && l.words.size() == 2) {
            p.self_collision = l.words[1] == "off" ? std::string() : l.words[1];
            p.self_given = true;
        } else {
            error(l.number, "`" + l.words[0] + "` is not a preset setting");
        }
    }
    if (i >= lines.size())
        error(head.number, "preset " + p.name + " has no `end`");
    for (const auto &q : c_.presets)
        if (q.name == p.name)
            warn(head.number, "preset " + p.name + " is defined again; the last definition is used");
    c_.presets.push_back(std::move(p));
}

void Reader::group(const std::vector<Line> &lines, std::size_t &i)
{
    const Line &head = lines[i];
    GroupConfig g;
    g.line = head.number;
    // group <name> <surface|chain|hair> [preset <preset>]
    if (head.words.size() < 3) {
        error(head.number, "write `group <name> surface|chain|hair [preset <name>]`");
    } else {
        g.name = head.words[1];
        if (head.words[2] == "surface")
            g.kind = GroupKind::surface;
        else if (head.words[2] == "chain")
            g.kind = GroupKind::chain;
        else if (head.words[2] == "hair")
            g.kind = GroupKind::hair;
        else
            error(head.number, "group " + g.name + ": " + head.words[2] + " is not surface, chain or hair");
        if (head.words.size() == 5 && head.words[3] == "preset")
            g.preset = head.words[4];
        else if (head.words.size() != 3)
            error(head.number, "group " + g.name + ": after the kind only `preset <name>` may follow");
    }
    for (++i; i < lines.size(); ++i) {
        const Line &l = lines[i];
        const std::string &w = l.words[0];
        if (w == "end")
            break;
        if (param_line(l, g.params) || mode_line(l, g.mode, &g.policy))
            continue;
        if (w == "attach") {
            bone_list(l, 1, g.attach);
        } else if (w == "follow_bone") { // follow_bone <bone> radius <r> [weight <w>] [always <a>]
            FollowBone f;
            float r = 0.0F, wt = 0.5F, always = 0.0F;
            bool ok = l.words.size() >= 4 && l.words.size() % 2 == 0 && l.words[2] == "radius" &&
                      number(l.words[3], r) && r > 0.0F;
            for (std::size_t k = 4; ok && k + 1 < l.words.size(); k += 2) {
                float v = 0.0F;
                ok = number(l.words[k + 1], v) && v >= 0.0F && v <= 1.0F;
                if (ok && l.words[k] == "weight")
                    wt = v;
                else if (ok && l.words[k] == "always")
                    always = v;
                else
                    ok = false;
            }
            if (!ok) {
                error(l.number, "write `follow_bone <bone> radius <r> [weight <0..1>] [always <0..1>]`");
                continue;
            }
            f.bone = {l.words[1], l.number};
            f.radius = length(r);
            f.weight = wt;
            f.always = always;
            g.follow_bones.push_back(f);
        } else if (w == "strip" || w == "chain" || w == "tuft") {
            std::vector<BoneRef> s;
            if (bone_list(l, 1, s))
                g.strips.push_back(std::move(s));
        } else if (w == "tufts") { // hair: one single-bone tuft each
            std::vector<BoneRef> s;
            if (bone_list(l, 1, s))
                for (auto &b : s)
                    g.strips.push_back({b});
        } else if (w == "ring") {
            std::vector<BoneRef> s;
            if (bone_list(l, 1, s)) {
                if (s.size() < 3)
                    error(l.number, "a ring joins at least 3 controls");
                else
                    g.rings.push_back(std::move(s));
            }
        } else if (w == "topology") {
            if (l.words.size() == 2 && (l.words[1] == "mesh" || l.words[1] == "explicit"))
                g.mesh_topology = l.words[1] == "mesh";
            else
                error(l.number, "write `topology mesh|explicit`");
        } else if (w == "connect" || w == "disconnect") {
            if (l.words.size() != 3) {
                error(l.number, "write `" + w + " <bone> <bone>`");
                continue;
            }
            (w == "connect" ? g.connect : g.disconnect).push_back({{l.words[1], l.number}, {l.words[2], l.number}});
        } else if (w == "exclude") {
            bone_list(l, 1, g.exclude);
        } else if (w == "offset") {
            Vec3 v;
            if (l.words.size() != 5 || !vec3(l, 2, v)) {
                error(l.number, "write `offset <bone> <x> <y> <z>` (bone-local, in the file's units)");
                continue;
            }
            g.offsets.push_back({{l.words[1], l.number}, v * c_.units});
        } else if (w == "bone") { // bone <bone> <param> <value> [hem ...] | bone <bone> mode <mode>
            if (l.words.size() < 4) {
                error(l.number, "write `bone <bone> <setting> <value>`");
                continue;
            }
            BoneOverride o;
            o.bone = {l.words[1], l.number};
            Line rest;
            rest.number = l.number;
            rest.words.assign(l.words.begin() + 2, l.words.end());
            if (!param_line(rest, o.params) && !mode_line(rest, o.mode, nullptr)) {
                error(l.number, "bone " + l.words[1] + ": " + rest.words[0] + " is not a setting a bone can override");
                continue;
            }
            g.overrides.push_back(std::move(o));
        } else if (w == "collide") {
            g.collide.assign(l.words.begin() + 1, l.words.end());
            g.collide_given = true;
        } else if (w == "self_collision" && l.words.size() == 2) {
            g.self_collision = l.words[1] == "off" ? std::string() : l.words[1];
            g.self_given = true;
        } else if (w == "settle") {
            if (l.words.size() >= 2 && (l.words[1] == "on" || l.words[1] == "off"))
                g.settle = l.words[1] == "on";
            else
                error(l.number, "write `settle on|off [steps <n>]`");
            if (l.words.size() == 4 && l.words[2] == "steps" &&
                (!integer(l.words[3], g.settle_steps) || g.settle_steps < 1 || g.settle_steps > 6000))
                error(l.number, "settle steps: 1 to 6000");
        } else if (w == "hang") {
            Vec3 d;
            if (l.words.size() != 4 || !vec3(l, 1, d) || secondary::length(d) < 1e-6F) {
                error(l.number, "write `hang <x> <y> <z>` (a direction, model space at rest)");
                continue;
            }
            g.hang = normalized(d);
            g.hang_given = true;
        } else if (w == "twist") {
            if (l.words.size() == 2 && (l.words[1] == "neighbours" || l.words[1] == "reference"))
                g.twist_from_neighbours = l.words[1] == "neighbours";
            else
                error(l.number, "write `twist neighbours|reference`");
        } else if (w == "wind") {
            wind_line(l, g.wind);
        } else {
            error(l.number, "`" + w + "` is not a group setting");
        }
    }
    if (i >= lines.size())
        error(head.number, "group " + g.name + " has no `end`");
    c_.groups.push_back(std::move(g));
}

// collider <name> sphere <bone> radius <r> [offset x y z] [tags ...]
// collider <name> capsule <bone> <end bone>|length <l> radius <r> [<end radius>] [offset ...]
//          [end_offset ...] [fit [<percentile>] [scale <s>]] [tags ...]
void Reader::collider(const Line &l)
{
    ColliderConfig c;
    c.line = l.number;
    const auto bad = [&](const std::string &why) {
        error(l.number, "collider: " + why +
                            " (write `collider <name> sphere|capsule <bone> [<end bone>|length <l>] radius <r> [<end "
                            "r>] ...`)");
    };
    if (l.words.size() < 4)
        return bad("too short");
    c.name = l.words[1];
    if (l.words[2] == "sphere")
        c.kind = ShapeKind::sphere;
    else if (l.words[2] == "capsule")
        c.kind = ShapeKind::capsule;
    else
        return bad(l.words[2] + " is not sphere or capsule");
    c.bone = {l.words[3], l.number};
    std::size_t i = 4;
    if (c.kind == ShapeKind::capsule) {
        if (i < l.words.size() && l.words[i] == "length") {
            float v = 0.0F;
            if (i + 1 >= l.words.size() || !number(l.words[i + 1], v) || v <= 0.0F)
                return bad("length takes a positive number");
            c.length = length(v);
            i += 2;
        } else if (i < l.words.size() && l.words[i] != "radius" && l.words[i] != "fit") {
            c.end_bone = {l.words[i], l.number};
            c.end_given = true;
            ++i;
        } else {
            return bad("a capsule needs an end bone or `length <l>`");
        }
    }
    bool radius = false;
    while (i < l.words.size()) {
        const std::string &w = l.words[i];
        float v = 0.0F;
        if (w == "radius" && i + 1 < l.words.size() && number(l.words[i + 1], v) && v >= 0.0F) {
            c.radius = length(v);
            radius = true;
            i += 2;
            if (i < l.words.size() && number(l.words[i], v)) {
                if (v < 0.0F)
                    return bad("an end radius cannot be negative");
                c.end_radius = length(v);
                ++i;
            }
        } else if ((w == "offset" || w == "end_offset") && vec3(l, i + 1, w == "offset" ? c.offset : c.end_offset)) {
            (w == "offset" ? c.offset : c.end_offset) = (w == "offset" ? c.offset : c.end_offset) * c_.units;
            i += 4;
        } else if (w == "fit") {
            c.fit = true;
            radius = true;
            ++i;
            if (i < l.words.size() && number(l.words[i], v)) {
                if (v <= 0.0F || v > 1.0F)
                    return bad("fit's percentile is above 0 and at most 1");
                c.fit_percentile = v;
                ++i;
            }
            if (i + 1 < l.words.size() && l.words[i] == "scale" && number(l.words[i + 1], v) && v > 0.0F) {
                c.fit_scale = v;
                i += 2;
            }
        } else if (w == "clear") {
            c.clear = true;
            ++i;
        } else if (w == "side" && i + 1 < l.words.size() && (l.words[i + 1] == "rest" || l.words[i + 1] == "motion")) {
            c.rest_side = l.words[i + 1] == "rest";
            i += 2;
        } else if (w == "tags") {
            c.tags.assign(l.words.begin() + static_cast<std::ptrdiff_t>(i) + 1, l.words.end());
            i = l.words.size();
        } else {
            return bad("`" + w + "` is not understood here");
        }
    }
    if (!radius)
        return bad("give a radius or `fit`");
    if (c.tags.empty())
        c.tags.push_back("body");
    for (const auto &o : c_.colliders)
        if (o.name == c.name)
            error(l.number, "collider " + c.name + " is defined twice");
    c_.colliders.push_back(std::move(c));
}

// lod <distance> substeps <n> iterations <n> [self_collision off] [environment off] | lod <distance> frozen
void Reader::lod(const Line &l)
{
    LodConfig d;
    if (l.words.size() < 3 || !number(l.words[1], d.distance) || d.distance < 0.0F) {
        error(l.number, "write `lod <distance> substeps <n> iterations <n> [self_collision off] [environment off]`");
        return;
    }
    d.distance = length(d.distance);
    for (std::size_t i = 2; i < l.words.size(); ++i) {
        const std::string &w = l.words[i];
        if (w == "frozen") {
            d.frozen = true;
        } else if ((w == "substeps" || w == "iterations") && i + 1 < l.words.size()) {
            int n = 0;
            if (!integer(l.words[++i], n) || n < 1 || n > 16) {
                error(l.number, "lod: " + w + " is 1 to 16");
                return;
            }
            (w == "substeps" ? d.substeps : d.iterations) = n;
        } else if ((w == "self_collision" || w == "environment") && i + 1 < l.words.size() && l.words[i + 1] == "off") {
            (w == "self_collision" ? d.self_collision : d.environment) = false;
            ++i;
        } else {
            error(l.number, "lod: `" + w + "` is not understood");
            return;
        }
    }
    c_.lods.push_back(d);
}

// control <name> ... end
void Reader::control(const std::vector<Line> &lines, std::size_t &i)
{
    const Line &head = lines[i];
    ControlConfig k;
    k.line = head.number;
    if (head.words.size() != 2)
        error(head.number, "write `control <name>`");
    else
        k.name = head.words[1];
    for (++i; i < lines.size(); ++i) {
        const Line &l = lines[i];
        const std::string &w = l.words[0];
        if (w == "end")
            break;
        if (param_line(l, k.params) || mode_line(l, k.mode, &k.policy))
            continue;
        float v = 0.0F;
        if (w == "animation") {
            k.animations.assign(l.words.begin() + 1, l.words.end());
            if (k.animations.empty())
                error(l.number, "write `animation <game animation>... ` (`*`: any)");
        } else if (w == "slot" && l.words.size() == 2 && integer(l.words[1], k.slot) && k.slot >= 0) {
        } else if (w == "section" && l.words.size() == 2 && number(l.words[1], k.section) && k.section >= 0.0F) {
        } else if (w == "time" && l.words.size() >= 3) {
            if (!number(l.words[1], k.from) || (l.words[2] != "end" && !number(l.words[2], k.to)) || k.from < 0.0F ||
                (l.words[2] != "end" && k.to <= k.from)) {
                error(l.number, "write `time <from> <to>|end [seconds|frames|ticks]`, from before to");
                continue;
            }
            if (l.words[2] == "end")
                k.to = kInf;
            if (l.words.size() == 4) {
                if (l.words[3] == "seconds")
                    k.unit = TimeUnit::seconds;
                else if (l.words[3] == "frames")
                    k.unit = TimeUnit::frames;
                else if (l.words[3] == "ticks")
                    k.unit = TimeUnit::ticks;
                else
                    error(l.number, "time's unit is seconds, frames (the game's, 30 a second) or ticks (60)");
            }
        } else if (w == "loop" && l.words.size() == 2 && (l.words[1] == "on" || l.words[1] == "off")) {
            k.loop = l.words[1] == "on";
        } else if (w == "groups") {
            k.groups.assign(l.words.begin() + 1, l.words.end());
        } else if (w == "bones") {
            bone_list(l, 1, k.bones);
        } else if (w == "weight" && l.words.size() == 2 && number(l.words[1], v) && v >= 0.0F && v <= 1.0F) {
            k.weight = v;
        } else if (w == "blend" && l.words.size() == 3) {
            float a = 0.0F, b = 0.0F;
            if (!number(l.words[1], a) || !number(l.words[2], b) || a < 0.0F || b < 0.0F)
                error(l.number, "write `blend <in seconds> <out seconds>`");
            k.blend_in = a;
            k.blend_out = b;
        } else if (w == "priority" && l.words.size() == 2 && integer(l.words[1], k.priority)) {
        } else if (w == "wind") {
            wind_line(l, k.wind);
        } else {
            error(l.number, "`" + w + "` is not a control setting, or its value is wrong");
        }
    }
    if (i >= lines.size())
        error(head.number, "control " + k.name + " has no `end`");
    if (k.animations.empty())
        error(head.number, "control " + k.name + " names no `animation`");
    if (k.groups.empty() && k.bones.empty())
        error(head.number, "control " + k.name + " names no `groups` or `bones`");
    c_.controls.push_back(std::move(k));
}

void Reader::run(const std::vector<Line> &lines, int depth)
{
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Line &l = lines[i];
        const std::string &w = l.words[0];
        float v = 0.0F;
        int n = 0;
        if (w == "secondary_motion") {
            if (l.words.size() != 2 || !integer(l.words[1], n)) {
                error(l.number, "write `secondary_motion <version>` (this port reads version 1)");
            } else if (n != kFormatVersion) {
                error(l.number, "version " + l.words[1] + " is not one this port reads (it reads " +
                                    std::to_string(kFormatVersion) + ")");
            } else if (depth == 0) {
                c_.version = n;
            }
        } else if (w == "units") {
            if (l.words.size() == 2 && (l.words[1] == "m" || l.words[1] == "metres" || l.words[1] == "meters"))
                c_.units = 1.0F;
            else if (l.words.size() == 2 && (l.words[1] == "cm" || l.words[1] == "centimetres"))
                c_.units = 0.01F;
            else
                error(l.number, "units are m or cm");
        } else if (w == "include") {
            if (l.words.size() != 2) {
                error(l.number, "write `include <file>` (beside this one)");
                continue;
            }
            if (depth >= 4) {
                error(l.number, "includes nest too deeply");
                continue;
            }
            const auto path = folder_ / std::filesystem::path(std::u8string(l.words[1].begin(), l.words[1].end()));
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                error(l.number, "cannot open " + l.words[1] + " (looked beside this file)");
                continue;
            }
            const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            c_.files.push_back(path);
            const float units = c_.units;
            Reader inner(c_, out_, l.words[1], folder_);
            inner.run(lines_of(text), depth + 1);
            c_.units = units; // an include's units stay its own
        } else if (w == "simulation") {
            for (std::size_t k = 1; k + 1 < l.words.size(); k += 2) {
                const std::string &key = l.words[k];
                if (key == "rate" && number(l.words[k + 1], v) && v >= 10.0F && v <= 480.0F)
                    c_.rate = v;
                else if (key == "substeps" && integer(l.words[k + 1], n) && n >= 1 && n <= 16)
                    c_.substeps = n;
                else if (key == "iterations" && integer(l.words[k + 1], n) && n >= 1 && n <= 16)
                    c_.iterations = n;
                else if (key == "max_catchup" && integer(l.words[k + 1], n) && n >= 1 && n <= 30)
                    c_.max_catchup = n;
                else
                    error(l.number, "simulation: rate (10-480 Hz), substeps (1-16), iterations (1-16) or "
                                    "max_catchup (1-30): `" +
                                        key + " " + l.words[k + 1] + "` is not one");
            }
            if (l.words.size() % 2 == 0)
                error(l.number, "simulation: " + l.words.back() + " has no value");
        } else if (w == "gravity") {
            Vec3 g;
            if (l.words.size() != 4 || !vec3(l, 1, g))
                error(l.number, "write `gravity <x> <y> <z>` (m/s^2, the world's y up)");
            else
                c_.gravity = g;
        } else if (w == "teleport") {
            if (l.words.size() != 3 || !number(l.words[1], v) || v <= 0.0F || !number(l.words[2], c_.teleport_degrees))
                error(l.number, "write `teleport <distance> <degrees>`: a jump of the attachment in one tick that "
                                "rebases the state");
            else
                c_.teleport_distance = length(v);
        } else if (w == "idle_reset" && l.words.size() == 2 && number(l.words[1], v) && v > 0.0F) {
            c_.idle_reset = v;
        } else if (w == "environment") {
            if (l.words.size() >= 2 && (l.words[1] == "on" || l.words[1] == "off"))
                c_.environment = l.words[1] == "on";
            else
                error(l.number, "write `environment on|off [thickness <t>]`");
            if (l.words.size() == 4 && l.words[2] == "thickness" && number(l.words[3], v) && v >= 0.0F)
                c_.environment_thickness = length(v);
        } else if (w == "first_pose_iterations" && l.words.size() == 2 && integer(l.words[1], n) && n >= 0 &&
                   n <= 500) {
            c_.settle_iterations = n;
        } else if (w == "wind") {
            wind_line(l, c_.wind);
        } else if (w == "lod") {
            lod(l);
        } else if (w == "debug" && l.words.size() == 2) {
            c_.debug = l.words[1] == "on";
        } else if (w == "profile" && l.words.size() == 2) {
            c_.profile = l.words[1] == "on";
        } else if (w == "preset") {
            preset(lines, i);
        } else if (w == "group") {
            group(lines, i);
        } else if (w == "collider") {
            collider(l);
        } else if (w == "control") {
            control(lines, i);
        } else {
            error(l.number, "`" + w + "` is not a statement of this file");
        }
    }
}

// presets' `extends` resolved, then each group's preset under its own settings
void resolve(Config &c, Diagnostics &out, const std::string &file)
{
    std::map<std::string, const PresetConfig *> by_name;
    for (const auto &p : c.presets)
        by_name[p.name] = &p;
    const auto chain = [&](const std::string &start, int line, std::vector<const PresetConfig *> &list) {
        std::set<std::string> seen;
        for (std::string name = start; !name.empty();) {
            const auto it = by_name.find(name);
            if (it == by_name.end()) {
                out.push_back({true, file, line, "there is no preset " + name});
                return false;
            }
            if (!seen.insert(name).second) {
                out.push_back({true, file, line, "presets extend each other in a circle at " + name});
                return false;
            }
            list.push_back(it->second);
            name = it->second->extends;
        }
        return true;
    };
    std::set<std::string> names;
    for (auto &g : c.groups) {
        if (!names.insert(g.name).second)
            out.push_back({true, file, g.line, "group " + g.name + " is defined twice"});
        ParamSet merged = default_params();
        ModeSetting mode;
        PolicySetting policy;
        std::vector<std::string> collide; // none named: every collider
        bool collide_given = false;
        std::string self;
        std::vector<const PresetConfig *> list;
        if (!g.preset.empty() && chain(g.preset, g.line, list)) {
            for (auto it = list.rbegin(); it != list.rend(); ++it) { // the base first
                merged.merge((*it)->params);
                if ((*it)->mode.given)
                    mode = (*it)->mode;
                if ((*it)->policy.given)
                    policy = (*it)->policy;
                if ((*it)->collide_given) {
                    collide = (*it)->collide;
                    collide_given = true;
                }
                if ((*it)->self_given)
                    self = (*it)->self_collision;
            }
        }
        merged.merge(g.params);
        g.params = merged;
        if (!g.mode.given)
            g.mode = mode;
        if (!g.policy.given)
            g.policy = policy;
        if (!g.collide_given) {
            g.collide = collide;
            g.collide_given = collide_given;
        }
        if (!g.self_given)
            g.self_collision = self;
        if (g.strips.empty())
            out.push_back({true, file, g.line, "group " + g.name + " has no controls (strip, chain, tuft or tufts)"});
        if (g.attach.empty() && g.kind != GroupKind::hair)
            out.push_back({true, file, g.line,
                           "group " + g.name + " names no `attach` bone (the body bone its reference shape follows)"});
    }
    for (const auto &k : c.controls)
        for (const auto &gname : k.groups)
            if (!names.count(gname))
                out.push_back({true, file, k.line, "control " + k.name + ": there is no group " + gname});
}

} // namespace

const ParamInfo &param_info(Param p)
{
    return kParams[static_cast<int>(p)];
}

bool param_named(const std::string &name, Param &out)
{
    for (int i = 0; i < kParamCount; ++i)
        if (name == kParams[i].name) {
            out = static_cast<Param>(i);
            return true;
        }
    return false;
}

void ParamSet::merge(const ParamSet &over)
{
    for (std::size_t i = 0; i < static_cast<std::size_t>(kParamCount); ++i) {
        if (over.given[i]) {
            value[i] = over.value[i];
            given.set(i);
            if (!over.hem_given[i])
                hem_given.reset(i); // a new plain value without a hem is uniform
        }
        if (over.hem_given[i]) {
            hem[i] = over.hem[i];
            curve[i] = over.curve[i];
            hem_given.set(i);
        }
    }
}

float ParamSet::at(Param p, float depth) const
{
    const auto i = static_cast<std::size_t>(p);
    const float v = given[i] ? value[i] : param_info(p).fallback;
    if (!hem_given[i])
        return v;
    const float d = std::fmin(1.0F, std::fmax(0.0F, depth));
    return v + (hem[i] - v) * std::pow(d, curve[i] > 0.0F ? curve[i] : 1.0F);
}

ParamSet default_params()
{
    ParamSet s;
    for (int i = 0; i < kParamCount; ++i)
        s.value[static_cast<std::size_t>(i)] = kParams[i].fallback;
    return s;
}

std::string format(const Diagnostic &d)
{
    return d.file + (d.line > 0 ? ":" + std::to_string(d.line) : std::string()) + ": " +
           (d.error ? "error: " : "warning: ") + d.text;
}

bool has_errors(const Diagnostics &d)
{
    return std::any_of(d.begin(), d.end(), [](const Diagnostic &x) { return x.error; });
}

Config parse_config(const std::string &text, const std::string &name, Diagnostics &out,
                    const std::filesystem::path &folder)
{
    Config c;
    const std::size_t before = out.size();
    Reader reader(c, out, name, folder);
    reader.run(lines_of(text), 0);
    if (c.version == 0 && !has_errors(Diagnostics(out.begin() + static_cast<std::ptrdiff_t>(before), out.end())))
        out.push_back({true, name, 1, "the file does not start with `secondary_motion 1` (its format version)"});
    resolve(c, out, name);
    std::sort(c.lods.begin(), c.lods.end(),
              [](const LodConfig &a, const LodConfig &b) { return a.distance < b.distance; });
    return c;
}

Config read_config(const std::filesystem::path &path, Diagnostics &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out.push_back({true, path.filename().string(), 0, "cannot open the file"});
        return {};
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const auto name = path.filename().u8string();
    Config c = parse_config(text, std::string(name.begin(), name.end()), out, path.parent_path());
    c.files.insert(c.files.begin(), path);
    return c;
}

} // namespace khdays::secondary
