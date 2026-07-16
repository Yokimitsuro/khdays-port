#pragma once

#include <cstddef>
#include <cstdint>

#include "khdays/assets/tex0.h"  // DecodedTexture

namespace khdays::assets {

// The colour stage of the DS's MobiClip video decoder.
//
// The codec is **YCoCg, not YUV** — treating it as YUV yields a colour cast that
// no matrix or range fixes, because they are different colour spaces. The DS's
// inverse, reproduced here exactly:
//
//     t = Y - Cg;   R = t + Co;   G = Y + Cg;   B = t - Co
//
// each term saturated to 5 bits through a 768-byte table that is exactly
// `clamp(x >> 3, 0, 31)` over x in [-256, 511] (the padding absorbs chroma
// over/underflow without a branch), packed into BGR555 with bit 15 set.
//
// **Plane layout:** `luma` is height x 256. `chroma` is height/2 rows of a full
// 256 bytes each — 128 bytes of Co followed by 128 bytes of Cg — so the Cg of a
// pixel is 0x80 further along the *same* row, not the next one.
//
// This was verified byte-exact (81,920 / 81,920) against a real frame captured
// out of the DS's VRAM.
namespace mobiclip {

// The DS's own output: BGR555, bit 15 set, written at `stride_pixels` per row.
// `dst` must hold at least stride_pixels * height u16.
void frame_to_bgr555(const std::uint8_t* luma, const std::uint8_t* chroma,
                     int width, int height, std::uint16_t* dst,
                     int stride_pixels);

// The neutral form the engine consumes.
//
// `ds_exact` reproduces the DS pixel-for-pixel: 5-bit colour plus the 2x2
// checkerboard dither the hardware needs to hide 5-bit banding. That dither is
// **not optional** for fidelity — dropping it changes 24% of pixels.
//
// With `ds_exact = false` the conversion keeps full 8-bit precision and skips
// the dither, which is strictly better on a modern display: the dither only
// existed to compensate for the DS's 5-bit output.
DecodedTexture frame_to_rgba(const std::uint8_t* luma, const std::uint8_t* chroma,
                             int width, int height, bool ds_exact = false);

}  // namespace mobiclip
}  // namespace khdays::assets
