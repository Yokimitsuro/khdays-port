/* The replacement graphics (runtime/mods/textures.h) on the graphics card: the mods' 3D textures, 2D
 * tile sheets and models' textures in one buffer both passes read -- a table of four words per
 * replacement, then the texels, a still picture's followed by smaller copies (info bit 0x8)
 * (docs/RENDERING.md, "On the graphics card"). */
#ifndef KHDAYS_GPU_HD_H
#define KHDAYS_GPU_HD_H

#include "../../gfx/g3d.h"

#include <SDL3/SDL.h>

/* Creates the buffer on `device`. Opening it again on the same device does nothing. */
int khdays_gpu_hd_open(SDL_GPUDevice *device);
void khdays_gpu_hd_close(void);

/* Uploads the replacements the card does not have yet, and the ones whose
 * pixels changed, in a copy pass of their own in `cmd` (call it before the
 * passes that read them). When the buffer must grow a larger one is made and
 * what the card had is copied over on the card. The 3D's pictures
 * (KHDAYS_REPLACEMENT_ON_DEMAND) are uploaded only when polygons drawn use
 * them (khdays_gpu_hd_sync_drawn); past the most one buffer holds (4 GB: the
 * shaders' byte addresses are 32 bits) those not drawn in this picture leave
 * the card, the least lately drawn first, and come back when drawn. A picture
 * not on the card reads as one white texel. Returns 0 if the upload failed. */
int khdays_gpu_hd_sync(SDL_GPUCommandBuffer *cmd);

/* The same before drawing `polys`: their on-demand pictures put on the card. */
int khdays_gpu_hd_sync_drawn(SDL_GPUCommandBuffer *cmd, const KhdaysG3dPolygon *polys, int32_t npolys);

/* A new picture is being drawn: what the last one drew may leave the card. */
void khdays_gpu_hd_next_frame(void);

/* Whether replacement `id`'s texels are on the card (a pass that writes them -- kh2_sequence_gpu.c -- must
 * not write through the white texel's entry of one that is not). */
int khdays_gpu_hd_on_card(uint32_t id);

/* For the tests: the most the buffer holds lowered to `bytes` (0: the most, 4 GB), before opening it. */
void khdays_gpu_hd_set_most(uint64_t bytes);

SDL_GPUBuffer *khdays_gpu_hd_buffer(void);

#endif
