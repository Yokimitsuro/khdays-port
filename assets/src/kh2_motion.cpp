#include "khdays/assets/kh2_motion.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "khdays/assets/kh2_bar.h"

// Each step follows the function of KH2's PS3 build (motion_prototype0.cpp)
// named beside it; docs/KH2_MODELS.md lists the tables. Where the PS3 code does
// something surprising it is kept, and said so.

namespace khdays::assets::kh2 {

namespace {

constexpr Mat4 kUnit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr float kPi = 3.1415927F;
// Past these a malformed file is cut short; the 388 motions of the HD pack's msets reach depth 13 and 190 visits.
constexpr int kExpressionDepth = 64;
constexpr std::uint32_t kExpressionVisits = 1U << 20U;  // per evaluation
constexpr int kMostRepeats = 4096;                      // an F-curve's repeats counted out, before fmod

// The motion's header fields (kn::MotionPrototype0RAW), offsets from the header.
enum : std::size_t {
    kModelNum = 0x10,
    kCalcNum = 0x12,
    kHrc = 0x18,
    kFlags = 0x1C,
    kTimeNum = 0x20,
    kConstants = 0x24,
    kConstantNum = 0x28,
    kPositionInfo = 0x2C,
    kDirect = 0x30,
    kDirectNum = 0x34,
    kIndirect = 0x38,
    kIndirectNum = 0x3C,
    kKeys = 0x40,
    kTimes = 0x44,
    kValues = 0x48,
    kSlopes = 0x4C,
    kConstraints = 0x50,
    kConstraintNum = 0x54,
    kActive = 0x58,
    kLimiters = 0x5C,
    kExpressions = 0x60,
    kExpressionNum = 0x64,
    kNodes = 0x68,
    kNodeNum = 0x6C,
    kStart = 0x90,
    kEnd = 0x94,
    kFps = 0x98,
    kRet = 0x9C,
    kExtEffector = 0xA0,
    kHeaderSize = 0xB0,
};

class Reader final {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    const std::uint8_t* at(std::size_t o, std::size_t n) const {
        if (o > size_ || n > size_ - o) {
            throw std::runtime_error(
                "KH2 motion: a table runs past the end of its data; the file is damaged: extract it again from your "
                "game");
        }
        return data_ + o;
    }
    std::uint8_t u8(std::size_t o) const {
        return *at(o, 1);
    }
    std::uint16_t u16(std::size_t o) const {
        const auto* p = at(o, 2);
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8U));
    }
    std::int16_t i16(std::size_t o) const {
        return static_cast<std::int16_t>(u16(o));
    }
    std::uint32_t u32(std::size_t o) const {
        const auto* p = at(o, 4);
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
               (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
    }
    std::int32_t i32(std::size_t o) const {
        return static_cast<std::int32_t>(u32(o));
    }
    float f32(std::size_t o) const {
        const std::uint32_t bits = u32(o);
        float f = 0.0F;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }
    Vec4 vec(std::size_t o) const {
        return {f32(o), f32(o + 4), f32(o + 8), f32(o + 12)};
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
};

// --- Axa::FMATRIX44 and kn::FMatrix ---

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t row = 0; row < 4; ++row) {
            float s = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                s += a[k * 4 + row] * b[c * 4 + k];
            }
            r[c * 4 + row] = s;
        }
    }
    return r;
}

Vec4 mul(const Mat4& m, const Vec4& v) {
    Vec4 r{};
    for (std::size_t row = 0; row < 4; ++row) {
        r[row] = m[row] * v[0] + m[4 + row] * v[1] + m[8 + row] * v[2] + m[12 + row] * v[3];
    }
    return r;
}

// kn::applyMatrixXYZw1: m * (v.xyz, 1), w 1.
Vec4 apply1(const Mat4& m, const Vec4& v) {
    Vec4 r = mul(m, Vec4{v[0], v[1], v[2], 1.0F});
    r[3] = 1.0F;
    return r;
}

Vec4 column(const Mat4& m, std::size_t c) {
    return {m[c * 4], m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3]};
}

void set_column(Mat4& m, std::size_t c, const Vec4& v) {
    for (std::size_t i = 0; i < 4; ++i) {
        m[c * 4 + i] = v[i];
    }
}

Mat4 inverse(const Mat4& m) {
    Mat4 inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0F) {
        return kUnit;
    }
    for (float& f : inv) {
        f /= det;
    }
    return inv;
}

float wrap(float a) {
    // only a malformed file's are this far out: one turn at a time would never end
    if (std::fabs(a) >= 1e4F) {
        a = std::isfinite(a) ? std::fmod(a, 6.2831855F) : 0.0F;
    }
    if (a <= 0.0F) {
        while (a < -kPi) {
            a += 6.2831855F;
        }
    } else {
        while (a > kPi) {
            a -= 6.2831855F;
        }
    }
    return a;
}

// kn::FMatrix::rotateX/Y/Z (0xdb830...): the turn applied after the matrix (R * m).
Mat4 rotate(const Mat4& m, int axis, float a) {
    if (a == 0.0F) {
        return m;
    }
    a = wrap(a);
    const float c = std::cos(a), s = std::sin(a);
    Mat4 r = kUnit;
    const std::size_t i = axis == 0 ? 1U : 0U, j = axis == 2 ? 1U : 2U;  // the plane it turns
    const float sign = axis == 1 ? -1.0F : 1.0F;
    r[i * 4 + i] = c;
    r[j * 4 + j] = c;
    r[i * 4 + j] = s * sign;
    r[j * 4 + i] = -s * sign;
    return mul(r, m);
}

// kn::FMatrix::rotateXYZ (0xdbd58): Rz * Ry * Rx * m.
Mat4 rotate_xyz(const Mat4& m, const Vec4& r) {
    return rotate(rotate(rotate(m, 0, r[0]), 1, r[1]), 2, r[2]);
}

// kn::FMatrix::scaling (0xdb568): the columns scaled.
void scaling(Mat4& m, const Vec4& s) {
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t i = 0; i < 4; ++i) {
            m[c * 4 + i] *= s[c];
        }
    }
}

std::array<float, 3> cross(const Vec4& a, const Vec4& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// Axa::FVECTOR4::normalize: x y z to a length of 1; returns the length.
float normalize(Vec4& v) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.0F) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
    return l;
}

Vec4 as_vec(const std::array<float, 3>& a) {
    return {a[0], a[1], a[2], 0.0F};
}

// kn::FMatrix::toRot (0xdc2a8): the turn x y z of R = Rz * Ry * Rx, of the two
// that make it the one nearer `rot`'s, added to it.
void to_rot(const Mat4& m, Vec4& rot) {
    const float sy = std::clamp(m[2], -1.0F, 1.0F);
    float x = 0.0F, y = 0.0F, z = 0.0F;
    if (std::sqrt(std::max(0.0F, m[0] * m[0] + m[1] * m[1])) <= 0.0000019073486F) {
        const float a = sy <= 0.0F ? std::atan2(m[4], m[5]) : std::atan2(-m[4], m[5]);
        y = std::asin(-sy);
        x = a * 0.5F;
        z = sy < 0.0F ? -x : x;
    } else {
        z = std::atan2(m[1], m[0]);
        y = std::asin(-sy);
        x = std::atan2(m[6], m[10]);
    }
    const Vec4 a{wrap(x - rot[0]), wrap(y - rot[1]), wrap(z - rot[2]), 0.0F};
    const Vec4 b{wrap(x + kPi - rot[0]), wrap(kPi - y - rot[1]), wrap(z + kPi - rot[2]), 0.0F};
    const Vec4& d = a[0] * a[0] + a[1] * a[1] + a[2] * a[2] > b[0] * b[0] + b[1] * b[1] + b[2] * b[2] ? b : a;
    rot[0] += d[0];
    rot[1] += d[1];
    rot[2] += d[2];
}

// kn::align_XY (0xfc0e4): X along `x`, Y toward `y`; the translation kept.
void align_xy(const Vec4& x, const Vec4& y, Mat4& m) {
    Vec4 c0 = x;
    normalize(c0);
    c0[3] = 0.0F;
    const Vec4 z0 = as_vec(cross(c0, y));
    Vec4 c1 = as_vec(cross(z0, x));
    normalize(c1);
    Vec4 c2 = as_vec(cross(c0, c1));
    normalize(c2);
    set_column(m, 0, c0);
    set_column(m, 1, c1);
    set_column(m, 2, c2);
}

// kn::Motion::calcIKJoint (0xf56a0): the bone from `self` aimed at `target`
// (mat0), and with `mat1` the two bones of lengths len0 and len1 bent to reach
// it, the knee turned by `sgn`. A turn about X (prot0, prot1) is kept on each.
void ik_joint(const Vec4& self, const Vec4& target, float len0, float len1, bool sgn, Mat4& mat0, Mat4* mat1,
              float prot0, float prot1, const Vec4* up) {
    Vec4 x{target[0] - self[0], target[1] - self[1], target[2] - self[2], 0.0F};
    const float distance = normalize(x);
    Vec4 y{}, z{};
    if (up != nullptr) {
        y = {(*up)[0] - self[0], (*up)[1] - self[1], (*up)[2] - self[2], 0.0F};
        normalize(y);
        z = as_vec(cross(x, y));
        normalize(z);
        y = as_vec(cross(z, x));
        normalize(y);
    } else {
        y = {-x[1], x[0], 0.0F, 0.0F};
        normalize(y);
        z = as_vec(cross(x, y));
        normalize(z);
    }
    Mat4 base = kUnit;
    set_column(base, 0, x);
    set_column(base, 1, y);
    set_column(base, 2, z);
    if (prot0 != 0.0F) {
        base = mul(base, rotate(kUnit, 0, prot0));
    }
    if (mat1 == nullptr) {
        mat0 = base;
        return;
    }
    const float twice = len0 * 2.0F * len1;
    float c = twice != 0.0F ? (distance * distance - len0 * len0 - len1 * len1) / twice : 1.0F;
    c = std::clamp(c, -1.0F, 1.0F);
    float s = std::sqrt(std::max(0.0F, 1.0F - c * c));
    if (sgn) {
        s = -s;
    }
    *mat1 = kUnit;
    (*mat1)[0] = c;
    (*mat1)[1] = s;
    (*mat1)[4] = -s;
    (*mat1)[5] = c;
    if (prot1 != 0.0F) {
        *mat1 = mul(*mat1, rotate(kUnit, 0, prot1));
    }
    const float across = len1 * s * distance, along = (c * len1 + len0) * distance;
    const float r = std::sqrt(along * along + across * across);
    const float c0 = r > 0.0F ? along / r : 0.0F, s0 = r > 0.0F ? -across / r : 0.0F;
    Mat4 turn = kUnit;
    turn[0] = c0;
    turn[1] = s0;
    turn[4] = -s0;
    turn[5] = c0;
    mat0 = mul(base, turn);
}

}  // namespace

// --- The skeleton ---

MotionSkeleton read_motion_skeleton(const std::uint8_t* data, const std::size_t size) {
    const Reader b(data, size);
    MotionSkeleton s;
    for (const BarEntry& e : bar_entries(data, size)) {
        if (e.type == 0x04 && e.size != 0 && s.joints.empty()) {
            const std::size_t h = e.offset + 0x90;
            const std::uint16_t bones = b.u16(h + 0x10);
            const std::size_t table = h + b.u32(h + 0x14);
            for (std::uint16_t i = 0; i < bones; ++i) {
                const std::size_t o = table + i * 0x40U;
                MotionSkeleton::Joint j;
                j.parent = b.i32(o + 4);
                j.scale = b.vec(o + 0x10);
                j.rotation = b.vec(o + 0x20);
                j.translation = b.vec(o + 0x30);
                s.joints.push_back(j);
            }
        } else if (e.type == 0x17 && e.size >= 4) {  // YS::COLLISION_ELEM, 20 bytes each after a count
            const std::size_t count = std::min<std::size_t>(b.u32(e.offset), (e.size - 4) / 20);
            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t c = e.offset + 4 + i * 20;
                const std::uint8_t type = b.u8(c + 4);
                if (type == 10 || type == 11) {
                    s.feet.push_back({b.u16(c + 6) & 0xFFF, type == 11, static_cast<float>(b.i16(c + 0x12))});
                }
            }
        }
    }
    if (s.joints.empty()) {
        throw std::runtime_error("KH2 model: the BAR has no model entry (type 4) to move; name a KH2 obj/*.mdlx");
    }
    return s;
}

// --- A motion ---

namespace {

// Every F-curve's keys, as fcurve reads them: a slope only where the key's type or the curve's mode uses it.
void validate_curves(const Reader& m, const std::size_t table, const std::uint32_t count) {
    const std::size_t keys = m.u32(kKeys), times = m.u32(kTimes), values = m.u32(kValues), slopes = m.u32(kSlopes);
    const auto left = [&](std::size_t k) { (void)m.at(slopes + m.u16(keys + k * 8U + 4) * 4U, 4); };
    const auto right = [&](std::size_t k) { (void)m.at(slopes + m.u16(keys + k * 8U + 6) * 4U, 4); };
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t f = table + i * 6U;
        const unsigned modes = m.u8(f + 2);
        const std::size_t num = m.u8(f + 3), first = m.u16(f + 4);
        if (num == 0) {
            continue;
        }
        const std::size_t last = first + num - 1;
        for (std::size_t k = first; k <= last; ++k) {
            const std::uint16_t word = m.u16(keys + k * 8U);
            (void)m.at(times + (word & 0xFFFCU), 4);
            (void)m.at(values + m.u16(keys + k * 8U + 2) * 4U, 4);
            if ((word & 3U) == 2U) {
                right(k);
                left(std::min(k + 1, last));
            }
        }
        if (((modes >> 4U) & 3U) == 1U) {
            left(first);
        }
        if ((modes >> 6U) == 1U) {
            right(last);
        }
    }
}

// What each pass reads through an offset the motion's header gives, so a bad file fails at load, not while drawn.
void validate_tables(const Reader& m, const std::size_t joints) {
    const std::uint32_t direct = m.u32(kDirectNum), indirect = m.u32(kIndirectNum);
    validate_curves(m, m.u32(kDirect), direct);
    validate_curves(m, m.u32(kIndirect), indirect);
    const std::size_t position = m.u32(kPositionInfo);
    (void)m.at(position, 16);
    if (m.i32(position + 12) != 0) {
        (void)m.at(position, 84);
        for (std::size_t i = 0; i < 9; ++i) {
            const std::int64_t f = m.i32(position + 48 + i * 4);
            if (f != -1 && (f < 0 || f >= std::int64_t{direct} + indirect)) {
                throw std::runtime_error("KH2 motion: its root motion names F-curve " + std::to_string(f) +
                                         "; the file is damaged: extract it again from your game");
            }
        }
    }
    const std::size_t constraints = m.u32(kConstraints), active = m.u32(kActive), limiters = m.u32(kLimiters);
    for (std::uint32_t i = 0; i < m.u32(kConstraintNum); ++i) {
        const std::size_t c = constraints + i * 12U;
        const std::size_t num = m.u16(c + 8), base = m.u16(c + 10);
        if (num != 0) {
            (void)m.at(active + base * 8U, num * 8U);
        }
        if (const std::uint8_t type = m.u8(c); type == 12 || type == 13) {
            (void)m.at(limiters + m.u16(c + 6) * 48U, 48);
        }
    }
    std::size_t effectors = 0;
    for (std::size_t k = 0; k < joints; ++k) {
        const std::uint32_t flag = m.u32(m.u32(kFlags) + k * 4U);
        if ((flag & 0xFFFFU) >= joints) {
            throw std::runtime_error("KH2 motion: its joint list names joint " + std::to_string(flag & 0xFFFFU) +
                                     " of " + std::to_string(joints) +
                                     "; the file is damaged: extract it again from your game");
        }
        effectors += ((flag >> 16U) & 3U) != 0 && ((flag >> 23U) & 1U) != 0 ? 1U : 0U;
    }
    if (effectors != 0) {
        (void)m.at(m.u32(kExtEffector), effectors * 2U);
    }
}

}  // namespace

Motion::Motion(const std::uint8_t* data, const std::size_t size, const std::size_t at) {
    const Reader b(data, size);
    (void)b.at(at, kHeaderSize);
    // its type, the header's first word (kn::motionSetup 0xf8e8c): 0 MotionPrototype0, 1 MotionRAW
    if (b.u32(at) != 0) {
        throw std::runtime_error("KH2 motion: type " + std::to_string(b.u32(at)) +
                                 " (only interpolated motions, MotionPrototype0, are played)");
    }
    bytes_.assign(data + at, data + size);
    const Reader m(bytes_.data(), bytes_.size());
    model_num_ = m.u16(kModelNum);
    calc_num_ = m.u16(kCalcNum);
    start_ = m.f32(kStart);
    end_ = m.f32(kEnd);
    fps_ = m.f32(kFps);
    ret_ = m.f32(kRet);
    if (calc_num_ < model_num_ || !(fps_ > 0.0F)) {
        throw std::runtime_error(
            "KH2 motion: a header with fewer joints than its model's, or no frame rate; the file is damaged: extract "
            "it again from your game");
    }
    // the tables each pass reads, whole
    (void)m.at(m.u32(kHrc), static_cast<std::size_t>(calc_num_ - model_num_) * 64U);
    (void)m.at(m.u32(kFlags), static_cast<std::size_t>(calc_num_) * 4U);
    (void)m.at(m.u32(kTimes), static_cast<std::size_t>(m.u32(kTimeNum)) * 4U);
    (void)m.at(m.u32(kConstants), static_cast<std::size_t>(m.u32(kConstantNum)) * 8U);
    (void)m.at(m.u32(kDirect), static_cast<std::size_t>(m.u32(kDirectNum)) * 6U);
    (void)m.at(m.u32(kIndirect), static_cast<std::size_t>(m.u32(kIndirectNum)) * 6U);
    (void)m.at(m.u32(kConstraints), static_cast<std::size_t>(m.u32(kConstraintNum)) * 12U);
    (void)m.at(m.u32(kExpressions), static_cast<std::size_t>(m.u32(kExpressionNum)) * 8U);
    (void)m.at(m.u32(kNodes), static_cast<std::size_t>(m.u32(kNodeNum)) * 12U);
    validate_tables(m, static_cast<std::size_t>(calc_num_));
}

namespace {

// MotionPrototype0::getTimeIndex (0xfa028): the time table's entries either side of `t`.
void time_index(const Reader& m, float t, int& before, int& after) {
    const int n = static_cast<int>(m.u32(kTimeNum));
    const std::size_t times = m.u32(kTimes);
    const auto time = [&](int i) { return m.f32(times + static_cast<std::size_t>(i) * 4U); };
    if (n == 0 || t >= time(std::max(n - 1, 0))) {
        before = n - 1;
        after = n;
        return;
    }
    if (t <= time(0)) {
        before = -1;
        after = 0;
        return;
    }
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        const int mid = (lo + hi) / 2;
        const float v = time(mid);
        if (v <= t) {
            if (v >= t) {
                before = after = mid;
                return;
            }
            lo = mid;
        } else {
            hi = mid;
        }
    }
    before = lo;
    after = hi;
}

// `t` before `first` brought up to it by whole periods, as the PS3 build loops; fmod once it is far out, where a
// malformed `t` would never get there.
float repeat_up(float t, const float first, const float period) {
    if (first - t > period * kMostRepeats) {
        t = first - std::fmod(first - t, period);
    }
    for (int i = 0; i <= kMostRepeats && t < first; ++i) {
        t += period;
    }
    return t;
}

// `t` after `last` brought down to it
float repeat_down(float t, const float last, const float period) {
    if (t - last > period * kMostRepeats) {
        t = last + std::fmod(t - last, period);
    }
    for (int i = 0; i <= kMostRepeats && t > last; ++i) {
        t -= period;
    }
    return t;
}

// MotionPrototype0::calcFcurveValue (0xfa118): an F-curve (6 bytes at `f`) at
// `t`; before its first key and after its last as its pre and post modes say
// (hold, the end key's slope, repeat).
float fcurve(const Reader& m, std::size_t f, int before, int after, float t) {
    const std::size_t keys = m.u32(kKeys), times = m.u32(kTimes), values = m.u32(kValues), slopes = m.u32(kSlopes);
    const std::uint8_t modes = m.u8(f + 2);
    const int num = m.u8(f + 3);
    const int base = m.u16(f + 4);
    if (num == 0) {
        return 0.0F;
    }
    const auto key = [&](int k) { return keys + static_cast<std::size_t>(k) * 8U; };
    const auto when = [&](int k) { return m.f32(times + (m.u16(key(k)) & 0xFFFCU)); };
    const auto index = [&](int k) { return static_cast<int>(m.u16(key(k)) >> 2U); };
    const auto value = [&](int k) { return m.f32(values + m.u16(key(k) + 2) * 4U); };
    const auto left = [&](int k) { return m.f32(slopes + m.u16(key(k) + 4) * 4U); };
    const auto right = [&](int k) { return m.f32(slopes + m.u16(key(k) + 6) * 4U); };
    const int first = base, last = base + num - 1;
    if (after <= index(first)) {
        const int pre = (modes >> 4U) & 3;
        if (pre == 0) {
            return value(first);
        }
        if (pre == 1) {
            return value(first) - (when(first) - t) * left(first);
        }
        if (pre == 3) {
            return 0.0F;  // asserted against
        }
        if (const float period = when(last) - when(first); period > 0.0F) {
            t = repeat_up(t, when(first), period);
        }
        time_index(m, t, before, after);
    } else if (before >= index(last)) {
        const int post = modes >> 6U;
        if (post == 0) {
            return value(last);
        }
        if (post == 1) {
            return (t - when(last)) * right(last) + value(last);
        }
        if (post == 3) {
            return 0.0F;
        }
        if (const float period = when(last) - when(first); period > 0.0F) {
            t = repeat_down(t, when(last), period);
        }
        time_index(m, t, before, after);
    }
    // the keys either side
    int k0 = first, k1 = first;
    for (int k = first; k <= last; ++k) {
        if (when(k) == t) {
            return value(k);
        }
        if (index(k) <= before) {
            k0 = k;
        }
    }
    k1 = std::min(k0 + 1, last);
    const float dt = t - when(k0);
    switch (m.u16(key(k0)) & 3U) {
    case 0: return value(k0);
    case 1: return value(k0) + (value(k1) - value(k0)) * dt / (when(k1) - when(k0));
    case 2: {
        const float d = 1.0F / (when(k1) - when(k0));
        const float t2 = dt * dt, t3 = t2 * dt;
        return left(k1) * (t3 * d * d - d * t2) + right(k0) * (dt - (d * t2 * 2.0F - t3 * d * d)) +
               value(k0) * (t3 * d * d * 2.0F * d - t2 * 3.0F * d * d + 1.0F) +
               value(k1) * -(t3 * d * d * 2.0F * d - t2 * 3.0F * d * d);
    }
    default: return 0.0F;  // asserted against
    }
}

float fcurve_at(const Reader& m, int index, float t) {
    const int direct = static_cast<int>(m.u32(kDirectNum));
    const std::size_t f = index < direct ? m.u32(kDirect) + static_cast<std::size_t>(index) * 6U
                                         : m.u32(kIndirect) + static_cast<std::size_t>(index - direct) * 6U;
    int before = 0, after = 0;
    time_index(m, t, before, after);
    return fcurve(m, f, before, after, t);
}

// MotionPrototype0::calcLimitValue (0xf9878): `value` kept within the limiter
// (48 bytes at `l`): a sphere (type 1, radius min.x) or a box on the axes its
// flags name, about `base` unless type 0 or global; with damping, eased into
// the last dampingWidth.
void limit(const Reader& m, std::size_t l, const Vec4& base, Vec4& value) {
    const std::uint32_t w = m.u32(l);
    const unsigned type = w & 7U;
    const float width = m.f32(l + 8), strength = m.f32(l + 12);
    const Vec4 lo = m.vec(l + 16), hi = m.vec(l + 32);
    const Vec4 b = type == 0 || ((w >> 3U) & 1U) != 0 ? Vec4{0, 0, 0, 1} : base;
    Vec4 v{value[0] - b[0], value[1] - b[1], value[2] - b[2], value[3] - b[3]};
    const auto ease = [&](float over) { return std::exp((strength - 1.0F) * (over / width)); };
    if (type == 1) {
        const float r = lo[0];
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        float to = -1.0F;
        if (strength == 0.0F) {
            if (length > r) {
                to = r;
            }
        } else if (const float over = length - (r - width); over > 0.0F) {
            to = width * (1.0F - ease(over)) + (r - width);
        }
        if (to >= 0.0F) {
            normalize(v);
            for (std::size_t i = 0; i < 3; ++i) {
                value[i] = b[i] + v[i] * to;
            }
        }
    } else {
        for (std::size_t i = 0; i < 3; ++i) {
            const bool has_min = ((w >> (4U + 2U * i)) & 1U) != 0, has_max = ((w >> (5U + 2U * i)) & 1U) != 0;
            if (strength == 0.0F) {
                if (has_min && v[i] < lo[i]) {
                    value[i] = b[i] + lo[i];
                } else if (has_max && v[i] > hi[i]) {
                    value[i] = b[i] + hi[i];
                }
            } else if (has_min && (lo[i] + width) - v[i] > 0.0F) {
                // as the PS3 build has it: eased above min + width, not below
                value[i] = width * (1.0F - ease((lo[i] + width) - v[i])) + (lo[i] + width + b[i]);
            } else if (has_max && v[i] - (hi[i] - width) > 0.0F) {
                value[i] = width * (1.0F - ease(v[i] - (hi[i] - width))) + (b[i] + (hi[i] - width));
            }
        }
    }
    value[3] = 1.0F;
}

}  // namespace

Mat4 Motion::root_motion(const float ticks) const {
    // MotionPrototype0::getPosition (0x1028c0): scale, rotation, translation and
    // nine F-curves (-1 none) over them; T * Rz * Ry * Rx * S.
    const Reader m(bytes_.data(), bytes_.size());
    const std::size_t p = m.u32(kPositionInfo);
    if (m.i32(p + 12) == 0) {
        return kUnit;
    }
    const float t = ticks + start_ * 60.0F / fps_;
    std::array<float, 9> c{m.f32(p),      m.f32(p + 4),  m.f32(p + 8),  m.f32(p + 16), m.f32(p + 20),
                           m.f32(p + 24), m.f32(p + 32), m.f32(p + 36), m.f32(p + 40)};
    for (std::size_t i = 0; i < 9; ++i) {
        if (const int f = m.i32(p + 48 + i * 4); f != -1) {
            c[i] = fcurve_at(m, f, t);
        }
    }
    Mat4 r = rotate_xyz(kUnit, {c[3], c[4], c[5], 0.0F});
    r[12] += c[6];
    r[13] += c[7];
    r[14] += c[8];
    scaling(r, {c[0], c[1], c[2], 1.0F});
    return r;
}

// --- The set ---

namespace {

// A motion's attributes from `at` (MotionAttributes); null when they run past `end`
std::shared_ptr<MotionAttributes> read_attributes(const std::uint8_t* data, const std::size_t at,
                                                  const std::size_t end) {
    if (end < at + 4) {
        return nullptr;
    }
    const auto u16 = [&](std::size_t p) { return static_cast<std::uint16_t>(data[p] | (data[p + 1] << 8U)); };
    auto out = std::make_shared<MotionAttributes>();
    const unsigned ranges = data[at], triggers = data[at + 1];
    std::size_t p = at + 4;
    for (unsigned k = 0; k < ranges; ++k) {
        if (p + 6 > end || p + 6 + 2U * data[p + 5] > end) {
            return nullptr;
        }
        MotionAttributes::Range r{u16(p), u16(p + 2), data[p + 4], {}};
        for (unsigned n = 0; n < data[p + 5]; ++n) {
            r.params.push_back(u16(p + 6 + 2U * n));
        }
        p += 6 + 2U * data[p + 5];
        out->ranges.push_back(std::move(r));
    }
    if (const std::uint16_t offset = u16(at + 2); offset != 0) {
        p = at + offset;
    }
    for (unsigned k = 0; k < triggers; ++k) {
        if (p + 4 > end || p + 4 + 2U * data[p + 3] > end) {
            return nullptr;
        }
        MotionAttributes::Trigger t{u16(p), data[p + 2], {}};
        for (unsigned n = 0; n < data[p + 3]; ++n) {
            t.params.push_back(u16(p + 4 + 2U * n));
        }
        p += 4 + 2U * data[p + 3];
        out->triggers.push_back(std::move(t));
    }
    return out;
}

}  // namespace

MotionSet::MotionSet(const std::uint8_t* data, const std::size_t size) {
    for (const BarEntry& e : bar_entries(data, size)) {
        names_.push_back(e.name);
        std::shared_ptr<Motion> motion;
        std::shared_ptr<MotionAttributes> attributes;
        if (e.size != 0 && is_bar(data, size, e.offset)) {
            for (const BarEntry& inner : bar_entries(data, e.offset + e.size, e.offset, true)) {
                if (inner.type == 0x09 && inner.size > 0x90 && !motion) {
                    try {
                        motion = std::make_shared<Motion>(data, inner.offset + inner.size, inner.offset + 0x90);
                    } catch (const std::runtime_error&) {
                        motion.reset();  // a slot it cannot play: the caller falls back
                    }
                } else if (inner.type == 0x10 && inner.size >= 4 && !attributes) {
                    attributes = read_attributes(data, inner.offset, inner.offset + inner.size);
                }
            }
        }
        motions_.push_back(std::move(motion));
        attributes_.push_back(std::move(attributes));
    }
}

void MotionSet::add(const std::size_t at, const MotionSet& other) {
    if (at < motions_.size()) {
        throw std::runtime_error("slot " + std::to_string(at) + " is the set's own (it has " +
                                 std::to_string(motions_.size()) + ")");
    }
    motions_.resize(at);
    names_.resize(at);
    attributes_.resize(at);
    motions_.insert(motions_.end(), other.motions_.begin(), other.motions_.end());
    names_.insert(names_.end(), other.names_.begin(), other.names_.end());
    attributes_.insert(attributes_.end(), other.attributes_.begin(), other.attributes_.end());
}

const MotionAttributes* MotionSet::attributes(const std::size_t slot) const {
    return slot < attributes_.size() ? attributes_[slot].get() : nullptr;
}

const Motion* MotionSet::motion(const std::size_t slot) const {
    return slot < motions_.size() ? motions_[slot].get() : nullptr;
}

std::string MotionSet::name(const std::size_t slot) const {
    return slot < names_.size() ? names_[slot] : "";
}

// --- The player ---

MotionPlayer::MotionPlayer(MotionSkeleton skeleton) : skeleton_(std::move(skeleton)) {}

void MotionPlayer::constants(const Motion& motion) {
    // MotionPrototype0::setConstantValue (0xfa658), on a change of motion: the
    // model's rest, then the motion's constants; the helpers from its table.
    const Reader m(motion.bytes_.data(), motion.bytes_.size());
    const auto n = static_cast<std::size_t>(motion.joints());
    const std::size_t model = std::min(skeleton_.joints.size(), static_cast<std::size_t>(motion.model_joints()));
    if (scale_.size() < n) {
        scale_.resize(n);
        rotation_.resize(n);
        translation_.resize(n);
        gscale_.resize(n);
        lmatrix_.resize(n, kUnit);
        ugm_.resize(n, kUnit);
        all_gmatrix_.resize(n, kUnit);
    }
    for (std::size_t j = 0; j < model; ++j) {
        scale_[j] = skeleton_.joints[j].scale;
        rotation_[j] = skeleton_.joints[j].rotation;
        translation_[j] = skeleton_.joints[j].translation;
    }
    for (std::uint32_t i = 0; i < m.u32(kConstantNum); ++i) {
        const std::size_t c = m.u32(kConstants) + i * 8U;
        const std::size_t id = m.u16(c);
        const unsigned track = m.u16(c + 2);
        if (id >= model || track > 8) {
            continue;
        }
        (track < 3 ? scale_ : track < 6 ? rotation_ : translation_)[id][track % 3] = m.f32(c + 4);
    }
    // bones the motion's model has and this skeleton lacks: neutral, as they have no rest here
    const auto own = static_cast<std::size_t>(motion.model_joints());
    for (std::size_t j = model; j < own; ++j) {
        const MotionSkeleton::Joint neutral;
        scale_[j] = neutral.scale;
        rotation_[j] = neutral.rotation;
        translation_[j] = neutral.translation;
    }
    for (std::size_t j = own; j < n; ++j) {
        const std::size_t h = m.u32(kHrc) + (j - own) * 64U;
        scale_[j] = m.vec(h + 16);
        rotation_[j] = m.vec(h + 32);
        translation_[j] = m.vec(h + 48);
    }
    active_.assign(m.u32(kConstraintNum), 0);
    for (std::size_t i = 0; i < active_.size(); ++i) {
        active_[i] = m.u8(m.u32(kConstraints) + i * 12U + 1);
    }
}

void MotionPlayer::curves(const Motion& motion, const float t) {
    // MotionPrototype0::setFcurveValue (0xfa8ec): direct curves move the model's
    // joints, indirect ones the helpers; tracks 0-2 scale, 3-5 rotation, 6-8 translation.
    const Reader m(motion.bytes_.data(), motion.bytes_.size());
    int before = 0, after = 0;
    time_index(m, t, before, after);
    const auto model = static_cast<std::size_t>(motion.model_joints());
    for (int pass = 0; pass < 2; ++pass) {
        const std::size_t table = m.u32(pass == 0 ? kDirect : kIndirect);
        const std::uint32_t count = m.u32(pass == 0 ? kDirectNum : kIndirectNum);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t f = table + i * 6U;
            const std::size_t id = m.u16(f) + (pass == 0 ? 0U : model);
            const unsigned track = m.u8(f + 2) & 0xFU;
            if (id >= scale_.size() || (pass == 0 && id >= model) || track > 8) {
                continue;  // asserted against
            }
            (track < 3 ? scale_ : track < 6 ? rotation_ : translation_)[id][track % 3] = fcurve(m, f, before, after, t);
        }
    }
}

void MotionPlayer::activation(const Motion& motion, const float t) {
    // MotionPrototype0::setConstraintActivation (0x101cd4): each constraint on or
    // off as its activation table says at `t`; before its first entry, the
    // opposite of that entry's.
    const Reader m(motion.bytes_.data(), motion.bytes_.size());
    const std::size_t table = m.u32(kActive);
    for (std::size_t i = 0; i < active_.size(); ++i) {
        const std::size_t c = m.u32(kConstraints) + i * 12U;
        const int num = m.u16(c + 8), base = m.u16(c + 10);
        if (num == 0) {
            continue;
        }
        const auto time = [&](int k) { return m.f32(table + static_cast<std::size_t>(k) * 8U); };
        const auto on = [&](int k) { return m.u32(table + static_cast<std::size_t>(k) * 8U + 4) != 0; };
        int lo = base, hi = base + num - 1;
        if (t < time(lo)) {
            active_[i] = on(lo) ? 0 : 1;
            continue;
        }
        if (t >= time(hi)) {
            active_[i] = on(hi) ? 1 : 0;
            continue;
        }
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            if (t >= time(mid)) {
                lo = mid;
                if (t <= time(mid)) {
                    break;
                }
            } else {
                hi = mid;
            }
        }
        active_[i] = on(lo) ? 1 : 0;
    }
}

float MotionPlayer::expression(const Motion& motion, const int node, const int depth) {
    // MotionPrototype0::evalExpression (0xfb048): a tree of 12-byte nodes (u32:
    // type in its low byte, global in bit 8, a joint in the high half; f32
    // value; u16 car, cdr). Angles in degrees.
    const Reader m(motion.bytes_.data(), motion.bytes_.size());
    const auto count = static_cast<int>(m.u32(kNodeNum));
    if (node < 0 || node >= count || depth >= kExpressionDepth || ++expression_visits_ > kExpressionVisits) {
        return 0.0F;  // the two limits: a node that reaches itself, or nodes shared to no end
    }
    const std::size_t nodes = m.u32(kNodes);
    const auto at = [&](int n) { return nodes + static_cast<std::size_t>(n) * 12U; };
    const std::size_t p = at(node);
    const std::uint32_t word = m.u32(p);
    const int type = static_cast<std::int8_t>(word & 0xFFU);
    const bool global = (word & 0x100U) != 0;
    const std::size_t joint = word >> 16U;
    const int car = m.u16(p + 8), cdr = m.u16(p + 10);
    const auto a = [&] { return expression(motion, car, depth + 1); };
    const auto b = [&] { return expression(motion, cdr, depth + 1); };
    constexpr float kDeg = 57.295776F, kRad = 0.017453292F;
    const auto in_joints = [&](std::size_t j) { return j < scale_.size(); };
    switch (type) {
    case 0: return std::sin(a() * kRad);
    case 1: return std::cos(a() * kRad);
    case 2: return std::tan(a() * kRad);
    case 3: return std::asin(std::clamp(a(), -1.0F, 1.0F)) * kDeg;
    case 4: {
        const float v = a();
        return (v > 1.0F ? 0.0F : v < -1.0F ? kPi : std::acos(v)) * kDeg;
    }
    case 5: return std::atan(a()) * kDeg;
    case 6: return std::log(a());
    case 7: return std::exp(a());
    case 8: return std::fabs(a());
    case 9: {
        const float x = a();
        return std::pow(x, b());
    }
    case 10: return std::sqrt(std::fabs(a()));
    case 11: {
        const float x = a(), y = b();
        return x - y >= 0.0F ? y : x;
    }
    case 12: {
        const float x = a(), y = b();
        return y - x >= 0.0F ? y : x;
    }
    case 13: {  // the mean of a list
        if (car >= count) {
            return 0.0F;
        }
        float sum = 0.0F;
        int n = 0;
        for (int l = car; l < count && m.u16(at(l) + 8) < count && n < count; l = m.u16(at(l) + 10)) {
            sum += expression(motion, m.u16(at(l) + 8), depth + 1);
            ++n;
        }
        return n != 0 ? sum / static_cast<float>(n) : 0.0F;
    }
    case 14: {  // (if c then else) as a list
        if (car >= count) {
            return 0.0F;
        }
        const int l1 = car, l2 = m.u16(at(l1) + 10);
        if (l2 >= count) {
            return 0.0F;
        }
        if (expression(motion, m.u16(at(l1) + 8), depth + 1) == 0.0F) {
            const int l3 = m.u16(at(l2) + 10);
            return l3 < count ? expression(motion, m.u16(at(l3) + 8), depth + 1) : 0.0F;
        }
        return expression(motion, m.u16(at(l2) + 8), depth + 1);
    }
    case 15:
    case 43: {  // an F-curve at a time in the motion's frames (43: in degrees)
        const float v = fcurve_at(m, cdr, a() * 60.0F / motion.fps());
        return type == 43 ? v * kDeg : v;
    }
    case 16: {  // the distance between two joints
        const auto j0 = static_cast<std::size_t>(car), j1 = static_cast<std::size_t>(cdr);
        if (!in_joints(j0) || !in_joints(j1)) {
            return 0.0F;
        }
        const Vec4 p0 = column(all_gmatrix_[j0], 3), p1 = column(all_gmatrix_[j1], 3);
        const float dx = p0[0] - p1[0], dy = p0[1] - p1[1], dz = p0[2] - p1[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    case 17: {
        const float x = a(), y = std::fabs(b());
        return y * (x / y - std::trunc(x / y));
    }
    case 18: {
        const float x = a();
        return x + b();
    }
    case 19: {
        const float x = a();
        return x - b();
    }
    case 20: {
        const float x = a();
        return x * b();
    }
    case 21: {
        const float x = a();
        return x / b();
    }
    case 22: {
        const float fx = a(), fy = b();
        if (!(std::fabs(fx) < 2147483648.0F) || !(std::fabs(fy) < 2147483648.0F)) {
            return 0.0F;  // no int holds it: the cast is undefined, and INT_MIN % -1 traps
        }
        const int x = static_cast<int>(fx), y = static_cast<int>(fy);
        return y != 0 ? static_cast<float>(x % y) : 0.0F;
    }
    case 23: {
        const float x = a(), y = b();
        return x == y ? 1.0F : 0.0F;
    }
    case 24: {
        const float x = a(), y = b();
        return y - x >= 0.0F ? 0.0F : 1.0F;
    }
    case 25: {
        const float x = a(), y = b();
        return x - y >= 0.0F ? 1.0F : 0.0F;
    }
    case 26: {
        const float x = a(), y = b();
        return x - y >= 0.0F ? 0.0F : 1.0F;
    }
    case 27: {
        const float x = a(), y = b();
        return y - x >= 0.0F ? 1.0F : 0.0F;
    }
    case 28: return a() == 0.0F || b() == 0.0F ? 0.0F : 1.0F;
    case 29: return a() == 0.0F && b() == 0.0F ? 0.0F : 1.0F;
    case 30: return frame_;
    case 31: return m.f32(p + 4);
    case 32:
    case 33:
    case 34:
        if (!in_joints(joint)) {
            return 0.0F;
        }
        return (global ? all_gmatrix_ : lmatrix_)[joint][12 + static_cast<std::size_t>(type - 32)];
    case 35:
    case 36:
    case 37: {
        if (!in_joints(joint)) {
            return 0.0F;
        }
        Vec4 r = rotation_[joint];
        if (global) {
            to_rot(ugm_[joint], r);
        }
        return r[static_cast<std::size_t>(type - 35)] * kDeg;
    }
    case 38:
    case 39:
    case 40:
        if (!in_joints(joint)) {
            return 0.0F;
        }
        return (global ? gscale_ : scale_)[joint][static_cast<std::size_t>(type - 38)];
    default: return 0.0F;  // 41 and 42: list cells
    }
}

void MotionPlayer::evaluate(const Motion& motion, const float ticks, const Mat4& world, const GroundRay* ground) {
    // MotionPrototype0::calc (0x101e28)
    if (current_ != &motion) {
        constants(motion);
        current_ = &motion;
    }
    expression_visits_ = 0;
    const float t = ticks + motion.start() * 60.0F / motion.fps();
    frame_ = t * motion.fps() * 0.016666668F + motion.start();
    curves(motion, t);
    activation(motion, t);
    matrices(motion, world, ground);
    const auto model = std::min(all_gmatrix_.size(), skeleton_.joints.size());
    gmatrix_.assign(all_gmatrix_.begin(), all_gmatrix_.begin() + static_cast<std::ptrdiff_t>(model));
}

void MotionPlayer::matrices(const Motion& motion, const Mat4& world, const GroundRay* ground) {
    // MotionPrototype0::calcMatrix (0xfc500): each joint in the flag table's
    // order: its expressions, its constraints, an IK chain or its own turn.
    const Reader m(motion.bytes_.data(), motion.bytes_.size());
    const int model = motion.model_joints();
    const auto n = static_cast<std::size_t>(motion.joints());
    const std::size_t hrc = m.u32(kHrc);
    const auto parent_of = [&](std::size_t id) -> int {
        if (id < static_cast<std::size_t>(model)) {
            return id < skeleton_.joints.size() ? skeleton_.joints[id].parent : -1;
        }
        return m.i16(hrc + (id - static_cast<std::size_t>(model)) * 64U + 4);
    };
    const auto length_of = [&](std::size_t id) -> float {  // the rest hierarchy's scale w
        if (id < static_cast<std::size_t>(model)) {
            return id < skeleton_.joints.size() ? skeleton_.joints[id].scale[3] : 0.0F;
        }
        return m.f32(hrc + (id - static_cast<std::size_t>(model)) * 64U + 28);
    };
    const auto ok = [&](int id) { return id >= 0 && static_cast<std::size_t>(id) < n; };
    const auto pos = [&](int id) {
        return ok(id) ? column(all_gmatrix_[static_cast<std::size_t>(id)], 3) : Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    };
    // getCnsPos (0xfada0, 0xfaf8c): a position constraint's joint, or the middle of type 5's two
    const auto cns_pos = [&](std::size_t x) {
        const Vec4 a = pos(m.u16(x + 4));
        if (m.u8(x) != 5) {
            return a;
        }
        const Vec4 b = pos(m.u16(x + 6));
        return Vec4{(a[0] + b[0]) * 0.5F, (a[1] + b[1]) * 0.5F, (a[2] + b[2]) * 0.5F, (a[3] + b[3]) * 0.5F};
    };

    const std::size_t constraints = m.u32(kConstraints);
    const std::uint32_t constraint_num = m.u32(kConstraintNum);
    const auto cns = [&](std::uint32_t i) { return constraints + i * 12U; };
    const std::size_t expressions = m.u32(kExpressions);
    const std::uint32_t expression_num = m.u32(kExpressionNum);
    const std::size_t limiters = m.u32(kLimiters);
    std::size_t ext = m.u32(kExtEffector);
    std::uint32_t e = 0, c = 0;

    for (std::size_t k = 0; k < n; ++k) {
        std::uint32_t flag = m.u32(m.u32(kFlags) + k * 4U);
        const std::size_t id = flag & 0xFFFFU;
        if (id >= n) {
            throw std::runtime_error("KH2 motion: joint " + std::to_string(id) + " of " + std::to_string(n) +
                                     "; the file is damaged: extract it again from your game");
        }
        const int parent = parent_of(id);
        const Mat4& pg = ok(parent) ? all_gmatrix_[static_cast<std::size_t>(parent)] : kUnit;
        const Mat4& pu = ok(parent) ? ugm_[static_cast<std::size_t>(parent)] : kUnit;

        // its expressions (sorted by target)
        for (; e < expression_num && m.u16(expressions + e * 8U) == id; ++e) {
            const std::size_t x = expressions + e * 8U;
            const float v = expression(motion, m.u16(x + 6), 0);
            const unsigned track = m.u16(x + 2);
            if (track < 3) {
                scale_[id][track] = v;
            } else if (track < 6) {
                rotation_[id][track - 3] = v * 0.017453292F;
            } else if (track < 9) {
                translation_[id][track - 6] = v;
            }
            flag &= ~(1U << 21U);
        }

        // its constraints (sorted by passive joint), the active ones
        int pos1 = -1, pos2 = -1, orient = -1, scale_cns = -1, pos_limit = -1, pos_limit_base = -1, rot_limit = -1;
        for (; c < constraint_num && m.u16(cns(c) + 2) == id; ++c) {
            if (active_[c] == 0) {
                continue;
            }
            const std::size_t x = cns(c);
            const int active0 = m.u16(x + 4), active1 = m.u16(x + 6);
            switch (m.u8(x)) {
            case 13: rot_limit = active1; break;
            case 12:
                pos_limit = active1;
                pos_limit_base = active0;
                break;
            case 6: scale_cns = static_cast<int>(c); break;
            case 5:
                pos1 = active0;
                orient = static_cast<int>(c);
                pos2 = active1;
                break;
            case 2:
            case 3:
            case 4: orient = static_cast<int>(c); break;
            case 0:
                if (pos1 == -1) {
                    pos1 = active0;
                } else if (pos2 == -1) {
                    pos2 = active0;
                }
                break;
            default: break;
            }
        }
        const auto type_of = [&](int i) { return i >= 0 ? m.u8(cns(static_cast<std::uint32_t>(i))) : 0xFFU; };
        const auto active_id = [&](int i, int which) {
            return static_cast<int>(
                m.u16(cns(static_cast<std::uint32_t>(i)) + 4 + static_cast<std::size_t>(which) * 2));
        };

        // an IK chain: its effector, a position constraint on the joint after it
        unsigned ik = (flag >> 16U) & 3U;
        int effector = -1;
        if (ik != 0) {
            int target = ik == 1 ? static_cast<int>(id) + 1 : static_cast<int>(id) + 2;
            if (((flag >> 23U) & 1U) != 0) {
                target = m.i16(ext);
                ext += 2;
            }
            for (; c < constraint_num; ++c) {
                const unsigned type = m.u8(cns(c));
                if (active_[c] != 0 && m.u16(cns(c) + 2) == target && (type <= 1 || type == 5)) {
                    effector = static_cast<int>(c++);
                    break;
                }
            }
            if (effector < 0) {
                flag &= ~(3U << 16U);
                ik = 0;
            }
        }

        // the global scale
        Vec4& gs = gscale_[id];
        if (scale_cns >= 0 && ok(active_id(scale_cns, 0))) {
            gs = gscale_[static_cast<std::size_t>(active_id(scale_cns, 0))];
            if (ok(parent)) {
                for (std::size_t i = 0; i < 3; ++i) {
                    scale_[id][i] = gs[i] / gscale_[static_cast<std::size_t>(parent)][i];
                }
            } else {
                scale_[id] = gs;
            }
        } else if (ok(parent)) {
            for (std::size_t i = 0; i < 4; ++i) {
                gs[i] = gscale_[static_cast<std::size_t>(parent)][i] * scale_[id][i];
            }
        } else {
            gs = scale_[id];
        }

        const bool to_rotation = ((flag >> 22U) & 1U) != 0;
        if (((flag >> 21U) & 1U) != 0) {  // calculated: placed by an IK chain before it
            if (to_rotation) {
                to_rot(lmatrix_[id], rotation_[id]);
            }
            continue;
        }

        if (ik >= 2) {
            // two bones bent to reach the effector
            if (orient >= 0 && type_of(orient) != 4) {
                orient = -1;
            }
            const std::size_t child = id + 1;
            if (child >= n) {
                continue;
            }
            Vec4 l = translation_[id];
            l[3] = 1.0F;
            const Vec4 g = mul(pg, l);
            const Mat4 inv = inverse(pu);
            const Vec4 lp = mul(inv, g);
            for (; c < constraint_num && m.u16(cns(c) + 2) == child; ++c) {
            }
            for (std::size_t i = 0; i < 4; ++i) {
                gscale_[child][i] = gs[i] * scale_[child][i];
            }
            const std::size_t ex = cns(static_cast<std::uint32_t>(effector));
            Vec4 tg = cns_pos(ex);
            Vec4 tl = mul(inv, tg);
            if (ground != nullptr && *ground) {  // IKAdjust: the foot on the ground under it
                for (const auto& foot : skeleton_.feet) {
                    if (foot.bone < 0 || static_cast<std::size_t>(foot.bone) >= skeleton_.joints.size() ||
                        parent_of(static_cast<std::size_t>(foot.bone)) != static_cast<int>(id)) {
                        continue;
                    }
                    // the bias: the element's height (make_ik_adjust always sets
                    // enableBias, so the model's getIKBias is never used)
                    const float bias = foot.bias;
                    const float s = std::sqrt(world[0] * world[0] + world[1] * world[1] + world[2] * world[2]);
                    const float reach = gscale_[child][0] * length_of(child);
                    const Vec4 p = apply1(world, tg);
                    std::array<float, 3> to{p[0], p[1] - s * bias, p[2]};
                    const std::array<float, 3> from{p[0], to[1] + s * (reach + bias), p[2]};
                    if (foot.below) {
                        to[1] -= s * (reach + bias);
                    }
                    std::array<float, 3> hit{};
                    if ((*ground)(from, to, hit)) {
                        tg = apply1(inverse(world), {hit[0], hit[1] + s * bias, hit[2], 1.0F});
                        tl = mul(inv, tg);
                    }
                    break;
                }
            }
            Vec4 up{};
            const bool has_up = orient >= 0 && ok(active_id(orient, 0));
            if (has_up) {
                up = apply1(inv, pos(active_id(orient, 0)));
            }
            ik_joint(lp, tl, gs[0] * length_of(id), gscale_[child][0] * length_of(child), ik == 3, lmatrix_[id],
                     &lmatrix_[child], rotation_[id][0], rotation_[child][0], has_up ? &up : nullptr);
            if (to_rotation) {
                to_rot(lmatrix_[id], rotation_[id]);
            }
            lmatrix_[id][12] = l[0];  // as the PS3 build has it: only x
            ugm_[id] = mul(pu, lmatrix_[id]);
            all_gmatrix_[id] = ugm_[id];
            scaling(all_gmatrix_[id], gs);
            set_column(all_gmatrix_[id], 3, g);
            lmatrix_[child][12] = length_of(id);
            ugm_[child] = mul(ugm_[id], lmatrix_[child]);
            all_gmatrix_[child] = ugm_[child];
            scaling(all_gmatrix_[child], gscale_[child]);
            set_column(all_gmatrix_[child], 3, apply1(all_gmatrix_[id], column(lmatrix_[child], 3)));
            const std::size_t end = m.u16(ex + 2);
            if (end < n) {
                set_column(all_gmatrix_[end], 3, apply1(all_gmatrix_[child], translation_[end]));
            }
            continue;
        }

        if (ik == 1) {
            // one bone aimed at the effector
            Vec4 l = translation_[id];
            l[3] = 1.0F;
            const Vec4 g = mul(pg, l);
            const Mat4 inv = inverse(pu);
            const Vec4 lp = mul(inv, g);
            const std::size_t ex = cns(static_cast<std::uint32_t>(effector));
            const Vec4 tl = mul(inv, cns_pos(ex));
            Vec4 up{};
            const bool has_up = orient >= 0 && type_of(orient) == 4 && ok(active_id(orient, 0));
            if (has_up) {
                up = apply1(inv, pos(active_id(orient, 0)));
            }
            ik_joint(lp, tl, gs[0] * length_of(id), 0.0F, false, lmatrix_[id], nullptr, rotation_[id][0], 0.0F,
                     has_up ? &up : nullptr);
            if (to_rotation) {
                to_rot(lmatrix_[id], rotation_[id]);
            }
            lmatrix_[id][12] = l[0];  // as the PS3 build has it: only x
            ugm_[id] = mul(pu, lmatrix_[id]);
            all_gmatrix_[id] = ugm_[id];
            scaling(all_gmatrix_[id], gs);
            set_column(all_gmatrix_[id], 3, g);
            const std::size_t end = m.u16(ex + 2);
            if (end < n) {
                set_column(all_gmatrix_[end], 3, apply1(all_gmatrix_[id], translation_[end]));
            }
            continue;
        }

        // its own place and turn, or its constraints'
        const bool trans_flag = ((flag >> 18U) & 1U) != 0, rot_flag = ((flag >> 19U) & 1U) != 0;
        const bool constrained = pos1 != -1 || pos2 != -1;
        Mat4 inv = kUnit;
        if (constrained || orient >= 0 || trans_flag || rot_flag || pos_limit >= 0 || rot_limit >= 0) {
            inv = inverse(pg);
        }
        Vec4 l{}, g{};
        const auto limited = [&](Vec4& v) {
            if (pos_limit >= 0 && ok(pos_limit_base)) {
                limit(m, limiters + static_cast<std::size_t>(pos_limit) * 48U, mul(inv, pos(pos_limit_base)), v);
                return true;
            }
            return false;
        };
        if (trans_flag) {
            g = pos(static_cast<int>(id));
            g[3] = 1.0F;
            l = mul(inv, g);
            l[3] = 1.0F;
        } else if (!constrained) {
            l = translation_[id];
            l[3] = 1.0F;
            limited(l);
            g = mul(pg, l);
            g[3] = 1.0F;
        } else {
            g = pos(pos1);
            if (pos2 >= 0) {
                const Vec4 b = pos(pos2);
                g = {(g[0] + b[0]) * 0.5F, (g[1] + b[1]) * 0.5F, (g[2] + b[2]) * 0.5F, (g[3] + b[3]) * 0.5F};
            }
            l = mul(inv, g);
            l[3] = 1.0F;
            if (limited(l)) {
                g = mul(pg, l);
            }
            g[3] = 1.0F;
        }

        if (orient >= 0) {
            const unsigned type = type_of(orient);
            if (type == 2) {  // its turn copied
                if (ok(active_id(orient, 0))) {
                    ugm_[id] = ugm_[static_cast<std::size_t>(active_id(orient, 0))];
                }
            } else if (type == 3 || type == 5) {  // aimed at a joint, X along, kept upright
                const int target = active_id(orient, type == 3 ? 0 : 1);
                if (ok(target)) {
                    const Vec4 tl = mul(inv, pos(target));
                    const Vec4 dir{tl[0] - l[0], tl[1] - l[1], tl[2] - l[2], tl[3] - l[3]};
                    align_xy(dir, {0.0F, 1.0F, 0.0F, 0.0F}, lmatrix_[id]);
                    lmatrix_[id] = mul(lmatrix_[id], rotate(kUnit, 0, rotation_[id][0]));
                    set_column(lmatrix_[id], 3, l);
                    ugm_[id] = mul(pu, lmatrix_[id]);
                }
            }
            // type 4 (an up vector) only steers IK
        } else {
            if (rot_limit >= 0) {
                const Vec4 r = rotation_[id];
                limit(m, limiters + static_cast<std::size_t>(rot_limit) * 48U, r, rotation_[id]);
            }
            lmatrix_[id] = rotate_xyz(kUnit, rotation_[id]);
            set_column(lmatrix_[id], 3, l);
            ugm_[id] = !ok(parent) || rot_flag ? lmatrix_[id] : mul(pu, lmatrix_[id]);
        }
        all_gmatrix_[id] = ugm_[id];
        scaling(all_gmatrix_[id], gs);
        set_column(all_gmatrix_[id], 3, g);
        if (orient >= 0 || rot_flag) {
            lmatrix_[id] = mul(inverse(pu), ugm_[id]);
            set_column(lmatrix_[id], 3, l);
            if (to_rotation) {
                to_rot(lmatrix_[id], rotation_[id]);
            }
        }
    }
}

// --- One model's pose on another's bones ---

namespace {

// A skeleton's rest gmatrix (rotate_xyz with its translation, under its parent: the solver's lmatrix
// with no motion) and each bone's local matrix.
void rest_of(const MotionSkeleton& s, std::vector<Mat4>& global, std::vector<Mat4>& local) {
    const std::size_t n = s.joints.size();
    global.assign(n, kUnit);
    local.assign(n, kUnit);
    std::vector<char> done(n, 0);
    for (std::size_t pass = 0; pass <= n; ++pass) {
        for (std::size_t j = 0; j < n; ++j) {
            const int p = s.joints[j].parent;
            if (done[j] || (p >= 0 && (static_cast<std::size_t>(p) >= n || !done[static_cast<std::size_t>(p)]))) {
                continue;
            }
            Mat4 l = rotate_xyz(kUnit, s.joints[j].rotation);
            Vec4 t = s.joints[j].translation;
            t[3] = 1.0F;
            set_column(l, 3, t);
            local[j] = l;
            global[j] = p >= 0 ? mul(global[static_cast<std::size_t>(p)], l) : l;
            done[j] = 1;
        }
    }
}

// m with its axes made unit (a bone's scale taken out of its turn)
Mat4 unscaled(Mat4 m) {
    for (std::size_t c = 0; c < 3; ++c) {
        Vec4 v = column(m, c);
        v[3] = 0.0F;
        if (normalize(v) > 0.0F) {
            set_column(m, c, v);
        }
    }
    return m;
}

Mat4 turn_of(Mat4 m) {
    set_column(m, 3, {0.0F, 0.0F, 0.0F, 1.0F});
    return m;
}

// the shortest turn taking direction `from` to direction `to`
Mat4 arc(Vec4 from, Vec4 to) {
    from[3] = to[3] = 0.0F;
    if (normalize(from) <= 0.0F || normalize(to) <= 0.0F) {
        return kUnit;
    }
    const float c = std::fmax(-1.0F, std::fmin(1.0F, from[0] * to[0] + from[1] * to[1] + from[2] * to[2]));
    Vec4 axis = as_vec(cross(from, to));
    const float s = normalize(axis);
    if (s < 1e-6F) {
        return kUnit;  // the same way, or exactly opposite: left alone
    }
    const float t = 1.0F - c, x = axis[0], y = axis[1], z = axis[2];
    return {t * x * x + c,
            t * x * y + s * z,
            t * x * z - s * y,
            0.0F,
            t * x * y - s * z,
            t * y * y + c,
            t * y * z + s * x,
            0.0F,
            t * x * z + s * y,
            t * y * z - s * x,
            t * z * z + c,
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            1.0F};
}

Vec4 minus(const Vec4& a, const Vec4& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2], 0.0F};
}

Vec4 along(const Vec4& a, const Vec4& d, float s) {
    return {a[0] + d[0] * s, a[1] + d[1] * s, a[2] + d[2] * s, 1.0F};
}

float dot3(const Vec4& a, const Vec4& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

}  // namespace

Retarget::Retarget(const MotionSkeleton& source, const MotionSkeleton& target, std::vector<int> source_of,
                   std::vector<Limb> limbs)
    : source_of_(std::move(source_of)), limbs_(std::move(limbs)) {
    const std::size_t n = target.joints.size();
    if (source_of_.size() != n) {
        throw std::runtime_error("KH2 retarget: a source bone for each of the target's " + std::to_string(n) +
                                 " bones is needed");
    }
    for (std::size_t t = 0; t < n; ++t) {
        if (source_of_[t] >= static_cast<int>(source.joints.size())) {
            throw std::runtime_error("KH2 retarget: target bone " + std::to_string(t) + " follows source bone " +
                                     std::to_string(source_of_[t]) + ", which the source does not have (it has " +
                                     std::to_string(source.joints.size()) + ")");
        }
    }
    std::vector<Mat4> source_rest, source_local;
    rest_of(source, source_rest, source_local);
    source_rest_inverse_.reserve(source_rest.size());
    for (const Mat4& r : source_rest) {
        source_rest_inverse_.push_back(inverse(r));
        source_rest_head_.push_back(column(r, 3));
    }
    for (const auto& j : source.joints) {
        source_parent_.push_back(j.parent >= 0 && static_cast<std::size_t>(j.parent) < source.joints.size() ? j.parent
                                                                                                            : -1);
    }
    rest_of(target, target_rest_, target_local_);
    target_rest_inverse_.reserve(target_rest_.size());
    for (const Mat4& r : target_rest_) {
        target_rest_inverse_.push_back(inverse(r));
    }
    parent_.resize(n);
    for (std::size_t t = 0; t < n; ++t) {
        parent_[t] = target.joints[t].parent >= 0 && static_cast<std::size_t>(target.joints[t].parent) < n
                         ? target.joints[t].parent
                         : -1;
    }
    carrier_.assign(n, -1);
    carried_local_.assign(n, kUnit);
    order_bones();
    const auto below = [&](int b, int a) {
        for (int up = parent_[static_cast<std::size_t>(b)]; up >= 0; up = parent_[static_cast<std::size_t>(up)]) {
            if (up == a) {
                return true;
            }
        }
        return false;
    };
    for (const Limb& l : limbs_) {
        const bool in = l.upper >= 0 && l.lower >= 0 && l.end >= 0 && static_cast<std::size_t>(l.upper) < n &&
                        static_cast<std::size_t>(l.lower) < n && static_cast<std::size_t>(l.end) < n;
        if (!in || !below(l.lower, l.upper) || !below(l.end, l.lower) ||
            source_of_[static_cast<std::size_t>(l.upper)] < 0 || source_of_[static_cast<std::size_t>(l.lower)] < 0 ||
            source_of_[static_cast<std::size_t>(l.end)] < 0) {
            throw std::runtime_error("KH2 retarget: limb " + std::to_string(l.upper) + " " + std::to_string(l.lower) +
                                     " " + std::to_string(l.end) +
                                     ": three bones that follow source bones, each below the one before, are needed");
        }
        if (l.lengths && source_parent_[static_cast<std::size_t>(source_of_[static_cast<std::size_t>(l.lower)])] < 0) {
            throw std::runtime_error("KH2 retarget: limb " + std::to_string(l.upper) + " " + std::to_string(l.lower) +
                                     " " + std::to_string(l.end) +
                                     " with `lengths`: its lower bone's source bone has no parent to measure from");
        }
        if (l.reach >= 0 &&
            (static_cast<std::size_t>(l.reach) >= n || source_of_[static_cast<std::size_t>(l.reach)] < 0)) {
            throw std::runtime_error("KH2 retarget: limb " + std::to_string(l.upper) + " " + std::to_string(l.lower) +
                                     " " + std::to_string(l.end) + " reaching for bone " + std::to_string(l.reach) +
                                     ": that bone follows no source bone");
        }
    }
    // the `lengths` limbs' upper and lower bones: their way to their child joint, in their rest space
    stretch_axis_.assign(n, {0.0F, 0.0F, 0.0F, 0.0F});
    for (const Limb& l : limbs_) {
        if (!l.lengths) {
            continue;
        }
        const auto way = [&](int bone, int child) {
            Vec4 v = apply1(target_rest_inverse_[static_cast<std::size_t>(bone)],
                            column(target_rest_[static_cast<std::size_t>(child)], 3));
            v[3] = 0.0F;
            normalize(v);
            return v;
        };
        stretch_axis_[static_cast<std::size_t>(l.upper)] = way(l.upper, l.lower);
        stretch_axis_[static_cast<std::size_t>(l.lower)] = way(l.lower, l.end);
    }
}

void Retarget::order_bones() {
    const std::size_t n = parent_.size();
    order_.clear();
    std::vector<char> done(n, 0);
    const auto ready = [&](int b) { return b < 0 || done[static_cast<std::size_t>(b)] != 0; };
    for (std::size_t pass = 0; pass <= n && order_.size() < n; ++pass) {
        for (std::size_t t = 0; t < n; ++t) {
            if (!done[t] && ready(parent_[t]) && ready(carrier_[t])) {
                order_.push_back(t);
                done[t] = 1;
            }
        }
    }
}

void Retarget::carry(const int bone, const int by, const Mat4& local) {
    const int n = static_cast<int>(parent_.size());
    bool below = false;  // `by` at or under `bone`: it would carry itself
    for (int up = by; up >= 0 && up < n && !below; up = parent_[static_cast<std::size_t>(up)]) {
        below = up == bone;
    }
    if (bone < 0 || bone >= n || by < 0 || by >= n || below) {
        throw std::runtime_error("KH2 retarget: bone " + std::to_string(bone) + " carried by bone " +
                                 std::to_string(by) + ": two of the target's " + std::to_string(n) +
                                 " bones are needed, the carrier not the bone or below it");
    }
    carrier_[static_cast<std::size_t>(bone)] = by;
    carried_local_[static_cast<std::size_t>(bone)] = local;
    order_bones();
    if (order_.size() < parent_.size()) {
        carrier_[static_cast<std::size_t>(bone)] = -1;
        order_bones();
        throw std::runtime_error("KH2 retarget: bone " + std::to_string(bone) + " carried by bone " +
                                 std::to_string(by) + " closes a loop of carried bones");
    }
}

// Target bone t turned from its rest as its source bone is (then by `swing`), its head where its parent
// carries it (the target's own lengths; a root's moved as its source moves) or where a limb's bend put
// it (`heads`, when `moved`); an unmapped bone at rest under its parent; a carried one on its carrier.
Mat4 Retarget::pose(const std::size_t t, const std::vector<Mat4>& source, const std::vector<Mat4>& out,
                    const std::vector<Mat4>& swing, const std::vector<Vec4>& heads, const std::vector<char>& moved,
                    const std::vector<float>& stretch) const {
    const int p = parent_[t];
    const int s = source_of_[t];
    if (carrier_[t] >= 0) {
        return mul(out[static_cast<std::size_t>(carrier_[t])], carried_local_[t]);
    }
    if (s < 0 || static_cast<std::size_t>(s) >= source.size()) {
        return p >= 0 ? mul(out[static_cast<std::size_t>(p)], target_local_[t]) : target_rest_[t];
    }
    const Mat4 d =
        mul(unscaled(source[static_cast<std::size_t>(s)]), source_rest_inverse_[static_cast<std::size_t>(s)]);
    Mat4 m = mul(swing[t], mul(turn_of(d), turn_of(target_rest_[t])));
    if (stretch[t] != 1.0F) {  // along its length way only (I + (k - 1) a a^T in its own space)
        const Vec4& a = stretch_axis_[t];
        Mat4 st = kUnit;
        for (std::size_t c = 0; c < 3; ++c) {
            for (std::size_t r = 0; r < 3; ++r) {
                st[c * 4 + r] += (stretch[t] - 1.0F) * a[r] * a[c];
            }
        }
        m = mul(m, st);
    }
    // a root where its source is plus the rests' difference, not turned by the source root's turn
    const Vec4 head =
        moved[t] ? heads[t]
        : p >= 0 ? apply1(out[static_cast<std::size_t>(p)], column(target_local_[t], 3))
                 : along(column(source[static_cast<std::size_t>(s)], 3),
                         minus(column(target_rest_[t], 3), source_rest_head_[static_cast<std::size_t>(s)]), 1.0F);
    set_column(m, 3, head);
    return m;
}

// Where target bone t's rest head is carried now by the source bone target bone `by` follows.
Vec4 Retarget::carried(const std::vector<Mat4>& source, const int t, const int by) const {
    const auto s = static_cast<std::size_t>(source_of_[static_cast<std::size_t>(by)]);
    const Mat4 d = mul(unscaled(source[s]), source_rest_inverse_[s]);
    return apply1(d, column(target_rest_[static_cast<std::size_t>(t)], 3));
}

void Retarget::apply(const std::vector<Mat4>& source, std::vector<Mat4>& out) const {
    const std::size_t n = target_rest_.size();
    out.assign(n, kUnit);
    std::vector<Mat4> swing(n, kUnit);
    std::vector<Vec4> heads(n, {0.0F, 0.0F, 0.0F, 1.0F});
    std::vector<char> moved(n, 0);
    std::vector<float> stretch(n, 1.0F);
    const auto pass = [&] {
        for (const std::size_t t : order_) {
            out[t] = pose(t, source, out, swing, heads, moved, stretch);
        }
    };
    pass();
    for (const Limb& l : limbs_) {
        const auto u = static_cast<std::size_t>(l.upper), m = static_cast<std::size_t>(l.lower),
                   e = static_cast<std::size_t>(l.end);
        const int by = l.reach >= 0 ? l.reach : l.end;  // whose source carries where the end reaches
        if (source_of_[e] < 0 || static_cast<std::size_t>(source_of_[e]) >= source.size() ||
            static_cast<std::size_t>(source_of_[m]) >= source.size() ||
            static_cast<std::size_t>(source_of_[u]) >= source.size() ||
            static_cast<std::size_t>(source_of_[static_cast<std::size_t>(by)]) >= source.size()) {
            continue;
        }
        // the limb's own lengths (its rest's), from where its upper bone rides now
        const Vec4 ru = column(target_rest_[u], 3), rm = column(target_rest_[m], 3), re = column(target_rest_[e], 3);
        float l0 = std::sqrt(dot3(minus(rm, ru), minus(rm, ru)));
        float l1 = std::sqrt(dot3(minus(re, rm), minus(re, rm)));
        const float rest0 = l0, rest1 = l1;
        // `lengths`: the source limb's joints (upper: its lower's parent), lengths and way
        Vec4 su{}, sm{}, se{};
        if (l.lengths) {
            const auto s_m = static_cast<std::size_t>(source_of_[m]);
            su = column(source[static_cast<std::size_t>(source_parent_[s_m])], 3);
            sm = column(source[s_m], 3);
            se = column(source[static_cast<std::size_t>(source_of_[static_cast<std::size_t>(by)])], 3);
            l0 = std::sqrt(dot3(minus(sm, su), minus(sm, su)));
            l1 = std::sqrt(dot3(minus(se, sm), minus(se, sm)));
            if (l0 > 0.0F && l1 > 0.0F && rest0 > 0.0F && rest1 > 0.0F) {
                stretch[u] = l0 / rest0;
                stretch[m] = l1 / rest1;
                pass();
            }
        }
        const Vec4 hip = column(out[u], 3);
        const Vec4 mid = column(out[m], 3);
        Vec4 dir = l.lengths ? minus(se, su) : minus(carried(source, l.end, by), hip);
        float dist = normalize(dir);
        if (l0 <= 0.0F || l1 <= 0.0F || dist <= 0.0F) {
            continue;
        }
        dist = std::fmax(std::fabs(l0 - l1) * 1.001F, std::fmin(dist, (l0 + l1) * 0.999F));
        // the bend's side: toward where the source's middle carries the target's (`lengths`: the source's
        // own middle), else as it is now
        const Vec4 pole = l.lengths ? minus(sm, su) : minus(carried(source, l.lower, l.lower), hip);
        Vec4 side = along(pole, dir, -dot3(pole, dir));
        side[3] = 0.0F;
        if (normalize(side) <= 1e-4F) {
            side = along(minus(mid, hip), dir, -dot3(minus(mid, hip), dir));
            side[3] = 0.0F;
            if (normalize(side) <= 0.0F) {
                continue;
            }
        }
        const float a = (l0 * l0 - l1 * l1 + dist * dist) / (2.0F * dist);
        const float h = std::sqrt(std::fmax(0.0F, l0 * l0 - a * a));
        const Vec4 knee = along(along(hip, dir, a), side, h);
        const Vec4 end = along(hip, dir, dist);
        // the upper bone turned so its way to the lower's rest head points at the knee, the lower bone
        // moved to the knee and turned so its way to the end's points at the end, the end moved there
        const Vec4 lower_way = apply1(mul(out[u], target_rest_inverse_[u]), rm);
        swing[u] = mul(arc(minus(lower_way, hip), minus(knee, hip)), swing[u]);
        heads[m] = knee;
        moved[m] = 1;
        pass();
        const Vec4 end_way = apply1(mul(out[m], target_rest_inverse_[m]), re);
        swing[m] = mul(arc(minus(end_way, knee), minus(end, knee)), swing[m]);
        heads[e] = end;
        moved[e] = 1;
        pass();
    }
}

namespace {

using Mat3 = std::array<float, 9>;  // column-major

// a matrix's turn: its columns made orthonormal (Gram-Schmidt from x), its scale left out
Mat3 turn3(const Mat4& m) {
    std::array<float, 3> x{m[0], m[1], m[2]}, y{m[4], m[5], m[6]};
    const auto unit = [](std::array<float, 3>& v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 1e-12F)
            for (float& c : v)
                c /= l;
    };
    unit(x);
    const float d = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
    for (int k = 0; k < 3; ++k)
        y[k] -= x[k] * d;
    unit(y);
    const std::array<float, 3> z{x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]};
    return {x[0], x[1], x[2], y[0], y[1], y[2], z[0], z[1], z[2]};
}

Mat3 mul3(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            for (int k = 0; k < 3; ++k)
                r[c * 3 + row] += a[k * 3 + row] * b[c * 3 + k];
    return r;
}

Mat3 transpose3(const Mat3& a) {
    return {a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]};
}

}  // namespace

float twist_about(const Mat4& rest_a, const Mat4& rest_b, const Mat4& now_a, const Mat4& now_b,
                  const std::array<float, 3>& axis) {
    // b's turn against a since rest, in the rest model space's axes
    const Mat3 m = mul3(mul3(turn3(rest_a), transpose3(turn3(now_a))), mul3(turn3(now_b), transpose3(turn3(rest_b))));
    // as a quaternion (its largest component first, for precision)
    const auto at = [&](int row, int col) { return m[col * 3 + row]; };
    float w = 0.0F, x = 0.0F, y = 0.0F, z = 0.0F;
    const float tr = at(0, 0) + at(1, 1) + at(2, 2);
    if (tr > 0.0F) {
        const float s = std::sqrt(tr + 1.0F) * 2.0F;
        w = 0.25F * s;
        x = (at(2, 1) - at(1, 2)) / s;
        y = (at(0, 2) - at(2, 0)) / s;
        z = (at(1, 0) - at(0, 1)) / s;
    } else if (at(0, 0) > at(1, 1) && at(0, 0) > at(2, 2)) {
        const float s = std::sqrt(1.0F + at(0, 0) - at(1, 1) - at(2, 2)) * 2.0F;
        w = (at(2, 1) - at(1, 2)) / s;
        x = 0.25F * s;
        y = (at(0, 1) + at(1, 0)) / s;
        z = (at(0, 2) + at(2, 0)) / s;
    } else if (at(1, 1) > at(2, 2)) {
        const float s = std::sqrt(1.0F + at(1, 1) - at(0, 0) - at(2, 2)) * 2.0F;
        w = (at(0, 2) - at(2, 0)) / s;
        x = (at(0, 1) + at(1, 0)) / s;
        y = 0.25F * s;
        z = (at(1, 2) + at(2, 1)) / s;
    } else {
        const float s = std::sqrt(1.0F + at(2, 2) - at(0, 0) - at(1, 1)) * 2.0F;
        w = (at(1, 0) - at(0, 1)) / s;
        x = (at(0, 2) + at(2, 0)) / s;
        y = (at(1, 2) + at(2, 1)) / s;
        z = 0.25F * s;
    }
    // the twist part of it (swing-twist about the axis)
    float a = 2.0F * std::atan2(x * axis[0] + y * axis[1] + z * axis[2], w);
    if (a > kPi)
        a -= 2.0F * kPi;
    else if (a < -kPi)
        a += 2.0F * kPi;
    // past 135 degrees eased back to none at half a turn, so it never jumps from one side of it to the other
    const float most = 0.75F * kPi;
    if (std::fabs(a) > most)
        a = std::copysign(most, a) * (kPi - std::fabs(a)) / (kPi - most);
    return a;
}

Mat4 twist_turn(const Mat4& rest_a, const Mat4& now_a, const std::array<float, 3>& axis, const float radians,
                const std::array<float, 3>& pivot) {
    // the axis as a carries it now
    const Mat3 carry = mul3(turn3(now_a), transpose3(turn3(rest_a)));
    const float u[3] = {carry[0] * axis[0] + carry[3] * axis[1] + carry[6] * axis[2],
                        carry[1] * axis[0] + carry[4] * axis[1] + carry[7] * axis[2],
                        carry[2] * axis[0] + carry[5] * axis[1] + carry[8] * axis[2]};
    // Rodrigues: c I + s [u]x + (1 - c) u u^T, about the pivot
    const float c = std::cos(radians), s = std::sin(radians), t = 1.0F - c;
    Mat4 r{t * u[0] * u[0] + c,
           t * u[0] * u[1] + s * u[2],
           t * u[0] * u[2] - s * u[1],
           0.0F,
           t * u[0] * u[1] - s * u[2],
           t * u[1] * u[1] + c,
           t * u[1] * u[2] + s * u[0],
           0.0F,
           t * u[0] * u[2] + s * u[1],
           t * u[1] * u[2] - s * u[0],
           t * u[2] * u[2] + c,
           0.0F,
           0.0F,
           0.0F,
           0.0F,
           1.0F};
    for (int row = 0; row < 3; ++row)
        r[12 + row] = pivot[row] - (r[row] * pivot[0] + r[4 + row] * pivot[1] + r[8 + row] * pivot[2]);
    return r;
}

}  // namespace khdays::assets::kh2
