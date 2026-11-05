/* The 3D's texture memory past the DS's: a model or texture file whose textures and palettes no longer fit
 * the DS's 512 KB of texture VRAM (its 3D texture palettes, 96 KB) is set up in a bank of its own -- texture
 * and palette memory of the PC's, as empty as the DS's after a reset -- instead of being left untextured (a
 * mission of many enemy kinds turned the room white). The game binds the file to its places there as it
 * would to the DS's; the polygons a draw of it sends are marked with its bank, and the frame package draws
 * them with the texture decoded from the bank (the replacements' path, textures.h). */
#ifndef KHDAYS_TEXTURE_BANKS_H
#define KHDAYS_TEXTURE_BANKS_H

#include "../../gfx/g3d.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* G3dRes_DefaultSetup's start for `file` (an NSBMD or NSBTX): a bank of an older file at its place is
 * dropped, and the DS's texture and palette VRAM managers' states kept (khdays_texture_banks_open). */
void khdays_texture_banks_file(void *file);

/* The file's VRAM could not be found: the managers as before its allocations, then as empty as after a
 * reset, for the allocations that follow (the game's own). 1 on success. */
int khdays_texture_banks_open(void);

/* After those allocations and the keys set in the file's texture block `tex` (its NNSG3dResTex):
 * `allocated` whether they all succeeded; then the textures and palettes are copied into a new bank for
 * `file` (marked loaded, as the game's Tex_LoadVram and Gfx_UploadBlock mark them), and the managers'
 * states are the DS's again. */
void khdays_texture_banks_close(void *file, void *tex, int allocated);

/* NNSi_G3dDrawInternal's start for `model` (its NNSG3dResMdl), and its end (and a single shape's draw):
 * the textures sent in between are in the bank of the file holding `model` (0 the DS's VRAM). */
void khdays_texture_banks_draw_begin(const void *model);
void khdays_texture_banks_draw_end(void);

/* Tex0_GetTexPlttParams' PLTT_BASE for `file` (the game's own TEXIMAGE_PARAM and PLTT_BASE of its first
 * texture, which the field's prize pictures are drawn with outside a model's draw): `pltt` with the file's
 * bank above the DS's bits (g3d.h KHDAYS_G3D_PLTT_BANK_TAG) when the file is in a bank, else as it is. */
uint32_t khdays_texture_banks_tag_pltt(const void *file, uint32_t pltt);

/* The replacement (textures.h) a polygon of bank `bank` with TEXIMAGE_PARAM `param` and PLTT_BASE `pltt`
 * is drawn with: the bank's texture decoded (or a mod's for its name while HD graphics are on); -1 when
 * the bank is gone or the polygon is untextured. */
int32_t khdays_texture_banks_replacement(int32_t bank, uint32_t param, uint32_t pltt);

#ifdef __cplusplus
}
#endif

#endif
