/* The frame package: what the game's thread hands the window for each frame shown (display.c fills
 * it, host/render/gpu_present.c draws it). Plain data with no pointers into DS memory, so it can be
 * written to a file and drawn later (khdays-render-test). The two screens are always filled; the rest
 * only when the window asks (khdays_host_frame_wants), to draw the 3D at a higher resolution and
 * compose it the same way. */
#ifndef KHDAYS_FRAME_PACKAGE_H
#define KHDAYS_FRAME_PACKAGE_H

#include "../../gfx/frame.h"
#include "../../gpu/gpu2d.h"
#include "../../gfx/g3d.h"
#include "../../gpu/vram_map.h"
#include "../../ui/situation.h"
#include "../../gfx/sequence_image.h"

#include <stddef.h>
#include <stdint.h>

#define KHDAYS_FRAME_W 256
#define KHDAYS_FRAME_H 192
#define KHDAYS_FRAME_PIXELS (KHDAYS_FRAME_W * KHDAYS_FRAME_H)
#define KHDAYS_FRAME_TEX_BYTES 0x80000u    /* the 3D texture slots, 512 KB */
#define KHDAYS_FRAME_TEXPAL_BYTES 0x18000u /* the texture palette slots, 96 KB */
#define KHDAYS_FRAME_VRAM_PAGE 0x4000u     /* 16 KB */

/* What the host asks for (khdays_host_frame_wants). */
enum {
    KHDAYS_WANT_3D = 1 << 0,          /* the 3D list, its registers and texture VRAM */
    KHDAYS_WANT_COMPOSITION = 1 << 1, /* engine A's composition records */
    KHDAYS_WANT_SOURCES = 1 << 2,     /* the 2D engines' source records (HD 2D graphics) */
    KHDAYS_WANT_2D = 1 << 3,          /* the port's own 2D: the screens' descriptions and what they read */
    KHDAYS_WANT_MOVIE = 1 << 4,       /* the 2D may show a movie's picture (video.h): composed in 8 bits */
    KHDAYS_WANT_SITUATION = 1 << 5,   /* the one-screen picture's inputs */
};

/* KHDAYS_WANT_2D: the memory the screens' layers and sprites read (as the
 * game maps its banks), by 16 KB page, each area from its first page. */
enum {
    KHDAYS_2D_ABG = 0,      /* engine A's layers, 512 KB */
    KHDAYS_2D_AOBJ = 32,    /* engine A's sprites, 256 KB */
    KHDAYS_2D_BBG = 48,     /* engine B's layers, 128 KB */
    KHDAYS_2D_BOBJ = 56,    /* engine B's sprites, 128 KB */
    KHDAYS_2D_ABGEXT = 64,  /* engine A's extended layer palettes, 32 KB */
    KHDAYS_2D_AOBJEXT = 66, /* engine A's extended sprite palette, 8 KB of its page */
    KHDAYS_2D_BBGEXT = 67,
    KHDAYS_2D_BOBJEXT = 69,
    KHDAYS_2D_VRAM = 70, /* the bank engine A's VRAM display shows, 128 KB */
    KHDAYS_FRAME_2D_PAGES = 78
};

/* The map of the tiles drawn instead: a word for each 32 bytes of the 2D
 * memory's first 64 pages (runtime/mods/tiles2d.h). */
#define KHDAYS_FRAME_HD2D_PAGES 64
#define KHDAYS_FRAME_HD2D_PAGE_WORDS 512

/* A screen's elements (gfx/elements.h): `count` records of KE_WORDS, the topmost of a slot first;
 * the pointer kept in 8 bytes whatever the process's size (a recording is read as it lies). */
typedef struct KhdaysFrameElements {
    uint32_t count, capacity;
    KHDAYS_G3D_POINTER(uint32_t, records);
} KhdaysFrameElements;

/* Information state: low bits own each engine's banner, bits 8/9 have an image. Availability
 * survives an empty message, a completed exit and a scene change, so native crops stay disabled. */
#define KHDAYS_INFORMATION_AVAILABLE (1u << 16)

typedef struct KhdaysFramePackage {
    uint32_t frame;     /* the VBlank it was drawn at */
    uint32_t alike;     /* how many VBlanks before it showed the frame before it again (nothing changed) */
    uint32_t wants;     /* what was filled (KHDAYS_WANT_*) */
    uint8_t upper;      /* the engine on the upper screen (POWCNT1 bit 15) */
    uint8_t list3d_cut; /* nothing to draw between the list before and list3d */
    uint8_t capture;    /* the game uses the display capture: engine A's picture may show on the other screen next */
    uint8_t pad[1];
    uint64_t time;          /* the VBlank's time, in the game's cycles (runtime/core/clock.h) */
    uint64_t list3d_time;   /* the VBlank list3d was first shown at: the logic frame's time */
    uint32_t list3d_serial; /* moves with each new list3d (one per logic frame) */
    uint32_t pad2;
    uint32_t screens[2][KHDAYS_FRAME_PIXELS]; /* engine A, engine B: 0x00RRGGBB, as port/gpu draws them */
    KhdaysGpu2dInfo info[2];                  /* blending, master brightness, BG0 scroll */
    /* KHDAYS_WANT_3D */
    uint32_t layer3d[KHDAYS_FRAME_PIXELS]; /* the 3D the software renderer drew (6-bit RGB, alpha 5) */
    uint8_t io3d[0x100]; /* the ARM9 I/O registers 0x04000300-0x040003ff (fog, edge, toon, clear, alpha test) */
    uint16_t disp3dcnt;
    uint16_t unused3d;
    KhdaysG3dList list3d; /* gfx/g3d.h, in drawing order */
    /* Per 16 KB page, a count that moves whenever the page's bytes change:
     * the host re-reads a page whose count differs from the one it last
     * read (packages it never drew do not hide a change). */
    uint32_t tex_version[KHDAYS_FRAME_TEX_BYTES / KHDAYS_FRAME_VRAM_PAGE];
    uint32_t texpal_version[KHDAYS_FRAME_TEXPAL_BYTES / KHDAYS_FRAME_VRAM_PAGE];
    uint8_t tex[KHDAYS_FRAME_TEX_BYTES]; /* the texture slots as the 3D engine read them (unmapped: 0) */
    uint8_t texpal[KHDAYS_FRAME_TEXPAL_BYTES];
    /* KHDAYS_WANT_COMPOSITION */
    KhdaysGpu2dComposition composition[KHDAYS_FRAME_PIXELS]; /* gpu2d.h */
    /* and when the 3D list is wider than the DS's view: the records over its
     * columns, the HUD kept to the sides (gpu2d.h KhdaysGpu2dWide) */
    int32_t wide_columns; /* 0: none */
    int32_t wide_pieces;
    KhdaysGpu2dPiece wide_piece[KHDAYS_GPU2D_MAX_PIECES];
    KhdaysGpu2dComposition wide[KHDAYS_GPU2D_MAX_COLUMNS * KHDAYS_FRAME_H];
    /* KHDAYS_WANT_2D */
    uint32_t desc2d[2][KHDAYS_DESC_WORDS];         /* engine A's screen, engine B's (port/gfx/frame.h) */
    uint8_t palette2d[0x800];                      /* both engines' palettes, 0x05000000 */
    uint32_t mem2d_version[KHDAYS_FRAME_2D_PAGES]; /* as tex_version */
    uint8_t mem2d[KHDAYS_FRAME_2D_PAGES * KHDAYS_FRAME_VRAM_PAGE];
    /* the tiles drawn instead (HD 2D from mods): the map of the layers' and
     * sprites' memory (runtime/mods/tiles2d.h), with its pages' counts */
    uint32_t hd2d_version[KHDAYS_FRAME_HD2D_PAGES];
    uint32_t hd2d[KHDAYS_FRAME_HD2D_PAGES * KHDAYS_FRAME_HD2D_PAGE_WORDS];
    /* the port's elements of the parts that are on, each screen's; desc2d's KD_ELEMENTS says how many */
    KhdaysFrameElements elements[2];
    /* what those parts stand for, left out of its slot (desc2d's KD_LEFT_OUT):
     * a bit for each cell of the text layers' maps, one per sprite (gfx/elements.h) */
    uint8_t left_out_cells[2][KE_LEFT_CELL_WORDS * 4];
    uint8_t left_out_obj[2][KE_LEFT_OBJ_WORDS * 4];
    /* KHDAYS_WANT_SITUATION: the one-screen situation's inputs (ui/situation.h); last, and not recorded */
    KhdaysSituationInputs situation;
    /* The display capture as the host takes it again at its own resolution (gfx_render.c), not
     * recorded either: DISPCAPCNT when a capture is armed at this VBlank (else 0), the captures taken
     * into each VRAM bank before it, that count after the last one that did not read the bank before
     * (a blend's first: engine A's picture alone; 0 none), and a bit for each bank that still holds
     * its last capture. */
    uint32_t dispcapcnt;
    uint32_t bank_captures[4];
    uint32_t bank_plain[4];
    uint32_t bank_intact;
    /* The field's room change is being drawn (runtime/display/field_wipe.h): its wipe's quad is in this
     * frame's 3D, and the window covers the columns beside it. Not recorded either. */
    uint32_t field_wipe;
    /* Transient native image commands, owned by this package, not part of version-14 recordings. */
    KhdaysSequenceImage sequence_image;
    KhdaysSequenceImage portrait_images[4];
    KhdaysSequenceImage party_images[3];
    KhdaysSequenceImage label_images[5];
    KhdaysSequenceImage enemy_image;
    KhdaysSequenceImage sequence3d_images[KSI_3D_PROGRAMS];
    KhdaysSequenceImage information_images[2];
    KhdaysSequenceImage obtained_image;
    KhdaysSequenceImage notification_images[3];
    KhdaysSequenceImage mission_panel_image;
    KhdaysSequenceImage minimap_images[4];
    KhdaysSequenceImage radar_image;
    uint32_t mission_panel_state; /* bit 0: replacement owns panel; bit 1: native panel visible */
    int mission_panel_backdrop;
    uint32_t information_hold_frames;
    uint32_t information_state;
    uint32_t hud_options;
} KhdaysFramePackage;

/* What KHDAYS_RECORD_FRAMES writes of a package (version 14): all of it but
 * the situation's inputs. */
#define KHDAYS_FRAME_RECORDED offsetof(KhdaysFramePackage, situation)

/* Engine A's pixel (x, y) composed again from the package's composition
 * records and a 3D layer (layer3d's format), as the screen shows it
 * (0x00RRGGBB). With the package's own layer3d this is screens[0]'s pixel:
 * the check that the records say what the 2D engine did (frame_package.c). */
uint32_t khdays_frame_pixel_again(const KhdaysFramePackage *package, const uint32_t *layer3d, int x, int y);

/* How many of engine A's pixels come out different when composed again
 * that way, and the first of them (-1 when none). */
int khdays_frame_check_composition(const KhdaysFramePackage *package, const uint32_t *layer3d, int *first);

#endif
