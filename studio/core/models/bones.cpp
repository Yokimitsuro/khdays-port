#include "core/models/bones.h"
#include "khdays/assets/matrix4.h"

#include "core/files/paths.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace studio {

namespace {

constexpr Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// models.cpp line 58
constexpr float kPoseDegrees = 20.0f;

std::array<float, 3> translation(const Mat& m) {
    return {m[12], m[13], m[14]};
}

float length(const std::array<float, 3>& v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// models.cpp arc(), lines 131-151
Mat arc(std::array<float, 3> from, std::array<float, 3> to, float& degrees) {
    const float lf = length(from), lt = length(to);
    for (int i = 0; i < 3; ++i) {
        from[static_cast<std::size_t>(i)] /= lf;
        to[static_cast<std::size_t>(i)] /= lt;
    }
    const float c = std::fmax(-1.0f, std::fmin(1.0f, from[0] * to[0] + from[1] * to[1] + from[2] * to[2]));
    std::array<float, 3> axis{from[1] * to[2] - from[2] * to[1], from[2] * to[0] - from[0] * to[2],
                              from[0] * to[1] - from[1] * to[0]};
    degrees = std::acos(c) * 57.2957795f;
    const float s = length(axis);
    if (s < 1e-6f)
        return kIdentity;
    for (auto& a : axis)
        a /= s;
    const float t = 1.0f - c, x = axis[0], y = axis[1], z = axis[2];
    return {t * x * x + c,
            t * x * y + s * z,
            t * x * z - s * y,
            0,
            t * x * y - s * z,
            t * y * y + c,
            t * y * z + s * x,
            0,
            t * x * z + s * y,
            t * y * z - s * x,
            t * z * z + c,
            0,
            0,
            0,
            0,
            1};
}

// models.cpp place(), lines 153-163: K = T(at) * linear(d) * correction * T(-head)
Mat place(const std::array<float, 3>& at, const Mat& d, const Mat& correction, const std::array<float, 3>& head) {
    Mat l = d;
    l[12] = l[13] = l[14] = 0.0f;
    Mat k = mat_mul(l, correction);
    const auto h = mat_vec(k, head);
    k[12] = at[0] - h[0];
    k[13] = at[1] - h[1];
    k[14] = at[2] - h[2];
    return k;
}

}  // namespace

Mat mat_mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
            r[static_cast<std::size_t>(c * 4 + row)] = s;
        }
    }
    return r;
}

Mat mat_inverse(const Mat& m) {
    return khdays::assets::inverse4(m);
}

std::array<float, 3> mat_point(const Mat& m, const std::array<float, 3>& p) {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

std::array<float, 3> mat_vec(const Mat& m, const std::array<float, 3>& v) {
    return {m[0] * v[0] + m[4] * v[1] + m[8] * v[2], m[1] * v[0] + m[5] * v[1] + m[9] * v[2],
            m[2] * v[0] + m[6] * v[1] + m[10] * v[2]};
}

BonesFile read_bones_file(const fs::path& file) {
    // models.cpp read_bones(), lines 209-227
    BonesFile b;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == '#')
            b.comments.push_back(line);
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        std::istringstream words(line);
        std::string a, c;
        if (!(words >> a >> c))
            continue;
        if (a == "scale") {
            b.scale = std::strtof(c.c_str(), nullptr);
            if (!(b.scale > 0.0f))
                b.scale = 1.0f;
        } else if (a == "cull") {  // another value: ignored, as the port does
            if (c == "back")
                b.cull = Cull::back;
            if (c == "none")
                b.cull = Cull::none;
        } else {
            b.pairs.emplace_back(a, c);
        }
    }
    return b;
}

bool writable_name(const std::string& name) {
    if (name.empty())
        return false;
    for (const unsigned char ch : name) {
        if (ch == '#' || std::isspace(ch))
            return false;
    }
    return true;
}

std::string bones_text(const BonesFile& b) {
    std::ostringstream out;
    if (b.comments.empty()) {
        out << "# written by khdays-studio: the joints of this model that follow the game's nodes\n"
               "# (<joint> <node>; a joint named like a node follows it without a line), its size\n"
               "# to the game's (scale), and whether its back faces are culled\n";
    }
    for (const auto& c : b.comments)
        out << c << "\n";
    if (b.scale != 1.0f)
        out << "scale " << b.scale << "\n";
    if (b.cull == Cull::back)
        out << "cull back\n";
    if (b.cull == Cull::none)
        out << "cull none\n";
    for (const auto& [joint, node] : b.pairs)
        out << joint << " " << node << "\n";
    return out.str();
}

Rig build_rig(const GameModel& g, const khdays::assets::ImportedModel& mod,
              const std::map<std::string, std::string>& pairs, float scale) {
    Rig rig;
    rig.scale = scale;
    const float k = scale;
    const auto& names = g.model.object_names;

    // the joints, parents first; each to the node it follows (lines 273-310)
    const std::size_t n = mod.joints.size();
    std::vector<int> order, placed(n, -1);
    for (std::size_t pass = 0; order.size() < n && pass <= n; ++pass) {
        for (std::size_t j = 0; j < n; ++j) {
            const int p = mod.joints[j].parent;
            if (placed[j] < 0 && (p < 0 || placed[static_cast<std::size_t>(p)] >= 0)) {
                placed[j] = static_cast<int>(order.size());
                order.push_back(static_cast<int>(j));
            }
        }
    }
    for (const int j : order) {
        const auto& in = mod.joints[static_cast<std::size_t>(j)];
        RigJoint out;
        out.name = in.name;
        out.palette_index = in.palette_index;
        out.parent = in.parent >= 0 ? placed[static_cast<std::size_t>(in.parent)] : -1;
        const auto pair = pairs.find(in.name);
        const std::string& wanted = pair != pairs.end() ? pair->second : in.name;
        for (std::size_t node = 0; node < names.size(); ++node) {
            if (names[node] == wanted)
                out.node = static_cast<int>(node);
        }
        if (pair != pairs.end() && out.node < 0)
            rig.missing_nodes.push_back(wanted);
        Mat world, ibm;
        std::copy(in.world.begin(), in.world.end(), world.begin());
        std::copy(in.inverse_bind.begin(), in.inverse_bind.end(), ibm.begin());
        Mat s = kIdentity;
        s[0] = s[5] = s[10] = k;
        out.bind = mat_mul(s, mat_mul(world, ibm));
        const auto h = translation(world);
        out.rest_head = h;
        out.head = {h[0] * k, h[1] * k, h[2] * k};
        rig.joint_of_palette[in.palette_index] = static_cast<int>(rig.joints.size());
        rig.joints.push_back(std::move(out));
    }

    // the rest pose correction, from the roots down (lines 312-360)
    auto descends = [&](int j, int from) {
        for (int up = j; up >= 0; up = rig.joints[static_cast<std::size_t>(up)].parent) {
            if (up == from)
                return true;
        }
        return false;
    };
    for (std::size_t j = 0; j < rig.joints.size(); ++j) {
        RigJoint& joint = rig.joints[j];
        joint.correction =
            joint.parent >= 0 ? rig.joints[static_cast<std::size_t>(joint.parent)].correction : kIdentity;
        if (joint.node < 0)
            continue;
        const auto hg = translation(g.rest[static_cast<std::size_t>(joint.node)]);
        int child = -1, children = 0;
        for (std::size_t c = 0; c < rig.joints.size(); ++c) {
            const int cn = rig.joints[c].node;
            if (cn >= 0 && g.parents[static_cast<std::size_t>(cn)] == joint.node && c != j &&
                descends(static_cast<int>(c), static_cast<int>(j))) {
                child = static_cast<int>(c);
                ++children;
            }
        }
        if (children == 1) {
            const RigJoint& to = rig.joints[static_cast<std::size_t>(child)];
            const auto hc = translation(g.rest[static_cast<std::size_t>(to.node)]);
            const auto ds = mat_vec(
                joint.correction, {to.head[0] - joint.head[0], to.head[1] - joint.head[1], to.head[2] - joint.head[2]});
            const std::array<float, 3> dg{hc[0] - hg[0], hc[1] - hg[1], hc[2] - hg[2]};
            if (length(ds) > 1e-5f && length(dg) > 1e-5f) {
                float degrees = 0.0f;
                const Mat r = arc(ds, dg, degrees);
                if (degrees > kPoseDegrees)
                    joint.correction = mat_mul(r, joint.correction);
            }
        }
        bool identity = true;
        for (int i = 0; i < 16; ++i) {
            identity = identity && std::fabs(joint.correction[static_cast<std::size_t>(i)] -
                                             kIdentity[static_cast<std::size_t>(i)]) < 1e-6f;
        }
        joint.exact = identity && std::fabs(joint.head[0] - hg[0]) < 1e-4f &&
                      std::fabs(joint.head[1] - hg[1]) < 1e-4f && std::fabs(joint.head[2] - hg[2]) < 1e-4f;
    }
    return rig;
}

void pose_rig(const GameModel& g, const Rig& rig, const std::vector<Mat>& node_world, std::vector<Mat>* skin,
              Mat* unskinned, std::vector<Mat>* place_out) {
    // D per node (lines 428-452); in the preview every node is sent, so the
    // first node is the model's first
    const std::size_t nodes = g.rest.size();
    std::vector<Mat> d(nodes, kIdentity);
    for (std::size_t node = 0; node < nodes && node < node_world.size(); ++node) {
        d[node] = mat_mul(node_world[node], mat_inverse(g.rest[node]));
    }
    const Mat root = nodes > 0 ? d[0] : kIdentity;

    // each joint, parents first (lines 471-486)
    std::vector<Mat> k(rig.joints.size(), kIdentity);
    skin->assign(rig.joints.size(), kIdentity);
    for (std::size_t j = 0; j < rig.joints.size(); ++j) {
        const RigJoint& joint = rig.joints[j];
        const Mat* parent = joint.parent >= 0 ? &k[static_cast<std::size_t>(joint.parent)] : nullptr;
        if (joint.node >= 0 && joint.exact) {
            k[j] = d[static_cast<std::size_t>(joint.node)];
        } else if (joint.node >= 0) {
            const Mat& dg = d[static_cast<std::size_t>(joint.node)];
            k[j] = place(mat_point(parent != nullptr ? *parent : dg, joint.head), dg, joint.correction, joint.head);
        } else {
            k[j] = parent != nullptr ? *parent : root;
        }
        (*skin)[j] = mat_mul(k[j], joint.bind);
    }
    // a mesh with no skin: placed with the model's first node (lines 487-488)
    *unskinned = mat_mul(root, Mat{rig.scale, 0, 0, 0, 0, rig.scale, 0, 0, 0, 0, rig.scale, 0, 0, 0, 0, 1});
    if (place_out != nullptr)
        *place_out = std::move(k);
}

CarriedPairs carry_pairs(const std::map<std::string, std::string>& before, const khdays::assets::ImportedModel& model,
                         std::map<std::string, std::string>* pairs) {
    CarriedPairs out;
    for (const auto& [joint, node] : before) {
        if (pairs->count(joint) != 0)
            continue;  // its own bones.txt names it
        const bool has =
            std::any_of(model.joints.begin(), model.joints.end(), [&](const auto& j) { return j.name == joint; });
        if (has) {
            (*pairs)[joint] = node;
            ++out.kept;
        } else {
            out.lost.push_back(joint);
        }
    }
    return out;
}

std::map<int, int> propose_pairs(const GameModel& g, const Rig& rig) {
    // One proposal for each game node no joint follows yet: the nearest joint
    // that follows nothing, nearest pairs first, each joint proposed once --
    // so a model's many extra joints (fingers, hair, cloth) are left to move
    // with their parents, as the port does with them.
    std::vector<char> followed(g.rest.size(), 0);
    for (const auto& j : rig.joints) {
        if (j.node >= 0)
            followed[static_cast<std::size_t>(j.node)] = 1;
    }
    struct Candidate {
        float distance;
        int joint, node;
    };
    std::vector<Candidate> all;
    for (std::size_t node = 0; node < g.rest.size(); ++node) {
        if (followed[node])
            continue;
        const auto h = translation(g.rest[node]);
        for (std::size_t j = 0; j < rig.joints.size(); ++j) {
            if (rig.joints[j].node >= 0)
                continue;
            const std::array<float, 3> dv{h[0] - rig.joints[j].head[0], h[1] - rig.joints[j].head[1],
                                          h[2] - rig.joints[j].head[2]};
            all.push_back({length(dv), static_cast<int>(j), static_cast<int>(node)});
        }
    }
    std::sort(all.begin(), all.end(), [](const Candidate& a, const Candidate& b) {
        return a.distance < b.distance || (a.distance == b.distance && a.joint < b.joint);
    });
    std::map<int, int> out;
    std::vector<char> node_taken(g.rest.size(), 0);
    for (const auto& c : all) {
        if (node_taken[static_cast<std::size_t>(c.node)] || out.count(c.joint) != 0)
            continue;
        node_taken[static_cast<std::size_t>(c.node)] = 1;
        out[c.joint] = c.node;
    }
    return out;
}

}  // namespace studio
