/* A screen of the port's drawn from a mod's pieces: a file of lines its tool writes (pieces in an
 * atlas, places, text styles, a font and its glyphs, input icons), the game's own strings with its DS
 * buttons drawn as the player's inputs' icons, and the drawing of pieces and text in a 1920 x 1080
 * picture fitted to the window. The lines: docs/HD_SCREENS.md, "The shared lines". */
#ifndef KHDAYS_HOST_LAYOUT_SCREEN_H
#define KHDAYS_HOST_LAYOUT_SCREEN_H

#include "../../runtime/core/rom.h"

#include <SDL3/SDL.h>

/* A text's character for the player's Y input (drawn as its icon, as the game's A, B and X): the
 * game's text has none for Y; the port's (Unicode's private use). */
#define KHDAYS_LAYOUT_GLYPH_Y 0xE059u
#define KHDAYS_LAYOUT_W 1920.0f
#define KHDAYS_LAYOUT_H 1080.0f
#define KHDAYS_LAYOUT_NAME 40

typedef struct KhdaysLayoutPiece {
    char name[KHDAYS_LAYOUT_NAME];
    float u, v, w, h;
} KhdaysLayoutPiece;

typedef struct KhdaysLayoutPlace {
    char name[KHDAYS_LAYOUT_NAME];
    float x, y, w, h;
} KhdaysLayoutPlace;

typedef struct KhdaysLayoutText {
    char name[KHDAYS_LAYOUT_NAME];
    float x, y, size;
    int align; /* 0 left, 1 centre, 2 right */
    int grey;
    int colour[3]; /* its grey, or its own colour */
} KhdaysLayoutText;

typedef struct KhdaysLayoutGlyph {
    unsigned code;
    float x, y, w, left, advance;
} KhdaysLayoutGlyph;

typedef struct KhdaysLayoutStrings {
    unsigned short **text; /* each record, UTF-16, NUL-ended */
    int count;
} KhdaysLayoutStrings;

typedef struct KhdaysLayout {
    const char *who; /* for the log */
    SDL_Renderer *renderer;
    SDL_Texture *atlas, *font;
    int aw, ah, fw, fh;
    int font_colours; /* the font's own colours (`rgba`): no grey, no shadow of the port's */
    KhdaysLayoutPiece *pieces;
    int piece_count;
    KhdaysLayoutPlace *places;
    int place_count;
    KhdaysLayoutText *texts;
    int text_count;
    KhdaysLayoutGlyph *glyphs;
    int glyph_count;
    float cell, ink_top, ink_bottom;
} KhdaysLayout;

/* The picture in the window, and the game's fade over it. */
typedef struct KhdaysLayoutFrame {
    float scale, ox, oy;
    int logical_w, logical_h; /* the renderer's own presentation, set aside while drawing */
    SDL_RendererLogicalPresentation mode;
} KhdaysLayoutFrame;

/* Its own: frees what a layout holds (it can be read again). */
void khdays_layout_forget(KhdaysLayout *l);
/* Reads one of the lines above into the layout (its files beside `folder`, a path ending in its
 * separator); 0 for any other line. */
int khdays_layout_line(KhdaysLayout *l, char *line, const char *folder);
/* After the lines: the glyphs sorted. Whether it has its atlas, its font and pieces. */
int khdays_layout_ready(KhdaysLayout *l);

const KhdaysLayoutPiece *khdays_layout_piece(const KhdaysLayout *l, const char *name);
const KhdaysLayoutPlace *khdays_layout_place(const KhdaysLayout *l, const char *name);
const KhdaysLayoutText *khdays_layout_text(const KhdaysLayout *l, const char *name);

/* A PNG as a texture of the layout's renderer (its grey as alpha, the colour white, with
 * grey_as_alpha); NULL when it cannot be read (said). */
SDL_Texture *khdays_layout_texture(KhdaysLayout *l, const char *path, int grey_as_alpha, int *w, int *h);
void khdays_layout_utf8(const char *in, unsigned short *out, int room);
/* A string table of the game's (u32 its header's size, u32 the count, then each record's u32
 * length -- these 4 bytes in it -- and its UTF-16LE text), as the game reads it. */
void khdays_layout_strings(KhdaysRomReader *reader, const char *path, KhdaysLayoutStrings *out, const char *who);
void khdays_layout_strings_free(KhdaysLayoutStrings *s);
const unsigned short *khdays_layout_string(const KhdaysLayoutStrings *s, int record);

/* Begins drawing over the window: the renderer's logical size set aside, the window black, the
 * picture fitted in it; khdays_layout_end puts the renderer back. */
void khdays_layout_begin(SDL_Renderer *renderer, KhdaysLayoutFrame *fr);
void khdays_layout_end(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr);
/* The same over what the window holds (the game's picture under a screen of the port's): black only
 * around the picture. */
void khdays_layout_begin_over(SDL_Renderer *renderer, KhdaysLayoutFrame *fr);
SDL_FRect khdays_layout_to_window(const KhdaysLayoutFrame *fr, float x, float y, float w, float h);

void khdays_layout_draw_piece(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const char *name, float x, float y,
                              float w, float h, float alpha);
/* The same, its texels multiplied by a colour (0-255). */
void khdays_layout_draw_tinted(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const char *name, float x, float y,
                               float w, float h, const Uint8 colour[3], float alpha);
/* A piece where its place says, offset by (dx, dy). */
void khdays_layout_draw_placed(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const char *name, float dx, float dy,
                               float alpha);
/* A line's width in the picture's texels at `size` (icons for the DS's buttons). */
float khdays_layout_line_width(const KhdaysLayout *l, const unsigned short *t, float size);
/* The icon of the input the player has for an action (KHDAYS_ACT_*) on the device played, `h` high
 * with its top-left at (x, y), or its name in brackets; its width (0: none). */
float khdays_layout_draw_action(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, int action, float x, float y,
                                float h, float alpha);
/* The same, on the keyboard the action's input `key` (an SDL scancode) when the player has it among the
 * action's (the arrows for L and R where they turn pages), else as above. */
float khdays_layout_draw_action_key(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, int action, int key, float x,
                                    float y, float h, float alpha);
/* A line with its capitals' top at y, x its left. */
void khdays_layout_draw_line(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const unsigned short *t, float x,
                             float y, float size, float grey, float alpha, int shadow);
/* The same in a colour (0-255 each). */
void khdays_layout_draw_line_rgb(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const unsigned short *t, float x,
                                 float y, float size, const Uint8 rgb[3], float alpha, int shadow);
/* A line where a text style puts it (offset by dx, dy), its shadow first (none with the font's
 * own colours). */
void khdays_layout_draw_text(const KhdaysLayout *l, const KhdaysLayoutFrame *fr, const char *style,
                             const unsigned short *t, float dx, float dy, float alpha);
/* An oval and a rectangle with round corners filled with a colour (0-255), in the picture's texels;
 * and their rings, `thickness` inside their edge. */
void khdays_layout_fill_oval(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float cx, float cy, float rx,
                             float ry, const Uint8 colour[3], float alpha);
void khdays_layout_ring_oval(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float cx, float cy, float rx,
                             float ry, float thickness, const Uint8 colour[3], float alpha);
void khdays_layout_fill_rounded(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float x, float y, float w, float h,
                                float radius, const Uint8 colour[3], float alpha);
void khdays_layout_ring_rounded(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float x, float y, float w, float h,
                                float radius, float thickness, const Uint8 colour[3], float alpha);
/* The game's fade over the whole window: a master brightness (-16 black to 16 white). */
void khdays_layout_shade(SDL_Renderer *renderer, int master);
/* The renderer is gone: the input icons' textures with it. */
void khdays_layout_renderer_gone(void);

/* The pointing glove's sway, as KH2's (its camp.2ld's and pause.2ld's glove): 10.4 layout units each way
 * about 5, a swing every 89 / 3 of the game's frames, whatever the refresh rate (a glove drawn at its place +
 * this). Every menu's glove sways so (ui/composition.c's `sway` too). */
#define KHDAYS_GLOVE_FRAMES (89.0 / 3.0)
#define KHDAYS_GLOVE_MIDDLE 5.0f
#define KHDAYS_GLOVE_REACH 10.4f /* KH2's 4 dots, at 1080 / 416 */
float khdays_layout_glove_sway(void);

/* Kingdom Hearts II's cursor light, "the firefly" (its pause.2ld's animation groups 7 to 9: a gold glow
 * that grows in, then wanders over a small figure, added to what is under it), by the selected entry:
 * its picture `layouts/kh2pause/firefly.png` (the HD PACK's, from the player's collection). Each screen
 * keeps its own (`which`): drawn again within a quarter second it flies over to where it is drawn now,
 * else it grows in there. (x, y) its middle in the picture's units; `unit` the picture's units for one
 * of KH2's screen pixels (KHDAYS_FIREFLY_UNIT on a full screen of the picture). Nothing without the
 * picture. */
enum {
    KHDAYS_FIREFLY_SAVE,
    KHDAYS_FIREFLY_CAMP,
    KHDAYS_FIREFLY_CINE,
    KHDAYS_FIREFLY_ROOM,
    KHDAYS_FIREFLY_PAUSE,
    KHDAYS_FIREFLY_GAME, /* the game's own menus' composition (ui/composition.c) */
    KHDAYS_FIREFLY_TAB,  /* the camp's pages' option beside the MENU or SAVE pill (camp_screen.c tab_button) */
    KHDAYS_FIREFLY_SHOP, /* the Moogle Shop's menu (shop_screen.c) */
    KHDAYS_FIREFLY_COUNT
};
#define KHDAYS_FIREFLY_UNIT (1080.0f / 416.0f)
void khdays_layout_firefly(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, int which, float x, float y, float unit,
                           float alpha);

#endif
