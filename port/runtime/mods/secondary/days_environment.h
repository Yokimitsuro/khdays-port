// The room the game has loaded, as a secondary-motion environment (collision.h): its collision faces
// read straight from the entity manager's collision blobs, never through the game's casts (they keep
// shared state and call objects back). docs/SECONDARY_MOTION.md, "The game's rooms", has the layout
// and the functions it follows.
#pragma once

#include "collision.h"

#include <cstdint>
#include <vector>

namespace khdays::secondary {

class DaysEnvironment final : public Environment {
public:
    // `world_per_metre`: Days world units in one of the solver's metres (the actor's scale), for the
    // gather that follows.
    void set_scale(float world_per_metre)
    {
        scale_ = world_per_metre > 0.0F ? world_per_metre : 1.0F;
    }
    // Re-reads which blobs are loaded (cheap: a few words each) and rebuilds a room's BVH when it changed.
    // Call on the game's thread once a frame, before gathering.
    void refresh();
    void gather(const Aabb &box, std::vector<Triangle> &out) override;
    std::uint64_t generation() const override
    {
        return generation_;
    }
    std::size_t rooms() const
    {
        return rooms_.size();
    }

private:
    struct Room {
        std::uint32_t blob = 0;
        std::uint32_t key[8] = {};       // the header words that change with the room (counts, region, pointers)
        TriangleBvh bvh;                 // world units
        std::vector<std::uint32_t> face; // per triangle: its face record's DS address (its disabled flag)
    };
    struct Box { // a room box (a moving or opening object), world units
        Vec3 pos;
        float half_w = 0, half_d = 0, height = 0, angle = 0;
    };
    void build(Room &room);
    std::vector<Room> rooms_;
    std::vector<Box> boxes_;
    std::vector<int> hits_;
    std::vector<Triangle> box_tris_;
    float scale_ = 1.0F;
    std::uint64_t generation_ = 1;
    std::uint32_t manager_ = 0;
};

} // namespace khdays::secondary
