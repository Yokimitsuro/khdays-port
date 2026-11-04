// Collision tests and the triangle BVH (collision.h).

#include "collision.h"

#include <algorithm>
#include <array>

namespace khdays::secondary {

// Ericson, Real-Time Collision Detection 5.1.5: by the Voronoi regions of the triangle
Vec3 closest_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c)
{
    // an edge with no length (two corners on one place) has no share along it
    const auto share = [](float part, float whole) { return whole > 0.0F ? part / whole : 0.0F; };
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F)
        return a;
    const Vec3 bp = p - b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3)
        return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F)
        return a + ab * share(d1, d1 - d3);
    const Vec3 cp = p - c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6)
        return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F)
        return a + ac * share(d2, d2 - d6);
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F)
        return b + (c - b) * share(d4 - d3, (d4 - d3) + (d5 - d6));
    const float denom = 1.0F / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

// Ericson 5.1.9
void closest_segments(Vec3 p0, Vec3 p1, Vec3 q0, Vec3 q1, float &s, float &t)
{
    const Vec3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
    const float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    constexpr float eps = 1e-12F;
    if (a <= eps && e <= eps) {
        s = t = 0.0F;
        return;
    }
    if (a <= eps) {
        s = 0.0F;
        t = minf(1.0F, maxf(0.0F, f / e));
        return;
    }
    const float c = dot(d1, r);
    if (e <= eps) {
        t = 0.0F;
        s = minf(1.0F, maxf(0.0F, -c / a));
        return;
    }
    const float b = dot(d1, d2), denom = a * e - b * b;
    s = denom > eps ? minf(1.0F, maxf(0.0F, (b * f - c * e) / denom)) : 0.0F;
    t = (b * s + f) / e;
    if (t < 0.0F) {
        t = 0.0F;
        s = minf(1.0F, maxf(0.0F, -c / a));
    } else if (t > 1.0F) {
        t = 1.0F;
        s = minf(1.0F, maxf(0.0F, (b - c) / a));
    }
}

float closest_segment_triangle(Vec3 p0, Vec3 p1, Vec3 a, Vec3 b, Vec3 c, Vec3 &on_segment, Vec3 &on_triangle,
                               bool &crosses)
{
    crosses = false;
    // through the triangle: the segment meets its plane inside it
    const Vec3 n = cross(b - a, c - a);
    const float d0 = dot(p0 - a, n), d1 = dot(p1 - a, n);
    if ((d0 < 0.0F) != (d1 < 0.0F) && d0 != d1) {
        const float u = d0 / (d0 - d1);
        const Vec3 x = p0 + (p1 - p0) * u;
        const Vec3 q = closest_on_triangle(x, a, b, c);
        if (dot(q - x, q - x) < 1e-12F) {
            crosses = true;
            on_segment = on_triangle = x;
            return 0.0F;
        }
    }
    // else the nearest of: each end against the triangle, the segment against each edge
    float best = 1e30F;
    const auto keep = [&](Vec3 s, Vec3 t) {
        const float d = dot(s - t, s - t);
        if (d < best) {
            best = d;
            on_segment = s;
            on_triangle = t;
        }
    };
    keep(p0, closest_on_triangle(p0, a, b, c));
    keep(p1, closest_on_triangle(p1, a, b, c));
    const Vec3 edge[3][2] = {{a, b}, {b, c}, {c, a}};
    for (const auto &e : edge) {
        float s, t;
        closest_segments(p0, p1, e[0], e[1], s, t);
        keep(lerp(p0, p1, s), lerp(e[0], e[1], t));
    }
    return std::sqrt(best);
}

void TriangleBvh::build(std::vector<Triangle> triangles)
{
    tris_ = std::move(triangles);
    nodes_.clear();
    order_.resize(tris_.size());
    boxes_.resize(tris_.size());
    for (std::size_t i = 0; i < tris_.size(); ++i) {
        order_[i] = static_cast<int>(i);
        Aabb b;
        b.add(tris_[i].a);
        b.add(tris_[i].b);
        b.add(tris_[i].c);
        boxes_[i] = b;
    }
    nodes_.reserve(tris_.size() * 2 + 1);
    if (!tris_.empty())
        split(0, static_cast<int>(tris_.size()), 0);
}

int TriangleBvh::split(int first, int count, int depth)
{
    const int index = static_cast<int>(nodes_.size());
    nodes_.emplace_back();
    Aabb box, centres;
    for (int i = first; i < first + count; ++i) {
        const Aabb &b = boxes_[static_cast<std::size_t>(order_[static_cast<std::size_t>(i)])];
        box.add(b.lo);
        box.add(b.hi);
        centres.add((b.lo + b.hi) * 0.5F);
    }
    nodes_[static_cast<std::size_t>(index)].box = box;
    if (count <= 4 || depth > 40) {
        nodes_[static_cast<std::size_t>(index)].first = first;
        nodes_[static_cast<std::size_t>(index)].count = count;
        return index;
    }
    const Vec3 ext = centres.hi - centres.lo;
    const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
    const auto key = [&](int t) {
        const Aabb &b = boxes_[static_cast<std::size_t>(t)];
        return axis == 0 ? b.lo.x + b.hi.x : axis == 1 ? b.lo.y + b.hi.y : b.lo.z + b.hi.z;
    };
    const int half = count / 2;
    std::nth_element(order_.begin() + first, order_.begin() + first + half, order_.begin() + first + count,
                     [&](int x, int y) { return key(x) < key(y); });
    const int l = split(first, half, depth + 1);
    const int r = split(first + half, count - half, depth + 1);
    nodes_[static_cast<std::size_t>(index)].left = l;
    nodes_[static_cast<std::size_t>(index)].right = r;
    return index;
}

void TriangleBvh::query(const Aabb &box, std::vector<int> &out) const
{
    if (nodes_.empty())
        return;
    std::array<int, 96> stack{};
    std::size_t top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node &n = nodes_[static_cast<std::size_t>(stack[--top])];
        if (!n.box.overlaps(box))
            continue;
        if (n.left < 0) {
            for (int i = n.first; i < n.first + n.count; ++i) {
                const int t = order_[static_cast<std::size_t>(i)];
                if (boxes_[static_cast<std::size_t>(t)].overlaps(box))
                    out.push_back(t);
            }
        } else if (top + 2 <= stack.size()) {
            stack[top++] = n.left;
            stack[top++] = n.right;
        }
    }
}

StaticEnvironment::StaticEnvironment(std::vector<Triangle> triangles)
{
    bvh_.build(std::move(triangles));
}

void StaticEnvironment::gather(const Aabb &box, std::vector<Triangle> &out)
{
    hits_.clear();
    bvh_.query(box, hits_);
    for (const int i : hits_)
        out.push_back(bvh_.triangles()[static_cast<std::size_t>(i)]);
}

void StaticEnvironment::translate(Vec3 delta)
{
    std::vector<Triangle> t = bvh_.triangles();
    for (auto &x : t) {
        x.a += delta;
        x.b += delta;
        x.c += delta;
    }
    bvh_.build(std::move(t));
}

void box_triangles(Vec3 lo, Vec3 hi, Surface kind, std::vector<Triangle> &out)
{
    const Vec3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
                       {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}};
    // each face's corners counter-clockwise seen from outside
    const int f[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
    for (const auto &q : f) {
        const Vec3 a = p[q[0]], b = p[q[1]], c = p[q[2]], d = p[q[3]];
        const Vec3 n = normalized(cross(b - a, c - a));
        out.push_back({a, b, c, n, kind});
        out.push_back({a, c, d, n, kind});
    }
}

} // namespace khdays::secondary
