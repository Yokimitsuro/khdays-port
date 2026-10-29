/* The names of the game's graphics, from where their bytes came from (origin.h): a file as origin.c
 * names it ("mi/wd/wd_tt@1600": the packed entry at 0x1600; a plain path: the whole file), read and
 * decompressed as the game reads it, and the dictionaries of its NitroSystem resources. A texture's
 * name: "<file>[#<entry>]/tex/<texture>[@<palette>]" (a second of the same name in the same data gets
 * "~2"). */
#ifndef KHDAYS_NAMES_H
#define KHDAYS_NAMES_H

#include "../../hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The texture whose data starts at `offset` into `file`'s data (for a 4x4
 * compressed one, format 5, its 4x4 data), with the palette that starts at
 * `pltt_offset` into `pltt_file`'s (`pltt_file` NULL: no palette, or none
 * known), named into `out`. 0 if no texture starts there. */
int khdays_names_texture(const char *file, u32 offset, int format, const char *pltt_file, u32 pltt_offset, char *out,
                         int size);

/* A 2D tile sheet: an NCGR's `tiles` of `tile_bytes` (32 or 64) from `tiles_at` into its file's data,
 * `columns` to a row of its image (the NCGR's width when it holds them all, else 16 or fewer); `data`
 * its tiles' bytes as the ROM holds them, kept while the port runs. */
typedef struct KhdaysTileSheet {
    u32 tiles_at, tile_bytes, tiles, columns;
    const u8 *data;
} KhdaysTileSheet;

/* The tile sheet whose tiles hold `offset` into `file`'s data, named into
 * `out`: "<file>[#<entry>]/chr/<n>", the data's n-th NCGR (from 0). 0 if
 * none does. */
int khdays_names_tiles(const char *file, u32 offset, char *out, int size, KhdaysTileSheet *sheet);

/* That sheet written out as the dump writes it, under `directory`; 0 if
 * none or it could not be written. */
int khdays_names_dump_sheet(const char *file, u32 offset, const char *directory);

/* The model whose data (NNSG3dResMdl) starts at `offset` into `file`'s data,
 * named into `out`: "<file>[#<entry>]/mdl/<model>", the model's name as its
 * MDL0 dictionary gives it -- the name the dump writes it under. 0 if no
 * model starts there. */
int khdays_names_model(const char *file, u32 offset, char *out, int size);

/* The joint animation whose data (NNSG3dResJntAnm) starts at `offset` into `file`'s data: its name as its
 * BCA0's JNT0 dictionary gives it ("b_ro_A001"), into `out`. 0 if none starts there. */
int khdays_names_animation(const char *file, u32 offset, char *out, int size);

/* khdays-native --dump-graphics [DIR] (default saves/dump/graphics), without the game running: every
 * texture of every NitroSystem resource in the ROM as DIR/<name>.png (with each palette the models'
 * materials use, else the first), every model as <name>.gltf, every 2D tile sheet as an indexed
 * <name>.png. Inside a repository only under its saves/. Returns the exit code. */
#define KHDAYS_GRAPHICS_DUMP_DIRECTORY "saves/dump/graphics"
int khdays_graphics_dump(const char *directory);

#ifdef __cplusplus
}

#include <cstddef>
#include <vector>

/* For the models (models.cpp): the BMD0 that holds the model at `offset`
 * into `file`'s data, read from the ROM, and the model's index in it. */
bool khdays_names_model_bmd0(const char *file, u32 offset, std::vector<unsigned char> &bmd0, std::size_t &index);
#endif

#endif
