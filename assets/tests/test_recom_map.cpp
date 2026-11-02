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
// UNPACK of a strip of two triangles on its one texture; a .VTM's list.

namespace {

using Bytes = std::vector<std::uint8_t>;
namespace recom = khdays::assets::recom;

void expect(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error(what);
}

struct Writer final {
    Bytes b;
    void u8(unsigned v) { b.push_back(static_cast<std::uint8_t>(v)); }
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
    void zeros(std::size_t n) { b.insert(b.end(), n, 0); }
    void pad_to(std::size_t at) { zeros(at - b.size()); }
};

Bytes azf() {
    Writer w;
    w.u32(8);     // the table: two entries, the layout and the piece
    w.u32(0x10);  // the piece's offset, set below
    // the layout at 8
    const std::size_t layout = w.b.size();
    w.u16(1);
    w.pad_to(layout + 0x20);
    w.u32(1);         // the piece
    w.u16(0xFFFF);    // no parent
    w.u16(0);
    w.u16(0);         // turn x
    w.u16(900);       // turn y: a quarter
    w.u16(0);         // turn z
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
        w.f32(0.5F);
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
    bool threw = false;
    try {
        const Bytes junk(16, 0);
        (void)recom::room_model(junk.data(), junk.size(), "x", {}, {});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a file that is no .AZF refused");
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
}

}  // namespace

int main() {
    try {
        test_room();
        test_picture_list();
        std::cout << "Re:CoM map test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Re:CoM map test failed: " << e.what() << "\n";
        return 1;
    }
}
