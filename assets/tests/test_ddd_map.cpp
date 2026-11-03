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
#include <tuple>
#include <vector>

#include "khdays/assets/ddd_map.h"

// A Dream Drop Distance room made here byte by byte as the reader expects it (docs/FORMATS.md, "Dream Drop
// Distance's rooms"): a PMP placing one PMO, whose section's first group holds a record of triangles and a strip
// stitched by a degenerate triangle and its second group a blended triangle, its texture found as a plain 32-bit DDS;
// a BCD of a triangle and a four-sided face; a TXA animating its texture. Birth by Sleep's (docs/FORMATS.md, "Birth by
// Sleep's rooms"): a version 3 PMO of a mesh of triangles in their own colours and a mesh of two strips in the mesh's
// colour, positions in floats and in 16 bits; a version 2 BCD; a map archive.

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

Bytes room(const char* texture = "tex_a") {
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
    w.text(texture, 12);
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
    w.text(texture, 12);
    w.zeros(16);
    return w.b;
}

// A BCD: one part of four vertices, a triangle and a four-sided face.
Bytes collision(bool version2 = false) {
    Writer w;
    w.text("@bcd", 4);
    w.u32(version2 ? 2 : 0x0a);
    w.u32(1);
    w.u32(0x10);
    for (int i = 0; i < 6; ++i)
        w.f32(i < 3 ? 0.0F : 1.0F);  // its box
    if (version2)
        w.zeros(0x0c);  // (version 2's part is 0x40 bytes, its counts after 12 more)
    const std::uint32_t vertices = version2 ? 0x50 : 0x40;
    w.u16(4);
    w.u16(2);
    w.u32(vertices);
    w.u32(vertices + 0x40);
    w.zeros(vertices - w.b.size());
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

// A room's sky (its texture named so): its meshes under a sky node, drawn around the camera.
void test_sky() {
    const Bytes pmp = room("eh99_sky01");
    const auto made = ddd::room_model(
        pmp.data(), pmp.size(), "eh_99", [](const std::string&) { return std::filesystem::path(); },
        [](const std::string&, const khdays::assets::DecodedTexture&) {});
    const auto& scene = made.scene;
    bool all_sky = !scene.meshes.empty();
    for (const auto& mesh : scene.meshes)
        all_sky = all_sky && mesh.node > 0 && scene.nodes[static_cast<std::size_t>(mesh.node)].sky;
    expect(all_sky, "a sky's meshes under a sky node");
    const Bytes forest = room("sw01_forest");
    const auto trees = ddd::room_model(
        forest.data(), forest.size(), "sw_01", [](const std::string&) { return std::filesystem::path(); },
        [](const std::string&, const khdays::assets::DecodedTexture&) {});
    bool none_sky = true;
    for (const auto& mesh : trees.scene.meshes)
        none_sky = none_sky && mesh.node == 0;
    expect(none_sky, "a forest is no backdrop");
    Bytes flagged = room("dp01_ball");
    flagged[0x20 + 0x2c] = 1;  // the object's bit: drawn around the camera
    const auto ball = ddd::room_model(
        flagged.data(), flagged.size(), "dp_01", [](const std::string&) { return std::filesystem::path(); },
        [](const std::string&, const khdays::assets::DecodedTexture&) {});
    bool ball_sky = !ball.scene.meshes.empty();
    for (const auto& mesh : ball.scene.meshes)
        ball_sky = ball_sky && mesh.node > 0 && ball.scene.nodes[static_cast<std::size_t>(mesh.node)].sky;
    expect(ball_sky, "an object its bit marks a sky, whatever its texture's name");
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
    expect(std::fabs(p.uvs[1][0] - 1) < 1e-4F, "uv as written");
    expect(p.colors[0] == std::array<std::uint8_t, 4>{0x40, 0x80, 0xFF, 0xFF},
           "colour doubled (0x80 is 1, brighter kept at 1), alpha as written");
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

// A TXA of two entries: one on the room's texture (a sheet of two frames, four steps -- one shown for no time, one of
// a random length), one on a texture the room has not.
Bytes txa() {
    Writer w;
    w.text("TXA", 4);
    w.u16(2);
    w.u16(2);  // entries
    w.zeros(8);
    for (const auto& [name, texture] : {std::pair<const char*, const char*>{"anim_a", "tex_a"}, {"other", "tex_b"}}) {
        w.text(name, 16);
        w.text(texture, 16);
        w.zeros(8);
        w.u16(4);  // the sheet: 4 x 2, a frame 2 x 2
        w.u16(2);
        w.u16(2);
        w.u16(2);
        w.u32(1);
        w.u32(0);  // its animation's offset, set below
    }
    const std::array<std::array<unsigned, 4>, 4> steps{{{0, 0, 10, 0}, {2, 0, 0, 0}, {2, 0, 5, 15}, {0, 0, 20, 0}}};
    for (std::size_t e = 0; e < 2; ++e) {
        w.at(0x10 + e * 0x38 + 0x34, static_cast<std::uint32_t>(w.b.size()));
        const std::size_t count = e == 0 ? steps.size() : 1;
        w.text(e == 0 ? "01" : "02", 16);
        w.u16(static_cast<unsigned>(-static_cast<int>(count)) & 0xFFFF);
        w.u16(static_cast<unsigned>(count));
        w.u32(static_cast<std::uint32_t>(w.b.size() + 4));
        for (std::size_t f = 0; f < count; ++f) {
            for (const unsigned v : steps[f])
                w.u16(v);
            w.zeros(4);
        }
    }
    return w.b;
}

void test_texture_animation() {
    const Bytes data = txa();
    const auto animations = ddd::texture_animations(data.data(), data.size());
    expect(animations.size() == 2 && animations[0].texture == "tex_a" && animations[1].texture == "tex_b",
           "each entry's animation");
    const auto& a = animations[0];
    expect(a.frames.size() == 3 && a.frames[1].x == 2 && a.frames[1].length == 10 && a.loop_from == 0,
           "the step shown for no time left out, the random one at its middle");
    const Bytes pmp = room();
    const auto folder = std::filesystem::temp_directory_path() / "khdays_ddd_txa_test";
    std::filesystem::create_directories(folder);
    const auto sheet = folder / "tex_aa.dds";
    {
        const Bytes dds = plain_dds();  // 2 x 1: the sheet at half its size
        std::ofstream(sheet, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()),
                                                    static_cast<std::streamsize>(dds.size()));
    }
    std::map<std::string, khdays::assets::DecodedTexture> written;
    const auto write = [&](const std::string& file, const khdays::assets::DecodedTexture& t) { written[file] = t; };
    auto made = ddd::room_model(
        pmp.data(), pmp.size(), "eh_99", [&](const std::string&) { return std::filesystem::path(); }, write);
    // the room's texture as the reader names it, its picture not in the copy
    for (auto& m : made.scene.materials)
        m.texture = "tex_a.png";
    made.report.clear();
    ddd::animate_textures(
        &made, animations, {"tex_b_00", "tex_aa"},
        [&](const std::string& name) { return name == "tex_aa" ? sheet : std::filesystem::path(); }, write);
    expect(made.report.empty(), "nothing left out: " + (made.report.empty() ? std::string() : made.report.front()));
    expect(written.size() == 2 && written["tex_a_0.png"].width == 1 && written["tex_a_0.png"].rgba[0] == 200 &&
               written["tex_a_1.png"].rgba[0] == 100,
           "each place cut once, at the sheet's scale");
    for (const auto& m : made.scene.materials)
        expect(m.frame_pictures == std::vector<std::string>{"tex_a_0.png", "tex_a_1.png"} &&
                   m.frames == std::vector<std::array<int, 2>>{{0, 10}, {1, 10}, {0, 20}} && m.frames_loop,
               "both materials of the texture shown frame by frame");
    ddd::animate_textures(&made, animations, {}, {}, write);
    expect(made.report.size() == 1, "a sheet the copy has not said");
    bool threw = false;
    try {
        const Bytes junk(64, 0);
        (void)ddd::texture_animations(junk.data(), junk.size());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a file that is no TXA refused");
    std::filesystem::remove_all(folder);
}

}  // namespace

// A Birth by Sleep room: a PMP placing a version 3 PMO moved by (10, 0, 0), its scale 2.
Bytes bbs_room() {
    Writer w;
    w.text("PMP", 4);
    w.zeros(0x0c);
    w.u16(1);  // objects
    w.u16(1);
    w.zeros(6);
    w.u16(1);  // textures
    const std::size_t texture_list_field = w.b.size();
    w.u32(0);
    w.f32(10);
    w.zeros(20);
    w.f32(1);
    w.f32(1);
    w.f32(1);
    const std::size_t pmo_field = w.b.size();
    w.u32(0);
    w.zeros(8);
    const std::size_t pmo = w.b.size();
    w.at(pmo_field, static_cast<std::uint32_t>(pmo));
    w.text("PMO", 4);
    w.u8(1);
    w.u8(1);
    w.u8(3);  // version 3
    w.u8(0);
    w.u8(1);  // its textures
    w.zeros(3);
    w.u32(0);     // no skeleton
    w.u32(0xc0);  // first pass
    w.u16(4);
    w.u16(10);
    w.f32(2);  // its scale
    w.u32(0);  // no second pass
    w.zeros(0xa0 - (w.b.size() - pmo));
    w.u32(0);
    w.text("tex_b", 12);
    w.zeros(16);
    // a mesh of triangles: texture coordinates in 8 bits, colours 8888, positions in floats (20 bytes)
    w.u16(3);
    w.u8(0);
    w.u8(20);
    w.u32(0x3000019DU);
    w.zeros(4);
    for (const auto& p : {std::array<float, 3>{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}) {
        w.u8(64);
        w.u8(128);
        w.zeros(2);
        w.u32(0xFFFF8040U);  // R 0x40, G 0x80, B 0xff, A 0xff
        w.f32(p[0]);
        w.f32(p[1]);
        w.f32(p[2]);
    }
    // a mesh of two strips (3 and 4 vertices) in the mesh's colour: coordinates in 8 bits, positions in 16 (8 bytes)
    w.u16(7);
    w.u8(0);
    w.u8(8);
    w.u32(0x41000101U);
    w.u8(0);
    w.u8(2);  // its strips
    w.u16(0);
    w.u32(0x80808080U);  // its colour
    w.u16(3);
    w.u16(4);
    for (int k = 0; k < 7; ++k) {
        w.u8(0);
        w.u8(0);
        w.u16(static_cast<unsigned>(k * 4096) & 0xFFFF);
        w.u16(k % 2 == 0 ? 16384U : 0U);
        w.u16(static_cast<unsigned>(k) * 1000U);
    }
    w.u32(0);  // no more meshes
    w.at(texture_list_field, static_cast<std::uint32_t>(w.b.size()));
    w.u32(0);
    w.text("tex_b", 12);
    w.zeros(16);
    return w.b;
}

void test_bbs_room() {
    const Bytes pmp = bbs_room();
    const auto made = ddd::room_model(
        pmp.data(), pmp.size(), "jb_99", [](const std::string&) { return std::filesystem::path(); },
        [](const std::string&, const khdays::assets::DecodedTexture&) {});
    expect(made.report.size() == 1, "only its missing picture said: " + std::to_string(made.report.size()));
    const auto& scene = made.scene;
    expect(scene.meshes.size() == 1 && scene.meshes[0].primitives.size() == 1, "one mesh of its texture");
    const auto& p = scene.meshes[0].primitives[0];
    expect(p.indices.size() == 12, "a triangle and two strips' three: " + std::to_string(p.indices.size() / 3));
    expect(std::fabs(p.positions[0][0] - 12) < 1e-4F, "a float position times its scale, moved by the object");
    expect(std::fabs(p.uvs[0][0] - 0.5F) < 1e-4F && std::fabs(p.uvs[0][1] - 1) < 1e-4F, "8-bit coordinates, 128 is 1");
    expect(p.colors[0] == std::array<std::uint8_t, 4>{0x80, 0xFF, 0xFF, 0xFF}, "its colour doubled, alpha as written");
    expect(std::fabs(p.positions[3][1] - 1) < 1e-4F && std::fabs(p.positions[3][0] - 10) < 1e-4F,
           "a 16-bit position: 32768 is 1, times its scale");
    expect(p.colors[3] == std::array<std::uint8_t, 4>{0xFF, 0xFF, 0xFF, 0x80}, "the mesh's colour for its vertices");
}

void test_bbs_collision() {
    const Bytes bcd = collision(true);
    const auto scene = ddd::collision_scene(bcd.data(), bcd.size(), "jb_99");
    expect(scene.meshes.size() == 1 && scene.meshes[0].primitives[0].indices.size() == 9,
           "a version 2 BCD's triangle and four-sided face");
}

void test_arc() {
    Writer w;
    w.text("ARC", 4);
    w.u16(1);
    w.u16(2);
    w.zeros(8);
    for (const auto& [name, at, size] :
         {std::tuple<const char*, unsigned, unsigned>{"jb_99.pmp", 0x50, 4}, {"jb_99a.bcd", 0x54, 8}}) {
        w.u32(0);
        w.u32(at);
        w.u32(size);
        w.u32(0);
        w.text(name, 16);
    }
    w.zeros(12);
    const auto members = ddd::arc_members(w.b.data(), w.b.size());
    expect(members.size() == 2 && members[0].name == "jb_99.pmp" && members[1].offset == 0x54 && members[1].size == 8,
           "the archive's members");
    Writer link;  // a room another archive holds
    link.text("ARC", 4);
    link.u16(1);
    link.u16(1);
    link.zeros(8);
    link.u32(ddd::kArcMapLink);
    link.zeros(12);
    link.text("sb05", 16);
    const auto linked = ddd::arc_members(link.b.data(), link.b.size());
    expect(linked.size() == 1 && linked[0].kind == ddd::kArcMapLink && linked[0].name == "sb05" && linked[0].size == 0,
           "a link to another map's room");
    w.at(0x10 + 0x20 + 8, 0x1000);  // a member past its end
    bool threw = false;
    try {
        (void)ddd::arc_members(w.b.data(), w.b.size());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "a member past the archive's end refused");
}

int main() {
    try {
        test_room();
        test_sky();
        test_bbs_room();
        test_bbs_collision();
        test_arc();
        test_collision();
        test_texture_animation();
        std::cout << "DDD map test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "DDD map test failed: " << e.what() << "\n";
        return 1;
    }
}
