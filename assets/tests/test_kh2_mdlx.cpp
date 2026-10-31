#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "khdays/assets/kh2_mdlx.h"

// A KH2 .mdlx made here, byte by byte: a BAR of a two-bone model whose packet
// blends a vertex from both bones, and its textures, one 8-bit picture sent to
// the GS as 32-bit pixels with its palette.

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
    void zeros(std::size_t n) {
        b.insert(b.end(), n, 0);
    }
    void pad(std::size_t align) {
        while (b.size() % align != 0) {
            u8(0);
        }
    }
    void set_u32(std::size_t at, std::uint32_t v) {
        for (std::size_t i = 0; i < 4; ++i) {
            b.at(at + i) = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
        }
    }
    void set_u64(std::size_t at, std::uint64_t v) {
        set_u32(at, static_cast<std::uint32_t>(v));
        set_u32(at + 4, static_cast<std::uint32_t>(v >> 32U));
    }
};

Bytes bar(const std::vector<std::pair<std::uint32_t, Bytes>>& entries) {
    Writer w;
    w.u32(0x01524142U);  // "BAR\x01"
    w.u32(static_cast<std::uint32_t>(entries.size()));
    w.zeros(8);
    const std::size_t table = w.size();
    w.zeros(entries.size() * 16U);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        w.pad(16);
        w.set_u32(table + i * 16U, entries[i].first);
        w.set_u32(table + i * 16U + 8, static_cast<std::uint32_t>(w.size()));
        w.set_u32(table + i * 16U + 12, static_cast<std::uint32_t>(entries[i].second.size()));
        w.b.insert(w.b.end(), entries[i].second.begin(), entries[i].second.end());
    }
    return w.b;
}

void vif(Writer& w, unsigned cmd, unsigned num, unsigned imm) {
    w.u32(imm | (num << 16U) | (cmd << 24U));
}

constexpr float kHalfTurn = 1.5707963F;

// Bone 0 at the origin; bone 1 under it, 100 up and turned a quarter about z.
// The packet's positions: 0-1 on bone 0, 2-3 on bone 1 (x y z pre-weighted,
// the weight in w). Weight group 0 (one bone): positions 0 and 2; group 1
// (two bones): 1 (0.25) with 3 (0.75). The strip: blended vertices 0 1 2.
Bytes model(bool weighted) {
    Writer w;
    w.zeros(0x90);
    const std::size_t h = w.size();
    w.u32(3);
    w.zeros(12);
    w.u16(2);  // bones
    w.u16(1);  // textures
    w.u32(0x40);
    w.u32(0);
    w.u32(1);  // subparts
    const std::size_t subpart = w.size();
    w.zeros(0x20);
    for (int i = 0; i < 2; ++i) {
        w.u32(static_cast<std::uint32_t>(i));
        w.u32(i == 0 ? 0xFFFFFFFFU : 0U);
        w.zeros(8);
        const float turn = i == 1 ? kHalfTurn : 0.0F, up = i == 1 ? 100.0F : 0.0F;
        for (const float f : {1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, turn, 0.0F, 0.0F, up, 0.0F, 1.0F}) {
            w.f32(f);
        }
    }
    // the packet's VU memory, quadword by quadword (all V4-32)
    Writer codes;
    vif(codes, 0x01, 0, 0x0101);  // STCYCL 1, 1
    vif(codes, 0x6C, 15, 0);
    for (const std::uint32_t word :
         {1U, 0U, 0U, 0U, 3U, 4U, 11U, 0U, 0U, 0U, weighted ? 2U : 0U, 12U, 4U, 7U, 13U, 2U}) {
        codes.u32(word);
    }
    for (const auto& [u, index, flag] : {std::array<unsigned, 3>{0, 0, 0x10}, {2048, 1, 0x10}, {4096, 2, 0x20}}) {
        codes.u32(u);
        codes.u32(4096U - u);
        codes.u32(index);
        codes.u32(flag);
    }
    for (const auto& p : {std::array<float, 4>{10, 0, 0, 1}, {0, 5, 0, 0.25F}, {10, 0, 0, 1}, {3, 0, 0, 0.75F}}) {
        for (const float f : p) {
            codes.f32(f);
        }
    }
    for (const std::uint32_t word : {2U, 2U, 0U, 0U, 2U, 1U, 0U, 0U, 0U, 2U, 0U, 0U, 1U, 3U, 0U, 0U}) {
        codes.u32(word);  // affiliation 11, group counts 12, indices 13 (group 0) and 14 (group 1)
    }
    const std::size_t chain = w.size();
    for (const auto& [bone, address] : {std::pair{1U, 24U}, std::pair{0U, 20U}}) {  // matrices, out of order
        w.u32(4U | (3U << 28U));
        w.u32(bone);
        vif(w, 0x01, 0, 0x0101);
        vif(w, 0x6C, 4, address);
    }
    while ((codes.size() - 8) % 16 != 0) {  // the codes' first 8 bytes go in the tag's upper half
        codes.u32(0);
    }
    w.u32(static_cast<std::uint32_t>((codes.size() - 8) / 16) | (1U << 28U));
    w.u32(0);
    w.b.insert(w.b.end(), codes.b.begin(), codes.b.end());
    w.u32(6U << 28U);  // ret
    w.zeros(12);
    w.set_u32(subpart, 2);  // attribute: translucent
    w.set_u32(subpart + 4, 0);
    w.set_u32(subpart + 0x10, static_cast<std::uint32_t>(chain - h));
    return w.b;
}

// A send packet: BITBLTBUF (DBP, DBW, DPSM), TRXREG (w, h), the pixels' address at 0x74.
void send(Writer& w, std::size_t at, unsigned dbp, unsigned dbw, unsigned width, unsigned height, std::size_t pixels) {
    w.set_u64(at + 0x20, (static_cast<std::uint64_t>(dbp) << 32U) | (static_cast<std::uint64_t>(dbw) << 48U));
    w.set_u64(at + 0x40, width | (static_cast<std::uint64_t>(height) << 32U));
    w.set_u32(at + 0x74, static_cast<std::uint32_t>(pixels));
}

// One 16 x 16 8-bit texture (TBP0 100, TBW 2, palette at CBP 200; or 4-bit),
// sent as an 8 x 8 PSMCT32 picture whose bytes count 0..255; the palette sent
// as 16 x 16 PSMCT32, colour k = (k, 255 - k, 7, 0x40).
Bytes textures(unsigned psm) {
    Writer w;
    w.u16(0);
    w.u16(0);
    w.u32(256);    // colours
    w.u32(1);      // pictures
    w.u32(1);      // GS infos
    w.u32(0x30);   // pictures' numbers
    w.u32(0x40);   // send packets
    w.u32(0x160);  // GS infos
    w.u32(0x200);  // pixels
    w.u32(0x300);  // palette
    w.zeros(0x700 - w.size());
    send(w, 0x40, 200, 1, 16, 16, 0x300);
    send(w, 0xD0, 100, 1, 8, 8, 0x200);
    const std::uint64_t tex0 =
        100U | (2U << 14U) | (static_cast<std::uint64_t>(psm) << 20U) | (4ULL << 26U) | (4ULL << 30U) | (200ULL << 37U);
    w.set_u64(0x160 + 112, tex0);
    for (unsigned k = 0; k < 256; ++k) {
        w.b[0x200 + k] = static_cast<std::uint8_t>(k);
        w.set_u32(0x300 + k * 4U, k | ((255U - k) << 8U) | (7U << 16U) | (0x40U << 24U));
    }
    return w.b;
}

// The textures above with a texture animation in their footer (TEXA, then _KN5): two 2 x 2 sprites drawn at
// (4, 6); slot ids 30..33, 30 its first animation (sprite 0 for 2 frames, then stop), 33 its second (sprite 1 for
// 4-8 frames, then back); the sprites' pixels palette indices 10-13 and 20-23.
Bytes textures_with_face() {
    Writer w;
    const Bytes base = textures(0x13);
    w.b = base;
    for (const char c : std::string("TEXA")) {
        w.u8(static_cast<unsigned>(c));
    }
    w.u32(96);
    const std::size_t t = w.size();
    w.u16(0);   // +0
    w.u16(0);   // target: GS info 0
    w.u16(0);   // layers
    w.u16(8);   // bits per pixel
    w.u16(30);  // first slot id
    w.u16(33);  // last
    w.u16(2);   // animations
    w.u16(2);   // sprites
    w.u16(4);   // x
    w.u16(6);   // y
    w.u16(2);   // width
    w.u16(2);   // height
    w.u32(40);  // slot table
    w.u32(48);  // animation table
    w.u32(88);  // sprites
    w.u32(0);   // default animation
    for (const unsigned s : {1U, 0U, 0U, 2U}) {
        w.u16(s);
    }
    w.u32(56);
    w.u32(72);
    for (const unsigned v : {0U, 2U, 2U, 0U, 3U, 0U, 0U, 0U}) {  // sprite 0 for 2 frames; stop
        w.u16(v);
    }
    for (const unsigned v : {0U, 4U, 8U, 1U, 2U | (0xFFFU << 4U), 0U, 0U, 0U}) {  // sprite 1 for 4-8; back 1
        w.u16(v);
    }
    for (const unsigned p : {10U, 11U, 12U, 13U, 20U, 21U, 22U, 23U}) {
        w.u8(p);
    }
    expect(w.size() == t + 96, "the TEXA chunk's size");
    for (const char c : std::string("_KN5")) {
        w.u8(static_cast<unsigned>(c));
    }
    w.u32(0);
    return w.b;
}

void test_face() {
    const Bytes file = bar({{0x04, model(true)}, {0x07, textures_with_face()}});
    const auto m = kh2::import_mdlx(file.data(), file.size(), "test");
    expect(m.textures.size() == 1 && m.texture_animations.size() == 1, "the texture and its animated part");
    const auto& a = m.texture_animations[0];
    expect(a.texture == "tex0" && a.x == 4 && a.y == 6 && a.width == 2 && a.height == 2, "drawn over tex0 at (4, 6)");
    expect(a.first_slot == 30 && a.slot_script == std::vector<int>{0, -1, -1, 1}, "slot 30 the first, 33 the second");
    using Kind = khdays::assets::ImportedTextureAnimation::Step::Kind;
    expect(a.scripts.size() == 2 && a.scripts[0].size() == 2 && a.scripts[0][1].kind == Kind::stop,
           "the first: a sprite, then stop");
    const auto& s = a.scripts[1];
    expect(s.size() == 2 && s[0].sprite == 1 && s[0].min_frames == 4 && s[0].max_frames == 8 &&
               s[1].kind == Kind::jump && s[1].jump == -1,
           "the second: sprite 1 for 4-8 frames, then back one");
    expect(a.sprites.size() == 2 && a.sprites[0].size() == 16U, "two 2 x 2 RGBA sprites");
    // colour k = (k, 255 - k, 7, 0x40) in the palette as the GS keeps it (CSM1): index 10 is entry 18, 20 is entry 12
    expect(a.sprites[0][0] == 18 && a.sprites[0][1] == 237 && a.sprites[0][3] == 0x80, "index 10 through the palette");
    expect(a.sprites[1][0] == 12, "index 20: entries 8-15 and 16-23 swapped");
}

void test_model() {
    const Bytes file = bar({{0x04, model(true)}, {0x07, textures(0x13)}});
    const auto m = kh2::import_mdlx(file.data(), file.size(), "test");
    expect(m.joints.size() == 2 && m.joints[0].name == "kh2_0" && m.joints[1].name == "kh2_1" &&
               m.joints[1].parent == 0,
           "the bones are the joints");
    expect(near(m.joints[1].world[13], 1.0F) && near(m.joints[1].world[1], 1.0F), "bone 1 at rest, in metres");
    expect(m.model.palette.size() == 3, "an identity, then a palette entry per joint");
    expect(m.model.meshes.size() == 1 && m.model.meshes[0].vertices.size() == 3, "one triangle");
    const auto& mesh = m.model.meshes[0];
    expect(mesh.texture_name == "tex0" && mesh.alpha_mode == khdays::assets::NeutralMesh::AlphaMode::blend,
           "its texture, translucent");
    const auto& v = mesh.vertices;
    // 10 x on bone 0; 10 x on bone 1 turned to y, 100 up; 0.25 of (0 20 0) on bone 0 with 0.75 of (4 0 0) on bone 1
    expect(near(v[0].position[0], 0.1F) && near(v[0].position[1], 0.0F), "a vertex on bone 0");
    expect(near(v[1].position[0], 0.0F) && near(v[1].position[1], 1.1F) && v[1].joints[0] == 2U,
           "a vertex on bone 1, turned");
    expect(near(v[2].position[0], 0.0F) && near(v[2].position[1], 0.83F), "a blended vertex at rest");
    expect(v[2].joints[0] == 2U && near(v[2].weights[0], 0.75F) && v[2].joints[1] == 1U && near(v[2].weights[1], 0.25F),
           "the blended vertex's bones, heaviest first");
    expect(near(v[1].texcoord[0], 8.0F) && near(v[1].texcoord[1], 8.0F), "texture coordinates in texels");

    expect(m.textures.size() == 1 && m.missing_textures.empty(), "the texture decoded");
    const auto& t = m.textures[0].second;
    expect(t.width == 16 && t.height == 16 && t.rgba.size() == 16U * 16U * 4U, "16 x 16");
    // red: where in the sent palette the pixel's colour was (k for colour k)
    const auto sent_at = [&t](int x, int y) { return t.rgba[static_cast<std::size_t>((y * 16 + x) * 4)]; };
    // The GS's PSMT8 in a PSMCT32 block (as a game texture decodes): (1, 0) is
    // byte 4 sent, (2, 0) byte 8, (0, 2) byte 17. CSM1 keeps entries 8-15 of
    // each 32 where 16-23 are sent, and 16-23 where 8-15 are.
    expect(sent_at(0, 0) == 0 && sent_at(1, 0) == 4, "8-bit pixels where the GS keeps them");
    expect(sent_at(2, 0) == 16 && sent_at(0, 2) == 9, "palette entries 8 and 17 from 16 and 9");
    expect(t.rgba[1] == 255 && t.rgba[2] == 7 && t.rgba[3] == 0x80, "the palette's colour, alpha 0x40 widened");
}

void test_weightless() {
    const Bytes file = bar({{0x04, model(false)}});
    const auto m = kh2::import_mdlx(file.data(), file.size(), "test");
    const auto& v = m.model.meshes.at(0).vertices;
    // without weight groups the strip indexes the positions themselves, each on its matrix's bone
    expect(near(v[1].position[1], 0.05F) && v[1].joints[0] == 1U && near(v[1].weights[0], 1.0F),
           "a vertex of one bone, its w not a weight");
    expect(near(v[2].position[1], 1.1F) && v[2].joints[0] == 2U, "position 2 on bone 1");
    expect(m.textures.empty() && m.model.meshes[0].texture_name.empty(), "no textures: drawn white");
}

void test_refusals() {
    const auto refused = [](const Bytes& file) {
        try {
            (void)kh2::import_mdlx(file.data(), file.size(), "test");
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };
    expect(refused(Bytes{1, 2, 3}), "not a BAR");
    expect(refused(bar({{0x07, textures(0x13)}})), "no model");
    const Bytes four_bit = bar({{0x04, model(true)}, {0x07, textures(0x14)}});
    const auto m = kh2::import_mdlx(four_bit.data(), four_bit.size(), "test");
    expect(m.textures.empty() && m.missing_textures.size() == 1, "a texture it cannot decode is reported");
}

}  // namespace

int main() {
    try {
        test_model();
        test_face();
        test_weightless();
        test_refusals();
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "kh2 mdlx: ok\n";
    return 0;
}
