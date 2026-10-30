/* The port's own 2D, composed on the graphics card (gfx_render.c): both screens of a frame package
 * composed from their descriptions (gfx/frame.h) by shaders/compose2d.hlsl -- the same code the CPU
 * runs in gfx/compose_cpu.c -- at N times the DS's resolution. Window's thread only. */
#ifndef KHDAYS_GFX_RENDER_H
#define KHDAYS_GFX_RENDER_H

#include <SDL3/SDL.h>

typedef struct KhdaysFramePackage KhdaysFramePackage; /* runtime/display/frame_package.h */

/* Creates the pipeline and buffers on `device`. Returns 0 (with the reason in
 * SDL_GetError) when it cannot. */
int khdays_gfx_render_open(SDL_GPUDevice *device);
void khdays_gfx_render_close(void);

/* Composes both screens of `package` (which must hold KHDAYS_WANT_2D) at
 * `scale` x 256 x 192 and submits the work. Engine A's 3D comes from the
 * package at 1x or, with `raster`, from gpu_raster3d.c's targets at `scale`
 * (drawn before by khdays_raster3d_rasterize). Returns 0 (with the reason in
 * SDL_GetError) when it cannot. */
int khdays_gfx_render_draw(const KhdaysFramePackage *package, int scale, int raster);

/* What the one-screen picture is made of: the situation's shapes (gfx/onescreen.h) and the colours
 * they make transparent (keys), the frame (KOS_*), the brightness rule (KOB_*), and the picture's width
 * in DS columns (the 3D's). */
typedef struct KhdaysOneScreenDraw {
    const uint32_t *shapes;
    int shape_count;
    const uint32_t *keys;
    int key_count;
    int frame;
    int rule;
    int columns;
    /* the composition's picture behind everything (ui/composition.h), or NULL; and its `hide3d all` */
    const uint8_t *backdrop;
    int backdrop_w, backdrop_h;
    float backdrop_ax, backdrop_ay;
    int backdrop_fit; /* inside the picture instead of covering it */
    int hide3d_all;
    int lower_whole; /* engine A, the lower screen, composed with its 3D (ui/composition.h) */
    /* The Moogle Shop's screens composed apart (ui/composition.h shop_*), and the camp's (the Files page's upper
     * screen, the Panels page's): by screen (0 the upper, 1 the lower), KHDAYS_SHOP_WANT_* bits, the layers its cut
     * leaves out, whether its normal one keeps the DS's fade, its 3D (at 1x) and is slid back by so many dots;
     * their size, in texels a DS dot (up to KHDAYS_SHOP_SCALE) */
    unsigned shop_screens[2], shop_cut_layers[2];
    int shop_fade[2], shop_with3d[2], shop_slide[2];
    int shop_scale;
    /* an animated picture's: the size that covers the picture, its ground (8-bit RGB) and this
     * frame's pieces (ui/composition.h KhdaysPiece), from the atlas `backdrop` is then */
    int animated;
    int picture_w, picture_h;
    uint32_t ground;
    const void *pieces;
    int piece_count;
} KhdaysOneScreenDraw;

/* Like khdays_gfx_render_draw, but for one screen: the screens are composed
 * without the 3D (KDF_SPLIT) at the DS's 256 columns, then the one picture is
 * made from them and the 3D at `scale` (with `raster`, as wide as
 * one->columns). */
int khdays_gfx_render_draw_one(const KhdaysFramePackage *package, int scale, int raster,
                               const KhdaysOneScreenDraw *one);

/* The size the window shows the draws that follow at, in window pixels a DS pixel. What is made larger
 * (the antialiasing, the window's size rounded up) is shrunk to it on the card (shrink.hlsl), each
 * pixel the average of the area under it. 0: as made. */
void khdays_gfx_render_set_shown(float pixels_per_ds_pixel);

/* The last one-screen picture made, as a texture of `renderer`, and its width
 * in DS columns (NULL if none). */
SDL_Texture *khdays_gfx_render_picture(SDL_Renderer *renderer, int *columns);

/* The Moogle Shop's screens (KhdaysOneScreenDraw shop_screens), composed apart from the one picture at up to
 * KHDAYS_SHOP_SCALE times the DS's size, the HD tiles and fonts in them (the CPU's composition has none): screen
 * `lower` (0 the upper) as the DS shows it (KHDAYS_SHOP_NORMAL: no 3D, opaque), or without the layers its cut
 * leaves out over a black backdrop (KHDAYS_SHOP_CUT_BLACK: its colours times how much they cover) or a white one
 * (KHDAYS_SHOP_CUT_WHITE: the two differ by what does not cover), as a texture of `renderer`, *scale its texels a
 * DS dot, *made a number that changes when it is composed anew. NULL when none is: never wanted, or not wanted in
 * the last picture that wanted some (the screens held as they were while none is: the list floating out). */
enum { KHDAYS_SHOP_NORMAL, KHDAYS_SHOP_CUT_BLACK, KHDAYS_SHOP_CUT_WHITE, KHDAYS_SHOP_VARIANTS };
#define KHDAYS_SHOP_WANT_NORMAL 1u
#define KHDAYS_SHOP_WANT_CUT 2u
#define KHDAYS_SHOP_SCALE 4
SDL_Texture *khdays_gfx_render_shop(SDL_Renderer *renderer, int lower, int variant, int *scale, unsigned *made);
/* Whether the last picture that wanted some made that one. */
int khdays_gfx_render_shop_made(int lower, int variant);

/* How many DS columns the last drawn screen of engine `engine` covers (256,
 * or more for engine A in widescreen). */
int khdays_gfx_render_columns(int engine);

/* The last drawn screen of engine `engine`, as a texture of `renderer` (NULL if none). */
SDL_Texture *khdays_gfx_render_texture(SDL_Renderer *renderer, int engine);

/* With the 3D on both screens engine B's screen shows, as on the DS, the capture of engine A's picture
 * from the VBlank before (kept by the port): whether it shows, and that picture as a texture with its
 * width in DS columns (NULL when engine B's own screen should show). */
int khdays_gfx_render_captured_shown(void);
SDL_Texture *khdays_gfx_render_captured(SDL_Renderer *renderer, int *columns);

/* Whether package `p` is a frame of a screen blend the port takes at its own resolution: engine A
 * shows a VRAM bank and is drawn as its layers and 3D instead (the 3D rasterized as for its 3D). */
int khdays_gfx_render_capture_frame(const KhdaysFramePackage *p);

/* Reads back the last drawn screen of engine `engine` (2: the one-screen
 * picture, 3: the kept capture), waiting for the card: *width x *height
 * pixels as 0x00RRGGBB, to free with SDL_free. NULL on failure. */
uint32_t *khdays_gfx_render_read(int engine, int *width, int *height);

#endif
