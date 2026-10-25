/* The DS's two 2D display engines (GBATEK, "DS Video" and the GBA's "LCD"
 * chapters it builds on). Pure: it reads the hardware state it is given and
 * writes pixels, so the game runtime and the savestate test share it. */
#ifndef KHDAYS_GPU2D_H
#define KHDAYS_GPU2D_H

#include "vram_map.h"

#include <stdint.h>
#include <stdio.h>

#define KHDAYS_SCREEN_W 256
#define KHDAYS_SCREEN_H 192

typedef struct KhdaysGpuInput {
    const uint8_t *io;      /* the ARM9 I/O registers from 0x04000000 (engine B's at +0x1000) */
    const uint8_t *palette; /* 0x05000000, 2 KB */
    const uint8_t *oam;     /* 0x07000000, 2 KB */
    const KhdaysVramPages *vram;
    const uint8_t *bank[4]; /* banks A-D, for the VRAM display mode */
    /* The 3D engine's output for BG0: 256x192, per pixel RGB 6-bit each in
     * bits 0-5/8-13/16-21 and the alpha (0-31) in bits 24-28; alpha 0 is
     * transparent. NULL: no 3D image (the layer is transparent). */
    const uint32_t *layer3d;
    /* Engine A's text layers' cells drawn transparent: a bit per cell, cell (cx, cy) of layer l bit
     * l * 4096 + cy * 64 + cx (gfx/elements.h KE_CELLS), what a picture the window draws apart stands
     * for (ui/elements.h khdays_ui_left_out_cell_captured). NULL: none. */
    const uint8_t *left_out_cells;
} KhdaysGpuInput;

/* What an engine keeps from one frame to the next. */
typedef struct KhdaysGpu2dState {
    uint8_t win_y[2]; /* WIN0/WIN1 vertically active (GBATEK: "DS Window Glitches") */
} KhdaysGpu2dState;

/* How a pixel of engine A would come out with the 3D (BG0) opaque there,
 * and with it transparent -- so a 3D drawn at a higher resolution can be
 * composed as the 2D engine composes it (khdays_gpu2d_recompose). Colours
 * are BGR555, before master brightness. */
enum {
    KHDAYS_2D_3D_HIDDEN = 0, /* the 3D cannot show here: the pixel is `under` either way */
    KHDAYS_2D_3D_TOP = 1,    /* the 3D is the top layer; `other` is the layer below it */
    KHDAYS_2D_3D_SECOND = 2, /* the 3D is the second layer; `other` is the layer above, `effect` the top's */
};
enum {
    KHDAYS_2D_EFFECT_NONE = 0,
    KHDAYS_2D_EFFECT_BLEND = 1,    /* by EVA and EVB */
    KHDAYS_2D_EFFECT_BRIGHTEN = 2, /* by EVY */
    KHDAYS_2D_EFFECT_DARKEN = 3,
};
typedef struct KhdaysGpu2dComposition {
    uint16_t under;       /* the pixel with the 3D transparent */
    uint16_t other;       /* the layer the 3D pairs with */
    uint8_t role;         /* KHDAYS_2D_3D_* */
    uint8_t alpha_target; /* TOP: the layer below is a second target (a 3D alpha under 31 blends by it) */
    uint8_t effect;       /* KHDAYS_2D_EFFECT_*: what applies otherwise */
    uint8_t unused;
} KhdaysGpu2dComposition;

/* A frame's blending coefficients and master brightness, per engine. */
typedef struct KhdaysGpu2dInfo {
    uint8_t eva, evb, evy;
    uint8_t is3d;           /* engine A shows the 3D on BG0 this frame (and is not blanked) */
    uint16_t master_bright; /* MASTER_BRIGHT */
    uint16_t bg0_hofs;      /* the 3D layer's horizontal scroll (over 512 dots) */
    uint16_t backdrop;      /* the backdrop colour (BG palette 0) */
    uint16_t bldcnt;        /* BLDCNT: which layers blend, and how */
} KhdaysGpu2dInfo;

/* The 2D engine's colour for a pixel whose composition record is `rec`,
 * given the 3D there (BGR555 and alpha 0-31): what compose_line gives. */
uint16_t khdays_gpu2d_recompose(const KhdaysGpu2dComposition *rec, uint16_t color3d, unsigned alpha3d,
                                const KhdaysGpu2dInfo *info);

/* A composed colour as the screen shows it (0x00RRGGBB): the master
 * brightness applied on the 6-bit intensities. */
uint32_t khdays_gpu2d_finish(uint16_t color, const KhdaysGpu2dInfo *info);

/* Widescreen, engine A: the composition records over `columns` DS pixels, the DS's 256 in the middle,
 * the HUD's pieces (touching 8x8 cells of layers 1-3) moved to the edge of the half they lie in, a
 * piece across the middle staying (docs/RENDERING.md). Filled by khdays_gpu2d_frame_ex. */
#define KHDAYS_GPU2D_MAX_COLUMNS 768
#define KHDAYS_GPU2D_MAX_PIECES 64
typedef struct KhdaysGpu2dPiece {
    uint8_t x0, y0, x1, y1; /* its cells, DS columns and lines / 8 */
    int8_t side;            /* -1 moved left, 0 stays, 1 moved right */
    uint8_t unused[3];
} KhdaysGpu2dPiece;
typedef struct KhdaysGpu2dWide {
    int columns;                     /* 256 < columns <= KHDAYS_GPU2D_MAX_COLUMNS, even */
    KhdaysGpu2dComposition *records; /* columns x 192 */
    int pieces;                      /* what it found, for KHDAYS_TRACE_2D */
    KhdaysGpu2dPiece piece[KHDAYS_GPU2D_MAX_PIECES];
    int check;      /* also make each record the long way ... */
    int mismatches; /* ... and count those the shortcuts got otherwise */
} KhdaysGpu2dWide;

/* Renders one engine's frame (0 = A, 1 = B) into `out` as 0x00RRGGBB; if
 * `out555` is not NULL, also as BGR555 (the 6-bit result halved) -- what the
 * savestate test compares with DeSmuME's framebuffer. */
void khdays_gpu2d_frame(const KhdaysGpuInput *in, KhdaysGpu2dState *state, int engine, uint32_t *out, uint16_t *out555);

/* The same, and when `records` (256x192) is not NULL engine A's
 * composition records where it shows the 3D, when `wide` is not NULL its
 * widescreen records too, and when `info` is not NULL the frame's blending
 * and brightness. */
void khdays_gpu2d_frame_ex(const KhdaysGpuInput *in, KhdaysGpu2dState *state, int engine, uint32_t *out,
                           uint16_t *out555, KhdaysGpu2dComposition *records, KhdaysGpu2dWide *wide,
                           KhdaysGpu2dInfo *info);

/* Engine A's BG, 3D and OBJ composed as display mode 1 shows them, whatever the display mode, before
 * the master brightness, as BGR555: the display capture's source A. `state` is the windows' state at
 * the frame's start; it is left as it is. */
void khdays_gpu2d_graphics(const KhdaysGpuInput *in, const KhdaysGpu2dState *state, uint16_t *out555);

/* A frame not drawn (it shows the same as the one before): only what an
 * engine carries into the next frame -- the windows' vertical flags --
 * moves on as if it had been. */
void khdays_gpu2d_skip_frame(const KhdaysGpuInput *in, KhdaysGpu2dState *state, int engine);

/* What an engine shows, as text (KHDAYS_TRACE_2D): its display control,
 * blending and each BG layer (on or off, kind, priority, scroll), then every
 * OBJ on the screen (not disabled, not parked off it) -- its OAM index,
 * position, size, tiles, palette, priority and mode -- one per line. */
void khdays_gpu2d_describe(const KhdaysGpuInput *in, int engine, FILE *out);

#endif
