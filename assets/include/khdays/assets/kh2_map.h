#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "khdays/assets/kh2_bar.h"
#include "khdays/assets/texture.h"  // DecodedTexture

// KINGDOM HEARTS II FINAL MIX's maps (map/<world><nn>.map in the player's
// KINGDOM HEARTS HD 1.5+2.5 ReMIX copy), read for the studio's KH2 rooms: the
// BAR archive and its entries, texture containers, map and object models, piece
// placements and motions, collision and radar pictures. Written from public
// descriptions of the formats and the files themselves; every reader throws
// std::runtime_error naming what it could not read.
namespace khdays::assets::kh2 {

// Entry types in a map's BAR.
inline constexpr std::uint32_t kEntryModel = 0x04;       // MAP, SK0, SK1, BOB
inline constexpr std::uint32_t kEntryCollision = 0x06;   // ID_<w>: where characters walk (COCT)
inline constexpr std::uint32_t kEntryTextures = 0x07;    // the model of the same name's textures
inline constexpr std::uint32_t kEntryRadar = 0x0a;       // rada: the room's top view (TIM2)
inline constexpr std::uint32_t kEntryPlacements = 0x15;  // out: where each piece (BOB) stands
inline constexpr std::uint32_t kEntryMotions = 0x1e;     // low 16 bits: a piece's motions (a BAR)
inline constexpr std::uint32_t kEntryMotion = 0x09;      // in a motion BAR: one motion

// --- Textures ---

// One texture of a container (a GS info): its GS registers TEX0 (size) and
// CLAMP (wrap modes and region). Wrap modes are the GS's: 0 repeat, 1 clamp,
// 2 region clamp (texels MIN..MAX), 3 region repeat (u = (u & MIN) | MAX).
struct GsTexture final {
    int width = 0;
    int height = 0;
    int wrap_u = 0;
    int wrap_v = 0;
    int min_u = 0;
    int max_u = 0;
    int min_v = 0;
    int max_v = 0;
    // The part of the texture in use, in texels: what its HD picture covers.
    int region_x = 0;
    int region_y = 0;
    int region_width = 0;
    int region_height = 0;
};

// Texture coordinates over the whole texture (as the models store them) moved
// into the used region's own 0..1, the HD picture's.
std::array<float, 2> region_uv(const GsTexture& texture, float u, float v);

// UVSC: a scroll speed by its index (the model's groups name the index).
struct UvScroll final {
    std::uint32_t index = 0;
    float u = 0.0F;  // as stored: 2^31 = a texture's width per game frame (not yet checked in motion)
    float v = 0.0F;
};

// One step of a texture animation's frame table.
struct TextureFrame final {
    enum class Kind { Sprite, Base, Jump, Stop };
    Kind kind = Kind::Sprite;
    int jump = 0;        // Jump: steps to move (negative: back)
    int min_length = 0;  // game frames; a length between min and max is drawn at random
    int max_length = 0;
    int sprite = 0;
};

// TEXA: sprites drawn over part of a texture in turn (blinking lights, eyes).
struct TextureAnimation final {
    int target = 0;  // GS info index in the container
    int sprite_count = 0;
    int x = 0;  // where the sprites go in the texture, texels
    int y = 0;
    int width = 0;
    int height = 0;
    int default_animation = 0;
    std::vector<std::vector<TextureFrame>> animations;
    int hd_sheet = -1;  // its sprites' HD sheet, numbered within the container (see TextureContainer)
};

struct TextureContainer final {
    std::vector<GsTexture> textures;
    std::vector<UvScroll> scrolls;
    std::vector<TextureAnimation> animations;
    // The HD pictures (-N.dds) of a map run through its containers in BAR order:
    // each GS info, followed by one sprite sheet per TEXA aimed at it. Here,
    // within this container: GS info i is hd_pictures[i]; hd_count in all.
    std::vector<int> hd_pictures;
    int hd_count = 0;
};

TextureContainer read_textures(const std::uint8_t* data, std::size_t size, std::size_t at);

// TIM2 pictures (radar): 4-bit, 8-bit (palette in the PS2's order) or 32-bit;
// PS2 alpha (0x80 = opaque) widened to 0..255.
std::vector<DecodedTexture> read_tim2(const std::uint8_t* data, std::size_t size, std::size_t at);

// --- Models ---

struct Vertex final {
    std::array<float, 3> position{};                            // KH2 units; object models: in its bone's space
    std::array<float, 2> uv{};                                  // over the whole texture, as stored
    std::array<std::uint8_t, 4> color{0x80, 0x80, 0x80, 0x80};  // as stored: 0x80 = 1.0
    int bone = 0;
};

struct Triangle final {
    std::array<Vertex, 3> corners{};
    bool two_sided = false;  // a strip vertex flag 0x00 (cloth seen from both sides)
};

// A map model's DMA chain or an object model's subpart: one texture.
struct Group final {
    int texture = 0;  // GS info index in the model's container
    bool transparent = false;
    std::uint8_t uv_option = 0;  // as stored: bit 1 scrolls, bits 2-5 the UVSC index
    std::vector<Triangle> triangles;

    std::optional<int> scroll_index() const;
};

struct Bone final {
    int parent = -1;
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
    std::array<float, 3> rotation{};  // radians, x y z
    std::array<float, 3> translation{};
};

struct Model final {
    int kind = 0;  // 2 map model (background, sky), 3 object model (piece)
    std::vector<Group> groups;
    std::vector<Bone> bones;
};

Model read_model(const std::uint8_t* data, std::size_t size, std::size_t at);

// --- Pieces ---

// A row of the placement table (u32 8, u32 length, 0x68-byte rows), in KH2's
// world axes (y down, z back: the model's are (x, -y, -z)). A piece hidden until
// an event stands at y 1,000,000.
struct Placement final {
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};  // radians
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
    int piece = 0;    // which BOB, in BAR order
    int motion = -1;  // which of its motions it plays; -1 none
};

std::vector<Placement> read_placements(const std::uint8_t* data, std::size_t size, std::size_t at);

struct MotionKey final {
    float time = 0.0F;  // motion frames (at the motion's fps)
    float value = 0.0F;
    int interpolation = 0;  // 0 constant, 1 linear, 2 Hermite
    float tangent_in = 0.0F;
    float tangent_out = 0.0F;
};

// A joint's channel over time: channels 0-2 scale, 3-5 rotation (radians), 6-8
// translation, x y z each. Cycle types before/after the keys: 0 hold the end key,
// 1 offset, 2 repeat, 3 zero.
struct MotionCurve final {
    int joint = 0;
    int channel = 0;
    int pre = 0;
    int post = 0;
    std::vector<MotionKey> keys;
};

struct PoseValue final {
    int joint = 0;
    int channel = 0;
    float value = 0.0F;
};

// An interpolated motion (the only kind seen in pieces: F-curves, no
// constraints or expressions), in KH2's world axes like the placements.
struct Motion final {
    float start = 0.0F;  // motion frames
    float end = 0.0F;
    float fps = 0.0F;
    float loop_from = 0.0F;  // where it continues after the end
    std::vector<PoseValue> pose;
    std::vector<MotionCurve> curves;
    bool has_constraints = false;  // not played; reported
};

// The motions of a piece's motion BAR, in its order (empty entries skipped).
std::vector<Motion> read_motions(const std::uint8_t* data, std::size_t size, std::size_t at);
float evaluate(const MotionCurve& curve, float time);

// --- Collision ---

// A face of a COCT collision, its corners in the model's axes (the COCT stores
// (x, -y, -z)), quads split in two.
struct CollisionFace final {
    std::array<std::array<float, 3>, 3> corners{};
    std::uint32_t surface = 0;  // the COCT's surface flags
};

std::vector<CollisionFace> read_collision(const std::uint8_t* data, std::size_t size, std::size_t at);

// --- The map ---

// A model and its textures: the background (MAP), a sky (SK0, SK1) or a piece (BOB).
struct MapPart final {
    std::string name;
    std::size_t model_entry = 0;  // indices into MapFile::entries
    std::size_t texture_entry = 0;
    std::optional<std::size_t> motion_entry;  // pieces that move
    int first_hd_picture = 0;                 // its container's first -N.dds
};

struct MapFile final {
    std::vector<BarEntry> entries;
    std::vector<MapPart> parts;  // BAR order: the background first
    std::optional<std::size_t> placements;
    std::optional<std::size_t> collision;  // ID_
    std::vector<std::size_t> radar;
    int hd_pictures = 0;  // the -N.dds the parts' textures take
};

MapFile read_map(const std::uint8_t* data, std::size_t size);

}  // namespace khdays::assets::kh2
