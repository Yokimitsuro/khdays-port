// The secondary-motion solver's small vector algebra: 3-vectors, unit quaternions and frames
// (rotation, translation, per-axis scale). Matrices are 4x4 column-major for column vectors, as
// models.cpp and the asset library keep them (docs/SECONDARY_MOTION.md).
#pragma once

#include <array>
#include <cmath>

namespace khdays::secondary {

using Mat4 = std::array<float, 16>;
constexpr Mat4 kIdentity4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// min and max inlined (MSVC calls the C library's fmin/fmax, which also order NaNs)
inline float minf(float a, float b)
{
    return a < b ? a : b;
}
inline float maxf(float a, float b)
{
    return a > b ? a : b;
}
inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

struct Vec3 {
    float x = 0.0F, y = 0.0F, z = 0.0F;
};

inline Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator-(Vec3 a)
{
    return {-a.x, -a.y, -a.z};
}
inline Vec3 operator*(Vec3 a, float s)
{
    return {a.x * s, a.y * s, a.z * s};
}
inline Vec3 operator*(float s, Vec3 a)
{
    return {a.x * s, a.y * s, a.z * s};
}
inline Vec3 &operator+=(Vec3 &a, Vec3 b)
{
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}
inline Vec3 &operator-=(Vec3 &a, Vec3 b)
{
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}
inline float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a)
{
    return std::sqrt(dot(a, a));
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t)
{
    return a + (b - a) * t;
}
// `a` scaled to unit length, or `fallback` when it is (nearly) zero
inline Vec3 normalized(Vec3 a, Vec3 fallback = {0.0F, 1.0F, 0.0F})
{
    const float l = length(a);
    return l > 1e-12F ? a * (1.0F / l) : fallback;
}
inline bool finite(Vec3 a)
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
// a unit vector at right angles to unit `a`
inline Vec3 perpendicular(Vec3 a)
{
    const Vec3 b = std::fabs(a.x) < 0.6F ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    return normalized(cross(a, b));
}

struct Quat {
    float x = 0.0F, y = 0.0F, z = 0.0F, w = 1.0F;
};

inline Quat operator*(Quat a, Quat b)
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat conjugate(Quat q)
{
    return {-q.x, -q.y, -q.z, q.w};
}
inline Quat normalized(Quat q)
{
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(l > 1e-12F))
        return {};
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}
inline Vec3 rotate(Quat q, Vec3 v)
{
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0F;
    return v + t * q.w + cross(u, t);
}
inline Quat axis_angle(Vec3 unit_axis, float radians)
{
    const float s = std::sin(radians * 0.5F);
    return {unit_axis.x * s, unit_axis.y * s, unit_axis.z * s, std::cos(radians * 0.5F)};
}
// The shortest turn taking unit `from` to unit `to`; exactly opposite: a half turn about any
// perpendicular axis.
inline Quat arc(Vec3 from, Vec3 to)
{
    const float c = dot(from, to);
    if (c < -0.999999F)
        return axis_angle(perpendicular(from), 3.14159265F);
    const Vec3 a = cross(from, to);
    return normalized(Quat{a.x, a.y, a.z, 1.0F + c});
}
// Normalized linear interpolation along the shorter way (frames a substep apart turn little).
inline Quat nlerp(Quat a, Quat b, float t)
{
    const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = d < 0.0F ? -1.0F : 1.0F;
    return normalized(Quat{a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t,
                           a.w + (s * b.w - a.w) * t});
}
inline float angle_between(Quat a, Quat b)
{
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.0F * std::acos(std::fmin(1.0F, d));
}

// The rotation of an orthonormal basis (columns x, y, z).
inline Quat from_basis(Vec3 x, Vec3 y, Vec3 z)
{
    const float trace = x.x + y.y + z.z;
    Quat q;
    if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        q = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25F * s};
    } else if (x.x > y.y && x.x > z.z) {
        const float s = std::sqrt(1.0F + x.x - y.y - z.z) * 2.0F;
        q = {0.25F * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        const float s = std::sqrt(1.0F + y.y - x.x - z.z) * 2.0F;
        q = {(y.x + x.y) / s, 0.25F * s, (z.y + y.z) / s, (z.x - x.z) / s};
    } else {
        const float s = std::sqrt(1.0F + z.z - x.x - y.y) * 2.0F;
        q = {(z.x + x.z) / s, (z.y + y.z) / s, 0.25F * s, (x.y - y.x) / s};
    }
    return normalized(q);
}

// A bone's place: its rotation, translation and scale along its own axes (matrix = T * R * S).
// A blended (non-rigid) matrix gives the rotation of its Gram-Schmidt basis and its column lengths.
struct Frame {
    Quat q;
    Vec3 t;
    Vec3 s{1.0F, 1.0F, 1.0F};
};

inline Vec3 apply(const Frame &f, Vec3 local)
{
    return f.t + rotate(f.q, Vec3{local.x * f.s.x, local.y * f.s.y, local.z * f.s.z});
}
// the inverse of apply
inline Vec3 apply_inverse(const Frame &f, Vec3 world)
{
    const Vec3 l = rotate(conjugate(f.q), world - f.t);
    return {f.s.x != 0.0F ? l.x / f.s.x : 0.0F, f.s.y != 0.0F ? l.y / f.s.y : 0.0F, f.s.z != 0.0F ? l.z / f.s.z : 0.0F};
}

inline Frame frame_of(const Mat4 &m)
{
    Frame f;
    Vec3 x{m[0], m[1], m[2]}, y{m[4], m[5], m[6]}, z{m[8], m[9], m[10]};
    f.s = {length(x), length(y), length(z)};
    x = normalized(x, {1, 0, 0});
    y = normalized(y - x * dot(x, y), perpendicular(x));
    const Vec3 zz = cross(x, y);
    if (dot(zz, z) < 0.0F) // mirrored: keep a rotation, carry the sign in the scale
        f.s.z = -f.s.z;
    f.q = from_basis(x, y, zz);
    f.t = {m[12], m[13], m[14]};
    return f;
}

inline Mat4 matrix_of(const Frame &f)
{
    const Vec3 x = rotate(f.q, {f.s.x, 0, 0}), y = rotate(f.q, {0, f.s.y, 0}), z = rotate(f.q, {0, 0, f.s.z});
    return {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, f.t.x, f.t.y, f.t.z, 1};
}

inline Frame interpolate(const Frame &a, const Frame &b, float t)
{
    return {nlerp(a.q, b.q, t), lerp(a.t, b.t, t), lerp(a.s, b.s, t)};
}

// b expressed in a's space (rigid part; scales kept from b)
inline Frame relative(const Frame &a, const Frame &b)
{
    const Quat ia = conjugate(a.q);
    return {ia * b.q, rotate(ia, b.t - a.t), b.s};
}
// rel placed in a's space (the inverse of relative)
inline Frame compose(const Frame &a, const Frame &rel)
{
    return {normalized(a.q * rel.q), a.t + rotate(a.q, rel.t), rel.s};
}

inline Mat4 mul(const Mat4 &a, const Mat4 &b)
{
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0F;
            for (int k = 0; k < 4; ++k)
                s += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
            r[static_cast<std::size_t>(c * 4 + row)] = s;
        }
    return r;
}

inline Vec3 point(const Mat4 &m, Vec3 p)
{
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}

// The inverse of an affine matrix (its last row 0 0 0 1); the identity when singular.
inline Mat4 inverse_affine(const Mat4 &m)
{
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-20F)
        return kIdentity4;
    const float k = 1.0F / det;
    Mat4 r{};
    r[0] = A * k;
    r[1] = B * k;
    r[2] = C * k;
    r[4] = -(b * i - c * h) * k;
    r[5] = (a * i - c * g) * k;
    r[6] = -(a * h - b * g) * k;
    r[8] = (b * f - c * e) * k;
    r[9] = -(a * f - c * d) * k;
    r[10] = (a * e - b * d) * k;
    r[12] = -(r[0] * m[12] + r[4] * m[13] + r[8] * m[14]);
    r[13] = -(r[1] * m[12] + r[5] * m[13] + r[9] * m[14]);
    r[14] = -(r[2] * m[12] + r[6] * m[13] + r[10] * m[14]);
    r[15] = 1.0F;
    return r;
}

constexpr float kPi = 3.14159265358979F;
constexpr float kDegrees = kPi / 180.0F;

} // namespace khdays::secondary
