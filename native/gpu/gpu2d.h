/* The DS's two 2D display engines (GBATEK, "DS Video" and the GBA's "LCD"
 * chapters it builds on). Pure: it reads the hardware state it is given and
 * writes pixels, so the game runtime and the savestate test share it. */
#ifndef KHDAYS_GPU2D_H
#define KHDAYS_GPU2D_H

#include "vram_map.h"

#include <stdint.h>

#define KHDAYS_SCREEN_W 256
#define KHDAYS_SCREEN_H 192

typedef struct KhdaysGpuInput {
    const uint8_t *io;       /* the ARM9 I/O registers from 0x04000000 (engine B's at +0x1000) */
    const uint8_t *palette;  /* 0x05000000, 2 KB */
    const uint8_t *oam;      /* 0x07000000, 2 KB */
    const KhdaysVramPages *vram;
    const uint8_t *bank[4];  /* banks A-D, for the VRAM display mode */
    /* The 3D engine's output for BG0: 256x192, per pixel RGB 6-bit each in
     * bits 0-5/8-13/16-21 and the alpha (0-31) in bits 24-28; alpha 0 is
     * transparent. NULL: no 3D image (the layer is transparent). */
    const uint32_t *layer3d;
} KhdaysGpuInput;

/* What an engine keeps from one frame to the next. */
typedef struct KhdaysGpu2dState {
    uint8_t win_y[2];  /* WIN0/WIN1 vertically active (GBATEK: "DS Window Glitches") */
} KhdaysGpu2dState;

/* Renders one engine's frame (0 = A, 1 = B) into `out` as 0x00RRGGBB; if
 * `out555` is not NULL, also as BGR555 (the 6-bit result halved) -- what the
 * savestate test compares with DeSmuME's framebuffer. */
void khdays_gpu2d_frame(const KhdaysGpuInput *in, KhdaysGpu2dState *state, int engine,
                        uint32_t *out, uint16_t *out555);

#endif
