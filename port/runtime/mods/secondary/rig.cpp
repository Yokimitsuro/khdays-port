// Cooking a model's secondary motion (rig.h, docs/SECONDARY_MOTION.md).

#include "rig.h"

#include "collision.h"

#include "khdays/assets/model_import.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>
#include <sstream>

namespace khdays::secondary {

namespace {

constexpr float kTiny = 1e-4F; // m: shorter links are degenerate

struct Cook {
    const Config &config;
    const SkeletonDesc &sk;
    const MeshDesc &mesh;
    Diagnostics &out;
    Rig &rig;
    std::string file;
    std::vector<Frame> rest;
    std::vector<std::vector<int>> children;
    std::vector<int> owner_group; // per bone: the group whose control it is (-1)
    std::vector<int> particle_of; // per bone: its control particle (-1)
    std::vector<int> dominant;    // per vertex: its heaviest bone

    void error(int line, const std::string &text)
    {
        out.push_back({true, file, line, text});
    }
    void warn(int line, const std::string &text)
    {
        out.push_back({false, file, line, text});
    }
    bool descends(int bone, int from) const
    {
        for (int up = sk.parent[static_cast<std::size_t>(bone)]; up >= 0; up = sk.parent[static_cast<std::size_t>(up)])
            if (up == from)
                return true;
        return false;
    }
    Vec3 head(int bone) const
    {
        return rest[static_cast<std::size_t>(bone)].t;
    }
    Vec3 world(int bone, Vec3 local) const
    {
        return apply(rest[static_cast<std::size_t>(bone)], local);
    }
    Vec3 local(int bone, Vec3 w) const
    {
        return apply_inverse(rest[static_cast<std::size_t>(bone)], w);
    }
    std::uint32_t tags(const std::vector<std::string> &names, int line, const std::string &who)
    {
        std::uint32_t bits = 0;
        for (const auto &n : names) {
            const auto it = std::find(rig.tag_names.begin(), rig.tag_names.end(), n);
            if (it == rig.tag_names.end()) {
                if (n != "none")
                    error(line, who + ": no collider carries the tag " + n);
                continue;
            }
            bits |= 1U << static_cast<unsigned>(it - rig.tag_names.begin());
        }
        return bits;
    }

    void colliders();
    float fit(int bone, Vec3 a, Vec3 b, float percentile, float from_t, float to_t) const;
    void group(std::size_t gi);
    void finish();
};

// the radius holding `percentile` of the vertices skinned mostly to `bone` around segment a-b (only
// those whose closest point lies between from_t and to_t along it)
float Cook::fit(int bone, Vec3 a, Vec3 b, float percentile, float from_t, float to_t) const
{
    std::vector<float> d;
    const Vec3 ab = b - a;
    const float ll = dot(ab, ab);
    for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
        if (dominant[v] != bone)
            continue;
        const Vec3 p = mesh.vertices[v].position;
        // a capsule's vertices beside its segment only: those past its ends (a hip above a thigh) measured
        // to the end would size it by its neighbour
        const float t = ll > 0.0F ? dot(p - a, ab) / ll : 0.0F;
        if (t < from_t || t > to_t)
            continue;
        d.push_back(length(p - (a + ab * t)));
    }
    if (d.empty())
        return -1.0F;
    std::sort(d.begin(), d.end());
    return d[std::min(d.size() - 1, static_cast<std::size_t>(percentile * static_cast<float>(d.size())))];
}

void Cook::colliders()
{
    for (const auto &c : config.colliders)
        for (const auto &t : c.tags)
            if (std::find(rig.tag_names.begin(), rig.tag_names.end(), t) == rig.tag_names.end()) {
                if (rig.tag_names.size() >= 32) {
                    error(c.line, "more than 32 collider tags: share a tag between colliders that act alike");
                    break;
                }
                rig.tag_names.push_back(t);
            }
    for (const auto &c : config.colliders) {
        Collider k;
        k.name = c.name;
        k.kind = c.kind;
        k.bone = resolve_bone(sk, c.bone, file, out);
        if (k.bone < 0)
            continue;
        k.offset = c.offset;
        k.rest_side = c.rest_side;
        k.radius = c.radius;
        k.end_radius = c.end_radius >= 0.0F ? c.end_radius : c.radius;
        if (c.kind == ShapeKind::capsule) {
            if (c.end_given) {
                k.end_bone = resolve_bone(sk, c.end_bone, file, out);
                if (k.end_bone < 0)
                    continue;
                k.end_offset = c.end_offset;
            } else {
                k.end_bone = k.bone;
                k.end_offset = c.offset + Vec3{c.length, 0.0F, 0.0F};
            }
        }
        const Vec3 a = world(k.bone, k.offset);
        const Vec3 b = c.kind == ShapeKind::capsule ? world(k.end_bone, k.end_offset) : a;
        if (c.kind == ShapeKind::capsule && length(b - a) < kTiny) {
            error(c.line, "collider " + c.name +
                              ": its two ends are at the same place (give an end bone further on, "
                              "or `length`)");
            continue;
        }
        if (c.fit) {
            const float r0 = c.kind == ShapeKind::sphere ? fit(k.bone, a, a, c.fit_percentile, 0.0F, 1.0F)
                                                         : fit(k.bone, a, b, c.fit_percentile, 0.0F, 0.5F);
            const float r1 = c.kind == ShapeKind::sphere ? r0 : fit(k.bone, a, b, c.fit_percentile, 0.5F, 1.0F);
            if (r0 < 0.0F && r1 < 0.0F) {
                error(c.line, "collider " + c.name + ": `fit` found no vertices skinned to " + c.bone.text +
                                  " (give a radius)");
                continue;
            }
            k.radius = (r0 >= 0.0F ? r0 : r1) * c.fit_scale;
            k.end_radius = (r1 >= 0.0F ? r1 : r0) * c.fit_scale;
        }
        if (!(k.radius > 0.0F) && !(k.end_radius > 0.0F)) {
            error(c.line, "collider " + c.name + " has no size");
            continue;
        }
        for (const auto &t : c.tags)
            k.tags |= 1U << static_cast<unsigned>(std::find(rig.tag_names.begin(), rig.tag_names.end(), t) -
                                                  rig.tag_names.begin());
        rig.colliders.push_back(k);
    }
}

void Cook::group(std::size_t gi)
{
    const GroupConfig &g = config.groups[gi];
    GroupRig gr;
    gr.name = g.name;
    gr.kind = g.kind;
    gr.wind = g.wind;
    if (g.wind.given && g.wind.space == WindSpace::bone)
        gr.wind_bone = resolve_bone(sk, g.wind.bone, file, out);
    gr.settle = g.settle;
    gr.settle_steps = g.settle_steps;
    gr.shell_push_from = g.shell_push_from;
    gr.environment = g.environment;
    gr.clip_world = g.clip_world && g.kind == GroupKind::surface;
    gr.clip_inset = g.clip_inset;
    gr.twist_from_neighbours = g.twist_from_neighbours && g.kind == GroupKind::surface;
    gr.first = static_cast<int>(rig.particles.size());
    const int group_index = static_cast<int>(rig.groups.size());
    std::vector<int> attach;
    for (const auto &a : g.attach) {
        const int b = resolve_bone(sk, a, file, out);
        if (b >= 0)
            attach.push_back(b);
    }
    std::set<int> excluded;
    for (const auto &e : g.exclude) {
        const int b = resolve_bone(sk, e, file, out);
        if (b >= 0)
            excluded.insert(b);
    }
    // the controls, strip by strip; a bone in two strips (a fork) is one control
    std::vector<std::vector<int>> strips;
    std::vector<int> controls;
    for (const auto &s : g.strips) {
        std::vector<int> strip;
        for (const auto &ref : s) {
            const int b = resolve_bone(sk, ref, file, out);
            if (b < 0 || excluded.count(b))
                continue;
            const int other = owner_group[static_cast<std::size_t>(b)];
            if (other >= 0 && other != group_index) {
                error(ref.line, "bone " + ref.text + " is a control of group " +
                                    config.groups[static_cast<std::size_t>(other)].name +
                                    " already (a bone has one owner)");
                continue;
            }
            if (std::find(attach.begin(), attach.end(), b) != attach.end()) {
                error(ref.line, "bone " + ref.text + " is both attached to and simulated in group " + g.name);
                continue;
            }
            if (other < 0) {
                owner_group[static_cast<std::size_t>(b)] = group_index;
                controls.push_back(b);
            }
            if (std::find(strip.begin(), strip.end(), b) != strip.end()) {
                error(ref.line, "bone " + ref.text + " is twice in one strip");
                continue;
            }
            strip.push_back(b);
        }
        if (!strip.empty())
            strips.push_back(std::move(strip));
    }
    if (controls.empty()) {
        error(g.line, "group " + g.name + " has no controls left");
        rig.groups.push_back(gr); // its place kept: groups are indexed by their order
        return;
    }
    gr.reference_bone = !attach.empty() ? attach.front() : sk.parent[static_cast<std::size_t>(controls.front())];
    if (gr.reference_bone < 0) {
        error(g.line, "group " + g.name + ": its first control is a root bone; name an `attach` bone");
        rig.groups.push_back(gr);
        return;
    }
    const std::uint32_t mask = g.collide_given ? tags(g.collide, g.line, "group " + g.name) : 0xffffffffU;
    gr.shell_push = g.shell_push.empty() ? 0U : tags(g.shell_push, g.line, "group " + g.name + ": shell_push");
    // self-collision groups by name
    int self = -1;
    if (!g.self_collision.empty()) {
        for (std::size_t k = 0; k < rig.groups.size() && k < gi; ++k)
            if (config.groups[k].self_collision == g.self_collision) {
                self = rig.groups[k].self_group;
                break;
            }
        if (self < 0) {
            int most = -1;
            for (const auto &x : rig.groups)
                most = std::max(most, x.self_group);
            self = most + 1;
        }
    }
    gr.self_group = self;

    // a control's settings: the group's, then its bone's overrides
    std::map<int, ParamSet> bone_params;
    std::map<int, ModeSetting> bone_mode;
    for (const auto &o : g.overrides) {
        const int b = resolve_bone(sk, o.bone, file, out);
        if (b < 0)
            continue;
        if (std::find(controls.begin(), controls.end(), b) == controls.end()) {
            error(o.bone.line, "bone " + o.bone.text + " is not a control of group " + g.name);
            continue;
        }
        bone_params[b].merge(o.params);
        if (o.mode.given)
            bone_mode[b] = o.mode;
    }
    std::map<int, Vec3> explicit_offset;
    for (const auto &[ref, v] : g.offsets) {
        const int b = resolve_bone(sk, ref, file, out);
        if (b >= 0)
            explicit_offset[b] = v;
    }

    // levels down each strip (a fork's second strip continues from the shared control)
    std::map<int, int> level;
    std::map<int, int> strip_length; // per control: its strip's length in levels
    for (const auto &s : strips) {
        int l0 = level.count(s.front()) ? level[s.front()] : 0;
        for (std::size_t i = 0; i < s.size(); ++i)
            if (!level.count(s[i]))
                level[s[i]] = l0 + static_cast<int>(i);
        const int deepest = l0 + static_cast<int>(s.size());
        for (const int b : s)
            strip_length[b] = std::max(strip_length[b], deepest);
    }

    // each control's particle at its point: the next control's head when that is below it, else the
    // middle of its vertices (influence-weighted), else its first child's head
    for (const int b : controls) {
        Particle p;
        p.bone = b;
        p.group = group_index;
        p.dynamic = true;
        p.collide = mask;
        p.self_group = self;
        Vec3 offset;
        bool found = false;
        if (const auto e = explicit_offset.find(b); e != explicit_offset.end()) {
            offset = e->second;
            found = true;
        }
        for (const auto &s : strips) {
            for (std::size_t i = 0; !found && i + 1 < s.size(); ++i)
                if (s[i] == b && descends(s[i + 1], b)) {
                    offset = local(b, head(s[i + 1]));
                    found = length(offset) > kTiny;
                }
        }
        int mixed = 0, own = 0;
        if (!found) {
            Vec3 sum;
            float total = 0.0F;
            for (const auto &v : mesh.vertices) {
                for (std::size_t k = 0; k < 4; ++k) {
                    if (v.bones[k] != b || v.weights[k] < 0.25F)
                        continue;
                    sum += local(b, v.position) * v.weights[k];
                    total += v.weights[k];
                    ++own;
                    mixed += v.weights[k] < 0.999F ? 1 : 0;
                }
            }
            if (total > 0.0F) {
                offset = sum * (1.0F / total);
                found = length(offset) > kTiny;
            }
        }
        if (!found)
            for (const int c : children[static_cast<std::size_t>(b)])
                if (length(head(c) - head(b)) > kTiny) {
                    offset = local(b, head(c));
                    found = true;
                    warn(g.line, "group " + g.name + ": control " + sk.names[static_cast<std::size_t>(b)] +
                                     " has no vertices of its own; its point is its child's head");
                    break;
                }
        if (!found) {
            error(g.line, "group " + g.name + ": control " + sk.names[static_cast<std::size_t>(b)] +
                              " is a zero-length helper with no vertices and no child further on: give it `offset "
                              "<bone> <x> <y> <z>` or leave it out");
            continue;
        }
        if (mixed > 0)
            warn(g.line, "group " + g.name + ": control " + sk.names[static_cast<std::size_t>(b)] + " shares " +
                             std::to_string(mixed) + " of its " + std::to_string(own) +
                             " vertices with other bones; they follow it only by their weight");
        p.offset = offset;
        const float lv = static_cast<float>(level[b] + 1), n = static_cast<float>(std::max(1, strip_length[b]));
        p.depth = std::fmin(1.0F, lv / n);
        ParamSet ps = g.params;
        if (const auto o = bone_params.find(b); o != bone_params.end())
            ps.merge(o->second);
        ParamBlock block;
        for (int k = 0; k < kParamCount; ++k)
            block.v[static_cast<std::size_t>(k)] = ps.at(static_cast<Param>(k), p.depth);
        block.mode = bone_mode.count(b) ? bone_mode[b].mode : g.mode.mode;
        block.policy = g.policy.policy;
        p.inv_mass = 1.0F / block.get(Param::mass);
        p.thickness = block.get(Param::thickness);
        p.params = static_cast<int>(rig.params.size());
        rig.params.push_back(block);
        particle_of[static_cast<std::size_t>(b)] = static_cast<int>(rig.particles.size());
        rig.particles.push_back(p);
    }
    if (has_errors(out)) {
        rig.groups.push_back(gr);
        return;
    }
    // anchors and links down each strip
    std::set<std::uint64_t> linked;
    const auto add_distance = [&](int a, int b, Link kind) {
        if (a == b || !linked.insert(pair_key(a, b)).second)
            return;
        const Particle &pa = rig.particles[static_cast<std::size_t>(a)],
                       &pb = rig.particles[static_cast<std::size_t>(b)];
        const float d = length(world(pa.bone, pa.offset) - world(pb.bone, pb.offset));
        if (d < kTiny) {
            error(g.line, "group " + g.name + ": " + sk.names[static_cast<std::size_t>(pa.bone)] + " and " +
                              sk.names[static_cast<std::size_t>(pb.bone)] +
                              " are at the same place at rest: a link between them is degenerate (disconnect them, or "
                              "give an `offset`)");
            return;
        }
        rig.distances.push_back({a, b, d, kind});
    };
    std::map<int, int> anchor_of_head; // bone -> its kinematic anchor particle
    for (const auto &s : strips) {
        for (std::size_t i = 0; i < s.size(); ++i) {
            const int pi = particle_of[static_cast<std::size_t>(s[i])];
            if (pi < 0)
                continue;
            Particle &p = rig.particles[static_cast<std::size_t>(pi)];
            if (p.anchor >= 0)
                continue; // a shared control, linked from its first strip
            int prev = -1;
            if (i > 0)
                prev = particle_of[static_cast<std::size_t>(s[i - 1])];
            else
                for (int up = sk.parent[static_cast<std::size_t>(s[i])]; up >= 0 && prev < 0;
                     up = sk.parent[static_cast<std::size_t>(up)])
                    if (owner_group[static_cast<std::size_t>(up)] == group_index)
                        prev = -2; // below another control of the group: a carried anchor
            if (prev >= 0) {
                rig.particles[static_cast<std::size_t>(pi)].anchor = prev;
                add_distance(prev, pi, Link::stretch);
                continue;
            }
            // a kinematic anchor at the control's head, riding its parent
            const int parent = sk.parent[static_cast<std::size_t>(s[i])];
            if (parent < 0) {
                error(g.line, "group " + g.name + ": " + sk.names[static_cast<std::size_t>(s[i])] +
                                  " is a root bone and cannot hang from anything");
                continue;
            }
            Particle a;
            a.bone = parent;
            a.offset = local(parent, head(s[i]));
            a.group = group_index;
            a.dynamic = false;
            a.collide = 0;
            a.params = rig.particles[static_cast<std::size_t>(pi)].params;
            a.thickness = rig.particles[static_cast<std::size_t>(pi)].thickness;
            a.carrier = prev == -2 ? -3 : -1; // resolved once the bones are ordered
            const int ai = static_cast<int>(rig.particles.size());
            rig.particles.push_back(a);
            anchor_of_head[s[i]] = ai;
            rig.particles[static_cast<std::size_t>(pi)].anchor = ai;
            add_distance(ai, pi, Link::anchor);
        }
    }
    gr.last = static_cast<int>(rig.particles.size());

    // neighbours from the mesh: triangles whose corners' heaviest bones are three (or two) controls
    std::set<std::array<int, 3>> faces;
    std::set<std::uint64_t> cut;
    for (const auto &[a, b] : g.disconnect) {
        const int x = resolve_bone(sk, a, file, out), y = resolve_bone(sk, b, file, out);
        if (x >= 0 && y >= 0)
            cut.insert(pair_key(x, y));
    }
    std::set<std::uint64_t> mesh_edges;
    if (g.mesh_topology) {
        for (std::size_t t = 0; t + 2 < mesh.triangles.size(); t += 3) {
            std::array<int, 3> c{};
            bool all = true;
            for (std::size_t k = 0; k < 3; ++k) {
                const int b = dominant[mesh.triangles[t + k]];
                c[k] = b >= 0 && owner_group[static_cast<std::size_t>(b)] == group_index ? b : -1;
                all = all && c[k] >= 0;
            }
            for (std::size_t x = 0; x < 3; ++x)
                for (std::size_t y = x + 1; y < 3; ++y)
                    if (c[x] >= 0 && c[y] >= 0 && c[x] != c[y] && !cut.count(pair_key(c[x], c[y])))
                        mesh_edges.insert(pair_key(c[x], c[y]));
            if (all && c[0] != c[1] && c[1] != c[2] && c[0] != c[2] && !cut.count(pair_key(c[0], c[1])) &&
                !cut.count(pair_key(c[1], c[2])) && !cut.count(pair_key(c[0], c[2]))) {
                std::sort(c.begin(), c.end());
                faces.insert(c);
            }
        }
        if (mesh_edges.empty())
            warn(g.line, "group " + g.name +
                             ": `topology mesh` found no triangles joining its controls; they move "
                             "as separate strips");
    }
    for (const auto &[a, b] : g.connect) {
        const int x = resolve_bone(sk, a, file, out), y = resolve_bone(sk, b, file, out);
        if (x >= 0 && y >= 0) {
            if (owner_group[static_cast<std::size_t>(x)] != group_index ||
                owner_group[static_cast<std::size_t>(y)] != group_index)
                error(a.line, "connect " + a.text + " " + b.text + ": both must be controls of group " + g.name);
            else
                mesh_edges.insert(pair_key(x, y));
        }
    }
    for (const std::uint64_t e : mesh_edges) {
        const int x = static_cast<int>(e >> 32), y = static_cast<int>(e & 0xffffffffU);
        const int px = particle_of[static_cast<std::size_t>(x)], py = particle_of[static_cast<std::size_t>(y)];
        if (px < 0 || py < 0)
            continue;
        add_distance(px, py, level[x] == level[y] ? Link::row : Link::shear);
    }
    for (const auto &f : faces) {
        const int a = particle_of[static_cast<std::size_t>(f[0])], b = particle_of[static_cast<std::size_t>(f[1])],
                  c = particle_of[static_cast<std::size_t>(f[2])];
        if (a < 0 || b < 0 || c < 0)
            continue;
        const Vec3 pa = world(f[0], rig.particles[static_cast<std::size_t>(a)].offset),
                   pb = world(f[1], rig.particles[static_cast<std::size_t>(b)].offset),
                   pc = world(f[2], rig.particles[static_cast<std::size_t>(c)].offset);
        if (length(cross(pb - pa, pc - pa)) < kTiny * kTiny) {
            warn(g.line, "group " + g.name + ": a face of " + sk.names[static_cast<std::size_t>(f[0])] + ", " +
                             sk.names[static_cast<std::size_t>(f[1])] + " and " +
                             sk.names[static_cast<std::size_t>(f[2])] + " has no area and is left out");
            continue;
        }
        rig.faces.push_back({a, b, c});
    }

    // neighbour lists, bends (straight through a control) and row neighbours for the twist
    const int first = gr.first, last = gr.last;
    std::vector<std::vector<int>> near(static_cast<std::size_t>(last - first));
    for (const auto &d : rig.distances)
        if (d.a >= first && d.a < last && d.b >= first && d.b < last) {
            near[static_cast<std::size_t>(d.a - first)].push_back(d.b);
            near[static_cast<std::size_t>(d.b - first)].push_back(d.a);
        }
    const auto rest_point = [&](int i) {
        const Particle &p = rig.particles[static_cast<std::size_t>(i)];
        return world(p.bone, p.offset);
    };
    std::vector<std::array<int, 2>> bends;
    for (int b = first; b < last; ++b) {
        const auto &n = near[static_cast<std::size_t>(b - first)];
        for (std::size_t x = 0; x < n.size(); ++x)
            for (std::size_t y = x + 1; y < n.size(); ++y) {
                const Vec3 u = normalized(rest_point(n[x]) - rest_point(b)),
                           v = normalized(rest_point(n[y]) - rest_point(b));
                if (dot(u, v) < -0.85F) // within ~32 degrees of straight
                    bends.push_back({n[x], n[y]});
            }
    }
    for (const auto &bd : bends)
        if (rig.particles[static_cast<std::size_t>(bd[0])].dynamic ||
            rig.particles[static_cast<std::size_t>(bd[1])].dynamic)
            add_distance(bd[0], bd[1], Link::bend);
    for (int i = first; i < last; ++i) {
        Particle &p = rig.particles[static_cast<std::size_t>(i)];
        if (!p.dynamic || g.kind != GroupKind::surface)
            continue;
        std::vector<int> row;
        for (const int n : near[static_cast<std::size_t>(i - first)])
            if (rig.particles[static_cast<std::size_t>(n)].dynamic &&
                level[rig.particles[static_cast<std::size_t>(n)].bone] == level[p.bone])
                row.push_back(n);
        if (row.size() >= 2) {
            float best = 2.0F;
            for (std::size_t x = 0; x < row.size(); ++x)
                for (std::size_t y = x + 1; y < row.size(); ++y) {
                    const float c = dot(normalized(rest_point(row[x]) - rest_point(i)),
                                        normalized(rest_point(row[y]) - rest_point(i)));
                    if (c < best) {
                        best = c;
                        p.left = row[x];
                        p.right = row[y];
                    }
                }
        } else if (row.size() == 1) {
            p.left = i;
            p.right = row[0];
        }
    }
    // tethers: the path down the strip from its anchor
    for (int i = first; i < last; ++i) {
        Particle &p = rig.particles[static_cast<std::size_t>(i)];
        if (!p.dynamic)
            continue;
        float path = 0.0F;
        int at = i, steps = 0;
        while (at >= 0 && rig.particles[static_cast<std::size_t>(at)].dynamic && steps++ < 1000) {
            const int up = rig.particles[static_cast<std::size_t>(at)].anchor;
            if (up < 0)
                break;
            path += length(rest_point(at) - rest_point(up));
            at = up;
        }
        p.root = at;
        p.tether = path;
    }
    // reference shape: rest, in the reference bone's space
    for (int i = first; i < last; ++i) {
        Particle &p = rig.particles[static_cast<std::size_t>(i)];
        p.reference = local(gr.reference_bone, rest_point(i));
    }
    // follow bones: a control near one (a thigh under the coat) partly rides it, more toward the hem
    if (!g.follow_bones.empty()) {
        struct Seg {
            int bone;
            Vec3 a, b;
            float radius, weight, always;
        };
        std::vector<Seg> segs;
        for (const auto &f : g.follow_bones) {
            const int b = resolve_bone(sk, f.bone, file, out);
            if (b < 0)
                continue;
            if (b == gr.reference_bone || owner_group[static_cast<std::size_t>(b)] >= 0) {
                error(f.bone.line,
                      "follow_bone " + f.bone.text + " must be a body bone other than the group's first attach");
                continue;
            }
            Vec3 end = head(b);
            for (const int c : children[static_cast<std::size_t>(b)]) // the limb's continuation: its furthest child
                if (length(head(c) - head(b)) > length(end - head(b)))
                    end = head(c);
            segs.push_back({b, head(b), end, f.radius, f.weight, f.always});
        }
        for (int i = first; i < last; ++i) {
            Particle &p = rig.particles[static_cast<std::size_t>(i)];
            if (!p.dynamic)
                continue;
            const Vec3 x = rest_point(i);
            ReferenceBlend blend;
            blend.bone[0] = gr.reference_bone;
            blend.point[0] = p.reference;
            for (const Seg &s : segs) {
                if (blend.count >= 4)
                    break;
                const Vec3 ab = s.b - s.a;
                const float ll = dot(ab, ab);
                const float t = ll > 0.0F ? std::fmin(1.0F, std::fmax(0.0F, dot(x - s.a, ab) / ll)) : 0.0F;
                const float rest_gap = length(x - (s.a + ab * t));
                const float near_by = std::fmax(0.0F, 1.0F - rest_gap / s.radius);
                if (near_by <= 0.0F)
                    continue;
                // the most this bone may carry it, once it has come up into it (Simulation::blend_weights)
                const auto k = static_cast<std::size_t>(blend.count++);
                blend.bone[k] = s.bone;
                blend.weight[k] = s.weight;
                blend.point[k] = local(s.bone, x);
                blend.end[k] = local(s.bone, s.b);
                blend.gap[k] = rest_gap;
                blend.always[k] = s.always * near_by;
            }
            if (blend.count == 1)
                continue;
            blend.weight[0] = 1.0F;
            for (int k = 0; k < blend.count; ++k) // the control bone's frame in each, for rebuilding it
                blend.frame[static_cast<std::size_t>(k)] =
                    relative(rest[static_cast<std::size_t>(blend.bone[static_cast<std::size_t>(k)])],
                             rest[static_cast<std::size_t>(p.bone)]);
            p.blend = static_cast<int>(rig.blends.size());
            rig.blends.push_back(blend);
        }
    }
    // a hanging start: each chain hung straight along `hang` from its anchor, its links kept
    if (g.hang_given && g.kind == GroupKind::chain) {
        const Vec3 down = normalized(g.hang);
        for (const auto &s : strips) {
            Vec3 at;
            bool started = false;
            for (const int b : s) {
                const int pi = particle_of[static_cast<std::size_t>(b)];
                if (pi < 0)
                    continue;
                Particle &p = rig.particles[static_cast<std::size_t>(pi)];
                const Vec3 from = started ? at : rest_point(p.anchor);
                const float len = length(rest_point(pi) - rest_point(p.anchor));
                at = from + down * len;
                started = true;
                p.reference = local(gr.reference_bone, at);
            }
        }
    }
    rig.groups.push_back(gr);
    // rig freedom: a surface whose controls are mostly unconnected cannot fold as one piece
    if (g.kind == GroupKind::surface) {
        int lonely = 0;
        for (int i = first; i < last; ++i)
            if (rig.particles[static_cast<std::size_t>(i)].dynamic &&
                near[static_cast<std::size_t>(i - first)].size() < 3)
                ++lonely;
        if (lonely * 2 > static_cast<int>(controls.size()))
            warn(g.line, "group " + g.name + ": " + std::to_string(lonely) + " of its " +
                             std::to_string(controls.size()) +
                             " controls have fewer than three neighbours: the rig lacks the connections for a "
                             "coordinated surface (add `topology mesh` or `connect` lines)");
    }
}

void Cook::finish()
{
    const std::size_t nb = sk.names.size();
    // the simulated bones, parents first (by depth from the root)
    std::vector<int> depth(nb, 0);
    for (std::size_t b = 0; b < nb; ++b)
        for (int up = sk.parent[b]; up >= 0; up = sk.parent[static_cast<std::size_t>(up)])
            ++depth[b];
    std::vector<int> sim;
    for (std::size_t b = 0; b < nb; ++b)
        if (particle_of[b] >= 0)
            sim.push_back(static_cast<int>(b));
    std::stable_sort(sim.begin(), sim.end(), [&](int x, int y) {
        return depth[static_cast<std::size_t>(x)] < depth[static_cast<std::size_t>(y)];
    });
    std::vector<int> sim_index(nb, -1);
    for (const int b : sim) {
        sim_index[static_cast<std::size_t>(b)] = static_cast<int>(rig.bones_out.size());
        SimBone s;
        s.bone = b;
        s.particle = particle_of[static_cast<std::size_t>(b)];
        const Particle &p = rig.particles[static_cast<std::size_t>(s.particle)];
        s.group = p.group;
        for (int up = sk.parent[static_cast<std::size_t>(b)]; up >= 0 && s.parent_sim < 0;
             up = sk.parent[static_cast<std::size_t>(up)])
            s.parent_sim = sim_index[static_cast<std::size_t>(up)];
        const GroupRig &gr = rig.groups[static_cast<std::size_t>(s.group)];
        s.reference = relative(rest[static_cast<std::size_t>(gr.reference_bone)], rest[static_cast<std::size_t>(b)]);
        rig.bones_out.push_back(s);
    }
    // a collider on a simulated bone follows it as simulated
    for (Collider &k : rig.colliders) {
        if (k.bone >= 0)
            k.sim = sim_index[static_cast<std::size_t>(k.bone)];
        if (k.kind == ShapeKind::capsule && k.end_bone >= 0)
            k.end_sim = sim_index[static_cast<std::size_t>(k.end_bone)];
    }
    // anchors under a simulated bone ride it
    for (auto &p : rig.particles) {
        if (p.dynamic || p.carrier != -3)
            continue;
        p.carrier = -1;
        for (int up = p.bone; up >= 0 && p.carrier < 0; up = sk.parent[static_cast<std::size_t>(up)])
            p.carrier = sim_index[static_cast<std::size_t>(up)];
    }
    // bones carried by simulated ones
    std::vector<int> order(nb);
    for (std::size_t b = 0; b < nb; ++b)
        order[b] = static_cast<int>(b);
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
        return depth[static_cast<std::size_t>(x)] < depth[static_cast<std::size_t>(y)];
    });
    for (const int b : order) {
        if (sim_index[static_cast<std::size_t>(b)] >= 0)
            continue;
        int by = -1;
        for (int up = sk.parent[static_cast<std::size_t>(b)]; up >= 0 && by < 0;
             up = sk.parent[static_cast<std::size_t>(up)])
            by = sim_index[static_cast<std::size_t>(up)];
        if (by >= 0)
            rig.carried.push_back({b, by});
    }
    // the bones the solver reads
    std::set<int> need;
    for (const auto &p : rig.particles)
        need.insert(p.bone);
    for (const auto &g : rig.groups)
        need.insert(g.reference_bone);
    for (const auto &b : rig.blends)
        for (int k = 0; k < b.count; ++k)
            need.insert(b.bone[static_cast<std::size_t>(k)]);
    for (const auto &c : rig.colliders) {
        need.insert(c.bone);
        if (c.end_bone >= 0)
            need.insert(c.end_bone);
    }
    for (const auto &s : rig.bones_out)
        need.insert(s.bone);
    for (const auto &g : rig.groups)
        if (g.wind_bone >= 0)
            need.insert(g.wind_bone);
    if (rig.wind_bone >= 0)
        need.insert(rig.wind_bone);
    for (const auto &c : rig.carried)
        need.insert(c.bone);
    rig.needed.assign(need.begin(), need.end());
    for (const auto &d : rig.distances)
        if (d.kind != Link::bend)
            rig.edges.push_back({d.a, d.b});

    // self-collision: particles never pushed off their own faces' neighbours
    std::vector<std::vector<int>> near(rig.particles.size());
    for (const auto &d : rig.distances) {
        if (d.kind == Link::bend)
            continue;
        near[static_cast<std::size_t>(d.a)].push_back(d.b);
        near[static_cast<std::size_t>(d.b)].push_back(d.a);
    }
    std::set<std::uint64_t> excluded;
    for (std::size_t a = 0; a < rig.particles.size(); ++a) {
        if (rig.particles[a].self_group < 0)
            continue;
        excluded.insert(pair_key(static_cast<int>(a), static_cast<int>(a)));
        for (const int b : near[a]) {
            excluded.insert(pair_key(static_cast<int>(a), b));
            for (const int c : near[static_cast<std::size_t>(b)])
                excluded.insert(pair_key(static_cast<int>(a), c));
        }
    }
    rig.self_excluded.assign(excluded.begin(), excluded.end());
    const auto rest_point = [&](std::size_t i) { return world(rig.particles[i].bone, rig.particles[i].offset); };
    // `clear` colliders shrunk so that every control they collide with starts outside them
    for (std::size_t c = 0; c < rig.colliders.size(); ++c) {
        Collider &k = rig.colliders[c];
        const auto cfg = std::find_if(config.colliders.begin(), config.colliders.end(),
                                      [&](const ColliderConfig &x) { return x.name == k.name; });
        if (cfg == config.colliders.end() || !cfg->clear)
            continue;
        const Vec3 a = world(k.bone, k.offset);
        const Vec3 b = k.kind == ShapeKind::capsule ? world(k.end_bone, k.end_offset) : a;
        float scale = 1.0F;
        for (std::size_t i = 0; i < rig.particles.size(); ++i) {
            const Particle &p = rig.particles[i];
            if (!p.dynamic || !(p.collide & k.tags))
                continue;
            const Vec3 x = rest_point(i);
            const float t = along_segment(x, a, b);
            const float r = k.radius + (k.end_radius - k.radius) * t;
            if (r > 0.0F)
                scale = std::fmin(scale, (length(x - lerp(a, b, t)) - p.thickness - 0.002F) / r);
        }
        // and every face between them: the coat's surface, not only its controls, clear of it at rest
        for (const Face &f : rig.faces) {
            const Particle &p = rig.particles[static_cast<std::size_t>(f.a)];
            if (!(p.collide & k.tags))
                continue;
            Vec3 on_core, on_face;
            bool crosses = false;
            const float d = closest_segment_triangle(
                a, b, rest_point(static_cast<std::size_t>(f.a)), rest_point(static_cast<std::size_t>(f.b)),
                rest_point(static_cast<std::size_t>(f.c)), on_core, on_face, crosses);
            const float r = k.radius + (k.end_radius - k.radius) * along_segment(on_core, a, b);
            if (r > 0.0F)
                scale = std::fmin(scale, crosses ? 0.0F : (d - p.thickness - 0.002F) / r);
        }
        if (scale < 0.5F)
            warn(cfg->line, "collider " + k.name + ": `clear` would shrink it to " +
                                std::to_string(int(scale * 100.0F)) +
                                "% of its fitted size; it is kept at half (a control sits too close to it at rest)");
        scale = std::fmax(0.5F, scale);
        k.radius *= scale;
        k.end_radius *= scale;
    }
    // each pair's authored clearance: how near the collider's core the control or face hangs at rest
    rig.point_clearance.assign(rig.colliders.size() * rig.particles.size(), 0.0F);
    rig.face_clearance.assign(rig.colliders.size() * rig.faces.size(), 0.0F);
    for (std::size_t c = 0; c < rig.colliders.size(); ++c) {
        const Collider &k = rig.colliders[c];
        const Vec3 a = world(k.bone, k.offset);
        const Vec3 b = k.kind == ShapeKind::capsule ? world(k.end_bone, k.end_offset) : a;
        const Vec3 &sc = rest[static_cast<std::size_t>(k.bone)].s;
        const float unit = 1.0F / std::fmax(1e-6F, (std::fabs(sc.x) + std::fabs(sc.y) + std::fabs(sc.z)) / 3.0F);
        for (std::size_t i = 0; i < rig.particles.size(); ++i) {
            const Particle &p = rig.particles[i];
            if (!p.dynamic || !(p.collide & k.tags))
                continue;
            float u, t;
            const Vec3 x = world(p.bone, p.offset);
            closest_segments(x, x, a, b, u, t);
            rig.point_clearance[c * rig.particles.size() + i] = std::fmax(1e-4F, length(x - lerp(a, b, t)) * unit);
        }
        for (std::size_t f = 0; f < rig.faces.size(); ++f) {
            const Face &face = rig.faces[f];
            if (!(rig.particles[static_cast<std::size_t>(face.a)].collide & k.tags))
                continue;
            const auto at = [&](int i) {
                return world(rig.particles[static_cast<std::size_t>(i)].bone,
                             rig.particles[static_cast<std::size_t>(i)].offset);
            };
            Vec3 on_core, on_face;
            bool crosses = false;
            const float d =
                closest_segment_triangle(a, b, at(face.a), at(face.b), at(face.c), on_core, on_face, crosses);
            rig.face_clearance[c * rig.faces.size() + f] = std::fmax(1e-4F, (crosses ? 0.0F : d) * unit);
        }
    }
    // controls inside a collider at rest are pushed out of it when the references are prepared
    // controls (animation)
    for (const auto &k : config.controls) {
        ControlRig r;
        r.config = &k;
        for (const Param p : {Param::mass, Param::thickness}) // read here, once: blending them would do nothing
            if (k.params.given[static_cast<std::size_t>(p)])
                error(k.line, "control " + k.name + ": `" + param_info(p).name +
                                  "` is fixed when the model loads; set it on the group or on a `bone` line");
        if (k.wind.given && k.wind.space == WindSpace::bone)
            r.wind_bone = resolve_bone(sk, k.wind.bone, file, out);
        for (const auto &name : k.groups)
            for (std::size_t gi = 0; gi < rig.groups.size(); ++gi)
                if (rig.groups[gi].name == name) {
                    r.groups.push_back(static_cast<int>(gi));
                    for (int i = rig.groups[gi].first; i < rig.groups[gi].last; ++i)
                        if (rig.particles[static_cast<std::size_t>(i)].dynamic)
                            r.particles.push_back(i);
                }
        for (const auto &ref : k.bones) {
            const int b = resolve_bone(sk, ref, file, out);
            if (b < 0)
                continue;
            if (particle_of[static_cast<std::size_t>(b)] < 0) {
                error(ref.line, "control " + k.name + ": bone " + ref.text + " is not simulated");
                continue;
            }
            r.particles.push_back(particle_of[static_cast<std::size_t>(b)]);
        }
        std::sort(r.particles.begin(), r.particles.end());
        r.particles.erase(std::unique(r.particles.begin(), r.particles.end()), r.particles.end());
        rig.controls.push_back(std::move(r));
    }
    int dyn = 0;
    for (const auto &p : rig.particles)
        dyn += p.dynamic ? 1 : 0;
    std::ostringstream s;
    s << rig.groups.size() << " groups, " << dyn << " controls, "
      << rig.particles.size() - static_cast<std::size_t>(dyn) << " anchors, " << rig.distances.size() << " links, "
      << rig.faces.size() << " faces, " << rig.colliders.size() << " colliders, " << rig.bones_out.size()
      << " bones rebuilt, " << rig.carried.size() << " carried";
    rig.summary = s.str();
}

} // namespace

bool group_follows_game(const Rig &rig, std::size_t g)
{
    const GroupRig &group = rig.groups[g];
    for (int i = group.first; i < group.last; ++i) {
        const Particle &p = rig.particles[static_cast<std::size_t>(i)];
        const ParamBlock &b = rig.params[static_cast<std::size_t>(p.params)];
        if (p.dynamic && (b.get(Param::game_shape) > 0.0F || b.get(Param::cutscene_sim) >= 0.5F))
            return true;
    }
    for (const ControlRig &c : rig.controls) {
        const auto &given = c.config->params.given;
        if (!given[static_cast<std::size_t>(Param::game_shape)] &&
            !given[static_cast<std::size_t>(Param::cutscene_sim)])
            continue;
        for (const int i : c.particles)
            if (i >= group.first && i < group.last)
                return true;
    }
    return false;
}

int resolve_bone(const SkeletonDesc &skeleton, const BoneRef &ref, const std::string &file, Diagnostics &out)
{
    const auto find = [&](const std::string &name) {
        for (std::size_t i = 0; i < skeleton.names.size(); ++i)
            if (skeleton.names[i] == name)
                return static_cast<int>(i);
        return -1;
    };
    int b = find(ref.text);
    if (b >= 0)
        return b;
    const std::string &t = ref.text;
    if (!t.empty() && std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
        b = find("kh2_" + t);
        if (b >= 0)
            return b;
    }
    if (t.rfind("path:", 0) == 0) { // each name a child of the one before
        int at = -1;
        std::istringstream parts(t.substr(5));
        std::string part;
        bool ok = true;
        while (ok && std::getline(parts, part, '/')) {
            int next = -1;
            for (std::size_t i = 0; i < skeleton.names.size() && next < 0; ++i)
                if (skeleton.names[i] == part && skeleton.parent[i] == at)
                    next = static_cast<int>(i);
            if (next < 0 && at >= 0) // allow skipping unnamed in-betweens: any descendant
                for (std::size_t i = 0; i < skeleton.names.size() && next < 0; ++i)
                    if (skeleton.names[i] == part)
                        for (int up = skeleton.parent[i]; up >= 0; up = skeleton.parent[static_cast<std::size_t>(up)])
                            if (up == at) {
                                next = static_cast<int>(i);
                                break;
                            }
            ok = next >= 0;
            at = next;
        }
        if (ok && at >= 0)
            return at;
    }
    out.push_back({true, file, ref.line,
                   "there is no bone " + t +
                       " in the model (bones are named as the model names them; a KH2 model's "
                       "bone N is kh2_N or just N; or write path:<a>/<b>/...)"});
    return -1;
}

void describe(const assets::ImportedModel &model, SkeletonDesc &skeleton, MeshDesc &mesh)
{
    skeleton = {};
    mesh = {};
    std::map<std::uint32_t, int> joint_of_palette;
    for (std::size_t j = 0; j < model.joints.size(); ++j) {
        const auto &in = model.joints[j];
        skeleton.names.push_back(in.name);
        skeleton.parent.push_back(in.parent);
        Mat4 w;
        std::copy(in.world.begin(), in.world.end(), w.begin());
        skeleton.rest.push_back(w);
        joint_of_palette[in.palette_index] = static_cast<int>(j);
    }
    for (const auto &m : model.model.meshes) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto &v : m.vertices) {
            MeshDesc::Vertex o;
            o.position = {v.position[0], v.position[1], v.position[2]};
            for (std::size_t k = 0; k < 4; ++k) {
                const auto it = joint_of_palette.find(v.joints[k]);
                o.bones[k] = v.weights[k] > 0.0F && it != joint_of_palette.end() ? it->second : -1;
                o.weights[k] = o.bones[k] >= 0 ? v.weights[k] : 0.0F;
            }
            mesh.vertices.push_back(o);
        }
        for (const auto i : m.indices)
            mesh.triangles.push_back(base + i);
    }
}

Rig cook(const Config &config, const SkeletonDesc &skeleton, const MeshDesc &mesh, Diagnostics &out)
{
    Rig rig;
    rig.config = config;
    rig.skeleton = skeleton;
    Cook c{rig.config, rig.skeleton, mesh, out, rig, {}, {}, {}, {}, {}, {}};
    c.file = config.files.empty() ? std::string("secondary motion") : file_name(config.files.front());
    const std::size_t nb = skeleton.names.size();
    if (skeleton.parent.size() != nb || skeleton.rest.size() != nb) {
        out.push_back({true, c.file, 0, "the model's skeleton is malformed: export or extract the model again"});
        return rig;
    }
    for (std::size_t b = 0; b < nb; ++b) {
        c.rest.push_back(frame_of(skeleton.rest[b]));
        if (skeleton.parent[b] >= static_cast<int>(nb)) {
            out.push_back({true, c.file, 0,
                           "the model's bone " + skeleton.names[b] +
                               " has a parent out of range: export or extract the model again"});
            return rig;
        }
    }
    c.children.assign(nb, {});
    for (std::size_t b = 0; b < nb; ++b)
        if (skeleton.parent[b] >= 0)
            c.children[static_cast<std::size_t>(skeleton.parent[b])].push_back(static_cast<int>(b));
    c.owner_group.assign(nb, -1);
    c.particle_of.assign(nb, -1);
    c.dominant.reserve(mesh.vertices.size());
    for (const auto &v : mesh.vertices) {
        int best = -1;
        float w = 0.0F;
        for (std::size_t k = 0; k < 4; ++k)
            if (v.bones[k] >= 0 && v.weights[k] > w) {
                w = v.weights[k];
                best = v.bones[k];
            }
        c.dominant.push_back(best);
    }
    c.colliders();
    if (config.wind.given && config.wind.space == WindSpace::bone)
        rig.wind_bone = resolve_bone(skeleton, config.wind.bone, c.file, out);
    for (std::size_t g = 0; g < config.groups.size(); ++g)
        c.group(g);
    if (!has_errors(out))
        c.finish();
    return rig;
}

// `clip_world`'s volumes: each strip from its fixed point down to its end (a forked strip's shared top in each
// branch) a column, the columns round the body by their ends' angle about the coat's axis at rest, each beside
// the next stitched by its share of the way down (closed all round, across a slit too), capped by fans from
// the middle of the tops and of the ends; wound outward at rest
std::vector<ClipVolume> clip_volumes_of(const Rig &rig, const std::vector<Vec3> &rest)
{
    std::vector<ClipVolume> out;
    const auto particle = [&](int i) -> const Particle & { return rig.particles[static_cast<std::size_t>(i)]; };
    const auto rest_of = [&](int i) { return rest[static_cast<std::size_t>(i)]; };
    if (rest.size() != rig.particles.size())
        return out;
    for (const GroupRig &g : rig.groups) {
        if (!g.clip_world)
            continue;
        std::vector<std::uint8_t> parent(rig.particles.size(), 0);
        for (int i = g.first; i < g.last; ++i)
            if (particle(i).dynamic && particle(i).anchor >= 0)
                parent[static_cast<std::size_t>(particle(i).anchor)] = 1;
        std::vector<std::vector<int>> columns;
        for (int i = g.first; i < g.last; ++i) {
            if (!particle(i).dynamic || parent[static_cast<std::size_t>(i)])
                continue;
            std::vector<int> col;
            for (int k = i; k >= 0 && col.size() < 64; k = particle(k).anchor) {
                col.push_back(k);
                if (!particle(k).dynamic)
                    break;
            }
            std::reverse(col.begin(), col.end());
            if (col.size() >= 2)
                columns.push_back(std::move(col));
        }
        ClipVolume cv;
        cv.group = g.name;
        cv.columns = columns.size();
        if (columns.size() < 3) {
            out.push_back(std::move(cv)); // no faces: fewer than three strips
            continue;
        }
        Vec3 top{}, bottom{};
        for (const auto &col : columns) {
            top += rest_of(col.front()) * (1.0F / static_cast<float>(columns.size()));
            bottom += rest_of(col.back()) * (1.0F / static_cast<float>(columns.size()));
        }
        const Vec3 up = normalized(top - bottom), u = normalized(perpendicular(up)), v = cross(up, u);
        const auto angle = [&](const std::vector<int> &col) {
            const Vec3 d = rest_of(col.back()) - bottom;
            return std::atan2(dot(d, v), dot(d, u));
        };
        std::sort(columns.begin(), columns.end(),
                  [&](const std::vector<int> &a, const std::vector<int> &b) { return angle(a) < angle(b); });
        cv.inset = g.clip_inset;
        std::map<int, int> vertex;
        const auto at = [&](int p) {
            const auto [it, added] = vertex.emplace(p, static_cast<int>(cv.particles.size()));
            if (added)
                cv.particles.push_back(p);
            return it->second;
        };
        const auto face = [&](int a, int b, int c) {
            if (a != b && b != c && a != c)
                cv.faces.push_back({a, b, c});
        };
        // a quad split along the drawn mesh's diagonal (its faces'), so the volume's surface is the coat's: split
        // the other way, the floor showed through as a line along the quads
        std::set<std::uint64_t> drawn;
        for (const Face &f : rig.faces)
            for (const auto &[a, b] : {std::pair{f.a, f.b}, std::pair{f.b, f.c}, std::pair{f.c, f.a}})
                drawn.insert(pair_key(a, b));
        for (std::size_t c = 0; c < columns.size(); ++c) {
            const auto &l = columns[c], &r = columns[(c + 1) % columns.size()];
            const float nl = static_cast<float>(l.size() - 1), nr = static_cast<float>(r.size() - 1);
            std::size_t i = 0, j = 0;
            while (i + 1 < l.size() || j + 1 < r.size()) {
                bool left = j + 1 >= r.size() ||
                            (i + 1 < l.size() && static_cast<float>(i + 1) / nl <= static_cast<float>(j + 1) / nr);
                const bool quad =
                    i + 1 < l.size() && j + 1 < r.size() && (i + 1) * (r.size() - 1) == (j + 1) * (l.size() - 1);
                if (quad && drawn.count(pair_key(l[i], r[j + 1])) != 0 && drawn.count(pair_key(l[i + 1], r[j])) == 0)
                    left = false;
                if (left) {
                    face(at(l[i]), at(l[i + 1]), at(r[j]));
                    ++i;
                } else {
                    face(at(l[i]), at(r[j + 1]), at(r[j]));
                    ++j;
                }
            }
        }
        const auto rest_at = [&](int k) { return rest_of(cv.particles[static_cast<std::size_t>(k)]); };
        float outward = 0.0F;
        for (const auto &f : cv.faces) {
            const Vec3 a = rest_at(f[0]), b = rest_at(f[1]), cc = rest_at(f[2]);
            const Vec3 mid = (a + b + cc) * (1.0F / 3.0F) - bottom;
            outward += dot(cross(b - a, cc - a), mid - up * dot(mid, up));
        }
        if (outward < 0.0F)
            for (auto &f : cv.faces)
                std::swap(f[1], f[2]);
        for (const auto &col : columns)
            for (auto [ring, p] : {std::pair{&cv.top, col.front()}, std::pair{&cv.bottom, col.back()}}) {
                const int k = at(p);
                if (ring->empty() || (ring->back() != k && ring->front() != k))
                    ring->push_back(k);
            }
        // the caps: fans from the rings' middles (after the particles), turned up at the top and down at the bottom
        const int middle_top = static_cast<int>(cv.particles.size());
        for (const auto &[ring, middle, sign] :
             {std::tuple{&cv.top, middle_top, 1.0F}, std::tuple{&cv.bottom, middle_top + 1, -1.0F}}) {
            Vec3 centre{};
            for (const int k : *ring)
                centre += rest_at(k) * (1.0F / static_cast<float>(ring->size()));
            const std::size_t before = cv.faces.size();
            float turn = 0.0F;
            for (std::size_t k = 0; k < ring->size(); ++k) {
                const int a = (*ring)[k], b = (*ring)[(k + 1) % ring->size()];
                cv.faces.push_back({middle, a, b});
                turn += dot(cross(rest_at(a) - centre, rest_at(b) - centre), up);
            }
            if (turn * sign < 0.0F)
                for (std::size_t q = before; q < cv.faces.size(); ++q)
                    std::swap(cv.faces[q][1], cv.faces[q][2]);
        }
        // closed: each edge on two faces, once each way (an open one lets the room through a pixel's count)
        std::map<std::pair<int, int>, int> edges;
        for (const auto &f : cv.faces)
            for (int k = 0; k < 3; ++k)
                ++edges[{f[static_cast<std::size_t>(k)], f[static_cast<std::size_t>((k + 1) % 3)]}];
        for (const auto &[e, n] : edges)
            cv.open_edges += n != 1 || edges.count({e.second, e.first}) == 0 ? 1U : 0U;
        out.push_back(std::move(cv));
    }
    return out;
}

} // namespace khdays::secondary
