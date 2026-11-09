// A mod's model loaded: its file's meshes, joints and textures, fitted to the game's skeleton.

#include "model_parts.h"

namespace khdays::model_parts {

// `align`'s turn, at rest, of a part's points (each about its joint's head) onto the game's: a foot about the
// vertical by the way to its front fifth (both rests stand); a hand by the way to its far fifth (the fingers)
// and its flat (the least spread), the flat's sign nearest the model's
Mat align_part(bool foot, const std::vector<std::array<float, 3>> &own, const std::vector<std::array<float, 3>> &its,
               float &degrees)
{
    using V = std::array<float, 3>;
    degrees = 0.0F;
    if (own.size() < 8 || its.size() < 8)
        return kIdentity;
    const auto unit = [](V v) {
        const float l = length(v);
        return l > 0.0F ? V{v[0] / l, v[1] / l, v[2] / l} : v;
    };
    // the mean of the fifth of the points that rank highest by `key`
    const auto top = [](const std::vector<V> &p, const auto &key) {
        std::vector<float> keys;
        for (const V &v : p)
            keys.push_back(key(v));
        std::vector<float> sorted = keys;
        std::sort(sorted.begin(), sorted.end());
        const float cut = sorted[sorted.size() * 4 / 5];
        V sum{};
        int n = 0;
        for (std::size_t i = 0; i < p.size(); ++i)
            if (keys[i] >= cut) {
                for (std::size_t a = 0; a < 3; ++a)
                    sum[a] += p[i][a];
                ++n;
            }
        return V{sum[0] / static_cast<float>(n), sum[1] / static_cast<float>(n), sum[2] / static_cast<float>(n)};
    };
    Mat r = kIdentity;
    if (foot) {
        const auto front = [](const V &v) { return v[2]; };
        const V a = top(own, front), b = top(its, front);
        const float turn = std::atan2(b[0], b[2]) - std::atan2(a[0], a[2]);
        r[0] = r[10] = std::cos(turn);
        r[8] = std::sin(turn);
        r[2] = -std::sin(turn);
        degrees = std::fabs(turn) * 57.2957795F;
        return r;
    }
    const auto far = [](const V &v) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; };
    // the least-spread axis of the points about their mean (Jacobi on the covariance)
    const auto flat = [](const std::vector<V> &p) {
        V mean{};
        for (const V &v : p)
            for (std::size_t a = 0; a < 3; ++a)
                mean[a] += v[a] / static_cast<float>(p.size());
        double cov[3][3] = {};
        for (const V &v : p)
            for (std::size_t a = 0; a < 3; ++a)
                for (std::size_t b = 0; b < 3; ++b)
                    cov[a][b] += static_cast<double>(v[a] - mean[a]) * static_cast<double>(v[b] - mean[b]);
        double e[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int sweep = 0; sweep < 30; ++sweep)
            for (int pq = 0; pq < 3; ++pq) {
                const int P = pq == 2 ? 1 : 0, Q = pq == 0 ? 1 : 2;
                if (std::fabs(cov[P][Q]) < 1e-18)
                    continue;
                const double th = 0.5 * std::atan2(2.0 * cov[P][Q], cov[Q][Q] - cov[P][P]);
                const double c = std::cos(th), s = std::sin(th);
                for (int x = 0; x < 3; ++x) {
                    const double xp = cov[x][P], xq = cov[x][Q];
                    cov[x][P] = c * xp - s * xq;
                    cov[x][Q] = s * xp + c * xq;
                }
                for (int x = 0; x < 3; ++x) {
                    const double px = cov[P][x], qx = cov[Q][x];
                    cov[P][x] = c * px - s * qx;
                    cov[Q][x] = s * px + c * qx;
                }
                for (int x = 0; x < 3; ++x) {
                    const double xp = e[x][P], xq = e[x][Q];
                    e[x][P] = c * xp - s * xq;
                    e[x][Q] = s * xp + c * xq;
                }
            }
        int least = 0;
        for (int a = 1; a < 3; ++a)
            if (cov[a][a] < cov[least][least])
                least = a;
        return V{static_cast<float>(e[0][least]), static_cast<float>(e[1][least]), static_cast<float>(e[2][least])};
    };
    const auto frame = [&](V f, V n) { // columns: the way, the flat made square to it, their cross
        f = unit(f);
        const float along = n[0] * f[0] + n[1] * f[1] + n[2] * f[2];
        n = unit(V{n[0] - f[0] * along, n[1] - f[1] * along, n[2] - f[2] * along});
        const V c{f[1] * n[2] - f[2] * n[1], f[2] * n[0] - f[0] * n[2], f[0] * n[1] - f[1] * n[0]};
        Mat m = kIdentity;
        for (std::size_t a = 0; a < 3; ++a) {
            m[a] = f[a];
            m[4 + a] = n[a];
            m[8 + a] = c[a];
        }
        return m;
    };
    const V fa = top(own, far), fb = top(its, far);
    V na = flat(own), nb = flat(its);
    if (na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2] < 0.0F)
        nb = V{-nb[0], -nb[1], -nb[2]};
    r = mul(frame(fb, nb), inverse(frame(fa, na)));
    degrees = std::acos(std::fmax(-1.0F, std::fmin(1.0F, (r[0] + r[5] + r[10] - 1.0F) * 0.5F))) * 57.2957795F;
    return r;
}

namespace {

// What load's steps share: the game's model and the mod's as read, the mod's bones.txt, its scale, and where
// each of the file's joints went among Model::joints.
struct Loading {
    const std::string &name, &path;
    khdays::assets::NeutralModel game;
    std::vector<Mat> rest; // the game's nodes at rest, in the model's world
    khdays::assets::ImportedModel mod;
    const KhdaysCharacter *character = nullptr;
    bool own_skeleton = false; // a mod's character's own: none of its joints follows a node of the game's
    BonesFile bones;
    float k = 1.0F;                                // the model's scale
    std::vector<int> placed;                       // each file joint's index in Model::joints
    std::map<std::uint32_t, int> joint_of_palette; // a vertex's joint (its palette index) to Model::joints
};

// Whether joint `j` is `from` or under it.
bool descends(const Model &model, int j, int from)
{
    for (int up = j; up >= 0; up = model.joints[static_cast<std::size_t>(up)].parent) {
        if (up == from)
            return true;
    }
    return false;
}

// The nearest joint above `j` that follows a game node, -1 none.
int paired_above(const Model &model, int j)
{
    int a = model.joints[static_cast<std::size_t>(j)].parent;
    while (a >= 0 && model.joints[static_cast<std::size_t>(a)].node < 0)
        a = model.joints[static_cast<std::size_t>(a)].parent;
    return a;
}

// Reads the game's model: its nodes' names, parents and rest matrices (`file` null: a model drawn at a place of the
// port's, one node). False when it cannot be read.
bool read_game_model(Model *model, Loading &loading, const char *file, u32 offset)
{
    namespace assets = khdays::assets;
    const std::string &name = loading.name;
    std::vector<unsigned char> bmd0;
    std::size_t index = 0;
    assets::NeutralModel &game = loading.game;
    if (file == nullptr) {
        game.object_names = {"place"};
        game.object_matrices = {kIdentity};
        game.object_parents = {-1};
    } else {
        try {
            if (!khdays_names_model_bmd0(file, offset, bmd0, index))
                throw std::runtime_error("not in its file");
            game = assets::decode_model_geometry(bmd0.data(), bmd0.size(), index);
        } catch (const std::exception &e) {
            std::fprintf(
                stderr,
                "models: %s: the game's model cannot be read (%s): the game's is drawn; check the mod's folder name against --dump-graphics\n",
                name.c_str(), e.what());
            return false;
        }
    }
    loading.rest = game.skinning != nullptr ? assets::compute_bone_world_matrices(*game.skinning, game.object_matrices)
                                            : game.object_matrices;
    for (const auto &w : loading.rest)
        model->node_rest_inverse.push_back(inverse(w));
    model->node_parent = game.object_parents;
    model->node_parent.resize(loading.rest.size(), -1);
    model->node_names = game.object_names;
    model->node_names.resize(loading.rest.size());
    return true;
}

// Reads the mod's model: glTF (.gltf, .glb), FBX or KH2's (.mdlx), the same neutral form. False when it cannot be read.
bool read_mod_model(Loading &loading)
{
    namespace assets = khdays::assets;
    const std::string &path = loading.path;
    assets::ImportedModel &mod = loading.mod;
    try {
        const auto extension = u8(path).extension();
        if (extension == ".fbx" || extension == ".FBX")
            mod = assets::import_fbx(u8(path));
        else if (extension == ".mdlx" || extension == ".MDLX")
            mod = assets::kh2::import_mdlx(u8(path));
        else
            mod = assets::import_gltf(u8(path));
    } catch (const std::exception &e) {
        std::fprintf(stderr, "models: %s cannot be read (%s): export it again; the game's is drawn\n", path.c_str(),
                     e.what());
        khdays_log("models: %s cannot be read (%s): export it again; the game's is drawn", path.c_str(), e.what());
        return false;
    }
    for (const auto &[texture, why] : mod.missing_textures) {
        std::fprintf(stderr,
                     "models: %s: texture %s not found or not readable (%s); put it beside the model; drawn white\n",
                     path.c_str(), texture.c_str(), why.c_str());
        khdays_log("models: %s: texture %s not found or not readable (%s); put it beside the model; drawn white",
                   path.c_str(), texture.c_str(), why.c_str());
    }
    for (const std::string &note : mod.notes) {
        std::fprintf(stderr, "models: %s: %s\n", path.c_str(), note.c_str());
        khdays_log("models: %s: %s", path.c_str(), note.c_str());
    }
    return true;
}

// Reads what sizes and settles the model: a mod's character's own skeleton and scale, and its bones.txt.
void read_settings(Model *model, Loading &loading)
{
    const std::string &name = loading.name, &path = loading.path;
    loading.character = khdays_characters_of_model(name.c_str());
    loading.own_skeleton = loading.character != nullptr && loading.character->own_skeleton;
    if (!loading.own_skeleton && (u8(path).extension() == ".fbx" || u8(path).extension() == ".FBX")) {
        loading.mod.animations.clear(); /* an FBX model's takes are not its own animation */
    }
    loading.bones = read_bones(path);
    const BonesFile &bones = loading.bones;
    model->scale = bones.scale;
    model->cull = bones.cull;
    model->pose_degrees = bones.pose_degrees;
    model->held_name = bones.held;
    model->held_rest = bones.held_rest;
    model->held_own_turn = bones.held_own_turn;
    if (bones.held_own_turn)
        model->held_from_game = inverse(turn_of(bones.held_turn));
    model->cuff_settings = bones.cuff_settings;
    model->setting = setting_named(name, bones.setting);
    model->kept_lists = kept_lists(name, loading.game, bones.keep);
    // a mod's character's [model] scale over its bones file's, whatever its skeleton (as the studio shows it)
    if (loading.character != nullptr && loading.character->model_scale > 0.0F) {
        model->scale = loading.character->model_scale;
    }
    loading.k = model->scale;
}

// Adds the joints, parents first, each to the node it follows.
void add_joints(Model *model, Loading &loading)
{
    const std::string &name = loading.name;
    const khdays::assets::NeutralModel &game = loading.game;
    auto &mod = loading.mod;
    const bool own_skeleton = loading.own_skeleton;
    const BonesFile &bones = loading.bones;
    const float k = loading.k;
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
        const auto pair = bones.pairs.find(in.name);
        const std::string &wanted = pair != bones.pairs.end() ? pair->second : in.name;
        for (std::size_t g = 0; g < game.object_names.size() && !own_skeleton; ++g) {
            if (game.object_names[g] == wanted)
                out.node = static_cast<int>(g);
        }
        if (pair != bones.pairs.end() && out.node < 0) {
            std::fprintf(stderr, "models: %s: no node %s for joint %s; check the pair in bones.txt\n", name.c_str(),
                         wanted.c_str(), in.name.c_str());
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
    loading.placed = std::move(placed);
    loading.joint_of_palette = std::move(joint_of_palette);
}

// Corrects each joint's rest pose to its node's, from the roots down; `align` turns an end whole.
void correct_rest_pose(Model *model, const Loading &loading)
{
    const std::string &name = loading.name;
    const khdays::assets::NeutralModel &game = loading.game;
    const std::vector<Mat> &rest = loading.rest;
    auto &mod = loading.mod;
    const BonesFile &bones = loading.bones;
    const float k = loading.k;
    const std::map<std::uint32_t, int> &joint_of_palette = loading.joint_of_palette;
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
                descends(*model, static_cast<int>(c), static_cast<int>(j))) {
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
                if (degrees > model->pose_degrees) {
                    joint.correction = mul(r, joint.correction);
                }
                if (tracing()) {
                    std::fprintf(stderr, "models: %s: %s follows %s, %.1f degrees from its pose%s\n", name.c_str(),
                                 joint.name.c_str(), game.object_names[static_cast<std::size_t>(joint.node)].c_str(),
                                 degrees, degrees > model->pose_degrees ? ", turned" : "");
                }
            }
        } else if (tracing()) {
            std::fprintf(stderr, "models: %s: %s follows %s\n", name.c_str(), joint.name.c_str(),
                         game.object_names[static_cast<std::size_t>(joint.node)].c_str());
        }
        // `align`: an end (a hand, a foot) turned whole onto the game's at rest by its shape (the way above only
        // turns a bone toward its one paired child, never about itself; ends have none)
        if (const auto al = bones.aligns.find(joint.name); al != bones.aligns.end()) {
            std::vector<std::array<float, 3>> own, its;
            const Mat &wg = rest[static_cast<std::size_t>(joint.node)];
            for (const auto &mesh : mod.model.meshes)
                for (const auto &v : mesh.vertices) {
                    const auto it = joint_of_palette.find(v.joints[heaviest_weight(v)]);
                    if (it == joint_of_palette.end())
                        continue;
                    const int c = it->second;
                    if (c != static_cast<int>(j) && !(model->joints[static_cast<std::size_t>(c)].node < 0 &&
                                                      descends(*model, c, static_cast<int>(j))))
                        continue;
                    own.push_back(
                        vec(joint.correction, {v.position[0] * k - joint.head[0], v.position[1] * k - joint.head[1],
                                               v.position[2] * k - joint.head[2]}));
                }
            for (const auto &mesh : game.meshes)
                for (const auto &v : mesh.vertices) {
                    const std::uint32_t pal = v.joints[0];
                    if (pal >= game.palette.size() || pal >= game.palette_bones.size() ||
                        game.palette_bones[pal].empty())
                        continue;
                    auto bone = game.palette_bones[pal].front();
                    for (const auto &bw : game.palette_bones[pal])
                        if (bw.second > bone.second)
                            bone = bw;
                    if (static_cast<int>(bone.first) != joint.node)
                        continue;
                    Mat pm;
                    std::copy(game.palette[pal].begin(), game.palette[pal].end(), pm.begin());
                    const auto at = point(pm, v.position);
                    its.push_back({at[0] - wg[12], at[1] - wg[13], at[2] - wg[14]});
                }
            float degrees = 0.0F;
            const Mat r = align_part(al->second == "foot", own, its, degrees);
            joint.correction = mul(r, joint.correction);
            khdays_log("models: %s: %s turned %.1f degrees onto %s at rest (align %s; %zu and %zu vertices)",
                       name.c_str(), joint.name.c_str(), static_cast<double>(degrees),
                       game.object_names[static_cast<std::size_t>(joint.node)].c_str(), al->second.c_str(), own.size(),
                       its.size());
        }
        bool identity = true;
        for (int i = 0; i < 16; ++i)
            identity = identity && std::fabs(joint.correction[i] - kIdentity[i]) < 1e-6F;
        joint.exact = identity && std::fabs(joint.head[0] - hg[0]) < 1e-4F &&
                      std::fabs(joint.head[1] - hg[1]) < 1e-4F && std::fabs(joint.head[2] - hg[2]) < 1e-4F;
    }
}

// A mod's character's: its hands' weapons (their size, the base's hand nodes) and, with a paired skeleton, each paired
// joint's travel and size (Joint::travels): a model bigger or smaller than its base, or shaped otherwise, rises and
// falls with the game's hips at its own size, its feet kept on the ground, and holds its weapons in its own hands.
void set_character_joints(Model *model, const Loading &loading)
{
    const KhdaysCharacter *character = loading.character;
    if (character == nullptr)
        return;
    model->weapon_scale = character->weapon_scale > 0.0F ? character->weapon_scale : 1.0F;
    model->weapon_node.assign(model->node_names.size(), 0);
    for (int hand = 0; hand < 2; ++hand) {
        const char *name = hand == 0 && character->base_weapon_node != nullptr
                               ? character->base_weapon_node
                               : khdays_character_base_hand(character->base, hand);
        for (std::size_t g = 0; name != nullptr && g < model->node_names.size(); ++g)
            if (model->node_names[g] == name)
                model->weapon_node[g] = 1;
    }
    if (loading.own_skeleton)
        return;
    model->attached_on_joints = true;
    const std::vector<Mat> &rest = loading.rest;
    for (Joint &joint : model->joints) {
        if (joint.node < 0)
            continue;
        int above = joint.parent;
        while (above >= 0 && model->joints[static_cast<std::size_t>(above)].node < 0)
            above = model->joints[static_cast<std::size_t>(above)].parent;
        // from that joint (else the model's origin) and its node (else the game model's top node, its origin)
        std::array<float, 3> own_from{0.0F, 0.0F, 0.0F}, game_from;
        int top = joint.node;
        while (model->node_parent[static_cast<std::size_t>(top)] >= 0 &&
               model->node_parent[static_cast<std::size_t>(top)] != top)
            top = model->node_parent[static_cast<std::size_t>(top)];
        if (above >= 0) {
            const Joint &from = model->joints[static_cast<std::size_t>(above)];
            own_from = from.head;
            game_from = translation(rest[static_cast<std::size_t>(from.node)]);
        } else {
            game_from = translation(rest[static_cast<std::size_t>(top)]);
        }
        const auto game_at = translation(rest[static_cast<std::size_t>(joint.node)]);
        const float game_length =
            length({game_at[0] - game_from[0], game_at[1] - game_from[1], game_at[2] - game_from[2]});
        const float own_length =
            length({joint.head[0] - own_from[0], joint.head[1] - own_from[1], joint.head[2] - own_from[2]});
        joint.size = game_length > 1e-3F && own_length > 1e-3F ? own_length / game_length : 0.0F;
        joint.travels = !joint.exact;
        joint.travel_from = above >= 0 ? model->joints[static_cast<std::size_t>(above)].node : -1;
    }
    // a joint with no length to measure (a node at its parent's place, as a weapon's target at the model's origin):
    // the model's size, the middle of its joints'
    std::vector<float> sizes;
    for (const Joint &joint : model->joints)
        if (joint.node >= 0 && joint.size > 0.0F)
            sizes.push_back(joint.size);
    std::sort(sizes.begin(), sizes.end());
    const float middle = sizes.empty() ? 1.0F : sizes[sizes.size() / 2];
    for (Joint &joint : model->joints)
        if (joint.size <= 0.0F)
            joint.size = middle;
}

// Adds the two-bone limbs (`ik`): three paired joints, each below the one before.
void add_limbs(Model *model, const Loading &loading)
{
    const std::string &name = loading.name;
    const BonesFile &bones = loading.bones;
    const float k = loading.k;
    for (const auto &line : bones.limbs) {
        const auto &names = line.joints;
        std::array<int, 3> limb{-1, -1, -1};
        for (std::size_t i = 0; i < 3; ++i)
            limb[i] = joint_named(*model, names[i]);
        bool ok = limb[0] >= 0 && limb[1] >= 0 && limb[2] >= 0 && descends(*model, limb[1], limb[0]) &&
                  descends(*model, limb[2], limb[1]) && limb[1] != limb[0] && limb[2] != limb[1];
        for (std::size_t i = 0; ok && i < 3; ++i)
            ok = model->joints[static_cast<std::size_t>(limb[i])].node >= 0;
        if (!ok) {
            std::fprintf(stderr,
                         "models: %s: ik %s %s %s: each must be a paired joint below the one before; the line is "
                         "ignored\n",
                         name.c_str(), names[0].c_str(), names[1].c_str(), names[2].c_str());
            khdays_log("models: %s: ik %s %s %s: each must be a paired joint below the one before; the line is ignored",
                       name.c_str(), names[0].c_str(), names[1].c_str(), names[2].c_str());
            continue;
        }
        const float cm = 0.01F * k; // the offset's centimetres in the model's (scaled) units
        model->limbs.push_back({limb, line.exact, {line.offset[0] * cm, line.offset[1] * cm, line.offset[2] * cm}});
    }
}

// Sets what the game attaches to its nodes to be carried by the model's joints: each node's frame on the
// joint of its own node, else of its nearest ancestor's with one, as it sits under that node at rest.
void carry_attachments(Model *model, const Loading &loading)
{
    const std::vector<Mat> &rest = loading.rest;
    for (Joint &joint : model->joints) {
        if (joint.node < 0)
            continue;
        // the joint's frame at rest as the game's pose turns it (its correction), against its node's
        const Mat frame = axes_of(mul(joint.correction, mul(joint.bind, inverse(joint.inverse_bind))));
        joint.to_node = mul(inverse(frame), axes_of(rest[static_cast<std::size_t>(joint.node)]));
    }
    model->node_joint.assign(rest.size(), -1);
    model->node_local.assign(rest.size(), kIdentity);
    for (std::size_t g = 0; g < rest.size(); ++g)
        for (int a = static_cast<int>(g); a >= 0 && model->node_joint[g] < 0;
             a = a == model->node_parent[static_cast<std::size_t>(a)] ? -1
                                                                      : model->node_parent[static_cast<std::size_t>(a)])
            for (std::size_t j = 0; j < model->joints.size(); ++j)
                if (model->joints[j].node == a) {
                    model->node_joint[g] = static_cast<int>(j);
                    model->node_local[g] = mul(inverse(rest[static_cast<std::size_t>(a)]), rest[g]);
                    break;
                }
    for (const Mat &w : rest)
        model->node_rest_place.push_back(translation(w));
}

// `place`: joints on their game nodes' own places; the bone above each (when that joint's only paired
// child) stretched to reach it, with the joints that ride it.
void place_joints(Model *model, const Loading &loading)
{
    const std::string &name = loading.name;
    const BonesFile &bones = loading.bones;
    for (const auto &[joint_name, by] : bones.places) {
        const int found = joint_named(*model, joint_name);
        if (found < 0 || model->joints[static_cast<std::size_t>(found)].node < 0) {
            khdays_log("models: %s: place %s: no paired joint of that name; pair it in bones.txt; the line is ignored",
                       name.c_str(), joint_name.c_str());
            continue;
        }
        const auto j = static_cast<std::size_t>(found);
        model->joints[j].placed = true;
        model->joints[j].place_offset = {by[0] * 0.01F, by[1] * 0.01F, by[2] * 0.01F};
    }
    for (std::size_t c = 0; c < model->joints.size(); ++c) {
        if (!model->joints[c].placed)
            continue;
        const int p = paired_above(*model, static_cast<int>(c));
        int children = 0;
        for (std::size_t o = 0; o < model->joints.size(); ++o)
            if (model->joints[o].node >= 0 && paired_above(*model, static_cast<int>(o)) == p)
                ++children;
        if (p < 0 || children != 1)
            continue; // a bone with several paired children (the pelvis) is left; its mesh takes up the difference
        Stretch st;
        st.joint = p;
        st.child = static_cast<int>(c);
        for (std::size_t o = 0; o < model->joints.size(); ++o)
            if (static_cast<int>(o) == p ||
                (model->joints[o].node < 0 && paired_above(*model, static_cast<int>(o)) == p &&
                 descends(*model, static_cast<int>(o), p)))
                st.riders.push_back(static_cast<int>(o));
        model->stretches.push_back(std::move(st));
    }
    std::sort(model->stretches.begin(), model->stretches.end(),
              [](const Stretch &x, const Stretch &y) { return x.joint < y.joint; });
}

// `spine`: its chain, the node carrying its base, the pelvis it swings.
void add_spine(Model *model, const Loading &loading)
{
    const std::string &name = loading.name;
    const auto &spine_line = loading.bones.spine;
    if (!spine_line[0].empty()) {
        Spine sp;
        const int base = joint_named(*model, spine_line[0]);
        sp.end = joint_named(*model, spine_line[1]);
        bool ok = base >= 0 && sp.end >= 0 && model->joints[static_cast<std::size_t>(sp.end)].node >= 0;
        for (int a = ok ? model->joints[static_cast<std::size_t>(sp.end)].parent : -1; a >= 0;
             a = model->joints[static_cast<std::size_t>(a)].parent) {
            sp.chain.insert(sp.chain.begin(), a);
            if (a == base)
                break;
        }
        ok = ok && !sp.chain.empty() && sp.chain.front() == base;
        for (std::size_t c = 1; ok && c <= sp.chain.size() && sp.base_node < 0; ++c)
            sp.base_node = model->joints[static_cast<std::size_t>(c < sp.chain.size() ? sp.chain[c] : sp.end)].node;
        ok = ok && sp.base_node >= 0;
        for (sp.root = base; ok && model->joints[static_cast<std::size_t>(sp.root)].parent >= 0;)
            sp.root = model->joints[static_cast<std::size_t>(sp.root)].parent;
        if (ok && !spine_line[2].empty()) {
            sp.hips = joint_named(*model, spine_line[2]);
            sp.pelvis = sp.hips >= 0 ? (model->joints[static_cast<std::size_t>(sp.hips)].node >= 0
                                            ? sp.hips
                                            : paired_above(*model, sp.hips))
                                     : -1;
            ok = sp.pelvis >= 0 && sp.pelvis != sp.root &&
                 std::find(sp.chain.begin(), sp.chain.end(), sp.pelvis) == sp.chain.end();
        }
        if (ok) {
            model->spine = std::move(sp);
        } else {
            std::fprintf(stderr,
                         "models: %s: spine %s %s: the end must be a paired joint below the base, and `hips` a "
                         "joint whose paired joint above is off the chain; the line is ignored\n",
                         name.c_str(), spine_line[0].c_str(), spine_line[1].c_str());
            khdays_log("models: %s: spine %s %s: not a chain to a paired joint; check its joints; the line is ignored",
                       name.c_str(), spine_line[0].c_str(), spine_line[1].c_str());
        }
    }
}

// Gives the cloth its `game_shape`: the game model at rest, the node each bone follows, the scale.
void set_cloth_game_shape(Model *model, const Loading &loading)
{
    const khdays::assets::NeutralModel &game = loading.game;
    auto &mod = loading.mod;
    const float k = loading.k;
    const std::vector<int> &placed = loading.placed;
    if (model->secondary) {
        khdays::secondary::GameMesh gm;
        gm.scale = k;
        gm.node_parent = model->node_parent;
        for (const auto &at : model->node_rest_place)
            gm.node_place.push_back({at[0], at[1], at[2]});
        gm.node_of_bone.assign(mod.joints.size(), -1);
        for (std::size_t b = 0; b < mod.joints.size(); ++b)
            if (placed[b] >= 0)
                gm.node_of_bone[b] = model->joints[static_cast<std::size_t>(placed[b])].node;
        for (const auto &mesh : game.meshes) {
            const auto base = static_cast<std::uint32_t>(gm.positions.size());
            for (const auto i : mesh.indices)
                gm.triangles.push_back(base + i);
            for (const auto &v : mesh.vertices) {
                const std::uint32_t pal = v.joints[0];
                if (pal >= game.palette.size() || pal >= game.palette_bones.size()) { // kept: the triangles count it
                    gm.positions.push_back({});
                    gm.weights.push_back({{{-1, 0.0F}, {-1, 0.0F}, {-1, 0.0F}, {-1, 0.0F}}});
                    continue;
                }
                Mat pm;
                std::copy(game.palette[pal].begin(), game.palette[pal].end(), pm.begin());
                const auto at = point(pm, v.position);
                std::array<std::pair<int, float>, 4> w{};
                for (auto &x : w)
                    x = {-1, 0.0F};
                const auto &weights = game.palette_bones[pal]; // its first four, heaviest first below
                for (std::size_t j = 0; j < weights.size() && j < w.size(); ++j)
                    w[j] = {static_cast<int>(weights[j].first), weights[j].second};
                std::sort(w.begin(), w.end(), [](const auto &x, const auto &y) { return x.second > y.second; });
                gm.positions.push_back({at[0], at[1], at[2]});
                gm.weights.push_back(w);
            }
        }
        khdays::secondary::set_game_mesh(*model->secondary, gm);
    }
}

// `mesh_scale`: the joint's vertices scaled about its rest place in its own axes (B S B^-1 at rest, B
// its rest frame); the bones (its children, the cloth's colliders) keep their sizes.
void add_mesh_scales(Model *model, const Loading &loading)
{
    const std::string &name = loading.name;
    const BonesFile &bones = loading.bones;
    for (const MeshScale &s : bones.mesh_scales) {
        const int found = joint_named(*model, s.joint);
        if (found < 0) {
            std::fprintf(stderr, "models: %s: mesh_scale %s: no such joint; check its name; the line is ignored\n",
                         name.c_str(), s.joint.c_str());
            khdays_log("models: %s: mesh_scale %s: no such joint; check its name; the line is ignored", name.c_str(),
                       s.joint.c_str());
            continue;
        }
        const auto j = static_cast<std::size_t>(found);
        Mat by = kIdentity; // 0 as a thousandth: its normals stay finite
        by[0] = std::fmax(s.by[0], 1e-3F);
        by[5] = std::fmax(s.by[1], 1e-3F);
        by[10] = std::fmax(s.by[2], 1e-3F);
        const Mat &ib = model->joints[j].inverse_bind;
        model->mesh_scales.emplace_back(j, mul(inverse(ib), mul(by, ib)));
    }
}

// `twist`: the joints named, the turned joint's parent, its descendants (parents come first).
void add_twists(Model *model, const Loading &loading)
{
    const std::string &name = loading.name;
    const BonesFile &bones = loading.bones;
    for (const TwistLine &line : bones.twists) {
        std::array<std::size_t, 4> at{};
        bool found = true;
        for (std::size_t i = 0; i < 4 && found; ++i) {
            const int named = joint_named(*model, line.joints[i]);
            found = named >= 0;
            at[i] = found ? static_cast<std::size_t>(named) : 0U;
            if (!found) {
                std::fprintf(stderr, "models: %s: twist: no joint %s; check its name; the line is ignored\n",
                             name.c_str(), line.joints[i].c_str());
                khdays_log("models: %s: twist: no joint %s; check its name; the line is ignored", name.c_str(),
                           line.joints[i].c_str());
            }
        }
        if (!found || model->joints[at[0]].parent < 0)
            continue;
        Twist t;
        t.joint = at[0];
        t.by = at[1];
        t.parent = static_cast<std::size_t>(model->joints[at[0]].parent);
        t.to = at[3];
        t.share = line.share;
        t.swing = line.swing;
        if (!line.except.empty()) {
            const int named = joint_named(*model, line.except);
            if (named < 0) {
                khdays_log("models: %s: swing: no joint %s; check its name in bones.txt; it swings every way",
                           name.c_str(), line.except.c_str());
            } else {
                const auto &a = model->joints[at[0]].head, &b = model->joints[static_cast<std::size_t>(named)].head;
                t.except = true;
                t.except_way = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            }
        }
        const Mat from = inverse(model->joints[at[2]].inverse_bind), to = inverse(model->joints[at[3]].inverse_bind);
        const std::array<float, 3> d{to[12] - from[12], to[13] - from[13], to[14] - from[14]};
        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!line.swing && l < 1e-6F) {
            std::fprintf(
                stderr,
                "models: %s: twist: %s and %s are in one place (no axis); name two joints apart; the line is ignored\n",
                name.c_str(), line.joints[2].c_str(), line.joints[3].c_str());
            continue;
        }
        if (!line.swing)
            t.axis = {d[0] / l, d[1] / l, d[2] / l};
        std::vector<char> under(model->joints.size(), 0);
        for (std::size_t j = 0; j < model->joints.size(); ++j) {
            const int up = model->joints[j].parent;
            under[j] = j == t.joint || (up >= 0 && under[static_cast<std::size_t>(up)]) ? 1 : 0;
            if (under[j])
                t.turned.push_back(j);
        }
        model->twists.push_back(std::move(t));
    }
}

// Adds the meshes and their textures, each texture as its material takes its alpha (glTF alphaMode):
// kept; all opaque for an opaque material; for a mask each texel drawn or not by the cutoff, opaque;
// where the file does not say (FBX), translucent when any texel or vertex colour is part way. The face's
// blocks (a KH2 model's eyes and mouth) make the textures under them changing. Returns the vertices and
// triangles added.
std::pair<std::size_t, std::size_t> add_meshes(Model *model, Loading &loading)
{
    namespace assets = khdays::assets;
    const std::string &name = loading.name;
    auto &mod = loading.mod;
    const std::map<std::uint32_t, int> &joint_of_palette = loading.joint_of_palette;
    using AlphaMode = assets::NeutralMesh::AlphaMode;
    std::map<std::string, const assets::DecodedTexture *> texture_of;
    for (const auto &[key, texture] : mod.textures)
        texture_of[key] = &texture;
    struct Made {
        int replacement = -1, w = 0, h = 0;
        bool translucent = false;
    };
    std::map<std::string, Made> made;
    std::set<std::string> face_keys;
    for (auto &a : mod.texture_animations) {
        if (a.sprites.empty() || a.scripts.empty())
            continue;
        face_keys.insert(a.texture);
        auto block = std::make_unique<FaceBlock>();
        block->animation = std::move(a);
        const auto &slots = block->animation.slot_script;
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i] == static_cast<int>(block->animation.scripts.size()) - 1)
                block->last_slot = block->animation.first_slot + static_cast<int>(i);
        block->player = std::make_unique<khdays::face::Player>(
            block->animation, static_cast<std::uint32_t>(0x9E3779B9U * (model->face_blocks.size() + 1)));
        model->face_blocks.push_back(std::move(block));
    }
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
        if (face_keys.count(key)) { // changing: the face's sprites patched in as they play
            FaceTexture f;
            f.key = key;
            f.width = texture.width;
            f.height = texture.height;
            f.picture.assign(pixels, pixels + bytes);
            f.alpha = mode == AlphaMode::opaque ? 1 : mode == AlphaMode::mask ? 2 : 0;
            f.least = least;
            std::free(pixels);
            f.replacement = khdays_textures_add_changing((name + "/" + key + tag + "@face").c_str(), texture.width,
                                                         texture.height, 0);
            if (f.replacement < 0)
                return nullptr;
            std::memcpy(khdays_textures_change_begin(f.replacement), f.picture.data(), bytes);
            khdays_textures_change_end(f.replacement);
            m.replacement = f.replacement;
            model->face_textures.push_back(std::move(f));
        } else {
            m.replacement =
                khdays_textures_add((name + "/" + key + tag).c_str(), texture.width, texture.height, pixels);
        }
        m.w = texture.width;
        m.h = texture.height;
        m.translucent = mode == AlphaMode::blend || (mode == AlphaMode::unknown && partial);
        return &(made[key + tag] = m);
    };
    std::size_t vertices = 0, triangles = 0;
    for (auto &in : mod.model.meshes) {
        Mesh out;
        out.shadow_sprite = in.material_name == "ef_htd" || in.material_name == "kage00";
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
    return {vertices, triangles};
}

// `hide`: the vertices these joints carry most, between those heights at rest, not drawn, and faded back in
// below over `fade` (a gradient into the coat's dark inside). A triangle all hidden is dropped; one in
// the fade goes to a blended mesh of its own (the same picture), the rest kept as they were: the whole
// body blended would be drawn translucent, its parts covering each other wrongly.
void hide_vertices(Model *model, const Loading &loading)
{
    const std::string &name = loading.name;
    const BonesFile &bones = loading.bones;
    for (const HideLine &h : bones.hides) {
        std::vector<std::uint8_t> joint_hidden(model->joints.size(), 0);
        for (const auto &want : h.joints) {
            const int j = joint_named(*model, want);
            if (j < 0) {
                khdays_log("models: %s: hide %s: no such joint; check its name; it is left out", name.c_str(),
                           want.c_str());
                continue;
            }
            joint_hidden[static_cast<std::size_t>(j)] = 1;
        }
        const float top = h.top * 0.01F, bottom = h.bottom * 0.01F, fade = h.fade * 0.01F;
        std::vector<Mesh> faded_meshes;
        std::size_t dropped = 0, faded = 0;
        for (Mesh &mesh : model->meshes) {
            // each vertex's alpha: 0 hidden, 1 shown, between in the fade below the bottom
            std::vector<float> alpha(mesh.vertices.size(), 1.0F);
            for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
                const auto &vx = mesh.vertices[v];
                const std::uint32_t j = vx.joints[heaviest_weight(vx)];
                if (j >= joint_hidden.size() || !joint_hidden[j] || vx.position[1] > top)
                    continue;
                if (vx.position[1] >= bottom)
                    alpha[v] = 0.0F;
                else if (fade > 0.0F && vx.position[1] > bottom - fade)
                    alpha[v] = (bottom - vx.position[1]) / fade;
            }
            std::vector<std::uint32_t> kept, fading;
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                const float a0 = alpha[mesh.indices[t]], a1 = alpha[mesh.indices[t + 1]],
                            a2 = alpha[mesh.indices[t + 2]];
                auto &to = a0 >= 1.0F && a1 >= 1.0F && a2 >= 1.0F ? kept : fading;
                // a corner hidden: none of it, but at the bottom edge, where it fades from clear
                const float highest = std::fmax(mesh.vertices[mesh.indices[t]].position[1],
                                                std::fmax(mesh.vertices[mesh.indices[t + 1]].position[1],
                                                          mesh.vertices[mesh.indices[t + 2]].position[1]));
                if ((a0 <= 0.0F && a1 <= 0.0F && a2 <= 0.0F) ||
                    ((a0 <= 0.0F || a1 <= 0.0F || a2 <= 0.0F) && highest > bottom + fade)) {
                    ++dropped;
                    continue;
                }
                to.insert(to.end(), mesh.indices.begin() + static_cast<std::ptrdiff_t>(t),
                          mesh.indices.begin() + static_cast<std::ptrdiff_t>(t + 3));
            }
            if (!fading.empty()) {
                Mesh f = mesh; // its picture and settings; its own vertices, their alpha the fade's
                f.indices = std::move(fading);
                for (std::size_t v = 0; v < f.vertices.size(); ++v)
                    f.vertices[v].color[3] = static_cast<std::uint8_t>(
                        std::lround(std::fmin(1.0F, alpha[v]) * static_cast<float>(f.vertices[v].color[3])));
                f.vertex_alpha = VertexAlpha::blended;
                f.translucent = true;
                faded += f.indices.size() / 3;
                faded_meshes.push_back(std::move(f));
            }
            mesh.indices = std::move(kept);
        }
        for (Mesh &f : faded_meshes)
            model->meshes.push_back(std::move(f));
        khdays_log("models: %s: hide: %zu triangles not drawn, %zu fading", name.c_str(), dropped, faded);
    }
}

// Numbers the sky layers in KH2's painting order: opaque and cut-out meshes first, then blended ones.
void order_sky_layers(Model &model)
{
    int sky_layer = 0;
    for (const bool blended : {false, true})
        for (Mesh &mesh : model.meshes)
            if (mesh.sky != 0 && mesh.translucent == blended)
                mesh.sky = ++sky_layer;
}

// Takes the model's own animation: the nodes its skinned meshes' joints are (with their parents) move; one
// that moves another node would move a mesh made still on loading, which is not played. An own
// skeleton keeps its nodes and plays its description's clips, its weapon on its own bone.
void take_own_animation(Model *model, Loading &loading)
{
    const std::string &name = loading.name;
    const std::vector<Mat> &rest = loading.rest;
    auto &mod = loading.mod;
    const KhdaysCharacter *character = loading.character;
    const bool own_skeleton = loading.own_skeleton;
    const float k = loading.k;
    if (!own_skeleton && !mod.animations.empty()) {
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
                    khdays_log(
                        "models: %s: animation %s moves a node no skinned mesh follows: not played; take it out of the file",
                        name.c_str(), a.name.c_str());
                    break;
                }
            }
        }
        model->nodes = std::move(mod.nodes);
        model->animations = std::move(mod.animations);
        model->own_motion = true;
    }
    if (own_skeleton) {
        model->own = std::make_unique<OwnSkeleton>();
        OwnSkeleton &o = *model->own;
        o.character = character;
        for (const auto &node : mod.nodes)
            o.rest.push_back({node.translation, node.rotation, node.scale});
        for (const Joint &joint : model->joints) {
            if (joint.parent < 0 && joint.room_node >= 0) {
                o.root = joint.room_node;
                break;
            }
        }
        model->nodes = std::move(mod.nodes);
        // its weapons: the game's on its base's hand nodes (the camp's names: sa_h_R, ax_h_L ...; the right's named
        // by base_weapon_node when given), each moved onto its own bone of the same name (a skeleton made like the
        // game's; the right's named by weapon_bone when given). A hand the base model lacks has no weapon there.
        std::vector<Mat> at_rest;
        Mat s = kIdentity;
        s[0] = s[5] = s[10] = k;
        for (int hand = 0; hand < 2; ++hand) {
            const bool right = hand == 0;
            const char *game_name = right && character->base_weapon_node != nullptr
                                        ? character->base_weapon_node
                                        : khdays_character_base_hand(character->base, hand);
            if (game_name == nullptr)
                continue;
            OwnSkeleton::Held held;
            for (std::size_t g = 0; g < model->node_names.size() && held.game_node < 0; ++g) {
                if (model->node_names[g] == game_name)
                    held.game_node = static_cast<int>(g);
            }
            if (held.game_node < 0) {
                if (right && character->base_weapon_node != nullptr)
                    khdays_log("characters: %s: the base's node %s not found; check base_weapon_node in its "
                               "description: the weapon stays where the base's hand is",
                               character->identity, game_name);
                continue;
            }
            const char *bone = right && character->weapon_bone != nullptr ? character->weapon_bone : game_name;
            for (std::size_t i = 0; i < model->nodes.size() && held.bone < 0; ++i) {
                if (model->nodes[i].name == bone)
                    held.bone = static_cast<int>(i);
            }
            if (held.bone < 0) {
                khdays_log("characters: %s: its skeleton has no bone %s: the weapon at the base's %s stays where the "
                           "base's hand is%s",
                           character->identity, bone, game_name,
                           right ? " (weapon_bone in its description names the bone)" : "");
                continue;
            }
            if (at_rest.empty())
                at_rest = own_places(model->nodes, o.rest);
            if (right && character->has_weapon_turn) {
                // the bone's own axes, unscaled, turned (x, then y, then z)
                const auto *by = character->weapon_turn;
                held.fix = mul(inverse(s), turn_of({by[0], by[1], by[2]}));
            } else {
                // held as the base holds it: turned from the bone's axes at rest to the base node's at rest (the
                // weapon at the bone itself: the two rests' arms are not where each other's are)
                const Mat turn = axes_of(mul(inverse(at_rest[static_cast<std::size_t>(held.bone)]),
                                             rest[static_cast<std::size_t>(held.game_node)]));
                held.fix = mul(inverse(s), turn);
            }
            o.held.push_back(held);
        }
        std::string weapons;
        for (const OwnSkeleton::Held &h : o.held)
            weapons += (weapons.empty() ? ", its weapons on its bones " : ", ") +
                       model->nodes[static_cast<std::size_t>(h.bone)].name + " (the base's " +
                       model->node_names[static_cast<std::size_t>(h.game_node)] + ")";
        khdays_log("models: %s: %s's own skeleton (%zu nodes), its %d clips by the base's animations%s", name.c_str(),
                   character->identity, model->nodes.size(), character->action_count, weapons.c_str());
    }
}

// Says what was loaded, and with KHDAYS_TRACE_MODELS and the session log on, each joint, the node it
// follows, its turn from that node's pose, and each node's place at rest (a model that comes out wrong).
void log_loaded(const Model *model, const Loading &loading, std::size_t vertices, std::size_t triangles)
{
    const std::string &name = loading.name;
    const std::string &path = loading.path;
    const std::vector<Mat> &rest = loading.rest;
    const float k = loading.k;
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
}
} // namespace

std::unique_ptr<Model> load(const std::string &name, const std::string &path, const char *file, u32 offset)
{
    auto model = std::make_unique<Model>();
    model->name = name;
    model->path = path;
    Loading loading{name, path};
    if (!read_game_model(model.get(), loading, file, offset) || !read_mod_model(loading))
        return nullptr;
    read_settings(model.get(), loading);
    add_joints(model.get(), loading);
    // its secondary motion, cooked against the file's skeleton and skinned mesh before the meshes are taken
    {
        std::vector<Mat> inverse_binds;
        for (const Joint &joint : model->joints)
            inverse_binds.push_back(joint.inverse_bind);
        model->secondary =
            khdays::secondary::load_model_secondary(path, name, loading.mod, loading.placed, inverse_binds);
    }
    correct_rest_pose(model.get(), loading);
    set_character_joints(model.get(), loading);
    add_limbs(model.get(), loading);
    carry_attachments(model.get(), loading);
    place_joints(model.get(), loading);
    add_spine(model.get(), loading);
    set_cloth_game_shape(model.get(), loading);
    add_mesh_scales(model.get(), loading);
    add_twists(model.get(), loading);
    const auto [vertices, triangles] = add_meshes(model.get(), loading);
    hide_vertices(model.get(), loading);
    order_sky_layers(*model);
    take_own_animation(model.get(), loading);
    log_loaded(model.get(), loading, vertices, triangles);
    set_rest_size(*model);
    resolve_cuffs(*model, loading.bones.cuffs, loading.bones.cuff_pose, name);
    load_kh2_motions(*model, path);
    return model;
}

} // namespace khdays::model_parts
