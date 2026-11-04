/* Engine A's screen with its 3D drawn at N times the DS's resolution on the graphics card
 * (gpu_raster3d.c): the package's polygons rasterized by shaders/raster3d.hlsl with gpu3d.c's rules,
 * then edge marking, fog and the 2D composition by shaders/compose.hlsl. Window's thread only. */
#ifndef KHDAYS_GPU_RASTER3D_H
#define KHDAYS_GPU_RASTER3D_H

#include <SDL3/SDL.h>

typedef struct KhdaysFramePackage KhdaysFramePackage; /* runtime/display/frame_package.h */

/* Makes the pipelines and buffers on `device`. Returns 0 (with the reason in
 * SDL_GetError) when it cannot. */
int khdays_raster3d_open(SDL_GPUDevice *device);

/* Lets go of everything (before the device goes; `renderer`'s wrapped
 * texture with it). */
void khdays_raster3d_close(void);

/* Draws engine A's screen of `package` (which must hold the 3D and the
 * composition records) at `scale` x 256 x 192 and submits the work. Returns
 * 0 when it cannot (the targets for that scale cannot be made, say), with
 * the reason in SDL_GetError. */
int khdays_raster3d_draw(const KhdaysFramePackage *package, int scale, int texture_smoothing);

/* Draws only the 3D of `package` at `scale` (no composition, so no records
 * needed), for the port's own 2D to compose with (gfx_render.c). It uses the
 * same targets as khdays_raster3d_draw. */
int khdays_raster3d_rasterize(const KhdaysFramePackage *package, int scale, int texture_smoothing);

/* The HUD apart, for a 2D drawn finer than the 3D (gpu_present.c): with it on,
 * khdays_raster3d_rasterize_hud draws the list's HUD polygons alone (the command
 * menu, gfx/g3d.h KHDAYS_G3D_HUD) at the 2D's scale into a picture of their own,
 * over nothing (no clear colour's alpha or fog, no rear plane), and
 * khdays_raster3d_rasterize every other polygon at its scale; the 2D composes the
 * HUD over the rest (khdays_raster3d_hud_targets: 0 when there is no HUD picture
 * of this frame). Each size keeps its own targets while it is on; off (the
 * default), the list is drawn whole. */
void khdays_raster3d_set_hud_apart(int on);
int khdays_raster3d_rasterize_hud(const KhdaysFramePackage *package, int scale, int texture_smoothing);
int khdays_raster3d_hud_targets(SDL_GPUTexture **color, SDL_GPUTexture **attr, int *columns);

/* Sets a 3D list to draw instead of the package's (a picture between two logic frames); NULL goes back
 * to the package's. */
struct KhdaysG3dList;
void khdays_raster3d_use_list(const struct KhdaysG3dList *list);

/* Whether the card's own rasterizer draws the 3D (gpu_native3d.h) rather than raster3d.hlsl alone, and
 * switching it (KHDAYS_3D=compute starts with it off): on only when the native renderer could be made. */
int khdays_raster3d_native(void);
void khdays_raster3d_set_native(int on);

/* Sets whether the 3D is drawn in 8 bits per channel instead of the DS's 6 (when HD textures are
 * drawn), and says whether it is. */
void khdays_raster3d_set_fine(int on);
int khdays_raster3d_fine(void);

/* The 3D's last drawn targets (the colour, and the depth with the polygon
 * ID, edge and fog flags) and how many DS columns they cover. Returns 0 if
 * there are none. The card's rasterizer keeps the depth, IDs, edges and fog
 * only when the picture's finishing reads them (fog, edge marking). */
int khdays_raster3d_targets(SDL_GPUTexture **color, SDL_GPUTexture **attr, int *columns);

/* The last drawn screen, as a texture of `renderer` (NULL if none). */
SDL_Texture *khdays_raster3d_texture(SDL_Renderer *renderer);

/* Writes the last few draws (frame, scale, polygons, vertices, the busiest
 * tile), one line each, for a report when the graphics card stops answering. */
void khdays_raster3d_recent(char *out, size_t size);

/* Reads the last drawn screen back from the graphics card (waiting for it):
 * *width x *height pixels as 0x00RRGGBB, to free with SDL_free. For the tests
 * and the HD screenshots. NULL on failure. */
uint32_t *khdays_raster3d_read(int *width, int *height);

/* One screen: the HUD's polygons are not moved to the sides as in widescreen. */
void khdays_raster3d_set_one_screen(int on);

/* One screen: the composition's 3D shapes (ui/composition.h) for the next
 * draws, which place the HUD. The array stays the caller's and must stay
 * valid until the next call. */
typedef struct KhdaysShape3d KhdaysShape3d;
void khdays_raster3d_set_hud_shapes(const KhdaysShape3d *shapes, int count);

#endif
