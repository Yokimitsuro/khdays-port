#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Material colour animation (Nitro BMA0 / NSBMA): per material, keyframed
// diffuse, ambient, specular and emission colours plus the polygon alpha.
// Decoded to a neutral form: sampling returns colours and alpha in 0..1.
//
// Evaluation reproduces NitroSystem's nsbma.c exactly as the decompilation
// has it -- func_01ff9fd4 (colour, including the step-2/step-4 RGB555
// interpolation) and func_02018830 (alpha), called per material by
// GetMatColAnm_ (libs/nns/g3d/calls/func_020189f0.c).
namespace khdays::assets {

struct MaterialColorState final {
    std::array<float, 3> diffuse{};
    std::array<float, 3> ambient{};
    std::array<float, 3> specular{};
    std::array<float, 3> emission{};
    // Polygon alpha. The DS does not draw a material whose alpha is 0
    // (func_01ffbbf0 flags it transparent and its shapes are skipped).
    float alpha = 1.0F;
};

struct MaterialColorAnimation final {
    std::string name;
    std::uint16_t frame_count = 0;

    struct Track final {
        std::string material;
        // diffuse, ambient, specular, emission, polygon alpha: each either a
        // constant or an offset into `block` plus a sampling step.
        std::array<std::uint32_t, 5> info{};
    };
    std::vector<Track> tracks;

    // The animation block the track offsets are relative to.
    std::vector<std::uint8_t> block;
};

// Decode animation `index` of a BMA0 resource. Throws on malformed data.
MaterialColorAnimation load_nsbma(
    const std::uint8_t* data, std::size_t size, std::size_t index = 0);

// The material's state at a whole `frame` (clamped to the animation), or
// nullopt when the animation does not address `material`. Throws when a track
// points outside the animation block.
std::optional<MaterialColorState> sample_material_color(
    const MaterialColorAnimation& animation, std::string_view material,
    std::uint32_t frame);

}  // namespace khdays::assets
