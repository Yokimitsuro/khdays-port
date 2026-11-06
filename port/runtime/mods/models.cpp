// A mod's models drawn instead of the game's (models.h): matrices column-major for column vectors;
// each joint drawn with K_j = T(head now) * linear(D_G) * C_j * T(-head_j), D_G = M_G * inverse(W_G)
// for the node G it follows and C_j its rest pose correction; faces culled as the file says unless
// <model>.bones.txt says otherwise (docs/RENDERING.md, "Mods' graphics"). A replaced room plays its own
// animation (room_animation.cpp) by the game's frames, stopped while the game is paused.

#include "model_parts.h"

#include "kh2_corridors.h"

extern "C" {
#include "../../host/app/host.h" // [video] culling (KHDAYS_HOST_NO_CULLING)
}

namespace khdays::model_parts {

// The draw is one of Days's ovals drawn as KH2's corridor (kh2_corridors.h): its shapes left out.
bool corridor_drawn = false;

void trace_floor(const Model &m, const std::string &state)
{
    if (!tracing())
        return;
    FloorTrace &t = floor_traces[m.name];
    if (t.said != state)
        khdays_log("models: %s: IKAdjust %s", m.name.c_str(), state.c_str());
    t.said = state;
}

Floor floor_of(const Model &m, const Mat &base)
{
    Floor f;
    float camera[16];
    const bool feet = (m.kh2_skeleton != nullptr && !m.kh2_skeleton->feet.empty()) ||
                      (m.kh2_own_skeleton != nullptr && !m.kh2_own_skeleton->feet.empty() && !m.kh2_own_slots.empty());
    if (!m.kh2_ik_adjust || !feet || drawn_object == nullptr || !khdays_g3d_camera(camera)) {
        trace_floor(m, "off: ik_adjust off, no feet in the model or no camera yet");
        return f;
    }
    const KhdaysRef object = KHDAYS_REF(drawn_object);
    const auto found = grounds.find(object - 0x24U);
    if (found == grounds.end() || khdays_events_vblank_count() - found->second.vblank > 4U) {
        if (tracing()) { // the actors settled lately near its render object, to find the link to its mover
            std::string near;
            for (const auto &[record, g] : grounds) {
                const long long off = static_cast<long long>(object) - static_cast<long long>(record);
                if (khdays_events_vblank_count() - g.vblank > 4U || off < -0x4000 || off > 0x4000)
                    continue;
                char one[64];
                std::snprintf(one, sizeof(one), " %08x (render object - record = %lld)", record, off);
                near += one;
            }
            trace_floor(m, found == grounds.end() ? "off: no mover's owner at render object - 0x24; near:" +
                                                        (near.empty() ? std::string(" none") : near)
                                                  : "off: its mover was not put on a floor lately");
        }
        return f;
    }
    const auto *mover = static_cast<const unsigned char *>(KHDAYS_W(found->second.mover));
    KhdaysRef record = 0;
    s32 speed = 0;
    u16 index = 0;
    std::memcpy(&record, mover, 4);
    std::memcpy(&speed, mover + 0x38, 4);
    std::memcpy(&index, mover + 0x46, 2);
    const bool on_floor = speed == static_cast<s32>(0x80000000U); // Mover_ResolveFloor: landed
    if (record != found->first || static_cast<std::int16_t>(index) < 0 || !on_floor) {
        trace_floor(m, !on_floor ? "off: its actor is not on the floor" : "off: its mover changed");
        return f;
    }
    // the locals the game's functions read are passed by reference: the game thread's stack must be
    // in the game's memory (it is, as the game's own casts pass theirs)
    int here = 0;
    if (static_cast<std::uintptr_t>(reinterpret_cast<const unsigned char *>(&here) - khdays_window) >= 0x100000000ULL) {
        trace_floor(m, "off: the game thread's stack is outside the game's memory");
        return f;
    }
    trace_floor(m, "on");
    Mat view_of_world;
    std::copy(camera, camera + 16, view_of_world.begin());
    Mat cm = kIdentity; // KH2's centimetres to the model's metres
    cm[0] = cm[5] = cm[10] = 0.01F;
    f.world = mul(inverse(view_of_world), mul(base, cm)); // Days' world is y up, as the solver's
    FloorTrace *t = tracing() ? &floor_traces[m.name] : nullptr;
    if (t != nullptr && ++t->draws % 60U == 0U) {
        s32 at[3] = {};
        std::memcpy(at, static_cast<const unsigned char *>(KHDAYS_W(record)) + 0xa8, sizeof(at));
        khdays_log("models: %s: IKAdjust %u rays, %u hits; actor at %.2f %.2f %.2f, model at %.2f %.2f %.2f; "
                   "last ray %.2f %.2f %.2f to %.2f %.2f %.2f, hit %.2f %.2f %.2f (Days' world)",
                   m.name.c_str(), t->rays, t->hits, static_cast<double>(at[0]) / 4096.0,
                   static_cast<double>(at[1]) / 4096.0, static_cast<double>(at[2]) / 4096.0,
                   static_cast<double>(f.world[12]), static_cast<double>(f.world[13]), static_cast<double>(f.world[14]),
                   static_cast<double>(t->from[0]), static_cast<double>(t->from[1]), static_cast<double>(t->from[2]),
                   static_cast<double>(t->to[0]), static_cast<double>(t->to[1]), static_cast<double>(t->to[2]),
                   static_cast<double>(t->hit[0]), static_cast<double>(t->hit[1]), static_cast<double>(t->hit[2]));
        t->rays = t->hits = 0;
    }
    f.ray = [record, index, t](const std::array<float, 3> &from, const std::array<float, 3> &to,
                               std::array<float, 3> &hit) {
        if (t != nullptr) {
            ++t->rays;
            t->from = from;
            t->to = to;
        }
        struct Vec {
            s32 x, y, z;
        };
        struct Params { // CollCastParams
            KhdaysRef origin, direction;
            s32 radius;
            u16 unit, flags;
            KhdaysRef exclude;
        };
        const auto fx = [](float v) { return static_cast<s32>(std::lround(v * 4096.0F)); };
        Vec origin{fx(from[0]), fx(from[1]), fx(from[2])};
        Vec direction{fx(to[0] - from[0]), fx(to[1] - from[1]), fx(to[2] - from[2])};
        Vec at{};
        Params params{KHDAYS_REF(&origin), KHDAYS_REF(&direction), 0, 0, 0, record};
        const KhdaysRef world = GetTrackEntryBase(index);
        if (world == 0)
            return false;
        const KhdaysRef h = Collision_RunRayCast(world, KHDAYS_REF(&params));
        if (h == 0)
            return false;
        s32 distance = 0;
        std::memcpy(&distance, static_cast<const unsigned char *>(KHDAYS_W(h)) + 12, 4);
        Vec3ScaleAddQ27(distance, KHDAYS_REF(&direction), KHDAYS_REF(&origin), KHDAYS_REF(&at));
        hit = {static_cast<float>(at.x) / 4096.0F, static_cast<float>(at.y) / 4096.0F,
               static_cast<float>(at.z) / 4096.0F};
        if (t != nullptr) {
            ++t->hits;
            t->hit = hit;
        }
        return true;
    };
    f.on = true;
    return f;
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

// The view's sides a point in clip space lies past, a bit each: x past kx * w or -kx * w, y past ky * w or -ky * w
// (khdays_g3d_mod_cull_sides). A piece or triangle whose corners all lie past one side is not seen.
unsigned past_sides(float x, float y, float w, float kx, float ky)
{
    return (x > kx * w ? 1u : 0u) | (x < -kx * w ? 2u : 0u) | (y > ky * w ? 4u : 0u) | (y < -ky * w ? 8u : 0u);
}

// The face's frame (face.h), once a game frame: each block's slot held by what comes first -- its character's text typing
// (`face talk`), the game's pattern animation (`face ds`) unless what it would cover is `face closed`, its speaker's last
// portrait (`face portrait`), the KH2 motion's ranges (id 23) --, the motion's triggers passed (4 a default, 5 the file's
// back), then the frames since the last played. A block whose sprite changes is patched into each copy of its texture (KH2
// sends the texture, then the sprites over it: kn::ModelSKL::setTexture)
void play_face(Model &m)
{
    if (m.face_blocks.empty())
        return;
    const u32 now = khdays_events_vblank_count();
    const u32 frames = m.face_clock ? now - m.face_vblank : 1U;
    if (m.face_clock && frames == 0)
        return; // drawn again this frame: another actor, or a part
    if (m.face_clock && frames > 300U)
        m.face_portrait = -1; // not drawn for 5 seconds: another scene
    m.face_clock = true;
    m.face_vblank = now;
    if (std::find(m.face.speakers.begin(), m.face.speakers.end(), dialogue_speaker) != m.face.speakers.end())
        m.face_portrait = dialogue_speaker;
    if (khdays_link_paused())
        return;
    const FacePortrait *portrait = nullptr;
    for (const FacePortrait &shown : m.face.portraits)
        if (shown.portrait == m.face_portrait)
            portrait = &shown;
    const int talk = portrait != nullptr && portrait->talk >= 0 ? portrait->talk : m.face.talk;
    const bool talking =
        talk >= 0 && dialogue_typing && now - dialogue_typed <= 6U &&
        std::find(m.face.speakers.begin(), m.face.speakers.end(), dialogue_speaker) != m.face.speakers.end();
    int ds_slot = -1;
    for (const auto &[pattern, slot] : m.face.ds)
        if (std::find(materials_playing.begin(), materials_playing.end(), pattern) != materials_playing.end())
            ds_slot = slot;
    const auto *attributes = m.face.ranges ? face_motion.attributes : nullptr;
    const float ticks = face_motion.ticks, before = face_motion.before;
    for (auto &block : m.face_blocks) {
        khdays::face::Player &player = *block->player;
        int held = -1;
        if (attributes != nullptr) {
            for (const auto &t : attributes->triggers) {
                const auto f = static_cast<float>(t.frame);
                const bool passed = before < 0.0F     ? f <= ticks
                                    : ticks >= before ? f > before && f <= ticks
                                                      : f > before || f <= ticks; // looped
                if (passed && t.id == 4 && !t.params.empty())
                    player.set_default(t.params[0]);
                else if (passed && t.id == 5)
                    player.reset_default();
            }
            for (const auto &r : attributes->ranges)
                if (r.id == 23 && !r.params.empty() && static_cast<float>(r.start) <= ticks &&
                    ticks <= static_cast<float>(r.end) && player.knows(r.params[0]))
                    held = r.params[0];
        }
        if (portrait != nullptr)
            for (const int slot : portrait->slots) {
                const int s = slot == -2 ? block->last_slot : slot;
                if (s >= 0 && player.knows(s))
                    held = s;
            }
        if (ds_slot >= 0 && player.knows(ds_slot) &&
            std::find(m.face.closed.begin(), m.face.closed.end(), held) == m.face.closed.end())
            held = ds_slot;
        if (talking && player.knows(talk))
            held = talk;
        if (held >= 0)
            player.set(held);
        player.play(static_cast<float>(std::min(frames, 30U))); // (a long gap: not drawn, or loading)
    }
    for (FaceTexture &t : m.face_textures) {
        bool changed = false;
        for (const auto &block : m.face_blocks)
            changed = changed || (block->animation.texture == t.key && block->player->sprite() != block->shown);
        if (!changed)
            continue;
        auto *texels = reinterpret_cast<std::uint8_t *>(khdays_textures_change_begin(t.replacement));
        for (const auto &block : m.face_blocks) {
            const auto &a = block->animation;
            if (a.texture != t.key)
                continue;
            const int sprite = block->player->sprite();
            for (int y = std::max(0, -a.y); y < a.height && a.y + y < t.height; ++y)
                for (int x = std::max(0, -a.x); x < a.width && a.x + x < t.width; ++x) {
                    const std::size_t at = 4 * (static_cast<std::size_t>(a.y + y) * static_cast<std::size_t>(t.width) +
                                                static_cast<std::size_t>(a.x + x));
                    if (sprite < 0) {
                        std::memcpy(texels + at, t.picture.data() + at, 4);
                        continue;
                    }
                    const std::uint8_t *px = a.sprites[static_cast<std::size_t>(sprite)].data() +
                                             4 * (static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width) +
                                                  static_cast<std::size_t>(x));
                    std::memcpy(texels + at, px, 3);
                    texels[at + 3] = t.alpha == 1 ? 255 : t.alpha == 2 ? (px[3] >= t.least ? 255 : 0) : px[3];
                }
        }
        khdays_textures_change_end(t.replacement);
    }
    for (auto &block : m.face_blocks)
        block->shown = block->player->sprite();
}

// `held <joint> rest`: a weapon made at rest where the bone holding it is at rest (B_EX390's baked keyblade bones and
// W_EX010's bone 2), so it moves as that bone has moved from rest. `root` is the game's weapon frame, the `weapon`
// line's turn on the bone; the joint, those above it and those under it not on a node of the game's are carried.
void hold_on_bone(Model &m, const Mat &root)
{
    const auto held =
        std::find_if(m.joints.begin(), m.joints.end(), [&](const Joint &j) { return j.name == m.held_name; });
    if (held == m.joints.end())
        return;
    if (!m.held_own_turn && !m.held_rest_checked) {
        m.held_rest_checked = true;
        const auto own = translation(inverse(held->inverse_bind)), bone = translation(holding_bone.rest);
        const float off = length({own[0] - bone[0], own[1] - bone[1], own[2] - bone[2]});
        if (off > 0.01F)
            khdays_log(
                "models: %s: `held %s rest`: its rest is %.1f cm from the holding bone's, so it sits that far off "
                "the hand; give it `held %s` with the body's `weapon_motions`, or a model made at that bone's rest",
                m.name.c_str(), m.held_name.c_str(), static_cast<double>(off * 100.0F), m.held_name.c_str());
    }
    Mat by = kIdentity;
    by[0] = by[5] = by[10] = m.scale;
    const Mat &from_game = m.held_own_turn ? m.held_from_game : holding_bone.from_game;
    const Mat on_bone = mul(root, mul(from_game, mul(by, held->inverse_bind)));
    std::vector<char> carried(m.joints.size(), 0);
    for (int up = static_cast<int>(held - m.joints.begin()); up >= 0;
         up = m.joints[static_cast<std::size_t>(up)].parent)
        carried[static_cast<std::size_t>(up)] = 1;
    for (std::size_t j = 0; j < m.joints.size(); ++j) {
        const int parent = m.joints[j].parent;
        if (carried[j] || (m.joints[j].node < 0 && parent >= 0 && carried[static_cast<std::size_t>(parent)])) {
            carried[j] = 1;
            skin[j] = on_bone;
        }
    }
}

void draw(Model &m)
{
    float pos[16], vecm[16], proj_f[16];
    khdays_g3d_matrices(pos, vecm, proj_f);
    Mat proj;
    std::copy(proj_f, proj_f + 16, proj.begin());
    const std::size_t nodes = m.node_rest_inverse.size();

    // D per node; one the draw did not send takes its parent's (the first sent for a root)
    node_move.assign(nodes, kIdentity);
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
                node_move[g] = mul(node_matrix[g], m.node_rest_inverse[g]);
            } else if (p >= 0 && static_cast<std::size_t>(p) < nodes && p != static_cast<int>(g)) {
                if (!done[static_cast<std::size_t>(p)])
                    continue;
                node_move[g] = node_move[static_cast<std::size_t>(p)];
            } else {
                node_move[g] = mul(node_matrix[static_cast<std::size_t>(first)],
                                   m.node_rest_inverse[static_cast<std::size_t>(first)]);
            }
            done[g] = 1;
        }
    }
    const Mat &root = node_move[static_cast<std::size_t>(first)];
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
            const Mat &D = node_move[g];
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
            Mat l = node_move[g];
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
    if (m.own) {
        moved = m.own->world_now;
    } else if (m.own_motion) {
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

    // each joint, parents first; `ds_pose` turns by the game's animation with the most weight that plays no
    // KH2 motion (its own lines, else the default's)
    std::vector<const Mat *> ds_turn(m.joints.size(), nullptr);
    if (!m.ds_turns.empty()) {
        const Playing *game_anim = nullptr;
        for (const Playing &p : playing) {
            const auto s = m.kh2_sections.find(p.name);
            const bool kh2 =
                m.kh2_motions && s != m.kh2_sections.end() && !s->second.empty() && s->second.front().from <= p.frame;
            if (!kh2 && (game_anim == nullptr || p.ratio > game_anim->ratio))
                game_anim = &p;
        }
        auto turns = game_anim != nullptr ? m.ds_turns.find(game_anim->name) : m.ds_turns.end();
        if (turns == m.ds_turns.end())
            turns = m.ds_turns.find("");
        if (turns != m.ds_turns.end())
            for (const auto &[j, turn] : turns->second)
                ds_turn[j] = &turn;
    }
    joint_place.resize(m.joints.size());
    skin.resize(m.joints.size());
    const auto pose = [&](std::size_t j) {
        const Joint &joint = m.joints[j];
        const Mat *parent = joint.parent >= 0 ? &joint_place[static_cast<std::size_t>(joint.parent)] : nullptr;
        if (joint.node >= 0 && joint.exact)
            return node_move[static_cast<std::size_t>(joint.node)];
        if (joint.node >= 0) {
            const Mat &dg = node_move[static_cast<std::size_t>(joint.node)];
            if (joint.placed) { // on the game's node itself (its offset turned as the node is)
                const auto &at = m.node_rest_place[static_cast<std::size_t>(joint.node)];
                auto where = point(
                    dg, {at[0] + joint.place_offset[0], at[1] + joint.place_offset[1], at[2] + joint.place_offset[2]});
                if (joint.rides >= 0) {
                    const auto r = static_cast<std::size_t>(joint.rides);
                    const auto by = static_cast<std::size_t>(m.joints[r].node);
                    const auto bone = point(joint_place[r], m.joints[r].head), node = point(node_move[by], m.node_rest_place[by]);
                    for (std::size_t c = 0; c < 3; ++c)
                        where[c] += bone[c] - node[c];
                }
                return place(where, dg, joint.correction, joint.head);
            }
            if (joint.travels) { // a mod's character's: carried, then moved as its node travels, at its size
                const auto &at_rest = m.node_rest_place[static_cast<std::size_t>(joint.node)];
                const Mat &from =
                    joint.travel_from >= 0 ? node_move[static_cast<std::size_t>(joint.travel_from)] : root;
                const auto now = point(dg, at_rest), carried = point(from, at_rest);
                auto at = point(parent != nullptr ? *parent : root, joint.head);
                for (std::size_t c = 0; c < 3; ++c)
                    at[c] += joint.size * (now[c] - carried[c]);
                return place(at, dg, joint.correction, joint.head);
            }
            return place(point(parent != nullptr ? *parent : dg, joint.head), dg, joint.correction, joint.head);
        }
        const Mat &carried = parent != nullptr ? *parent : root;
        return ds_turn[j] != nullptr ? mul(carried, *ds_turn[j]) : carried;
    };
    for (std::size_t j = 0; j < m.joints.size(); ++j)
        joint_place[j] = pose(j);
    if (m.spine.end >= 0)
        bend_spine(m, pose);
    for (const auto &limb : m.limbs) {
        bool placed = false;
        for (const int j : limb.joints)
            placed = placed || m.joints[static_cast<std::size_t>(j)].placed;
        if (!placed) // a placed limb is where the game's is: nothing to reach
            reach(m, limb, pose);
    }
    // the bones above placed joints swung and stretched along themselves to reach them
    for (const Stretch &st : m.stretches) {
        const auto p = static_cast<std::size_t>(st.joint), c = static_cast<std::size_t>(st.child);
        const auto hp = point(joint_place[p], m.joints[p].head), rigid = point(joint_place[p], m.joints[c].head),
                   actual = point(joint_place[c], m.joints[c].head);
        const Mat x = turned_onto(hp, {rigid[0] - hp[0], rigid[1] - hp[1], rigid[2] - hp[2]},
                                  {actual[0] - hp[0], actual[1] - hp[1], actual[2] - hp[2]});
        for (const int r : st.riders)
            joint_place[static_cast<std::size_t>(r)] = mul(x, joint_place[static_cast<std::size_t>(r)]);
    }
    if (!m.hands.empty())
        pose_hands(m);
    for (std::size_t j = 0; j < m.joints.size(); ++j)
        if (m.joints[j].after_rides)
            joint_place[j] = pose(j);
    for (std::size_t j = 0; j < m.joints.size(); ++j) {
        const Joint &joint = m.joints[j];
        const Mat *w = moved_world(joint, moved);
        skin[j] = w != nullptr ? mul(scaled_root, mul(*w, joint.inverse_bind)) : mul(joint_place[j], joint.bind);
    }
    // bones.txt's `held`: a weapon's bones from its root to the weapon itself where the body holding it puts them
    // (its `weapon_motions` pose, from the frame the game draws the weapon at), the bones under them carried
    // (a chain, for the cloth); a body unseen for a second leaves the model as the game's nodes place it
    const u32 held_now = khdays_events_vblank_count();
    const bool on_bone =
        !m.held_name.empty() && m.held_rest && (m.held_own_turn || held_now - holding_bone.vblank < 60U);
    if (on_bone) {
        hold_on_bone(m, root);
    } else if (!m.held_name.empty() && !m.held_rest && !held_weapon.pose.empty() &&
               held_now - held_weapon.vblank < 60U) {
        if (m.held_bone.size() != m.joints.size()) { // the held joint and those above it, by their KH2 bones
            m.held_bone.assign(m.joints.size(), -1);
            for (std::size_t j = 0; j < m.joints.size(); ++j)
                if (m.joints[j].name == m.held_name)
                    for (int up = static_cast<int>(j); up >= 0; up = m.joints[static_cast<std::size_t>(up)].parent) {
                        const std::string &name = m.joints[static_cast<std::size_t>(up)].name;
                        if (name.rfind("kh2_", 0) == 0)
                            m.held_bone[static_cast<std::size_t>(up)] = std::atoi(name.c_str() + 4);
                    }
        }
        Mat by = kIdentity;
        by[0] = by[5] = by[10] = m.scale;
        std::vector<char> moved_joint(m.joints.size(), 0);
        for (std::size_t j = 0; j < m.joints.size(); ++j) {
            const int b = m.held_bone[j], parent = m.joints[j].parent;
            if (b >= 0 && static_cast<std::size_t>(b) < held_weapon.pose.size()) {
                skin[j] = mul(mul(root, mul(held_weapon.from_game, held_weapon.pose[static_cast<std::size_t>(b)])),
                              mul(by, m.joints[j].inverse_bind));
                moved_joint[j] = 1;
            } else if (m.joints[j].node < 0 && parent >= 0 && moved_joint[static_cast<std::size_t>(parent)]) {
                skin[j] = skin[static_cast<std::size_t>(parent)];
                moved_joint[j] = 1;
            }
        }
    }
    // KH2 motions on the game's root node (its first: roxas_R, never animated), whatever bone 0 follows
    face_motion = {};
    kh2_game_share = 1.0F;
    kh2_pose_cut = false;
    kh2_played.clear();
    kh2_weapon_pose.clear();
    kh2_weapon_share = 1.0F;
    if (m.kh2_motions && !m.kh2_sections.empty())
        play_kh2(m, scaled_root);
    play_face(m);
    // bones.txt's `twist`: those joints (and the ones under them) turned with the other joint's twist, before the
    // cloth reads the pose
    for (const Twist &t : m.twists) {
        namespace kh2 = khdays::assets::kh2;
        const auto rest = [&](std::size_t j) { return inverse(m.joints[j].inverse_bind); };
        const auto now = [&](std::size_t j) { return mul(skin[j], rest(j)); };
        if (t.swing) {
            const Mat frame_now = mul(axes_of(now(t.to)), inverse(axes_of(rest(t.to))));
            const auto kept = vec(frame_now, vec(axes_of(rest(t.by)), {1.0F, 0.0F, 0.0F}));
            auto gone = vec(axes_of(now(t.by)), {1.0F, 0.0F, 0.0F});
            if (t.except) {
                // from the joint's own frame: a `twist` has already turned it against `frame`
                const auto way = vec(mul(axes_of(now(t.joint)), inverse(axes_of(rest(t.joint)))), t.except_way);
                gone = khdays::models::cuff_swing_way(kept, gone,
                                                      khdays::models::hand_unit(khdays::models::hand_add(
                                                          way, kept, -khdays::models::hand_dot(way, kept))));
            }
            const auto toward = khdays::models::hand_unit(khdays::models::hand_add(
                khdays::models::hand_add({0.0F, 0.0F, 0.0F}, kept, 1.0F - t.share), gone, t.share));
            const Mat at = now(t.joint);
            const Mat turn = turned_onto({at[12], at[13], at[14]}, kept, toward);
            for (const std::size_t j : t.turned)
                skin[j] = mul(turn, skin[j]);
            continue;
        }
        const Mat parent_rest = rest(t.parent), parent_now = now(t.parent), to = now(t.to);
        const float angle = t.share * kh2::twist_about(parent_rest, rest(t.by), parent_now, now(t.by), t.axis);
        const Mat turn = kh2::twist_turn(parent_rest, parent_now, t.axis, angle, {to[12], to[13], to[14]});
        for (const std::size_t j : t.turned)
            skin[j] = mul(turn, skin[j]);
    }
    pose_cuffs(m);
    // secondary motion over the finished pose (the game's or the KH2 motions', blended), before skinning
    if (m.secondary) {
        khdays::secondary::DrawInput in;
        in.actor = drawn_object;
        in.root = scaled_root;
        in.start = drawn_start;
        in.base = drawn_base;
        in.base_kind = drawn_base_kind;
        in.camera = drawn_camera;
        in.has_camera = drawn_has_camera;
        in.projection = proj_f;
        in.game_nodes = &node_move;
        in.pose_cut = kh2_pose_cut;
        for (const Playing &p : playing) {
            khdays::secondary::DrawAnimation a{p.name, -1, -1.0F, p.frame, p.frames, p.ratio};
            if (const auto played = kh2_played.find(p.name); played != kh2_played.end()) {
                a.slot = static_cast<int>(played->second.first);
                a.section = played->second.second;
            }
            in.animations.push_back(std::move(a));
        }
        khdays::secondary::apply_model_secondary(*m.secondary, in, skin);
    }
    // a joint's frame as drawn, without bones.txt's `scale`: what the game attaches to it keeps the game's size
    Mat unscale = kIdentity;
    unscale[0] = unscale[5] = unscale[10] = m.scale > 0.0F ? 1.0F / m.scale : 1.0F;
    const auto joint_frame = [&](std::size_t j) {
        return mul(mul(skin[j], inverse(m.joints[j].inverse_bind)), unscale);
    };
    // the frame a weapon is drawn at on joint j for node g: the line's turn; with `weapon_motions` the weapon itself
    // turned in the hand as its motion turns it from its held pose (slot 0's first frame: the line's turn alone,
    // KH2's idle), and the turn from it to the weapon's root kept for a `held` weapon model
    const std::vector<Mat> &weapon_pose = kh2_weapon_pose.empty() ? m.weapon_rest : kh2_weapon_pose;
    const float weapon_share = m.weapon_motions && !kh2_weapon_pose.empty() ? kh2_weapon_share : kh2_game_share;
    const auto hold = [&](std::size_t j, std::size_t g) {
        const Mat turn = g < m.kh2_weapon_turn.size() ? m.kh2_weapon_turn[g] : kIdentity;
        holding_bone = {inverse(turn), inverse(m.joints[j].inverse_bind), khdays_events_vblank_count()};
        if (!m.weapon_motions || static_cast<std::size_t>(m.weapon_blade) >= weapon_pose.size())
            return mul(joint_frame(j), turn);
        const auto b = static_cast<std::size_t>(m.weapon_blade);
        const Mat weapon = mul(weapon_pose[b], mul(inverse(m.weapon_rest[b]), turn));
        held_weapon.pose = weapon_pose;
        held_weapon.from_game = inverse(weapon);
        held_weapon.vblank = khdays_events_vblank_count();
        return mul(joint_frame(j), weapon);
    };
    // a joint placed on its weapon's own node is already the weapon's frame in every mix, as KH2 hangs a weapon on the
    // holder's mixed bone: turning it toward the game's again by its share would mix the game's pose in twice
    const auto own_place = [&](std::size_t j, std::size_t g) {
        return !m.weapon_motions && m.joints[j].placed && m.joints[j].node == static_cast<int>(g);
    };
    // `weapon_model`: where the bone is now for the game's weapon object, which may be drawn before or after the body
    if (m.weapon_model_joint >= 0 && m.weapon_model_hand >= 0 && !m.weapon_model_file.empty()) {
        weapon_model_hold.file = m.weapon_model_file;
        weapon_model_hold.node = m.weapon_model_node;
        weapon_model_hold.hand = joint_frame(static_cast<std::size_t>(m.weapon_model_hand));
        weapon_model_hold.grip = mul(m.weapon_model_rel, m.weapon_model_turn);
        weapon_model_hold.grip_valid = m.weapon_model_rel_valid;
        weapon_model_hold.kh2_share = std::clamp(1.0F - kh2_game_share, 0.0F, 1.0F);
        weapon_model_hold.vblank = khdays_events_vblank_count();
    }
    // `weapon <node> hand <bone>`: the game's frame for what it attaches there, carried by the KH2 hand as the game's
    // hand node carries it, so it stays in the fist in KH2 motions; one the game sends away from its hand (a throw)
    // is only moved by how far the KH2 hand stands from the game's, from 0.6 to 1.5 forearms away, so a knife metres
    // off does not swing round as the hand turns
    const auto carried = [&](std::size_t g, const Mat &game) {
        const auto j = static_cast<std::size_t>(m.kh2_weapon_hand[g]);
        const auto up = static_cast<std::size_t>(m.kh2_weapon_hand_up[g]);
        const Mat &game_hand = node_matrix[static_cast<std::size_t>(m.joints[j].node)];
        const Mat hand = mul(joint_frame(j), m.joints[j].to_node); // the game's hand node where the KH2 hand is
        const Mat rigid = mul(hand, mul(inverse(game_hand), game));
        const Mat forearm = joint_frame(up), kh2_hand = joint_frame(j);
        float reach = 0.0F, away = 0.0F;
        for (std::size_t c = 12; c < 15; ++c) {
            reach += (kh2_hand[c] - forearm[c]) * (kh2_hand[c] - forearm[c]);
            away += (game[c] - game_hand[c]) * (game[c] - game_hand[c]);
        }
        reach = std::sqrt(reach);
        away = std::sqrt(away);
        const float t = std::clamp((away - 0.6F * reach) / std::fmax(0.9F * reach, 1e-6F), 0.0F, 1.0F);
        const float loose = t * t * (3.0F - 2.0F * t);
        Mat out = khdays::models::attached_frame(rigid, game, loose);
        for (std::size_t c = 12; c < 15; ++c)
            out[c] = rigid[c] + (game[c] + hand[c] - game_hand[c] - rigid[c]) * loose;
        return out;
    };
    const auto is_carried = [&](std::size_t g) {
        return g < m.kh2_weapon_hand.size() && m.kh2_weapon_hand[g] >= 0 &&
               static_cast<std::size_t>(m.joints[static_cast<std::size_t>(m.kh2_weapon_hand[g])].node) <
                   node_matrix.size();
    };
    // node g's frame where the model's joint carries it: its frame under that joint's node at rest; a mod's character's
    // as the game moves it under that node now (a weapon node turning in the hand), its place at the joint's size
    const auto on_joint = [&](std::size_t g) {
        const auto j = static_cast<std::size_t>(m.node_joint[g]);
        Mat local = m.node_local[g];
        if (m.attached_on_joints) {
            const auto a = static_cast<std::size_t>(m.joints[j].node);
            local = mul(inverse(mul(node_move[a], inverse(m.node_rest_inverse[a]))),
                        mul(node_move[g], inverse(m.node_rest_inverse[g])));
            for (std::size_t c = 12; c < 15; ++c)
                local[c] *= m.joints[j].size;
        }
        return mul(mul(joint_frame(j), m.joints[j].to_node), local);
    };
    // a mod's character's hands' weapons at its weapon_scale
    const auto weapon_sized = [&](std::size_t g, Mat at) {
        if (g < m.weapon_node.size() && m.weapon_node[g] && m.weapon_scale != 1.0F)
            for (std::size_t c = 0; c < 11; ++c)
                at[c] *= m.weapon_scale;
        return at;
    };
    // what the game attaches to a node (its weapons) on the joint a `weapon` line names, as it is drawn, or (a mod's
    // character's paired skeleton) where the model's joint carries the node: its saved matrix (MtxFx43, the same space
    // as the nodes') written over
    for (const auto &[saved, node] : saved_joints) {
        const bool hand_held = is_carried(node);
        const bool line = !hand_held && node < m.kh2_weapon.size() && m.kh2_weapon[node] >= 0;
        if (!hand_held && !line && !(m.attached_on_joints && node < m.node_joint.size() && m.node_joint[node] >= 0))
            continue;
        s32 fx[12];
        std::memcpy(fx, saved, sizeof(fx)); // the game's own frame for it, as it saved it
        Mat game = kIdentity;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 3; ++r)
                game[static_cast<std::size_t>(c * 4 + r)] = static_cast<float>(fx[c * 3 + r]) / 4096.0F;
        const auto j = line ? static_cast<std::size_t>(m.kh2_weapon[node]) : 0U;
        const Mat at = weapon_sized(
            node, hand_held             ? carried(node, game)
                  : !line               ? on_joint(node)
                  : own_place(j, node) ? hold(j, node)
                                       : khdays::models::attached_frame(hold(j, node), game, weapon_share));
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 3; ++r)
                fx[c * 3 + r] = static_cast<s32>(std::lround(at[static_cast<std::size_t>(c * 4 + r)] * 4096.0F));
        std::memcpy(saved, fx, sizeof(fx));
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
            khdays_log("models:   K %zu %s %s", j, m.joints[j].name.c_str(), line(joint_place[j]).c_str());
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
    // what the game attaches to its nodes (NNS_G3dGetResultMtx after the draw): each node's frame where the model's
    // joint is (a `weapon` line's turn for its node), for result_matrix
    if (drawn_object != nullptr) {                  // (a model at a place of the port's has no render object)
        auto &results = node_results[drawn_object]; // (forgotten with its object: forget_gone)
        results.assign(nodes, {false, kIdentity});
        for (std::size_t g = 0; g < nodes && g < m.node_joint.size(); ++g) {
            if (is_carried(g) && g < node_matrix.size()) {
                results[g] = {true, carried(g, node_matrix[g])};
            } else if (g < m.kh2_weapon.size() && m.kh2_weapon[g] >= 0) {
                const auto j = static_cast<std::size_t>(m.kh2_weapon[g]);
                results[g] = {true, own_place(j, g)
                                        ? hold(j, g)
                                        : khdays::models::attached_frame(hold(j, g), node_matrix[g], weapon_share)};
            } else if (m.node_joint[g] >= 0) {
                results[g] = {true, on_joint(g)};
            }
        }
    }
    // bones.txt's mesh scales, last: the cloth and the weapons were placed on the unscaled bones
    for (const auto &[j, by] : m.mesh_scales)
        if (j < skin.size())
            skin[j] = mul(skin[j], by);
    const Mat unskinned = scaled_root; // a mesh with no skin: placed with the model's first node

    // `clip_world`: the coat's closed volume, not drawn; the room inside it is left out, this model's own
    // polygons drawn there (docs/RENDERING.md)
    clip_triangles.clear();
    if (m.secondary)
        khdays::secondary::clip_volumes(*m.secondary, skin, clip_triangles);
    for (const auto &t : clip_triangles) {
        KhdaysG3dClipVertex tri[3] = {};
        for (int c = 0; c < 3; ++c) {
            const auto &p = t[static_cast<std::size_t>(c)];
            KhdaysG3dClipVertex &o = tri[c];
            o.x = proj[0] * p.x + proj[4] * p.y + proj[8] * p.z + proj[12];
            o.y = proj[1] * p.x + proj[5] * p.y + proj[9] * p.z + proj[13];
            o.z = proj[2] * p.x + proj[6] * p.y + proj[10] * p.z + proj[14];
            o.w = proj[3] * p.x + proj[7] * p.y + proj[11] * p.z + proj[15];
            o.a = 1.0F;
        }
        khdays_g3d_clip_polygon(tri);
    }
    khdays_g3d_set_clip_owner(!clip_triangles.empty());

    // the polygons' attributes: the game's material's, the faces the mesh's. A mesh drawn whole (OPAQUE or
    // MASK) is drawn opaque when the game's material is all but opaque: the rooms are drawn at alpha 30 of
    // 31, and a translucent pixel takes no other translucent polygon of the same ID (the DS's rule), so a
    // mod's room, one ID, would show only what it drew first. A material fading
    // further keeps its alpha.
    const uint32_t tex = (7u << 26) | (7u << 23) | (7u << 20) | (1u << 17) | (1u << 16); // 1024x1024, repeated
    const uint32_t game_alpha = (material.attr >> 16) & 31u;
    // what lies wholly past a side of the view is left out (unless [video] culling is off): whole meshes by their
    // boxes, then triangles
    float side_x = 0.0F, side_y = 0.0F;
    const bool cull = (khdays_host_graphics_flags() & KHDAYS_HOST_NO_CULLING) == 0 &&
                      khdays_g3d_mod_cull_sides(&side_x, &side_y) != 0;
    std::size_t left_out = 0;
    for (const Mesh &mesh : m.meshes) {
        if (mesh.shadow_sprite && khdays_shadow_has_sprite())
            continue;
        // its picture now, and how far its texture has scrolled
        const Mesh::Picture picture = picture_now(mesh, m.frames);
        const float tex_w = picture.w, tex_h = picture.h;
        const auto offset = khdays::room::scroll_offset(mesh.scroll, m.frames, tex_w, tex_h);
        uint32_t attr = (material.attr & ~0xc0u) | (mesh.double_sided ? 0xc0u : 0x80u);
        if (!picture.translucent && game_alpha >= 30u)
            attr = (attr & ~(31u << 16)) | (31u << 16);
        // a piece wholly past a side of the view: left out before any of its vertices is placed (a big room's
        // pieces behind and beside the camera)
        if (cull && mesh.boxed) {
            const Mat &s = mesh.box_joint < skin.size() ? skin[mesh.box_joint] : unskinned;
            unsigned past = 0xfu;
            for (std::size_t c = 0; c < 8 && past != 0; ++c) {
                const auto p = point(s, {(c & 1) != 0 ? mesh.box_high[0] : mesh.box_low[0],
                                         (c & 2) != 0 ? mesh.box_high[1] : mesh.box_low[1],
                                         (c & 4) != 0 ? mesh.box_high[2] : mesh.box_low[2]});
                past &= past_sides(proj[0] * p[0] + proj[4] * p[1] + proj[8] * p[2] + proj[12],
                                   proj[1] * p[0] + proj[5] * p[1] + proj[9] * p[2] + proj[13],
                                   proj[3] * p[0] + proj[7] * p[1] + proj[11] * p[2] + proj[15], side_x, side_y);
            }
            if (past != 0) {
                khdays_g3d_mod_skip(attr, mesh.drawn_triangles);
                ++left_out;
                continue;
            }
        }
        clip.resize(mesh.vertices.size());
        clip_past.resize(mesh.vertices.size());
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
            clip_past[i] = cull ? static_cast<unsigned char>(past_sides(o.x, o.y, o.w, side_x, side_y)) : 0;
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
        // each triangle, but one wholly past a side of the view (its corners all past the same one): counted, not made
        std::uint32_t past_view = 0;
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            uint32_t tri_attr;
            if (!triangle_attr(mesh, t, attr, tri_attr))
                continue;
            if ((clip_past[mesh.indices[t]] & clip_past[mesh.indices[t + 1]] & clip_past[mesh.indices[t + 2]]) != 0) {
                ++past_view;
                continue;
            }
            if (past_view != 0) {
                khdays_g3d_mod_skip(tri_attr, past_view);
                past_view = 0;
            }
            const KhdaysG3dClipVertex tri[3] = {clip[mesh.indices[t]], clip[mesh.indices[t + 1]],
                                                clip[mesh.indices[t + 2]]};
            khdays_g3d_mod_polygon(tri, 3, tri_attr, tex, picture.replacement,
                                   picture.translucent || mesh.vertex_alpha == VertexAlpha::blended, mesh.sky);
        }
        if (past_view != 0)
            khdays_g3d_mod_skip(attr, past_view);
    }
    if (trace != 0 && khdays_log_on() && (m.draws == 120 || m.draws == 600))
        khdays_log("models: %s draw %d: %zu of its %zu meshes past the view, left out", m.name.c_str(), m.draws,
                   left_out, m.meshes.size());
    khdays_g3d_set_clip_owner(0);
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

// The joint animations `render_obj` plays: NNSG3dRenderObj anmJnt (+0x10), each NNSG3dAnmObj's
// frame (+0, fx32), ratio (+4, fx32), resAnm (+8: its J0AC) and next (+0x10).
void read_playing(const void *render_obj)
{
    playing.clear();
    materials_playing.clear();
    if (render_obj == nullptr)
        return;
    u32 mat = 0; // anmMat (+0x08): the material animations, the eyes' pattern among them
    std::memcpy(&mat, static_cast<const unsigned char *>(render_obj) + 0x08, 4);
    each_linked(mat, 0x10U, 0x14U, [&](KhdaysRef at) {
        u32 anm = 0;
        std::memcpy(&anm, KHDAYS_W(at + 8U), 4);
        materials_playing.push_back(animation_name(anm));
        return true;
    });
    u32 obj = 0;
    std::memcpy(&obj, static_cast<const unsigned char *>(render_obj) + 0x10, 4);
    each_linked(obj, 0x10U, 0x14U, [&](KhdaysRef at) {
        const auto *a = static_cast<const unsigned char *>(KHDAYS_W(at));
        s32 frame = 0, ratio = 0;
        u32 anm = 0;
        std::memcpy(&frame, a, 4);
        std::memcpy(&ratio, a + 4, 4);
        std::memcpy(&anm, a + 8, 4);
        const std::string &name = animation_name(anm);
        u16 frames = 0;
        if (anm != 0)
            std::memcpy(&frames, static_cast<const unsigned char *>(KHDAYS_W(anm)) + 4, 2);
        playing.push_back({name, static_cast<float>(frame) / 4096.0F, static_cast<float>(ratio) / 4096.0F,
                           static_cast<float>(frames)});
        return true;
    });
}

// What is kept by the game's objects (each render object's last draw, its node's mover and state, its nodes' frames
// and its KH2 actor) is forgotten once the object is gone: undrawn for a minute (its memory freed or another's now),
// or for a KH2 actor undrawn for kActorGap (it starts afresh then anyway). Looked over every ten seconds.
void forget_gone(u32 now)
{
    constexpr u32 kGone = 3600, kEvery = 600; // vblanks
    static u32 looked = 0;
    if (now - looked < kEvery)
        return;
    looked = now;
    std::erase_if(drawn_lately, [&](const auto &e) { return now - e.second > kGone; });
    std::erase_if(grounds, [&](const auto &e) { return now - e.second.vblank > kGone; });
    // a node's render object is 0x24 after it (the mover's link, khdays_models_actor_state)
    std::erase_if(actor_states, [&](const auto &e) { return drawn_lately.count(e.first + 0x24U) == 0; });
    std::erase_if(node_results, [&](const auto &e) { return drawn_lately.count(KHDAYS_REF(e.first)) == 0; });
    const unsigned frame = khdays_link_frame();
    std::erase_if(kh2_actors, [&](const auto &e) { return frame - e.second.drawn_frame > kActorGap; });
}

} // namespace khdays::model_parts

using namespace khdays::model_parts;

extern "C" void khdays_walk_model_draw(unsigned model_resource); /* runtime/input/walk.h */
extern "C" void khdays_walk_model_draw_end(void);

extern "C" void khdays_models_draw_begin(const void *resource, const void *render_obj)
{
    if (render_obj != nullptr)
        drawn_lately[KHDAYS_REF(render_obj)] = khdays_events_vblank_count();
    if (depth == 0) {
        forget_gone(khdays_events_vblank_count());
        begun_object = render_obj;
        khdays_g3d_draw_begin(resource); /* the polygons' key */
        if (resource != nullptr) {
            khdays_walk_model_draw(KHDAYS_REF(resource)); /* the Walk mod: the player's weapon to the hand */
        }
    }
    if (depth > 0) {
        if (outer.size() <= static_cast<std::size_t>(depth))
            outer.resize(static_cast<std::size_t>(depth) + 1);
        outer[static_cast<std::size_t>(depth)] = active;
        ++depth;
        active = nullptr;
        return;
    }
    ++depth;
    drawn_animation = next_animation;
    next_animation = 0;
    const bool hd = khdays_textures_hd() != 0;
    if (resource == nullptr) {
        return;
    }
    corridor_drawn = khdays_kh2_corridors_begin(resource, render_obj) != 0;
    if (corridor_drawn) {
        return; /* KH2's corridor drawn in its place, with or without HD graphics */
    }
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    if (!khdays_origin_of_ram(KHDAYS_REF(resource), &file, &offset, &run)) { // its DS address
        return;
    }
    // a `weapon_model` line's weapon, its body drawn lately: its node of the line's name, placed in khdays_models_node_placed
    weapon_model_drawing = false;
    weapon_model_node_index = -1;
    if (!weapon_model_hold.file.empty() && khdays_events_vblank_count() - weapon_model_hold.vblank < 60U &&
        std::strstr(file, weapon_model_hold.file.c_str()) != nullptr) {
        // its node of the line's name, from the model's node dictionary (NNSG3dResMdl: numNode at +0x17, the
        // NNSG3dResDict of its nodes at +0x40, after the 0x2c bytes of its info at +0x14; read from Zexion's books:
        // its entries' header at +6, their names at the header's +2, 16 bytes each): the file's other models (pages,
        // effects, swing traces) have none and are left alone
        const u32 mdl = KHDAYS_REF(resource), dict = mdl + 0x40U;
        if (khdays_game_ram(mdl) && khdays_game_ram(dict + 8U)) {
            const u32 count = *static_cast<const std::uint8_t *>(KHDAYS_W(dict + 1U));
            const u32 header = dict + *static_cast<const std::uint16_t *>(KHDAYS_W(dict + 6U));
            const u32 names =
                khdays_game_ram(header + 4U) ? header + *static_cast<const std::uint16_t *>(KHDAYS_W(header + 2U)) : 0U;
            for (u32 i = 0; names != 0U && i < count && khdays_game_ram(names + 16U * i + 15U); ++i) {
                const char *text = static_cast<const char *>(KHDAYS_W(names + 16U * i));
                if (std::string(text, strnlen(text, 16)) == weapon_model_hold.node) {
                    weapon_model_node_index = static_cast<int>(i);
                    break;
                }
            }
        }
        weapon_model_drawing = weapon_model_node_index >= 0;
        static std::set<std::string> told; // once a model: which of the file's models carry the line's node
        if (told.size() < 256 && told.insert(std::string(file) + "@" + std::to_string(offset)).second)
            khdays_log("models: weapon_model %s@%x: %s", file, offset,
                       weapon_model_node_index >= 0
                           ? ("node " + weapon_model_hold.node + " is " + std::to_string(weapon_model_node_index) +
                              ": placed on the body's bone in KH2 motions")
                                 .c_str()
                           : ("no node " + weapon_model_hold.node + ": left as the game draws it").c_str());
    }
    // HD off: only a mission world's rooms and the mods' characters (their own folders, ba/ch/c<number>/)
    if (!hd && std::strstr(file, "mi/wd/wd_") == nullptr && std::strncmp(file, "ba/ch/c", 7) != 0) {
        return;
    }
    char key[160];
    std::snprintf(key, sizeof(key), "%s@%x", file, offset);
    auto it = models.find(key);
    if (it == models.end()) {
        char name[160], relative[200], path[260];
        std::unique_ptr<Model> loaded;
        const bool named = khdays_names_model(file, offset, name, sizeof(name)) != 0;
        if (named && khdays_characters_model(name, path, sizeof(path))) {
            loaded = load(name, path, file, offset);
            if (loaded) {
                loaded->character = true;
                khdays_log("models: %s drawn from %s, a mod's character", name, path);
            }
        } else if (named && khdays_rooms_model(name, path, sizeof(path))) {
            loaded = load(name, path, file, offset); /* a room a mod adds: its own model */
            if (loaded) {
                loaded->room = true;
                khdays_log("models: %s drawn from %s, a room a mod adds", name, path);
            }
        } else if (named) {
            /* glTF first, then binary glTF, FBX and KH2's */
            for (const char *extension : {".gltf", ".glb", ".fbx", ".mdlx"}) {
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
    if (active != nullptr && !hd && !active->room && !active->character) {
        active = nullptr;
    }
    if (active != nullptr && active->setting >= 0 && !khdays_hud_setting(active->setting)) {
        active = nullptr; /* its switch off: the game's own */
    }
    model_start = static_cast<const unsigned char *>(resource);
    if (active != nullptr && active->own) {
        active->own->world_now = own_pose(*active); /* its pose before its nodes: its weapon's */
    }
    if (active != nullptr) {
        shapes = 0;
        node_matrix.assign(active->node_rest_inverse.size(), kIdentity);
        node_sent.assign(active->node_rest_inverse.size(), 0);
        saved_joints.clear();
        read_playing(render_obj);
        drawn_object = render_obj;
        {
            float start[16], vecm[16], proj[16];
            khdays_g3d_matrices(start, vecm, proj);
            std::copy(start, start + 16, drawn_start.begin());
            float base[16], camera[16];
            drawn_base_kind = khdays_g3d_base(base);
            std::copy(base, base + 16, drawn_base.begin());
            drawn_has_camera = khdays_g3d_camera(camera) != 0;
            if (drawn_has_camera)
                std::copy(camera, camera + 16, drawn_camera.begin());
        }
        if (tracing()) { // what to name in <model>.motions.txt, when it changes
            static std::string said;
            std::string now = active->name + ":";
            for (const Playing &p : playing)
                now += " " + (p.name.empty() ? std::string("(unnamed)") : p.name);
            if (now != said)
                khdays_log("models: %s", now.c_str());
            said = now;
        }
    }
}

extern "C" void khdays_models_draw_animation(unsigned animation)
{
    next_animation = animation;
}

extern "C" void khdays_models_node(unsigned node, int sent)
{
    if (corridor_drawn && depth == 1 && sent)
        khdays_kh2_corridors_node(node);
    if (active == nullptr || !sent || node >= node_matrix.size()) {
        return;
    }
    float pos[16], vecm[16], proj[16];
    khdays_g3d_matrices(pos, vecm, proj);
    std::copy(pos, pos + 16, node_matrix[node].begin());
    node_sent[node] = 1;
}

// A `weapon_model` weapon's node of the line's name, as the game has just placed it (NNSi_G3dFuncSbc_NODEDESC, after the
// joint is sent and before the node's matrix is stored for what hangs from it): both grips are read on the body's hand
// as drawn (the same space, the current matrix: what khdays_models_node reads), the game's from that matrix and KH2's
// from the bone on the hand in the KH2 motions, and the node loaded on the hand by the two mixed by KH2's share, so a
// fade turns it once from one grip to the other; its own scale kept. The game's own animations leave it as it is.
void place_weapon_model()
{
    const float share = weapon_model_hold.kh2_share;
    if (!(share > 0.001F) || !weapon_model_hold.grip_valid)
        return;
    float pos[16], vecm[16], proj[16];
    khdays_g3d_matrices(pos, vecm, proj);
    Mat game;
    std::copy(pos, pos + 16, game.begin());
    // each frame's axes made unit, their lengths kept: the hand's (the body's scale) and the weapon's own
    const auto unit = [](Mat m, std::array<float, 3> &lengths) {
        for (std::size_t c = 0; c < 3; ++c) {
            lengths[c] = length({m[c * 4], m[c * 4 + 1], m[c * 4 + 2]});
            for (std::size_t e = 0; e < 3; ++e)
                m[c * 4 + e] /= lengths[c] > 1e-9F ? lengths[c] : 1.0F;
        }
        return m;
    };
    std::array<float, 3> body{}, own{};
    const Mat hand = unit(weapon_model_hold.hand, body), weapon = unit(game, own);
    const Mat game_grip = mul(inverse(hand), weapon);
    Mat kh2_grip = weapon_model_hold.grip; // the hand's own units (the model's metres): the body's scale on its place
    for (std::size_t e = 0; e < 3; ++e)
        kh2_grip[12 + e] *= body[0];
    Mat grip = khdays::models::attached_frame(game_grip, kh2_grip, share);
    for (std::size_t e = 0; e < 3; ++e)
        grip[12 + e] = game_grip[12 + e] + (kh2_grip[12 + e] - game_grip[12 + e]) * share;
    Mat out = mul(hand, grip);
    for (std::size_t c = 0; c < 3; ++c)
        for (std::size_t e = 0; e < 3; ++e)
            out[c * 4 + e] *= own[c];
    // MTX_MODE 2 (the position and vector matrices), MTX_LOAD_4x4: the DS's rows are these columns
    const uint32_t mode = 2;
    khdays_g3d_command(0x10, &mode);
    uint32_t words[16];
    for (int i = 0; i < 16; ++i)
        words[i] = static_cast<uint32_t>(static_cast<int32_t>(std::lround(out[static_cast<std::size_t>(i)] * 4096.0F)));
    khdays_g3d_command(0x16, words);
}

extern "C" void khdays_models_node_placed(unsigned node, int sent)
{
    if (weapon_model_drawing && sent && static_cast<int>(node) == weapon_model_node_index) {
        place_weapon_model();
        return;
    }
    if (active == nullptr || !active->own || !sent)
        return;
    const OwnSkeleton &o = *active->own;
    const OwnSkeleton::Held *held = nullptr;
    for (const OwnSkeleton::Held &h : o.held) {
        if (h.game_node == static_cast<int>(node))
            held = &h;
    }
    if (held == nullptr || held->bone < 0 || static_cast<std::size_t>(held->bone) >= o.world_now.size())
        return;
    // the model's place: its first node drawn so far, as the joints' (draw)
    int first = -1;
    for (std::size_t g = 0; g < node_sent.size() && first < 0; ++g) {
        if (node_sent[g])
            first = static_cast<int>(g);
    }
    if (first < 0)
        return;
    Mat s = kIdentity;
    s[0] = s[5] = s[10] = active->scale;
    const Mat root =
        mul(node_matrix[static_cast<std::size_t>(first)], active->node_rest_inverse[static_cast<std::size_t>(first)]);
    Mat hand = mul(mul(root, s), mul(o.world_now[static_cast<std::size_t>(held->bone)], held->fix));
    for (std::size_t c = 0; c < 11 && active->weapon_scale != 1.0F; ++c)
        hand[c] *= active->weapon_scale; // [model] weapon_scale: its weapons at that size
    // MTX_MODE 2 (the position and vector matrices), MTX_LOAD_4x4: the DS's rows are these columns
    const uint32_t mode = 2;
    khdays_g3d_command(0x10, &mode);
    uint32_t words[16];
    for (int i = 0; i < 16; ++i)
        words[i] =
            static_cast<uint32_t>(static_cast<int32_t>(std::lround(hand[static_cast<std::size_t>(i)] * 4096.0F)));
    khdays_g3d_command(0x16, words);
}

// Ov022_EnterActorState (through controls_hooks.c's khdays_controls_actor_entered_state): an actor's state as it entered it,
// by its scene node (actor +0x20, whose render object is at +0x24 as the ground's link has it), when it entered it,
// and when it last left state 5 (damage in the air, a launch included: Ov022_ResolveGuardBreakState)
extern "C" void khdays_models_actor_state(int actor, int state)
{
    const auto a = static_cast<KhdaysRef>(actor);
    if (a == 0 || !khdays_game_ram(a + 0x20) || !khdays_game_ram(a + 0x23))
        return;
    KhdaysRef node = 0;
    std::memcpy(&node, KHDAYS_W(a + 0x20), 4);
    if (node == 0)
        return;
    ActorState &s = actor_states[node]; // (forgotten with its object: forget_gone)
    const u32 now = khdays_events_vblank_count();
    if (s.state == kAirDamageState && state != kAirDamageState)
        s.left_air = now;
    s.state = state;
    s.entered = now;
}

extern "C" void khdays_models_actor_ground(void *mover)
{
    KhdaysRef owner = 0;
    std::memcpy(&owner, mover, 4);
    if (owner == 0)
        return;
    grounds[owner] = {KHDAYS_REF(mover), khdays_events_vblank_count()}; // (forgotten unseen: forget_gone)
}

// NNS_G3dGetResultMtx (prepare.py HOOKS): a replaced model's node as the model's joint carries it, in the matrices the
// game reads back after the draw (MtxFx43 position, MtxFx33 direction; either may be null); 0 when not replaced
extern "C" int khdays_models_result_matrix(const void *render_obj, unsigned node, s32 *position, s32 *direction)
{
    const auto it = node_results.find(render_obj);
    if (it == node_results.end() || node >= it->second.size() || !it->second[node].first)
        return 0;
    const Mat &mtx = it->second[node].second;
    if (position != nullptr)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 3; ++r)
                position[c * 3 + r] = static_cast<s32>(std::lround(mtx[static_cast<std::size_t>(c * 4 + r)] * 4096.0F));
    if (direction != nullptr)
        for (int c = 0; c < 3; ++c) {
            const float l = length({mtx[static_cast<std::size_t>(c * 4)], mtx[static_cast<std::size_t>(c * 4 + 1)],
                                    mtx[static_cast<std::size_t>(c * 4 + 2)]});
            for (int r = 0; r < 3; ++r)
                direction[c * 3 + r] = static_cast<s32>(
                    std::lround(l > 0.0F ? mtx[static_cast<std::size_t>(c * 4 + r)] / l * 4096.0F : 0.0F));
        }
    return 1;
}

extern "C" void khdays_models_joint_saved(void *matrix, unsigned node)
{
    // a `weapon` line's node, on a KH2 bone or carried by a hand (`weapon ... hand`), or a mod's character's node on
    // its joint
    if (active != nullptr && depth == 1 &&
        ((node < active->kh2_weapon.size() && active->kh2_weapon[node] >= 0) ||
         (node < active->kh2_weapon_hand.size() && active->kh2_weapon_hand[node] >= 0) ||
         (active->attached_on_joints && node < active->node_joint.size() && active->node_joint[node] >= 0)))
        saved_joints.emplace_back(matrix, node);
}

// The same, the matrix given by its address in the game's memory (the camp menus' model: Ov025/Ov008
// RefreshMatchingMatrices keep it in their object, reached through plain addresses)
extern "C" void khdays_models_joint_saved_at(unsigned matrix, unsigned node)
{
    if (khdays_game_ram(matrix) && khdays_game_ram(matrix + 0x2fU))
        khdays_models_joint_saved(KHDAYS_W(matrix), node);
}

namespace {
// A shape's list with its matrix commands run and its drawing left out: packed words of up to four command
// numbers, each command's parameters after them in order. 1: left out.
int matrices_only(const void *list, unsigned int size)
{
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
} // namespace

extern "C" int khdays_models_shape(const void *list, unsigned int size)
{
    if (active == nullptr) {
        if (!corridor_drawn || depth != 1)
            return 0; /* the game's own (or a draw inside the oval's) */
        khdays_kh2_corridors_shape();
        return matrices_only(list, size);
    }
    if (shapes++ == 0) {
        khdays_g3d_material(&material);
    }
    // a shape of a node the mod's model keeps: the game's own, sent
    if (!active->kept_lists.empty() && model_start != nullptr && list != nullptr &&
        active->kept_lists.count(static_cast<std::uint32_t>(static_cast<const unsigned char *>(list) - model_start)) !=
            0) {
        return 0;
    }
    if (khdays_shadow_sprite_active())
        return 0; // retain the original flat shadow until its animated replacement is ready
    return matrices_only(list, size);
}

extern "C" void khdays_models_dialogue_speaker(int portrait)
{
    dialogue_speaker = portrait;
    dialogue_typing = false;
}

extern "C" void khdays_models_dialogue_typed(void)
{
    dialogue_typed = khdays_events_vblank_count();
    dialogue_typing = true;
}

extern "C" void khdays_models_draw_end(void)
{
    if (depth <= 0) {
        return;
    }
    if (--depth != 0) {
        active = outer[static_cast<std::size_t>(depth)];
        return;
    }
    khdays_walk_model_draw_end();
    weapon_model_drawing = false;
    // its polygons inside the game model's draw: keyed by the draw for the
    // pictures between lists (g3d_interp.c), not among the loose ones, whose
    // number moves with every effect's
    if (active != nullptr && shapes > 0) {
        draw(*active);
    } else if (active == nullptr) {
        node_results.erase(begun_object); /* drawn as the game's: what is attached to it reads the game's nodes */
    }
    khdays_g3d_draw_end();
    active = nullptr;
    corridor_drawn = false;
}

namespace {
// the models drawn at places of the port's, by their file; null: one that cannot be read
std::unordered_map<std::string, std::unique_ptr<Model>> placed_models;

Model *placed(const char *path)
{
    auto it = placed_models.find(path);
    if (it == placed_models.end()) {
        auto model = load(path, path, nullptr, 0);
        if (!model) {
            khdays_log("models: %s cannot be drawn at its place; the message above says why it cannot be read", path);
        } else if (tracing()) {
            std::fprintf(stderr, "models: %s drawn at its place (%zu meshes)\n", path, model->meshes.size());
        }
        it = placed_models.emplace(path, std::move(model)).first;
    }
    return it->second.get();
}
} // namespace

extern "C" int khdays_models_placed_bounds(const char *path, float low[3], float high[3])
{
    const Model *m = placed(path);
    if (m == nullptr || m->meshes.empty())
        return 0;
    for (std::size_t c = 0; c < 3; ++c) {
        low[c] = m->low[c];
        high[c] = m->high[c];
    }
    return 1;
}

extern "C" int khdays_models_draw_placed(const char *path, const float place[16], unsigned int attr)
{
    Model *m = placed(path);
    if (m == nullptr || active != nullptr)
        return 0;
    // draw()'s state: its one node at the place, a material that lights nothing (its own colours), no render object and
    // nothing of the last game model's draw
    drawn_object = nullptr;
    playing.clear();
    materials_playing.clear();
    saved_joints.clear();
    node_matrix.assign(1, kIdentity);
    std::copy(place, place + 16, node_matrix[0].begin());
    node_sent.assign(1, 1);
    material = {};
    material.attr = attr & ~0xfu;
    material.color[0] = material.color[1] = material.color[2] = 31;
    draw(*m);
    return 1;
}
