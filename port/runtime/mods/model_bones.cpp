// A model's <model>.bones.txt: its lines read, and its rules resolved against the model's joints.

#include "model_parts.h"

namespace khdays::model_parts {

BonesFile read_bones(const std::string &model_file)
{
    BonesFile bones;
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
            bones.scale = std::strtof(b.c_str(), nullptr);
            if (!(bones.scale > 0.0F))
                bones.scale = 1.0F;
        } else if (a == "game_scale") {
            bones.game_scale = std::strtof(b.c_str(), nullptr);
            if (!(bones.game_scale > 0.0F)) {
                std::fprintf(stderr, "models: %s: write `game_scale <factor over 0>`; the line is ignored\n",
                             path.c_str());
                bones.game_scale = 1.0F;
            }
        } else if (a == "ik_side") {
            if (b == "kh2_hip" || b == "game")
                bones.ik_side_from_hip = b == "kh2_hip";
            else
                std::fprintf(stderr, "models: %s: write `ik_side kh2_hip` or `ik_side game`; the line is ignored\n",
                             path.c_str());
        } else if (a == "lower_hips") {
            bones.lower_hips = std::strtof(b.c_str(), nullptr);
            if (!(bones.lower_hips > 0.0F && bones.lower_hips <= 2.0F)) {
                std::fprintf(stderr,
                             "models: %s: write `lower_hips <share over 0, up to 2; 1 lowers the hips by all the legs "
                             "lack>`; the line is ignored\n",
                             path.c_str());
                bones.lower_hips = 0.0F;
            }
        } else if (a == "keep") {
            for (std::string node = b; !node.empty(); node.clear(), words >> node)
                bones.keep.push_back(node);
        } else if (a == "switch") {
            bones.setting = b;
        } else if (a == "cull") {
            if (b == "back") {
                bones.cull = 1;
            } else if (b == "none") {
                bones.cull = 2;
            } else {
                std::fprintf(stderr, "models: %s: cull %s: write `cull back` or `cull none`; the line is ignored\n",
                             path.c_str(), b.c_str());
            }
        } else if (a == "pose_degrees") {
            char *end = nullptr;
            const float degrees = std::strtof(b.c_str(), &end);
            if (end != b.c_str() && *end == '\0' && degrees >= 0.0F) {
                bones.pose_degrees = degrees;
            } else {
                std::fprintf(
                    stderr,
                    "models: %s: pose_degrees %s: write `pose_degrees <degrees, 0 or more>`; the line is ignored\n",
                    path.c_str(), b.c_str());
            }
        } else if (a == "mesh_scale") {
            MeshScale s{b, {}};
            if (words >> s.by[0] >> s.by[1] >> s.by[2] && s.by[0] >= 0.0F && s.by[1] >= 0.0F && s.by[2] >= 0.0F) {
                bones.mesh_scales.push_back(s);
            } else {
                std::fprintf(stderr,
                             "models: %s: write `mesh_scale <joint> <x> <y> <z>` (each 0 or more, in the joint's own "
                             "axes); the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "ik") {
            LimbLine limb;
            limb.joints[0] = b;
            std::string x;
            bool ok = static_cast<bool>(words >> limb.joints[1] >> limb.joints[2]);
            if (ok && words >> x) {
                limb.exact = x == "exact";
                ok = limb.exact;
                if (ok && words >> limb.offset[0])
                    ok = static_cast<bool>(words >> limb.offset[1] >> limb.offset[2]);
            }
            if (ok) {
                bones.limbs.push_back(limb);
            } else {
                std::fprintf(stderr,
                             "models: %s: write `ik <upper joint> <lower joint> <end joint> [exact [<x> <y> <z>]]` "
                             "(the offset in centimetres); the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "place") {
            std::array<float, 3> at{};
            std::vector<std::string> rest;
            for (std::string w; words >> w;)
                rest.push_back(w);
            const auto ride = std::find(rest.begin(), rest.end(), "ride");
            const bool riding = ride != rest.end();
            const std::string ridden = riding && ride + 1 != rest.end() ? *(ride + 1) : std::string{};
            const bool extra = riding && ride + 1 != rest.end() && ride + 2 != rest.end();
            if (riding)
                rest.erase(ride, rest.end());
            std::istringstream offset(rest.size() == 3 ? rest[0] + " " + rest[1] + " " + rest[2] : "");
            if (extra || (!rest.empty() && !(offset >> at[0] >> at[1] >> at[2]))) {
                std::fprintf(
                    stderr,
                    "models: %s: write `place <joint> [<x> <y> <z>] [ride [<bone>]]` (the offset in the game's "
                    "centimetres); the line is ignored\n",
                    path.c_str());
            } else {
                bones.places[b] = at;
                if (riding)
                    bones.places_riding[b] = ridden;
            }
        } else if (a == "limit_animations" || a == "limit_shapes") { // <name|prefix*>...
            auto &to = a == "limit_animations" ? bones.limit_animations : bones.limit_shapes;
            std::string w = b;
            do
                to.push_back(w);
            while (words >> w);
        } else if (a == "hide_mesh" || a == "limit_show_mesh" || a == "limit_hide_mesh") {
            // hide_mesh <index>...: whole meshes of the file not drawn (a weapon made into the hand's own mesh);
            // limit_show_mesh, limit_hide_mesh: drawn only while a `limit_animations` animation plays, or then not
            std::vector<int> meshes;
            bool ok = true;
            std::string w = b;
            do {
                char *end = nullptr;
                const long n = std::strtol(w.c_str(), &end, 10);
                ok = ok && !w.empty() && end != nullptr && *end == '\0' && n >= 0;
                if (ok)
                    meshes.push_back(static_cast<int>(n));
            } while (ok && words >> w);
            if (ok) {
                auto &to = a == "hide_mesh"         ? bones.hide_meshes
                           : a == "limit_show_mesh" ? bones.limit_show_meshes
                                                    : bones.limit_hide_meshes;
                to.insert(to.end(), meshes.begin(), meshes.end());
            } else {
                std::fprintf(stderr,
                             "models: %s: write `%s <index>...` (the file's meshes, whole numbers from 0); the line is "
                             "ignored\n",
                             path.c_str(), a.c_str());
            }
        } else if (a == "hide") {
            HideLine h;
            h.joints.push_back(b);
            std::string w;
            bool ok = false;
            while (words >> w) {
                if (w == "from") {
                    ok = static_cast<bool>(words >> h.top) && static_cast<bool>(words >> w) && w == "to" &&
                         static_cast<bool>(words >> h.bottom) && h.bottom < h.top;
                    if (ok && words >> w)
                        ok = w == "fade" && static_cast<bool>(words >> h.fade) && h.fade >= 0.0F;
                    break;
                }
                h.joints.push_back(w);
            }
            if (ok) {
                bones.hides.push_back(std::move(h));
            } else {
                std::fprintf(stderr,
                             "models: %s: write `hide <joint>... from <top> to <bottom> [fade <length>]` (the model's "
                             "own centimetres at rest, the top above the bottom); the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "spine") {
            std::string end, word, hips, onto;
            bool ok = static_cast<bool>(words >> end);
            while (ok && words >> word) {
                if (word == "hips" && hips.empty())
                    ok = static_cast<bool>(words >> hips);
                else if (word == "onto" && onto.empty())
                    onto = word;
                else
                    ok = false;
            }
            if (ok) {
                bones.spine = {b, end, hips, onto};
            } else {
                std::fprintf(stderr,
                             "models: %s: write `spine <base joint> <end joint> [hips <joint>] [onto]`; the line is "
                             "ignored\n",
                             path.c_str());
            }
        } else if (a == "twist") {
            TwistLine t;
            t.joints[0] = b;
            bool ok = static_cast<bool>(words >> t.joints[1] >> t.joints[2] >> t.joints[3]);
            if (ok && !(words >> t.share))
                t.share = 1.0F;
            if (ok && t.share >= 0.0F && t.share <= 1.0F) {
                bones.twists.push_back(t);
            } else {
                std::fprintf(stderr,
                             "models: %s: write `twist <joint> <by joint> <axis from joint> <axis to joint> "
                             "[<share, 0 to 1>]`; the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "cuff_fit") {
            // cuff_fit <clearance cm> <most open degrees> <shortest> <neighbours> <band from> <band to> <most shift cm>
            float clearance = 0.0F;
            khdays::models::CuffFit::Settings given = bones.cuff_settings;
            float most = 0.0F, slide = 0.0F;
            std::istringstream first(b);
            if ((first >> clearance) &&
                (words >> most >> given.shortest >> given.neighbours >> given.band_from >> given.band_to >> slide)) {
                given.most_shift = slide * 0.01F;
                given.clearance = clearance * 0.01F;
                given.most_open = most * 0.0174532925F;
                bones.cuff_settings = given;
            } else {
                std::fprintf(
                    stderr,
                    "models: %s: write `cuff_fit <clearance cm> <most open degrees> <shortest> <neighbours> <band "
                    "from> <band to> <most shift cm>`; the line is ignored\n",
                    path.c_str());
            }
        } else if (a == "cuff_motion_flaps") {
            // cuff_motion_flaps on|off: off, the flaps stand at rest on the cuff before the fit opens them, as in the
            // game's own animations; a KH2 motion flaring them (N_CM000_BTL's right, up to 58 degrees) was opened twice
            if (b == "on" || b == "off") {
                bones.cuff_motion_flaps = b == "on";
            } else {
                std::fprintf(stderr, "models: %s: write `cuff_motion_flaps on` or `off`; the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "cuff_thumb") {
            // cuff_thumb <more clearance cm> [<most open degrees>]: the last flap of each `cuff`
            float clearance = 0.0F, most = 0.0F;
            std::istringstream first(b);
            if (first >> clearance) {
                words >> most;
                bones.cuff_settings.own_flap = khdays::models::CuffFit::kFlaps - 1;
                bones.cuff_settings.own_clearance = clearance * 0.01F;
                bones.cuff_settings.own_most_open = most * 0.0174532925F;
            } else {
                std::fprintf(stderr,
                             "models: %s: write `cuff_thumb <more clearance cm> [<most open degrees>]`; the line is "
                             "ignored\n",
                             path.c_str());
            }
        } else if (a == "cuff_pose") {
            // cuff_pose <the hand's bend degrees> [<its pivot past its head, cm>], toward each `cuff`'s third flap;
            // `cuff_pose_bone`s say the pose
            std::istringstream first(b);
            float pivot = 0.0F;
            if (!(first >> bones.cuff_settings.pose_bend) || bones.cuff_settings.pose_bend <= 0.0F) {
                bones.cuff_settings.pose_bend = 0.0F;
                std::fprintf(
                    stderr,
                    "models: %s: write `cuff_pose <the hand's bend, degrees> [<its pivot past its head, cm>]`; "
                    "the line is ignored\n",
                    path.c_str());
            } else if (words >> pivot) {
                bones.cuff_settings.pose_pivot = pivot * 0.01F;
            }
        } else if (a == "cuff_pose_bone") {
            CuffPoseLine p;
            p.joint = b;
            if (words >> p.degrees[0] >> p.degrees[1] >> p.degrees[2] >> p.scale[0] >> p.scale[1] >> p.scale[2])
                bones.cuff_pose.push_back(p);
            else
                std::fprintf(stderr,
                             "models: %s: write `cuff_pose_bone <joint> <x> <y> <z degrees> <scale x> <y> <z>`; the "
                             "line is ignored\n",
                             path.c_str());
        } else if (a == "cuff") {
            CuffLine c;
            c.joints[0] = b;
            bool ok = true;
            for (std::size_t i = 1; i < c.joints.size(); ++i)
                ok = ok && static_cast<bool>(words >> c.joints[i]);
            if (ok)
                bones.cuffs.push_back(c);
            else
                std::fprintf(
                    stderr,
                    "models: %s: write `cuff <root joint> <hand joint> <flap> <flap> <flap> <flap>`, the flaps "
                    "in order round the ring; the line is ignored\n",
                    path.c_str());
        } else if (a == "swing") {
            TwistLine t;
            t.swing = true;
            t.joints[0] = b;
            bool ok = static_cast<bool>(words >> t.joints[1] >> t.joints[2]);
            t.joints[3] = t.joints[2];
            if (ok && !(words >> t.share))
                t.share = 1.0F;
            else if (ok)
                words >> t.except;
            if (ok && t.share >= 0.0F && t.share <= 1.0F) {
                bones.twists.push_back(t);
            } else {
                std::fprintf(stderr,
                             "models: %s: write `swing <joint> <by joint> <frame joint> [<share, 0 to 1> [<except "
                             "toward joint>]]`; the line is ignored\n",
                             path.c_str());
            }
        } else if (a == "held") { // held <joint>: a weapon posed by the weapon motions of the body holding it
            bones.held = b;
            std::string how;
            // ... rest: made at the holding bone's rest, no motions; rest <x> <y> <z>: the turn into the game's weapon
            // frame given here (as a `weapon` line's), for what no `weapon` line holds (a cutscene's own object)
            // ... turn <x> <y> <z>: posed by the motions, its joint's rest frame turned so (degrees about its own x,
            // y, z) into the weapon bone's, for a model made in other axes than the weapon the motions are for
            // ... body: posed so, on the frame the body's `weapon` line gave its weapon (the game draws it elsewhere)
            const bool has_word = static_cast<bool>(words >> how);
            bones.held_rest = has_word && how == "rest";
            bones.held_onto = has_word && how == "turn";
            bones.held_body = has_word && how == "body";
            const bool turn_given =
                (bones.held_rest || bones.held_onto) &&
                static_cast<bool>(words >> bones.held_turn[0] >> bones.held_turn[1] >> bones.held_turn[2]);
            bones.held_own_turn = bones.held_rest && turn_given;
            if (bones.held_onto && !turn_given) {
                bones.held_onto = false;
                std::fprintf(stderr, "models: %s: write `held %s turn <x> <y> <z>` (degrees); held without a turn\n",
                             path.c_str(), b.c_str());
            }
        } else if (a == "align") {
            std::string kind;
            if (words >> kind && (kind == "foot" || kind == "hand" || kind == "rest")) {
                bones.aligns[b] = kind;
            } else {
                std::fprintf(stderr, "models: %s: write `align <joint> foot|hand|rest`; the line is ignored\n",
                             path.c_str());
            }
        } else {
            bones.pairs[a] = b;
        }
    }
    return bones;
}

// The HD replacement switch (hud_settings.h) a bones.txt's `switch` names; -1 none (drawn always).
int setting_named(const std::string &name, const std::string &key)
{
    if (key.empty())
        return -1;
    for (int s = 0; s < KHDAYS_OPT_COUNT; ++s)
        if (key == khdays_hud_setting_keys[s])
            return s;
    std::fprintf(
        stderr,
        "models: %s: switch %s is no HD replacement's; name a key of hud_settings.h in bones.txt; drawn always\n",
        name.c_str(), key.c_str());
    return -1;
}

// The game's own shapes a bones.txt's `keep` keeps drawn: each whose vertices all hang from the nodes named (their
// lists' places in the model).
std::set<std::uint32_t> kept_lists(const std::string &name, const khdays::assets::NeutralModel &game,
                                   const std::vector<std::string> &keep)
{
    std::set<std::uint32_t> nodes, lists;
    for (const auto &node : keep) {
        const auto at = std::find(game.object_names.begin(), game.object_names.end(), node);
        if (at == game.object_names.end())
            std::fprintf(
                stderr,
                "models: %s: no node %s to keep; check the `keep` line against the game's names (--dump-graphics)\n",
                name.c_str(), node.c_str());
        else
            nodes.insert(static_cast<std::uint32_t>(at - game.object_names.begin()));
    }
    if (nodes.empty())
        return lists;
    for (const auto &mesh : game.meshes) {
        bool kept = !mesh.vertices.empty();
        for (const auto &v : mesh.vertices) {
            const auto entry = static_cast<std::size_t>(v.joints[0]);
            const auto &from = entry < game.palette_bones.size() ? game.palette_bones[entry]
                                                                 : decltype(game.palette_bones)::value_type{};
            kept = kept && !from.empty() &&
                   std::all_of(from.begin(), from.end(), [&](const auto &b) { return nodes.count(b.first) != 0; });
        }
        if (kept)
            lists.insert(mesh.list_offset);
    }
    return lists;
}

// bones.txt's `limit_shapes`: the game's shapes of those names (a prefix ending in *), by their lists' places. A shape
// whose material is fully transparent is never sent (NNSi_G3dFuncSbc_MAT, _SHP), so one drawn shows the look.
std::set<std::uint32_t> named_lists(const std::string &name, const khdays::assets::NeutralModel &game,
                                    const std::vector<std::string> &wanted)
{
    std::set<std::uint32_t> lists;
    for (const auto &want : wanted) {
        const bool prefix = !want.empty() && want.back() == '*';
        std::size_t found = 0;
        for (const auto &mesh : game.meshes)
            if (prefix ? mesh.name.compare(0, want.size() - 1, want, 0, want.size() - 1) == 0 : mesh.name == want) {
                lists.insert(mesh.list_offset);
                ++found;
            }
        khdays_log("models: %s: limit_shapes %s: %zu of the game's shapes%s", name.c_str(), want.c_str(), found,
                   found == 0 ? "; check the name against the game's (--dump-graphics)" : "");
    }
    return lists;
}

// The model's size at rest: each vertex where its joints' binds put it (an unbound one at the model's scale).
void set_rest_size(Model &model)
{
    const float k = model.scale;
    model.low = {INFINITY, INFINITY, INFINITY};
    model.high = {-INFINITY, -INFINITY, -INFINITY};
    for (const Mesh &mesh : model.meshes)
        for (const auto &v : mesh.vertices) {
            std::array<float, 3> p{0, 0, 0};
            float total = 0.0F;
            for (int w = 0; w < 4; ++w) {
                const float weight = v.weights[static_cast<std::size_t>(w)];
                const std::uint32_t jt = v.joints[static_cast<std::size_t>(w)];
                if (weight <= 0.0F || jt >= model.joints.size())
                    continue;
                const auto q = point(model.joints[jt].bind, v.position);
                for (int c = 0; c < 3; ++c)
                    p[static_cast<std::size_t>(c)] += weight * q[static_cast<std::size_t>(c)];
                total += weight;
            }
            if (total <= 0.0F)
                p = {v.position[0] * k, v.position[1] * k, v.position[2] * k};
            else
                for (auto &c : p)
                    c /= total;
            for (std::size_t c = 0; c < 3; ++c) {
                model.low[c] = std::fmin(model.low[c], p[c]);
                model.high[c] = std::fmax(model.high[c], p[c]);
            }
        }
}

// A turn of `degrees` about x, then y, then z (each about the fixed axes of the frame it is applied in)
Mat turn_of(const std::array<float, 3> &degrees)
{
    Mat turn = kIdentity;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float r = degrees[axis] * 0.0174532925F, c = std::cos(r), s = std::sin(r);
        const std::size_t u = (axis + 1) % 3, v = (axis + 2) % 3;
        Mat one = kIdentity;
        one[u * 4 + u] = c;
        one[v * 4 + v] = c;
        one[u * 4 + v] = s;
        one[v * 4 + u] = -s;
        turn = mul(one, turn);
    }
    return turn;
}

std::size_t hand_pose_index(const std::string &name)
{
    using namespace khdays::models;
    return name == "relaxed" ? kHandRelaxed
           : name == "open"  ? kHandOpen
           : name == "fist"  ? kHandFist
           : name == "flat"  ? kHandFlat
           : name == "grip"  ? kHandGrip
                             : kHandPoses;
}

// The rules as each joint's turn per game animation (Model::ds_turns): later lines win for a bone, and an
// animation's own lines over the default's
void resolve_ds_poses(Model &model, const std::vector<DsPoseRule> &rules,
                      const std::function<void(const std::string &)> &say)
{
    namespace kh2 = khdays::assets::kh2;
    std::map<std::string, std::map<int, std::size_t>> scopes; // animation -> bone -> rule
    for (std::size_t i = 0; i < rules.size(); ++i) {
        const std::vector<std::string> names =
            rules[i].animations.empty() ? std::vector<std::string>{""} : rules[i].animations;
        for (const std::string &a : names)
            for (const int b : rules[i].bones)
                scopes[a][b] = i;
    }
    if (const auto all = scopes.find(""); all != scopes.end())
        for (auto &[a, bones] : scopes)
            if (!a.empty())
                for (const auto &[b, i] : all->second)
                    bones.emplace(b, i); // the default's, unless the animation's own lines name the bone
    std::map<std::pair<int, float>, std::vector<kh2::Mat4>> poses; // a slot's pose on this model's bones
    const auto pose_of = [&](int slot, float frame) -> const std::vector<kh2::Mat4> * {
        const auto key = std::make_pair(slot, frame);
        if (const auto f = poses.find(key); f != poses.end())
            return &f->second;
        const kh2::Motion *motion =
            model.kh2_motions ? model.kh2_motions->motion(static_cast<std::size_t>(slot)) : nullptr;
        if (motion == nullptr)
            return nullptr;
        std::vector<kh2::Mat4> &out = poses[key];
        try {
            kh2::MotionPlayer player(skeleton_of(model, static_cast<std::size_t>(slot)));
            player.evaluate(*motion, frame * 2.0F, kIdentity, nullptr);
            pose_on_model(model, static_cast<std::size_t>(slot), player.joints(), out);
        } catch (const std::exception &e) {
            say("ds_pose: mset slot " + std::to_string(slot) + " cannot be played (" + e.what() +
                "); replace the mset or name another slot");
            out.clear();
        }
        return &out;
    };
    std::set<std::string> told;
    for (const auto &[a, bones] : scopes) {
        auto &turns = model.ds_turns[a];
        for (const auto &[b, i] : bones) {
            const DsPoseRule &rule = rules[i];
            const std::string bone = "ds_pose: bone " + std::to_string(b);
            const auto it = std::find(model.kh2_bone.begin(), model.kh2_bone.end(), b);
            if (it == model.kh2_bone.end()) {
                if (told.insert(bone).second)
                    say(bone + ": the model has no such bone; check the line's numbers; the line leaves it");
                continue;
            }
            const auto j = static_cast<std::size_t>(it - model.kh2_bone.begin());
            const Joint &joint = model.joints[j];
            // a joint on a game node takes a `rotate` on top of the node's turn (riku_h's pelvis in his glides), not a slot's
            if ((joint.node >= 0 && rule.slot >= 0) || joint.parent < 0) {
                if (told.insert(bone).second)
                    say(bone +
                        (joint.node >= 0
                             ? ": it follows a game node (bones.txt), which poses it; only `ds_pose rotate` "
                               "turns it further"
                             : ": it has no parent to turn from") +
                        "; take it off the line; the line leaves it");
                continue;
            }
            const auto p = static_cast<std::size_t>(joint.parent);
            Mat turn = turn_of(rule.degrees);
            if (rule.slot >= 0) {
                const std::vector<kh2::Mat4> *g = pose_of(rule.slot, rule.frame);
                const int pb = model.kh2_bone[p];
                if (g == nullptr || pb < 0 || static_cast<std::size_t>(b) >= g->size() ||
                    static_cast<std::size_t>(pb) >= g->size()) {
                    if (told.insert(bone + " slot").second)
                        say(bone + ": mset slot " + std::to_string(rule.slot) +
                            " gives it no pose (no such motion, or no mset); name a slot with a motion; the line "
                            "leaves it");
                    continue;
                }
                // its turn from its parent in the pose, against the same at rest, in its own axes
                const Mat posed = mul(inverse(axes_of((*g)[static_cast<std::size_t>(pb)])),
                                      axes_of((*g)[static_cast<std::size_t>(b)]));
                const Mat rest =
                    mul(inverse(axes_of(inverse(model.joints[p].inverse_bind))), axes_of(inverse(joint.inverse_bind)));
                turn = mul(inverse(rest), posed);
            }
            // about its head, in the space the joints' K take (the model at rest): B turn B^-1
            const Mat frame = mul(joint.bind, inverse(joint.inverse_bind));
            turns.emplace_back(j, mul(frame, mul(turn, inverse(frame))));
        }
    }
}

void resolve_hands(Model &model, const std::vector<HandRule> &rules, const std::vector<HandPoseRule> &poses,
                   const std::vector<HandBodyRule> &body, const std::function<void(const std::string &)> &say)
{
    using khdays::models::kHandPoses;
    const auto joint_of = [&](int bone) {
        const auto it = std::find(model.kh2_bone.begin(), model.kh2_bone.end(), bone);
        return it != model.kh2_bone.end() ? static_cast<int>(it - model.kh2_bone.begin()) : -1;
    };
    const auto frame_of = [&](std::size_t j) {
        return mul(model.joints[j].bind, inverse(model.joints[j].inverse_bind));
    };
    const auto under = [&](int j, std::size_t top) {
        for (; j >= 0; j = model.joints[static_cast<std::size_t>(j)].parent)
            if (static_cast<std::size_t>(j) == top)
                return true;
        return false;
    };
    static const char *const kPoseNames[] = {"relaxed", "open", "fist", "flat", "grip"};
    for (const HandRule &rule : rules) {
        const int hand = joint_of(rule.hand), forearm = joint_of(rule.forearm);
        if (hand < 0 || forearm < 0 || model.joints[static_cast<std::size_t>(forearm)].parent < 0) {
            say("hand: the model has no bone " + std::to_string(hand < 0 ? rule.hand : rule.forearm) +
                " or it has no parent; check the `hand` line's bone numbers; the line is ignored");
            continue;
        }
        const float back = length(rule.back);
        if (back <= 0.0F) {
            say("hand " + std::to_string(rule.hand) +
                ": `back` has no length; write the axis out of the back of the hand in its own axes; the line is ignored");
            continue;
        }
        Model::Hand h;
        h.joint = static_cast<std::size_t>(hand);
        h.forearm = static_cast<std::size_t>(forearm);
        h.upper = static_cast<std::size_t>(model.joints[h.forearm].parent);
        h.node = model.joints[h.joint].node;
        h.frame = frame_of(h.joint);
        h.forearm_frame = frame_of(h.forearm);
        h.back = {rule.back[0] / back, rule.back[1] / back, rule.back[2] / back};
        const auto &hh = model.joints[h.joint].head, &fh = model.joints[h.forearm].head;
        const std::array<float, 3> way = {hh[0] - fh[0], hh[1] - fh[1], hh[2] - fh[2]};
        const float reach = length(way);
        if (reach <= 0.0F) {
            say("hand " + std::to_string(rule.hand) +
                ": the hand and forearm bones are in one place; name the bones at the elbow and wrist; the line is ignored");
            continue;
        }
        h.forearm_way = vec(inverse(axes_of(h.forearm_frame)), {way[0] / reach, way[1] / reach, way[2] / reach});
        std::vector<int> fingers = rule.fingers;
        std::sort(fingers.begin(), fingers.end(), [&](int a, int b) { return joint_of(a) < joint_of(b); });
        for (const int bone : fingers) {
            const int j = joint_of(bone);
            if (j < 0 || model.joints[static_cast<std::size_t>(j)].node >= 0 ||
                model.joints[static_cast<std::size_t>(j)].parent < 0) {
                say("hand: finger bone " + std::to_string(bone) +
                    (j < 0 ? ": the model has no such bone; check the `hand` line's numbers"
                           : ": it follows a game node (bones.txt) or has no parent; take it off the `hand` line") +
                    "; the bone is left out");
                continue;
            }
            Model::HandFinger f;
            f.joint = static_cast<std::size_t>(j);
            f.parent = static_cast<std::size_t>(model.joints[f.joint].parent);
            f.frame = frame_of(f.joint);
            f.frame_inverse = inverse(f.frame);
            f.poses.fill({0.0F, 0.0F, 0.0F, 1.0F});
            for (const HandPoseRule &pose : poses)
                if (std::find(pose.bones.begin(), pose.bones.end(), bone) != pose.bones.end())
                    f.poses[pose.pose] = khdays::models::frame_detail::quat_of(turn_of(pose.degrees));
            h.fingers.push_back(f);
        }
        // each finger: a run of joints from one whose parent is no finger joint; the first (in the model's order) is
        // taken as the thumb. Its spheres at its joints that are apart, and past its last by half that bone
        const auto is_finger = [&](std::size_t j) {
            return std::any_of(h.fingers.begin(), h.fingers.end(), [&](const auto &f) { return f.joint == j; });
        };
        for (std::size_t i = 0; i < h.fingers.size(); ++i) {
            if (is_finger(h.fingers[i].parent)) {
                h.digits.back().last = i;
                continue;
            }
            Model::HandDigit digit;
            digit.first = digit.last = i;
            h.digits.push_back(digit);
        }
        for (std::size_t n = 0; n < h.digits.size(); ++n) {
            Model::HandDigit &digit = h.digits[n];
            digit.thumb = n == 0;
            std::array<float, 3> last = model.joints[h.fingers[digit.first].joint].head;
            std::array<float, 3> along{};
            digit.spheres.emplace_back(h.fingers[digit.first].joint, last);
            for (std::size_t i = digit.first + 1; i <= digit.last; ++i) {
                const auto &at = model.joints[h.fingers[i].joint].head;
                const std::array<float, 3> d = {at[0] - last[0], at[1] - last[1], at[2] - last[2]};
                if (length(d) < 0.001F)
                    continue;
                along = d;
                last = at;
                digit.spheres.emplace_back(h.fingers[i].joint, at);
            }
            digit.spheres.emplace_back(
                h.fingers[digit.last].joint,
                std::array<float, 3>{last[0] + along[0] * 0.8F, last[1] + along[1] * 0.8F, last[2] + along[2] * 0.8F});
        }
        // the palm: the wrist and the fingers' roots (all but the thumb's), what the arm moves out of things
        std::array<float, 3> sum = hh;
        float n = 1.0F;
        h.samples.push_back({hh[0], hh[1], hh[2], 0.025F});
        for (std::size_t d = 1; d < h.digits.size(); ++d) {
            const auto &root = model.joints[h.fingers[h.digits[d].first].joint].head;
            sum = {sum[0] + root[0], sum[1] + root[1], sum[2] + root[2]};
            n += 1.0F;
            h.samples.push_back({root[0], root[1], root[2], 0.012F});
        }
        h.palm = {sum[0] / n, sum[1] / n, sum[2] / n};
        h.samples.push_back({h.palm[0], h.palm[1], h.palm[2], 0.022F});
        for (std::size_t j = 0; j < model.joints.size(); ++j) {
            const int at = static_cast<int>(j);
            if (under(at, h.joint))
                h.hand_joints.push_back(j);
            else if (under(at, h.forearm))
                h.forearm_joints.push_back(j);
            else if (under(at, h.upper))
                h.upper_joints.push_back(j);
        }
        model.hands.push_back(std::move(h));
    }
    if (model.hands.empty())
        return;
    for (std::size_t p = 0; p < kHandPoses; ++p)
        if (std::none_of(poses.begin(), poses.end(), [&](const HandPoseRule &r) { return r.pose == p; }))
            say(std::string("hand: no `hand_pose ") + kPoseNames[p] + "` lines: that pose is the fingers at rest");
    // a game node on a hand: under the hand's node, or carried by a joint under the hand's (its `weapon` line)
    model.node_hand.assign(model.node_names.size(), -1);
    for (std::size_t g = 0; g < model.node_hand.size(); ++g)
        for (std::size_t i = 0; i < model.hands.size(); ++i) {
            bool on = g < model.kh2_weapon.size() && model.kh2_weapon[g] >= 0 &&
                      under(model.kh2_weapon[g], model.hands[i].joint);
            int a = static_cast<int>(g);
            for (std::size_t up = 0; !on && a >= 0 && up < model.node_parent.size(); ++up) {
                on = a == model.hands[i].node;
                a = model.node_parent[static_cast<std::size_t>(a)];
            }
            if (on)
                model.node_hand[g] = static_cast<int>(i);
        }
    // the body's parts: their vertices as they stand at rest
    for (const HandBodyRule &rule : body) {
        const int first = joint_of(rule.bones.front()), from = joint_of(rule.from), to = joint_of(rule.to);
        if (first < 0 || from < 0 || to < 0) {
            say("hand_body: a bone of the line is not in the model; check its numbers; the line is ignored");
            continue;
        }
        std::vector<khdays::models::HandVec> points;
        for (const Mesh &mesh : model.meshes)
            for (const auto &v : mesh.vertices) {
                const std::uint32_t j = v.joints[heaviest_weight(v)];
                if (j < model.kh2_bone.size() &&
                    std::find(rule.bones.begin(), rule.bones.end(), model.kh2_bone[j]) != rule.bones.end())
                    points.push_back(point(model.joints[j].bind, v.position));
            }
        Model::HandBody part;
        part.joint = static_cast<std::size_t>(first);
        part.margin = rule.margin * 0.01F;
        part.shape.bake(model.joints[static_cast<std::size_t>(from)].head,
                        model.joints[static_cast<std::size_t>(to)].head, points);
        if (points.empty())
            say("hand_body: no vertices are carried by bone " + std::to_string(rule.bones.front()) +
                " and the others of its line; check their numbers");
        model.hand_body.push_back(std::move(part));
    }
}

// bones.txt's `cuff` lines: the cuff's surface and flaps at rest, and the hand's vertices that must stay inside it
void resolve_cuffs(Model &model, const std::vector<CuffLine> &lines, const std::vector<CuffPoseLine> &pose_lines,
                   const std::string &name)
{
    using namespace khdays::models;
    const auto subtree = [&](std::size_t top) {
        std::vector<std::size_t> out;
        std::vector<char> under(model.joints.size(), 0);
        for (std::size_t j = 0; j < model.joints.size(); ++j) {
            const int up = model.joints[j].parent;
            under[j] = j == top || (up >= 0 && under[static_cast<std::size_t>(up)]) ? 1 : 0;
            if (under[j])
                out.push_back(j);
        }
        return out;
    };
    for (const CuffLine &line : lines) {
        std::array<int, 6> at{};
        bool found = true;
        for (std::size_t i = 0; i < at.size(); ++i) {
            at[i] = joint_named(model, line.joints[i]);
            if (at[i] < 0) {
                khdays_log("models: %s: cuff: no joint %s; check its name in bones.txt; the line is ignored",
                           name.c_str(), line.joints[i].c_str());
                found = false;
            }
        }
        if (!found)
            continue;
        Model::Cuff cuff;
        cuff.root = static_cast<std::size_t>(at[0]);
        cuff.hand = static_cast<std::size_t>(at[1]);
        std::vector<int> flap_of(model.joints.size(), -1);
        std::array<HandVec, CuffFit::kFlaps> hinges{}, tips{};
        std::array<std::size_t, CuffFit::kFlaps> tip_joint{};
        for (std::size_t f = 0; f < CuffFit::kFlaps; ++f) {
            const auto top = static_cast<std::size_t>(at[f + 2]);
            cuff.flap_joints[f] = subtree(top);
            hinges[f] = model.joints[top].head;
            // its end: the joint furthest from its root; what it carries bends there
            float furthest = -1.0F;
            for (const std::size_t j : cuff.flap_joints[f]) {
                flap_of[j] = static_cast<int>(f);
                const float l = length(hand_sub(model.joints[j].head, hinges[f]));
                if (l > furthest) {
                    furthest = l;
                    tip_joint[f] = j;
                }
            }
            tips[f] = model.joints[tip_joint[f]].head;
            cuff.tip_joints[f] = subtree(tip_joint[f]);
        }
        // a flap whose bones all stand at its hinge (B_EX400's: one bone each, its head the root's, no tip bone) ends
        // its last bone's own length along that bone's x (the file's: 7.3 cm there), and has no tip of its own to bend
        for (std::size_t f = 0; f < CuffFit::kFlaps; ++f) {
            if (length(hand_sub(tips[f], hinges[f])) > 1e-4F)
                continue;
            for (const std::size_t j : cuff.flap_joints[f]) {
                if (model.joints[j].length <= 0.0F)
                    continue;
                const Mat axes = axes_of(inverse(model.joints[j].inverse_bind));
                const HandVec x = hand_unit({axes[0], axes[1], axes[2]});
                tips[f] = hand_add(model.joints[j].head, x, model.joints[j].length);
                tip_joint[f] = j;
            }
            cuff.tip_joints[f].clear();
        }
        cuff.slides = subtree(
            model.joints[cuff.root].parent >= 0 ? static_cast<std::size_t>(model.joints[cuff.root].parent) : cuff.root);
        std::vector<char> ring(model.joints.size(), 0); // what the cuff's surface is: the flaps and the root's own
        for (const std::size_t j : subtree(cuff.root))
            ring[j] = 1;
        if (const int up = model.joints[cuff.root].parent; up >= 0)
            ring[static_cast<std::size_t>(up)] = 1;
        std::vector<char> on_hand(model.joints.size(), 0);
        for (const std::size_t j : subtree(static_cast<std::size_t>(at[1])))
            on_hand[j] = 1;
        std::vector<CuffFit::Triangle> triangles;
        std::vector<std::array<std::size_t, 3>> triangle_joints; // each corner's joint
        std::set<std::array<int, 3>> taken; // the hand's vertices, thinned to a centimetre apart at rest
        for (const Mesh &mesh : model.meshes) {
            // only what is drawn: a `hide`n weapon baked into the hand (N_CM000_BTL's scythe, 2 m long) is not fitted
            // round, its vertices kept after its triangles are dropped
            std::vector<char> drawn(mesh.vertices.size(), 0);
            for (const std::uint32_t i : mesh.indices)
                if (i < drawn.size())
                    drawn[i] = 1;
            for (std::size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
                const auto &v = mesh.vertices[vi];
                const std::uint32_t j = v.joints[heaviest_weight(v)];
                if (j >= on_hand.size() || !on_hand[j] || !drawn[vi])
                    continue;
                const auto rest = point(model.joints[j].bind, v.position);
                const std::array<int, 3> cell = {static_cast<int>(std::floor(rest[0] * 100.0F)),
                                                 static_cast<int>(std::floor(rest[1] * 100.0F)),
                                                 static_cast<int>(std::floor(rest[2] * 100.0F))};
                if (taken.insert(cell).second)
                    cuff.points.emplace_back(static_cast<std::size_t>(j), v.position);
            }
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                std::array<std::size_t, 3> joints{};
                bool all_ring = true, any_flap = false;
                CuffFit::Triangle triangle;
                for (std::size_t i = 0; i < 3; ++i) {
                    const auto &v = mesh.vertices[mesh.indices[t + i]];
                    const std::uint32_t j = v.joints[heaviest_weight(v)];
                    all_ring = all_ring && j < ring.size() && ring[j];
                    const int flap = j < flap_of.size() ? flap_of[j] : -1;
                    any_flap = any_flap || flap >= 0;
                    joints[i] = j;
                    if (j < model.joints.size())
                        triangle.at[i] = point(model.joints[j].bind, v.position);
                    triangle.flap[i] = flap;
                }
                if (all_ring && any_flap) {
                    triangles.push_back(triangle);
                    triangle_joints.push_back(joints);
                }
            }
        }
        cuff.fit.bake(model.joints[cuff.root].head, hinges, tips, triangles);
        if (model.cuff_settings.pose_bend > 0.0F && !pose_lines.empty() && cuff.fit.ready) {
            // the flaps' bones, parents first: a rest frame is the joint's axes at its head, in the model's own space
            const auto frame_at = [&](std::size_t j) {
                Mat f = axes_of(inverse(model.joints[j].inverse_bind));
                f[12] = model.joints[j].head[0];
                f[13] = model.joints[j].head[1];
                f[14] = model.joints[j].head[2];
                return f;
            };
            std::vector<int> among(model.joints.size(), -1);
            const int ring_joint = model.joints[cuff.root].parent;
            for (std::size_t j = 0; j < model.joints.size(); ++j) {
                if (flap_of[j] < 0 && j != cuff.root && static_cast<int>(j) != ring_joint)
                    continue;
                CuffPoseBone bone;
                bone.joint = j;
                const int up = model.joints[j].parent;
                bone.up = up >= 0 ? among[static_cast<std::size_t>(up)] : -1;
                bone.up_rest = up >= 0 ? frame_at(static_cast<std::size_t>(up)) : kIdentity;
                bone.local = mul(inverse(bone.up_rest), frame_at(j));
                bone.back = inverse(frame_at(j));
                for (const CuffPoseLine &p : pose_lines)
                    if (p.joint == model.joints[j].name) {
                        bone.turn = frame_detail::quat_of(turn_of(p.degrees));
                        bone.scale = p.scale;
                    }
                among[j] = static_cast<int>(cuff.pose.size());
                cuff.pose.push_back(bone);
            }
            if (ring_joint >= 0)
                cuff.pose_ring = among[static_cast<std::size_t>(ring_joint)];
            for (std::size_t f = 0; f < CuffFit::kFlaps; ++f)
                for (const std::size_t j : cuff.flap_joints[f])
                    cuff.flap_pose[f].push_back(among[j]);
            const Mat hand_axes = axes_of(inverse(model.joints[cuff.hand].inverse_bind));
            cuff.hand_way = hand_unit({hand_axes[0], hand_axes[1], hand_axes[2]});
            const HandVec back = cuff.fit.out[2];
            cuff.toward = hand_unit(hand_add(back, cuff.hand_way, -hand_dot(back, cuff.hand_way)));
            std::vector<Mat> moved;
            for (int s = 0; s < kCuffSurfaces; ++s) {
                cuff_pose_at(cuff.pose, static_cast<float>(s) / static_cast<float>(kCuffSurfaces - 1), moved);
                const auto at_share = [&](std::size_t j, const HandVec &p) {
                    return among[j] >= 0 ? point(moved[static_cast<std::size_t>(among[j])], p) : p;
                };
                std::array<HandVec, CuffFit::kFlaps> posed_hinges{}, posed_tips{};
                for (std::size_t f = 0; f < CuffFit::kFlaps; ++f) {
                    posed_hinges[f] = at_share(static_cast<std::size_t>(at[f + 2]), hinges[f]);
                    posed_tips[f] = at_share(tip_joint[f], tips[f]);
                }
                std::vector<CuffFit::Triangle> posed = triangles;
                for (std::size_t t = 0; t < posed.size(); ++t)
                    for (std::size_t i = 0; i < 3; ++i)
                        posed[t].at[i] = at_share(triangle_joints[t][i], posed[t].at[i]);
                cuff.surfaces.emplace_back();
                cuff.surfaces.back().bake(at_share(cuff.root, model.joints[cuff.root].head), posed_hinges, posed_tips,
                                          posed);
            }
        }
        if (!cuff.fit.ready || cuff.points.empty()) {
            khdays_log(
                "models: %s: cuff %s: %zu triangles and %zu of the hand's vertices: check the line's joints; the line "
                "is ignored",
                name.c_str(), line.joints[0].c_str(), triangles.size(), cuff.points.size());
            continue;
        }
        khdays_log("models: %s: cuff %s fitted round %zu of %s's vertices (%zu triangles, %.1f to %.1f cm along)",
                   name.c_str(), line.joints[0].c_str(), cuff.points.size(), line.joints[1].c_str(), triangles.size(),
                   static_cast<double>(cuff.fit.from * 100.0F), static_cast<double>(cuff.fit.to * 100.0F));
        model.cuffs.push_back(std::move(cuff));
    }
}

} // namespace khdays::model_parts
