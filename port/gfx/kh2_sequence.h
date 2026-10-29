#pragma once

#include "khdays/assets/kh2_sequence.h"
#include "khdays/assets/texture.h"
#include "sequence_image.h"

#include <span>

namespace khdays::render::kh2 {

struct SequenceView final {
    float x = 0, y = 0, scale = 1;
};

// Straight-alpha RGBA composition for the native face over the CPU reference gauge.
std::uint32_t source_over(std::uint32_t under, std::uint32_t top);

void encode_sequence_quads(KhdaysSequenceImage &image, std::span<const assets::kh2::SequenceQuad> quads);

// Reference rasterizer: ordered affine quads, source-alpha/add/subtract blending,
// and depth-only masks. Coordinates and UVs use the original IMGD's logical size.
void draw_sequence(std::span<const assets::kh2::SequenceQuad> quads, const assets::DecodedTexture &atlas,
                   int logical_width, int logical_height, std::span<std::uint32_t> rgba, int width, int height,
                   const SequenceView &view = {});

} // namespace khdays::render::kh2
