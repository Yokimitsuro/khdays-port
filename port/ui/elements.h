/* The port's elements: what
 * the parts draw on each screen this frame, each in a slot of the DS's
 * composition and placed by the layout, and the images they are drawn
 * with. */
#ifndef KHDAYS_UI_ELEMENTS_H
#define KHDAYS_UI_ELEMENTS_H

#include "../gfx/elements.h"

#include <stdint.h>

/* One element of a part this frame. */
typedef struct KhdaysUiElement {
    const char *id;   /* "<part>.<name>", the layout's key (static storage) */
    int screen;       /* 0 engine A's, 1 engine B's */
    uint32_t slot;    /* KE_SLOT: KES_* */
    float x, y, w, h; /* its DS place: where the game's drawing of it is, DS pixels */
    int image;        /* from khdays_ui_image_get: its texels cover the place */
} KhdaysUiElement;

/* A part's elements are asked for at each VBlank whose frame is drawn (and
 * only while the part is not off). Its `emit` gives them with khdays_ui_emit,
 * and with khdays_ui_left_out_* what the game's own drawing of it covers
 * (which is then left out of its slot). */
void khdays_ui_register(int part, void (*emit)(void));
void khdays_ui_emit(int part, const KhdaysUiElement *element);
/* Move a supplementary image with a queued anchor, including when that anchor
 * follows a HUD piece rather than a layout line. */
void khdays_ui_emit_following(int part, const KhdaysUiElement *element, const KhdaysUiElement *anchor);

/* For a part drawn from the port's own state (the pause's Retry cursor): when that state changes, the
 * next VBlank composes the frame again even if the game's picture is the same. */
void khdays_ui_changed(void);
int khdays_ui_take_changed(void);
/* cell (cx, cy) of text layer `layer`'s map (0-63 each, gfx/elements.h
 * KE_CELLS), or sprite `index` of the screen's 128 */
void khdays_ui_left_out_cell(int part, int screen, int layer, int cx, int cy);
void khdays_ui_left_out_obj(int part, int screen, int index);
/* The same for a cell whose picture the window draws apart from the composition (the Menu Overhaul's
 * pause): also left out of engine A's display capture, which the game shows again in its screen blends
 * (an event's camera cuts), and its layer left to it: the other parts' elements in that layer are not
 * drawn (a scene's text canvas, whose characters the game's pause has taken). */
void khdays_ui_left_out_cell_captured(int part, int screen, int layer, int cx, int cy);
const uint8_t *khdays_ui_captured_cell_bits(int screen);
/* Whether a part that is not off already stands for cell (cx, cy) of text
 * layer `layer` this frame (the parts emit in their order, the text first). */
int khdays_ui_cell_taken(int screen, int layer, int cx, int cy);

/* An image of `width` x `height` texels (KET_* or colours), cleared. An
 * image of that size given back earlier is used again. Its texels are
 * written between _begin and _end. */
int khdays_ui_image_get(int width, int height);
void khdays_ui_image_put(int image);
uint32_t *khdays_ui_image_begin(int image);
void khdays_ui_image_end(int image);

/* Palette utilities for native HD glyph overlays; colors are AABBGGRR. */
uint32_t khdays_ui_palette_colour(int engine, uint32_t slot, uint32_t indexed);
uint32_t khdays_ui_palette_hash(int engine);

/* The scale the images are drawn at: the internal resolution's (1 without a window). */
int khdays_ui_scale(void);

/* At VBlank, gathers the elements of every part that is not off and applies the layout
 * (`wide_margin`: engine A's columns beside the DS's on each side, 0 at 4:3; `upper`: the engine on
 * the upper screen). An element placed on the other screen is drawn in direct colour from its own
 * engine's palettes; one no line places moves with the one it lies inside. */
void khdays_ui_collect(int wide_margin, int upper);

/* This frame's records (KE_WORDS words each, the topmost of a slot first):
 * those of the parts that are on (`list` 0), or of every part that is not off
 * (`list` 1, with the checked ones at their DS place). Also what the checked
 * parts' game drawing covers on the screen, as bit sets: one bit per cell of
 * the text layers' maps and one per sprite (NULL when nothing). */
const uint32_t *khdays_ui_records(int screen, int list, uint32_t *count);
const uint8_t *khdays_ui_left_out_cell_bits(int screen, int list);
const uint8_t *khdays_ui_left_out_obj_bits(int screen, int list);

/* The rectangle around the checked parts' elements on the screen this frame
 * (DS pixels, [x0, x1) x [y0, y1)). Returns 0 when there are none. */
int khdays_ui_checked_bounds(int screen, int *x0, int *y0, int *x1, int *y1);

/* The id of the topmost element of `list` whose place covers DS point
 * (x, y), or NULL: for the check's reports. */
const char *khdays_ui_element_at(int screen, int list, float x, float y);

/* An image's texels by its KE_IMAGE, for the composition on the CPU. */
const uint32_t *khdays_ui_texels(uint32_t image);

#endif
