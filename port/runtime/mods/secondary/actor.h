// One actor's secondary motion over time (docs/SECONDARY_MOTION.md, "Time"): fixed-rate ticks on the
// game's clock however often the actor is drawn, the received poses interpolated between ticks,
// rebased on teleports and restarted on discontinuities; animation controls and wind evaluated each
// tick; timing kept for the profile.
#pragma once

#include "solver.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace khdays::secondary {

// A game animation the actor plays: its name, its KH2 mset slot and motions.txt section when it plays
// one, how far into it (seconds), its length (0 unknown) and its weight in the game's blend.
struct AnimationState {
    std::string name;
    int slot = -1;
    float section = -1.0F;
    float seconds = 0.0F;
    float length = 0.0F;
    float weight = 1.0F;
};

struct ActorInput {
    double seconds = 0.0;                     // the game's clock: stands while it is paused, its rate the game's
    std::uint64_t epoch = 0;                  // changes where the clock is not continuous (a save state, a reload)
    const std::vector<Frame> *pose = nullptr; // every needed bone, the solver's space
    Frame placement;                          // the actor's own frame (actor-relative wind), the solver's space
    std::vector<AnimationState> animations;
    float camera_distance = 0.0F; // m, for the level of detail
    // the view cut (a camera cut): what the body did since the last draw is a new shot, not motion; the cloth is
    // carried with it and settled in the new pose instead of being flung
    bool cut = false;
    // the pose cut to another motion with no fade (a damage's KH2 motion starting): legs jumping far in it start
    // the cloth afresh on the new pose, draped round them, rather than carried through them from the last one
    bool pose_cut = false;
    Environment *environment = nullptr;
    // `game_shape`: each particle's place on the replaced game model's surface (the solver's space) and how far
    // it may take it there (0 none: too far from that surface); null without
    const std::vector<Vec3> *game_targets = nullptr;
    const std::vector<float> *game_weight = nullptr;
    // `cutscene_sim`: the shell over those places (Simulation::set_game_shell); null without
    const std::vector<std::array<int, 3>> *game_shell = nullptr;
    const std::vector<std::uint8_t> *game_shell_open = nullptr; // per shell face: across an opening (not pushed)
};

// The last measurements of an actor's cost: per update (microseconds), kept for percentiles.
struct Profile {
    std::vector<float> totals; // a ring of the last updates' costs
    std::size_t next = 0;
    std::uint64_t updates = 0, ticks = 0, rebases = 0, restarts = 0;
    float startup_max = 0.0F;
    PhaseTimes phases; // summed since the last report
    void add(float total);
    float percentile(float p) const;
};

class Actor {
public:
    Actor(std::shared_ptr<const Rig> rig, std::uint32_t seed);

    void update(const ActorInput &in);
    // The bones as the simulation draws them at `pose` (the last update's): each rebuilt or carried
    // bone in `out`, flagged in `written`. Nothing when the actor is frozen by its level of detail.
    void output(const std::vector<Frame> &pose, std::vector<Frame> &out, std::vector<std::uint8_t> &written);

    const Simulation &simulation() const
    {
        return sim_;
    }
    const Rig &rig() const
    {
        return *rig_;
    }
    const Profile &profile() const
    {
        return profile_;
    }
    Profile &profile()
    {
        return profile_;
    }
    std::uint64_t generation() const
    {
        return generation_;
    }
    bool frozen() const
    {
        return frozen_;
    }
    // the weight each control (rig().controls) had at the last tick
    const std::vector<float> &control_weights() const
    {
        return control_applied_;
    }

private:
    void start(const ActorInput &in);
    void drive(const ActorInput &in, float dt);
    TickSettings tick_settings(const LodConfig &l, const ActorInput &in) const;
    Vec3 wind_of(const Wind &w, int bone, const ActorInput &in, std::size_t group) const;
    const LodConfig &lod(const ActorInput &in) const;

    std::shared_ptr<const Rig> rig_;
    Simulation sim_;
    std::uint32_t seed_;
    bool started_ = false, frozen_ = false;
    // a cut seen on a draw that moved no time (ActorInput cut, pose_cut), kept for the next that does
    bool cut_waiting_ = false, pose_cut_waiting_ = false;
    std::uint64_t epoch_ = 0, generation_ = 0;
    double last_seconds_ = 0.0, sim_seconds_ = 0.0;
    double settled_at_ = -1e9;    // the game's time of the last settle (a start, a cut)
    std::int64_t ticks_done_ = 0; // ticks since the start, sim_seconds_ = ticks_done_ / rate from it
    std::vector<Frame> last_pose_, from_, to_;
    std::vector<std::uint8_t> jumped_;              // per group: its reference bone jumped this draw
    std::vector<Vec3> last_game_, game_a_, game_b_; // the last draw's game targets, a tick's start and end
    std::vector<float> control_applied_;
    std::vector<std::size_t> control_order_; // the controls by priority, lowest first (drive)
    Profile profile_;
    LodConfig full_;
};

// A deterministic smooth noise in about -1..1: a few incommensurate sines (gusts and turbulence).
float smooth_noise(float t, std::uint32_t seed);

} // namespace khdays::secondary
