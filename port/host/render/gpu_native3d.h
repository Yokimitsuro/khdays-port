/* The 3D list drawn by the graphics card's own rasterizer (gpu_native3d.c, shaders/native3d*.hlsl,
 * docs/RENDERING.md "The native 3D"), into raster3d's two targets: the polygons up to the first native
 * sequence (those from it on are raster3d.hlsl's, which resumes from the targets). Window's thread only. */
#ifndef KHDAYS_GPU_NATIVE3D_H
#define KHDAYS_GPU_NATIVE3D_H

#include <SDL3/SDL.h>

struct KhdaysG3dList;

/* Makes the shaders on `device`. Returns 0 (with the reason in SDL_GetError) when it cannot: then
 * raster3d.hlsl draws everything. */
int khdays_native3d_open(SDL_GPUDevice *device);
void khdays_native3d_close(void);

/* What a picture is drawn with (gpu_raster3d.c fills it from the frame package). */
typedef struct KhdaysNative3dFrame {
    int scale, width, height; /* N, the targets' size in pixels */
    int origin_x;             /* the DS column of the targets' first */
    uint32_t disp3dcnt, swap_params, alpha_ref, clear_color, clear_depth;
    uint32_t smooth; /* raster3d's: bit 0 texture smoothing, bit 1 screen space stretched */
    uint32_t fine;   /* colours in 8 bits a channel */
    uint32_t toon[16];
} KhdaysNative3dFrame;

/* How many of `list`'s polygons it draws: those before the first native sequence. */
int khdays_native3d_count(const struct KhdaysG3dList *list);

/* Gets the first `count` polygons of `list` ready to draw for `frame`: the triangles' corners (their
 * bytes in *corner_bytes, to upload with khdays_native3d_upload), the pipelines and targets they need.
 * Returns 0 (with the reason in SDL_GetError) when it cannot. */
int khdays_native3d_prepare(const struct KhdaysG3dList *list, int count, const KhdaysNative3dFrame *frame,
                            uint32_t *corner_bytes);

/* The corners (khdays_native3d_prepare's bytes), to copy into the caller's transfer buffer. */
const void *khdays_native3d_corners(void);

/* Uploads the corners from `upload` at `offset`, in the caller's copy pass. */
void khdays_native3d_upload(SDL_GPUCopyPass *copy, SDL_GPUTransferBuffer *upload, uint32_t offset, uint32_t bytes);

/* Records the drawing of the prepared polygons into `cmd`, from the list's buffers (raster3d's), into
 * `color3d` and `attr3d`. */
void khdays_native3d_draw(SDL_GPUCommandBuffer *cmd, const KhdaysNative3dFrame *frame, SDL_GPUBuffer *polys,
                          SDL_GPUBuffer *verts, SDL_GPUBuffer *vram, SDL_GPUBuffer *hd, SDL_GPUTexture *color3d,
                          SDL_GPUTexture *attr3d);

#endif
