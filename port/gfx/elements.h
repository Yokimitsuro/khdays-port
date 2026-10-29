/* A screen's elements: things the port draws itself, each in one slot of the DS's composition (a BG
 * layer, or the sprites at a priority), with an image drawn at the internal resolution and a place in
 * DS pixels; KD_ELEMENTS says how many follow, KE_WORDS words each, the topmost of a slot first. Plain
 * C, only #defines: the shaders include it too (docs/RENDERING.md). */
#ifndef KHDAYS_GFX_ELEMENTS_H
#define KHDAYS_GFX_ELEMENTS_H

#define KE_WORDS 8

#define KE_SLOT 0            /* KES_* */
#define KES_NUMBER_MASK 0x3u /* the BG layer (0-3), or the sprites' priority */
#define KES_SPRITES 0x4u     /* in the sprites' slot of that priority, else the BG layer's */
#define KES_DIRECT                                                                                                     \
    0x8u                  /* texels are colours (RGBA: in a layer's slot their alpha shows, over the
                               * elements and the picture under them -- gfx/compose.h kd_element_layer;
                               * in the sprites' from half their alpha), else palette indices */
#define KES_SEMI_SHIFT 4  /* sprites: 1 drawn as a semi-transparent sprite (the DS's mode 1) */
#define KES_FINE 0x20u    /* its image is finer than the DS's pixels (a mod's font): the frame is drawn at full size */
#define KES_PICTURE 0x40u /* on one screen, an image placed independently of the screen's crop */
#define KES_HUD 0x80u     /* independent field HUD image: follows the one-screen situation's HUD visibility */
#define KES_NATIVE_FADE 0x100u  /* follow the native layer's fade without requiring the field HUD */
#define KES_PAUSE_LAYOUT 0x200u /* independently positioned pause pieces, centered on one screen */

#define KE_X 1     /* float bits: its left edge, DS pixels (fractions allowed) */
#define KE_Y 2     /* its top */
#define KE_W 3     /* float bits: its drawn width, DS pixels */
#define KE_H 4     /* its drawn height */
#define KE_IMAGE 5 /* its image: the replacement (runtime/mods/textures.h) holding its texels */
#define KE_IW 6    /* the image's width and height in texels */
#define KE_IH 7

/* An image's texels: 0 transparent; indexed, KET_OPAQUE | the palette index (with KET_EXTENDED an
 * 8-bit character's index in the extended palette bits 12-15 name); direct colour, RGBA with red in
 * bits 0-7. */
#define KET_OPAQUE 0x100u
#define KET_EXTENDED 0x200u
#define KET_EXT_PAL_SHIFT 12

/* What the parts stand for, left out of the composition (the description's
 * KD_LEFT_OUT): a bit per cell of each text layer's map -- cell (cx, cy) of
 * layer l bit l * KE_CELLS + cy * 64 + cx, the cells of the 512x512 dots a
 * map can cover, a smaller map's in its corner -- then one per sprite. */
#define KE_CELLS 4096u
#define KE_LEFT_CELL_WORDS (4u * KE_CELLS / 32u)
#define KE_LEFT_OBJ_WORDS (128u / 32u)

#endif
