#include "core/rooms/room_collision.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace studio {

namespace {

constexpr int kDx[4] = {-1, 1, -1, 1};
constexpr int kDz[4] = {-1, -1, 1, 1};
constexpr std::uint16_t kLast = 0x8000, kSkip = 0x4000;
constexpr std::size_t kSplitAbove = 8;

// The surface tags a collision material's own name gives its faces: a name as a room's export writes one --
// `ground`, `wall` or `ceiling`, then names (`wall nohit`; KH2's maps' floors and walls are named so) -- gives
// those of its names that are surface tags. Gates and walls that open stay the rules': the modder places them.
std::vector<std::string> tags_in_name(const std::string& material) {
    std::vector<std::string> words;
    for (std::size_t at = 0; at < material.size();) {
        const std::size_t end = std::min(material.find(' ', at), material.size());
        if (end > at)
            words.push_back(material.substr(at, end - at));
        at = end + 1;
    }
    std::vector<std::string> tags;
    if (words.empty() || (words[0] != "ground" && words[0] != "wall" && words[0] != "ceiling"))
        return tags;
    for (std::size_t k = 1; k < words.size(); ++k) {
        if (std::find(kSurfaceTags.begin(), kSurfaceTags.end(), words[k]) != kSurfaceTags.end())
            tags.push_back(words[k]);
    }
    return tags;
}

struct Square {
    std::int64_t x = 0, z = 0, size = 0;
    Square child(int c) const {
        return {x + kDx[c] * size / 4, z + kDz[c] * size / 4, size / 2};
    }
    bool holds(const std::array<std::int32_t, 4>& b) const {
        return x - size / 2 <= b[0] && b[2] <= x + size / 2 && z - size / 2 <= b[1] && b[3] <= z + size / 2;
    }
    bool meets(const std::array<std::int64_t, 4>& q) const {
        return x - size / 2 <= q[2] && q[0] <= x + size / 2 && z - size / 2 <= q[3] && q[1] <= z + size / 2;
    }
};

// Each node's square, by walking down from the root; false when a node is
// reached twice or a child index is outside the tree.
bool squares(const RoomCollision& c, std::vector<Square>* out) {
    out->assign(c.nodes.size(), Square{});
    if (c.nodes.empty())
        return true;
    std::vector<bool> seen(c.nodes.size(), false);
    std::vector<std::size_t> todo{0};
    (*out)[0] = {c.root(0), c.root(1), c.root(2)};
    seen[0] = true;
    while (!todo.empty()) {
        const std::size_t k = todo.back();
        todo.pop_back();
        for (int ch = 0; ch < 4; ++ch) {
            const std::int32_t n = c.nodes[k].children[static_cast<std::size_t>(ch)];
            if (n < 0)
                continue;
            if (static_cast<std::size_t>(n) >= c.nodes.size() || seen[static_cast<std::size_t>(n)])
                return false;
            seen[static_cast<std::size_t>(n)] = true;
            (*out)[static_cast<std::size_t>(n)] = (*out)[k].child(ch);
            todo.push_back(static_cast<std::size_t>(n));
        }
    }
    return std::all_of(seen.begin(), seen.end(), [](bool b) { return b; });
}

// The faces of node `k`'s run in list `l` (indices).
std::vector<std::size_t> run(const RoomCollision& c, std::size_t k, int l) {
    std::vector<std::size_t> out;
    const auto& faces = c.faces[static_cast<std::size_t>(l)];
    const std::int16_t first = c.nodes[k].first[static_cast<std::size_t>(l)];
    if (first < 0)
        return out;
    for (std::size_t j = static_cast<std::size_t>(first); j < faces.size(); ++j) {
        out.push_back(j);
        if (faces[j].flags & kLast)
            break;
    }
    return out;
}

using FaceKey = std::tuple<std::array<std::int32_t, 4>, std::array<std::array<std::int32_t, 3>, 4>, std::uint16_t>;
FaceKey key_of(const RoomFace& f) {
    return {f.bounds, f.v, f.corners};
}

bool overlaps(const RoomFace& f, const std::array<std::int64_t, 4>& q) {
    return f.bounds[0] <= q[2] && q[0] <= f.bounds[2] && f.bounds[1] <= q[3] && q[1] <= f.bounds[3];
}

// The faces of list `l` the walk visits for box `q` whose bounds meet it.
std::multiset<FaceKey> walk(const RoomCollision& c, const std::vector<Square>& sq, int l,
                            const std::array<std::int64_t, 4>& q) {
    std::multiset<FaceKey> found;
    if (c.nodes.empty())
        return found;
    std::vector<std::size_t> todo{0};
    while (!todo.empty()) {
        const std::size_t k = todo.back();
        todo.pop_back();
        if (!sq[k].meets(q))
            continue;
        for (const auto j : run(c, k, l)) {
            const RoomFace& f = c.faces[static_cast<std::size_t>(l)][j];
            if (!(f.flags & kSkip) && overlaps(f, q))
                found.insert(key_of(f));
        }
        for (int ch = 0; ch < 4; ++ch) {
            const std::int32_t n = c.nodes[k].children[static_cast<std::size_t>(ch)];
            if (n >= 0 && (c.nodes[k].flags >> (4 * l + ch) & 1))
                todo.push_back(static_cast<std::size_t>(n));
        }
    }
    return found;
}

}  // namespace

std::string check_tree(const RoomCollision& c) {
    const std::size_t total = c.faces[0].size() + c.faces[1].size() + c.faces[2].size();
    if (c.nodes.empty())
        return total == 0 ? std::string() : "faces with no tree";
    if (c.nodes[0].parent != -1)
        return "the root has a parent";
    std::vector<Square> sq;
    if (!squares(c, &sq))
        return "children reached twice, outside the tree, or nodes not reached";
    const std::int64_t smallest = c.root(3);
    for (std::size_t k = 0; k < c.nodes.size(); ++k) {
        int level = 0;
        for (std::int64_t size = sq[k].size; size < c.root(2) && level < 64; size *= 2)
            ++level;
        if (level >= kTreeLevels)
            return "node " + std::to_string(k) + ": deeper than the walk's " + std::to_string(kTreeLevels) + " levels";
    }
    std::vector<std::vector<int>> owners[3];
    for (int l = 0; l < 3; ++l)
        owners[l].assign(c.faces[static_cast<std::size_t>(l)].size(), {});
    for (std::size_t k = 0; k < c.nodes.size(); ++k) {
        const RoomNode& n = c.nodes[k];
        if ((n.flags & 0xf000) != 0)
            return "node " + std::to_string(k) + ": flags 12-15 set on disk";
        for (int ch = 0; ch < 4; ++ch) {
            const std::int32_t m = n.children[static_cast<std::size_t>(ch)];
            if (m >= 0 && c.nodes[static_cast<std::size_t>(m)].parent != static_cast<std::int32_t>(k)) {
                return "node " + std::to_string(m) + ": not its parent's child";
            }
            if (m >= 0 && sq[k].size <= smallest)
                return "node " + std::to_string(k) + ": children below the smallest size";
        }
        for (int l = 0; l < 3; ++l) {
            for (const auto j : run(c, k, l)) {
                owners[l][j].push_back(static_cast<int>(k));
                if (!sq[k].holds(c.faces[static_cast<std::size_t>(l)][j].bounds)) {
                    return "list " + std::to_string(l) + " face " + std::to_string(j) + ": outside its node's square";
                }
            }
            const auto r = run(c, k, l);
            if (!r.empty() && !(c.faces[static_cast<std::size_t>(l)][r.back()].flags & kLast)) {
                return "node " + std::to_string(k) + " list " + std::to_string(l) + ": a run not closed";
            }
        }
    }
    for (int l = 0; l < 3; ++l) {
        for (std::size_t j = 0; j < owners[l].size(); ++j) {
            if (owners[l][j].size() != 1) {
                return "list " + std::to_string(l) + " face " + std::to_string(j) + ": in " +
                       std::to_string(owners[l][j].size()) + " runs";
            }
        }
    }
    // the flags: per list, the children whose subtree holds faces of it
    std::vector<int> order;
    std::vector<std::size_t> todo{0};
    while (!todo.empty()) {
        const std::size_t k = todo.back();
        todo.pop_back();
        order.push_back(static_cast<int>(k));
        for (const auto m : c.nodes[k].children) {
            if (m >= 0)
                todo.push_back(static_cast<std::size_t>(m));
        }
    }
    std::vector<std::array<bool, 3>> holds(c.nodes.size(), {false, false, false});
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const std::size_t k = static_cast<std::size_t>(*it);
        std::uint16_t want = 0;
        for (int l = 0; l < 3; ++l) {
            holds[k][static_cast<std::size_t>(l)] = !run(c, k, l).empty();
            for (int ch = 0; ch < 4; ++ch) {
                const std::int32_t m = c.nodes[k].children[static_cast<std::size_t>(ch)];
                if (m >= 0 && holds[static_cast<std::size_t>(m)][static_cast<std::size_t>(l)]) {
                    want = static_cast<std::uint16_t>(want | 1u << (4 * l + ch));
                    holds[k][static_cast<std::size_t>(l)] = true;
                }
            }
        }
        if (want != c.nodes[k].flags)
            return "node " + std::to_string(k) + ": flags not as its children hold";
    }
    return {};
}

void build_tree(RoomCollision* c, std::int32_t centre_x, std::int32_t centre_z, std::int32_t size,
                std::int32_t smallest, std::array<std::vector<std::size_t>, 3>* order) {
    c->set_root(centre_x, centre_z, size, smallest);
    struct Item {
        int list;
        RoomFace face;
        std::size_t index;  // in its list before
    };
    std::vector<Item> all;
    for (int l = 0; l < 3; ++l) {
        const auto& list = c->faces[static_cast<std::size_t>(l)];
        for (std::size_t j = 0; j < list.size(); ++j)
            all.push_back({l, list[j], j});
    }
    std::array<std::vector<RoomFace>, 3> faces;
    if (order != nullptr) {
        for (auto& o : *order)
            o.clear();
    }
    std::vector<RoomNode> nodes;
    // depth first, the node's own faces first; children in order 0-3
    std::function<std::size_t(std::vector<Item>&, const Square&, std::int32_t)> make = [&](std::vector<Item>& items,
                                                                                           const Square& s,
                                                                                           std::int32_t parent) {
        const std::size_t k = nodes.size();
        nodes.push_back(RoomNode{});
        nodes[k].parent = parent;
        std::vector<Item> stay;
        std::array<std::vector<Item>, 4> down;
        if (items.size() > kSplitAbove && s.size > smallest) {
            for (auto& it : items) {
                int to = -1;
                for (int ch = 0; ch < 4 && to < 0; ++ch) {
                    if (s.child(ch).holds(it.face.bounds))
                        to = ch;
                }
                (to < 0 ? stay : down[static_cast<std::size_t>(to)]).push_back(std::move(it));
            }
        } else {
            stay = std::move(items);
        }
        for (int l = 0; l < 3; ++l) {
            bool any = false;
            for (const auto& it : stay) {
                if (it.list != l)
                    continue;
                if (!any)
                    nodes[k].first[static_cast<std::size_t>(l)] =
                        static_cast<std::int16_t>(faces[static_cast<std::size_t>(l)].size());
                any = true;
                RoomFace f = it.face;
                f.flags = static_cast<std::uint16_t>(f.flags & kSkip);
                faces[static_cast<std::size_t>(l)].push_back(f);
                if (order != nullptr)
                    (*order)[static_cast<std::size_t>(l)].push_back(it.index);
            }
            if (any)
                faces[static_cast<std::size_t>(l)].back().flags |= kLast;
        }
        for (int ch = 0; ch < 4; ++ch) {
            if (down[static_cast<std::size_t>(ch)].empty())
                continue;
            std::array<bool, 3> lists{false, false, false};
            for (const auto& it : down[static_cast<std::size_t>(ch)])
                lists[static_cast<std::size_t>(it.list)] = true;
            const std::size_t m = make(down[static_cast<std::size_t>(ch)], s.child(ch), static_cast<std::int32_t>(k));
            nodes[k].children[static_cast<std::size_t>(ch)] = static_cast<std::int32_t>(m);
            for (int l = 0; l < 3; ++l) {
                if (lists[static_cast<std::size_t>(l)])
                    nodes[k].flags = static_cast<std::uint16_t>(nodes[k].flags | 1u << (4 * l + ch));
            }
        }
        return k;
    };
    if (!all.empty())
        make(all, Square{centre_x, centre_z, size}, -1);
    c->faces = std::move(faces);
    c->nodes = std::move(nodes);
}

std::string compare_walks(const RoomCollision& a, const RoomCollision& b) {
    std::vector<Square> sa, sb;
    if (!squares(a, &sa) || !squares(b, &sb))
        return "a tree that cannot be walked";
    const std::int64_t x0 = a.root(0) - a.root(2) / 2, z0 = a.root(1) - a.root(2) / 2, span = a.root(2);
    const int cells = 16;
    for (int i = 0; i < cells; ++i) {
        for (int j = 0; j < cells; ++j) {
            // a box of a cell, and a point at its corner (as a ray straight down)
            const std::int64_t qx = x0 + span * i / cells, qz = z0 + span * j / cells;
            for (const std::array<std::int64_t, 4> q :
                 {std::array<std::int64_t, 4>{qx, qz, qx + span / cells, qz + span / cells},
                  std::array<std::int64_t, 4>{qx, qz, qx, qz}}) {
                for (int l = 0; l < 3; ++l) {
                    std::multiset<FaceKey> every;
                    for (const auto& f : a.faces[static_cast<std::size_t>(l)]) {
                        if (!(f.flags & kSkip) && overlaps(f, q))
                            every.insert(key_of(f));
                    }
                    if (walk(a, sa, l, q) != every)
                        return "the first tree's walk misses faces of list " + std::to_string(l);
                    if (walk(b, sb, l, q) != every)
                        return "the second tree's walk finds other faces of list " + std::to_string(l);
                }
            }
        }
    }
    return {};
}

}  // namespace studio

namespace studio {

namespace {

using Vec3d = std::array<double, 3>;
using Fx3 = std::array<std::int32_t, 3>;

Vec3d sub(const Fx3& a, const Fx3& b) {
    return {static_cast<double>(a[0]) - b[0], static_cast<double>(a[1]) - b[1], static_cast<double>(a[2]) - b[2]};
}
Vec3d cross(const Vec3d& a, const Vec3d& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Vec3d& a, const Vec3d& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
double length(const Vec3d& a) {
    return std::sqrt(dot(a, a));
}

// A plane of the face's 0x3c bytes: slot 0 the face's, 1-4 its edges'.
void put_plane(RoomFace* f, int slot, const Vec3d& direction, const Fx3& at) {
    const double l = length(direction);
    std::int16_t n[3] = {0, 0, 0};
    if (l > 0) {
        for (std::size_t k = 0; k < 3; ++k)
            n[k] = static_cast<std::int16_t>(std::lround(direction[k] / l * 4096.0));
    }
    const std::int64_t sum = static_cast<std::int64_t>(at[0]) * n[0] + static_cast<std::int64_t>(at[1]) * n[1] +
                             static_cast<std::int64_t>(at[2]) * n[2];
    const std::int32_t d = static_cast<std::int32_t>(sum >> 12);
    std::uint8_t* o = f->planes.data() + 12 * slot;
    for (int k = 0; k < 3; ++k) {
        o[2 * k] = static_cast<std::uint8_t>(n[k] & 0xff);
        o[2 * k + 1] = static_cast<std::uint8_t>((n[k] >> 8) & 0xff);
    }
    o[6] = o[7] = 0;
    for (int k = 0; k < 4; ++k)
        o[8 + k] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(d) >> (8 * k)) & 0xff);
}

Vec3d face_normal(const RoomFace& f) {
    return cross(sub(f.v[1], f.v[0]), sub(f.v[2], f.v[0]));
}

// Whether a face of 3-4 corners is convex and has area, turning one way
// about its normal (v1 - v0) x (v2 - v0).
bool convex(const RoomFace& f) {
    const Vec3d n = face_normal(f);
    if (length(n) <= 0)
        return false;
    const std::size_t m = f.corners;
    for (std::size_t i = 0; i < m; ++i) {
        const Vec3d a = sub(f.v[(i + 1) % m], f.v[i]), b = sub(f.v[(i + 2) % m], f.v[(i + 1) % m]);
        if (dot(cross(a, b), n) <= 0)
            return false;
    }
    return true;
}

std::array<std::int32_t, 4> bounds_of(const RoomFace& f) {
    std::array<std::int32_t, 4> b{f.v[0][0], f.v[0][2], f.v[0][0], f.v[0][2]};
    for (std::size_t i = 1; i < f.corners; ++i) {
        b[0] = std::min(b[0], f.v[i][0]);
        b[1] = std::min(b[1], f.v[i][2]);
        b[2] = std::max(b[2], f.v[i][0]);
        b[3] = std::max(b[3], f.v[i][2]);
    }
    return b;
}

bool is_gate(const std::string& name) {
    return name.size() == 6 && name.rfind("gate", 0) == 0 && std::isdigit(static_cast<unsigned char>(name[4])) &&
           std::isdigit(static_cast<unsigned char>(name[5]));
}

// Whether point (x, z) is inside face `g` seen from above (either winding).
bool holds_point(const RoomFace& g, double x, double z) {
    int sign = 0;
    for (std::size_t i = 0; i < g.corners; ++i) {
        const auto& a = g.v[i];
        const auto& b = g.v[(i + 1) % g.corners];
        const double c =
            (static_cast<double>(b[0]) - a[0]) * (z - a[2]) - (static_cast<double>(b[2]) - a[2]) * (x - a[0]);
        const int s = c > 0 ? 1 : c < 0 ? -1 : 0;
        if (s != 0 && sign != 0 && s != sign)
            return false;
        if (s != 0)
            sign = s;
    }
    return true;
}

// Whether point `p` is within 0.15 units of face f, over it.
bool near_face(const RoomFace& f, const Vec3d& p) {
    if (f.corners < 3)
        return false;
    const Vec3d n = face_normal(f);
    const double len = length(n);
    if (len <= 0)
        return false;
    const Vec3d d{p[0] - f.v[0][0], p[1] - f.v[0][1], p[2] - f.v[0][2]};
    if (std::abs(dot(n, d)) / len > 614.0)
        return false;
    for (std::size_t i = 0; i < f.corners; ++i) {
        const auto& a = f.v[i];
        const auto& b = f.v[(i + 1) % f.corners];
        const Vec3d edge{static_cast<double>(b[0]) - a[0], static_cast<double>(b[1]) - a[1],
                         static_cast<double>(b[2]) - a[2]};
        const Vec3d to{p[0] - a[0], p[1] - a[1], p[2] - a[2]};
        if (dot(cross(edge, to), n) < 0)
            return false;
    }
    return true;
}

}  // namespace

void compute_planes(int list, RoomFace* f) {
    (void)list;
    f->planes.fill(0);
    put_plane(f, 0, face_normal(*f), f->v[0]);
}

bool build_collision(const CollisionSource& source, const RoomCollision& original, const CollisionRules& rules,
                     RoomCollision* out, CollisionReport* report, std::string* why) {
    auto fail = [&](const std::string& m) {
        if (why != nullptr)
            *why = m;
        return false;
    };
    CollisionReport r;
    if (original.header.size() != 0xb0)
        return fail("the room has no collision in the game's data to replace");

    // the corners at the game's precision, welded
    std::map<Fx3, int> id_of;
    std::vector<Fx3> points;
    auto weld = [&](const std::array<float, 3>& p, int* id) {
        Fx3 q{};
        for (std::size_t k = 0; k < 3; ++k) {
            const double v = std::round(static_cast<double>(p[k]) * 4096.0);
            if (!(std::fabs(v) < 1073741824.0))
                return false;  // past 2^18 units: no
            q[k] = static_cast<std::int32_t>(v);
        }
        const auto found = id_of.emplace(q, static_cast<int>(points.size()));
        if (found.second)
            points.push_back(q);
        *id = found.first->second;
        return true;
    };
    struct Tri {
        std::array<int, 3> id{};
        Vec3d unit{};
        int list = kWall;
        std::vector<std::string> names;
        std::size_t index = 0;
    };
    std::vector<Tri> tris;
    std::map<std::string, std::vector<std::string>> tags_of;  // tags_in_name's, by material
    for (std::size_t t = 0; t < source.triangles.size(); ++t) {
        if (t < source.passable.size() && source.passable[t] != 0) {
            ++r.passable;
            continue;
        }
        Tri tri;
        tri.index = t;
        for (std::size_t k = 0; k < 3; ++k) {
            if (!weld(source.triangles[t][k], &tri.id[k]))
                return fail("triangle " + std::to_string(t) + " lies too far for the game's numbers");
        }
        const Vec3d n =
            cross(sub(points[static_cast<std::size_t>(tri.id[1])], points[static_cast<std::size_t>(tri.id[0])]),
                  sub(points[static_cast<std::size_t>(tri.id[2])], points[static_cast<std::size_t>(tri.id[0])]));
        const double l = length(n);
        if (tri.id[0] == tri.id[1] || tri.id[1] == tri.id[2] || tri.id[0] == tri.id[2] || l <= 0) {
            ++r.dropped;
            continue;
        }
        tri.unit = {n[0] / l, n[1] / l, n[2] / l};
        tri.list = tri.unit[1] > rules.ground_limit ? kGround : tri.unit[1] < -rules.ground_limit ? kCeiling : kWall;
        std::set<std::string> names;
        if (t < source.materials.size()) {
            const auto m = rules.names_by_material.find(source.materials[t]);
            if (m != rules.names_by_material.end())
                names.insert(m->second.begin(), m->second.end());
            auto own = tags_of.find(source.materials[t]);
            if (own == tags_of.end())
                own = tags_of.emplace(source.materials[t], tags_in_name(source.materials[t])).first;
            names.insert(own->second.begin(), own->second.end());
        }
        const auto byt = rules.names_by_triangle.find(t);
        if (byt != rules.names_by_triangle.end())
            names.insert(byt->second.begin(), byt->second.end());
        tri.names.assign(names.begin(), names.end());
        for (const auto& name : tri.names) {
            if (name.empty() || name.size() > 8)
                return fail("the name \"" + name + "\" is not 1-8 characters");
        }
        tris.push_back(std::move(tri));
    }

    // coplanar neighbours of one kind and names joined into convex quads:
    // triangle (x, p, q) and its neighbour across p-q, (q, p, y), make (x, p, y, q)
    std::map<std::pair<int, int>, std::vector<std::size_t>> by_edge;  // directed edge -> triangles
    for (std::size_t t = 0; t < tris.size(); ++t) {
        for (std::size_t k = 0; k < 3; ++k)
            by_edge[{tris[t].id[k], tris[t].id[(k + 1) % 3]}].push_back(t);
    }
    std::vector<bool> used(tris.size(), false);
    struct Piece {
        std::vector<int> ids;
        std::size_t tri;
        std::vector<std::size_t> sources;  // the triangles' indices in the source
    };
    std::vector<Piece> pieces;
    for (std::size_t t = 0; t < tris.size(); ++t) {
        if (used[t])
            continue;
        used[t] = true;
        const Tri& a = tris[t];
        bool joined = false;
        for (std::size_t k = 0; k < 3 && !joined; ++k) {
            const int p = a.id[k], q = a.id[(k + 1) % 3], x = a.id[(k + 2) % 3];
            const auto across = by_edge.find({q, p});
            if (across == by_edge.end())
                continue;
            for (const std::size_t u : across->second) {
                if (used[u] || tris[u].list != a.list || tris[u].names != a.names)
                    continue;
                if (dot(tris[u].unit, a.unit) < 1.0 - 1e-9)
                    continue;
                int y = -1;
                for (const int id : tris[u].id) {
                    if (id != p && id != q)
                        y = id;
                }
                if (y < 0)
                    continue;
                // the fourth corner on the first's plane (within 2 / 4096 of a unit)
                const Vec3d off = sub(points[static_cast<std::size_t>(y)], points[static_cast<std::size_t>(x)]);
                if (std::fabs(dot(off, a.unit)) > 2.0)
                    continue;
                RoomFace probe;
                probe.corners = 4;
                const int quad[4] = {x, p, y, q};
                for (std::size_t c = 0; c < 4; ++c)
                    probe.v[c] = points[static_cast<std::size_t>(quad[c])];
                if (!convex(probe))
                    continue;
                used[u] = true;
                pieces.push_back({{x, p, y, q}, t, {a.index, tris[u].index}});
                joined = true;
                break;
            }
        }
        if (!joined)
            pieces.push_back({{a.id[0], a.id[1], a.id[2]}, t, {a.index}});
    }

    // the names: the original's records, then any new one
    RoomCollision c;
    c.header = original.header;
    c.records18 = original.records18;
    c.named = original.named;
    auto record_of = [&](const std::string& name) -> int {
        for (std::size_t k = 0; k < c.named.size(); ++k) {
            if (c.named[k].text() == name)
                return static_cast<int>(k);
        }
        NamedRecord n;
        std::memcpy(n.name.data(), name.data(), std::min<std::size_t>(name.size(), 8));
        c.named.push_back(n);
        return static_cast<int>(c.named.size() - 1);
    };

    // each piece a face
    std::array<std::vector<std::vector<std::size_t>>, 3> sources;
    for (const Piece& piece : pieces) {
        const Tri& tri = tris[piece.tri];
        RoomFace f;
        f.corners = static_cast<std::uint16_t>(piece.ids.size());
        for (std::size_t k = 0; k < piece.ids.size(); ++k)
            f.v[k] = points[static_cast<std::size_t>(piece.ids[k])];
        f.bounds = bounds_of(f);
        const int list = tri.list;
        std::vector<int> gates, others;
        for (const auto& name : tri.names)
            (is_gate(name) ? gates : others).push_back(record_of(name));
        if (gates.size() > 1 || others.size() > 2) {
            return fail("triangle " + std::to_string(tri.index) + " is given " + std::to_string(tri.names.size()) +
                        " names: a face holds one gate and two others");
        }
        if (!gates.empty())
            f.slots[0] = static_cast<std::uint8_t>(gates[0]);
        for (std::size_t k = 0; k < others.size(); ++k)
            f.slots[1 + k] = static_cast<std::uint8_t>(others[k]);
        if (list == kGround) {
            // slot 3 and the last word: the nearest original ground's (not read)
            const Vec3d mid = {(static_cast<double>(f.bounds[0]) + f.bounds[2]) / 2, static_cast<double>(f.v[0][1]),
                               (static_cast<double>(f.bounds[1]) + f.bounds[3]) / 2};
            double best = -1;
            for (const auto& g : original.faces[kGround]) {
                const Vec3d gm = {(static_cast<double>(g.bounds[0]) + g.bounds[2]) / 2, static_cast<double>(g.v[0][1]),
                                  (static_cast<double>(g.bounds[1]) + g.bounds[3]) / 2};
                const Vec3d d{gm[0] - mid[0], gm[1] - mid[1], gm[2] - mid[2]};
                if (best < 0 || dot(d, d) < best) {
                    best = dot(d, d);
                    f.slots[3] = g.slots[3];
                    f.extra = g.extra;
                }
            }
        }
        compute_planes(list, &f);
        c.faces[static_cast<std::size_t>(list)].push_back(f);
        sources[static_cast<std::size_t>(list)].push_back(piece.sources);
    }
    if (c.named.size() > 0xff)
        return fail("more than 255 names");

    // the original's gates and walls that open that no new face takes: kept where the game had them
    for (std::size_t k = 0; k < original.named.size(); ++k) {
        const std::string name = original.named[k].text();
        if (!is_gate(name) && name.rfind("col_wall", 0) != 0)
            continue;
        if (rules.drop_original.count(name) != 0)
            continue;
        bool taken = false;
        for (int l = 0; l < 3 && !taken; ++l) {
            for (const auto& f : c.faces[static_cast<std::size_t>(l)]) {
                for (std::size_t p = 0; p < 4; ++p)
                    taken = taken || (slot_names(l, p) && f.slots[p] == k);
            }
        }
        if (taken)
            continue;
        bool any = false;
        for (int l = 0; l < 3; ++l) {
            for (const auto& f : original.faces[static_cast<std::size_t>(l)]) {
                bool names = false;
                for (std::size_t p = 0; p < 4; ++p)
                    names = names || (slot_names(l, p) && f.slots[p] == k);
                if (!names)
                    continue;
                // a face naming two kept names is copied once
                bool copied = false;
                for (const auto& g : c.faces[static_cast<std::size_t>(l)])
                    copied = copied || (g.v == f.v && g.slots == f.slots);
                if (copied)
                    continue;
                RoomFace g = f;
                g.flags = static_cast<std::uint16_t>(g.flags & ~0x8000u);
                c.faces[static_cast<std::size_t>(l)].push_back(g);
                sources[static_cast<std::size_t>(l)].push_back({});
                any = true;
            }
        }
        if (any)
            r.warnings.push_back({RoomWarning::name_kept, {name}});
    }
    // the markers moved
    if (!rules.markers.empty()) {
        auto list = markers(c);
        for (auto& m : list) {
            const auto moved = rules.markers.find(m.name);
            if (moved == rules.markers.end())
                continue;
            m.place = {moved->second[0], moved->second[1], moved->second[2]};
            m.extra = moved->second[3];
        }
        set_markers(&c, list);
    }
    for (int l = 0; l < 3; ++l) {
        if (c.faces[static_cast<std::size_t>(l)].size() > 0x7fff) {
            return fail("more than 32,767 faces of one kind (a node's first face is a 16-bit number)");
        }
    }
    const std::size_t total = c.faces[0].size() + c.faces[1].size() + c.faces[2].size();
    if (total == 0)
        return fail("no face: the model has no triangle with area");

    // the tree over a power-of-two square holding every face
    std::int64_t lo[2] = {INT64_MAX, INT64_MAX}, hi[2] = {INT64_MIN, INT64_MIN};
    for (const auto& list : c.faces) {
        for (const auto& f : list) {
            lo[0] = std::min<std::int64_t>(lo[0], f.bounds[0]);
            lo[1] = std::min<std::int64_t>(lo[1], f.bounds[1]);
            hi[0] = std::max<std::int64_t>(hi[0], f.bounds[2]);
            hi[1] = std::max<std::int64_t>(hi[1], f.bounds[3]);
        }
    }
    const std::int64_t extent = std::max(hi[0] - lo[0], hi[1] - lo[1]) + 2;
    std::int64_t smallest = original.root(3) > 0 ? original.root(3) : 8192;
    std::int64_t size = smallest * 2;
    while (size < extent)
        size *= 2;
    if (size > (std::int64_t{1} << 30))
        return fail("the room is wider than the game's numbers hold");
    smallest = std::max(smallest, size >> (kTreeLevels - 1));
    std::array<std::vector<std::size_t>, 3> order;
    build_tree(&c, static_cast<std::int32_t>((lo[0] + hi[0]) / 2), static_cast<std::int32_t>((lo[1] + hi[1]) / 2),
               static_cast<std::int32_t>(size), static_cast<std::int32_t>(smallest), &order);
    for (int l = 0; l < 3; ++l) {
        for (const std::size_t before : order[static_cast<std::size_t>(l)]) {
            r.face_triangles[static_cast<std::size_t>(l)].push_back(sources[static_cast<std::size_t>(l)][before]);
        }
    }

    // what the modder should know
    r.faces = {c.faces[0].size(), c.faces[1].size(), c.faces[2].size()};
    r.nodes = c.nodes.size();
    r.bytes = write_collision(c).size();
    if (r.dropped > 0)
        r.warnings.push_back({RoomWarning::dropped, {std::to_string(r.dropped)}});
    if (r.passable > 0)
        r.warnings.push_back({RoomWarning::passable, {std::to_string(r.passable)}});
    if (r.bytes > kLargestShippedCollision) {
        const std::size_t times = (r.bytes + kLargestShippedCollision / 2) / kLargestShippedCollision;
        r.warnings.push_back(
            {RoomWarning::too_big,
             {std::to_string(r.bytes), std::to_string(kLargestShippedCollision), std::to_string(times)}});
    }
    if (c.faces[kGround].empty())
        r.warnings.push_back({RoomWarning::no_ground, {}});
    for (std::size_t k = 0; k < original.named.size(); ++k) {
        const std::string name = original.named[k].text();
        bool on = false;
        for (int l = 0; l < 3 && !on; ++l) {
            for (const auto& f : c.faces[static_cast<std::size_t>(l)]) {
                for (std::size_t p = 0; p < 4; ++p)
                    on = on || (slot_names(l, p) && f.slots[p] == k);
            }
        }
        if (!on && (is_gate(name) || name.rfind("col_wall", 0) == 0)) {
            r.warnings.push_back({is_gate(name) ? RoomWarning::gate_unplaced : RoomWarning::wall_unplaced, {name}});
        }
    }
    for (const auto& m : markers(c)) {
        // a ground under it: a ground face holding its x/z, at most a unit above it
        bool under = false;
        for (const auto& g : c.faces[kGround]) {
            if (m.place[0] < g.bounds[0] || m.place[0] > g.bounds[2] || m.place[2] < g.bounds[1] ||
                m.place[2] > g.bounds[3])
                continue;
            if (!holds_point(g, m.place[0], m.place[2]))
                continue;
            const Vec3d n = face_normal(g);
            if (n[1] == 0)
                continue;
            const double y = g.v[0][1] - (n[0] * (m.place[0] - g.v[0][0]) + n[2] * (m.place[2] - g.v[0][2])) / n[1];
            if (y <= m.place[1] + 4096.0)
                under = true;
        }
        if (!under)
            r.warnings.push_back({RoomWarning::marker_floating, {m.name}});
        // a wall through its body, half a unit above its feet
        const Vec3d body{static_cast<double>(m.place[0]), m.place[1] + 2048.0, static_cast<double>(m.place[2])};
        bool in_wall = false;
        for (const auto& w : c.faces[kWall])
            in_wall = in_wall || near_face(w, body);
        if (in_wall) {
            char at[3][32];
            for (int k = 0; k < 3; ++k)
                std::snprintf(at[k], sizeof(at[k]), "%.2f", m.place[static_cast<std::size_t>(k)] / 4096.0);
            r.warnings.push_back({RoomWarning::marker_in_wall, {m.name, at[0], at[1], at[2]}});
        }
    }
    *out = std::move(c);
    if (report != nullptr)
        *report = std::move(r);
    return true;
}

std::string warning_text(const RoomWarning& w) {
    auto arg = [&](std::size_t k) { return k < w.args.size() ? w.args[k] : std::string(); };
    switch (w.kind) {
    case RoomWarning::dropped: return arg(0) + " triangles of no area at the game's precision left out";
    case RoomWarning::too_big:
        return "the collision is " + arg(0) + " bytes, " + arg(2) + " times the game's largest room (" + arg(1) +
               "); the port's memory takes it, but a simpler collision model is lighter";
    case RoomWarning::no_ground: return "no ground: a character falls through everything";
    case RoomWarning::gate_unplaced: return arg(0) + " is on no face: its exit cannot be taken";
    case RoomWarning::wall_unplaced: return arg(0) + " is on no face: the wall it opens is not there";
    case RoomWarning::name_kept: return arg(0) + " kept where the game had it (its faces copied from the original)";
    case RoomWarning::marker_floating: return "marker " + arg(0) + " has no ground under it";
    case RoomWarning::collision_shared: return "room " + arg(0) + " uses the same collision: it changes too";
    case RoomWarning::model_shared:
        return "room " + arg(0) + " draws model " + arg(1) + " too: it changes there as well";
    case RoomWarning::passable:
        return arg(0) +
               " triangles of translucent materials left out of the collision: nothing stands on them or stops at them";
    case RoomWarning::marker_in_wall:
        return "marker " + arg(0) + " stands in a wall (at " + arg(1) + " " + arg(2) + " " + arg(3) + ")";
    }
    return {};
}

bool ground_under(const RoomCollision& c, double x, double z, double top, double* y) {
    bool found = false;
    for (const auto& g : c.faces[kGround]) {
        if (x < g.bounds[0] || x > g.bounds[2] || z < g.bounds[1] || z > g.bounds[3] || g.corners < 3)
            continue;
        int sign = 0;
        bool inside = true;
        for (std::size_t i = 0; i < g.corners && inside; ++i) {
            const auto& a = g.v[i];
            const auto& b = g.v[(i + 1) % g.corners];
            const double cz =
                (static_cast<double>(b[0]) - a[0]) * (z - a[2]) - (static_cast<double>(b[2]) - a[2]) * (x - a[0]);
            const int s = cz > 0 ? 1 : cz < 0 ? -1 : 0;
            if (s != 0 && sign != 0 && s != sign)
                inside = false;
            if (s != 0)
                sign = s;
        }
        if (!inside)
            continue;
        const double e0[3] = {static_cast<double>(g.v[1][0]) - g.v[0][0], static_cast<double>(g.v[1][1]) - g.v[0][1],
                              static_cast<double>(g.v[1][2]) - g.v[0][2]};
        const double e1[3] = {static_cast<double>(g.v[2][0]) - g.v[0][0], static_cast<double>(g.v[2][1]) - g.v[0][1],
                              static_cast<double>(g.v[2][2]) - g.v[0][2]};
        const double n[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                             e0[0] * e1[1] - e0[1] * e1[0]};
        if (n[1] == 0)
            continue;
        const double h = g.v[0][1] - (n[0] * (x - g.v[0][0]) + n[2] * (z - g.v[0][2])) / n[1];
        if (h <= top && (!found || h > *y)) {
            *y = h;
            found = true;
        }
    }
    return found;
}

std::string check_collision(const RoomCollision& built, const RoomCollision& original) {
    const auto bytes = write_collision(built);
    RoomCollision back;
    std::string why;
    if (!parse_collision(bytes, &back, &why))
        return "it does not read back: " + why;
    if (write_collision(back) != bytes)
        return "it does not write back the same";
    if (built.header.size() != 0xb0)
        return "no header";
    if ((built.header[0x75] & 0x80) != 0)
        return "the header says its pointers are resolved";
    for (int l = 0; l < 3; ++l) {
        const auto& list = built.faces[static_cast<std::size_t>(l)];
        if (list.size() > 0x7fff)
            return "more faces of one kind than a node can point at";
        for (std::size_t j = 0; j < list.size(); ++j) {
            const RoomFace& f = list[j];
            const std::string at = "list " + std::to_string(l) + " face " + std::to_string(j);
            if (f.corners < 3 || f.corners > 4)
                return at + ": " + std::to_string(f.corners) + " corners";
            if (f.bounds != bounds_of(f))
                return at + ": bounds not its corners'";
            if (!convex(f))
                return at + ": not convex, or no area (no plane the game can build)";
            for (std::size_t p = 0; p < 4; ++p) {
                if (slot_names(l, p) && f.slots[p] != 0xff && f.slots[p] >= built.named.size())
                    return at + ": a slot past the names";
            }
        }
    }
    const std::string tree = check_tree(built);
    if (!tree.empty())
        return "the tree: " + tree;
    for (const auto& n : original.named) {
        bool kept = false;
        for (const auto& m : built.named)
            kept = kept || m.text() == n.text();
        if (!kept)
            return "the room's name " + n.text() + " is gone";
    }
    return {};
}

}  // namespace studio
