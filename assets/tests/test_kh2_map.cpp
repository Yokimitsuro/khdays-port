#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "khdays/assets/dds.h"
#include "khdays/assets/kh2_map.h"
#include "khdays/assets/kh2_room.h"

// KH2 map files made here, byte by byte, as the readers expect them: a BAR
// holding a background model and its textures, a collision, placements, a
// piece with its textures and motions, and a radar picture; DDS pictures.

namespace {

namespace kh2 = khdays::assets::kh2;
using Bytes = std::vector<std::uint8_t>;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4F;
}

struct Writer final {
    Bytes b;

    std::size_t size() const {
        return b.size();
    }
    void u8(unsigned v) {
        b.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    }
    void u16(unsigned v) {
        u8(v);
        u8(v >> 8U);
    }
    void u32(std::uint32_t v) {
        u16(v & 0xFFFFU);
        u16(v >> 16U);
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v >> 32U));
    }
    void f32(float f) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        u32(bits);
    }
    void text(const char* s, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            u8(static_cast<unsigned char>(s[i]));
        }
    }
    void bytes(const Bytes& other) {
        b.insert(b.end(), other.begin(), other.end());
    }
    void zeros(std::size_t n) {
        b.insert(b.end(), n, 0);
    }
    void pad(std::size_t align) {
        while (b.size() % align != 0) {
            u8(0);
        }
    }
    void set_u32(std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            b.at(at + i) = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU);
        }
    }
};

struct Entry final {
    std::uint32_t type;
    std::string name;
    Bytes data;
};

Bytes bar(const std::vector<Entry>& entries) {
    Writer w;
    w.text("BAR\x01", 4);
    w.u32(static_cast<std::uint32_t>(entries.size()));
    w.zeros(8);
    const std::size_t table = w.size();
    w.zeros(entries.size() * 16U);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        w.pad(16);
        const std::size_t e = table + i * 16U;
        w.set_u32(e, entries[i].type);
        for (std::size_t c = 0; c < 4 && c < entries[i].name.size(); ++c) {
            w.b[e + 4 + c] = static_cast<std::uint8_t>(entries[i].name[c]);
        }
        w.set_u32(e + 8, static_cast<std::uint32_t>(w.size()));
        w.set_u32(e + 12, static_cast<std::uint32_t>(entries[i].data.size()));
        w.bytes(entries[i].data);
    }
    return w.b;
}

// --- VIF ---

void vif(Writer& w, unsigned cmd, unsigned num, unsigned imm) {
    w.u32(imm | (num << 16U) | (cmd << 24U));
}

constexpr unsigned kUnsigned = 1U << 14U;

struct TestVertex final {
    int u;
    int v;
    unsigned index;
    unsigned flag;
    std::array<unsigned, 4> color;
};

// A packet's lists: header quadwords at 0, UVs (with indices in z and flags in
// w) at 4, colours after them, positions after the colours.
Writer list_codes(const std::vector<TestVertex>& vertices, const std::vector<std::array<float, 3>>& positions,
                  const std::uint32_t affiliation, const std::uint32_t matrices) {
    const auto n = static_cast<std::uint32_t>(vertices.size());
    const auto np = static_cast<std::uint32_t>(positions.size());
    Writer w;
    vif(w, 0x00, 0, 0);       // NOP
    vif(w, 0x01, 0, 0x0101);  // STCYCL 1, 1
    vif(w, 0x6C, 4, 0);       // UNPACK V4-32: the header
    for (const std::uint32_t word :
         {1U, 0U, 0U, 0U, n, 4U, affiliation, 0U, n, 4U + n, 0U, 0U, np, 4U + 2 * n, 0U, matrices}) {
        w.u32(word);
    }
    vif(w, 0x65, n, 4);  // UNPACK V2-16 (signed): UVs
    for (const auto& v : vertices) {
        w.u16(static_cast<unsigned>(v.u));
        w.u16(static_cast<unsigned>(v.v));
    }
    vif(w, 0x20, 0, 0);  // STMASK: into z only
    w.u32(0xCFCFCFCFU);
    vif(w, 0x72, n, 4 | kUnsigned);  // UNPACK S-8 masked: indices
    for (const auto& v : vertices) {
        w.u8(v.index);
    }
    w.pad(4);
    vif(w, 0x20, 0, 0);  // STMASK: into w only
    w.u32(0x3F3F3F3FU);
    vif(w, 0x72, n, 4 | kUnsigned);  // flags
    for (const auto& v : vertices) {
        w.u8(v.flag);
    }
    w.pad(4);
    vif(w, 0x6E, n, (4 + n) | kUnsigned);  // UNPACK V4-8: colours
    for (const auto& v : vertices) {
        for (const unsigned c : v.color) {
            w.u8(c);
        }
    }
    vif(w, 0x31, 0, 0);  // STCOL 1.0
    for (int i = 0; i < 4; ++i) {
        w.f32(1.0F);
    }
    vif(w, 0x20, 0, 0);  // STMASK: w from STCOL
    w.u32(0x80808080U);
    vif(w, 0x78, np, 4 + 2 * n);  // UNPACK V3-32 masked: positions
    for (const auto& p : positions) {
        for (const float f : p) {
            w.f32(f);
        }
    }
    return w;
}

// VIF codes padded with NOPs to 8 bytes + whole quadwords (a tag's upper half and its data).
Bytes tag_body(Bytes codes) {
    while (codes.size() < 8 || (codes.size() - 8) % 16 != 0) {
        codes.push_back(0);
    }
    return codes;
}

void cnt_tag(Writer& w, const Bytes& codes) {
    const Bytes body = tag_body(codes);
    w.u32(static_cast<std::uint32_t>((body.size() - 8) / 16) | (1U << 28U));
    w.u32(0);
    w.bytes(body);
}

void ret_tag(Writer& w) {
    w.u32(6U << 28U);
    w.zeros(12);
}

// --- The entries ---

Bytes map_model() {
    Writer w;
    w.zeros(0x90);
    const std::size_t h = w.size();
    w.u32(2);
    w.zeros(12);
    w.u32(2);  // chains
    w.zeros(12);
    const std::size_t chains = w.size();
    w.zeros(32);
    // Chain 0: texture 1, two triangles (0x20, then 0x00: seen from both sides).
    const std::size_t first = w.size();
    const std::vector<TestVertex> strip0 = {{0, 0, 0, 0x10, {0x80, 0x40, 0x20, 0x80}},
                                            {4096, 0, 1, 0x10, {0x80, 0x80, 0x80, 0x40}},
                                            {0, 4096, 2, 0x20, {0x80, 0x80, 0x80, 0x80}},
                                            {-2048, 2048, 3, 0x00, {0x10, 0x20, 0x30, 0x80}}};
    cnt_tag(w, list_codes(strip0, {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}, {100, 50, 100}}, 0, 0).b);
    ret_tag(w);
    // Chain 1: texture 0, transparent, scrolling by UVSC 1; one turned triangle (0x30).
    const std::size_t second = w.size();
    const std::vector<TestVertex> strip1 = {{0, 0, 0, 0x10, {0x80, 0x80, 0x80, 0x80}},
                                            {0, 0, 1, 0x10, {0x80, 0x80, 0x80, 0x80}},
                                            {0, 0, 2, 0x30, {0x80, 0x80, 0x80, 0x80}}};
    cnt_tag(w, list_codes(strip1, {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}, 0, 0).b);
    ret_tag(w);
    w.set_u32(chains, static_cast<std::uint32_t>(first - h));
    w.set_u32(chains + 4, 1);
    w.set_u32(chains + 16, static_cast<std::uint32_t>(second - h));
    w.set_u32(chains + 20, 0);
    w.b[chains + 16 + 10] = 1;                // transparent
    w.b[chains + 16 + 12] = 0x02 | (1 << 2);  // scroll, UVSC 1
    return w.b;
}

Bytes object_model() {
    Writer w;
    w.zeros(0x90);
    const std::size_t h = w.size();
    w.u32(3);
    w.zeros(12);
    w.u16(2);  // bones
    w.u16(0);
    w.u32(0x40);  // bone table
    w.u32(0);
    w.u16(1);  // subparts
    w.u16(0);
    const std::size_t subpart = w.size();
    w.zeros(0x20);
    for (int i = 0; i < 2; ++i) {  // bones
        w.u16(static_cast<unsigned>(i));
        w.u16(0);
        w.u32(i == 0 ? 0xFFFFFFFFU : 0U);
        w.zeros(8);
        for (const float f : {1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.5F * static_cast<float>(i), 0.0F,
                              10.0F * static_cast<float>(i), 0.0F, 0.0F, 1.0F}) {
            w.f32(f);
        }
    }
    // The packet's lists: the first part brought by a ref tag from here, the
    // rest inline in the cnt tag; positions 0-1 go with bone 0, 2 with bone 1.
    const std::vector<TestVertex> strip = {{0, 0, 0, 0x10, {0x80, 0x80, 0x80, 0x80}},
                                           {0, 0, 1, 0x10, {0x80, 0x80, 0x80, 0x80}},
                                           {0, 0, 2, 0x20, {0x20, 0x40, 0x60, 0x80}}};
    Writer codes = list_codes(strip, {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, 20, 2);
    vif(codes, 0x6C, 1, 20);  // the affiliation list
    for (const std::uint32_t c : {2U, 1U, 0U, 0U}) {
        codes.u32(c);
    }
    const Bytes all = tag_body(codes.b);
    const std::size_t ref_quads = 2;
    const Bytes ref_part(all.begin(), all.begin() + 8 + static_cast<std::ptrdiff_t>(ref_quads * 16));
    const Bytes cnt_part(all.begin() + 8 + static_cast<std::ptrdiff_t>(ref_quads * 16), all.end());
    w.pad(16);
    const std::size_t ref_data = w.size();
    w.bytes(Bytes(ref_part.begin() + 8, ref_part.end()));
    const std::size_t chain = w.size();
    w.u32(static_cast<std::uint32_t>(ref_quads) | (3U << 28U));  // ref: the packet's first part
    w.u32(static_cast<std::uint32_t>(ref_data - h));
    w.bytes(Bytes(ref_part.begin(), ref_part.begin() + 8));
    for (const auto& [bone, address] : {std::pair{1U, 64U}, std::pair{0U, 60U}}) {  // matrices, out of order
        w.u32(4U | (3U << 28U));
        w.u32(bone);
        vif(w, 0x01, 0, 0x0101);
        vif(w, 0x6C, 4, address);
    }
    cnt_tag(w, cnt_part);
    ret_tag(w);
    w.set_u32(subpart + 4, 0);
    w.set_u32(subpart + 0x10, static_cast<std::uint32_t>(chain - h));
    return w.b;
}

void gs_info(Writer& w, unsigned tw, unsigned th, std::uint64_t clamp) {
    const std::size_t g = w.size();
    w.zeros(0xA0);
    const std::uint64_t tex0 = (static_cast<std::uint64_t>(tw) << 26U) | (static_cast<std::uint64_t>(th) << 30U);
    for (int i = 0; i < 8; ++i) {
        w.b[g + 112 + i] = static_cast<std::uint8_t>((tex0 >> (8 * i)) & 0xFFU);
        w.b[g + 128 + i] = static_cast<std::uint8_t>((clamp >> (8 * i)) & 0xFFU);
    }
}

std::uint64_t clamp_bits(unsigned wms, unsigned wmt, unsigned minu, unsigned maxu, unsigned minv, unsigned maxv) {
    return wms | (wmt << 2U) | (static_cast<std::uint64_t>(minu) << 4U) | (static_cast<std::uint64_t>(maxu) << 14U) |
           (static_cast<std::uint64_t>(minv) << 24U) | (static_cast<std::uint64_t>(maxv) << 34U);
}

Bytes texture_container(bool footer) {
    Writer w;
    const unsigned count = footer ? 2 : 1;
    w.u32(0);
    w.u32(footer ? 4 : 0);  // colours
    w.u32(1);               // transfers
    w.u32(count);
    w.u32(0x24);
    w.u32(0x30);  // CLUT transfer
    w.u32(0x40);  // GS infos
    w.u32(0);
    w.u32(0x40 + count * 0xA0);  // palette
    w.zeros(0x40 - w.size());
    gs_info(w, 8, 7, clamp_bits(2, 2, 64, 127, 0, 63));  // 256x128, region 64..127 x 0..63
    if (footer) {
        gs_info(w, 8, 8, clamp_bits(3, 0, 127, 128, 0, 0));  // 256x256, u repeating 128..255
        w.zeros(16);                                         // the palette
        w.text("_DMY", 4);
        w.u32(0);
        w.text("UVSC", 4);
        w.u32(12);
        w.u32(1);
        w.f32(1000.0F);
        w.f32(-2000.0F);
        w.text("TEXA", 4);
        w.u32(88);
        const std::size_t t = w.size();
        for (const unsigned v : {2U, 0U, 0U, 4U, 0U, 0U, 1U, 2U, 64U, 0U, 32U, 32U}) {
            w.u16(v);
        }
        w.u32(40);  // slot table
        w.u32(44);  // animation table
        w.u32(72);  // sprites
        w.u32(0);
        w.u32(0);   // the slot table
        w.u32(48);  // animation 0's frames
        w.u16(0);   // sprite 0 for 10 frames
        w.u16(10);
        w.u16(10);
        w.u16(0);
        w.u16(1);  // the base picture for 20 to 30
        w.u16(20);
        w.u16(30);
        w.u16(0);
        w.u16(2U | (static_cast<unsigned>(-2) << 4U));  // back 2 steps
        w.zeros(6);
        w.zeros(t + 88 - w.size());
    }
    w.text("_KN5", 4);
    return w.b;
}

Bytes collision() {
    Writer w;
    w.text("COCT", 4);
    w.u32(1);
    w.zeros(8);
    const std::size_t tables = w.size();
    w.zeros(64);
    const std::size_t faces = w.size();
    // A floor (a quad) and a wall (a triangle).
    for (const auto& v : {std::array<int, 4>{0, 1, 2, 3}, std::array<int, 4>{0, 1, 4, -1}}) {
        w.u16(0);
        for (const int i : v) {
            w.u16(static_cast<unsigned>(i));
        }
        w.u16(0);
        w.u16(0xFFFF);
        w.u16(v[3] < 0 ? 1 : 0);
    }
    const std::size_t vertices = w.size();
    for (const auto& p :
         {std::array<float, 3>{0, 0, 0}, std::array<float, 3>{100, 0, 0}, std::array<float, 3>{100, 0, -100},
          std::array<float, 3>{0, 0, -100}, std::array<float, 3>{0, -100, 0}}) {
        for (const float f : p) {
            w.f32(f);
        }
        w.f32(1.0F);
    }
    const std::size_t flags = w.size();
    w.u32(0x31);
    w.u32(0x20);
    w.set_u32(tables + 0 * 8 + 0, 0);  // header
    w.set_u32(tables + 0 * 8 + 4, 0x50);
    w.set_u32(tables + 3 * 8 + 0, static_cast<std::uint32_t>(faces));
    w.set_u32(tables + 3 * 8 + 4, 32);
    w.set_u32(tables + 4 * 8 + 0, static_cast<std::uint32_t>(vertices));
    w.set_u32(tables + 4 * 8 + 4, 80);
    w.set_u32(tables + 7 * 8 + 0, static_cast<std::uint32_t>(flags));
    w.set_u32(tables + 7 * 8 + 4, 8);
    return w.b;
}

Bytes placements() {
    Writer w;
    w.u32(8);
    w.u32(0x68 * 2);
    for (const float y : {-200.0F, 1000000.0F}) {
        for (const float f : {100.0F, y, 300.0F, 0.0F, 1.5F, 0.0F, y > 0 ? -1.0F : 1.0F, 1.0F, 1.0F}) {
            w.f32(f);
        }
        w.u32(0);  // the piece
        w.u32(0);
        w.u32(y > 0 ? 0xFFFFFFFFU : 0U);  // its motion: none for the hidden one
        w.zeros(0x68 - 48);
    }
    return w.b;
}

Bytes motion() {
    Writer w;
    w.zeros(0x90);
    const std::size_t base = w.size();
    w.u32(0);  // interpolated
    w.zeros(12);
    const std::size_t h = w.size();
    w.zeros(0xA0);
    auto field = [&](std::size_t o, std::uint32_t v) { w.set_u32(h + o, v); };
    auto field_f = [&](std::size_t o, float f) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        field(o, bits);
    };
    field(0x00, 1U | (2U << 16U));
    field(0x04, 20);
    field_f(0x80, 0.0F);
    field_f(0x84, 20.0F);
    field_f(0x88, 30.0F);
    field_f(0x8c, 0.0F);
    auto here = [&] { return static_cast<std::uint32_t>(w.size() - base); };
    field(0x14, here());  // pose
    field(0x18, 1);
    w.u16(0);
    w.u16(7);
    w.f32(5.0F);
    field(0x20, here());  // curves: translation x repeating after its keys; rotation y Hermite
    field(0x24, 2);
    w.u16(0);
    w.u8(6U | (0U << 4U) | (2U << 6U));
    w.u8(3);
    w.u16(0);
    w.u16(0);
    w.u8(4);
    w.u8(2);
    w.u16(3);
    w.pad(4);
    field(0x30, here());  // keys: (interpolation | time << 2, value, tangent in, tangent out)
    for (const auto& k :
         {std::array<unsigned, 4>{1U | (0U << 2U), 0, 0, 0}, std::array<unsigned, 4>{0U | (1U << 2U), 1, 0, 0},
          std::array<unsigned, 4>{0U | (2U << 2U), 0, 0, 0}, std::array<unsigned, 4>{2U | (0U << 2U), 2, 0, 0},
          std::array<unsigned, 4>{2U | (1U << 2U), 1, 0, 0}}) {
        for (const unsigned v : k) {
            w.u16(v);
        }
    }
    field(0x34, here());  // times
    for (const float f : {0.0F, 10.0F, 20.0F}) {
        w.f32(f);
    }
    field(0x38, here());  // values
    for (const float f : {0.0F, 100.0F, 1.0F}) {
        w.f32(f);
    }
    field(0x3c, here());  // tangents
    w.f32(0.0F);
    return w.b;
}

Bytes tim2() {
    Writer w;
    w.text("TIM2", 4);
    w.u8(4);
    w.u8(0);
    w.u16(2);
    w.zeros(8);
    // A 4x2 4-bit picture as the radars have it: total and palette size not kept.
    w.u32(0xFFFFFFF7U);
    w.u32(0xFFFFFFF8U);
    w.u32(4);
    w.u16(0x30);
    w.u16(16);
    w.u8(0);
    w.u8(1);
    w.u8(3);
    w.u8(4);
    w.u16(4);
    w.u16(2);
    w.zeros(24);
    w.u8(0x21);  // pixels 1, 2
    w.u8(0x00);
    w.u8(0x00);
    w.u8(0xF0);  // pixel 7 = 15
    for (unsigned i = 0; i < 16; ++i) {
        w.u8(i * 16);
        w.u8(0);
        w.u8(0);
        w.u8(0x80);
    }
    // A 2x2 8-bit picture, its palette in the PS2's order (index 8 stored at 16).
    w.u32(0x30 + 4 + 1024);
    w.u32(1024);
    w.u32(4);
    w.u16(0x30);
    w.u16(256);
    w.u8(0);
    w.u8(1);
    w.u8(3);
    w.u8(5);
    w.u16(2);
    w.u16(2);
    w.zeros(24);
    w.u8(8);
    w.u8(0);
    w.u8(0);
    w.u8(0);
    const std::size_t palette = w.size();
    w.zeros(1024);
    w.b[palette + 16 * 4 + 0] = 10;
    w.b[palette + 16 * 4 + 1] = 20;
    w.b[palette + 16 * 4 + 2] = 30;
    w.b[palette + 16 * 4 + 3] = 0x40;
    return w.b;
}

Bytes dds(const char* fourcc, unsigned width, unsigned height, const Bytes& body) {
    Writer w;
    w.text("DDS ", 4);
    w.u32(124);
    w.u32(0);
    w.u32(height);
    w.u32(width);
    w.zeros(76 - w.size());
    w.u32(32);
    w.u32(fourcc != nullptr ? 4 : 0x41);
    if (fourcc != nullptr) {
        w.text(fourcc, 4);
        w.zeros(20);
    } else {
        w.u32(0);
        w.u32(32);
        w.u32(0x00FF0000U);
        w.u32(0x0000FF00U);
        w.u32(0x000000FFU);
        w.u32(0xFF000000U);
    }
    w.zeros(128 - w.size());
    w.bytes(body);
    return w.b;
}

void test_dds() {
    Writer block;
    block.u8(255);  // alpha endpoints
    block.u8(0);
    block.u8(0x08);  // pixel 1: alpha index 1 (0)
    block.zeros(5);
    block.u16(0xF800);       // red
    block.u16(0x001F);       // blue
    block.u32(0b11100100U);  // pixels 0-3: colours 0, 1, 2, 3
    const auto bc3 = dds("DXT5", 4, 4, block.b);
    const auto t = khdays::assets::decode_dds(bc3.data(), bc3.size(), "bc3");
    expect(t.width == 4 && t.height == 4 && t.format_name == "DXT5", "DXT5 size");
    auto px = [&](const khdays::assets::DecodedTexture& p, int i) {
        return std::array<int, 4>{p.rgba[i * 4], p.rgba[i * 4 + 1], p.rgba[i * 4 + 2], p.rgba[i * 4 + 3]};
    };
    expect(px(t, 0) == std::array<int, 4>{255, 0, 0, 255}, "DXT5 pixel 0");
    expect(px(t, 1) == std::array<int, 4>{0, 0, 255, 0}, "DXT5 pixel 1");
    expect(px(t, 2) == std::array<int, 4>{170, 0, 85, 255}, "DXT5 pixel 2");
    expect(px(t, 3) == std::array<int, 4>{85, 0, 170, 255}, "DXT5 pixel 3");

    const auto plain = dds(nullptr, 2, 1, Bytes{1, 2, 3, 4, 5, 6, 7, 8});
    const auto p = khdays::assets::decode_dds(plain.data(), plain.size(), "plain");
    expect(px(p, 0) == std::array<int, 4>{3, 2, 1, 4} && px(p, 1) == std::array<int, 4>{7, 6, 5, 8},
           "plain DDS by its masks");

    const auto other = dds("DXT1", 4, 4, Bytes(8, 0));
    bool refused = false;
    try {
        (void)khdays::assets::decode_dds(other.data(), other.size(), "dxt1");
    } catch (const std::runtime_error& e) {
        refused = std::string(e.what()).find("DXT1") != std::string::npos;
    }
    expect(refused, "another DDS kind refused by name");
}

Bytes map_file() {
    const Bytes motions = bar({{kh2::kEntryMotion, "tt_0", motion()}, {kh2::kEntryMotion, "tt_1", {}}});
    return bar({{kh2::kEntryModel, "MAP", map_model()},
                {kh2::kEntryTextures, "MAP", texture_container(true)},
                {kh2::kEntryCollision, "ID_t", collision()},
                {kh2::kEntryPlacements, "out", placements()},
                {kh2::kEntryModel, "BOB", object_model()},
                {kh2::kEntryTextures, "BOB", texture_container(false)},
                {0x1001eU, "BOB", motions},
                {kh2::kEntryRadar, "rada", tim2()}});
}

void test_map() {
    const Bytes file = map_file();
    const auto map = kh2::read_map(file.data(), file.size());
    expect(map.entries.size() == 8, "entries");
    expect(map.parts.size() == 2 && map.parts[0].name == "MAP" && map.parts[1].name == "BOB", "parts");
    expect(map.parts[0].model_entry == 0 && map.parts[0].texture_entry == 1 && !map.parts[0].motion_entry,
           "background");
    expect(map.parts[1].model_entry == 4 && map.parts[1].texture_entry == 5 && map.parts[1].motion_entry == 6U,
           "piece");
    expect(map.placements == 3U && map.collision == 2U && map.radar.size() == 1 && map.radar[0] == 7, "other entries");
    expect(map.parts[0].first_hd_picture == 0 && map.parts[1].first_hd_picture == 3 && map.hd_pictures == 4,
           "HD order");

    const auto& e = map.entries;
    const auto textures = kh2::read_textures(file.data(), file.size(), e[1].offset);
    expect(textures.textures.size() == 2, "GS infos");
    const auto& g0 = textures.textures[0];
    expect(g0.width == 256 && g0.height == 128 && g0.wrap_u == 2 && g0.region_x == 64 && g0.region_width == 64 &&
               g0.region_y == 0 && g0.region_height == 64,
           "region clamp");
    const auto& g1 = textures.textures[1];
    expect(g1.wrap_u == 3 && g1.region_x == 128 && g1.region_width == 128 && g1.region_height == 256, "region repeat");
    auto uv = kh2::region_uv(g0, 0.25F, 0.25F);
    expect(near(uv[0], 0.0F) && near(uv[1], 0.5F), "region clamp UVs");
    uv = kh2::region_uv(g0, 0.5F, 0.0F);
    expect(near(uv[0], 1.0F), "region clamp UVs, far edge");
    uv = kh2::region_uv(g1, 0.25F, 0.5F);
    expect(near(uv[0], 0.5F) && near(uv[1], 0.5F), "region repeat UVs");
    expect(textures.scrolls.size() == 1 && textures.scrolls[0].index == 1 && near(textures.scrolls[0].u, 1000.0F) &&
               near(textures.scrolls[0].v, -2000.0F),
           "UVSC");
    expect(textures.animations.size() == 1, "TEXA");
    const auto& a = textures.animations[0];
    expect(a.target == 0 && a.sprite_count == 2 && a.x == 64 && a.width == 32 && a.height == 32, "TEXA header");
    expect(a.animations.size() == 1 && a.animations[0].size() == 3, "TEXA frames");
    const auto& f = a.animations[0];
    expect(f[0].kind == kh2::TextureFrame::Kind::Sprite && f[0].min_length == 10 && f[0].sprite == 0, "frame 0");
    expect(f[1].kind == kh2::TextureFrame::Kind::Base && f[1].min_length == 20 && f[1].max_length == 30, "frame 1");
    expect(f[2].kind == kh2::TextureFrame::Kind::Jump && f[2].jump == -2, "frame 2");
    expect(textures.hd_pictures == std::vector<int>{0, 2} && a.hd_sheet == 1 && textures.hd_count == 3, "HD numbering");

    const auto background = kh2::read_model(file.data(), file.size(), e[0].offset);
    expect(background.kind == 2 && background.groups.size() == 2, "map model groups");
    const auto& c0 = background.groups[0];
    expect(c0.texture == 1 && !c0.transparent && !c0.scroll_index() && c0.triangles.size() == 2, "chain 0");
    expect(!c0.triangles[0].two_sided && c0.triangles[1].two_sided, "two-sided flag 0x00");
    const auto& v0 = c0.triangles[0].corners;
    expect(near(v0[1].position[0], 100.0F) && near(v0[2].position[2], 100.0F), "positions by index");
    expect(near(v0[1].uv[0], 1.0F) && near(v0[2].uv[1], 1.0F), "UVs / 4096");
    expect(v0[0].color == std::array<std::uint8_t, 4>{0x80, 0x40, 0x20, 0x80} && v0[1].color[3] == 0x40, "colours");
    const auto& v1 = c0.triangles[1].corners;
    expect(near(v1[2].uv[0], -0.5F) && near(v1[2].position[1], 50.0F) && v1[2].color[2] == 0x30, "fourth vertex");
    const auto& c1 = background.groups[1];
    expect(c1.texture == 0 && c1.transparent && c1.scroll_index() == 1 && c1.triangles.size() == 1, "chain 1");
    expect(near(c1.triangles[0].corners[0].position[0], 4.0F) && near(c1.triangles[0].corners[1].position[0], 1.0F),
           "turned triangle (0x30)");

    const auto piece = kh2::read_model(file.data(), file.size(), e[4].offset);
    expect(piece.kind == 3 && piece.bones.size() == 2 && piece.groups.size() == 1, "object model");
    expect(piece.bones[0].parent == -1 && piece.bones[1].parent == 0 && near(piece.bones[1].translation[0], 10.0F) &&
               near(piece.bones[1].rotation[2], 0.5F),
           "bones");
    const auto& pt = piece.groups[0].triangles;
    expect(pt.size() == 1, "object triangles");
    expect(pt[0].corners[0].bone == 0 && pt[0].corners[1].bone == 0 && pt[0].corners[2].bone == 1,
           "bones by affiliation");
    expect(near(pt[0].corners[2].position[2], 1.0F) && pt[0].corners[2].color[1] == 0x40, "object vertex");

    const auto rows = kh2::read_placements(file.data(), file.size(), e[3].offset);
    expect(rows.size() == 2 && near(rows[0].position[1], -200.0F) && near(rows[0].rotation[1], 1.5F) &&
               rows[0].piece == 0 && rows[0].motion == 0,
           "placement");
    expect(near(rows[1].position[1], 1000000.0F) && near(rows[1].scale[0], -1.0F) && rows[1].motion == -1,
           "hidden mirrored placement");

    const auto moves = kh2::read_motions(file.data(), file.size(), e[6].offset);
    expect(moves.size() == 1, "motions (empty skipped)");
    const auto& m = moves[0];
    expect(near(m.end, 20.0F) && near(m.fps, 30.0F) && !m.has_constraints, "motion header");
    expect(m.pose.size() == 1 && m.pose[0].channel == 7 && near(m.pose[0].value, 5.0F), "pose");
    expect(m.curves.size() == 2 && m.curves[0].channel == 6 && m.curves[0].post == 2 && m.curves[0].keys.size() == 3,
           "curves");
    expect(near(kh2::evaluate(m.curves[0], 5.0F), 50.0F), "linear key");
    expect(near(kh2::evaluate(m.curves[0], 15.0F), 100.0F), "constant key");
    expect(near(kh2::evaluate(m.curves[0], 25.0F), 50.0F), "repeat after the keys");
    expect(near(kh2::evaluate(m.curves[0], -1.0F), 0.0F), "hold before the keys");
    expect(near(kh2::evaluate(m.curves[1], 5.0F), 50.5F), "Hermite middle");

    const auto faces = kh2::read_collision(file.data(), file.size(), e[2].offset);
    expect(faces.size() == 3, "quad split, triangle kept");
    expect(near(faces[0].corners[2][2], 100.0F) && faces[0].surface == 0x31, "floor in the model's axes");
    expect(near(faces[2].corners[2][1], 100.0F) && faces[2].surface == 0x20, "wall in the model's axes");

    const auto radar = kh2::read_tim2(file.data(), file.size(), e[7].offset);
    expect(radar.size() == 2 && radar[0].width == 4 && radar[0].height == 2, "TIM2 pictures");
    expect(radar[0].rgba[0] == 16 && radar[0].rgba[4] == 32 && radar[0].rgba[7 * 4] == 240 && radar[0].rgba[3] == 255,
           "4-bit pixels");
    expect(radar[1].rgba[0] == 10 && radar[1].rgba[1] == 20 && radar[1].rgba[2] == 30 && radar[1].rgba[3] == 128,
           "8-bit palette in the PS2's order");
}

// The map made into a scene, with small HD pictures in a folder of the test's own.
void test_room(const Bytes& file) {
    const auto folder = std::filesystem::temp_directory_path() / "khdays-kh2-room-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    auto write = [&](const std::string& name, const Bytes& bytes) {
        std::ofstream out(folder / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    auto plain = [](unsigned width, unsigned height, std::uint8_t shade, std::uint8_t first_alpha = 255) {
        Bytes body;
        for (unsigned i = 0; i < width * height; ++i)
            body.insert(body.end(), {static_cast<std::uint8_t>(shade + i), shade, shade,
                                     static_cast<std::uint8_t>(i == 0 ? first_alpha : 255)});
        return dds(nullptr, width, height, body);
    };
    write("-0.dds", plain(4, 4, 10));     // the map's GS info 0
    write("-1.dds", plain(2, 4, 100));    // its sprites, two of 2x2 stacked
    write("-2.dds", plain(4, 4, 50, 0));  // GS info 1, one texel clear; the piece's (-3) left missing
    std::map<std::string, khdays::assets::DecodedTexture> pictures;
    const auto room = kh2::room_model(
        file.data(), file.size(), folder, "tt99",
        [&](const std::string& name, const khdays::assets::DecodedTexture& picture) { pictures[name] = picture; });
    std::filesystem::remove_all(folder);
    const auto& scene = room.scene;
    auto node = [&](const std::string& name) {
        for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
            if (scene.nodes[i].name == name)
                return static_cast<int>(i);
        }
        return -1;
    };
    expect(scene.nodes.size() == 5 && scene.nodes[0].name == "room" && node("background") == 1, "room nodes");
    const int bob = node("bob0");
    expect(bob == 2 && node("bob1") < 0 && node("bob0_j0") == 3 && node("bob0_j1") == 4, "piece nodes");
    const auto& b = scene.nodes[static_cast<std::size_t>(bob)];
    expect(near(b.translation[0], 100.0F) && near(b.translation[1], 200.0F) && near(b.translation[2], -300.0F),
           "piece placed in the models' axes");
    expect(near(b.rotation[1], std::sin(-0.75F)) && near(b.rotation[3], std::cos(-0.75F)), "piece turned");
    expect(scene.nodes[4].parent == 3 && scene.nodes[3].parent == bob, "bones under their parents");
    expect(near(scene.nodes[3].translation[1], 5.0F), "the motion's first pose");
    auto reported = [&](const char* what) {
        return std::any_of(room.report.begin(), room.report.end(),
                           [&](const std::string& line) { return line.find(what) != std::string::npos; });
    };
    expect(reported("hidden until an event") && reported("-3.dds") && reported("random length"), "report");

    expect(scene.meshes.size() == 2 && scene.meshes[0].node == 1 && scene.meshes[0].joints.empty(), "background mesh");
    expect(scene.meshes[1].joints == std::vector<int>{3, 4}, "piece skinned to its bones");
    const auto& pp = scene.meshes[1].primitives;
    expect(pp.size() == 1 && pp[0].joint_of == std::vector<int>{0, 0, 1}, "piece vertices to their bones");
    expect(scene.meshes[0].primitives.size() == 3, "a primitive per material");
    const kh2::Room* r = &room;
    auto material = [&](const char* name) -> const khdays::assets::SceneMaterial* {
        for (const auto& m : r->scene.materials) {
            if (m.name == name)
                return &m;
        }
        return nullptr;
    };
    const auto* one = material("map_t1");
    const auto* two = material("map_t1_2");
    const auto* moving = material("map_t0_cut_scroll1");
    expect(one != nullptr && two != nullptr && moving != nullptr, "materials by texture, sides and scroll");
    expect(one->texture == "tt99_2.png" && !one->double_sided && two->double_sided &&
               one->alpha_mode == khdays::assets::NeutralMesh::AlphaMode::mask,
           "a group not marked translucent, its clear texels cut (the PS2's alpha test), and its two-sided twin");
    expect(moving->texture == "tt99_0.png" && moving->alpha_mode == khdays::assets::NeutralMesh::AlphaMode::mask &&
               !moving->repeat[0],
           "a cut-out, region-clamped material");
    expect(std::fabs(moving->scroll[0] * 2147483648.0F - 4000.0F) < 0.01F &&
               std::fabs(moving->scroll[1] * 2147483648.0F + 4000.0F) < 0.01F,
           "scroll over the region");
    expect(moving->frame_pictures == std::vector<std::string>{"tt99_0.png", "tt99_0_s0.png", "tt99_0_s1.png"} &&
               moving->frames == std::vector<std::array<int, 2>>{{1, 10}, {0, 25}} && moving->frames_loop &&
               moving->frames_loop_from == 0,
           "texture frames");
    expect(pictures.size() == 4 && pictures.count("tt99_0_s1.png") == 1, "pictures handed over");
    const auto& s1 = pictures["tt99_0_s1.png"];
    expect(s1.rgba[0] == 100 && s1.rgba[2] == 104 && pictures["tt99_0.png"].rgba[0] == 10 &&
               s1.rgba[2 * 4 * 4 + 2] == 18,
           "a sprite drawn over its region");

    expect(scene.animations.size() == 1 && scene.animations[0].name == "bob0" && scene.animations[0].loop,
           "the piece's motion");
    const auto& tracks = scene.animations[0].tracks;
    expect(tracks.size() == 2 && tracks[0].path == khdays::assets::SceneTrack::Path::rotation &&
               tracks[1].path == khdays::assets::SceneTrack::Path::translation && tracks[1].node == 3,
           "tracks of the moving channels");
    expect(tracks[1].times.size() == 21 && near(tracks[1].times[5], 5.0F / 30.0F) &&
               near(tracks[1].values[15], 50.0F) && near(tracks[1].values[16], 5.0F),
           "keys every motion frame");

    // the map's own floors and walls, as a scene in its units and axes
    const auto walls = kh2::collision_scene(file.data(), file.size(), "tt99");
    expect(walls.meshes.size() == 1 && walls.meshes[0].primitives.size() == 1 && walls.materials[0].double_sided,
           "collision scene");
    const auto& cp = walls.meshes[0].primitives[0];
    expect(cp.positions.size() == 9 && near(cp.positions[2][2], 100.0F) && near(cp.positions[8][1], 100.0F),
           "collision triangles in the model's axes");

    const std::string gltf = khdays::assets::scene_to_gltf(scene);
    for (const char* part : {"\"khdays_frames\"", "\"khdays_scroll\"", "\"khdays_loop\":true", "\"skins\"",
                             "\"doubleSided\":true", "\"alphaMode\":\"MASK\"", "\"wrapS\":33071"}) {
        expect(gltf.find(part) != std::string::npos, std::string("glTF has ") + part);
    }
}

// A KH2 object (an .mdlx: its model and its textures) made into a scene: in a motion's first pose, turned, its light
// group blended by its brightness, a picture given in place of its own.
void test_object() {
    const auto folder = std::filesystem::temp_directory_path() / "khdays-kh2-object-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    auto write = [&](const std::string& name, const Bytes& bytes) {
        std::ofstream out(folder / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    auto plain = [](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        Bytes body;
        for (unsigned i = 0; i < 16; ++i)
            body.insert(body.end(), {b, g, r, 255});  // stored B, G, R, A (the masks dds() writes)
        return dds(nullptr, 4, 4, body);
    };
    write("-0.dds", plain(10, 20, 30));
    write("other.dds", plain(200, 100, 50));
    const Bytes model = object_model();
    const Bytes mdlx = bar(
        {{kh2::kEntryModel, "f_ex", model}, {kh2::kEntryTextures, "tim_", texture_container(false)}, {23, "f_ex", {}}});
    std::map<std::string, khdays::assets::DecodedTexture> pictures;
    auto sink = [&](const std::string& name, const khdays::assets::DecodedTexture& picture) {
        pictures[name] = picture;
    };

    kh2::ObjectOptions options;
    options.turn = 180.0F;
    options.pose = kh2::Motion{};
    options.pose->pose.push_back({1, 6, 7.0F});  // bone 1's x where its rest has 10
    const auto made = kh2::object_model(mdlx.data(), mdlx.size(), folder, "F_EX040_TEST", sink, options);
    const auto& scene = made.scene;
    expect(scene.nodes.size() == 3 && scene.nodes[0].name == "f_ex040_test" && scene.nodes[1].name == "j0" &&
               scene.nodes[2].name == "j1",
           "the object's node and a node per bone");
    expect(near(scene.nodes[0].rotation[1], 1.0F) && near(scene.nodes[0].rotation[3], 0.0F), "turned half round");
    expect(scene.nodes[1].parent == 0 && scene.nodes[2].parent == 1, "bones under their parents");
    expect(near(scene.nodes[2].translation[0], 7.0F) && near(scene.nodes[1].translation[0], 0.0F), "the motion's pose");
    expect(scene.meshes.size() == 1 && scene.meshes[0].joints == std::vector<int>{1, 2} &&
               scene.meshes[0].primitives.size() == 1 &&
               scene.meshes[0].primitives[0].joint_of == std::vector<int>{0, 0, 1},
           "its mesh skinned to its bones");
    expect(scene.materials.size() == 1, "one material");
    expect(scene.materials[0].texture == "F_EX040_TEST_0.png", "its HD picture: " + scene.materials[0].texture);
    expect(pictures.count("F_EX040_TEST_0.png") == 1 && pictures["F_EX040_TEST_0.png"].rgba[0] == 10,
           "the picture handed over");

    // its group as light added over the model (flag 0x400000), and a picture in place of -0.dds
    Bytes glowing = model;
    glowing[0x90 + 0x20 + 2] = 0x40;
    const Bytes glowing_mdlx =
        bar({{kh2::kEntryModel, "f_ex", glowing}, {kh2::kEntryTextures, "tim_", texture_container(false)}});
    pictures.clear();
    kh2::ObjectOptions other;
    other.pictures[0] = folder / "other.dds";
    const auto lit = kh2::object_model(glowing_mdlx.data(), glowing_mdlx.size(), folder, "F_EX040_TEST", sink, other);
    std::filesystem::remove_all(folder);
    expect(lit.scene.materials.size() == 1 &&
               lit.scene.materials[0].alpha_mode == khdays::assets::NeutralMesh::AlphaMode::blend &&
               lit.scene.materials[0].texture == "F_EX040_TEST_0_light.png",
           "its light blended");
    const auto& light = pictures["F_EX040_TEST_0_light.png"];
    expect(light.rgba.size() == 64 && light.rgba[0] == 200 && light.rgba[3] == 200,
           "the picture given, alpha its brightness");
    expect(std::any_of(lit.report.begin(), lit.report.end(),
                       [](const std::string& line) { return line.find("its light") != std::string::npos; }),
           "the light reported");

    // not an object: refused
    const Bytes map_mdlx =
        bar({{kh2::kEntryModel, "f_ex", map_model()}, {kh2::kEntryTextures, "tim_", texture_container(false)}});
    bool refused = false;
    try {
        (void)kh2::object_model(map_mdlx.data(), map_mdlx.size(), folder, "F_EX040_TEST", sink);
    } catch (const std::runtime_error&) {
        refused = true;
    }
    expect(refused, "a map's model is not an object");
}

void test_refusals() {
    auto refused = [](const Bytes& file, const char* name) {
        try {
            (void)kh2::read_map(file.data(), file.size());
        } catch (const std::runtime_error& e) {
            return std::string(e.what()).find(name) != std::string::npos;
        }
        return false;
    };
    expect(refused(
               bar({{kh2::kEntryModel, "BOB", object_model()}, {kh2::kEntryTextures, "BOB", texture_container(false)}}),
               "MAP"),
           "a map without its background refused");
    expect(refused(bar({{kh2::kEntryModel, "MAP", map_model()}}), "0x07 MAP"), "a model without textures refused");
    expect(refused(Bytes(32, 0), "BAR"), "not a BAR refused");
}

}  // namespace

int main() {
    try {
        test_dds();
        test_map();
        test_room(map_file());
        test_object();
        test_refusals();
        std::cout << "KH2 map test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "KH2 map test failed: " << e.what() << "\n";
        return 1;
    }
}
