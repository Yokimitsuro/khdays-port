/* The 3D's textures by name: each texture a shown list uses, named from where its bytes came from
 * (origin.h, names.h), written out as a PNG the first time it is seen while the dump is on
 * (KHDAYS_DUMP_GRAPHICS=DIR, or the setting), and drawn from a mod's PNG of that name
 * (mods/<Mod>/graphics/<name>.png, any size) while HD graphics are on. */
#ifndef KHDAYS_TEXTURES_H
#define KHDAYS_TEXTURES_H

#include "../../gfx/g3d.h"

#include <stdint.h>

/* Whether anything wants the textures' names (then origin.c follows the
 * game's data). */
int khdays_textures_wanted(void);

/* A new list is shown: its polygons' textures, named, dumped, their
 * replacements found. */
void khdays_textures_shown(const KhdaysG3dList *list);

/* The frame package's copy of the shown list: each polygon's `replacement`,
 * the texture drawn instead, or -1. */
void khdays_textures_mark(KhdaysG3dList *list);

/* A texture drawn instead of the game's: 8-bit RGBA in a word each (red in
 * the low byte), rows top to bottom. */
typedef struct KhdaysTextureReplacement {
    int width, height;
    const uint32_t *rgba;
    uint32_t info; /* what it is: 0 a picture, else KHDAYS_REPLACEMENT_* */
} KhdaysTextureReplacement;

/* A 2D tile sheet (tiles2d.h): its tiles in rows of `columns` (bits 16-31),
 * each `scale` times the DS's 8x8 (bits 8-15); INDEXED when its texels are
 * the game's palette indices (in their low byte) rather than colours. */
#define KHDAYS_REPLACEMENT_INDEXED 0x1u
#define KHDAYS_REPLACEMENT_TILES 0x2u

/* A bitmap the game wrote (a movie's frame, video.c): its picture drawn in
 * place of the bitmap's pixels at the picture's own size, the bitmap's rows
 * of `units` 32-byte units (bits 8-15) and `rows` rows (bits 16-31). The
 * units drawn instead are numbered from the bitmap's start (tiles2d.h). */
#define KHDAYS_REPLACEMENT_BITMAP 0x4u

/* A texture from a file's data, as the port draws it, into `rgba` (width x
 * height x 4 bytes, the sizes of `param`): `param` its TEXIMAGE_PARAM (the
 * place is ignored), `texels` its data, `index4x4` a 4x4 texture's palette
 * indices (format 5; else NULL), `palette` its palette (NULL for format 7).
 * For the dump without a game (--dump-graphics). */
void khdays_textures_decode(uint32_t param, const uint8_t *texels, uint32_t texel_bytes, const uint8_t *index4x4,
                            uint32_t index_bytes, const uint8_t *palette, uint32_t palette_bytes, uint8_t *rgba);

/* `rgba` (width x height) as a PNG at `path`, its directories made; says why
 * not. */
int khdays_textures_write_png(const char *path, const uint8_t *rgba, int width, int height);

/* Where what is drawn is written out now (KHDAYS_DUMP_GRAPHICS=DIR, or the
 * video settings' "Dump graphics"), or "" when nothing is. */
const char *khdays_textures_dump_dir(void);

/* Whether the replacements are drawn: the video settings' "HD graphics",
 * or KHDAYS_HD_GRAPHICS=1 without a window. */
int khdays_textures_hd(void);

/* `rgba` (8-bit RGBA, red first, rows top to bottom; kept, never freed) as
 * a texture drawn instead under `name` -- a mod's model's own (models.h):
 * its id, the one already added under that name, or -1 when there is no room. */
int khdays_textures_add(const char *name, int width, int height, const uint8_t *rgba);

/* The same for texels of any kind, with what they are (`info`). */
int khdays_textures_add_info(const char *name, int width, int height, const uint32_t *texels, uint32_t info);

/* Indices (one a pixel) as an indexed PNG at `path`, its directories made,
 * with `colours` RGB entries of `palette`, index 0 transparent; says why
 * not. */
int khdays_textures_write_indexed_png(const char *path, const uint8_t *indices, int width, int height,
                                      const uint8_t *palette, int colours);

/* How many there are, and each one -- for the renderer, on any thread: one
 * counted here never changes, but for a changing one's texels. */
int khdays_textures_replacements(void);
const KhdaysTextureReplacement *khdays_textures_replacement(int id);
/* The name replacement `id` was found under (the texture's: names.h), or NULL. */
const char *khdays_textures_name(int id);

/* A replacement whose texels the runtime changes as the game runs (a movie's frame: video.c), zero at
 * first: written between khdays_textures_change_begin and _end, which moves its version; read on
 * another thread between khdays_textures_read_begin and _end. One that never changes has version 0. */
int khdays_textures_add_changing(const char *name, int width, int height, uint32_t info);
uint32_t *khdays_textures_change_begin(int id);
void khdays_textures_change_end(int id);
uint32_t khdays_textures_version(int id);
void khdays_textures_read_begin(int id);
void khdays_textures_read_end(int id);

#endif
