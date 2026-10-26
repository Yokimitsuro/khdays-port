// A mod's models drawn instead of the game's (models.h): matrices column-major for column vectors;
// each joint drawn with K_j = T(head now) * linear(D_G) * C_j * T(-head_j), D_G = M_G * inverse(W_G)
// for the node G it follows and C_j its rest pose correction; faces culled as the file says unless
// <model>.bones.txt says otherwise (docs/RENDERING.md, "Mods' graphics"). A replaced room plays its own
// animation (room_animation.cpp) by the game's frames, stopped while the game is paused.

#include "models.h"

extern "C" {
#include "../../gfx/g3d.h"
#include "../core/origin.h"
#include "../core/events.h" // khdays_events_vblank_count
#include "../core/rom.h"    // khdays_mods_find
#include "../core/session_log.h"
#include "../scripts/game_link.h" // khdays_link_paused
#include "textures.h"
#include "../core/window.h"
}
#include "names.h"
#include "room_animation.h"

#include "khdays/assets/fbx.h"
#include "khdays/assets/gltf.h"
#include "khdays/assets/mesh.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using Mat = std::array<float, 16>;
constexpr Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// The rest pose correction leaves a joint as it is when the turn it would
// take is smaller than this: such differences are the two models' shapes,
// not their poses (a T-pose's arms are 70-90 degrees from arms-down).
constexpr float kPoseDegrees = 20.0F;

Mat mul(const Mat &a, const Mat &b)
{
    Mat r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0F;
            for (int k = 0; k < 4; ++k)
                s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    }
    return r;
}

// The general 4x4 inverse (cofactors); the identity when singular.
Mat inverse(const Mat &m)
{
    Mat inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12F)
        return kIdentity;
    for (auto &v : inv)
        v /= det;
    return inv;
}

std::array<float, 3> point(const Mat &m, const std::array<float, 3> &p)
{
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

std::array<float, 3> vec(const Mat &m, const std::array<float, 3> &v)
{
    return {m[0] * v[0] + m[4] * v[1] + m[8] * v[2], m[1] * v[0] + m[5] * v[1] + m[9] * v[2],
            m[2] * v[0] + m[6] * v[1] + m[10] * v[2]};
}

std::array<float, 3> translation(const Mat &m)
{
    return {m[12], m[13], m[14]};
}

float length(const std::array<float, 3> &v)
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// The shortest turn taking direction `from` to direction `to`, and its angle
// in degrees.
Mat arc(std::array<float, 3> from, std::array<float, 3> to, float &degrees)
{
    const float lf = length(from), lt = length(to);
    for (int i = 0; i < 3; ++i) {
        from[i] /= lf;
        to[i] /= lt;
    }
    const float c = std::fmax(-1.0F, std::fmin(1.0F, from[0] * to[0] + from[1] * to[1] + from[2] * to[2]));
    std::array<float, 3> axis{from[1] * to[2] - from[2] * to[1], from[2] * to[0] - from[0] * to[2],
                              from[0] * to[1] - from[1] * to[0]};
    degrees = std::acos(c) * 57.2957795F;
    const float s = length(axis);
    if (s < 1e-6F)
        return kIdentity; // the same way (or exactly opposite: left alone)
    for (auto &a : axis)
        a /= s;
    const float t = 1.0F - c, x = axis[0], y = axis[1], z = axis[2];
    return {t * x * x + c,
            t * x * y + s * z,
            t * x * z - s * y,
            0, // column 0
            t * x * y - s * z,
            t * y * y + c,
            t * y * z + s * x,
            0, // column 1
            t * x * z + s * y,
            t * y * z - s * x,
            t * z * z + c,
            0, // column 2
            0,
            0,
            0,
            1};
}

// K = T(at) * linear(d) * correction * T(-head)
Mat place(const std::array<float, 3> &at, const Mat &d, const Mat &correction, const std::array<float, 3> &head)
{
    Mat l = d;
    l[12] = l[13] = l[14] = 0.0F;
    Mat k = mul(l, correction);
    const auto h = vec(k, head);
    k[12] = at[0] - h[0];
    k[13] = at[1] - h[1];
    k[14] = at[2] - h[2];
    return k;
}

struct Joint {
    std::string name;
    int parent = -1;    // in Model::joints, which lists parents first
    int node = -1;      // the game's node it follows
    bool exact = false; // at its node's rest place, uncorrected: drawn with D_G
    std::array<float, 3> head{};
    float degrees = 0.0F; // its pose's turn from its node's (the session log's diagnosis)
    Mat correction = kIdentity;
    Mat bind = kIdentity; // the joint's vertices to the model at rest: scale * world * inverse bind
    int room_node = -1;   // its node in the file, which the model's own animation moves
    Mat inverse_bind = kIdentity;
};

// How a mesh's vertex colours' alpha counts (glTF: it multiplies the texture's): not at all (an opaque
// material); a triangle under the cutoff left out (a mask); the triangle's polygon alpha lowered by it
// (blended). Per triangle, by its corners' mean: the DS's alpha is a polygon's, not a vertex's.
enum class VertexAlpha { ignored, cut, blended };

struct Mesh {
    std::vector<khdays::assets::NeutralVertex> vertices; // joints: Model::joints indices, -1 none
    std::vector<std::uint32_t> indices;
    int replacement = -1;
    float tex_w = 1.0F, tex_h = 1.0F;
    bool double_sided = false, translucent = false;
    VertexAlpha vertex_alpha = VertexAlpha::ignored;
    float cutoff = 0.5F;
    // its own moving texture: texels added a game frame; pictures in turn (the textures made for them,
    // aligned with `frames`)
    std::array<float, 2> scroll{0.0F, 0.0F};
    struct Picture {
        int replacement = -1;
        float w = 1.0F, h = 1.0F;
        bool translucent = false;
    };
    std::vector<std::pair<std::string, int>> frames;
    std::vector<Picture> frame_pictures;
    bool frames_loop = true;
    std::size_t frames_loop_from = 0;
    // 0, or the sky layer it is (1 is painted first), drawn around the camera behind everything
    int sky = 0;
};

struct Model {
    std::string name;
    std::vector<Joint> joints;
    std::vector<Mesh> meshes;
    float scale = 1.0F;
    int cull = 0;                       // bones.txt's `cull`: 0 the file's own, 1 back faces culled, 2 both sides drawn
    std::vector<Mat> node_rest_inverse; // the game's model, per node
    std::vector<int> node_parent;
    std::vector<std::string> node_names;
    int draws = 0;     // for the session log's diagnosis
    bool room = false; // a room of a world its own mod replaces: drawn with HD graphics off too
    // its own animation (a room's): the file's nodes and animations, and its clock in game frames,
    // counted from its first draw while the game is not paused
    std::vector<khdays::assets::ImportedNode> nodes;
    std::vector<khdays::assets::ImportedAnimation> animations;
    bool own_motion = false;
    bool clock_started = false;
    std::uint64_t frames = 0;
    u32 last_vblank = 0;
};

int trace = -1;
int draws; // for KHDAYS_TRACE_MODELS=2

std::filesystem::path u8(const std::string &path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

bool tracing()
{
    if (trace < 0) {
        const char *v = std::getenv("KHDAYS_TRACE_MODELS");
        trace = v != nullptr && *v != '\0' && *v != '0' ? (std::atoi(v) > 1 ? 2 : 1) : 0;
    }
    return trace != 0;
}

// The pairs, scale and faces of `<model>.bones.txt` beside the model's file
// (.gltf, .glb or .fbx: its name without its extension).
void read_bones(const std::string &model_file, std::map<std::string, std::string> &pairs, float &scale, int &cull)
{
    const auto dot = model_file.find_last_of('.');
    const std::string path = (dot != std::string::npos ? model_file.substr(0, dot) : model_file) + ".bones.txt";
    std::ifstream in(u8(path));
    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        std::istringstream words(line);
        std::string a, b;
        if (!(words >> a >> b))
            continue;
        if (a == "scale") {
            scale = std::strtof(b.c_str(), nullptr);
            if (!(scale > 0.0F))
                scale = 1.0F;
        } else if (a == "cull") {
            if (b == "back") {
                cull = 1;
            } else if (b == "none") {
                cull = 2;
            } else {
                std::fprintf(stderr, "models: %s: cull %s is neither back nor none; the line is ignored\n",
                             path.c_str(), b.c_str());
            }
        } else {
            pairs[a] = b;
        }
    }
}

std::unique_ptr<Model> load(const std::string &name, const std::string &path, const char *file, u32 offset)
{
    namespace assets = khdays::assets;
    auto model = std::make_unique<Model>();
    model->name = name;

    // the game's model: its nodes' names, parents and rest matrices
    std::vector<unsigned char> bmd0;
    std::size_t index = 0;
    assets::NeutralModel game;
    try {
        if (!khdays_names_model_bmd0(file, offset, bmd0, index))
            throw std::runtime_error("not in its file");
        game = assets::decode_model_geometry(bmd0.data(), bmd0.size(), index);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "models: %s: the game's model cannot be read (%s): the game's is drawn\n", name.c_str(),
                     e.what());
        return nullptr;
    }
    const std::vector<Mat> rest = game.skinning != nullptr
                                      ? assets::compute_bone_world_matrices(*game.skinning, game.object_matrices)
                                      : game.object_matrices;
    for (const auto &w : rest)
        model->node_rest_inverse.push_back(inverse(w));
    model->node_parent = game.object_parents;
    model->node_parent.resize(rest.size(), -1);
    model->node_names = game.object_names;
    model->node_names.resize(rest.size());

    // the mod's model: glTF (.gltf, .glb) or FBX, the same neutral form
    assets::ImportedModel mod;
    try {
        const auto extension = u8(path).extension();
        mod = extension == ".fbx" || extension == ".FBX" ? assets::import_fbx(u8(path)) : assets::import_gltf(u8(path));
    } catch (const std::exception &e) {
        std::fprintf(stderr, "models: %s cannot be read (%s): the game's is drawn\n", path.c_str(), e.what());
        khdays_log("models: %s cannot be read (%s): the game's is drawn", path.c_str(), e.what());
        return nullptr;
    }
    for (const auto &[texture, why] : mod.missing_textures) {
        std::fprintf(stderr, "models: %s: texture %s not found or not readable (%s); drawn white\n", path.c_str(),
                     texture.c_str(), why.c_str());
        khdays_log("models: %s: texture %s not found or not readable (%s); drawn white", path.c_str(), texture.c_str(),
                   why.c_str());
    }
    std::map<std::string, std::string> pairs;
    read_bones(path, pairs, model->scale, model->cull);
    const float k = model->scale;

    // the joints, parents first; each to the node it follows
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
    std::map<std::uint32_t, int> joint_of_palette;
    for (const int j : order) {
        const auto &in = mod.joints[static_cast<std::size_t>(j)];
        Joint out;
        out.name = in.name;
        out.parent = in.parent >= 0 ? placed[static_cast<std::size_t>(in.parent)] : -1;
        const auto pair = pairs.find(in.name);
        const std::string &wanted = pair != pairs.end() ? pair->second : in.name;
        for (std::size_t g = 0; g < game.object_names.size(); ++g) {
            if (game.object_names[g] == wanted)
                out.node = static_cast<int>(g);
        }
        if (pair != pairs.end() && out.node < 0) {
            std::fprintf(stderr, "models: %s: no node %s for joint %s\n", name.c_str(), wanted.c_str(),
                         in.name.c_str());
        }
        Mat world, ibm;
        std::copy(in.world.begin(), in.world.end(), world.begin());
        std::copy(in.inverse_bind.begin(), in.inverse_bind.end(), ibm.begin());
        Mat s = kIdentity;
        s[0] = s[5] = s[10] = k;
        out.bind = mul(s, mul(world, ibm));
        out.room_node = in.node;
        out.inverse_bind = ibm;
        const auto h = translation(world);
        out.head = {h[0] * k, h[1] * k, h[2] * k};
        joint_of_palette[in.palette_index] = static_cast<int>(model->joints.size());
        model->joints.push_back(std::move(out));
    }

    // the rest pose correction, from the roots down
    auto descends = [&](int j, int from) {
        for (int up = j; up >= 0; up = model->joints[static_cast<std::size_t>(up)].parent) {
            if (up == from)
                return true;
        }
        return false;
    };
    for (std::size_t j = 0; j < model->joints.size(); ++j) {
        Joint &joint = model->joints[j];
        joint.correction =
            joint.parent >= 0 ? model->joints[static_cast<std::size_t>(joint.parent)].correction : kIdentity;
        if (joint.node < 0)
            continue;
        const auto hg = translation(rest[static_cast<std::size_t>(joint.node)]);
        int child = -1, children = 0;
        for (std::size_t c = 0; c < model->joints.size(); ++c) {
            const int cn = model->joints[c].node;
            if (cn >= 0 && model->node_parent[static_cast<std::size_t>(cn)] == joint.node && c != j &&
                descends(static_cast<int>(c), static_cast<int>(j))) {
                child = static_cast<int>(c);
                ++children;
            }
        }
        if (children == 1) {
            const Joint &to = model->joints[static_cast<std::size_t>(child)];
            const auto hc = translation(rest[static_cast<std::size_t>(to.node)]);
            const auto ds = vec(joint.correction,
                                {to.head[0] - joint.head[0], to.head[1] - joint.head[1], to.head[2] - joint.head[2]});
            const std::array<float, 3> dg{hc[0] - hg[0], hc[1] - hg[1], hc[2] - hg[2]};
            if (length(ds) > 1e-5F && length(dg) > 1e-5F) {
                float degrees = 0.0F;
                const Mat r = arc(ds, dg, degrees);
                joint.degrees = degrees;
                if (degrees > kPoseDegrees) {
                    joint.correction = mul(r, joint.correction);
                }
                if (tracing()) {
                    std::fprintf(stderr, "models: %s: %s follows %s, %.1f degrees from its pose%s\n", name.c_str(),
                                 joint.name.c_str(), game.object_names[static_cast<std::size_t>(joint.node)].c_str(),
                                 degrees, degrees > kPoseDegrees ? ", turned" : "");
                }
            }
        } else if (tracing()) {
            std::fprintf(stderr, "models: %s: %s follows %s\n", name.c_str(), joint.name.c_str(),
                         game.object_names[static_cast<std::size_t>(joint.node)].c_str());
        }
        bool identity = true;
        for (int i = 0; i < 16; ++i)
            identity = identity && std::fabs(joint.correction[i] - kIdentity[i]) < 1e-6F;
        joint.exact = identity && std::fabs(joint.head[0] - hg[0]) < 1e-4F &&
                      std::fabs(joint.head[1] - hg[1]) < 1e-4F && std::fabs(joint.head[2] - hg[2]) < 1e-4F;
    }

    // the meshes and their textures, each texture as its material takes its alpha (glTF alphaMode): kept;
    // all opaque for an opaque material; for a mask each texel drawn or not by the cutoff, opaque (a whole
    // body with soft cut-out edges came out seen through itself); where the file does not say (FBX),
    // translucent when any texel or vertex colour is part way (a room's shading in vertex colours black at
    // alpha 0.05 came out as a black floor)
    using AlphaMode = assets::NeutralMesh::AlphaMode;
    std::map<std::string, const assets::DecodedTexture *> texture_of;
    for (const auto &[key, texture] : mod.textures)
        texture_of[key] = &texture;
    struct Made {
        int replacement = -1, w = 0, h = 0;
        bool translucent = false;
    };
    std::map<std::string, Made> made;
    auto texture_for = [&](const std::string &key, AlphaMode mode, float cutoff) -> const Made * {
        const auto t = texture_of.find(key);
        if (t == texture_of.end())
            return nullptr;
        const std::string tag = mode == AlphaMode::opaque ? "@opaque"
                                : mode == AlphaMode::mask ? "@mask" + std::to_string(cutoff)
                                                          : "";
        if (const auto m = made.find(key + tag); m != made.end())
            return &m->second;
        const auto &texture = *t->second;
        // kept for as long as the port runs: the renderer reads it from any thread
        const std::size_t bytes = texture.rgba.size();
        auto *pixels = static_cast<std::uint8_t *>(std::malloc(bytes));
        if (pixels == nullptr)
            return nullptr;
        std::memcpy(pixels, texture.rgba.data(), bytes);
        bool partial = false;
        const auto least = static_cast<std::uint8_t>(std::fmin(255.0F, std::fmax(0.0F, std::ceil(cutoff * 255.0F))));
        for (std::size_t i = 3; i < bytes; i += 4) {
            if (mode == AlphaMode::opaque)
                pixels[i] = 255;
            if (mode == AlphaMode::mask)
                pixels[i] = pixels[i] >= least ? 255 : 0;
            partial = partial || (pixels[i] != 0 && pixels[i] != 255);
        }
        Made m;
        m.replacement = khdays_textures_add((name + "/" + key + tag).c_str(), texture.width, texture.height, pixels);
        m.w = texture.width;
        m.h = texture.height;
        m.translucent = mode == AlphaMode::blend || (mode == AlphaMode::unknown && partial);
        return &(made[key + tag] = m);
    };
    std::size_t vertices = 0, triangles = 0;
    for (auto &in : mod.model.meshes) {
        Mesh out;
        out.indices = std::move(in.indices);
        out.vertices = std::move(in.vertices);
        for (auto &v : out.vertices) {
            for (auto &jt : v.joints) {
                const auto it = joint_of_palette.find(jt);
                jt = it != joint_of_palette.end() ? static_cast<std::uint32_t>(it->second) : 0xffffffffU;
            }
        }
        out.double_sided = model->cull == 1 ? false : model->cull == 2 ? true : in.double_sided;
        if (const Made *t = texture_for(in.texture_name, in.alpha_mode, in.alpha_cutoff)) {
            out.replacement = t->replacement;
            out.tex_w = static_cast<float>(t->w);
            out.tex_h = static_cast<float>(t->h);
            out.translucent = t->translucent;
        }
        bool partial_vertex = false;
        for (const auto &v : out.vertices)
            partial_vertex = partial_vertex || (v.color[3] >= 8 && v.color[3] < 248);
        if (in.alpha_mode == AlphaMode::blend || in.alpha_mode == AlphaMode::unknown)
            out.translucent = out.translucent || in.alpha_mode == AlphaMode::blend || partial_vertex;
        // (a file that does not say, with its vertex alphas all 0 or all 255: painted without alpha, not cut)
        out.vertex_alpha = in.alpha_mode == AlphaMode::opaque                       ? VertexAlpha::ignored
                           : in.alpha_mode == AlphaMode::unknown && !partial_vertex ? VertexAlpha::ignored
                           : out.translucent                                        ? VertexAlpha::blended
                                                                                    : VertexAlpha::cut;
        out.cutoff = in.alpha_mode == AlphaMode::mask ? in.alpha_cutoff : 0.5F;
        out.scroll = in.scroll;
        out.sky = in.sky ? 1 : 0;
        for (const auto &[picture, length] : in.texture_frames) {
            Mesh::Picture shown;
            if (const Made *t = texture_for(picture, in.alpha_mode, in.alpha_cutoff)) {
                shown = {t->replacement, static_cast<float>(t->w), static_cast<float>(t->h), t->translucent};
            }
            out.frames.emplace_back(picture, length);
            out.frame_pictures.push_back(shown);
        }
        out.frames_loop = in.texture_frames_loop;
        out.frames_loop_from = in.texture_frames_loop_from;
        model->own_motion = model->own_motion || in.scroll[0] != 0.0F || in.scroll[1] != 0.0F || !out.frames.empty();
        vertices += out.vertices.size();
        triangles += out.indices.size() / 3;
        model->meshes.push_back(std::move(out));
    }
    // the sky layers in KH2's painting order: opaque and cut-out meshes first, then blended ones
    int sky_layer = 0;
    for (const bool blended : {false, true})
        for (Mesh &mesh : model->meshes)
            if (mesh.sky != 0 && mesh.translucent == blended)
                mesh.sky = ++sky_layer;
    // its own animation: the nodes its skinned meshes' joints are (with their parents) move; one that moves
    // another node would move a mesh made still on loading, which is not played
    if (!mod.animations.empty()) {
        std::vector<char> moving(mod.nodes.size(), 0);
        for (const Joint &joint : model->joints) {
            for (int up = joint.node < 0 ? joint.room_node : -1;
                 up >= 0 && static_cast<std::size_t>(up) < mod.nodes.size() && !moving[static_cast<std::size_t>(up)];
                 up = mod.nodes[static_cast<std::size_t>(up)].parent)
                moving[static_cast<std::size_t>(up)] = 1;
        }
        for (const auto &a : mod.animations) {
            for (const auto &track : a.tracks) {
                if (track.node < 0 || static_cast<std::size_t>(track.node) >= moving.size() ||
                    !moving[static_cast<std::size_t>(track.node)]) {
                    khdays_log("models: %s: animation %s moves a node no skinned mesh follows: not played",
                               name.c_str(), a.name.c_str());
                    break;
                }
            }
        }
        model->nodes = std::move(mod.nodes);
        model->animations = std::move(mod.animations);
        model->own_motion = true;
    }
    if (model->own_motion) {
        std::size_t scrolling = 0, framed = 0, sky = 0;
        for (const Mesh &mesh : model->meshes) {
            scrolling += mesh.scroll[0] != 0.0F || mesh.scroll[1] != 0.0F ? 1 : 0;
            framed += mesh.frames.empty() ? 0 : 1;
            sky += mesh.sky != 0 ? 1 : 0;
        }
        khdays_log("models: %s: its own animation: %zu moving parts' animations, %zu scrolling and %zu changing "
                   "textures, %zu sky meshes",
                   name.c_str(), model->animations.size(), scrolling, framed, sky);
    }
    std::size_t translucent = 0;
    for (const Mesh &mesh : model->meshes)
        translucent += mesh.translucent ? 1 : 0;
    std::fprintf(
        stderr,
        "models: %s drawn instead from %s (%zu meshes, %zu of them translucent, %zu vertices, %zu triangles, %zu joints%s)\n",
        name.c_str(), path.c_str(), model->meshes.size(), translucent, vertices, triangles, model->joints.size(),
        model->cull == 1   ? ", back faces culled"
        : model->cull == 2 ? ", both sides drawn"
                           : "");
    // the session log's diagnosis of a model that comes out wrong (KHDAYS_TRACE_MODELS with
    // the session log on): each joint, the node it follows, its turn from that node's pose,
    // and each node's place at rest
    if (tracing() && khdays_log_on()) {
        khdays_log("models: %s drawn instead from %s (%zu joints, %zu game nodes, scale %.3f)", name.c_str(),
                   path.c_str(), model->joints.size(), rest.size(), static_cast<double>(k));
        for (const Joint &joint : model->joints) {
            khdays_log(
                "models:   joint %s -> %s%s, %.0f degrees, head %.2f %.2f %.2f", joint.name.c_str(),
                joint.node >= 0 ? model->node_names[static_cast<std::size_t>(joint.node)].c_str() : "(its parent)",
                joint.exact ? " exactly" : "", static_cast<double>(joint.degrees), static_cast<double>(joint.head[0]),
                static_cast<double>(joint.head[1]), static_cast<double>(joint.head[2]));
        }
        for (std::size_t g = 0; g < rest.size(); ++g) {
            const Mat &w = rest[g];
            khdays_log("models:   node %zu %s (parent %d) at rest %.2f %.2f %.2f, scale %.3f %.3f %.3f", g,
                       model->node_names[g].c_str(), model->node_parent[g], static_cast<double>(w[12]),
                       static_cast<double>(w[13]), static_cast<double>(w[14]),
                       static_cast<double>(length({w[0], w[1], w[2]})), static_cast<double>(length({w[4], w[5], w[6]})),
                       static_cast<double>(length({w[8], w[9], w[10]})));
        }
    }
    return model;
}

// --- The draws -------------------------------------------------------------------------

std::unordered_map<std::string, std::unique_ptr<Model>> models; // by "<file>@<offset>"; null: not replaced
Model *active;
int depth;
Model *outer[8]; // the draws a draw's callbacks start: theirs are the game's
int shapes;
KhdaysG3dMaterial material;
std::vector<Mat> node_matrix;
std::vector<char> node_sent;

std::vector<Mat> d, k, skin;
std::vector<KhdaysG3dClipVertex> clip;

// A room's joint that its own animation moves: its node's matrix now (node_worlds), else nullptr.
const Mat *moved_world(const Joint &joint, const std::vector<Mat> &moved)
{
    if (joint.node >= 0 || joint.room_node < 0 || static_cast<std::size_t>(joint.room_node) >= moved.size())
        return nullptr;
    return &moved[static_cast<std::size_t>(joint.room_node)];
}

// The picture a mesh shows after `frames` game frames: its own, or the one its changing texture is on.
Mesh::Picture picture_now(const Mesh &mesh, std::uint64_t frames)
{
    Mesh::Picture now{mesh.replacement, mesh.tex_w, mesh.tex_h, mesh.translucent};
    if (!mesh.frames.empty()) {
        const Mesh::Picture &p =
            mesh.frame_pictures[khdays::room::frame_at(mesh.frames, mesh.frames_loop, mesh.frames_loop_from, frames)];
        if (p.replacement >= 0)
            now = {p.replacement, p.w, p.h, mesh.translucent || p.translucent};
    }
    return now;
}

// Triangle `t`'s polygon attributes, from the mesh's `attr` and its corners' vertex alpha; false when
// the triangle is not drawn (under a mask's cutoff, or blended down to nothing).
bool triangle_attr(const Mesh &mesh, std::size_t t, uint32_t attr, uint32_t &out)
{
    out = attr;
    if (mesh.vertex_alpha == VertexAlpha::ignored)
        return true;
    const float alpha = (static_cast<float>(mesh.vertices[mesh.indices[t]].color[3]) +
                         static_cast<float>(mesh.vertices[mesh.indices[t + 1]].color[3]) +
                         static_cast<float>(mesh.vertices[mesh.indices[t + 2]].color[3])) /
                        (3.0F * 255.0F);
    if (mesh.vertex_alpha == VertexAlpha::cut)
        return alpha >= mesh.cutoff;
    // blended: the vertex alphas are blended across the polygon; a fully clear one is skipped
    return alpha > 0.0F;
}

void draw(Model &m)
{
    float pos[16], vecm[16], proj_f[16];
    khdays_g3d_matrices(pos, vecm, proj_f);
    Mat proj;
    std::copy(proj_f, proj_f + 16, proj.begin());
    const std::size_t nodes = m.node_rest_inverse.size();

    // D per node; one the draw did not send takes its parent's (the first sent for a root)
    d.assign(nodes, kIdentity);
    int first = -1;
    for (std::size_t g = 0; g < nodes; ++g) {
        if (node_sent[g] && first < 0)
            first = static_cast<int>(g);
    }
    if (first < 0)
        return;
    std::vector<char> done(nodes, 0);
    for (std::size_t pass = 0; pass <= nodes; ++pass) {
        for (std::size_t g = 0; g < nodes; ++g) {
            if (done[g])
                continue;
            const int p = m.node_parent[g];
            if (node_sent[g]) {
                d[g] = mul(node_matrix[g], m.node_rest_inverse[g]);
            } else if (p >= 0 && static_cast<std::size_t>(p) < nodes && p != static_cast<int>(g)) {
                if (!done[static_cast<std::size_t>(p)])
                    continue;
                d[g] = d[static_cast<std::size_t>(p)];
            } else {
                d[g] = mul(node_matrix[static_cast<std::size_t>(first)],
                           m.node_rest_inverse[static_cast<std::size_t>(first)]);
            }
            done[g] = 1;
        }
    }
    const Mat &root = d[static_cast<std::size_t>(first)];
    // the session log's diagnosis: how many of its nodes a draw sends (the first draws, by
    // frame: several in one frame are draws of parts), and at two moments each node's
    // matrix the game sent, as the drawn picture's (the camera's) and its turn and scale
    // from rest (D)
    ++m.draws;
    if (trace != 0 && khdays_log_on() && (m.draws <= 6 || m.draws == 120 || m.draws == 600)) {
        int sent = 0;
        for (std::size_t g = 0; g < nodes; ++g)
            sent += node_sent[g] ? 1 : 0;
        khdays_log("models: %s draw %d: %d of %zu nodes sent, the first %d; the game's material: POLYGON_ATTR %08x",
                   m.name.c_str(), m.draws, sent, nodes, first, material.attr);
        for (std::size_t g = 0; (m.draws == 120 || m.draws == 600) && g < nodes; ++g) {
            const Mat &M = node_matrix[g];
            const Mat &D = d[g];
            khdays_log(
                "models:   node %zu %s%s: sent at %.2f %.2f %.2f scale %.3f %.3f %.3f; D scale %.3f %.3f %.3f at %.2f %.2f %.2f",
                g, m.node_names[g].c_str(), node_sent[g] ? "" : " (not sent)", static_cast<double>(M[12]),
                static_cast<double>(M[13]), static_cast<double>(M[14]), static_cast<double>(length({M[0], M[1], M[2]})),
                static_cast<double>(length({M[4], M[5], M[6]})), static_cast<double>(length({M[8], M[9], M[10]})),
                static_cast<double>(length({D[0], D[1], D[2]})), static_cast<double>(length({D[4], D[5], D[6]})),
                static_cast<double>(length({D[8], D[9], D[10]})), static_cast<double>(D[12]),
                static_cast<double>(D[13]), static_cast<double>(D[14]));
        }
    }
    if (trace == 2 && draws++ % 300 == 0) {
        // each node's turn from its rest, after the model's first node's (the camera's and placing's)
        Mat r = root;
        r[12] = r[13] = r[14] = 0.0F;
        const Mat ri = inverse(r);
        std::fprintf(stderr, "models: %s, draw %d:", m.name.c_str(), draws - 1);
        for (std::size_t g = 0; g < nodes; ++g) {
            Mat l = d[g];
            l[12] = l[13] = l[14] = 0.0F;
            const Mat q = mul(ri, l);
            const float tr = q[0] / length({q[0], q[1], q[2]}) + q[5] / length({q[4], q[5], q[6]}) +
                             q[10] / length({q[8], q[9], q[10]});
            std::fprintf(stderr, " %zu:%.0f", g,
                         std::acos(std::fmax(-1.0F, std::fmin(1.0F, (tr - 1.0F) / 2.0F))) * 57.2957795F);
        }
        std::fprintf(stderr, "\n");
    }

    // its own animation's clock: the game's frames since its first draw, standing while the game is paused
    std::vector<Mat> moved;
    if (m.own_motion) {
        const u32 now = khdays_events_vblank_count();
        if (m.clock_started && !khdays_link_paused())
            m.frames += now - m.last_vblank;
        m.clock_started = true;
        m.last_vblank = now;
        if (!m.animations.empty())
            moved = khdays::room::node_worlds(m.nodes, m.animations, static_cast<double>(m.frames) / 60.0);
    }
    Mat scale = kIdentity;
    scale[0] = scale[5] = scale[10] = m.scale;
    const Mat scaled_root = mul(root, scale);

    // each joint, parents first
    k.resize(m.joints.size());
    skin.resize(m.joints.size());
    for (std::size_t j = 0; j < m.joints.size(); ++j) {
        const Joint &joint = m.joints[j];
        const Mat *parent = joint.parent >= 0 ? &k[static_cast<std::size_t>(joint.parent)] : nullptr;
        if (joint.node >= 0 && joint.exact) {
            k[j] = d[static_cast<std::size_t>(joint.node)];
        } else if (joint.node >= 0) {
            const Mat &dg = d[static_cast<std::size_t>(joint.node)];
            k[j] = place(point(parent != nullptr ? *parent : dg, joint.head), dg, joint.correction, joint.head);
        } else {
            k[j] = parent != nullptr ? *parent : root;
        }
        const Mat *w = moved_world(joint, moved);
        skin[j] = w != nullptr ? mul(scaled_root, mul(*w, joint.inverse_bind)) : mul(k[j], joint.bind);
    }
    // the session log's diagnosis, whole: each node's matrix sent and each joint's K, to
    // pose the mod's model again offline (columns 0-3 of the 3x4)
    if (trace != 0 && khdays_log_on() && (m.draws == 120 || m.draws == 600)) {
        auto line = [](const Mat &a) {
            char text[400];
            std::snprintf(text, sizeof(text), "%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f",
                          static_cast<double>(a[0]), static_cast<double>(a[1]), static_cast<double>(a[2]),
                          static_cast<double>(a[4]), static_cast<double>(a[5]), static_cast<double>(a[6]),
                          static_cast<double>(a[8]), static_cast<double>(a[9]), static_cast<double>(a[10]),
                          static_cast<double>(a[12]), static_cast<double>(a[13]), static_cast<double>(a[14]));
            return std::string(text);
        };
        for (std::size_t g = 0; g < nodes; ++g) {
            khdays_log("models:   M %zu %s", g, line(node_matrix[g]).c_str());
        }
        for (std::size_t j = 0; j < m.joints.size(); ++j) {
            khdays_log("models:   K %zu %s %s", j, m.joints[j].name.c_str(), line(k[j]).c_str());
        }
        // a room's own moving parts: where each moved joint is in the file's space at this frame
        for (const Joint &joint : m.joints) {
            if (const Mat *w = moved_world(joint, moved)) {
                khdays_log("models:   moved %s at %.1f %.1f %.1f, frame %llu", joint.name.c_str(),
                           static_cast<double>((*w)[12]), static_cast<double>((*w)[13]), static_cast<double>((*w)[14]),
                           static_cast<unsigned long long>(m.frames));
            }
        }
    }
    const Mat unskinned = scaled_root; // a mesh with no skin: placed with the model's first node

    // the polygons' attributes: the game's material's, the faces the mesh's. A mesh drawn whole (OPAQUE or
    // MASK) is drawn opaque when the game's material is all but opaque: the rooms are drawn at alpha 30 of
    // 31, and a translucent pixel takes no other translucent polygon of the same ID (the DS's rule), so a
    // mod's room, one ID, came out as whatever it drew first (its sky over the rest). A material fading
    // further keeps its alpha.
    const uint32_t tex = (7u << 26) | (7u << 23) | (7u << 20) | (1u << 17) | (1u << 16); // 1024x1024, repeated
    const uint32_t game_alpha = (material.attr >> 16) & 31u;
    for (const Mesh &mesh : m.meshes) {
        // its picture now, and how far its texture has scrolled
        const Mesh::Picture picture = picture_now(mesh, m.frames);
        const float tex_w = picture.w, tex_h = picture.h;
        const auto offset = khdays::room::scroll_offset(mesh.scroll, m.frames, tex_w, tex_h);
        uint32_t attr = (material.attr & ~0xc0u) | (mesh.double_sided ? 0xc0u : 0x80u);
        if (!picture.translucent && game_alpha >= 30u)
            attr = (attr & ~(31u << 16)) | (31u << 16);
        clip.resize(mesh.vertices.size());
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
            const auto &v = mesh.vertices[i];
            std::array<float, 3> p{0, 0, 0}, nrm{0, 0, 0};
            float total = 0.0F;
            for (int w = 0; w < 4; ++w) {
                const float weight = v.weights[static_cast<std::size_t>(w)];
                if (weight <= 0.0F)
                    continue;
                const std::uint32_t jt = v.joints[static_cast<std::size_t>(w)];
                const Mat &s = jt < skin.size() ? skin[jt] : unskinned;
                // a sky stays around the camera: turned with the room, not moved
                const auto q = mesh.sky != 0 ? vec(s, v.position) : point(s, v.position);
                const auto n = vec(s, v.normal);
                for (int c = 0; c < 3; ++c) {
                    p[static_cast<std::size_t>(c)] += weight * q[static_cast<std::size_t>(c)];
                    nrm[static_cast<std::size_t>(c)] += weight * n[static_cast<std::size_t>(c)];
                }
                total += weight;
            }
            if (total <= 0.0F) {
                p = mesh.sky != 0 ? vec(unskinned, v.position) : point(unskinned, v.position);
                nrm = vec(unskinned, v.normal);
            } else if (std::fabs(total - 1.0F) > 1e-4F) {
                for (int c = 0; c < 3; ++c)
                    p[static_cast<std::size_t>(c)] /= total;
            }
            KhdaysG3dClipVertex &o = clip[i];
            o.x = proj[0] * p[0] + proj[4] * p[1] + proj[8] * p[2] + proj[12];
            o.y = proj[1] * p[0] + proj[5] * p[1] + proj[9] * p[2] + proj[13];
            o.z = proj[2] * p[0] + proj[6] * p[1] + proj[10] * p[2] + proj[14];
            o.w = proj[3] * p[0] + proj[7] * p[1] + proj[11] * p[2] + proj[15];
            const float ln = length(nrm);
            float rgb[3];
            if (ln > 1e-6F) {
                const float unit[3] = {nrm[0] / ln, nrm[1] / ln, nrm[2] / ln};
                khdays_g3d_light(&material, unit, rgb);
            } else {
                const float up[3] = {0.0F, 0.0F, 0.0F};
                khdays_g3d_light(&material, up, rgb);
            }
            o.r = rgb[0] * static_cast<float>(v.color[0]) / 255.0F;
            o.g = rgb[1] * static_cast<float>(v.color[1]) / 255.0F;
            o.b = rgb[2] * static_cast<float>(v.color[2]) / 255.0F;
            o.s = (v.texcoord[0] + offset[0]) / tex_w * 1024.0F * 16.0F;
            o.t = (v.texcoord[1] + offset[1]) / tex_h * 1024.0F * 16.0F;
            o.a = mesh.vertex_alpha == VertexAlpha::blended ? static_cast<float>(v.color[3]) / 255.0F : 1.0F;
        }
        // the session log's diagnosis: where the mesh falls on the screen (x/w and y/w: -1..1
        // the view), and how near (w)
        if (trace != 0 && khdays_log_on() && (m.draws == 120 || m.draws == 600) && !clip.empty()) {
            float x0 = 1e30F, x1 = -1e30F, y0 = 1e30F, y1 = -1e30F, w0 = 1e30F, w1 = -1e30F;
            int behind = 0;
            for (const auto &o : clip) {
                if (o.w <= 0.0F) {
                    ++behind;
                    continue;
                }
                x0 = std::fmin(x0, o.x / o.w), x1 = std::fmax(x1, o.x / o.w);
                y0 = std::fmin(y0, o.y / o.w), y1 = std::fmax(y1, o.y / o.w);
                w0 = std::fmin(w0, o.w), w1 = std::fmax(w1, o.w);
            }
            khdays_log(
                "models:   mesh of %zu vertices: x/w %.2f..%.2f, y/w %.2f..%.2f, w %.3f..%.3f, %d behind; proj %s",
                clip.size(), static_cast<double>(x0), static_cast<double>(x1), static_cast<double>(y0),
                static_cast<double>(y1), static_cast<double>(w0), static_cast<double>(w1), behind,
                [&] {
                    std::string t;
                    char one[24];
                    for (int i = 0; i < 16; ++i) {
                        std::snprintf(one, sizeof(one), "%s%.4f", i ? " " : "",
                                      static_cast<double>(proj[static_cast<std::size_t>(i)]));
                        t += one;
                    }
                    return t;
                }()
                    .c_str());
        }
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const KhdaysG3dClipVertex tri[3] = {clip[mesh.indices[t]], clip[mesh.indices[t + 1]],
                                                clip[mesh.indices[t + 2]]};
            uint32_t tri_attr;
            if (triangle_attr(mesh, t, attr, tri_attr))
                khdays_g3d_mod_polygon(tri, 3, tri_attr, tex, picture.replacement,
                                       picture.translucent || mesh.vertex_alpha == VertexAlpha::blended, mesh.sky);
        }
    }
}

// Whether `path` (a mod's model replacing `name`, `mi/wd/wd_<code>#<member>/mdl/<model>`)
// is in the mod that also replaces the world's archive (`files/mi/wd/wd_<code>`).
bool room_of_replaced_world(const std::string &name, const char *path)
{
    const auto hash = name.find('#');
    if (name.rfind("mi/wd/wd_", 0) != 0 || hash == std::string::npos)
        return false;
    char archive[260];
    if (!khdays_mods_find(("files/" + name.substr(0, hash)).c_str(), archive, sizeof(archive)))
        return false;
    // mods/<Mod>/files/mi/wd/wd_<code>: the mod is four folders up
    const std::filesystem::path mod = u8(archive).parent_path().parent_path().parent_path().parent_path();
    std::error_code ec;
    return std::filesystem::equivalent(mod / "graphics" / (u8(name).string() + u8(path).extension().string()), u8(path),
                                       ec);
}

} // namespace

extern "C" void khdays_walk_model_draw(unsigned model_resource); /* runtime/input/walk.h */
extern "C" void khdays_walk_model_draw_end(void);

extern "C" void khdays_models_draw_begin(const void *resource)
{
    if (depth == 0) {
        khdays_g3d_draw_begin(resource); /* the polygons' key */
        if (resource != nullptr) {
            khdays_walk_model_draw(KHDAYS_REF(resource)); /* the Walk mod: the player's weapon to the hand */
        }
    }
    if (depth > 0) {
        if (depth < 8)
            outer[depth] = active;
        ++depth;
        active = nullptr;
        return;
    }
    ++depth;
    const bool hd = khdays_textures_hd() != 0;
    if (resource == nullptr) {
        return;
    }
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    if (!khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run)) { // its DS address
        return;
    }
    if (!hd && std::strstr(file, "mi/wd/wd_") == nullptr) { // HD off: only a mission world's rooms
        return;
    }
    char key[160];
    std::snprintf(key, sizeof(key), "%s@%x", file, offset);
    auto it = models.find(key);
    if (it == models.end()) {
        char name[160], relative[200], path[260];
        std::unique_ptr<Model> loaded;
        if (khdays_names_model(file, offset, name, sizeof(name))) {
            /* glTF first, then binary glTF and FBX */
            for (const char *extension : {".gltf", ".glb", ".fbx"}) {
                std::snprintf(relative, sizeof(relative), "graphics/%s%s", name, extension);
                if (khdays_mods_find(relative, path, sizeof(path))) {
                    loaded = load(name, path, file, offset);
                    if (loaded && room_of_replaced_world(name, path)) {
                        loaded->room = true;
                        khdays_log(
                            "models: %s drawn from %s, its world replaced by the same mod (with HD graphics on or off)",
                            name, path);
                    }
                    break;
                }
            }
            if (tracing()) {
                std::fprintf(stderr, "models: %s drawn%s\n", name, loaded ? ", replaced" : "");
            }
        }
        it = models.emplace(key, std::move(loaded)).first;
    }
    active = it->second.get();
    if (active != nullptr && !hd && !active->room) {
        active = nullptr;
    }
    if (active != nullptr) {
        shapes = 0;
        node_matrix.assign(active->node_rest_inverse.size(), kIdentity);
        node_sent.assign(active->node_rest_inverse.size(), 0);
    }
}

extern "C" void khdays_models_node(unsigned node, int sent)
{
    if (active == nullptr || !sent || node >= node_matrix.size()) {
        return;
    }
    float pos[16], vecm[16], proj[16];
    khdays_g3d_matrices(pos, vecm, proj);
    std::copy(pos, pos + 16, node_matrix[node].begin());
    node_sent[node] = 1;
}

extern "C" int khdays_models_shape(const void *list, unsigned int size)
{
    if (active == nullptr) {
        return 0;
    }
    if (shapes++ == 0) {
        khdays_g3d_material(&material);
    }
    // the list's matrix commands run, its drawing left out: packed words of up
    // to four command numbers, each command's parameters after them in order
    const auto *words = static_cast<const uint32_t *>(list);
    const unsigned int count = list != nullptr ? size / 4U : 0U;
    for (unsigned int i = 0; i < count;) {
        const uint32_t packed = words[i++];
        for (int b = 0; b < 4; ++b) {
            const unsigned command = (packed >> (8 * b)) & 0xffU;
            const int n = khdays_g3d_param_count(command);
            if (n < 0)
                continue; // no command (0: a free slot of the word)
            if (i + static_cast<unsigned int>(n) > count)
                return 1;
            if (command >= 0x10U && command <= 0x1cU)
                khdays_g3d_command(command, &words[i]);
            i += static_cast<unsigned int>(n);
        }
    }
    return 1;
}

extern "C" void khdays_models_draw_end(void)
{
    if (depth <= 0) {
        return;
    }
    if (--depth != 0) {
        active = depth < 8 ? outer[depth] : nullptr;
        return;
    }
    khdays_walk_model_draw_end();
    // its polygons inside the game model's draw: keyed by the draw for the
    // pictures between lists (g3d_interp.c), not among the loose ones, whose
    // number moves with every effect's
    if (active != nullptr && shapes > 0) {
        draw(*active);
    }
    khdays_g3d_draw_end();
    active = nullptr;
}
