/* The port's 3D geometry: it answers what NitroSystem and the game ask of the DS's geometry engine,
 * its state the DS's in the DS's fixed point, and builds a polygon list the GPU renderer
 * (host/render/gpu_raster3d.c) and the reference rasterizer (gpu/gpu3d.c) draw. Commands run at once:
 * no FIFO, never busy; SwapBuffers closes the list. Plain C (docs/RENDERING.md, "The 3D"). */
#ifndef KHDAYS_GFX_G3D_H
#define KHDAYS_GFX_G3D_H

#include <stdint.h>

/* The DS's polygon and vertex RAM: what the DS draws of a list, in the
 * order the polygons come (the rest it drops). The port's lists have no such
 * limit: the polygons beyond are kept and drawn, marked KHDAYS_G3D_BEYOND_DS,
 * which the reference (gpu/gpu3d.c) leaves out as the DS does. */
#define KHDAYS_G3D_DS_POLYS 2048
#define KHDAYS_G3D_DS_VERTS 6144

/* A vertex of a list: clipped, through the viewport. The layout is the
 * shaders' too (port/shaders/raster3d.hlsl). */
typedef struct KhdaysG3dVertex {
    float x, y;    /* screen, pixels (y down) */
    float z;       /* z / w, -1..1 */
    float w;       /* clip w, 12 fractional bits as a float */
    float r, g, b; /* 6 bits each */
    float s, t;    /* texels, 4 fractional bits as a float */
    float a;       /* a mod's vertex alpha, 0..1, times the polygon's (the game's: 1) */
} KhdaysG3dVertex;

typedef struct KhdaysG3dPolygon {
    uint32_t attr, tex, pltt; /* POLYGON_ATTR, TEXIMAGE_PARAM, PLTT_BASE */
    int32_t first, count;     /* in the vertex list */
    float ymin, ymax;
    int32_t translucent;
    int32_t flags;       /* KHDAYS_G3D_HUD, KHDAYS_G3D_BEYOND_DS, KHDAYS_G3D_MOD */
    int32_t replacement; /* the texture drawn instead (runtime/mods/textures.h), or -1 */
} KhdaysG3dPolygon;

/* A polygon as the game sent it, before clipping, in a list's `pre` arrays: its vertices in clip space
 * (the game's in fixed point, a mod's in floats, ten words each), what it was drawn with, and the key
 * that finds the same polygon in the next list (its model draw and its place in it). */
typedef struct KhdaysG3dPreVertex {
    int32_t word[10];
} KhdaysG3dPreVertex;

typedef struct KhdaysG3dPreInfo {
    uint64_t draw;       /* the model draw's resource, 0 for none (8 bytes
                           * whatever the process's pointers: a frame package
                           * records it) */
    uint32_t occurrence; /* that resource's draws before this one in the list */
    uint32_t ordinal;    /* the polygon's place in the draw (or among those outside draws) */
    uint8_t viewport[4]; /* x1, y1, x2, y2 */
    int32_t widen;       /* the clip factor, in 1/4096 */
} KhdaysG3dPreInfo;

#define KHDAYS_G3D_HUD 0x1       /* sent by the HUD's own drawing (khdays_g3d_set_hud) */
#define KHDAYS_G3D_BEYOND_DS 0x2 /* the DS's polygon or vertex RAM would not have held it */
#define KHDAYS_G3D_MOD 0x4       /* a mod's model's, drawn instead of the game's (runtime/mods/models.h): no DS's */
#define KHDAYS_G3D_SKY 0x10      /* a mod's sky: behind everything else, its layer in bits 8-15 */
#define KHDAYS_G3D_SEQUENCE 0x20 /* ordered native sequence commands; pltt identifies the retained program */
#define KHDAYS_G3D_MARKER 0x40   /* screen-projected selection art: after world geometry, inside the 3D layer */
#define KHDAYS_G3D_SKY_LAYER(flags) (((flags) & KHDAYS_G3D_SKY) ? ((flags) >> 8) & 0xff : 0)
#define KHDAYS_G3D_OVER_2D                                                                                             \
    0x8 /* a HUD polygon the DS shows over all its 2D (its BG0 the top layer): the one
                                  * picture puts it in front of its shapes, widescreen leaves it in place (the
                                  * game over's floating orb, by the menu's line) */

/* A pointer member in 8 bytes whatever the process's pointers are: a frame
 * package (runtime/display/frame_package.h) records the list as it is in memory,
 * and is read the same by any build that writes one. */
#define KHDAYS_G3D_POINTER(type, name)                                                                                 \
    union {                                                                                                            \
        type *name;                                                                                                    \
        uint64_t name##_slot;                                                                                          \
    }

/* A list in drawing order: opaque, then translucent (Y-sorted unless manual), then selection
 * markers in submission order. It holds its polygons and vertices in memory of its own. */
typedef struct KhdaysG3dList {
    int32_t npolys, nverts;
    uint32_t swap_params; /* the SwapBuffers bits it is drawn with */
    float widen;          /* its horizontal clip factor (1 at 4:3) */
    int32_t beyond_ds;    /* how many of its polygons the DS would have dropped */
    KHDAYS_G3D_POINTER(KhdaysG3dPolygon, polys);
    KHDAYS_G3D_POINTER(KhdaysG3dVertex, verts);
    int32_t poly_capacity, vert_capacity; /* what `polys` and `verts` have room for */
    /* The same polygons before clipping, in the order sent, the culled ones
     * too: `pre` holds what each was drawn with (first and
     * count into `pre_verts`), `pre_info` its key. */
    /* The camera NitroSystem sent for it (NNS_G3dGlbFlushP: its projection
     * and camera matrices, the DS's fixed point, as the first flush of the
     * list gave them) and how many flushes it had. */
    int32_t flushes;
    int32_t camera[12], projection[16];
    int32_t npre, npre_verts;
    KHDAYS_G3D_POINTER(KhdaysG3dPolygon, pre);
    KHDAYS_G3D_POINTER(KhdaysG3dPreInfo, pre_info);
    KHDAYS_G3D_POINTER(KhdaysG3dPreVertex, pre_verts);
    int32_t pre_capacity, pre_vert_capacity;
} KhdaysG3dList;

/* `to` becomes a copy of `from`, its memory grown as needed; 0 when there is
 * no memory for it (`to` is then left empty). */
int khdays_g3d_list_copy(KhdaysG3dList *to, const KhdaysG3dList *from);

/* Room in `list` for `polys` polygons and `verts` vertices; 0 if there is
 * no memory. */
int khdays_g3d_list_reserve(KhdaysG3dList *list, int32_t polys, int32_t verts);

void khdays_g3d_list_free(KhdaysG3dList *list);

/* `out` becomes the list built from `from`'s polygons before clipping -- culled, clipped and put
 * through the viewport as the game's lists are -- with `vertices` in place of `from`'s pre_verts when
 * not NULL (gfx/g3d_interp.h). Touches nothing shared: safe on any thread. 0 when out of memory. */
int khdays_g3d_finish(const KhdaysG3dList *from, const KhdaysG3dPreVertex *vertices, KhdaysG3dList *out);

/* The model draws (runtime/mods/models.cpp): the polygons sent from now on come
 * from a draw of `resource`, until khdays_g3d_draw_end -- their key (above). */
void khdays_g3d_draw_begin(const void *resource);

/* NNS_G3dGlbFlushP (prepare.py HOOKS): the camera of the list being built. */
void khdays_g3d_frame_camera(const int32_t projection[16], const int32_t camera[12]);
void khdays_g3d_draw_end(void);

void khdays_g3d_reset(void);

/* --- What the game sends ---------------------------------------------------------- */

/* How many parameter words `command` takes; -1 for a number that is no
 * command (it takes none, and does nothing). */
int khdays_g3d_param_count(unsigned command);

/* One command with its parameters, run at once. */
void khdays_g3d_command(unsigned command, const uint32_t *params);

/* Words as GXFIFO takes them: a word of up to four packed command numbers,
 * then their parameters in order. A packed command's parameters may come in
 * later calls (the decoding carries over). */
void khdays_g3d_words(const uint32_t *words, uint32_t count);

/* One store to a command port, 0x04000440-0x040005ff (`offset` from
 * 0x04000000): one parameter of its command, or the command itself for one
 * that takes none. */
void khdays_g3d_port(uint32_t offset, uint32_t value);

/* --- What the game reads back ----------------------------------------------------- */

/* A status or result register, 0x04000600-0x040006a3 (`offset` from
 * 0x04000000): GXSTAT (never busy, the FIFO always empty, the matrix stack
 * levels and error, the box test), RAM_COUNT (what the DS's RAM would hold),
 * the position and vector test results, the clip and vector matrices. */
uint32_t khdays_g3d_read(uint32_t offset);

/* GXSTAT written: bit 15 acknowledges a matrix stack error (and resets the
 * projection and texture stacks' levels), bits 30-31 the FIFO's IRQ mode. */
void khdays_g3d_status_written(uint32_t value);

/* Whether the geometry FIFO's interrupt condition holds (GXSTAT bits 30-31:
 * less than half full, or empty -- with no FIFO, both always). */
int khdays_g3d_fifo_irq(void);

/* --- The lists ---------------------------------------------------------------------- */

/* Whether a SwapBuffers has closed a list the next VBlank shows. */
int khdays_g3d_swap_pending(void);

/* SwapBuffers so far: one per logic frame the game draws (KHDAYS_TRACE_TIMING). */
uint32_t khdays_g3d_swap_count(void);

/* VBlank: the oldest closed list becomes the one shown; returns whether
 * one did. */
int khdays_g3d_vblank(void);

/* The list shown, in drawing order (valid until the next VBlank). */
const KhdaysG3dList *khdays_g3d_shown(void);

/* --- The port's own marks (nothing the game reads or the DS draws changes) ------- */

/* A wider picture (widescreen): from the next list on, polygons are clipped
 * at x = +-k*w instead of +-w, keeping what lies beside the DS's view (the
 * vertices beyond its 256 columns get x < 0 or >= 256). 1 is the DS's. The
 * matrices and the tests stay the DS's. */
void khdays_g3d_set_widen(float k);

/* The polygons sent from now on are the HUD's (1), the HUD's shown over the
 * 2D (2: KHDAYS_G3D_OVER_2D), or not (0), so that widescreen can keep the HUD
 * to the sides and the one picture's 3D shapes place it (host/render/gpu_raster3d.c). */
void khdays_g3d_set_hud(int on);

/* Selection-marker draws only. Does not change HUD placement or the ordering of the 2D layers. */
void khdays_g3d_set_marker(int on);

/* The building list's stable slot (0..3), for transient images retained with that list. */
int khdays_g3d_build_slot(void);

/* --- A mod's model, drawn instead of the game's (runtime/mods/models.h) ---------------- */

/* The matrices now as floats (1.0 the DS's 0x1000): position, vector and
 * projection. The DS's row-major arrays for row vectors are, read as they
 * lie, column-major matrices for column vectors: the same transform. */
void khdays_g3d_matrices(float position[16], float vector[16], float projection[16]);

/* What the next polygons would be drawn with: the material and the colour
 * as the game set them. */
typedef struct KhdaysG3dMaterial {
    uint32_t attr;                                        /* POLYGON_ATTR as set (latched at the next BEGIN_VTXS) */
    uint32_t tex, pltt;                                   /* TEXIMAGE_PARAM, PLTT_BASE */
    int color[3];                                         /* the vertex colour, 5 bits each */
    int diffuse[3], ambient[3], specular[3], emission[3]; /* 5 bits each */
} KhdaysG3dMaterial;
void khdays_g3d_material(KhdaysG3dMaterial *out);

/* The colour a vertex of unit normal `n` (in the vector matrix's space) gets
 * from `m` and the lights now -- the DS's lighting (GBATEK's "Internal
 * Operation on Normal Command") with the lights `m->attr` turns on, in
 * floats and not rounded; `m->color` when it turns none on. 6 bits a
 * channel. */
void khdays_g3d_light(const KhdaysG3dMaterial *m, const float n[3], float rgb[3]);

/* A vertex of a mod's polygon in clip space, in floats (1.0 the DS's
 * 0x1000). */
typedef struct KhdaysG3dClipVertex {
    float x, y, z, w;
    float r, g, b; /* 6 bits */
    float s, t;    /* texels, 4 fractional bits */
    float a;       /* alpha, 0..1, blended across the polygon */
} KhdaysG3dClipVertex;

/* A polygon of a mod's model into the list being built, culled and clipped as the DS's are except at
 * the far plane: what lies past it is drawn at its depth. `sky` > 0 makes it a sky layer, behind
 * everything and in front of the layers before it. `replacement` is its texture
 * (runtime/mods/textures.h), -1 for none. */
void khdays_g3d_mod_polygon(const KhdaysG3dClipVertex *v, int n, uint32_t attr, uint32_t tex, int32_t replacement,
                            int translucent, int sky);

/* A screen-projected sequence in the 3D layer; its program is retained in the frame package.
 * The shader samples the atlas and blends each element against the scene at full alpha precision. */
void khdays_g3d_sequence_polygon(const KhdaysG3dClipVertex *v, int n, uint32_t attr, int32_t atlas, uint32_t program);

/* For traces: `hook` hears of each polygon added to the list being built,
 * as it is added (NULL: none). */
typedef void (*KhdaysG3dPolygonHook)(const KhdaysG3dPolygon *polygon, const KhdaysG3dVertex *vertices);
void khdays_g3d_set_polygon_hook(KhdaysG3dPolygonHook hook);

#endif
