// A made-up KH2 effect (PAX) in the layout KH2's own have (docs/FORMATS.md, "KH2's effects"), for the tests of its
// player (kh2_effect_test.cpp) and of the corridors' rules (kh2_corridor_test.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace khdays_test {

struct Bytes {
    std::vector<std::uint8_t> b;
    explicit Bytes(std::size_t n) : b(n, 0) {}
    void u8(std::size_t at, std::uint8_t v) { b.at(at) = v; }
    void u16(std::size_t at, std::uint16_t v) { std::memcpy(&b.at(at), &v, 2); }
    void u32(std::size_t at, std::uint32_t v) { std::memcpy(&b.at(at), &v, 4); }
    void s32(std::size_t at, std::int32_t v) { std::memcpy(&b.at(at), &v, 4); }
    void f32(std::size_t at, float v) { std::memcpy(&b.at(at), &v, 4); }
    void u64(std::size_t at, std::uint64_t v) { std::memcpy(&b.at(at), &v, 8); }
};

// A PAX: header -> 0x82 directory at 0x10 (no entries, one block) -> the 0x96 block at 0x40 with one emitter, one
// picture, one model. The emitter's layer: pppPoint (0, -10, 0), pppScale 2, pppColor 0x4000 each (then half its
// alpha at frame 2), pppMatrixXYZ, pppDrawMatrix, pppDrawMdl3 of model 0. `life` and `loop_end` in frames (0: never),
// `repeat` the emitter's flag.
inline std::vector<std::uint8_t> made_up(int life, int loop_end, bool repeat)
{
    Bytes x(0x1000);
    std::memcpy(x.b.data(), "PAX_", 4);
    x.u32(0xc, 0x10);           // the element table's end: the directory
    x.u32(0x10, 0x82);          // the directory, no 32-byte entries
    x.u32(0x20, 1);             // one block,
    x.u32(0x24, 0x30);          // at the directory + 0x30
    const std::size_t B = 0x40; // the block: {0x96, emitters, pictures, shapes, models, vertex lists}
    x.u32(B, 0x96);
    x.u32(B + 4, 1);
    x.u32(B + 8, 0x80); // the emitter at B + 0x80
    x.u32(B + 12, 1);
    x.u32(B + 16, 0x800); // the picture
    x.u32(B + 20, 0);     // no shape
    x.u32(B + 24, 1);
    x.u32(B + 28, 0x600); // the model
    x.u32(B + 32, 0);     // no vertex list
    const std::size_t E = B + 0x80, base = E + 0x110;
    constexpr std::int32_t F = 0x1000, never = 0x70000000;
    x.s32(base, 30 * F);         // the emitter's length
    x.u32(base + 4, repeat ? 1u : 0u);
    x.u32(base + 0xc, 0x10);     // the layers' list
    x.u32(base + 0x10, 1);
    x.u32(base + 0x14, 0x20);    // the layer at base + 0x20
    const std::size_t L = base + 0x20;
    x.s32(L + 0x10, 0);
    x.s32(L + 0x14, life ? life * F : never);
    x.s32(L + 0x18, loop_end ? 0 : never);
    x.s32(L + 0x1c, loop_end ? loop_end * F : never);
    x.u16(L + 0x26, 6);
    // the commands {code, size, keys, data, slots} (from base); their data from base + 0x100, slots from + 0x180
    struct C {
        std::uint32_t code;
        std::uint16_t size, keys;
    };
    const C cmds[] = {{0x28, 0x10, 1}, {0x2a, 0x10, 1}, {0x2b, 0xc, 2}, {0x57, 4, 1}, {0x70, 4, 1}, {0x89, 0x10, 1}};
    std::size_t data = 0x100, slots = 0x180;
    // the slots: position 0x20, scale 0x40, colour 0x50, matrix [0x20, 0x30, 0x40]
    const std::int32_t slot_lists[6][3] = {{0x20, 0, 0}, {0x40, 0, 0}, {0x50, 0, 0}, {0x20, 0x30, 0x40}, {0, 0, 0},
                                           {0x50, 0, 0}};
    for (int c = 0; c < 6; ++c) {
        const std::size_t at = L + 0x28 + 16 * std::size_t(c);
        x.u32(at, cmds[c].code);
        x.u16(at + 4, cmds[c].size);
        x.u16(at + 6, cmds[c].keys);
        x.u32(at + 8, std::uint32_t(data));
        x.u32(at + 12, std::uint32_t(slots));
        for (int k = 0; k < 3; ++k)
            x.s32(base + slots + 4 * std::size_t(k), slot_lists[c][k]);
        const std::size_t d = base + data;
        switch (cmds[c].code) {
        case 0x28: x.f32(d + 8, -10.0F); break;
        case 0x2a: x.f32(d + 4, 2.0F), x.f32(d + 8, 2.0F), x.f32(d + 12, 2.0F); break;
        case 0x2b:
            for (int k = 0; k < 4; ++k)
                x.u16(d + 4 + 2 * std::size_t(k), 0x4000);
            x.s32(d + 0xc, 2 * F); // the second key, frame 2: alpha -0x2000
            x.u16(d + 0xc + 4 + 6, std::uint16_t(-0x2000));
            break;
        case 0x89: x.s32(d + 4, 0); break; // model 0
        default: break;
        }
        x.u32(d + std::size_t(cmds[c].size) * cmds[c].keys, 0xfffff000u);
        data += std::size_t(cmds[c].size) * cmds[c].keys + 4;
        slots += 0x10;
    }
    // the model: header at +0x10 {flags 7, primitives +0x20, vertices +0x80, normals, counts, ALPHA 0x48}
    const std::size_t h = B + 0x600 + 0x10;
    x.u8(h, 7);
    x.s32(h + 4, 0x20);
    x.s32(h + 8, 0x80);
    x.u16(h + 0x10, 1);
    x.u16(h + 0x12, 4);
    x.u8(h + 0x16, 0x48);
    const std::size_t p = h + 0x20;
    x.u8(p + 1, 6); // a packet of one textured quad
    x.u16(p + 2, 1);
    const std::uint64_t tex0 = 0x3000u | (2ull << 14) | (0x13ull << 20) | (1ull << 26) | (1ull << 30) | (0x3e00ull << 37);
    x.u64(p + 8, tex0);
    const std::size_t q = p + 0x10;
    for (int c = 0; c < 4; ++c) {
        for (int k = 0; k < 4; ++k)
            x.u8(q + 4 * std::size_t(c) + std::size_t(k), 0x80);
        x.u16(q + 0x10 + 2 * std::size_t(c), std::uint16_t(c));
        x.u16(q + 0x18 + 4 * std::size_t(c), std::uint16_t((c & 1) * 4096));
        x.u16(q + 0x1a + 4 * std::size_t(c), std::uint16_t((c >> 1) * 4096));
    }
    x.u8(p + 0x10 + 0x28 + 1, 0xff); // the end of the packets
    const std::int16_t v[4][3] = {{-1, -1, 0}, {1, -1, 0}, {-1, 1, 0}, {1, 1, 0}};
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < 3; ++k)
            x.u16(h + 0x80 + 6 * std::size_t(c) + 2 * std::size_t(k), std::uint16_t(v[c][k]));
    // the picture: 2 x 2, 8-bit, indices 0..3, palette entry 1 red, 3 half-alpha white
    const std::size_t pic = B + 0x800;
    const std::uint16_t head[8] = {0x3000, 0x3e00, 2, 0x13, 0, 0, 2, 2};
    for (int k = 0; k < 8; ++k)
        x.u16(pic + 2 * std::size_t(k), head[k]);
    for (int k = 0; k < 4; ++k)
        x.u8(pic + 32 + std::size_t(k), std::uint8_t(k));
    x.u32(pic + 36 + 4 * 1, 0x800000ffu);
    x.u32(pic + 36 + 4 * 3, 0x40ffffffu);
    return x.b;
}

} // namespace khdays_test
