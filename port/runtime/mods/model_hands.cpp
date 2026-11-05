// A KH2 model's hands and cuffs: grip, contact with the body and the room, the sleeves' cuffs.

#include "model_parts.h"

namespace khdays::model_parts {

// A room's joint that its own animation moves: its node's matrix now (node_worlds), else nullptr.
const Mat *moved_world(const Joint &joint, const std::vector<Mat> &moved)
{
    if (joint.node >= 0 || joint.room_node < 0 || static_cast<std::size_t>(joint.room_node) >= moved.size())
        return nullptr;
    return &moved[static_cast<std::size_t>(joint.room_node)];
}

// The hands something is attached to in this draw (a bit per Model::hands). The drawn object's scene node (its render
// object at +0x20) lists its children: first at +0xc0, next at +0xc4, parent at +0xbc, the joint at +0xc8
// (SceneNode_AttachToModelJoint: Ov023_CmdAttachWeapons, Ov023_Cmd_ParentEntityToEntity). A child counts while its own
// render object was drawn lately.
unsigned held_hands(const Model &m)
{
    const auto word = [](KhdaysRef a, u32 &out) {
        if (!khdays_game_ram(a) || !khdays_game_ram(a + 3U))
            return false;
        std::memcpy(&out, KHDAYS_W(a), 4);
        return true;
    };
    if (drawn_object == nullptr)
        return 0U;
    const u32 now = khdays_events_vblank_count();
    const KhdaysRef node = KHDAYS_REF(drawn_object) - 0x20U;
    unsigned held = 0U;
    u32 first = 0U;
    if (!word(node + 0xc0U, first))
        return 0U;
    each_linked(first, 0xc4U, 0xccU, [&](KhdaysRef child) {
        u32 parent = 0U, joint = 0U;
        if (!word(child + 0xbcU, parent) || parent != node || !word(child + 0xc8U, joint))
            return false; // not a scene node's list (a render object the game keeps elsewhere)
        const std::size_t g = joint & 0xffffU;
        const auto seen = drawn_lately.find(child + 0x20U);
        if (seen != drawn_lately.end() && now - seen->second <= 6U && g < m.node_hand.size()) {
            if (m.node_hand[g] >= 0) {
                held |= 1U << static_cast<unsigned>(m.node_hand[g]);
            } else if (tracing()) {
                static std::set<std::string> told;
                if (told.insert(m.name + "/" + m.node_names[g]).second)
                    khdays_log("models: %s: something is attached to %s, not on a hand", m.name.c_str(),
                               m.node_names[g].c_str());
            }
        }
        return true;
    });
    return held;
}

// How deep a sphere is in the room, world space, and its way out (the push adds up as faces are met); a face only
// counts while `from` (further up the arm) stands in front of it, so a wall the arm is already through is left alone.
float room_depth(const std::vector<khdays::secondary::Triangle> &room, std::array<float, 3> p, float radius,
                 const std::array<float, 3> &from, std::array<float, 3> &push)
{
    namespace sc = khdays::secondary;
    using khdays::models::hand_add;
    float deepest = 0.0F;
    const sc::Vec3 elbow{from[0], from[1], from[2]};
    for (const sc::Triangle &t : room) {
        const std::array<float, 3> at = hand_add(p, push);
        const sc::Vec3 x{at[0], at[1], at[2]};
        if (sc::dot(elbow - t.a, t.n) < 0.0F)
            continue;
        const float side = sc::dot(x - t.a, t.n);
        if (side >= radius || side < -0.2F)
            continue;
        const sc::Vec3 q = sc::closest_on_triangle(x, t.a, t.b, t.c);
        const float dist = sc::length(x - q);
        if (dist >= radius && side >= 0.0F)
            continue;
        if (side < 0.0F && sc::length((x - t.n * side) - q) > 1e-3F)
            continue; // behind its plane, past its edge
        const float deep = side >= 0.0F ? radius - dist : radius - side;
        const sc::Vec3 way = side >= 0.0F && dist > 1e-5F ? (x - q) * (1.0F / dist) : t.n;
        push = hand_add(push, {way.x, way.y, way.z}, deep);
        deepest = std::fmax(deepest, deep);
    }
    return deepest;
}
float body_depth(const Model &m, const std::array<float, 3> &p, float radius, std::array<float, 3> &way, float margins)
{
    float deepest = 0.0F;
    for (std::size_t b = 0; b < m.hand_body.size(); ++b) {
        const Model::HandBody &part = m.hand_body[b];
        const std::array<float, 3> local = point(hand_body_inverse[b], p);
        std::array<float, 3> out{};
        const float deep = part.shape.depth(local, part.margin * margins + radius, out);
        if (deep > deepest) {
            deepest = deep;
            way = khdays::models::hand_unit(vec(joint_place[part.joint], out));
        }
    }
    return deepest;
}

// motions.txt's `hand`: after the arms have reached, each hand moved out of the body and the room, turned onto a surface
// it rests on, and its fingers between their poses, each lifted off what it would go into
void pose_hands(const Model &m)
{
    using namespace khdays::models;
    namespace sc = khdays::secondary;
    float camera[16];
    if (!khdays_g3d_camera(camera))
        return;
    hand_body_inverse.resize(m.hand_body.size());
    for (std::size_t b = 0; b < m.hand_body.size(); ++b)
        hand_body_inverse[b] = inverse(joint_place[m.hand_body[b].joint]);
    Mat view;
    std::copy(camera, camera + 16, view.begin());
    const Mat world = inverse(view);
    Kh2Actor &actor = actor_of(m);
    const u32 now = khdays_events_vblank_count();
    const u32 gap = now - actor.hand_vblank;
    // a body newly drawn takes its shares at once: nothing shows closing the hand
    const bool fresh = actor.hand_states.size() != m.hands.size() || gap > 12U;
    actor.hand_vblank = now;
    actor.hand_states.resize(m.hands.size());
    const float dt = static_cast<float>(gap) / 60.0F;
    const unsigned held = held_hands(m);
    const bool report = tracing() && (fresh || held != actor.hands_held || now / 30U != (now - gap) / 30U);
    actor.hands_held = held;
    // how fast the body travels: a cut's jump does not count
    std::array<float, 3> body_way{};
    float body_speed = 0.0F;
    if (m.hand_walk_joint >= 0) {
        const auto it = std::find(m.kh2_bone.begin(), m.kh2_bone.end(), m.hand_walk_joint);
        if (it != m.kh2_bone.end()) {
            const auto j = static_cast<std::size_t>(it - m.kh2_bone.begin());
            const auto at = point(world, point(joint_place[j], m.joints[j].head));
            if (!fresh && gap > 0U) {
                body_way = {(at[0] - actor.hand_body_at[0]) / dt, (at[1] - actor.hand_body_at[1]) / dt,
                            (at[2] - actor.hand_body_at[2]) / dt};
                body_speed = std::sqrt(body_way[0] * body_way[0] + body_way[2] * body_way[2]);
                if (body_speed > 12.0F) {
                    body_speed = 0.0F;
                    body_way = {};
                }
            }
            if (gap > 0U || fresh)
                actor.hand_body_at = at;
        }
    }
    // the animation with the most weight, for `hand_anim`
    const std::array<int, 2> *forced = nullptr;
    {
        float most = 0.0F;
        for (const Playing &p : playing)
            if (p.ratio > most) {
                most = p.ratio;
                const auto f = m.hand_anim.find(p.name);
                forced = f != m.hand_anim.end() ? &f->second : nullptr;
            }
    }
    std::vector<sc::Triangle> room;
    for (std::size_t i = 0; i < m.hands.size(); ++i) {
        const Model::Hand &h = m.hands[i];
        Kh2Actor::HandState &state = actor.hand_states[i];
        const auto head = [&](std::size_t j) { return point(joint_place[j], m.joints[j].head); };
        // the wrist's bend and the hand in the world, as the animation left them
        const Mat axes = axes_of(mul(joint_place[h.joint], h.frame));
        const auto forearm_way = vec(axes_of(mul(joint_place[h.forearm], h.forearm_frame)), h.forearm_way);
        const float bend = hand_bend_degrees(vec(inverse(axes), forearm_way), h.back);
        // near the `cuff_pose`'s full bend the hand shows its pose as authored: no fist, no lag
        const float pose_hold =
            m.cuff_settings.pose_bend > 0.0F ? cuff_pose_hold(bend / m.cuff_settings.pose_bend) : 0.0F;
        const float pose_straight =
            m.cuff_settings.pose_bend > 0.0F ? cuff_pose_straight(bend / m.cuff_settings.pose_bend) : 0.0F;
        const auto wrist_view = head(h.joint), elbow_view = head(h.forearm), shoulder_view = head(h.upper);
        const auto wrist = point(world, wrist_view), elbow = point(world, elbow_view);
        const auto back = vec(world, vec(axes, h.back));
        const auto palm_view = point(joint_place[h.joint], h.palm);
        const auto palm = point(world, palm_view);
        std::array<float, 3> moving{};
        if (!fresh && gap > 0U)
            moving = {(wrist[0] - state.at[0]) / dt, (wrist[1] - state.at[1]) / dt, (wrist[2] - state.at[2]) / dt};
        if (hand_length(moving) > 12.0F) // a cut
            moving = {};
        if (gap > 0U || fresh)
            state.at = wrist;
        const auto against_body = hand_sub(moving, body_way);
        const float speed = hand_length(against_body);
        // the room about the palm: the surface it could rest on
        room.clear();
        sc::Aabb box;
        const float r = m.hand_contact_far + 0.2F;
        box.add({palm[0] - r, palm[1] - r, palm[2] - r});
        box.add({palm[0] + r, palm[1] + r, palm[2] + r});
        sc::gather_room(box, room);
        const bool holds = ((held >> i) & 1U) != 0U;
        float contact_share = 0.0F;
        std::array<float, 3> surface_at{};
        bool found = false;
        std::array<float, 3> normal{0.0F, 1.0F, 0.0F};
        if (!holds) {
            float best = m.hand_contact_far + (state.touching ? 0.03F : 0.0F);
            const float least_facing = state.touching ? -0.4F : -0.55F;
            const sc::Vec3 at{palm[0], palm[1], palm[2]}, from{elbow[0], elbow[1], elbow[2]};
            for (const sc::Triangle &t : room) {
                if (sc::dot(from - t.a, t.n) < 0.0F)
                    continue;
                // the palm's way out (against the back of the hand) toward the face
                const float faces = -(back[0] * t.n.x + back[1] * t.n.y + back[2] * t.n.z);
                if (faces > least_facing)
                    continue;
                const float side = sc::dot(at - t.a, t.n);
                // straight under the palm: a nearer edge beside it (a pillow, the bed's frame) is another height
                const sc::Vec3 q = sc::closest_on_triangle(at, t.a, t.b, t.c);
                if (sc::length((at - t.n * side) - q) > 0.04F)
                    continue;
                const float dist = std::fabs(side);
                if (dist < best) {
                    best = dist;
                    found = true;
                    surface_at = {t.a.x, t.a.y, t.a.z};
                    normal = {t.n.x, t.n.y, t.n.z};
                    contact_share = 1.0F - hand_share(dist, m.hand_contact_near, m.hand_contact_far);
                }
            }
            contact_share *= 1.0F - hand_share(speed, m.hand_contact_speed, m.hand_contact_speed * 2.0F);
        }
        // the surface eased as the palm crosses from one face to another: the hand glides over the step
        if (found) {
            const float height = hand_dot(surface_at, normal);
            if (fresh || !state.touching) {
                state.surface = normal;
                state.surface_height = height;
            } else if (gap > 0U) {
                const float kept = std::exp(-dt / 0.12F);
                state.surface =
                    hand_unit(hand_add(hand_add({0.0F, 0.0F, 0.0F}, normal, 1.0F - kept), state.surface, kept));
                state.surface_height = height + (state.surface_height - height) * kept;
            }
        }
        state.touching = found;
        // the shares
        HandShares target{};
        target[kHandOpen] = hand_share(bend, m.hand_bend_from, m.hand_bend_to);
        target[kHandFist] = std::fmax(hand_share(speed, m.hand_fist_from, m.hand_fist_to),
                                      m.hand_walk_share * hand_share(body_speed, m.hand_walk_from, m.hand_walk_to)) *
                            (1.0F - pose_hold);
        target[kHandFlat] = contact_share;
        target[kHandGrip] = holds ? 1.0F : 0.0F;
        if (forced != nullptr && (*forced)[i] >= 0 && !holds) { // the animation says; the grip still wins
            const int p = (*forced)[i];
            target[kHandOpen] = p == kHandOpen ? 1.0F : 0.0F;
            target[kHandFist] = p == kHandFist ? 1.0F : 0.0F;
            target[kHandFlat] = p == kHandFlat ? 1.0F : 0.0F;
            target[kHandGrip] = p == kHandGrip ? 1.0F : 0.0F;
        }
        if (fresh) {
            state.shares = target;
            state.push = {};
            state.digit_bend.fill(bend);
            state.travel = 0.0F;
        } else {
            state.shares[kHandOpen] = hand_fade(state.shares[kHandOpen], target[kHandOpen], dt, 0.12F, 0.25F);
            state.shares[kHandFist] = hand_fade(state.shares[kHandFist], target[kHandFist], dt, 0.12F, 0.35F);
            state.shares[kHandFlat] = hand_fade(state.shares[kHandFlat], target[kHandFlat], dt, 0.12F, 0.25F);
            state.shares[kHandGrip] =
                hand_fade(state.shares[kHandGrip], target[kHandGrip], dt, m.hand_grip_fade, m.hand_grip_fade);
        }
        const float flat = state.shares[kHandFlat];
        // the hand turned so its palm lies on the surface, by the flat pose's share, at most 50 degrees
        if (flat > 0.001F) {
            const auto back_view = vec(axes, h.back);
            const auto wanted = vec(view, state.surface);
            const float cosine = std::clamp(hand_dot(back_view, wanted), -1.0F, 1.0F);
            const float degrees = std::acos(cosine) * 57.2957795F;
            const float share = flat * std::fmin(1.0F, 50.0F / std::fmax(degrees, 1e-3F));
            const auto toward =
                hand_unit(hand_add(hand_add({0.0F, 0.0F, 0.0F}, back_view, 1.0F - share), wanted, share));
            const Mat turn = turned_onto(head(h.joint), back_view, toward);
            for (const std::size_t j : h.hand_joints)
                joint_place[j] = mul(turn, joint_place[j]);
        }
        // the wrist's move: out of the body and the room; onto a surface it rests on, the palm as it now lies
        std::array<float, 3> push{};
        if (flat > 0.001F) {
            const auto turned = point(world, point(joint_place[h.joint], h.palm));
            const float over = hand_dot(turned, state.surface) - state.surface_height;
            push = hand_add(push, state.surface, (kPalmOver - over) * flat);
        }
        const auto sample_at = [&](const std::array<float, 4> &s, const std::array<float, 3> &by) {
            return hand_add(point(world, point(joint_place[h.joint], {s[0], s[1], s[2]})), by);
        };
        for (int round = 0; round < 3; ++round) {
            for (const auto &s : h.samples) {
                const auto at = sample_at(s, push);
                std::array<float, 3> way{};
                const float deep = body_depth(m, point(view, at), s[3], way);
                if (deep > 0.0F)
                    push = hand_add(push, vec(world, way), deep);
            }
            for (const auto &s : h.samples) {
                std::array<float, 3> moved{};
                const float deep = room_depth(room, sample_at(s, push), s[3], elbow, moved);
                if (deep > 0.0F)
                    push = hand_add(push, moved);
            }
        }
        if (const float l = hand_length(push); l > 0.2F)
            push = hand_add({0.0F, 0.0F, 0.0F}, push, 0.2F / l);
        // by a spring: the wrist never jumps and never overshoots, however what pushes it changes
        if (fresh) {
            state.push = push;
            state.push_speed = {};
        } else if (gap > 0U) {
            for (std::size_t c = 0; c < 3; ++c)
                hand_spring(state.push[c], state.push_speed[c], push[c], 22.0F, dt);
        }
        // the arm bent to it: the upper arm and forearm swung, the hand moved whole
        if (hand_length(state.push) > 1e-4F) {
            HandVec moved = hand_add(wrist_view, vec(view, state.push));
            const HandVec new_elbow = hand_arm_elbow(shoulder_view, elbow_view, wrist_view, moved);
            const Mat swing =
                turned_onto(shoulder_view, hand_sub(elbow_view, shoulder_view), hand_sub(new_elbow, shoulder_view));
            const Mat fore = mul(moved_by(hand_sub(new_elbow, elbow_view)),
                                 turned_onto(elbow_view, hand_sub(wrist_view, elbow_view), hand_sub(moved, new_elbow)));
            const Mat whole = moved_by(hand_sub(moved, wrist_view));
            for (const std::size_t j : h.upper_joints)
                joint_place[j] = mul(swing, joint_place[j]);
            for (const std::size_t j : h.forearm_joints)
                joint_place[j] = mul(fore, joint_place[j]);
            for (const std::size_t j : h.hand_joints)
                joint_place[j] = mul(whole, joint_place[j]);
        }
        // the fingers lag the wrist's turn and the hand's travel: curled as it bends back or moves that way
        if (fresh || gap > 0U) {
            const float kept = fresh ? 0.0F : std::exp(-dt / 0.09F);
            state.travel = hand_dot(against_body, back) + (state.travel - hand_dot(against_body, back)) * kept;
            for (std::size_t n = 0; n < state.digit_bend.size(); ++n) {
                const float lag = 0.04F + 0.012F * static_cast<float>(n); // the index first, the little finger last
                state.digit_bend[n] = fresh ? bend : bend + (state.digit_bend[n] - bend) * std::exp(-dt / lag);
            }
        }
        // each finger: its pose, and how far toward the flat one it must open to clear what it would go into
        const auto pose_digit = [&](const Model::HandDigit &digit, float open, float curl) {
            HandShares shares = state.shares;
            for (std::size_t f = digit.first; f <= digit.last; ++f) {
                const Model::HandFinger &finger = h.fingers[f];
                Quat q = hand_pose_turn(finger.poses, shares);
                const float straight = std::fmax(open, pose_straight * (1.0F - shares[kHandGrip]));
                if (straight > 0.0F)
                    q = hand_mix(q, finger.poses[kHandFlat], straight);
                if (!digit.thumb && f > digit.first)
                    q = hand_curled(q, curl);
                joint_place[finger.joint] =
                    mul(joint_place[finger.parent], mul(finger.frame, mul(hand_turn_matrix(q), finger.frame_inverse)));
            }
        };
        const auto digit_depth = [&](const Model::HandDigit &digit) {
            float deepest = 0.0F;
            for (const auto &[j, rest] : digit.spheres) {
                const auto at_view = point(joint_place[j], rest);
                std::array<float, 3> way{}, moved{};
                // the body's own surface: the parts' margins are for the wrist, or a hand hanging by the thigh opens
                deepest = std::fmax(deepest, body_depth(m, at_view, 0.009F, way, 0.0F));
                deepest = std::fmax(deepest, room_depth(room, point(world, at_view), 0.011F, elbow, moved));
            }
            return deepest;
        };
        const std::size_t digits = std::min(h.digits.size(), state.digit_open.size());
        const auto curl_of = [&](std::size_t n) {
            return std::clamp(m.hand_lag * (bend - state.digit_bend[n]) + state.travel * 6.0F, -14.0F, 14.0F) *
                   (1.0F - state.shares[kHandGrip]) * (1.0F - pose_hold);
        };
        std::array<float, 5> need{};
        for (std::size_t n = 0; n < digits; ++n) {
            const Model::HandDigit &digit = h.digits[n];
            const float curl = curl_of(n);
            // the grip stays on what it holds: nothing lifts it
            float open = 0.0F;
            if (state.shares[kHandGrip] < 0.5F) {
                pose_digit(digit, 0.0F, curl);
                if (digit_depth(digit) > 0.0F) {
                    float low = 0.0F, high = 1.0F;
                    for (int step = 0; step < 6; ++step) {
                        const float mid = (low + high) * 0.5F;
                        pose_digit(digit, mid, curl);
                        (digit_depth(digit) > 0.0F ? low : high) = mid;
                    }
                    open = high;
                }
            }
            need[n] = open;
        }
        // the four fingers give way together, the thumb alone; eased both ways: pushed open as they meet something,
        // not put there
        const std::array<float, 5> open = hand_linked_open(need, m.hand_finger_link);
        for (std::size_t n = 0; n < digits; ++n) {
            float &opened = state.digit_open[n];
            if (fresh)
                opened = open[n];
            else if (gap > 0U)
                opened = open[n] + (opened - open[n]) * std::exp(-dt / (open[n] > opened ? 0.06F : 0.2F));
            pose_digit(h.digits[n], opened, curl_of(n));
        }
        if (report) {
            // how deep in anything the hand is left
            float after = 0.0F;
            for (const auto &s : h.samples) {
                const auto at = point(joint_place[h.joint], {s[0], s[1], s[2]});
                std::array<float, 3> way{}, moved{};
                after = std::fmax(after, body_depth(m, at, s[3], way));
                after = std::fmax(after, room_depth(room, point(world, at), s[3], elbow, moved));
            }
            khdays_log(
                "models: %s: hand %zu: bend %.0f degrees, speed %.2f, open %.2f, fist %.2f, flat %.2f, grip %.2f%s, "
                "wrist moved %.1f cm, %.1f cm deep",
                m.name.c_str(), i, static_cast<double>(bend), static_cast<double>(speed),
                static_cast<double>(state.shares[kHandOpen]), static_cast<double>(state.shares[kHandFist]),
                static_cast<double>(state.shares[kHandFlat]), static_cast<double>(state.shares[kHandGrip]),
                holds ? ", holding" : "", static_cast<double>(hand_length(state.push) * 100.0F),
                static_cast<double>(after * 100.0F));
        }
    }
}

// bones.txt's `cuff`: this draw's fit of each cuff round its hand's vertices, eased, with its sway, onto the flaps' bones
void pose_cuffs(const Model &m)
{
    using namespace khdays::models;
    if (m.cuffs.empty())
        return;
    float camera[16];
    Mat world = kIdentity;
    const bool has_camera = khdays_g3d_camera(camera) != 0;
    if (has_camera) {
        Mat view;
        std::copy(camera, camera + 16, view.begin());
        world = inverse(view);
    }
    Kh2Actor &actor = actor_of(m);
    const u32 now = khdays_events_vblank_count();
    const u32 gap = now - actor.cuff_vblank;
    const bool fresh = actor.cuff_motions.size() != m.cuffs.size() || gap > 12U;
    actor.cuff_vblank = now;
    actor.cuff_motions.resize(m.cuffs.size());
    const float dt = static_cast<float>(gap) / 60.0F;
    static std::vector<HandVec> points;
    static std::vector<Mat> posed;
    for (std::size_t c = 0; c < m.cuffs.size(); ++c) {
        const Model::Cuff &cuff = m.cuffs[c];
        CuffMotion &motion = actor.cuff_motions[c];
        const Mat before = mul(skin[cuff.root], inverse(m.joints[cuff.root].bind));
        const Mat before_back = inverse(before);
        points.resize(cuff.points.size());
        for (std::size_t p = 0; p < cuff.points.size(); ++p)
            points[p] = point(before_back, point(skin[cuff.points[p].first], cuff.points[p].second));
        // how far into its pose: from the hand's bend, eased; the fit answers on the surface baked nearest there
        if (!cuff.surfaces.empty()) {
            const HandVec now_way =
                vec(before_back, vec(mul(skin[cuff.hand], inverse(m.joints[cuff.hand].bind)), cuff.hand_way));
            const float wanted =
                cuff_pose_share(cuff.hand_way, cuff.toward, hand_unit(now_way), m.cuff_settings.pose_bend);
            if (fresh) {
                motion.pose = wanted;
                motion.pose_speed = 0.0F;
            } else if (dt > 0.0F) {
                hand_spring(motion.pose, motion.pose_speed, wanted, 30.0F, dt);
            }
            motion.pose = std::clamp(motion.pose, 0.0F, 1.0F);
            cuff_pose_at(cuff.pose, motion.pose, posed);
        }
        // the cuff stands off round the hand as in the pose: what it fits round is the hand from there
        const HandVec pose_slide = cuff.surfaces.empty()
                                       ? HandVec{}
                                       : cuff_pose_slide(cuff.hand_way, cuff.toward, m.cuff_settings.pose_pivot,
                                                         motion.pose, m.cuff_settings.pose_bend);
        for (HandVec &p : points)
            p = hand_sub(p, pose_slide);
        const float along = cuff.surfaces.empty() ? 0.0F : motion.pose * static_cast<float>(cuff.surfaces.size() - 1);
        const std::size_t near_a = static_cast<std::size_t>(along),
                          near_b = cuff.surfaces.empty() ? 0U : std::min(near_a + 1U, cuff.surfaces.size() - 1U);
        const float between = along - static_cast<float>(near_a);
        const CuffFit &fit_a = cuff.surfaces.empty() ? cuff.fit : cuff.surfaces[near_a],
                      &fit_b = cuff.surfaces.empty() ? cuff.fit : cuff.surfaces[near_b];
        const float hold = cuff.surfaces.empty() ? 0.0F : cuff_pose_hold(motion.pose);
        const CuffFit::Target target =
            cuff_target_mix(cuff.surfaces.empty() ? cuff.fit.fit(points, m.cuff_settings)
                                                  : cuff_target_mix(fit_a.fit(points, m.cuff_settings),
                                                                    fit_b.fit(points, m.cuff_settings), between),
                            CuffFit::Target{}, hold);
        // the wrist's acceleration throws each flap; its weight hangs it
        const auto at = point(world, point(before, cuff.fit.center));
        HandVec accel{};
        if (!fresh && gap > 0U) {
            const HandVec moving = {(at[0] - motion.at[0]) / dt, (at[1] - motion.at[1]) / dt,
                                    (at[2] - motion.at[2]) / dt};
            if (hand_length(moving) < 12.0F) { // else a cut
                const float kept = std::exp(-dt / 0.05F);
                const HandVec eased = {moving[0] + (motion.moving[0] - moving[0]) * kept,
                                       moving[1] + (motion.moving[1] - moving[1]) * kept,
                                       moving[2] + (motion.moving[2] - moving[2]) * kept};
                accel = {(eased[0] - motion.moving[0]) / dt, (eased[1] - motion.moving[1]) / dt,
                         (eased[2] - motion.moving[2]) / dt};
                motion.moving = eased;
            } else {
                motion.moving = {};
            }
        } else if (fresh) {
            motion.moving = {};
        }
        if (fresh || gap > 0U)
            motion.at = at;
        CuffFit::Shares push{}, droop{};
        for (std::size_t f = 0; f < CuffFit::kFlaps; ++f) {
            const HandVec out = hand_unit(vec(world, vec(before, cuff.fit.out[f])));
            push[f] = std::clamp(hand_dot(accel, out), -60.0F, 60.0F);
            droop[f] = has_camera ? -out[1] * 0.09F : 0.0F;
        }
        cuff_motion_step(motion, target, push, droop, dt, fresh);
        // the whole cuff slid, then its flaps from there
        const Mat slide = mul(before, mul(moved_by(hand_add(motion.shift, pose_slide)), before_back));
        for (const std::size_t j : cuff.slides)
            skin[j] = mul(slide, skin[j]);
        const Mat frame = mul(slide, before);
        const Mat back = inverse(frame);
        // the ring's own change goes on last, in its frame, which a `swing` has left apart from the root's: the flaps
        // take the rest of theirs in the root's
        Mat ring_now = kIdentity, ring_undone = kIdentity;
        if (cuff.pose_ring >= 0) {
            const std::size_t ring = cuff.pose[static_cast<std::size_t>(cuff.pose_ring)].joint;
            ring_now = mul(skin[ring], inverse(m.joints[ring].bind));
            ring_undone = inverse(posed[static_cast<std::size_t>(cuff.pose_ring)]);
        }
        for (std::size_t f = 0; f < CuffFit::kFlaps; ++f) {
            const float open = motion.open[f] + motion.sway[f] * (1.0F - hold),
                        bend = std::clamp((motion.sway[f] - motion.sway_late[f]) * 1.2F, -0.2F, 0.2F) * (1.0F - hold);
            // the flap as it now stands in its pose, between the two surfaces either side
            const auto mixed = [&](const HandVec &a, const HandVec &b) { return hand_add(a, hand_sub(b, a), between); };
            const HandVec along_now = hand_unit(mixed(fit_a.along[f], fit_b.along[f]));
            const HandVec out_mixed = mixed(fit_a.out[f], fit_b.out[f]);
            const HandVec out_now = hand_unit(hand_add(out_mixed, along_now, -hand_dot(out_mixed, along_now)));
            const HandVec tip_now = mixed(fit_a.tip[f], fit_b.tip[f]);
            const HandVec way = hand_add(hand_add({0.0F, 0.0F, 0.0F}, along_now, std::cos(open) * motion.length[f]),
                                         out_now, std::sin(open) * motion.length[f]);
            const Mat flap = turned_onto(mixed(fit_a.hinge[f], fit_b.hinge[f]), along_now, way);
            const Mat tip =
                turned_onto(tip_now, along_now,
                            hand_add(hand_add({0.0F, 0.0F, 0.0F}, along_now, std::cos(bend)), out_now, std::sin(bend)));
            for (std::size_t n = 0; n < cuff.flap_joints[f].size(); ++n) {
                const std::size_t j = cuff.flap_joints[f][n];
                const bool far =
                    std::find(cuff.tip_joints[f].begin(), cuff.tip_joints[f].end(), j) != cuff.tip_joints[f].end();
                const Mat rest_to_fit = far ? mul(flap, tip) : flap;
                const int in_pose = cuff.surfaces.empty() ? -1 : cuff.flap_pose[f][n];
                const Mat all = in_pose >= 0
                                    ? mul(ring_undone, mul(rest_to_fit, posed[static_cast<std::size_t>(in_pose)]))
                                    : rest_to_fit;
                skin[j] = mul(frame, mul(all, mul(back, skin[j])));
            }
        }
        if (cuff.pose_ring >= 0) {
            const Mat ring_move =
                mul(ring_now, mul(posed[static_cast<std::size_t>(cuff.pose_ring)], inverse(ring_now)));
            for (const std::size_t j : cuff.slides)
                skin[j] = mul(ring_move, skin[j]);
        }
    }
}

} // namespace khdays::model_parts
