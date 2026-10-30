/* The one-screen picture's compositions: for each situation, which screens it shows, whose
 * brightness it takes, and the shapes that place pieces of the screens in it; read from a mod's
 * layouts/compositions.txt, else the port's own (compositions/port_days.txt). The file's format
 * and the animated pictures': docs/COMPOSITIONS.md. */
#ifndef KHDAYS_UI_COMPOSITION_H
#define KHDAYS_UI_COMPOSITION_H

#include "situation.h"
#include "../gfx/onescreen.h"

#include <stdint.h>

enum {
    KHDAYS_SCREENS_UPPER,       /* C x 192: the 3D and the shapes */
    KHDAYS_SCREENS_LOWER,       /* C x 192: the lower screen's backdrop and the shapes */
    KHDAYS_SCREENS_STACKED,     /* the two-screen picture */
    KHDAYS_SCREENS_SIDE_BY_SIDE /* both whole, the upper on the left */
};

/* A shape in the one picture, built for this frame: its record for the pass
 * (gfx/onescreen.h, KO_WORDS words) and its colours made transparent. */
typedef struct KhdaysShape {
    uint32_t words[KO_WORDS];
} KhdaysShape;

/* A 3D shape: the HUD's screen-space polygons wholly inside a rectangle of
 * the DS's screen (DS dots), placed in the picture (its top-left at x, y,
 * DS dots of the picture, scaled) or hidden; or, group "all",
 * every polygon whose box's middle is inside the rectangle (a model in a
 * menu's frame, which the port draws larger elsewhere). */
enum { KHDAYS_SHAPE3D_HUD, KHDAYS_SHAPE3D_ALL };
typedef struct KhdaysShape3d {
    int group; /* KHDAYS_SHAPE3D_* */
    int hide;
    float x, y, scale;
    float src_x, src_y, src_w, src_h;
    int whole; /* group "all": every polygon placed, not only those with their middle inside, where
                * the screen over the picture cuts the rest at its frame (the camp's model window) */
    int world; /* group "all": the world's polygons alone, the HUD's left where the HUD's shapes put them */
} KhdaysShape3d;

/* A piece of an animated picture as this frame has it, as the one-screen pass reads it
 * (KHDAYS_PIECE_WORDS words): its rectangle in the picture's texels, its texels in the atlas (a
 * negative width or height turns them over), its corners' colours (RGBA, 1 = the texel's own:
 * left top, right top, left bottom, right bottom), how it goes on what is under it. */
enum { KHDAYS_PIECE_OVER, KHDAYS_PIECE_ADD, KHDAYS_PIECE_TAKE };
#define KHDAYS_PIECE_WORDS 32
typedef struct KhdaysPiece {
    float rect[4];
    float texels[4];
    float corners[16];
    uint32_t blend;
    uint32_t unused[7];
} KhdaysPiece;

/* A rectangle of an animated picture the mouse clicks (`button`): its action and where it is, in
 * the picture's texels. */
typedef struct KhdaysButton {
    const char *action;
    float rect[4];
} KhdaysButton;

/* This frame's composition: the arrays are the module's, valid until the
 * next call. */
typedef struct KhdaysComposition {
    int screens;    /* KHDAYS_SCREENS_* */
    int brightness; /* KOB_* (gfx/onescreen.h) */
    const KhdaysShape *shapes;
    int shape_count;
    const uint32_t *keys; /* BGR555 colours made transparent (KO_KEYS) */
    int key_count;
    const KhdaysShape3d *shapes3d;
    int shape3d_count;
    /* the picture behind everything (`backdrop`): RGBA, red first, kept until the file is read
     * again; NULL when none. It covers the picture, its aspect kept, held at (backdrop_ax,
     * backdrop_ay): 0 its left or top edge on the picture's, 1 its right or bottom one. */
    const uint8_t *backdrop;
    int backdrop_w, backdrop_h;
    float backdrop_ax, backdrop_ay;
    int backdrop_fit; /* 1: inside the picture, its aspect kept, instead of covering it (`backdrop ...
                       * fit`; a screen of the port's laid out over it: host/screens/camp_screen.c) */
    int hide3d_all;   /* `hide3d all`: the 3D not drawn */
    int lower_whole;  /* the lower screen shown by the shapes whole, its 3D in it: engine A, when it is the lower
                       * screen, composed with its 3D (not apart from it) -- the 3D then not moved by any 3D shape */
    /* the Moogle Shop's screens composed apart (host/render/gfx_render.h khdays_gfx_render_shop), and the camp's
     * (the Files page's upper screen, the Panels page's): by screen (0 the upper, 1 the lower) which
     * (KHDAYS_SHOP_WANT_* bits), the layers the cut leaves out (a bit each), whether the normal one keeps the DS's
     * fade (the lower's list coming and going) and its 3D, how many dots it is slid back (the Panels page's layers
     * scrolled and sprites moved by its slide, put back); their texels a DS dot */
    unsigned shop_screens[2], shop_cut_layers[2];
    int shop_fade[2], shop_with3d[2], shop_slide[2];
    int shop_scale;
    /* an animated picture's: its size (the one that covers the picture; a picture's own size
     * otherwise), its ground (8-bit RGB, red in the low byte) and this frame's pieces, drawn in
     * order on it from the atlas `backdrop` is then; none for a picture */
    int animated; /* 1: `backdrop` is an animated picture's atlas */
    int picture_w, picture_h;
    uint32_t ground;
    const KhdaysPiece *pieces;
    int piece_count;
    const KhdaysButton *buttons; /* an animated picture's */
    int button_count;
} KhdaysComposition;

/* The file, read once (and again when the screens become one). */
void khdays_composition_load(void);

/* The player's HUD size ([video] hud_size: 1 to 8, 4 the default). */
void khdays_composition_set_hud_size(int size);
void khdays_composition_set_hud_options(unsigned options);
/* The HUD's factor its size gives (1 at the default size 4). */
float khdays_composition_hud_factor(void);
int khdays_composition_columns(void);

/* The inputs the player has for `action` on the device played, as icons are named (`icon`), in
 * the order to try; none (count 0) when the keyboard is played: the pieces' own texels. Copied. */
void khdays_composition_set_prompt(const char *action, const char *const *inputs, int count);

/* The composition for a situation in a picture C DS dots wide. */
void khdays_composition_for(const KhdaysSituation *situation, int columns, KhdaysComposition *out);

#endif
