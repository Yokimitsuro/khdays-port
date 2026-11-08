// KINGDOM HEARTS II's effects read and played (assets/kh2_effect.h): a made-up PAX in the layout KH2's own have
// (docs/FORMATS.md, "KH2's effects") -- one block, one emitter, a model and a picture -- and its run: a layer's
// keyed records placing and colouring a quad, its life, a repeating effect's hold and its end once stopped; the
// same directory read without the PAX header (KH1's and Re:CoM's .dpx); then blocks made from parts for the
// commands read since: the rotation orders, the random ones' ranges, pppPointAp's children, pppVtMime's blend of
// two models, a textured triangle (primitive kind 2).
#include "khdays/assets/kh2_effect.h"
#include "kh2_effect_made_up.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace khdays::assets::kh2;
using khdays_test::made_up;
using namespace khdays_test;

namespace {

void expect(bool test, const char *why)
{
    if (!test)
        throw std::runtime_error(why);
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-4F;
}

EffectMatrix identity()
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

void test_reading()
{
    const auto bytes = made_up(0, 0, false);
    const auto blocks = read_effect(bytes.data(), bytes.size());
    expect(blocks.size() == 1, "one block");
    const auto &b = blocks[0];
    expect(b.emitters.size() == 1 && b.emitters[0].layers.size() == 1, "one emitter, one layer");
    expect(b.emitters[0].length == 30 * 0x1000 && !b.emitters[0].repeat, "the emitter's length and flag");
    const auto &L = b.emitters[0].layers[0];
    expect(L.commands.size() == 6 && L.commands[2].keys.size() == 2, "the commands and their keys");
    expect(L.commands[3].slots[1] == 0x30, "a command's slots");
    expect(b.models.size() == 1 && b.models[0].primitives.size() == 1 && b.models[0].vertices.size() == 4,
           "the model");
    expect(b.models[0].alpha == 0x48 && b.models[0].primitives[0].corners == 4, "the model's blend and quad");
    expect(near(b.models[0].primitives[0].uv[3][0], 1.0F), "a quad's UV");
    expect(b.pictures.size() == 1 && b.pictures[0].w == 2, "the picture");
    const auto page = effect_page({&b.pictures[0]}, b.models[0].primitives[0].tex0);
    expect(page.width == 2 && page.height == 2, "the page's size from the TEX0");
    expect(page.rgba[4] == 0xff && page.rgba[7] == 0xff, "entry 1 red, its alpha 0x80 made 255");
    expect(page.rgba[15] == 0x80, "entry 3's alpha 0x40 made 0x80");

    auto broken = bytes;
    broken[0x10] = 0;
    bool refused = false;
    try {
        read_effect(broken.data(), broken.size());
    } catch (const std::runtime_error &) {
        refused = true;
    }
    expect(refused, "no directory refused");

    // KH1's and Re:Chain of Memories' .dpx: the same directory without the PAX header
    std::uint32_t directory = 0;
    std::memcpy(&directory, bytes.data() + 0xc, 4);
    const std::vector<std::uint8_t> dpx(bytes.begin() + directory, bytes.end());
    const auto bare = read_effect(dpx.data(), dpx.size());
    expect(bare.size() == 1 && bare[0].emitters.size() == 1 && bare[0].models.size() == 1, "a bare DPX read alike");
    auto neither = dpx;
    neither[0] = 0x83;
    refused = false;
    try {
        read_effect(neither.data(), neither.size());
    } catch (const std::runtime_error &) {
        refused = true;
    }
    expect(refused, "neither a PAX nor a DPX refused");
}

void test_run()
{
    const auto bytes = made_up(3, 0, false);
    auto block = std::make_shared<const EffectBlock>(read_effect(bytes.data(), bytes.size())[0]);
    EffectInstance fx(block, 0, 1);
    EffectCamera cam{identity(), identity(), identity()};
    std::vector<EffectDraw> draws;
    fx.tick(cam, &draws);
    expect(draws.size() == 1 && draws[0].triangles.size() == 6, "a quad as two triangles");
    const auto &t = draws[0].triangles;
    // vertex 0 (-1, -1) x scale 2 + (0, -10): (-2, -12); its colour 0x80 x 0x80 / 128 / 128
    expect(near(t[0].x, -2) && near(t[0].y, -12) && near(t[0].w, 1), "placed by its matrix");
    expect(near(t[0].r, 1) && near(t[0].a, 1), "coloured by pppColor at 1");
    expect(draws[0].alpha == 0x48 && !draws[0].depth_write, "the model's blend, no depth written (flag bit 0)");
    draws.clear();
    fx.tick(cam, &draws);
    draws.clear();
    fx.tick(cam, &draws); // frame 2: the colour's second key halves the alpha
    expect(!draws.empty() && near(draws[0].triangles[0].a, 0.5F), "a keyed record on its frame");
    expect(fx.objects() == 0, "the layer gone after its life");
    for (int k = 0; k < 40 && !fx.done(); ++k)
        fx.tick(cam, nullptr);
    expect(fx.done(), "a non-repeating effect done after its length");
}

void test_hold_and_stop()
{
    const auto bytes = made_up(0, 4, true);
    auto block = std::make_shared<const EffectBlock>(read_effect(bytes.data(), bytes.size())[0]);
    EffectInstance fx(block, 0, 1);
    EffectCamera cam{identity(), identity(), identity()};
    for (int k = 0; k < 200; ++k)
        fx.tick(cam, nullptr);
    expect(!fx.done() && fx.objects() == 1, "a repeating effect holds, its layer looping");
    fx.stop();
    for (int k = 0; k < 10 && !fx.done(); ++k)
        fx.tick(cam, nullptr);
    expect(fx.done(), "stopped: its never-ending layer let go, the effect done");
}

void test_unknown_command_refused()
{
    auto bytes = made_up(0, 0, false);
    // the first command's code -> one not read
    const std::size_t L = 0x40 + 0x80 + 0x110 + 0x20;
    bytes[L + 0x28] = 0x01; // in KH2's table, in none of its files
    auto block = std::make_shared<const EffectBlock>(read_effect(bytes.data(), bytes.size())[0]);
    bool refused = false;
    try {
        EffectInstance fx(block, 0, 1);
    } catch (const std::runtime_error &) {
        refused = true;
    }
    expect(refused, "a command not read refused");
}

// a command with one key at frame 0 whose bytes after the frame are given
MadeCommand command(std::uint32_t code, std::uint16_t size, std::initializer_list<std::int32_t> slots,
                    const std::function<void(std::vector<std::uint8_t> &)> &fill = {})
{
    MadeCommand c;
    c.code = code;
    c.size = size;
    c.keys.push_back(made_key(0, size));
    if (fill)
        fill(c.keys[0]);
    int i = 0;
    for (const auto s : slots)
        c.slots[i++] = s;
    return c;
}

// placed at the position slot 0x20 (pppPoint), angles 0x30, scale 1 at 0x40, colour 0x50; a matrix of `order`,
// pppDrawMatrix, pppDrawMdl3 of model `model`
std::vector<MadeCommand> placed(std::uint32_t order, float x, float y, float z, std::int32_t ax, std::int32_t ay,
                                int model = 0)
{
    std::vector<MadeCommand> out;
    out.push_back(command(0x28, 0x10, {0x20}, [&](auto &k) {
        put(k, 4, x);
        put(k, 8, y);
        put(k, 12, z);
    }));
    out.push_back(command(0x29, 0x10, {0x30}, [&](auto &k) {
        put(k, 4, ax);
        put(k, 8, ay);
    }));
    out.push_back(command(0x2a, 0x10, {0x40}, [](auto &k) {
        put(k, 4, 1.0F);
        put(k, 8, 1.0F);
        put(k, 12, 1.0F);
    }));
    out.push_back(command(0x2b, 0xc, {0x50}, [](auto &k) {
        for (int c = 0; c < 4; ++c)
            put(k, 4 + 2 * std::size_t(c), std::uint16_t(0x4000));
    }));
    out.push_back(command(order, 4, {0x20, 0x30, 0x40}));
    out.push_back(command(0x70, 4, {}));
    out.push_back(command(0x89, 0x10, {0x50}, [&](auto &k) { put(k, 4, std::int32_t(model)); }));
    return out;
}

MadeModel triangle(std::int16_t x, std::int16_t y, std::int16_t z, std::uint8_t kind = 0)
{
    MadeModel m;
    m.vertices = {{x, y, z}, {0, 0, 0}, {0, 1, 0}};
    MadePrimitive p;
    p.kind = kind;
    p.vertex[0] = 0;
    p.vertex[1] = 1;
    p.vertex[2] = 2;
    p.uv[0][0] = 4096;
    p.uv[2][1] = 2048;
    m.primitives.push_back(p);
    return m;
}

std::shared_ptr<const EffectBlock> block_of(const std::vector<std::uint8_t> &bytes)
{
    return std::make_shared<const EffectBlock>(read_effect(bytes.data(), bytes.size()).at(0));
}

EffectVertex first_corner(EffectInstance &fx, std::vector<EffectDraw> &draws)
{
    draws.clear();
    fx.tick(EffectCamera{identity(), identity(), identity()}, &draws);
    expect(!draws.empty() && !draws[0].triangles.empty(), "something drawn");
    return draws[0].triangles[0];
}

void test_rotation_orders()
{
    // (100, 0, 0) turned 90 degrees about x and about y: x first (pppMatrixXYZ) it ends at (0, 0, -100); z, y,
    // then x (pppMatrixZYX), at (0, 100, 0)
    const struct {
        std::uint32_t code;
        float x, y, z;
    } cases[] = {{0x57, 0, 0, -100}, {0x5c, 0, 100, 0}, {0xe6, 0, 0, -100}, {0xeb, 0, 100, 0}};
    for (const auto &c : cases) {
        MadeLayer L;
        L.commands = placed(c.code, 0, 0, 0, 0x4000, 0x4000);
        EffectInstance fx(block_of(made_block({L}, {triangle(100, 0, 0)}, {}, 10, false)), 0, 1);
        std::vector<EffectDraw> draws;
        const auto v = first_corner(fx, draws);
        expect(near(v.x, c.x) && near(v.y, c.y) && near(v.z, c.z), "a rotation order");
    }
}

void test_random_ranges()
{
    // pppRandUpFV / pppRandDownFV on the position: base (10, 20, 30) x one draw in [0, 1) or (-1, 0]
    for (const std::uint32_t code : {0x40u, 0x41u}) {
        for (std::uint32_t seed = 1; seed < 20; ++seed) {
            MadeLayer L;
            L.commands = placed(0x57, 0, 0, 0, 0, 0);
            L.commands.insert(L.commands.begin() + 1, command(code, 0x18, {0x60}, [](auto &k) {
                put(k, 4, std::int32_t(0x20));
                put(k, 8, 10.0F);
                put(k, 12, 20.0F);
                put(k, 16, 30.0F);
            }));
            EffectInstance fx(block_of(made_block({L}, {triangle(0, 0, 0)}, {}, 10, false)), 0, seed);
            std::vector<EffectDraw> draws;
            const auto v = first_corner(fx, draws);
            const float s = code == 0x40 ? 1.0F : -1.0F;
            expect(s * v.x >= 0 && s * v.x <= 10 && s * v.y >= 0 && s * v.y <= 20, "a range");
            expect(near(v.y, 2 * v.x) && near(v.z, 3 * v.x), "one draw for the three");
        }
    }
}

void test_point_ap()
{
    // layer 0 at (5, 6, 7) bears two of layer 1 every 3 ticks; they start where it is
    MadeLayer parent;
    parent.commands.push_back(command(0x28, 0x10, {0x20}, [](auto &k) {
        put(k, 4, 5.0F);
        put(k, 8, 6.0F);
        put(k, 12, 7.0F);
    }));
    parent.commands.push_back(command(0xac, 0x10, {0x20, 0x70}, [](auto &k) {
        put(k, 4, std::int32_t(1));    // the layer
        put(k, 8, std::int32_t(0x20)); // the child's slot
        put(k, 0xc, std::uint8_t(3));  // the wait
        put(k, 0xd, std::uint8_t(2));  // how many
    }));
    MadeLayer child;
    child.start = 0x7fffffff;
    child.life = 100 * 0x1000;
    child.commands = placed(0x57, 0, 0, 0, 0, 0);
    child.commands.erase(child.commands.begin()); // its position from the parent, not a pppPoint
    EffectInstance fx(block_of(made_block({parent, child}, {triangle(1, 0, 0)}, {}, 30, false)), 0, 1);
    const EffectCamera cam{identity(), identity(), identity()};
    fx.tick(cam, nullptr);
    expect(fx.objects() == 3, "two children born");
    std::vector<EffectDraw> draws;
    fx.tick(cam, &draws);
    expect(draws.size() == 2 && near(draws[0].triangles[0].x, 6) && near(draws[0].triangles[0].z, 7),
           "each where its parent's point is");
    fx.tick(cam, nullptr);
    fx.tick(cam, nullptr);
    expect(fx.objects() == 5, "two more after the wait");
}

void test_vt_mime()
{
    // vertex 0 half way from (0, 0, 0) to (100, 50, -20)
    MadeLayer L;
    L.commands = placed(0x57, 0, 0, 0, 0, 0);
    L.commands.insert(L.commands.end() - 1, command(0x31, 0x18, {0x60}, [](auto &k) {
        put(k, 4, std::int32_t(0));
        put(k, 8, std::int32_t(1));
        put(k, 0xc, 0.5F);
    }));
    EffectInstance fx(block_of(made_block({L}, {triangle(0, 0, 0), triangle(100, 50, -20)}, {}, 10, false)), 0, 1);
    std::vector<EffectDraw> draws;
    const auto v = first_corner(fx, draws);
    expect(near(v.x, 50) && near(v.y, 25) && near(v.z, -10), "the blend of two models");
}

void test_textured_triangle()
{
    MadeLayer L;
    L.commands = placed(0x57, 0, 0, 0, 0, 0);
    EffectInstance fx(block_of(made_block({L}, {triangle(3, 0, 0, 2)}, {}, 10, false)), 0, 1);
    std::vector<EffectDraw> draws;
    const auto v = first_corner(fx, draws);
    expect(draws[0].tex0 != 0 && near(v.u, 1.0F) && near(draws[0].triangles[2].v, 0.5F), "kind 2's page and UV");
}

} // namespace

// a bolt (pppKeTh) up from the origin, 10 a segment, toward a pppKeThTp's point on another layer at (0, aim, 0);
// its ribbon's vertices: a fan at each end, ten-vertex strips between (12 + (points - 3) x 24 + 12)
std::size_t bolt_vertices(float aim, EffectVertex &head)
{
    MadeLayer target;
    target.commands.push_back(command(0x28, 0x10, {0x20}, [&](auto &k) { put(k, 8, aim); }));
    target.commands.push_back(command(0x5d, 4, {0x20}));
    target.commands.push_back(command(0x7e, 0x18, {0x40}, [](auto &k) {
        put(k, 4, 5.0F);              // its reach
        put(k, 8, 50.0F);             // near
        put(k, 0xc, 0.0F);            // strength
        put(k, 0x10, std::int32_t(-1)); // no next point
    }));
    MadeLayer bolt;
    bolt.commands.push_back(command(0x03, 4, {0x10}));                                // ThRes8: 8 points
    bolt.commands.push_back(command(0x30, 0x10, {0x20}, [](auto &k) { put(k, 8, 1.0F); })); // up
    bolt.commands.push_back(command(0x81, 0x5c, {0x40, 0x10, 0x20}, [](auto &k) {
        put(k, 4, std::int32_t(0));    // the point: layer 0's
        put(k, 8, std::int32_t(0x40)); // its slot
        for (int j = 0; j < 4; ++j) {
            put(k, std::size_t(0xc + 2 * j), std::int16_t(128 * 128)); // the core 128 a byte
            put(k, std::size_t(0x14 + 2 * j), std::int16_t(64 * 128)); // the edges 64
        }
        put(k, 0x2c, 70.0F);   // 7 segments of 10
        put(k, 0x30, 1000.0F); // its size
        put(k, 0x34, 1.0F);    // as wide at the tail
        put(k, 0x38, 0.25F);   // the core's part
        put(k, 0x48, 100.0F);  // 10 segments a tick: grown at once
        put(k, 0x53, std::uint8_t(0xff)); // no branches
    }));
    EffectInstance fx(block_of(made_block({target, bolt}, {}, {}, 30, false)), 0, 1);
    const EffectCamera cam{identity(), identity(), identity()};
    std::vector<EffectDraw> draws;
    fx.tick(cam, &draws);
    for (const auto &d : draws)
        if (d.alpha == 0x48) {
            for (const auto &v : d.triangles)
                expect(std::isfinite(v.x) && std::isfinite(v.y), "the ribbon finite");
            head = d.triangles.at(0);
            return d.triangles.size();
        }
    return 0;
}

void test_lightning()
{
    EffectVertex head;
    expect(bolt_vertices(500, head) == 12 + 5 * 24 + 12, "8 points grown, far from the point");
    expect(near(head.x, 0) && near(head.y, 0) && near(head.r, 1) && near(head.a, 1),
           "its head fan at its start, the core's colour");
    expect(bolt_vertices(30, head) == 12 + 24 + 12, "4 points: it ends where it reaches the point");
}

// pppRyjMegaBirthUserCtrl2: its model at each of the game's matrices (none given: nothing drawn)
void test_game_matrices()
{
    MadeLayer L;
    L.commands.push_back(command(0xf0, 0x1c, {}, [](auto &k) {
        put(k, 4, std::int32_t(0xffff)); // no shape
        put(k, 0x10, std::int32_t(0));    // model 0
        for (int j = 0; j < 4; ++j)
            put(k, std::size_t(0x14 + j), std::uint8_t(0x80));
    }));
    EffectInstance fx(block_of(made_block({L}, {triangle(1, 2, 3)}, {}, 10, false)), 0, 1);
    const EffectCamera cam{identity(), identity(), identity()};
    std::vector<EffectDraw> draws;
    fx.tick(cam, &draws);
    expect(draws.empty(), "no matrices, nothing drawn");
    EffectGameMatrix g;
    g.matrix = identity();
    g.matrix[12] = 5, g.matrix[13] = 6, g.matrix[14] = 7;
    fx.set_matrices({g});
    draws.clear();
    fx.tick(cam, &draws);
    expect(draws.size() == 1 && near(draws[0].triangles[0].x, 6) && near(draws[0].triangles[0].z, 10),
           "the model at the matrix's place");
}

int main()
{
    try {
        test_reading();
        test_run();
        test_hold_and_stop();
        test_unknown_command_refused();
        test_rotation_orders();
        test_random_ranges();
        test_point_ap();
        test_vt_mime();
        test_textured_triangle();
        test_lightning();
        test_game_matrices();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "kh2 effect: %s\n", e.what());
        return 1;
    }
    std::printf("kh2 effect: all checks passed\n");
    return 0;
}
