#include "khdays/assets/collision.h"

#include <cstring>
#include <string>

namespace khdays::assets {

namespace {

std::uint16_t rd_u16(const std::uint8_t* p, const std::size_t o) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[o])
        | (static_cast<std::uint16_t>(p[o + 1U]) << 8U));
}

std::int16_t rd_s16(const std::uint8_t* p, const std::size_t o) {
    return static_cast<std::int16_t>(rd_u16(p, o));
}

std::uint32_t rd_u32(const std::uint8_t* p, const std::size_t o) {
    return static_cast<std::uint32_t>(p[o])
        | (static_cast<std::uint32_t>(p[o + 1U]) << 8U)
        | (static_cast<std::uint32_t>(p[o + 2U]) << 16U)
        | (static_cast<std::uint32_t>(p[o + 3U]) << 24U);
}

std::int32_t rd_s32(const std::uint8_t* p, const std::size_t o) {
    return static_cast<std::int32_t>(rd_u32(p, o));
}

// The ray test's tolerance, in 20.12.
constexpr std::int32_t kEpsilon = 0x80;

// A fraction of 1 along the cast segment; func_01ffd824 works in this scale
// because the DS divider yields (num << 32) / den and the code shifts by 5.
constexpr std::int32_t kFractionOne = 0x08000000;

// (a * x + b * z + 0x800) >> 12, the rounding the game uses throughout.
std::int32_t dot_fx(const std::int32_t a, const std::int16_t ax,
                    const std::int32_t b, const std::int16_t bz) {
    const std::int64_t sum = static_cast<std::int64_t>(a) * ax
        + static_cast<std::int64_t>(b) * bz + 0x800;
    return static_cast<std::int32_t>(sum >> 12);
}

}  // namespace

CollisionModel decode_collision_model(
    const std::uint8_t* data, const std::size_t size) {
    CollisionModel out;
    if (data == nullptr || size < 0xB0U) {
        return out;
    }
    const std::uint16_t flags = rd_u16(data, 0x74U);
    const std::uint16_t node_count = rd_u16(data, 0x7AU);
    const std::uint16_t face88_count = rd_u16(data, 0x7CU);
    const std::uint16_t face84_a = rd_u16(data, 0x7EU);
    const std::uint16_t face84_b = rd_u16(data, 0x80U);
    const std::uint16_t named_count = rd_u16(data, 0x82U);

    const std::size_t tree_root = rd_u32(data, 0x9CU);
    const std::size_t faces88 = rd_u32(data, 0xA0U);
    const std::size_t faces84_a = rd_u32(data, 0xA4U);
    const std::size_t named_records = rd_u32(data, 0xACU);

    // The header must describe itself consistently. Each run has to fit, and
    // the sections chain: this is what makes a mis-parse fail loudly instead of
    // producing plausible geometry.
    if (faces88 + static_cast<std::size_t>(face88_count) * 0x88U > size
        || named_records + static_cast<std::size_t>(named_count) * 0x14U > size
        || tree_root + static_cast<std::size_t>(node_count) * 0x20U > size
        || faces84_a + static_cast<std::size_t>(face84_a) * 0x84U > size) {
        return out;
    }

    out.valid = true;
    out.flags = flags;
    out.node_count = node_count;
    out.face84_count_a = face84_a;
    out.face84_count_b = face84_b;

    out.faces.reserve(face88_count);
    for (std::size_t i = 0; i < face88_count; ++i) {
        const std::uint8_t* f = data + faces88 + i * 0x88U;
        CollisionFace face;
        for (std::size_t k = 0; k < 4U; ++k) {
            face.bounds[k] = rd_s32(f, k * 4U);
        }
        face.flags = rd_u16(f, 0x10U);
        face.vertex_count = rd_u16(f, 0x12U);
        face.plane.x = rd_s16(f, 0x14U);
        face.plane.y = rd_s16(f, 0x16U);
        face.plane.z = rd_s16(f, 0x18U);
        face.plane.distance = rd_s32(f, 0x1CU);
        for (std::size_t e = 0; e < 4U; ++e) {
            const std::size_t o = 0x20U + e * 0x0CU;
            face.edges[e].x = rd_s16(f, o);
            face.edges[e].z = rd_s16(f, o + 4U);
            face.edges[e].distance = rd_s32(f, o + 8U);
        }
        for (std::size_t v = 0; v < 4U; ++v) {
            const std::size_t o = 0x50U + v * 0x0CU;
            face.vertices[v] = {rd_s32(f, o), rd_s32(f, o + 4U),
                                rd_s32(f, o + 8U)};
        }
        out.faces.push_back(face);
    }

    out.named.reserve(named_count);
    for (std::size_t i = 0; i < named_count; ++i) {
        const std::uint8_t* r = data + named_records + i * 0x14U;
        CollisionNamedRecord record;
        const auto* text = reinterpret_cast<const char*>(r);
        std::size_t len = 0;
        while (len < 0x0CU && text[len] != 0) {
            ++len;
        }
        record.name.assign(text, len);
        record.code = rd_u32(r, 0x0CU);
        record.payload = rd_u32(r, 0x10U);
        out.named.push_back(std::move(record));
    }
    return out;
}

GroundHit ground_at(
    const CollisionModel& model,
    const std::int32_t x,
    const std::int32_t z,
    const std::int32_t from_y,
    const std::int32_t to_y) {
    GroundHit best;
    // The game keeps the nearest fraction so far and rejects anything further.
    std::int32_t best_fraction = kFractionOne;

    for (std::size_t i = 0; i < model.faces.size(); ++i) {
        const CollisionFace& face = model.faces[i];
        if ((face.flags & kFaceFlagSkip) != 0U) {
            continue;
        }
        // Broad phase: a point query must lie inside the face's 2D bound.
        if (x < face.bounds[0] || z < face.bounds[1] || x > face.bounds[2]
            || z > face.bounds[3]) {
            continue;
        }

        std::int32_t start_value = 0;
        std::int32_t end_value = 0;
        if ((face.flags & kFaceFlagVerticalPlane) != 0U) {
            start_value = from_y;
            end_value = to_y;
        } else {
            const std::int64_t horizontal =
                static_cast<std::int64_t>(face.plane.x) * x
                + static_cast<std::int64_t>(face.plane.z) * z;
            start_value = static_cast<std::int32_t>(
                (horizontal + static_cast<std::int64_t>(face.plane.y) * from_y
                 + 0x800)
                >> 12);
            end_value = static_cast<std::int32_t>(
                (horizontal + static_cast<std::int64_t>(face.plane.y) * to_y
                 + 0x800)
                >> 12);
        }
        // The segment must cross the plane downward.
        if (start_value <= end_value) {
            continue;
        }
        const std::int32_t start_above = start_value - face.plane.distance;
        if (start_above < -kEpsilon) {
            continue;  // it starts below the surface
        }
        if (end_value - face.plane.distance > kEpsilon) {
            continue;  // it ends above the surface
        }

        const std::int32_t span = start_value - end_value;
        // Compare without dividing, exactly as the original does.
        const std::int32_t scaled_best = static_cast<std::int32_t>(
            (static_cast<std::int64_t>(best_fraction) * span) >> 27);
        if (start_above >= scaled_best) {
            continue;  // something nearer already won
        }

        std::int32_t fraction = 0;
        if (start_above < 0) {
            fraction = 0;
        } else if (start_above > span) {
            fraction = kFractionOne;
        } else {
            fraction = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(start_above) << 27) / span);
        }

        // Inside test: three edges always, the fourth only for a quad.
        const std::size_t edge_count = face.vertex_count == 4U ? 4U : 3U;
        bool inside = true;
        for (std::size_t e = 0; e < edge_count; ++e) {
            const std::int32_t value =
                dot_fx(x, face.edges[e].x, z, face.edges[e].z);
            if (value < face.edges[e].distance - kEpsilon) {
                inside = false;
                break;
            }
        }
        if (!inside) {
            continue;
        }

        best_fraction = fraction;
        best.hit = true;
        best.face_index = i;
        best.fraction = fraction;
        best.y = static_cast<std::int32_t>(
            from_y
            + ((static_cast<std::int64_t>(to_y - from_y) * fraction) >> 27));
    }
    return best;
}

}  // namespace khdays::assets
