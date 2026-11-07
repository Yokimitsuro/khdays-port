#pragma once

// KINGDOM HEARTS II's effects (PAX) read and played as KH2's own effect interpreter plays them (docs/FORMATS.md,
// "KH2's effects"): a PAX's 0x96 blocks -- each an effect: its emitter (layers of commands with keyed records),
// pictures, shapes, models and vertex lists -- and an instance that runs one at KH2's 60 ticks a second, opening,
// holding while it repeats and closing once told to stop, and gives what to draw each tick: triangles in clip
// space with their picture page, colour and the GS blend the game draws them with. Our own reading of the files
// and of the game's executable; no game data here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace khdays::assets::kh2 {

using EffectMatrix = std::array<float, 16>; // row vectors: v . M, the translation in the last row

struct EffectCommand final {
    std::uint32_t code = 0;                       // the command (its descriptor's index: pppPoint is 0x28 ...)
    std::vector<std::vector<std::uint8_t>> keys;  // its keyed records, each starting with its frame (1/4096)
    std::array<std::int32_t, 4> slots{};          // the slots it uses in an object's memory (from its +0xa0)
};

struct EffectLayer final {
    std::int32_t start = 0, life = 0, loop_start = 0, loop_end = 0; // in 1/4096 frames; 0x70000000 = never
    std::uint16_t order = 0;                                         // the drawing order among the layers
    std::vector<EffectCommand> commands;
};

struct EffectPrimitive final {
    std::uint8_t kind = 0;                 // 0-3 triangles, 4-7 quads; odd from 2 textured
    std::uint64_t tex0 = 0;                // the GS TEX0 of its packet (0 untextured)
    std::uint8_t corners = 3;              // 3 or 4 (a quad a strip: 0-1-2, 1-3-2)
    std::array<std::uint16_t, 4> vertex{}; // the model's vertices
    std::array<std::array<std::uint8_t, 4>, 4> colour{}; // RGBA each corner, 0x80 = 1
    std::array<std::array<float, 2>, 4> uv{};            // fractions of the page (textured kinds)
};

struct EffectModel final {
    std::uint8_t flags = 0, alpha = 0x44, fix = 0; // its header's flags, GS ALPHA_1 byte and FIX
    std::vector<std::array<float, 3>> vertices;
    std::vector<EffectPrimitive> primitives;
};

struct EffectShapeLayer final {
    std::array<std::array<float, 2>, 4> corners{}; // top-left, top-right, bottom-left, bottom-right (12.4 read)
    std::array<float, 4> rect{};                    // its part of the page: left, top, width, height (fractions)
    std::array<std::uint8_t, 4> colour{};           // 0x80 = 1
    std::uint8_t alpha = 0x44;                      // its GS ALPHA byte
};

struct EffectShapeFrame final {
    std::uint16_t wait = 0x200; // KH2 frames in 1/256
    bool loop = false;          // after the last, back to the first
    std::uint64_t tex0 = 0;
    std::vector<EffectShapeLayer> layers;
};

struct EffectShape final {
    std::vector<EffectShapeFrame> frames;
};

struct EffectVertexList final {
    std::uint32_t model = 0;
    std::vector<std::uint16_t> vertices;
};

struct EffectPicture final {
    std::uint16_t tbp = 0, cbp = 0, tbw = 0, psm = 0, x = 0, y = 0, w = 0, h = 0;
    std::vector<std::uint8_t> indices;                         // w x h, rows top to bottom
    std::array<std::array<std::uint8_t, 4>, 256> palette{};    // RGBA, alpha 0x80 = 1
};

struct EffectEmitter final {
    std::int32_t length = 0; // in 1/4096 frames
    bool repeat = false;     // runs until stopped
    std::vector<EffectLayer> layers;
};

struct EffectBlock final {
    std::vector<EffectEmitter> emitters;
    std::vector<EffectModel> models;
    std::vector<EffectShape> shapes;
    std::vector<EffectVertexList> vertex_lists;
    std::vector<EffectPicture> pictures;
};

// Every 0x96 block of a PAX (`PAX_` first); throws std::runtime_error naming what breaks the layout.
std::vector<EffectBlock> read_effect(const std::uint8_t *data, std::size_t size);
// A bare 0x96 block at `at` (effect/texcommon.dpd, KH2's resident pictures).
EffectBlock read_effect_block(const std::uint8_t *data, std::size_t size, std::size_t at);

// A TEX0's view of the pictures' pages (PSMT8, a page 128 x 64): width x height RGBA, alpha 0..255 (0x80 = 255).
struct EffectPage final {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
EffectPage effect_page(const std::vector<const EffectPicture *> &pictures, std::uint64_t tex0);

struct EffectVertex final {
    float x = 0, y = 0, z = 0, w = 1; // clip space
    float u = 0, v = 0;               // the page, fractions (may run past 1: the page repeats)
    float r = 1, g = 1, b = 1, a = 1; // the GS's colour / 128 (1 = as the picture is; up to 2)
};

// A run of triangles drawn alike, in the order given (KH2's order: the layers', each model's far to near).
struct EffectDraw final {
    std::uint64_t tex0 = 0;      // 0: untextured
    std::uint8_t alpha = 0x44;   // the GS ALPHA byte: (A - B) * C / 128 + D
    bool depth_test = true;      // only nearer than what is drawn (TEST 0x7000d); false: always (0x3000d)
    bool depth_write = false;    // the ZBUF's ZMSK clear
    bool depth_only = false;     // TEST 0x72001: the depth alone, no colour
    std::vector<EffectVertex> triangles;
};

struct EffectCamera final {
    EffectMatrix world{}, view{}, projection{}; // the instance's place, the camera, the projection
};

// One effect running. Not thread-safe; cheap to move.
class EffectInstance final {
public:
    // Plays the block's `emitter`; throws std::runtime_error if it uses a command or primitive not read.
    EffectInstance(std::shared_ptr<const EffectBlock> block, std::size_t emitter, std::uint32_t seed);
    ~EffectInstance();
    EffectInstance(EffectInstance &&) noexcept;
    EffectInstance &operator=(EffectInstance &&) noexcept;

    // One KH2 tick (1/60 s) with the camera given; `draws` gets what to draw when not null.
    void tick(const EffectCamera &camera, std::vector<EffectDraw> *draws);
    // A repeating effect closes: its layers stop looping and it ends when its objects have.
    void stop();
    bool done() const;
    std::size_t objects() const;
    const EffectBlock &block() const;

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace khdays::assets::kh2
