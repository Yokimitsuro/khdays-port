/* A BG layer of the DS's composition, as the parts read it: its
 * registers as the game set them, and its map's cells and the characters they
 * show in the engines' BG memory. */
#ifndef KHDAYS_UI_BG_H
#define KHDAYS_UI_BG_H

#include <stdint.h>

/* A text layer, as its registers have it now. */
typedef struct KhdaysBgLayer {
    uint32_t map, chars; /* in the engine's BG memory */
    int wt, ht;          /* its map's cells across and down (32 or 64) */
    int hofs, vofs;      /* its scroll */
    int colors256;       /* 8-bit characters */
    int priority;
} KhdaysBgLayer;

/* The engine's BG memory at `address`, or NULL where no bank is mapped. */
const uint8_t *khdays_bg_memory(int engine, uint32_t address);

/* Where BG `bg`'s characters start in the engine's BG memory, as its
 * registers say now (G2_GetBGnCharPtr's sum). */
uint32_t khdays_bg_chars_base(int engine, int bg);

/* Reads engine `engine`'s BG `bg` into `l` and returns 1 when it is shown and
 * is a text layer (by gfx/describe.c's rules); otherwise returns 0. */
int khdays_bg_text_layer(int engine, int bg, KhdaysBgLayer *l);

/* The 16-bit map entry of cell (cx, cy), wrapped to the map. */
uint32_t khdays_bg_map_entry(int engine, const KhdaysBgLayer *l, int cx, int cy);

/* Whether the character `entry` names shows a pixel that is not 0. */
int khdays_bg_cell_shows(int engine, const KhdaysBgLayer *l, uint32_t entry);

/* The file the bytes at `address` of the engine's BG memory came from
 * (runtime/core/origin.h; only while the names are followed), or NULL. */
const char *khdays_bg_origin(int engine, uint32_t address);
/* Source and contiguous byte range of resident BG data; false where no file is known. */
int khdays_bg_source(int engine, uint32_t address, const char **file, uint32_t *offset, uint32_t *run);

#endif
