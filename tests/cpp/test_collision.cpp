#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "khdays/assets/collision.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& b, const std::size_t o, const std::uint16_t v) {
    b.at(o) = static_cast<std::uint8_t>(v & 0xFFU);
    b.at(o + 1U) = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}

void put_u32(Bytes& b, const std::size_t o, const std::uint32_t v) {
    for (std::size_t i = 0; i < 4U; ++i) {
        b.at(o + i) = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
    }
}

void put_s32(Bytes& b, const std::size_t o, const std::int32_t v) {
    put_u32(b, o, static_cast<std::uint32_t>(v));
}

void put_s16(Bytes& b, const std::size_t o, const std::int16_t v) {
    put_u16(b, o, static_cast<std::uint16_t>(v));
}

void expect(const bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

constexpr std::int32_t kOne = 0x1000;

// A room blob holding one axis-aligned quad: the square x,z in [-1, 1] at
// height 0.5, with the four inward edge planes the game's inside test needs.
// Section offsets chain exactly as the shipped blobs do.
Bytes make_blob() {
    constexpr std::size_t kHeader = 0xB0U;
    constexpr std::size_t kFace = kHeader;         // no tree nodes
    constexpr std::size_t kNamed = kFace + 0x88U;  // no 0x84 faces
    constexpr std::size_t kEnd = kNamed + 0x14U;

    Bytes b(kEnd, 0U);
    put_u16(b, 0x74U, 0U);              // flags
    put_u16(b, 0x7AU, 0U);              // node count
    put_u16(b, 0x7CU, 1U);              // one 0x88 face
    put_u16(b, 0x7EU, 0U);
    put_u16(b, 0x80U, 0U);
    put_u16(b, 0x82U, 1U);              // one named record
    put_u32(b, 0x94U, kHeader);
    put_u32(b, 0x98U, kHeader);
    put_u32(b, 0x9CU, kHeader);         // tree root
    put_u32(b, 0xA0U, kFace);           // faces of 0x88
    put_u32(b, 0xA4U, kNamed);
    put_u32(b, 0xA8U, kNamed);
    put_u32(b, 0xACU, kNamed);          // named records

    put_s32(b, kFace + 0x00U, -kOne);   // minX
    put_s32(b, kFace + 0x04U, -kOne);   // minZ
    put_s32(b, kFace + 0x08U, kOne);    // maxX
    put_s32(b, kFace + 0x0CU, kOne);    // maxZ
    put_u16(b, kFace + 0x10U, 0x8000U); // last of the run
    put_u16(b, kFace + 0x12U, 4U);      // a quad
    put_s16(b, kFace + 0x14U, 0);       // plane normal (0, 1, 0)
    put_s16(b, kFace + 0x16U, static_cast<std::int16_t>(kOne));
    put_s16(b, kFace + 0x18U, 0);
    put_s32(b, kFace + 0x1CU, kOne / 2);  // the surface sits at y = 0.5

    // Edge planes, each pointing inward, all at distance -1.
    const std::int16_t dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (std::size_t e = 0; e < 4U; ++e) {
        const std::size_t o = kFace + 0x20U + e * 0x0CU;
        put_s16(b, o, static_cast<std::int16_t>(dirs[e][0] * kOne));
        put_s16(b, o + 4U, static_cast<std::int16_t>(dirs[e][1] * kOne));
        put_s32(b, o + 8U, -kOne);
    }
    // Corners, for completeness.
    const std::int32_t corners[4][2] = {
        {-kOne, -kOne}, {kOne, -kOne}, {kOne, kOne}, {-kOne, kOne}};
    for (std::size_t v = 0; v < 4U; ++v) {
        const std::size_t o = kFace + 0x50U + v * 0x0CU;
        put_s32(b, o, corners[v][0]);
        put_s32(b, o + 4U, kOne / 2);
        put_s32(b, o + 8U, corners[v][1]);
    }

    const char name[] = "nohit";
    for (std::size_t i = 0; i < sizeof(name) - 1U; ++i) {
        b.at(kNamed + i) = static_cast<std::uint8_t>(name[i]);
    }
    return b;
}

}  // namespace

int main() {
    try {
        const Bytes blob = make_blob();
        const auto model =
            khdays::assets::decode_collision_model(blob.data(), blob.size());
        expect(model.valid, "a well-formed blob decodes");
        expect(model.faces.size() == 1U, "one face");
        expect(model.faces[0].vertex_count == 4U, "the face is a quad");
        expect(model.faces[0].plane.y == kOne, "the plane points up");
        expect(model.faces[0].plane.distance == kOne / 2,
               "the plane distance survives");
        expect(model.named.size() == 1U && model.named[0].name == "nohit",
               "the named record decodes, NUL-trimmed");

        // The roster ground ray: start one unit up, cast fifty down.
        const std::int32_t from_y = kOne;
        const std::int32_t to_y = kOne - 0x32000;

        const auto centre =
            khdays::assets::ground_at(model, 0, 0, from_y, to_y);
        expect(centre.hit, "the ray finds the floor at the centre");
        expect(centre.y == kOne / 2,
               "the reported height is the plane's own distance");

        // Just inside a corner still hits; outside the 2D bound does not.
        expect(khdays::assets::ground_at(model, kOne - 1, kOne - 1, from_y,
                                         to_y)
                   .hit,
               "just inside the corner hits");
        expect(!khdays::assets::ground_at(model, 4 * kOne, 0, from_y, to_y).hit,
               "outside the bound misses");

        // A ray that starts below the surface must not hit it: the game rejects
        // a segment whose start is under the plane by more than its epsilon.
        expect(!khdays::assets::ground_at(model, 0, 0, 0, -0x32000).hit,
               "a ray starting below the surface misses");

        // An upward segment never hits: the test requires a downward crossing.
        expect(!khdays::assets::ground_at(model, 0, 0, to_y, from_y).hit,
               "an upward ray misses");

        // A header whose sections run past the end must fail loudly rather
        // than produce plausible geometry.
        Bytes broken = blob;
        put_u16(broken, 0x7CU, 0x400U);  // far more faces than the blob holds
        expect(!khdays::assets::decode_collision_model(broken.data(),
                                                       broken.size())
                    .valid,
               "an inconsistent header is rejected");

        std::cout << "Collision test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Collision test failed: " << error.what() << '\n';
        return 1;
    }
}
