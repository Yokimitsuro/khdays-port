#pragma once

#include <cstddef>

namespace khdays::assets::ps2 {

// Where the PS2's GS keeps a pixel in its local memory (4 MB of 32-bit words), for the pictures KH2's files hold as
// that memory has them. PSMCT32 pages are 64 x 32 pixels of 32 blocks of 8 x 8; PSMT8 pages 128 x 64 of 16 x 16
// blocks numbered the same, each block the same 256 bytes as a PSMCT32 block. A buffer starts at block `bp` (64
// words each) and its width `bw` counts 64 pixels. The addresses are not wrapped at the memory's end.

inline constexpr unsigned kGsBlocks[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21},
                                             {2, 3, 6, 7, 18, 19, 22, 23},
                                             {8, 9, 12, 13, 24, 25, 28, 29},
                                             {10, 11, 14, 15, 26, 27, 30, 31}};
inline constexpr unsigned kGsColumns[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13},       {2, 3, 6, 7, 10, 11, 14, 15},
                                              {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
                                              {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47},
                                              {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};

inline std::size_t gs_word(const unsigned bp, const unsigned page, const unsigned block, const unsigned column) {
    return static_cast<std::size_t>(bp) * 64U + static_cast<std::size_t>(page) * 2048U + block * 64U + column;
}

// the 32-bit word holding pixel (x, y) of a PSMCT32 buffer
inline std::size_t ct32_word(const unsigned x, const unsigned y, const unsigned bp, const unsigned bw) {
    const unsigned page = (y / 32U) * bw + x / 64U;
    return gs_word(bp, page, kGsBlocks[(y % 32U) / 8U][(x % 64U) / 8U], kGsColumns[y % 8U][x % 8U]);
}

// the byte (its word times 4 and its byte in it) holding pixel (x, y) of a PSMT8 buffer
inline std::size_t t8_byte(const unsigned x, const unsigned y, const unsigned bp, const unsigned bw) {
    const unsigned page = (y / 64U) * (bw / 2U) + x / 128U;
    // within a 16 x 16 block: which 32-bit word of the block's 8 x 8, and which of its bytes
    const unsigned bx = x % 16U, by = y % 16U;
    const unsigned swap = ((by + 2U) >> 2U & 1U) * 4U;
    const unsigned wy = ((by & ~3U) >> 1U) + (by & 1U);
    const unsigned wx = (bx + swap) & 7U;
    const unsigned byte = (by >> 1U & 1U) + (bx >> 2U & 2U);
    return gs_word(bp, page, kGsBlocks[(y % 64U) / 16U][(x % 128U) / 16U], kGsColumns[wy & 7U][wx]) * 4U + byte;
}

}  // namespace khdays::assets::ps2
