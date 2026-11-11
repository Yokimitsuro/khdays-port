#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/recom_map.h"

// A Re:Chain of Memories room made here byte by byte as the reader expects it (docs/FORMATS.md, "Re:Chain of
// Memories' rooms"): an .AZF of one piece placed by its layout (turned a quarter about y and moved), its stream one
// UNPACK of a strip of two triangles on its one texture; a .VTM's list; an .mcd of one piece.

namespace {

using Bytes = std::vector<std::uint8_t>;
namespace recom = khdays::assets::recom;

void expect(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error(what);
}

struct Writer final {
    Bytes b;
    void u8(unsigned v) {
        b.push_back(static_cast<std::uint8_t>(v));
    }
    void u16(unsigned v) {
        u8(v & 0xFF);
        u8((v >> 8) & 0xFF);
    }
    void u32(std::uint32_t v) {
        u16(v & 0xFFFF);
        u16(v >> 16);
    }
    void f32(float v) {
        std::uint32_t w;
        std::memcpy(&w, &v, 4);
        u32(w);
    }
    void text(const char* s, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            u8(i < std::strlen(s) ? static_cast<unsigned char>(s[i]) : 0);
    }
    void zeros(std::size_t n) {
        b.insert(b.end(), n, 0);
    }
    void pad_to(std::size_t at) {
        zeros(at - b.size());
    }
};

Bytes azf() {
    Writer w;
    w.u32(8);     // the table: two entries, the layout and the piece
    w.u32(0x10);  // the piece's offset, set below
    // the layout at 8
    const std::size_t layout = w.b.size();
    w.u16(1);
    w.pad_to(layout + 0x20);
    w.u32(1);       // the piece
    w.u16(0xFFFF);  // no parent
    w.u16(0);
    w.u16(0);    // turn x
    w.u16(900);  // turn y: a quarter
    w.u16(0);    // turn z
    w.u16(0);
    w.f32(10);
    w.f32(0);
    w.f32(0);
    w.f32(1);
    for (int i = 0; i < 4; ++i)
        w.f32(1);  // scale
    w.zeros(16);
    // the piece
    const std::size_t piece = w.b.size();
    for (int i = 0; i < 4; ++i)
        w.b[4 + i] = static_cast<std::uint8_t>(piece >> (8 * i));
    for (const std::uint32_t v : {0x03000001U, 0x30U, 0x50U, 1U, 0x90U, 0xB0U, 0U, 0U})
        w.u32(v);
    w.pad_to(piece + 0x30);
    w.text("ROOT", 16);
    w.zeros(0x10);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            w.f32(c == r ? 1.0F : 0.0F);
    w.text("tex_a", 32);
    // the stream: a DMA tag, STCYCL, UNPACK V4-32 of 2 + 4 x 4 qwords
    const unsigned qwords = 2 + 4 * 4;
    w.u32(0x10000000U | qwords);
    w.u32(0);
    w.u32(0x01000404U);
    w.u32(0x6C000000U | (qwords << 16));
    w.u32(qwords);
    w.u16(4);  // vertices
    w.u16(1);
    w.zeros(8);
    w.zeros(16);  // the GIF tag
    const std::array<std::array<float, 3>, 4> at{{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}}};
    const std::array<std::uint32_t, 4> kind{0xFFFF, 0xFFFF, 0, 0x20};
    for (std::size_t v = 0; v < 4; ++v) {
        w.u32(kind[v]);
        w.f32(1);
        w.u32(0);
        w.u32(0x2000);
        w.f32(at[v][0]);
        w.f32(at[v][1]);
        w.f32(at[v][2]);
        w.f32(0);
        for (const std::uint32_t c : {0x40U, 0x80U, 0x80U, 0x80U})
            w.u32(c);
        w.f32(static_cast<float>(v));
        w.f32(0.25F);
        w.f32(1);
        w.u32(0);  // its texture
    }
    w.u32(0x17000000U);  // MSCNT
    w.u32(0x60000000U);  // the DMA's return
    return w.b;
}

void test_room() {
    const Bytes data = azf();
    std::vector<std::string> asked;
    std::map<std::string, int> written;
    const auto made = recom::room_model(
        data.data(), data.size(), "MP990001",
        [&](const std::string& name) -> std::optional<khdays::assets::DecodedTexture> {
            asked.push_back(name);
            khdays::assets::DecodedTexture t;
            t.width = t.height = 1;
            t.rgba = {255, 255, 255, 255};
            return t;
        },
        [&](const std::string& file, const khdays::assets::DecodedTexture& t) { written[file] = t.width; });
    expect(asked == std::vector<std::string>{"tex_a"}, "its texture asked by name");
    expect(made.report.empty(), "nothing left out: " + (made.report.empty() ? std::string() : made.report.front()));
    expect(written.size() == 1 && written.count("tex_a.png") == 1, "its picture written once");
    const auto& p = made.scene.meshes.at(0).primitives.at(0);
    expect(p.indices.size() == 6, "two triangles: " + std::to_string(p.indices.size() / 3));
    // the second triangle (odd) turned: vertices 2, 1, 3; vertex 1 (1, 0, 0) a quarter about y is (0, 0, -1), moved
    // by 10 on x, then upright (x, -y, -z): (10, 0, 1)
    expect(std::fabs(p.positions[4][0] - 10) < 1e-4F && std::fabs(p.positions[4][2] - 1) < 1e-4F,
           "placed by its layout and turned upright");
    expect(p.colors[0] == std::array<std::uint8_t, 4>{0x80, 0xFF, 0xFF, 0xFF}, "colours doubled from the PS2's");
    expect(std::fabs(p.uvs[2][0] - 2) < 1e-4F, "s over q");
    expect(std::fabs(p.uvs[2][1] - 0.75F) < 1e-4F, "t from the picture's foot: glTF's v is 1 - t");
    bool threw = false;
    try {
        const Bytes junk(16, 0);
        (void)recom::room_model(junk.data(), junk.size(), "x", {}, {});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a file that is no .AZF refused");
}

// An .mcd of one piece moved by 10 on x: a floor quad and a triangle on its plane (y down; its normal +y: into the
// floor, as the .mcd's point).
Bytes mcd() {
    Writer w;
    w.text("@MCD", 4);
    w.u32(3);
    w.u16(1);  // pieces
    w.u16(1);
    w.zeros(0x4c - w.b.size());
    w.u32(0x50);  // the pieces' section: the pieces at 0x60
    w.zeros(0x60 - w.b.size());
    w.u32(0);
    w.u32(1);
    w.u32(0);       // its number
    w.u32(0);       // its mesh: right after the pieces
    w.zeros(0x30);  // move, turn, scale (not read)
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            w.f32(c == r ? 1.0F : (c == 3 && r == 0 ? 10.0F : 0.0F));
    w.zeros(0x40);  // its inverse
    const std::size_t mesh = w.b.size();
    w.zeros(0x20);  // its grid
    for (const std::uint32_t at : {0x40U, 0x40U, 0x50U, 0x90U, 0x90U, 0x90U, 0xc0U, 0xc0U})
        w.u32(at);
    w.zeros(mesh + 0x40 - w.b.size());
    w.f32(0);
    w.f32(1);
    w.f32(0);
    w.f32(0);  // its plane
    for (const auto& v : {std::array<float, 3>{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}) {
        w.f32(v[0]);
        w.f32(v[1]);
        w.f32(v[2]);
        w.f32(1);
    }
    for (const auto& c : {std::array<unsigned, 4>{0, 1, 2, 3}, {0, 1, 2, 2}}) {
        w.u16(0);
        w.u16(0x80);
        w.u16(0);  // its plane
        w.zeros(8);
        for (const unsigned i : c)
            w.u16(i);
        w.u16(1);
    }
    return w.b;
}

void test_collision() {
    const Bytes data = mcd();
    const auto scene = recom::collision_scene(data.data(), data.size(), "MP990001");
    const auto& p = scene.meshes.at(0).primitives.at(0);
    expect(p.indices.size() == 9, "a quad and a triangle: " + std::to_string(p.indices.size() / 3));
    for (std::size_t i = 0; i < p.positions.size(); i += 3) {
        const auto& a = p.positions[i];
        const auto& b = p.positions[i + 1];
        const auto& c = p.positions[i + 2];
        const float up = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
        expect(up > 0, "triangle " + std::to_string(i / 3) + " facing up, upright");
        for (const auto& q : {a, b, c})
            expect(q[0] >= 10 - 1e-4F && q[0] <= 11 + 1e-4F && q[2] <= 1e-4F, "moved by its piece, z turned");
    }
    bool threw = false;
    try {
        const Bytes junk(0x60, 0);
        (void)recom::collision_scene(junk.data(), junk.size(), "x");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a file that is no .mcd refused");
}

void test_picture_list() {
    Writer w;
    w.u32(0x40);
    w.text("gai_a_00.tm2", 24);
    w.u32(0x80004440U);
    w.u32(0x1000);
    w.text("kab_a_01.tm2", 24);
    w.u32(0);
    const auto list = recom::picture_list(w.b.data(), w.b.size());
    expect(list.size() == 2 && list[0].name == "gai_a_00" && list[0].offset == 0x40 && list[1].name == "kab_a_01" &&
               list[1].offset == 0x1000,
           "a .VTM's pictures by name");

    // the names first (MP020201's): 16 bytes, the offset at +0x10, two words, the size
    Writer n;
    n.text("al01_zka08.tm2", 16);
    for (const std::uint32_t v : {0x40U, 0U, 0U, 0x480U})
        n.u32(v);
    n.text("al01_zka07.tm2", 16);
    for (const std::uint32_t v : {0x4c0U, 0U, 0U, 0x480U})
        n.u32(v);
    const auto named = recom::picture_list(n.b.data(), n.b.size());
    expect(named.size() == 2 && named[0].name == "al01_zka08" && named[0].offset == 0x40 &&
               named[1].name == "al01_zka07" && named[1].offset == 0x4c0,
           "a .VTM whose names come first");
}

}  // namespace

int main() {
    try {
        test_room();
        test_picture_list();
        test_collision();
        std::cout << "Re:CoM map test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Re:CoM map test failed: " << e.what() << "\n";
        return 1;
    }
}
