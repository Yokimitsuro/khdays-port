// A mod's model's stance on the game's floor: the boss Saix drawn upright on a slope with his feet on it (`upright`),
// raised feet put down (`plant_feet`), the hips' dips deepened (`hip_motion`), the body lowered so the legs reach as
// the game's (`lower_hips`); and his replaced shadow traced to the session log.

#include "model_parts.h"

extern "C" const void *khdays_character_drawn_object(void); // character_shadows.c

namespace khdays::model_parts {

// The `exact` legs' foot nodes, and each game node's foot (its index there, -1 none): a node under a foot's moves with it
std::vector<int> exact_feet(const Model &m, std::vector<int> &foot_of)
{
    std::vector<int> feet;
    for (const Limb &limb : m.limbs) {
        const int node = m.joints[static_cast<std::size_t>(limb.joints[2])].node;
        if (limb.exact && node >= 0 && static_cast<std::size_t>(node) < node_move.size())
            feet.push_back(node);
    }
    foot_of.assign(node_move.size(), -1);
    for (std::size_t g = 0; g < node_move.size(); ++g)
        for (int up = static_cast<int>(g); up >= 0 && foot_of[g] < 0;
             up = up != m.node_parent[static_cast<std::size_t>(up)] ? m.node_parent[static_cast<std::size_t>(up)] : -1)
            for (std::size_t i = 0; i < feet.size(); ++i)
                if (feet[i] == up)
                    foot_of[g] = static_cast<int>(i);
    return feet;
}

namespace {

// The boss Saix's floor (bones.txt's `upright`), verified in the decomp: Obj_RenderModel draws his body, whose +0x84 is
// his actor and the actor's +0x38c the body (Ov259_Construct); the actor's +0x124 is the floor normal under him
// (Ov107_AiState_ResolveContacts' ray, fx32) and +0x17a bit 0 his floor contact (Ov107_AiState_IntegrateVelocity)
struct UprightFloor {
    bool found = false, on_floor = false;
    std::array<float, 3> normal{0.0F, 1.0F, 0.0F}; // unit, Days' world
    Mat base = kIdentity, view = kIdentity;        // his base (the world), and the drawn space's from the world
};
UprightFloor upright_floor;

// The boss Saix's replacement shadow in the session log (character_shadow.h's outcome for his model tree, actor +0x9c,
// the owner khdays_character_bind_enemy binds his flat shadow to): when it turns to or from the game's circle, and every
// 2 s while the circle stays, with why and the floor under him
void trace_shadow(Model &m, KhdaysRef actor)
{
    KhdaysRef tree = 0;
    s32 place[3] = {}, floor_gap = 0;
    if (!khdays_game_ram(actor + 0x13fU))
        return;
    std::memcpy(&tree, KHDAYS_W(actor + 0x9cU), 4);
    std::memcpy(place, KHDAYS_W(actor + 0x74U), sizeof(place)); // field_074, its place (the flat shadow's x z)
    std::memcpy(&floor_gap, KHDAYS_W(actor + 0x13cU), 4); // field_13c: the flat shadow sits at place y - it + 0x200
    const KhdaysShadowOutcome *o = khdays_shadow_outcome(tree);
    const bool fresh = o != nullptr && khdays_shadow_lists_finished() - o->list <= 4U;
    const int state = fresh ? o->outcome : -1;
    const u32 now = khdays_events_vblank_count();
    if (state == m.shadow_said && (state == KHDAYS_SHADOW_REPLACED_IT || now - m.shadow_told < 120U))
        return;
    m.shadow_said = state;
    m.shadow_told = now;
    static const char *const why[] = {"replaced",
                                      "kept the circle: none of his polygons captured",
                                      "kept the circle: no flat shadow of his captured",
                                      "kept the circle: his flat shadow has no height",
                                      "kept the circle: nothing in the band received it",
                                      "kept the circle: no room in the list"};
    const double fx = 1.0 / 4096.0;
    if (!fresh) {
        khdays_log(
            "models: %s: shadow: none made for his tree %08x lately (no flat shadow bound to it, or 32 casters); "
            "place y %.2f",
            m.name.c_str(), tree, place[1] * fx);
        return;
    }
    khdays_log("models: %s: shadow %s; band y %.2f..%.2f%s, his %d polygons y %.2f..%.2f; under him %d polygons y "
               "%.2f..%.2f: %d translucent (alpha %d), %d shadow mode, %d steep, %d other casters'; place y %.2f, flat "
               "shadow at y %.2f, on the floor %d",
               m.name.c_str(), why[std::clamp(o->outcome, 0, 5)], static_cast<double>(o->band_lo),
               static_cast<double>(o->band_hi), o->moved_below ? " (moved to the floor below)" : "", o->sources,
               static_cast<double>(o->own_lo), static_cast<double>(o->own_hi), o->under,
               static_cast<double>(o->under_lo), static_cast<double>(o->under_hi), o->translucent, o->translucent_alpha,
               o->shadow_mode, o->steep, o->casters, place[1] * fx, (place[1] - floor_gap + 0x200) * fx,
               upright_floor.on_floor ? 1 : 0);
}

} // namespace

// bones.txt's `upright`: Ov259_WatchSlot turns the boss Saix (his actor's +0xa0, the base Obj_RenderModel draws by) by
// the shortest turn from up to his floor's normal; that turn taken back out of the game's nodes about his place. His
// actor keeps it: his hit shapes and attacks are the game's (docs/KH2_MODELS.md)
void stand_upright(Model &m, std::size_t first)
{
    upright_floor = UprightFloor{};
    const auto say_once = [&](const char *why) {
        if (!m.said_upright)
            khdays_log("models: %s: `upright`: %s", m.name.c_str(), why);
        m.said_upright = true;
    };
    const void *drawn = khdays_character_drawn_object();
    const KhdaysRef body = drawn != nullptr ? KHDAYS_REF(drawn) : 0U;
    KhdaysRef actor = 0, held = 0;
    if (body != 0U && khdays_game_ram(body + 0x87U))
        std::memcpy(&actor, KHDAYS_W(body + 0x84U), 4);
    if (actor != 0U && khdays_game_ram(actor + 0x38fU))
        std::memcpy(&held, KHDAYS_W(actor + 0x38cU), 4);
    constexpr KhdaysRef kBase = 0x02047394U + 0x94U; // NNS_G3dGlb's base: rotation, translation, scale
    if (held == 0U || held != body || !khdays_game_ram(kBase + 15U * 4U - 1U)) {
        say_once("not drawn as the boss Saix's body (ov259's actor +0x38c), so left as the game tilts it; remove "
                 "`upright` from this model's bones.txt");
        return;
    }
    s32 n[3] = {};
    std::memcpy(n, KHDAYS_W(actor + 0x124U), sizeof(n));
    std::array<float, 3> normal{static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2])};
    const float nl = length(normal);
    if (nl > 0.0F && normal[1] / nl > 0.2F) // a wall's (ResolveContacts sets it on one too) left as no tilt
        for (std::size_t c = 0; c < 3; ++c)
            upright_floor.normal[c] = normal[c] / nl;
    upright_floor.on_floor = (*static_cast<const std::uint8_t *>(KHDAYS_W(actor + 0x17aU)) & 1U) != 0U;
    s32 words[15];
    std::memcpy(words, KHDAYS_W(kBase), sizeof(words));
    khdays_g3d_base_matrix(words, upright_floor.base.data());
    upright_floor.view = mul(drawn_start, inverse(upright_floor.base));
    upright_floor.found = true;
    say_once("drawn upright on his floor, his `exact` feet put on it");
    trace_shadow(m, actor);
    float degrees = 0.0F;
    Mat back = arc(upright_floor.normal, {0.0F, 1.0F, 0.0F}, degrees);
    if (!(degrees > 0.01F) || first >= node_move.size())
        return;
    const Mat &b = upright_floor.base; // about his place
    const std::array<float, 3> at{b[12], b[13], b[14]}, turned = point(back, at);
    for (std::size_t c = 0; c < 3; ++c)
        back[12 + c] = at[c] - turned[c];
    const Mat r = mul(upright_floor.view, mul(back, inverse(upright_floor.view)));
    for (Mat &dg : node_move)
        dg = mul(r, dg);
}

// bones.txt's `upright`, after plant_feet: while he touches the floor, each `exact` foot raised to where the floor's
// plane through his place lies under it, the body lowered by the most a foot goes down, so the legs bend onto the
// slope; a foot near its rest height turned with the floor. Eased over a few frames (the normal changes at once
// from one triangle to the next)
void feet_on_floor(const Model &m, std::size_t first)
{
    const UprightFloor &f = upright_floor;
    if (!f.found || first >= node_move.size())
        return;
    Kh2Actor &actor = actor_of(m);
    const u32 now = khdays_events_vblank_count();
    const u32 gap = now - actor.floor_vblank;
    actor.floor_vblank = now;
    const float ease = gap > 12U ? 1.0F : 1.0F - std::exp(-static_cast<float>(gap) / 60.0F * 15.0F);
    const Mat to_world = inverse(f.view);
    const std::array<float, 3> at{f.base[12], f.base[13], f.base[14]}, &n = f.normal;
    const float units = length({f.base[4], f.base[5], f.base[6]}); // world units a model unit, upward
    const auto &origin = m.node_rest_place[first];
    std::vector<int> feet;
    std::vector<float> rise, planted;
    for (const Limb &limb : m.limbs) {
        const int node = m.joints[static_cast<std::size_t>(limb.joints[2])].node;
        if (!limb.exact || node < 0 || static_cast<std::size_t>(node) >= node_move.size())
            continue;
        const auto &rest = m.node_rest_place[static_cast<std::size_t>(node)];
        const auto foot = point(to_world, point(node_move[static_cast<std::size_t>(node)], rest));
        const float floor = -(n[0] * (foot[0] - at[0]) + n[2] * (foot[2] - at[2])) / n[1];
        const float above = foot[1] - at[1] - (rest[1] - origin[1]) * units; // over its standing height
        feet.push_back(node);
        rise.push_back(f.on_floor ? floor : 0.0F);
        planted.push_back(f.on_floor ? std::clamp(1.0F - above / (0.15F * units), 0.0F, 1.0F) : 0.0F);
    }
    if (actor.floor_rise.size() != rise.size()) {
        actor.floor_rise.assign(rise.size(), 0.0F);
        actor.floor_drop = 0.0F;
    }
    float lowest = 0.0F;
    for (std::size_t i = 0; i < rise.size(); ++i) {
        actor.floor_rise[i] += (rise[i] - actor.floor_rise[i]) * ease;
        lowest = std::fmin(lowest, rise[i]);
    }
    actor.floor_drop += (-lowest - actor.floor_drop) * ease;
    std::vector<int> foot_of;
    exact_feet(m, foot_of);                                                    // the same feet, in the same order
    const auto lift = [&](float by) { return vec(f.view, {0.0F, by, 0.0F}); }; // the world's up, drawn
    float degrees = 0.0F;
    const Mat slope = arc({0.0F, 1.0F, 0.0F}, n, degrees);
    for (std::size_t i = 0; i < feet.size(); ++i) { // the foot turned with the floor about its ankle, by how planted
        if (!(degrees > 0.01F) || !(planted[i] > 0.0F))
            continue;
        const auto ankle =
            point(node_move[static_cast<std::size_t>(feet[i])], m.node_rest_place[static_cast<std::size_t>(feet[i])]);
        const Mat turn = khdays::models::attached_frame(kIdentity, slope, planted[i]); // that share of the slope's
        Mat about = mul(f.view, mul(turn, to_world));
        const auto moved = point(about, ankle);
        for (std::size_t c = 0; c < 3; ++c)
            about[12 + c] += ankle[c] - moved[c];
        for (std::size_t g = 0; g < node_move.size(); ++g)
            if (foot_of[g] == static_cast<int>(i))
                node_move[g] = mul(about, node_move[g]);
    }
    for (std::size_t g = 0; g < node_move.size(); ++g) {
        if (g == first)
            continue;
        const auto by =
            lift(foot_of[g] >= 0 ? actor.floor_rise[static_cast<std::size_t>(foot_of[g])] : -actor.floor_drop);
        for (std::size_t c = 0; c < 3; ++c)
            node_move[g][12 + c] += by[c];
    }
}

// motions.txt's `plant_feet`: in the animations it names, the game's skeleton (every node but the first, the actor's
// origin) lowered until the lower of its `exact` legs' feet stands as high over the origin as at rest, by those
// animations' share of the game's pose; none raised. The boss Saix's 016_018-026 keep the pelvis up and draw the feet
// up off the floor: lowered, the `exact` legs crouch onto feet on the floor
void plant_feet(const Model &m, std::size_t first)
{
    float share = 0.0F, total = 0.0F;
    for (const Playing &p : playing) {
        total += p.ratio;
        if (m.plant_feet.contains(p.name))
            share += p.ratio;
    }
    if (!(total > 0.0F) || !(share > 0.0F) || first >= node_move.size())
        return;
    const Mat &root = node_move[first];
    std::array<float, 3> up{root[4], root[5], root[6]};
    const float ul = length(up); // the model's height units in the drawn space
    if (!(ul > 0.0F))
        return;
    for (float &c : up)
        c /= ul;
    const auto &origin = m.node_rest_place[first];
    const auto origin_now = point(root, origin);
    float rise = INFINITY;
    for (const Limb &limb : m.limbs) {
        const int node = m.joints[static_cast<std::size_t>(limb.joints[2])].node;
        if (!limb.exact || node < 0 || static_cast<std::size_t>(node) >= node_move.size())
            continue;
        const auto &rest = m.node_rest_place[static_cast<std::size_t>(node)];
        const auto now = point(node_move[static_cast<std::size_t>(node)], rest);
        const float height =
            (now[0] - origin_now[0]) * up[0] + (now[1] - origin_now[1]) * up[1] + (now[2] - origin_now[2]) * up[2];
        rise = std::fmin(rise, height - (rest[1] - origin[1]) * ul);
    }
    if (!std::isfinite(rise) || !(rise > 0.0F))
        return;
    const float drop = rise * share / total;
    for (std::size_t g = 0; g < node_move.size(); ++g)
        if (g != first)
            for (std::size_t c = 0; c < 3; ++c)
                node_move[g][12 + c] -= up[c] * drop;
}

// motions.txt's `hip_motion <gain> <animation>...`: where the hips (the `exact` legs' hip nodes) dip below their rest
// height, every node but the first and the `exact` feet lowered so the dip is `gain` times as deep, saturating at 0.45
// of the rest height (D tanh(gain dip / D)); none as both feet leave the floor. The gain is the playing animations' by
// their shares (1 for the others), eased over 0.1 s (docs/KH2_MODELS.md, `hip_motion`)
void hip_motion(const Model &m, std::size_t first)
{
    Kh2Actor &actor = actor_of(m);
    const u32 now = khdays_events_vblank_count(), gap = now - actor.hip_vblank;
    actor.hip_vblank = now;
    const float ease = gap > 12U ? 1.0F : 1.0F - std::exp(-static_cast<float>(gap) / 60.0F / 0.1F);
    float total = 0.0F, weighted = 0.0F;
    for (const Playing &p : playing) {
        const auto g = m.hip_gain.find(p.name);
        total += p.ratio;
        weighted += p.ratio * (g != m.hip_gain.end() ? g->second : 1.0F);
    }
    const float gain = total > 0.0F ? weighted / total : 1.0F;
    if (first >= node_move.size())
        return;
    const Mat &root = node_move[first];
    std::array<float, 3> up{root[4], root[5], root[6]};
    const float ul = length(up);
    if (!(ul > 0.0F))
        return;
    for (float &c : up)
        c /= ul;
    const auto &origin = m.node_rest_place[first];
    const auto origin_now = point(root, origin);
    const auto height = [&](int node) { // over the origin now, and at rest
        const auto &rest = m.node_rest_place[static_cast<std::size_t>(node)];
        const auto at = point(node_move[static_cast<std::size_t>(node)], rest);
        return std::make_pair((at[0] - origin_now[0]) * up[0] + (at[1] - origin_now[1]) * up[1] +
                                  (at[2] - origin_now[2]) * up[2],
                              (rest[1] - origin[1]) * ul);
    };
    float dip = 0.0F, rest = 0.0F, lift = INFINITY;
    int legs = 0;
    for (const Limb &limb : m.limbs) {
        const int hip = m.joints[static_cast<std::size_t>(limb.joints[0])].node,
                  foot = m.joints[static_cast<std::size_t>(limb.joints[2])].node;
        if (!limb.exact || hip < 0 || foot < 0 || static_cast<std::size_t>(std::max(hip, foot)) >= node_move.size())
            continue;
        const auto [h, h0] = height(hip);
        const auto [f, f0] = height(foot);
        dip += h0 - h;
        rest += h0;
        lift = std::fmin(lift, f - f0);
        ++legs;
    }
    if (legs == 0)
        return;
    dip = std::fmax(dip / static_cast<float>(legs), 0.0F);
    const float deepest = 0.45F * rest / static_cast<float>(legs);
    if (!(deepest > 0.0F))
        return;
    const float grounded = std::clamp(1.0F - std::fmax(lift, 0.0F) / (0.2F * ul), 0.0F, 1.0F);
    const float want = gain > 1.0F ? std::fmax(deepest * std::tanh(gain * dip / deepest) - dip, 0.0F) * grounded : 0.0F;
    actor.hip_drop += (want - actor.hip_drop) * ease;
    const float drop = actor.hip_drop;
    if (!(drop > 1e-4F))
        return;
    std::vector<int> foot_of;
    exact_feet(m, foot_of);
    for (std::size_t g = 0; g < node_move.size(); ++g)
        if (g != first && foot_of[g] < 0)
            for (std::size_t c = 0; c < 3; ++c)
                node_move[g][12 + c] -= up[c] * drop;
}

// bones.txt's `lower_hips`: every joint moved down (the root's up) by the most any `exact` leg needs for its hip to
// stand from its foot, over the leg's length, as far as the game's thigh from its foot over the game's leg (none raises
// it). A KH2 leg shorter than the game's, or hips set higher, straightened where the game's knee bent
void lower_hips(const Model &m, const Mat &root)
{
    using V = std::array<float, 3>;
    V up{root[4], root[5], root[6]};
    const float ul = length(up);
    if (!(ul > 0.0F))
        return;
    for (float &c : up)
        c /= ul;
    const auto node_at = [&](int node) {
        return point(node_move[static_cast<std::size_t>(node)], m.node_rest_place[static_cast<std::size_t>(node)]);
    };
    const auto gap = [](const V &a, const V &b) { return length({a[0] - b[0], a[1] - b[1], a[2] - b[2]}); };
    float drop = 0.0F;
    for (const Limb &limb : m.limbs) {
        if (!limb.exact)
            continue;
        const Joint &a = m.joints[static_cast<std::size_t>(limb.joints[0])],
                    &b = m.joints[static_cast<std::size_t>(limb.joints[1])],
                    &c = m.joints[static_cast<std::size_t>(limb.joints[2])];
        if (a.node < 0 || b.node < 0 || c.node < 0)
            continue;
        const V hip = point(joint_place[static_cast<std::size_t>(limb.joints[0])], a.head),
                knee = point(joint_place[static_cast<std::size_t>(limb.joints[0])], b.head),
                end = point(joint_place[static_cast<std::size_t>(limb.joints[1])], c.head);
        const V hd = node_at(a.node), kd = node_at(b.node), ed = node_at(c.node);
        const float game = gap(kd, hd) + gap(ed, kd);
        if (!(game > 0.0F))
            continue;
        const float leg = gap(knee, hip) + gap(end, knee);
        const float wanted = std::fmin(gap(ed, hd) / game, 0.999F) * leg; // the hip's distance from the foot wanted
        const V shift = vec(node_move[static_cast<std::size_t>(c.node)], limb.offset);
        const V v{hip[0] - ed[0] - shift[0], hip[1] - ed[1] - shift[1], hip[2] - ed[2] - shift[2]};
        const float now = length(v), above = v[0] * up[0] + v[1] * up[1] + v[2] * up[2];
        if (!(now > wanted) || !(above > 0.0F))
            continue;
        // the height of moving the hip toward the foot by what it lacks: down a leg that hangs, little for one that
        // trails behind. Solving |v - t up| = wanted instead dropped the body to the foot's height where no drop could
        // reach it, and back: marluxia_h's run (its pelvis turned, its hips forward) bobbed 39 cm, 20 in a frame
        drop = std::fmax(drop, above * (1.0F - wanted / now));
    }
    drop *= m.lower_hips;
    if (!(drop > 0.0F))
        return;
    for (Mat &x : joint_place)
        for (std::size_t c = 0; c < 3; ++c)
            x[12 + c] -= up[c] * drop;
}

} // namespace khdays::model_parts
