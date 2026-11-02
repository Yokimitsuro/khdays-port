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

#include "khdays/assets/ddd_map.h"

// A Dream Drop Distance room made here byte by byte as the reader expects it (docs/FORMATS.md, "Dream Drop
// Distance's rooms"): a PMP placing one PMO, whose section's first group holds a record of triangles and a strip
// stitched by a degenerate triangle and its second group a blended triangle, its texture found as a plain 32-bit DDS;
// and a BCD of a triangle and a four-sided face.

namespace {

using Bytes = std::vector<std::uint8_t>;
namespace ddd = khdays::assets::ddd;

void expect(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error(what);
}

struct Writer final {
    Bytes b;
    void u8(unsigned v) { b.push_back(static_cast<std::uint8_t>(v)); }
    void u16(unsigned v) {
        u8(v & 0xFF);
        u8(v >> 8);
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
    void at(std::size_t offset, std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    }
};

// A vertex: uv (x 2048), colour, position (x 32768).
void vertex(Writer& w, float u, float v, float x, float y, float z) {
    w.u16(static_cast<unsigned>(u * 2048));
    w.u16(static_cast<unsigned>(v * 2048));
    w.u32(0xFF804020U);
    w.f32(x * 32768);
    w.f32(y * 32768);
    w.f32(z * 32768);
}

Bytes room() {
    Writer w;
    w.text("PMP", 4);
    w.zeros(0x0c);
    w.u16(1);  // objects
    w.u16(1);  // models
    w.zeros(6);
    w.u16(1);  // textures
    const std::size_t texture_list_field = w.b.size();
    w.u32(0);
    // the object: moved by (10, 0, 0), not turned, scale 1
    w.f32(10);
    w.f32(0);
    w.f32(0);
    w.zeros(12);
    w.f32(1);
    w.f32(1);
    w.f32(1);
    const std::size_t pmo_field = w.b.size();
    w.u32(0);
    w.zeros(8);
    // the PMO
    const std::size_t pmo = w.b.size();
    w.at(pmo_field, static_cast<std::uint32_t>(pmo));
    w.text("PMO", 4);
    w.u8(1);
    w.u8(1);
    w.u8(4);
    w.u8(0);
    w.u8(1);  // its textures
    w.zeros(3);
    w.u32(0);     // no skeleton
    w.u32(0xc0);  // first pass
    w.u16(4);     // triangles
    w.u16(9);     // vertices
    w.f32(1);
    w.u32(0);  // no second pass
    w.zeros(0xa0 - (w.b.size() - pmo));
    w.u32(0);
    w.text("tex_a", 12);
    w.zeros(16);
    // the section: the first group's 2 records at 0x100 (9 vertices), the second's 1 at 0x148 (3), 12 vertices at 0x180
    w.u32(0x100);
    w.u32(0x148);
    w.u32(9);
    w.u32(3);
    w.u32(2);
    w.u32(1);
    w.u32(0x180);
    w.u32(12 * 20);
    // a record: count, texture, vertex size, flags, then its group, its drawing (u16 at +0xa), its colour, 8 more
    auto record = [&](unsigned count, std::uint32_t flags, unsigned drawing) {
        w.u16(count);
        w.u8(0);
        w.u8(20);
        w.u32(flags);
        w.u16(0);
        w.u16(drawing);
        w.u32(0xFF808080U);
        w.zeros(8);
    };
    w.zeros(0x100 - (w.b.size() - pmo));
    record(3, 0x32000182U, 0);       // three vertices: a triangle
    record(6, 0x42000182U, 1);       // six: a strip a, b, c, c, d, e -- one triangle, two degenerate, then d's
    w.zeros(24);                     // the group's end
    record(3, 0x32000182U, 0x20);    // the second group's: blended
    w.zeros(0x180 - (w.b.size() - pmo));
    vertex(w, 0, 0, 0, 0, 0);
    vertex(w, 1, 0, 1, 0, 0);
    vertex(w, 0, 1, 0, 0, 1);
    vertex(w, 0, 0, 0, 1, 0);
    vertex(w, 1, 0, 1, 1, 0);
    vertex(w, 0, 1, 0, 1, 1);
    vertex(w, 0, 1, 0, 1, 1);
    vertex(w, 1, 1, 1, 1, 1);
    vertex(w, 0.5F, 0.5F, 2, 1, 1);
    vertex(w, 0, 0, 0, 2, 0);
    vertex(w, 1, 0, 1, 2, 0);
    vertex(w, 0, 1, 0, 2, 1);
    // the PMP's texture list
    w.at(texture_list_field, static_cast<std::uint32_t>(w.b.size()));
    w.u32(0);
    w.text("tex_a", 12);
    w.zeros(16);
    return w.b;
}

// A BCD: one part of four vertices, a triangle and a four-sided face.
Bytes collision() {
    Writer w;
    w.text("@bcd", 4);
    w.u32(0x0a);
    w.u32(1);
    w.u32(0x10);
    for (int i = 0; i < 6; ++i)
        w.f32(i < 3 ? 0.0F : 1.0F);  // its box
    w.u16(4);
    w.u16(2);
    w.u32(0x40);
    w.u32(0x80);
    w.zeros(0x40 - w.b.size());
    for (const auto& v : {std::array<float, 3>{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}) {
        w.f32(v[0]);
        w.f32(v[1]);
        w.f32(v[2]);
        w.f32(1);
    }
    // the triangle's corners in the order facing down, its plane up: turned up
    for (const auto& corners : {std::array<int, 4>{0, 1, 2, -1}, {0, 3, 2, 1}}) {
        w.f32(0);
        w.f32(1);
        w.f32(0);
        w.f32(0);  // its plane
        for (int c : corners)
            w.u16(static_cast<unsigned>(c) & 0xFFFF);
        w.zeros(24);
    }
    return w.b;
}

Bytes plain_dds() {
    Writer w;
    w.text("DDS ", 4);
    w.u32(124);
    w.u32(0);
    w.u32(1);  // height
    w.u32(2);  // width
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
    for (int i = 0; i < 8; ++i)
        w.u8(i < 4 ? 200 : 100);
    return w.b;
}

void test_room() {
    const Bytes pmp = room();
    expect(ddd::texture_names(pmp.data(), pmp.size()) == std::vector<std::string>{"tex_a"}, "the PMP's texture list");
    const auto folder = std::filesystem::temp_directory_path() / "khdays_ddd_map_test";
    std::filesystem::create_directories(folder);
    const auto picture = folder / "tex_a.dds";
    {
        const Bytes dds = plain_dds();
        std::ofstream(picture, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()),
                                                      static_cast<std::streamsize>(dds.size()));
    }
    std::map<std::string, int> written;
    const auto made = ddd::room_model(
        pmp.data(), pmp.size(), "eh_99",
        [&](const std::string& name) { return name == "tex_a" ? picture : std::filesystem::path(); },
        [&](const std::string& file, const khdays::assets::DecodedTexture& t) { written[file] = t.width; });
    expect(made.report.empty(), "nothing left out: " + (made.report.empty() ? std::string() : made.report.front()));
    expect(written.size() == 1 && written["tex_a.png"] == 2, "its picture written once");
    const auto& scene = made.scene;
    expect(scene.materials.size() == 2 && scene.materials[0].texture == "tex_a.png" &&
               scene.materials[1].texture == "tex_a.png",
           "two materials of its picture");
    expect(scene.materials[0].alpha_mode == khdays::assets::NeutralMesh::AlphaMode::mask &&
               scene.materials[1].alpha_mode == khdays::assets::NeutralMesh::AlphaMode::blend,
           "cut out by its picture's alpha; the second group's record blended");
    expect(scene.meshes.size() == 2 && scene.meshes[0].primitives.size() == 1, "two meshes");
    const auto& p = scene.meshes[0].primitives[0];
    // the triangle, then the strip's first and last real triangles (two stitched ones left out): 3 of them
    expect(p.indices.size() == 9, "triangles " + std::to_string(p.indices.size() / 3));
    expect(p.positions.size() == 9 && std::fabs(p.positions[1][0] - 11) < 1e-4F, "placed: moved by the object");
    expect(std::fabs(p.uvs[1][0] - 1) < 1e-4F && p.colors[0][0] == 0x20, "uv and colour as written");
    const auto& blended = scene.meshes[1].primitives[0];
    expect(blended.indices.size() == 3 && std::fabs(blended.positions[0][1] - 2) < 1e-4F,
           "the second group's vertices after the first's");
    // not a PMP
    bool threw = false;
    try {
        const Bytes junk(64, 0);
        (void)ddd::room_model(junk.data(), junk.size(), "x", {}, {});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a file that is no PMP refused");
    std::filesystem::remove_all(folder);
}

void test_collision() {
    Bytes bcd = collision();
    const auto scene = ddd::collision_scene(bcd.data(), bcd.size(), "eh_99");
    expect(scene.meshes.size() == 1, "one collision mesh");
    const auto& p = scene.meshes[0].primitives[0];
    expect(p.indices.size() == 9, "a triangle and a four-sided face as two: " + std::to_string(p.indices.size() / 3));
    for (std::size_t i = 0; i < p.indices.size(); i += 3) {
        const auto& a = p.positions[i];
        const auto& b = p.positions[i + 1];
        const auto& c = p.positions[i + 2];
        const float up = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
        expect(up > 0, "triangle " + std::to_string(i / 3) + " turned up as its plane");
    }
    bcd[0x80 + 48 + 0x10] = 9;  // a corner past the vertices
    bool threw = false;
    try {
        (void)ddd::collision_scene(bcd.data(), bcd.size(), "eh_99");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a corner past the vertices refused");
}

}  // namespace

int main() {
    try {
        test_room();
        test_collision();
        std::cout << "DDD map test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "DDD map test failed: " << e.what() << "\n";
        return 1;
    }
}
