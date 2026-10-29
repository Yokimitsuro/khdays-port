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
    bool player = true;           // the player keeps the full detail
    Environment *environment = nullptr;
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
    Vec3 wind_of(const Wind &w, int bone, const ActorInput &in, std::size_t group) const;
    const LodConfig &lod(const ActorInput &in) const;

    std::shared_ptr<const Rig> rig_;
    Simulation sim_;
    std::uint32_t seed_;
    bool started_ = false, frozen_ = false;
    std::uint64_t epoch_ = 0, generation_ = 0;
    double last_seconds_ = 0.0, sim_seconds_ = 0.0;
    std::int64_t ticks_done_ = 0; // ticks since the start, sim_seconds_ = ticks_done_ / rate from it
    std::vector<Frame> last_pose_, from_, to_;
    std::vector<float> control_applied_;
    Profile profile_;
    LodConfig full_;
};

// A deterministic smooth noise in about -1..1: a few incommensurate sines (gusts and turbulence).
float smooth_noise(float t, std::uint32_t seed);

} // namespace khdays::secondary
