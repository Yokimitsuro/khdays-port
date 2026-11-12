// The secondary-motion solver: one actor's particles stepped with XPBD (docs/SECONDARY_MOTION.md,
// "Solver"). All storage is sized at construction; a tick allocates nothing once the environment's
// triangle buffer has grown to its working size.
#pragma once

#include "collision.h"
#include "rig.h"

#include <array>
#include <cstdint>
#include <vector>

namespace khdays::secondary {

// Time spent by phase, in microseconds, summed since the last clear.
struct PhaseTimes {
    double pose = 0, solve = 0, model_collision = 0, environment_gather = 0, environment_query = 0, self_collision = 0,
           reconstruct = 0, startup = 0;
    void clear()
    {
        *this = {};
    }
};

// How a particle is driven this tick: its settings (a control's overrides blended in), how far its
// target is the animated pose (guide: 0 the prepared reference, 1 the animation) and how much the
// animation rules it outright (authority 1: it is the animation).
struct ParticleDrive {
    ParamBlock params;
    float guide = 0.0F;
    float authority = 0.0F;
};

struct TickSettings {
    float dt = 1.0F / 60.0F;
    int substeps = 4, iterations = 2;
    bool self_collision = true, environment = true;
    Vec3 gravity{0.0F, -9.81F, 0.0F};
    float environment_thickness = 0.02F;
    bool ground_only = false; // the room's floors alone
    Environment *env = nullptr;
};

class Simulation {
public:
    explicit Simulation(const Rig &rig);

    // At the pose given (every needed bone's frame, the solver's space): each control on its target,
    // still relative to it, then bounded relaxation against the colliders and the room.
    void initialize(const std::vector<Frame> &pose, const TickSettings &settings, int relax_iterations);
    // One tick from pose `from` to pose `to`, each needed bone moving between them.
    void tick(const std::vector<Frame> &from, const std::vector<Frame> &to, const TickSettings &settings);
    // Everything moved rigidly with group g's reference bone from `a` to `b` (a teleport): shape kept,
    // velocity relative to it zeroed. `groups`: only those groups (1 each), the others left as they are.
    void rebase(const std::vector<Frame> &a, const std::vector<Frame> &b,
                const std::vector<std::uint8_t> *groups = nullptr);
    // Ticks with `pose` held and the swing damped each tick, so the cloth hangs as this pose has it before it is
    // shown (a fresh start, a camera cut); the drawn shape then starts on it.
    void settle(const std::vector<Frame> &pose, const TickSettings &settings, int ticks);
    // `game_shape`: each control's place on the replaced game model's surface at the next tick's start and end
    // (the solver's space), and whether it has one
    void set_game_targets(const std::vector<Vec3> &from, const std::vector<Vec3> &to, const std::vector<float> &weight);
    // `cutscene_sim`: the shell over those places (triangles of particles, wound outward; a fixed particle at its
    // own place), which such a particle never goes inside
    void set_game_shell(const std::vector<std::array<int, 3>> &triangles, const std::vector<std::uint8_t> &open);
    // `cutscene_sim`: each such particle on its place at `pose`, still and drawn there (a new shot, the game's pose
    // snapping)
    void snap_to_game(const std::vector<Frame> &pose);
    // Whether this particle's settings take the game model's places (`game_shape`, `cutscene_sim`)
    bool follows_game(std::size_t i) const;

    // The bones at `pose` from the solved particles: each rebuilt bone and each bone it carries, in
    // `out` (sized like the skeleton), flagged in `written`.
    void reconstruct(const std::vector<Frame> &pose, std::vector<Frame> &out, std::vector<std::uint8_t> &written);

    std::vector<ParticleDrive> &drives()
    {
        return drive_;
    }
    // per group: the air's velocity this tick (solver space)
    std::vector<Vec3> &winds()
    {
        return wind_;
    }
    const std::vector<Vec3> &positions() const
    {
        return x_;
    }
    // the room's triangles the last tick gathered (solver space), and the most each pushed one point then (0: none)
    const std::vector<Triangle> &room() const
    {
        return env_;
    }
    const std::vector<float> &room_push() const
    {
        return room_push_;
    }
    const std::vector<Vec3> &targets() const
    {
        return target_;
    }
    // the targets as the animation (and the legs carrying it) place them, before the drape round the body
    const std::vector<Vec3> &raw_targets() const
    {
        return raw_;
    }
    const Rig &rig() const
    {
        return rig_;
    }
    // the colliders where they were at the end of the last tick: ends and radii
    struct Shape {
        Vec3 a, b;
        float ra = 0.0F, rb = 0.0F;
    };
    const std::vector<Shape> &colliders() const
    {
        return shape1_;
    }
    PhaseTimes times;
    int resets = 0;
    std::size_t environment_triangles = 0;

private:
    void frames_at(const std::vector<Frame> &from, const std::vector<Frame> &to, float u,
                   std::vector<Frame> &out) const;
    Vec3 target_of(std::size_t i, const std::vector<Frame> &pose) const;
    Vec3 reference_point(std::size_t i, const std::vector<Frame> &pose) const;
    std::array<float, 4> blend_weights(const ReferenceBlend &b, const std::vector<Frame> &pose, float scale) const;
    Frame reference_frame(int sim, const std::vector<Frame> &pose) const;
    Vec3 animated_of(std::size_t i, const std::vector<Frame> &pose) const;
    void place_kinematic(const std::vector<Frame> &pose);
    void shapes(const std::vector<Frame> &pose, std::vector<Shape> &out) const;
    void solve_links(float h);
    void solve_follow(float h);
    void solve_limits();
    void collide_model(float friction_scale, bool surfaces);
    void collide_environment(const TickSettings &s);
    void collide_self();
    void limit_strain(bool anchors_only);
    void animated_lengths();
    void face_normals();                                    // face_normal_prev_ from where the particles are now
    void read_room(const Aabb &box, const TickSettings &s); // env_ and env_boxes_: the room's triangles in `box`
    Frame bone_frame(int sim, const std::vector<Frame> &pose, bool twist);
    void recover();
    float w(std::size_t i) const; // the particle's inverse mass as the constraints see it

    const Rig &rig_;
    std::vector<ParticleDrive> drive_;
    std::vector<Vec3> wind_;
    // per particle
    std::vector<Vec3> x_, prev_, v_, target_, target_prev_, target_vel_, rest_;
    std::vector<float> lambda_follow_;
    std::vector<std::uint8_t> pinned_; // the animation's (authority 1) this substep
    bool fresh_ = true;                // the first substep after a start: velocity = the target's
    // per link
    std::vector<float> lambda_, compliance_;
    std::vector<float> rest_now_, tether_now_; // lengths this substep (animated_lengths)
    // the drawn shape (`smoothing`): each control in its group's reference bone space; solved_ holds the
    // solved positions while the bones are rebuilt on it
    std::vector<Vec3> shown_, solved_;
    std::vector<Vec3> drawn_; // the drawn controls, world (guard_drawn_faces)
    void guard_drawn_faces();
    std::vector<Vec3> pushed_; // contact's push on each control this substep (taken out of its velocity)
    std::vector<Vec3> game_from_, game_to_, game_now_; // set_game_targets; game_now_ at the substep's end
    std::vector<float> game_weight_;
    float game_shape_of(std::size_t i) const; // `game_shape` times the particle's weight, 0 without targets
    bool shelled(std::size_t i) const;        // `cutscene_sim` with a place on the game model
    std::vector<std::array<int, 3>> shell_faces_;
    std::vector<std::uint8_t> shell_open_; // per shell face: across an opening (a slit), not pushed
    void push_shell();
    std::vector<Vec3> shell_normal_; // per particle this substep: outward from the shell's faces round it
    void shell_normals();
    void keep_outside_shell();
    const Frame &reference_bone_frame(std::size_t i, const std::vector<Frame> &pose) const;
    // poses: the needed bones at the substep's start and end
    std::vector<Frame> sub0_, sub1_;
    std::vector<Shape> shape0_, shape1_;
    Aabb bounds_; // the dynamic particles' this substep, grown by their reach
    std::vector<Triangle> env_;
    std::vector<Aabb> env_boxes_;
    std::vector<float> room_push_; // per env_ triangle: the most it pushed one point this tick
    // per collider: the controls, edges and faces it may touch (tags matched, rest overlaps left out)
    std::vector<std::vector<int>> collider_points_, collider_edges_, collider_faces_;
    // a pair's authored clearance (Rig::point_clearance, face_clearance) at the collider's size now, or 0
    float point_clearance(std::size_t c, std::size_t i, const Shape &s) const;
    float face_clearance(std::size_t c, std::size_t f, const Shape &s) const;
    // the way out of the body: a control's in its group's reference bone space; body (rest-side) contact pushes
    // there however the body moved
    std::vector<Vec3> outward_;
    // the targets draped around the colliders down each strip (drape): order (anchors first), each
    // particle's colliders (collider, index into its candidates), raw and draped targets
    std::vector<int> drape_order_;
    std::vector<int> collider_order_; // contact order: side-motion colliders first, rest-side ones last
    std::vector<std::vector<std::pair<int, int>>> particle_colliders_;
    std::vector<Vec3> raw_, draped_;
    void drape(const std::vector<Frame> &pose, const std::vector<Shape> &shapes, std::vector<Vec3> &out);
    void ease_drape(float h, bool exact);
    std::vector<Vec3> drape_off_; // the drape's swing of each target, eased, in its reference bone's space
    // self-collision: per self-colliding particle, a bit per face of the faces it may touch
    std::vector<int> self_points_;
    std::vector<std::vector<std::uint64_t>> self_faces_ok_;
    std::vector<Vec3> face_normal_prev_;
    std::vector<Aabb> face_box_;
    // bone rebuilding
    std::vector<Frame> bone_out_;
    // each rebuilt bone as the last reconstruct left it, on its group's reference bone, for the colliders on
    // simulated bones (Collider::sim); false until a tick has rebuilt them (and after a reset)
    std::vector<Frame> sim_relative_;
    bool sim_relative_valid_ = false;
    std::vector<std::uint8_t> bone_done_;
    // a freely swinging bone (no swing limit): its last turn, and whether it has one since a start
    std::vector<Quat> swung_;
    std::vector<std::uint8_t> swung_valid_;
};

// The reference shapes made ready, once a model: a group with `rest` lines takes its shape from them
// (rest_shape); every other group's rest shape is pushed out of the colliders it collides with (its links
// kept), and those that ask (`settle on`) hung under gravity at the rest pose against them, their follow
// off, until still (`until_still`, else every step asked). Controls still inside a collider after that are
// left out of it, each named in `out`.
void prepare_references(Rig &rig, Diagnostics *out = nullptr, bool until_still = true);

// The skeleton at rest (`pose`, the rest frames) with the groups' `rest` turns in place of their bones' own,
// the bones below carried along
std::vector<Frame> rest_shape(const Rig &rig, const std::vector<Frame> &pose);

} // namespace khdays::secondary
