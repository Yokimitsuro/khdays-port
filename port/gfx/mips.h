/* A still picture's smaller copies (mipmaps), each half the one before down to 1 x 1, one after the other:
 * made by the renderer as it uploads a picture (host/render/gpu_hd.c) or ahead by whoever adds it
 * (KHDAYS_REPLACEMENT_SMALLER, runtime/mods/textures.h). */
#ifndef KHDAYS_GFX_MIPS_H
#define KHDAYS_GFX_MIPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The 32-bit texels the smaller copies of a w x h picture take together. */
uint32_t khdays_mips_words(uint32_t w, uint32_t h);

/* The smaller copies of `rgba` (w x h, 8-bit RGBA, red first) into `out` (khdays_mips_words texels): each
 * texel the four under it, their colours weighted by their alpha (a clear texel does not darken its
 * neighbours). */
void khdays_mips_make(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif
