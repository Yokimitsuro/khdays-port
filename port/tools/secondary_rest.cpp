// khdays-secondary-rest: a model's cloth reference shape computed once, as prepare_references would at load (each
// group pushed out of the colliders it collides with, the `settle on` groups hung under gravity against them),
// and written as `rest <bone> <x> <y> <z>` lines in its groups (docs/SECONDARY_MOTION.md, "Rest shape"). The game
// then takes the shape from those lines instead of simulating it at load.
#include "collision.h"
#include "config.h"
#include "rig.h"
#include "solver.h"

#include "khdays/assets/kh2_mdlx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace khdays;
using namespace khdays::secondary;

namespace {

struct Turn {
    std::string bone; // as the file names it
    Vec3 degrees;
};

std::string bone_text(const SkeletonDesc &sk, int b)
{
    const std::string &n = sk.names[static_cast<std::size_t>(b)];
    return n.rfind("kh2_", 0) == 0 ? n.substr(4) : n;
}

// as the lines write it (no "-0.00")
Vec3 two_decimals(Vec3 d)
{
    const auto r = [](float v) {
        const float x = std::round(v * 100.0F) / 100.0F;
        return x == 0.0F ? 0.0F : x;
    };
    return {r(d.x), r(d.y), r(d.z)};
}

std::vector<Frame> rest_pose(const Rig &rig)
{
    std::vector<Frame> pose(rig.skeleton.rest.size());
    for (std::size_t b = 0; b < pose.size(); ++b)
        pose[b] = frame_of(rig.skeleton.rest[b]);
    return pose;
}

std::vector<Vec3> references(const Rig &rig, const std::vector<Frame> &pose)
{
    std::vector<Vec3> at(rig.particles.size());
    for (std::size_t i = 0; i < rig.particles.size(); ++i) {
        const Particle &p = rig.particles[i];
        const int ref = rig.groups[static_cast<std::size_t>(p.group)].reference_bone;
        at[i] = p.dynamic ? apply(pose[static_cast<std::size_t>(ref)], p.reference) : Vec3{};
    }
    return at;
}

// the most a control of group g is inside a collider it collides with (cm; 0 none), as the load's check measures it
float deepest(const Rig &rig, const std::vector<Frame> &pose, const std::vector<Vec3> &at, int g,
              const std::vector<std::uint64_t> &excluded)
{
    float most = 0.0F;
    for (std::size_t c = 0; c < rig.colliders.size(); ++c) {
        const Collider &k = rig.colliders[c];
        const Vec3 a = apply(pose[static_cast<std::size_t>(k.bone)], k.offset);
        const Vec3 b =
            k.kind == ShapeKind::capsule ? apply(pose[static_cast<std::size_t>(k.end_bone)], k.end_offset) : a;
        for (std::size_t i = 0; i < rig.particles.size(); ++i) {
            const Particle &p = rig.particles[i];
            if (!p.dynamic || p.group != g || !(p.collide & k.tags) ||
                std::binary_search(excluded.begin(), excluded.end(), (static_cast<std::uint64_t>(c) << 32) | i))
                continue;
            const float t = along_segment(at[i], a, b);
            const float r = k.radius + (k.end_radius - k.radius) * t;
            const float clearance =
                rig.point_clearance.empty() ? 0.0F : rig.point_clearance[c * rig.particles.size() + i];
            const float inside =
                pair_reach(r + p.thickness, clearance, 0.5F * r + p.thickness) - length(at[i] - lerp(a, b, t));
            most = std::max(most, inside * 100.0F);
        }
    }
    return most;
}

// how far control i at x is inside the colliders it collides with, as the way out (0: outside)
Vec3 way_out(const Rig &rig, const std::vector<Frame> &pose, std::size_t i, Vec3 x,
             const std::vector<std::uint64_t> &excluded)
{
    Vec3 out{};
    const Particle &p = rig.particles[i];
    for (std::size_t c = 0; c < rig.colliders.size(); ++c) {
        const Collider &k = rig.colliders[c];
        if (!(p.collide & k.tags) ||
            std::binary_search(excluded.begin(), excluded.end(), (static_cast<std::uint64_t>(c) << 32) | i))
            continue;
        const Vec3 a = apply(pose[static_cast<std::size_t>(k.bone)], k.offset);
        const Vec3 b =
            k.kind == ShapeKind::capsule ? apply(pose[static_cast<std::size_t>(k.end_bone)], k.end_offset) : a;
        const float t = along_segment(x, a, b);
        const float r = k.radius + (k.end_radius - k.radius) * t;
        const float clearance = rig.point_clearance.empty() ? 0.0F : rig.point_clearance[c * rig.particles.size() + i];
        const Vec3 core = lerp(a, b, t);
        const float inside = pair_reach(r + p.thickness, clearance, 0.5F * r + p.thickness) - length(x - core);
        if (inside > 0.0F)
            out += normalized(x - core) * (inside + 0.001F);
    }
    return out;
}

// the turns' shape kept out of the colliders: a bone whose control lands inside one is turned about its head until
// it is out (a turn cannot give the few millimetres a link stretched in the hang), its children with it
void keep_outside(Rig &turned, const std::vector<Frame> &pose, const std::vector<std::uint64_t> &excluded)
{
    for (int pass = 0; pass < 40; ++pass) {
        bool moved = false;
        for (const SimBone &sb : turned.bones_out) {
            const Particle &p = turned.particles[static_cast<std::size_t>(sb.particle)];
            const int parent = turned.skeleton.parent[static_cast<std::size_t>(sb.bone)];
            auto &rest = turned.groups[static_cast<std::size_t>(sb.group)].rest;
            const auto it = std::find_if(rest.begin(), rest.end(), [&](const auto &r) { return r.first == sb.bone; });
            if (!p.dynamic || parent < 0 || it == rest.end())
                continue;
            const std::vector<Frame> shape = rest_shape(turned, pose);
            const Frame &f = shape[static_cast<std::size_t>(sb.bone)];
            const Vec3 x = apply(f, p.offset);
            const Vec3 push = way_out(turned, pose, static_cast<std::size_t>(sb.particle), x, excluded);
            if (length(push) < 1e-5F || length(x - f.t) < 1e-4F)
                continue;
            const Quat swing = arc(normalized(x - f.t), normalized(x + push - f.t));
            it->second = normalized(conjugate(shape[static_cast<std::size_t>(parent)].q) * (swing * f.q));
            moved = true;
        }
        if (!moved)
            return;
    }
}

// `file` with each group's `rest` lines (and the note above them) replaced by `turns`
bool write_file(const std::string &file, const std::map<std::string, std::vector<Turn>> &turns)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);)
        lines.push_back(!l.empty() && l.back() == '\r' ? l.substr(0, l.size() - 1) : l);
    const auto first_word = [](const std::string &l) {
        std::istringstream s(l);
        std::string w;
        s >> w;
        return w;
    };
    const std::string note = "# rest shape: hung against the colliders by khdays-secondary-rest (rerun it after "
                             "changing the group or its colliders)";
    std::vector<std::string> out;
    std::string group;
    std::string indent = "  ";
    for (const std::string &l : lines) {
        const std::string w = first_word(l);
        if (group.empty() && w == "group") {
            std::istringstream s(l);
            s >> group >> group;
            out.push_back(l);
            continue;
        }
        if (!group.empty()) {
            if (w == "rest" || l.find(note) != std::string::npos)
                continue;
            if (w == "end") {
                const auto it = turns.find(group);
                if (it != turns.end() && !it->second.empty()) {
                    out.push_back(indent + note);
                    char text[160];
                    for (const Turn &t : it->second) {
                        std::snprintf(text, sizeof(text), "%srest %s %.2f %.2f %.2f", indent.c_str(), t.bone.c_str(),
                                      static_cast<double>(t.degrees.x), static_cast<double>(t.degrees.y),
                                      static_cast<double>(t.degrees.z));
                        out.push_back(text);
                    }
                }
                group.clear();
            } else if (!w.empty() && w[0] != '#') {
                indent = l.substr(0, l.find_first_not_of(" \t"));
            }
        }
        out.push_back(l);
    }
    std::ofstream o(file, std::ios::binary);
    for (const std::string &l : out)
        o << l << '\n';
    return static_cast<bool>(o);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: khdays-secondary-rest <model.mdlx> <model.secondary.txt> [--write <file>]\n"
                             "  prints each group's `rest` lines; --write puts them in <file> (the .secondary.txt "
                             "itself, or the one it was copied from)\n");
        return 2;
    }
    std::string write;
    for (int i = 3; i < argc; ++i)
        if (std::string(argv[i]) == "--write" && i + 1 < argc)
            write = argv[++i];
    try {
        const auto imported = assets::kh2::import_mdlx(argv[1]);
        SkeletonDesc sk;
        MeshDesc mesh;
        describe(imported, sk, mesh);
        Diagnostics diag;
        Config config = read_config(argv[2], diag);
        for (auto &g : config.groups) // computed afresh, not from the lines it may have
            g.rest.clear();
        Rig hung = cook(config, sk, mesh, diag);
        if (has_errors(diag)) {
            for (const auto &d : diag)
                std::fprintf(stderr, "%s\n", format(d).c_str());
            return 1;
        }
        Diagnostics ignored;
        prepare_references(hung, &ignored, false); // every step asked, as the load did
        const std::vector<Frame> pose = rest_pose(hung);
        // each rebuilt bone's turn on its parent as the hung shape has it, parents first
        Rig turned = hung;
        for (const SimBone &sb : hung.bones_out) {
            const Particle &p = hung.particles[static_cast<std::size_t>(sb.particle)];
            const int parent = hung.skeleton.parent[static_cast<std::size_t>(sb.bone)];
            if (!p.dynamic || parent < 0)
                continue;
            const int ref = hung.groups[static_cast<std::size_t>(sb.group)].reference_bone;
            const Frame world = compose(pose[static_cast<std::size_t>(ref)], sb.reference);
            const std::vector<Frame> shape = rest_shape(turned, pose);
            const Quat local = normalized(conjugate(shape[static_cast<std::size_t>(parent)].q) * world.q);
            turned.groups[static_cast<std::size_t>(sb.group)].rest.push_back({sb.bone, local});
        }
        keep_outside(turned, pose, hung.collider_excluded);
        for (auto &g : turned.groups) // as written: two decimals
            for (auto &r : g.rest)
                r.second = rest_turn(two_decimals(rest_degrees(r.second)));
        // the shape the lines give, as the load builds it from the cooked rig, against the hung one
        Rig fresh = cook(config, sk, mesh, diag);
        for (std::size_t g = 0; g < fresh.groups.size(); ++g)
            fresh.groups[g].rest = turned.groups[g].rest;
        Diagnostics left;
        prepare_references(fresh, &left);
        const std::vector<Vec3> want = references(hung, pose), got = references(fresh, pose);
        std::map<std::string, std::vector<Turn>> lines;
        float worst = 0.0F;
        for (std::size_t g = 0; g < fresh.groups.size(); ++g) {
            const GroupRig &gr = fresh.groups[g];
            if (gr.rest.empty())
                continue;
            float off = 0.0F;
            for (int i = gr.first; i < gr.last; ++i)
                if (fresh.particles[static_cast<std::size_t>(i)].dynamic)
                    off = std::max(off, length(got[static_cast<std::size_t>(i)] - want[static_cast<std::size_t>(i)]));
            worst = std::max(worst, off * 100.0F);
            const float in_hung = deepest(hung, pose, want, static_cast<int>(g), hung.collider_excluded);
            const float in_got = deepest(fresh, pose, got, static_cast<int>(g), hung.collider_excluded);
            std::fprintf(stderr,
                         "%s: %zu bones, %.2f cm off the hung shape at most; inside a collider %.2f cm (hung "
                         "%.2f)\n",
                         gr.name.c_str(), gr.rest.size(), static_cast<double>(off * 100.0F),
                         static_cast<double>(in_got), static_cast<double>(in_hung));
            auto &out = lines[gr.name];
            for (const auto &[b, q] : gr.rest) {
                out.push_back({bone_text(sk, b), two_decimals(rest_degrees(q))});
                const Turn &t = out.back();
                std::printf("%s rest %s %.2f %.2f %.2f\n", gr.name.c_str(), t.bone.c_str(),
                            static_cast<double>(t.degrees.x), static_cast<double>(t.degrees.y),
                            static_cast<double>(t.degrees.z));
            }
        }
        std::fprintf(stderr, "worst: %.2f cm off the hung shape\n", static_cast<double>(worst));
        if (!write.empty() && !write_file(write, lines)) {
            std::fprintf(stderr, "could not write %s: check it exists and is not read-only\n", write.c_str());
            return 1;
        }
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s: %s\n", argv[1], e.what());
        return 1;
    }
}
