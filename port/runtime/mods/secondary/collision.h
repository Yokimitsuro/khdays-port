// Collision for the secondary-motion solver: closest-point tests, a static triangle BVH, and the
// environment a simulation asks for the triangles near it (docs/SECONDARY_MOTION.md, "Collision").
// The solver's space: the world's axes (y up), metres of the model.
#pragma once

#include "vecmath.h"

#include <cstdint>
#include <vector>

namespace khdays::secondary {

struct Aabb {
    Vec3 lo{1e30F, 1e30F, 1e30F}, hi{-1e30F, -1e30F, -1e30F};
    void add(Vec3 p)
    {
        lo = {minf(lo.x, p.x), minf(lo.y, p.y), minf(lo.z, p.z)};
        hi = {maxf(hi.x, p.x), maxf(hi.y, p.y), maxf(hi.z, p.z)};
    }
    void grow(float r)
    {
        lo = lo - Vec3{r, r, r};
        hi = hi + Vec3{r, r, r};
    }
    bool overlaps(const Aabb &o) const
    {
        return lo.x <= o.hi.x && hi.x >= o.lo.x && lo.y <= o.hi.y && hi.y >= o.lo.y && lo.z <= o.hi.z && hi.z >= o.lo.z;
    }
};

// A one-sided collision triangle: solid behind its normal (n points to where things stand).
enum class Surface : std::uint8_t { ground, wall, ceiling, object };
struct Triangle {
    Vec3 a, b, c, n;
    Surface kind = Surface::ground;
};

// The closest point of triangle abc to p.
Vec3 closest_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c);
// The closest points of segments p0-p1 and q0-q1: their parameters s and t (0..1).
void closest_segments(Vec3 p0, Vec3 p1, Vec3 q0, Vec3 q1, float &s, float &t);
// How far along segment a-b (0..1) its closest point to p lies; 0 when it has no length.
inline float along_segment(Vec3 p, Vec3 a, Vec3 b)
{
    const Vec3 ab = b - a;
    const float ll = dot(ab, ab);
    return ll > 1e-12F ? clampf(dot(p - a, ab) / ll, 0.0F, 1.0F) : 0.0F;
}
// The closest points of segment p0-p1 and triangle abc (`on_segment`, `on_triangle`) and their distance;
// `crosses` when the segment passes through the triangle (the distance is then 0).
float closest_segment_triangle(Vec3 p0, Vec3 p1, Vec3 a, Vec3 b, Vec3 c, Vec3 &on_segment, Vec3 &on_triangle,
                               bool &crosses);

// A bounding volume hierarchy over static triangles, built once; queries allocate nothing once the
// output has grown.
class TriangleBvh {
public:
    void build(std::vector<Triangle> triangles);
    void query(const Aabb &box, std::vector<int> &out) const; // indices into triangles()
    const std::vector<Triangle> &triangles() const
    {
        return tris_;
    }

private:
    struct Node {
        Aabb box;
        int left = -1, right = -1; // children, or -1 for a leaf
        int first = 0, count = 0;  // a leaf's triangles in order_
    };
    int split(int first, int count, int depth);
    std::vector<Triangle> tris_;
    std::vector<Node> nodes_;
    std::vector<int> order_;
    std::vector<Aabb> boxes_;
};

// What a simulation collides with besides its own body: the room, read, never cast through the
// game's functions. `gather` runs on the game's thread once a tick, appending the triangles within
// `box`.
class Environment {
public:
    virtual ~Environment() = default;
    virtual void gather(const Aabb &box, std::vector<Triangle> &out) = 0;
};

// An environment of fixed triangles (tests, the replay tool, and a room cached by its provider).
class StaticEnvironment final : public Environment {
public:
    explicit StaticEnvironment(std::vector<Triangle> triangles);
    void gather(const Aabb &box, std::vector<Triangle> &out) override;
    // moved by `delta` (a moving object in tests)
    void translate(Vec3 delta);

private:
    TriangleBvh bvh_;
    std::vector<int> hits_;
};

// Triangles of an axis-aligned box (its faces' normals outward), for tests and objects.
void box_triangles(Vec3 lo, Vec3 hi, Surface kind, std::vector<Triangle> &out);

} // namespace khdays::secondary
