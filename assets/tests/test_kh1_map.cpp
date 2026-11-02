#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/kh1_map.h"

// A KINGDOM HEARTS FINAL MIX room made here byte by byte as the reader expects it (docs/FORMATS.md, "KINGDOM HEARTS
// FINAL MIX's rooms"): an .mvbl of two textures (one in the picture buffer's spare bits and a plain page, named the
// other way round) and one mesh, a strip of two triangles on the first; seventeen plain pages, the last unnumbered; a
// .bin's collision entry; a .bin's entry table and an .img's header; a .gvbl, a sky of two groups.

namespace {

using Bytes = std::vector<std::uint8_t>;
namespace kh1 = khdays::assets::kh1;

void expect(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error(what);
}

struct Writer final {
    Bytes b;
    void u8(unsigned v) { b.push_back(static_cast<std::uint8_t>(v)); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            u8((v >> (8 * i)) & 0xFF);
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void f32(float v) {
        std::uint32_t w;
        std::memcpy(&w, &v, 4);
        u32(w);
    }
    void zeros(std::size_t n) { b.insert(b.end(), n, 0); }
};

// TEX0 of a page at `page` of kind `kind`; CLAMP region-clamped (2) or repeated (3) both ways.
void texture(Writer& w, std::uint32_t page, unsigned kind, unsigned wrap) {
    w.u64(page | (static_cast<std::uint64_t>(kind) << 20));
    w.u64(wrap | (wrap << 2));
}

struct Tex final {
    std::uint32_t page;
    unsigned kind, wrap;
};

// An .mvbl of `textures` and one mesh, a strip of two triangles, on texture `on`.
Bytes mvbl(const std::vector<Tex>& textures, std::size_t on) {
    Writer w;
    w.u32(0x9696);
    w.u32(1);  // meshes
    w.u32(static_cast<std::uint32_t>(textures.size()));
    w.u32(0);
    for (const auto& t : textures)
        texture(w, t.page, t.kind, t.wrap);
    w.zeros(0x80);  // the mesh's box
    w.u32(0x11111111);
    w.u32(0x22222222);
    w.u32(0x33333333);
    w.u32(0x44444444);
    const std::size_t table = w.b.size();
    w.u32(0x30000000);
    w.u32(static_cast<std::uint32_t>(table + 16));
    w.zeros(8);
    // the mesh
    w.u32(0x01234567);
    w.u32(1);
    w.zeros(8);
    w.u32(572);
    w.u32(4);
    w.zeros(8);
    texture(w, textures[on].page, textures[on].kind, textures[on].wrap);
    const std::array<std::array<float, 3>, 4> at{{{0, 0, 0}, {100, 0, 0}, {0, -50, 0}, {100, -50, 200}}};
    const std::array<std::uint32_t, 4> ends{0x1000, 0x1000, 1, 2};
    for (std::size_t k = 0; k < 4; ++k) {
        w.f32(at[k][0]);
        w.f32(at[k][1]);
        w.f32(at[k][2]);
        w.u32(0x80404020U);  // R 0x20 G 0x40 B 0x40 A 0x80
        w.f32(static_cast<float>(k) * 2);
        w.f32(0.5F);
        w.u32(ends[k]);
        w.u32(0);
    }
    return w.b;
}

Bytes plain_dds() {
    Writer w;
    for (const char c : std::string("DDS "))
        w.u8(static_cast<unsigned char>(c));
    w.u32(124);
    w.u32(0);
    w.u32(1);  // height
    w.u32(1);  // width
    w.zeros(76 - w.b.size());
    w.u32(32);
    w.u32(0x41);
    w.u32(0);
    w.u32(32);
    w.u32(0x00FF0000U);
    w.u32(0x0000FF00U);
    w.u32(0x000000FFU);
    w.u32(0xFF000000U);
    w.zeros(128 - w.b.size());
    w.u32(0xFFFFFFFFU);
    return w.b;
}

void test_room() {
    // in the picture buffer's spare bits (named after the plain pages), a plain page; the mesh on the first
    const Bytes data = mvbl({{0x0, 0x1b, 3}, {0x2600, 0x13, 2}}, 0);
    const auto folder = std::filesystem::temp_directory_path() / "khdays_kh1_map_test";
    std::filesystem::create_directories(folder);
    const auto picture = folder / "-1f900.dds";
    {
        const Bytes dds = plain_dds();
        std::ofstream(picture, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()),
                                                      static_cast<std::streamsize>(dds.size()));
    }
    std::map<std::string, int> written;
    std::vector<std::string> asked;
    const auto made = kh1::room_model(
        data.data(), data.size(), "tw99_01", kh1::ImgPages{0xf900, 2},
        [&](const std::string& name) {
            asked.push_back(name);
            return name == "1f900" ? picture : std::filesystem::path();
        },
        [&](const std::string& file, const khdays::assets::DecodedTexture& t) { written[file] = t.width; });
    // the plain page first (f900), the spare bits' page after it (1f900): the mesh's texture is the second
    expect(asked == std::vector<std::string>{"1f900"}, "its texture named by its page's place: " +
                                                           (asked.empty() ? std::string("-") : asked.front()));
    expect(made.report.empty(), "nothing left out: " + (made.report.empty() ? std::string() : made.report.front()));
    expect(written.size() == 1 && written["1f900.png"] == 1, "its picture written once");
    const auto& scene = made.scene;
    expect(scene.materials.size() == 1 && scene.materials[0].texture == "1f900.png" &&
               scene.materials[0].repeat == std::array<bool, 2>{true, true} && scene.materials[0].double_sided,
           "one material, its picture, repeated, both sides");
    const auto& p = scene.meshes.at(0).primitives.at(0);
    // two triangles: the strip's third vertex ends the first, the fourth (odd) the second turned
    expect(p.indices == std::vector<std::uint32_t>{0, 1, 2, 2, 1, 3}, "the strip's triangles");
    expect(std::fabs(p.positions[2][1] - 50) < 1e-4F && std::fabs(p.positions[3][2] + 200) < 1e-4F,
           "turned upright: y and z negated");
    expect(p.colors[0] == std::array<std::uint8_t, 4>{0x40, 0x80, 0x80, 0xFF}, "colours doubled from the PS2's");
    expect(std::fabs(p.uvs[3][0] - 6) < 1e-4F, "uv as written");
    // not an .mvbl
    bool threw = false;
    try {
        const Bytes junk(64, 0);
        (void)kh1::room_model(junk.data(), junk.size(), "x", {}, {}, {});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a file that is no .mvbl refused");
    std::filesystem::remove_all(folder);
}

void test_seventeenth_plain_page() {
    // seventeen plain pages: the remaster numbers sixteen; the seventeenth's picture is the one without a number
    std::vector<Tex> textures;
    for (std::uint32_t page = 0; page < 17; ++page)
        textures.push_back({0x2600 + page * 0x100, 0x13, 2});
    const Bytes data = mvbl(textures, 16);
    std::vector<std::string> asked;
    (void)kh1::room_model(
        data.data(), data.size(), "dc99_01", kh1::ImgPages{0x13100, 17},
        [&](const std::string& name) {
            asked.push_back(name);
            return std::filesystem::path();
        },
        {});
    expect(asked == std::vector<std::string>{"_NoName"}, "the seventeenth plain page's picture: " +
                                                             (asked.empty() ? std::string("-") : asked.front()));
}

// A .bin whose one entry is a collision: a triangle and a four-sided face on four vertices, y down.
void test_collision() {
    Writer w;
    w.u32(1);
    w.zeros(12);
    w.u32(0x20);  // the entry
    const std::size_t length_at = w.b.size();
    w.u32(0);
    w.zeros(0x20 - w.b.size());
    const std::size_t entry = w.b.size();
    // its sections: none, the vertices (4 x 12), none, the faces (2 x 20), four none
    const std::array<std::uint32_t, 8> lengths{0, 48, 0, 40, 0, 0, 0, 0};
    std::uint32_t at = 0x48;
    for (const auto length : lengths) {
        w.u32(at);
        w.u32(length);
        at += length;
    }
    w.u32(at);
    w.u32(0);
    for (const auto& v : {std::array<float, 3>{0, 0, 0}, {100, 0, 0}, {100, 0, 100}, {0, -50, 100}}) {
        w.f32(v[0]);
        w.f32(v[1]);
        w.f32(v[2]);
    }
    for (const auto& c : {std::array<int, 4>{0, 1, 2, -1}, {0, 1, 2, 3}}) {
        for (const int i : c) {
            w.u8(static_cast<unsigned>(i) & 0xFF);
            w.u8((static_cast<unsigned>(i) >> 8) & 0xFF);
        }
        w.zeros(12);
    }
    const auto length = static_cast<std::uint32_t>(w.b.size() - entry);
    for (int i = 0; i < 4; ++i)
        w.b[length_at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(length >> (8 * i));
    const auto scene = kh1::collision_scene(w.b.data(), w.b.size(), "tw99_01");
    const auto& p = scene.meshes.at(0).primitives.at(0);
    expect(p.indices.size() == 9, "a triangle and a four-sided face as two: " + std::to_string(p.indices.size() / 3));
    expect(std::fabs(p.positions[8][1] - 50) < 1e-4F && std::fabs(p.positions[8][2] + 100) < 1e-4F,
           "turned upright as the model");
    bool threw = false;
    try {
        Writer bin;  // no entries
        bin.u32(0);
        bin.zeros(12);
        (void)kh1::collision_scene(bin.b.data(), bin.b.size(), "x");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a .bin with no collision refused");
}

void test_bin_and_img() {
    Writer bin;
    bin.u32(2);
    bin.zeros(12);
    bin.u32(0x20);
    bin.u32(4);
    bin.u32(0x24);
    bin.u32(8);
    bin.zeros(0x2c - bin.b.size());
    const auto entries = kh1::bin_entries(bin.b.data(), bin.b.size());
    expect(entries.size() == 2 && entries[1].offset == 0x24 && entries[1].size == 8, "the .bin's entries");
    bin.b[0x1c] = 0x40;  // the second entry past the end
    bool threw = false;
    try {
        (void)kh1::bin_entries(bin.b.data(), bin.b.size());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "an entry past the end refused");
    Writer img;
    for (const std::uint32_t v : {0x40U, 0U, 0x40U, 0xc0U, 0x100U, 0xf800U, 0xf900U, 0x130000U})
        img.u32(v);
    const auto pages = kh1::img_pages(img.b.data(), img.b.size());
    expect(pages.at == 0xf900 && pages.count == 0x13, "the .img's pages");
}

// A .gvbl of two groups on one picture: a strip of two triangles, then one triangle fading by its vertices' alpha.
void test_sky() {
    Writer w;
    w.u32(0x4989);
    w.zeros(12);
    w.u32(0x44444444);
    w.u32(2);  // groups
    w.zeros(8);
    const std::array<std::uint32_t, 2> counts{4, 3};
    for (std::size_t g = 0; g < 2; ++g) {
        w.u32(g == 0 ? 0x25c : 0x21c);  // both kinds of group (Deep Jungle's tz00_08 has a 0x21c)
        w.u32(counts[g]);
        w.zeros(8);
        w.u32(0x1e2a50);  // its picture's offset
        w.u32(0);
        w.u32(g == 0 ? 0 : 5);
        w.u32(0);
        for (std::uint32_t k = 0; k < counts[g]; ++k) {
            w.f32(static_cast<float>(k) * 1000);
            w.f32(-6000);  // y down: above
            w.f32(static_cast<float>(k % 2) * 1000);
            w.u32(g == 0 ? 0x80808080U : 0x20808080U);
            w.f32(0);
            w.f32(1);
            w.u32(k < 2 ? 0x1000 : 1);
            w.u32(0);
        }
    }
    const auto folder = std::filesystem::temp_directory_path() / "khdays_kh1_sky_test";
    std::filesystem::create_directories(folder);
    const auto picture = folder / "-1e2a50.dds";
    {
        const Bytes dds = plain_dds();
        std::ofstream(picture, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()),
                                                      static_cast<std::streamsize>(dds.size()));
    }
    kh1::Room room;
    room.scene.name = "tw99_02";
    room.scene.nodes.push_back({"tw99_02", -1});
    std::vector<std::string> asked;
    std::map<std::string, int> written;
    const auto find = [&](const std::string& name) {
        asked.push_back(name);
        return name == "1e2a50" ? picture : std::filesystem::path();
    };
    const auto write = [&](const std::string& file, const khdays::assets::DecodedTexture& t) { written[file] = t.width; };
    kh1::add_sky(&room, w.b.data(), w.b.size(), find, write);
    kh1::add_sky(&room, w.b.data(), w.b.size(), find, write);  // a second sky on the same picture
    const auto& scene = room.scene;
    expect(room.report.empty(), "nothing left out: " + (room.report.empty() ? std::string() : room.report.front()));
    expect(scene.nodes.size() == 2 && scene.nodes[1].sky && scene.nodes[1].parent == 0, "one sky node, under the root");
    expect(asked == std::vector<std::string>{"1e2a50"} && written.size() == 1 && written["sky_1e2a50.png"] == 1,
           "its picture asked and written once");
    expect(scene.materials.size() == 1 && scene.materials[0].texture == "sky_1e2a50.png" &&
               scene.materials[0].alpha_mode == khdays::assets::NeutralMesh::AlphaMode::blend,
           "one blended material");
    expect(scene.meshes.size() == 4 && scene.meshes[1].node == 1, "a mesh a group, on the sky node");
    const auto& strip = scene.meshes[0].primitives.at(0);
    const auto& fading = scene.meshes[1].primitives.at(0);
    expect(strip.indices.size() == 6 && fading.indices.size() == 3, "the groups' triangles, each after the last");
    expect(std::fabs(strip.positions[0][1] - 6000) < 1e-4F, "turned upright: above");
    expect(fading.colors[0][3] == 0x40, "its vertices' alpha kept");
    bool threw = false;
    try {
        const Bytes junk(64, 0);
        kh1::add_sky(&room, junk.data(), junk.size(), find, write);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a file that is no .gvbl refused");
    std::filesystem::remove_all(folder);
}

}  // namespace

int main() {
    try {
        test_room();
        test_seventeenth_plain_page();
        test_collision();
        test_bin_and_img();
        test_sky();
        std::cout << "KH1 map test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "KH1 map test failed: " << e.what() << "\n";
        return 1;
    }
}
