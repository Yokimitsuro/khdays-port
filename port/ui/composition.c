/* The one-screen compositions (see composition.h).
 * The file is read once into sections. For
 * each frame, the sections for its situation whose conditions hold are taken
 * in the file's order and made into the pass's shapes for the picture's
 * width. */
#include "composition.h"
#include "../host/app/files.h"
#include "../host/render/png_io.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"
#include "../runtime/mods/installed.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The port's own compositions (compositions/port_days.txt), compiled in by CMake. */
extern const unsigned char khdays_compositions_port[];
extern const unsigned int khdays_compositions_port_size;

enum {
    CORNER_CENTRE,
    CORNER_TOP_LEFT,
    CORNER_TOP,
    CORNER_TOP_RIGHT,
    CORNER_RIGHT,
    CORNER_BOTTOM_RIGHT,
    CORNER_BOTTOM,
    CORNER_BOTTOM_LEFT,
    CORNER_LEFT,
    CORNER_KEEP
};
static const char *const corner_names[] = {"centre",       "top-left", "top",         "top-right", "right",
                                           "bottom-right", "bottom",   "bottom-left", "left",      "keep"};
static const char *const screens_names[] = {"upper", "lower", "stacked", "side-by-side"};
static const char *const brightness_names[] = {"default", "upper", "lower", "halves", "own"};
/* the HUD's screen-space 3D: the polygons the HUD's own drawing sends (gfx/g3d.h KHDAYS_G3D_HUD) */
static const char *const group_names[] = {"hud", "all"}; /* KHDAYS_SHAPE3D_* */

/* `push <state>[|<state>...] <dx> <dy> <frames>`: a shape moved by dx, dy (the port's units) while one of the
 * states holds, easing there and back over that many of the game's frames; its progress kept from frame to
 * frame. */
typedef struct Push {
    uint32_t states; /* 1 << KHDAYS_ST_*; 0 none */
    float dx, dy, frames;
    float t;     /* 0 at its place, 1 moved */
    uint32_t at; /* the game's frame t was last brought to */
} Push;

/* A shape as the file gives it: its lengths in the port's units (hud_factor), each a part at the
 * HUD's scale and a part that is not (`fixed(N)`). */
typedef struct ShapeDef {
    Push push;
    int lower;
    int whole; /* no rectangle: the whole screen */
    float x, y, w, h;
    int corner;
    float margin[4];       /* left, top, right, bottom */
    float margin_fixed[4]; /* their `fixed(N)` parts */
    float sx, sy;
    float sx_fixed, sy_fixed;
    int capped;           /* `at-most <length>` after the scale: never larger than it */
    float cap, cap_fixed; /* that length, at the HUD's scale and fixed */
    int cover;            /* `scale cover`: as large as covers the picture, its proportions kept */
    uint32_t flags;       /* KOF_* */
    float fade[4], opacity;
    float round;          /* `round <r>`: the corners' radius, source dots */
    uint32_t see_through; /* KO_SEE_THROUGH: a colour and its opacity */
    int key_first, key_count;
} ShapeDef;

typedef struct Shape3dDef {
    Push push;
    int group;
    int hide;
    float x, y, w, h;
    int corner;
    float margin[4];
    float scale;
    int fixed; /* `scale fixed(N)`: its scale and margins do not follow the HUD's */
} Shape3dDef;

enum { D_SCREENS, D_BRIGHTNESS, D_SHAPE, D_SHAPE3D, D_BACKDROP, D_PRELOAD, D_HIDE3D_ALL, D_MENU, D_HALVES, D_STOP };
typedef struct Directive {
    int kind;
    int value; /* D_SCREENS, D_BRIGHTNESS: the choice; D_SHAPE, D_SHAPE3D: its index; D_BACKDROP,
                 * D_PRELOAD: its name's (backdrops') */
} Directive;

typedef struct Condition {
    int state; /* KHDAYS_ST_*, or -1 for the picture's width or a mod */
    int negate;
    int width; /* 1 `wide` (16:9 or wider), 2 `narrow` (4:3); 0 none */
    int mod;   /* `mod-<id>`: a mod of the port's features installed (runtime/mods/installed.h): its id + 1; 0 none */
} Condition;

/* The picture's columns the compositions are made for this frame (`wide`, `narrow`). */
static int current_columns = 256;
#define WIDE_COLUMNS 336 /* 16:9's 342 and wider; 16:10's 308 is neither wide nor narrow */

typedef struct Section {
    int scene;
    int first_condition, conditions;
    int first_directive, directives;
} Section;

/* A picture behind everything (`backdrop <file> [at <corner>] [fit]`): its name, which may hold the
 * title menu's state, its anchor, and whether it fits inside the picture instead of covering it. */
typedef struct Backdrop {
    char *name;
    float ax, ay;
    int fit;
} Backdrop;

/* A PNG read on a thread of its own (`preload`): its file, and what the thread read. */
typedef struct Reading {
    SDL_Thread *thread;
    char path[1024];
    uint8_t *rgba;
    int w, h;
    const char *error;
} Reading;

/* A picture read for a backdrop's name: from a mod's layouts/ folder, or not there (rgba NULL). */
typedef struct Picture {
    char *name;
    uint8_t *rgba;
    int w, h;
    Reading *reading; /* still being read ahead, or NULL */
} Picture;

/* An animated picture (`backdrop <file>.txt`, composition.h): its clips, their pieces and the
 * pieces' keys. */
static const char *const blend_names[] = {"over", "add", "take"}; /* KHDAYS_PIECE_* */

typedef struct AnimKey {
    float frame;
    float v[8]; /* x0 y0 x1 y1, r g b a */
} AnimKey;

typedef struct AnimPiece {
    float texels[4];
    float corners[16];
    uint32_t blend;
    int first_key, keys;
    char *prompt; /* `prompt <action>`, or NULL */
    float sway;   /* `sway <units> <frames>`: KH2's pointing glove's swing, about half `units` each way, or 0 */
    /* `wander <units> <x swings> <y swings> <frames>`: KH2's firefly's figure, `units` each way, or 0 */
    float wander, wander_x, wander_y, wander_frames;
} AnimPiece;

/* An input's icon in the atlas (`icon`), and a rectangle the mouse clicks (`button`). */
typedef struct AnimIcon {
    char *name;
    float texels[4];
} AnimIcon;

typedef struct AnimButton {
    char *action;
    float rect[4];
} AnimButton;

typedef struct AnimClip {
    int frames, first_piece, pieces;
    char *after; /* the groups it follows, separated by spaces; NULL: the loop */
} AnimClip;

typedef struct Animation {
    char *name; /* as the backdrop's name gave it */
    char *group;
    int ok;    /* read, its atlas there */
    int atlas; /* its picture (pictures' index) */
    int picture_w, picture_h;
    uint32_t ground;
    int first_clip, clips;
    int loop; /* its loop (anim_clips' index), or -1 */
    int first_icon, icons;
    int first_button, buttons;
} Animation;

/* The player's HUD size, 1-8 (the settings' hud_size, 4 the default). */
static int hud_size = 4;

/* The port's units: a length is in the picture's DS dots at the HUD's scale, a factor of the HUD
 * size -- 1 at the default size 4, the others chosen from pictures of the field's HUD at each
 * size; `fixed(N)` a length that does not follow it. One scale for the 2D and the 3D shapes. */
static const float hud_factors[8] = {0.6f, 0.7f, 0.85f, 1.0f, 1.15f, 1.3f, 1.5f, 1.75f};

static float hud_factor(void)
{
    return hud_factors[hud_size - 1];
}

/* The file as read (in arrays that grow: no fixed count). */
static Section *sections;
static int section_count, section_room;
static Condition *conditions;
static int condition_count, condition_room;
static Directive *directives;
static int directive_count, directive_room;
static ShapeDef *shape_defs;
static int shape_def_count, shape_def_room;
static Shape3dDef *shape3d_defs;
static int shape3d_def_count, shape3d_def_room;
static uint32_t *def_keys;
static int def_key_count, def_key_room;
static Backdrop *backdrops;
static int backdrop_count, backdrop_room;
static Picture *pictures;
static int picture_count, picture_room;
static const Picture *picture_ready(int i);
static Animation *animations;
static int animation_count, animation_room;
static AnimClip *anim_clips;
static int anim_clip_count, anim_clip_room;
static AnimPiece *anim_pieces;
static int anim_piece_count, anim_piece_room;
static AnimKey *anim_keys;
static int anim_key_count, anim_key_room;
static AnimIcon *anim_icons;
static int anim_icon_count, anim_icon_room;
static AnimButton *anim_buttons;
static int anim_button_count, anim_button_room;
/* The inputs to show for one action (khdays_composition_set_prompt). */
static char *prompt_action;
static char **prompt_inputs;
static int prompt_input_count, prompt_input_room;
static int loaded;

/* This frame's composition (khdays_composition_for). */
static KhdaysShape *shapes;
static int shape_room;
static uint32_t *keys;
static int key_room;
static KhdaysShape3d *shapes3d;
static int shape3d_room;
static KhdaysPiece *pieces_now;
static int pieces_now_room;
static KhdaysButton *buttons_now;
static int buttons_now_room;
/* The animated picture the last composition showed (-1: none), the clip it plays (anim_clips'
 * index, -1 its loop) and the frame that began. */
static int last_animation = -1;
static int clip_playing = -1;
static uint32_t clip_began;
/* With the Menu Overhaul (runtime/mods/installed.h), a clip that follows another level of the menu (not the
 * picture's first showing) plays CLIP_FAST times as fast: the game takes the next press within a few frames,
 * and the clips made from the theater's left the menu unread well after it was ready. */
#define CLIP_FAST 2u
static int clip_fast;

/* Makes room in *array (`count` items of `size` bytes) for one more item. */
static int grow(void **array, int *room, int count, size_t size)
{
    if (count < *room) {
        return 1;
    }
    {
        const int want = *room != 0 ? *room * 2 : 16;
        void *more = realloc(*array, (size_t)want * size);
        if (more == NULL) {
            return 0;
        }
        *array = more;
        *room = want;
    }
    return 1;
}

static void say(const char *file, int line, const char *what, const char *text)
{
    fprintf(stderr, "one screen: %s, line %d: %s: %s (the line is ignored)\n", file, line, what, text);
    khdays_log("one screen: %s, line %d: %s: %s (the line is ignored)", file, line, what, text);
}

static int number(const char *s, float *out)
{
    char *end;
    const double v = strtod(s, &end);
    if (s == NULL || end == s || *end != '\0') {
        return 0;
    }
    *out = (float)v;
    return 1;
}

/* `fixed(N)`: N. */
static int fixed_value(const char *s, float *out)
{
    char *end;
    double v;
    if (strncmp(s, "fixed(", 6) != 0) {
        return 0;
    }
    v = strtod(s + 6, &end);
    if (end == s + 6 || strcmp(end, ")") != 0) {
        return 0;
    }
    *out = (float)v;
    return 1;
}

/* A length: `N` (at the HUD's scale), `fixed(N)` or `A+fixed(B)`, as *a at the HUD's scale and *b
 * fixed. *a and *b are left as they were when `s` is none of these. */
static int measure(const char *s, float *a, float *b)
{
    char *end;
    double first;
    const char *plus;
    if (s == NULL) {
        return 0;
    }
    plus = strchr(s, '+');
    if (fixed_value(s, b)) {
        *a = 0.0f;
        return 1;
    }
    first = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    if (*end == '\0') {
        *a = (float)first;
        *b = 0.0f;
        return 1;
    }
    if (plus == end && fixed_value(plus + 1, b)) {
        *a = (float)first;
        return 1;
    }
    return 0;
}

static int four_measures(char **t, int n, int at, float out[4], float fixed[4])
{
    if (at + 4 > n) {
        return 0;
    }
    for (int i = 0; i < 4; ++i) {
        if (!measure(t[at + i], &out[i], &fixed[i])) {
            return 0;
        }
    }
    return 1;
}

static int named(const char *const *names, int count, const char *s)
{
    for (int i = 0; s != NULL && i < count; ++i) {
        if (strcmp(names[i], s) == 0) {
            return i;
        }
    }
    return -1;
}

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* An 8-bit colour as the file gives it, in the DS's 5 bits a channel (BGR555). */
static int colour(char **t, int n, int at, uint32_t *out)
{
    float c[3];
    if (at + 3 > n || !number(t[at], &c[0]) || !number(t[at + 1], &c[1]) || !number(t[at + 2], &c[2])) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        if (c[i] < 0.0f || c[i] > 255.0f) {
            return 0;
        }
    }
    *out = ((uint32_t)c[0] >> 3) | (((uint32_t)c[1] >> 3) << 5) | (((uint32_t)c[2] >> 3) << 10);
    return 1;
}

static int four(char **t, int n, int at, float out[4])
{
    if (at + 4 > n) {
        return 0;
    }
    for (int i = 0; i < 4; ++i) {
        if (!number(t[at + i], &out[i])) {
            return 0;
        }
    }
    return 1;
}

/* `push <state>[|<state>...] <dx> <dy> <frames>` at t[at]; 1 when read. */
static int read_push(char **t, int n, int at, Push *p)
{
    float v[3];
    int state;
    uint32_t states = 0;
    if (at + 4 >= n || !number(t[at + 2], &v[0]) || !number(t[at + 3], &v[1]) || !number(t[at + 4], &v[2]) ||
        v[2] < 1.0f) {
        return 0;
    }
    for (const char *w = t[at + 1]; *w != '\0';) {
        const char *bar = strchr(w, '|');
        const int len = bar != NULL ? (int)(bar - w) : (int)strlen(w);
        if ((state = khdays_situation_state_named(w, len)) < 0 || state >= 32) {
            return 0;
        }
        states |= 1u << state;
        w += len + (bar != NULL);
    }
    p->states = states;
    p->dx = v[0];
    p->dy = v[1];
    p->frames = v[2];
    p->t = 0.0f;
    p->at = 0;
    return 1;
}

/* How far a push has its shape this frame (0 none, 1 all the way), eased; brought toward its states' truth by
 * the frames since it was last seen (a shape not shown for a while is where its states say). */
static float push_now(Push *p, const KhdaysSituation *situation)
{
    float step, target;
    if (p->states == 0) {
        return 0.0f;
    }
    target = (situation->states & p->states) != 0 ? 1.0f : 0.0f;
    step = p->at == 0 ? p->frames : (float)(uint32_t)(situation->frame - p->at);
    p->at = situation->frame != 0 ? situation->frame : 1u;
    step /= p->frames;
    if (step > 1.0f) {
        step = 1.0f;
    }
    p->t =
        p->t < target ? (p->t + step < target ? p->t + step : target) : (p->t - step > target ? p->t - step : target);
    return p->t * p->t * (3.0f - 2.0f * p->t); /* smoothstep */
}

/* Reads `shape <upper|lower> [<x> <y> <w> <h>] at <corner> ...`. Returns 1,
 * or 0 with the reason. */
static int read_shape(char **t, int n, ShapeDef *s, const char **why)
{
    int i = 1;
    memset(s, 0, sizeof(*s));
    s->sx = s->sy = 1.0f;
    s->opacity = 1.0f;
    s->key_first = def_key_count;
    if (i >= n || (strcmp(t[i], "upper") != 0 && strcmp(t[i], "lower") != 0)) {
        *why = "a shape is taken from the upper or the lower screen";
        return 0;
    }
    s->lower = strcmp(t[i++], "lower") == 0;
    s->whole = 1;
    if (i + 4 <= n && strcmp(t[i], "at") != 0) {
        if (!number(t[i], &s->x) || !number(t[i + 1], &s->y) || !number(t[i + 2], &s->w) || !number(t[i + 3], &s->h)) {
            *why = "a rectangle is four numbers";
            return 0;
        }
        if (s->x < 0.0f || s->y < 0.0f || s->w <= 0.0f || s->x + s->w > 256.0f || s->h <= 0.0f ||
            s->y + s->h > 192.0f) {
            *why = "the rectangle is not within its screen";
            return 0;
        }
        s->whole = 0;
        i += 4;
    }
    if (s->whole) {
        s->w = 256.0f;
        s->h = 192.0f;
    }
    if (i + 1 >= n || strcmp(t[i], "at") != 0 || (s->corner = named(corner_names, COUNT(corner_names), t[i + 1])) < 0) {
        *why = "`at` and a corner";
        return 0;
    }
    i += 2;
    while (i < n) {
        const char *w = t[i];
        if (strcmp(w, "margin") == 0 && four_measures(t, n, i + 1, s->margin, s->margin_fixed)) {
            i += 5;
        } else if (strcmp(w, "scale") == 0 && i + 1 < n && strcmp(t[i + 1], "cover") == 0) {
            s->cover = 1;
            i += 2;
        } else if (strcmp(w, "scale") == 0 && i + 1 < n && measure(t[i + 1], &s->sx, &s->sx_fixed)) {
            s->sy = s->sx;
            s->sy_fixed = s->sx_fixed;
            i += 2;
            if (i < n && measure(t[i], &s->sy, &s->sy_fixed)) {
                ++i;
            }
            if (s->sx < 0.0f || s->sx_fixed < 0.0f || s->sx + s->sx_fixed <= 0.0f || s->sy < 0.0f ||
                s->sy_fixed < 0.0f || s->sy + s->sy_fixed <= 0.0f) {
                *why = "a scale is above 0";
                return 0;
            }
            if (i + 1 < n && strcmp(t[i], "at-most") == 0) {
                if (!measure(t[i + 1], &s->cap, &s->cap_fixed) || s->cap < 0.0f || s->cap_fixed < 0.0f ||
                    s->cap + s->cap_fixed <= 0.0f) {
                    *why = "`at-most` and a length above 0";
                    return 0;
                }
                s->capped = 1;
                i += 2;
            }
        } else if (strcmp(w, "mirror") == 0 && i + 1 < n &&
                   (strcmp(t[i + 1], "x") == 0 || strcmp(t[i + 1], "y") == 0 || strcmp(t[i + 1], "xy") == 0)) {
            s->flags |= (strchr(t[i + 1], 'x') != NULL ? KOF_MIRROR_X : 0u) |
                        (strchr(t[i + 1], 'y') != NULL ? KOF_MIRROR_Y : 0u);
            i += 2;
        } else if (strcmp(w, "repeat") == 0) {
            if (i + 1 < n && strcmp(t[i + 1], "x") == 0) {
                s->flags |= KOF_REPEAT_X;
                i += 2;
            } else if (i + 1 < n && strcmp(t[i + 1], "y") == 0) {
                s->flags |= KOF_REPEAT_Y;
                i += 2;
            } else {
                s->flags |= KOF_REPEAT_X | KOF_REPEAT_Y;
                ++i;
            }
        } else if (strcmp(w, "3d") == 0) {
            /* the picture's 3D where the shape is, not the shape's source */
            s->flags |= KOF_3D;
            ++i;
        } else if (strcmp(w, "force") == 0) {
            s->flags |= KOF_FORCE;
            ++i;
        } else if (strcmp(w, "round") == 0 && i + 1 < n && number(t[i + 1], &s->round) && s->round > 0.0f) {
            /* `round <r>`: the corners rounded, a radius in the source's dots */
            s->flags |= KOF_ROUND;
            i += 2;
        } else if (strcmp(w, "key") == 0) {
            uint32_t c;
            if (!colour(t, n, i + 1, &c) || !grow((void **)&def_keys, &def_key_room, def_key_count, sizeof(uint32_t))) {
                *why = "a key is a colour: three values 0-255";
                return 0;
            }
            def_keys[def_key_count++] = c;
            ++s->key_count;
            i += 4;
        } else if (strcmp(w, "see-through") == 0) {
            /* `see-through <r> <g> <b> <a>`: the texels of that colour drawn at opacity a */
            uint32_t c;
            float a;
            if (!colour(t, n, i + 1, &c) || i + 4 >= n || !number(t[i + 4], &a) || a < 0.0f || a > 1.0f) {
                *why = "see-through is a colour (three values 0-255) and an opacity 0-1";
                return 0;
            }
            s->see_through = c | (uint32_t)(a * 255.0f + 0.5f) << 16;
            s->flags |= KOF_SEE_THROUGH;
            i += 5;
        } else if (strcmp(w, "dim") == 0 && i + 1 < n && (s->flags & KOF_WEIGHT) == 0u &&
                   number(t[i + 1], &s->opacity) && s->opacity >= 0.0f && s->opacity <= 1.0f) {
            /* `dim <f>`: the shape's colours times f, toward black (not mixed with what is under
             * it, as opacity is) */
            s->flags |= KOF_DIM;
            i += 2;
        } else if (strcmp(w, "opacity") == 0 && i + 1 < n && (s->flags & KOF_DIM) == 0u &&
                   number(t[i + 1], &s->opacity) && s->opacity >= 0.0f && s->opacity <= 1.0f) {
            s->flags |= KOF_WEIGHT;
            i += 2;
        } else if (strcmp(w, "fade") == 0 && (s->flags & KOF_DIM) == 0u && four(t, n, i + 1, s->fade)) {
            s->flags |= KOF_WEIGHT;
            i += 5;
        } else if (strcmp(w, "push") == 0 && read_push(t, n, i, &s->push)) {
            i += 5;
        } else {
            *why = "an effect this version does not know, or its values";
            return 0;
        }
    }
    return 1;
}

/* `shape3d <group> [<x> <y>] [<w> <h>] at <corner> [margin ...] [scale <s>]`, `hide3d <group>`. */
static int read_shape3d(char **t, int n, Shape3dDef *s, const char **why)
{
    int i = 2;
    memset(s, 0, sizeof(*s));
    s->scale = 1.0f;
    s->hide = strcmp(t[0], "hide3d") == 0;
    if (n < 2 || (s->group = named(group_names, COUNT(group_names), t[1])) < 0) {
        *why = "a group of the HUD's 3D polygons";
        return 0;
    }
    if (s->hide) {
        /* every piece of the group: a rectangle that holds them all, wherever they are */
        s->x = s->y = -100000.0f;
        s->w = s->h = 200000.0f;
        return n == 2 || (*why = "hide3d takes a group only", 0);
    }
    s->w = 256.0f;
    s->h = 192.0f;
    if (i + 4 <= n && strcmp(t[i + 2], "at") != 0 && number(t[i], &s->x) && number(t[i + 1], &s->y) &&
        number(t[i + 2], &s->w) && number(t[i + 3], &s->h)) {
        i += 4;
    } else if (i + 2 <= n && number(t[i], &s->w) && number(t[i + 1], &s->h)) {
        i += 2;
    }
    if (s->w <= 0.0f || s->h <= 0.0f) {
        *why = "the rectangle is not within its screen";
        return 0;
    }
    if (i + 1 >= n || strcmp(t[i], "at") != 0 || (s->corner = named(corner_names, COUNT(corner_names), t[i + 1])) < 0) {
        *why = "`at` and a corner";
        return 0;
    }
    i += 2;
    while (i < n) {
        if (strcmp(t[i], "margin") == 0 && four(t, n, i + 1, s->margin)) {
            i += 5;
        } else if (strcmp(t[i], "scale") == 0 && i + 1 < n && fixed_value(t[i + 1], &s->scale) && s->scale > 0.0f) {
            s->fixed = 1;
            i += 2;
        } else if (strcmp(t[i], "scale") == 0 && i + 1 < n && number(t[i + 1], &s->scale) && s->scale > 0.0f) {
            i += 2;
        } else if (strcmp(t[i], "push") == 0 && read_push(t, n, i, &s->push)) {
            i += 5;
        } else {
            *why = "margin, scale or push";
            return 0;
        }
    }
    return 1;
}

static int add_directive(int kind, int value)
{
    if (!grow((void **)&directives, &directive_room, directive_count, sizeof(Directive))) {
        return 0;
    }
    directives[directive_count].kind = kind;
    directives[directive_count].value = value;
    ++directive_count;
    ++sections[section_count - 1].directives;
    return 1;
}

/* `backdrop <file> [at <corner>] [fit]`, or `preload <file>` */
static int read_backdrop(char **t, int n, const char **why)
{
    static const float anchor[][2] = {{0.5f, 0.5f}, {0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.5f},
                                      {1.0f, 1.0f}, {0.5f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.5f}, {0.5f, 0.5f}};
    const int preload = strcmp(t[0], "preload") == 0;
    Backdrop b;
    int corner = CORNER_CENTRE;
    b.fit = !preload && n > 2 && strcmp(t[n - 1], "fit") == 0;
    if (b.fit) {
        --n;
    }
    if (preload ? n != 2 : n != 2 && !(n == 4 && strcmp(t[2], "at") == 0)) {
        *why = preload ? "preload <file>" : "backdrop <file> [at <corner>] [fit]";
        return 0;
    }
    if (n == 4 && (corner = named(corner_names, COUNT(corner_names), t[3])) < 0) {
        *why = "an unknown corner";
        return 0;
    }
    b.name = (char *)malloc(strlen(t[1]) + 1);
    if (b.name == NULL || !grow((void **)&backdrops, &backdrop_room, backdrop_count, sizeof(Backdrop)) ||
        !add_directive(preload ? D_PRELOAD : D_BACKDROP, backdrop_count)) {
        free(b.name);
        *why = "no memory";
        return 0;
    }
    strcpy(b.name, t[1]);
    b.ax = anchor[corner][0];
    b.ay = anchor[corner][1];
    backdrops[backdrop_count++] = b;
    return 1;
}

/* `[<situation> [if <condition> ...]]` */
static int read_section(char **t, int n, const char **why)
{
    Section *s;
    int i = 1;
    const int scene = khdays_situation_named(t[0], (int)strlen(t[0]));
    if (scene < 0) {
        *why = "an unknown situation";
        return 0;
    }
    if (!grow((void **)&sections, &section_room, section_count, sizeof(Section))) {
        return 0;
    }
    s = &sections[section_count];
    s->scene = scene;
    s->first_condition = condition_count;
    s->conditions = 0;
    s->first_directive = directive_count;
    s->directives = 0;
    if (n > 1) {
        if (strcmp(t[1], "if") != 0) {
            *why = "conditions follow `if`";
            return 0;
        }
        i = 2;
    }
    for (; i < n; ++i) {
        Condition c;
        const char *w = t[i];
        c.negate = w[0] == '!';
        w += c.negate;
        c.width = 0;
        c.mod = 0;
        c.state = khdays_situation_state_named(w, (int)strlen(w));
        if (strcmp(w, "wide") == 0 || strcmp(w, "narrow") == 0) {
            c.state = -1;
            c.width = w[0] == 'w' ? 1 : 2;
        } else if (strncmp(w, "mod-", 4) == 0 && khdays_mod_named(w + 4) >= 0) {
            c.state = -1;
            c.mod = khdays_mod_named(w + 4) + 1;
        } else if (c.state < 0) {
            *why = "an unknown state";
            return 0;
        }
        if (!grow((void **)&conditions, &condition_room, condition_count, sizeof(Condition))) {
            return 0;
        }
        conditions[condition_count++] = c;
        ++s->conditions;
    }
    ++section_count;
    return 1;
}

/* A line's words (the list grows). */
static char **line_words;
static int word_room;

static int split(char *line, const char *separators)
{
    int n = 0;
    for (char *w = strtok(line, separators); w != NULL; w = strtok(NULL, separators)) {
        if (!grow((void **)&line_words, &word_room, n, sizeof(char *))) {
            return -1;
        }
        line_words[n++] = w;
    }
    return n;
}

static void read_text(const char *file, char *text)
{
    int line = 0;
    const int first_section = section_count;
    char *next;
    for (char *at = text; at != NULL; at = next) {
        char *c, *copy, *shown;
        const char *why = NULL;
        int n;
        ++line;
        next = strchr(at, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if ((c = strchr(at, '#')) != NULL) {
            *c = '\0';
        }
        shown = (char *)malloc(strlen(at) + 1); /* the line as written, for the reports */
        copy = (char *)malloc(strlen(at) + 1);
        if (shown == NULL || copy == NULL) {
            free(shown);
            free(copy);
            return;
        }
        strcpy(shown, at);
        strcpy(copy, at);
        n = split(at, " \t\r");
        if (n < 0) {
            why = "no memory";
        } else if (n == 0) {
            free(shown);
            free(copy);
            continue;
        } else if (line_words[0][0] == '[') {
            char *close = strchr(copy, ']');
            if (close == NULL) {
                why = "a section's name ends with ]";
            } else {
                *close = '\0';
                n = split(strchr(copy, '[') + 1, " \t");
                if (n <= 0 || !read_section(line_words, n, &why)) {
                    why = why != NULL ? why : "a section names a situation";
                }
            }
        } else if (strcmp(line_words[0], "units") == 0) {
            /* the port's units, the only ones: the line says so */
            if (n != 2 || strcmp(line_words[1], "port") != 0 || section_count != first_section) {
                why = "`units port`, before the file's first section";
            }
        } else if (section_count == 0) {
            why = "a line before the first section";
        } else if (strcmp(line_words[0], "screens") == 0) {
            const int v = n == 2 ? named(screens_names, COUNT(screens_names), line_words[1]) : -1;
            if (v < 0 || !add_directive(D_SCREENS, v)) {
                why = "screens upper, lower, stacked or side-by-side";
            }
        } else if (strcmp(line_words[0], "brightness") == 0) {
            const int v = n == 2 ? named(brightness_names, COUNT(brightness_names), line_words[1]) : -1;
            if (v < 0 || !add_directive(D_BRIGHTNESS, v)) {
                why = "brightness default, upper, lower, halves or own";
            }
        } else if (strcmp(line_words[0], "shape") == 0) {
            ShapeDef s;
            const int keys_before = def_key_count;
            if (!read_shape(line_words, n, &s, &why)) {
                def_key_count = keys_before;
            } else if (!grow((void **)&shape_defs, &shape_def_room, shape_def_count, sizeof(ShapeDef)) ||
                       !add_directive(D_SHAPE, shape_def_count)) {
                why = "no memory";
            } else {
                shape_defs[shape_def_count++] = s;
            }
        } else if (strcmp(line_words[0], "backdrop") == 0 || strcmp(line_words[0], "preload") == 0) {
            read_backdrop(line_words, n, &why);
        } else if (strcmp(line_words[0], "hide3d") == 0 && n == 2 && strcmp(line_words[1], "all") == 0) {
            if (!add_directive(D_HIDE3D_ALL, 0)) {
                why = "no memory";
            }
        } else if (strcmp(line_words[0], "shape3d") == 0 || strcmp(line_words[0], "hide3d") == 0) {
            Shape3dDef s;
            if (read_shape3d(line_words, n, &s, &why)) {
                if (!grow((void **)&shape3d_defs, &shape3d_def_room, shape3d_def_count, sizeof(Shape3dDef)) ||
                    !add_directive(D_SHAPE3D, shape3d_def_count)) {
                    why = "no memory";
                } else {
                    shape3d_defs[shape3d_def_count++] = s;
                }
            }
        } else if (strcmp(line_words[0], "menu") == 0) {
            if (n != 1 || !add_directive(D_MENU, 0)) {
                why = "menu takes nothing";
            }
        } else if (strcmp(line_words[0], "halves") == 0) {
            if (n != 1 || !add_directive(D_HALVES, 0)) {
                why = "halves takes nothing";
            }
        } else if (strcmp(line_words[0], "stop") == 0) {
            if (n != 1 || !add_directive(D_STOP, 0)) {
                why = "stop takes nothing";
            }
        } else {
            why = "an unknown line";
        }
        if (why != NULL) {
            say(file, line, why, shown);
        }
        free(shown);
        free(copy);
    }
}

/* A file's text, ended by a NUL, to be freed; NULL when it cannot be read. */
static char *read_whole(const char *path)
{
    size_t room = 0x10000;
    long got = -1;
    char *text = NULL;
    for (;;) {
        char *grown = (char *)realloc(text, room + 1);
        if (grown == NULL) {
            free(text);
            return NULL;
        }
        text = grown;
        got = khdays_host_read_file(path, text, room);
        if (got < 0 || (size_t)got < room) {
            break;
        }
        room *= 2;
    }
    if (got < 0) {
        free(text);
        return NULL;
    }
    text[got] = '\0';
    return text;
}

/* Whether a file's text begins, after its comments and empty lines, with `units port`: a file in
 * other units is not the port's to read. */
static int in_port_units(const char *text)
{
    const char *at = text;
    while (*at != '\0') {
        const char *end = strchr(at, '\n'), *hash;
        size_t n = end != NULL ? (size_t)(end - at) : strlen(at);
        if ((hash = (const char *)memchr(at, '#', n)) != NULL) {
            n = (size_t)(hash - at);
        }
        while (n > 0 && (at[0] == ' ' || at[0] == '\t')) {
            ++at;
            --n;
        }
        while (n > 0 && (at[n - 1] == ' ' || at[n - 1] == '\t' || at[n - 1] == '\r')) {
            --n;
        }
        if (n > 0) {
            return n == 10 && strncmp(at, "units", 5) == 0 && (at[5] == ' ' || at[5] == '\t') &&
                   strncmp(at + 6, "port", 4) == 0;
        }
        if (end == NULL) {
            break;
        }
        at = end + 1;
    }
    return 0;
}

/* A file compiled into the executable, read as a file. */
static void read_compiled(const char *file, const unsigned char *bytes, unsigned int size)
{
    char *text = (char *)malloc((size_t)size + 1);
    if (text == NULL) {
        return;
    }
    memcpy(text, bytes, size);
    text[size] = '\0';
    read_text(file, text);
    free(text);
}

void khdays_composition_load(void)
{
    char path[1024];
    char *text = NULL;
    const char *file = NULL;
    section_count = condition_count = directive_count = 0;
    shape_def_count = shape3d_def_count = def_key_count = 0;
    for (int b = 0; b < backdrop_count; ++b) {
        free(backdrops[b].name);
    }
    for (int p = 0; p < picture_count; ++p) {
        picture_ready(p);
        free(pictures[p].name);
        khdays_png_free(pictures[p].rgba);
    }
    backdrop_count = picture_count = 0;
    for (int a = 0; a < animation_count; ++a) {
        free(animations[a].name);
        free(animations[a].group);
    }
    for (int c = 0; c < anim_clip_count; ++c) {
        free(anim_clips[c].after);
    }
    for (int p = 0; p < anim_piece_count; ++p) {
#pragma warning(suppress : 6001) /* every item below the count was written before the count moved: none uninitialised */
        free(anim_pieces[p].prompt);
    }
    for (int i = 0; i < anim_icon_count; ++i) {
#pragma warning(suppress : 6001) /* every item below the count was written before the count moved: none uninitialised */
        free(anim_icons[i].name);
    }
    for (int b = 0; b < anim_button_count; ++b) {
#pragma warning(suppress : 6001) /* every item below the count was written before the count moved: none uninitialised */
        free(anim_buttons[b].action);
    }
    animation_count = anim_clip_count = anim_piece_count = anim_key_count = 0;
    anim_icon_count = anim_button_count = 0;
    last_animation = -1;
    if (khdays_mods_find("layouts/compositions.txt", path, (int)sizeof(path))) {
        text = read_whole(path);
        if (text != NULL && !in_port_units(text)) {
            fprintf(stderr, "one screen: %s does not begin with `units port`; the port's own compositions are used\n",
                    path);
            khdays_log("one screen: %s does not begin with `units port`; the port's own compositions are used", path);
            free(text);
            text = NULL;
        } else if (text != NULL) {
            file = path;
        } else {
            fprintf(stderr, "one screen: %s cannot be read; the port's own compositions are used\n", path);
        }
    }
    if (text != NULL) {
        read_text(file, text);
        free(text);
    } else {
        file = "the port's compositions (ui/compositions/port_days.txt)";
        read_compiled("ui/compositions/port_days.txt", khdays_compositions_port, khdays_compositions_port_size);
    }
    loaded = 1;
    khdays_log("one screen: %s read: %d sections, %d shapes, %d 3D shapes", file, section_count, shape_def_count,
               shape3d_def_count);
}

static int holds(const Section *s, const KhdaysSituation *situation)
{
    if (s->scene != situation->scene) {
        return 0;
    }
    for (int i = 0; i < s->conditions; ++i) {
        const Condition *c = &conditions[s->first_condition + i];
        const int truth = c->state >= 0   ? (int)((situation->states >> c->state) & 1u)
                          : c->mod != 0   ? khdays_mod_installed(c->mod - 1)
                          : c->width == 1 ? current_columns >= WIDE_COLUMNS
                                          : current_columns <= 256;
        if (truth == c->negate) {
            return 0;
        }
    }
    return 1;
}

/* A backdrop's name for this frame: its {lang}, {menu}, {count} and {row} the title menu's. */
static void backdrop_name(const char *pattern, const KhdaysSituation *s, char *out, size_t size)
{
    static const char *const languages[] = {"", "en", "fr", "de", "it", "es"}; /* the firmware's 1-5 */
    static const char *const menus[] = {"",      "root", "story",    "mission",
                                        "intro", "logo", "newgame1", "newgame2"}; /* KHDAYS_TITLE_* */
    size_t at = 0;
    for (const char *p = pattern; *p != '\0' && at + 1 < size;) {
        char value[16];
        const char *name = NULL;
        if (strncmp(p, "{lang}", 6) == 0) {
            name = s->title_lang >= 1 && s->title_lang <= 5 ? languages[s->title_lang] : "";
            p += 6;
        } else if (strncmp(p, "{menu}", 6) == 0) {
            name = s->title_menu >= 0 && s->title_menu < (int)(sizeof(menus) / sizeof(menus[0])) ? menus[s->title_menu]
                                                                                                 : "";
            p += 6;
        } else if (strncmp(p, "{count}", 7) == 0) {
            snprintf(value, sizeof(value), "%d", s->title_count);
            name = value;
            p += 7;
        } else if (strncmp(p, "{row}", 5) == 0) {
            snprintf(value, sizeof(value), "%d", s->title_row);
            name = value;
            p += 5;
        }
        if (name != NULL) {
            for (; *name != '\0' && at + 1 < size; ++name) {
                out[at++] = *name;
            }
        } else {
            out[at++] = *p++;
        }
    }
    out[at] = '\0';
}

static int SDLCALL read_ahead(void *data)
{
    Reading *r = (Reading *)data;
    r->rgba = khdays_png_read(r->path, &r->w, &r->h, &r->error);
    return 0;
}

/* A thread reading the PNG at that path; NULL when none could start. */
static Reading *start_reading(const char *path)
{
    Reading *r = (Reading *)calloc(1, sizeof(Reading));
    if (r == NULL) {
        return NULL;
    }
    snprintf(r->path, sizeof(r->path), "%s", path);
    r->thread = SDL_CreateThread(read_ahead, "khdays picture", r);
    if (r->thread == NULL) {
        free(r);
        return NULL;
    }
    return r;
}

/* Picture `i`, its reading ahead waited for: NULL when no mod gives it or it cannot be read. */
static const Picture *picture_ready(int i)
{
    Picture *p = &pictures[i];
    if (p->reading != NULL) {
        Reading *r = p->reading;
        SDL_WaitThread(r->thread, NULL);
        p->rgba = r->rgba;
        p->w = r->w;
        p->h = r->h;
        khdays_log("one screen: the backdrop %s%s%s (read ahead)", r->path, p->rgba != NULL ? "" : " cannot be read: ",
                   p->rgba != NULL ? "" : (r->error != NULL ? r->error : "?"));
        free(r);
        p->reading = NULL;
    }
    return p->rgba != NULL ? p : NULL;
}

/* The picture of that name in a mod's layouts/ folder (pictures' index), read the first time it
 * is asked for (said in the session log, there or not) -- `ahead`: on a thread of its own,
 * picture_ready waiting for it; -1 when no mod gives it or it cannot be read. */
static int picture_entry(const char *name, int ahead)
{
    char relative[600], path[1024];
    const char *error = NULL;
    Picture p;
    for (int i = 0; i < picture_count; ++i) {
        if (strcmp(pictures[i].name, name) == 0) {
            return pictures[i].rgba != NULL || pictures[i].reading != NULL ? i : -1;
        }
    }
    p.name = (char *)malloc(strlen(name) + 1);
    if (p.name == NULL || !grow((void **)&pictures, &picture_room, picture_count, sizeof(Picture))) {
        free(p.name);
        return -1;
    }
    strcpy(p.name, name);
    p.rgba = NULL;
    p.w = p.h = 0;
    p.reading = NULL;
    snprintf(relative, sizeof(relative), "layouts/%s", name);
    if (!khdays_mods_find(relative, path, (int)sizeof(path))) {
        khdays_log("one screen: no mod gives %s", relative);
    } else if (!ahead || (p.reading = start_reading(path)) == NULL) {
        p.rgba = khdays_png_read(path, &p.w, &p.h, &error);
        khdays_log("one screen: the backdrop %s%s%s", path,
                   p.rgba != NULL ? "" : " cannot be read: ", p.rgba != NULL ? "" : (error != NULL ? error : "?"));
    }
    pictures[picture_count++] = p;
    return p.rgba != NULL || p.reading != NULL ? picture_count - 1 : -1;
}

/* The picture of that name, read now if it was not (picture_entry); NULL when none. */
static const Picture *picture_named(const char *name)
{
    const int i = picture_entry(name, 0);
    return i >= 0 ? picture_ready(i) : NULL;
}

static int animated(const char *name)
{
    const size_t n = strlen(name);
    return n > 4 && strcmp(name + n - 4, ".txt") == 0;
}

/* The line's words from `from` on as numbers: whether there were `count`, all numbers. */
static int numbers(int n, int from, int count, float *out)
{
    if (n != from + count) {
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        if (!number(line_words[from + i], &out[i])) {
            return 0;
        }
    }
    return 1;
}

/* Whether `word` is one of the words of `list` (separated by spaces). */
static int listed(const char *list, const char *word)
{
    const size_t n = strlen(word);
    for (const char *at = list; *at != '\0';) {
        const char *end = strchr(at, ' ');
        const size_t len = end != NULL ? (size_t)(end - at) : strlen(at);
        if (len == n && strncmp(at, word, n) == 0) {
            return 1;
        }
        at += len + (end != NULL);
    }
    return 0;
}

/* A clip (`clip <frames> after <group> ...`, or the loop: `loop <frames>`) from the line's words. */
static int read_clip(int n, int is_loop, Animation *a, const char **why)
{
    AnimClip c;
    float frames;
    size_t length = 0;
    if (is_loop ? n != 2 : n < 4 || strcmp(line_words[2], "after") != 0) {
        *why = is_loop ? "loop <frames>" : "clip <frames> after <group> ...";
        return 0;
    }
    if (!number(line_words[1], &frames) || frames < 1.0f) {
        *why = "a clip of no frames";
        return 0;
    }
    if (is_loop && a->loop >= 0) {
        *why = "a second loop";
        return 0;
    }
    memset(&c, 0, sizeof(c));
    c.frames = (int)frames;
    c.first_piece = anim_piece_count;
    for (int i = 3; !is_loop && i < n; ++i) {
        length += strlen(line_words[i]) + 1;
    }
    if (!is_loop && (c.after = (char *)malloc(length)) == NULL) {
        *why = "no memory";
        return 0;
    }
    if (!is_loop) {
        c.after[0] = '\0';
        for (int i = 3; i < n; ++i) {
            strcat(c.after, line_words[i]);
            if (i + 1 < n) {
                strcat(c.after, " ");
            }
        }
    }
    if (!grow((void **)&anim_clips, &anim_clip_room, anim_clip_count, sizeof(AnimClip))) {
        free(c.after);
        *why = "no memory";
        return 0;
    }
    if (a->clips == 0) {
        a->first_clip = anim_clip_count;
    }
    if (is_loop) {
        a->loop = anim_clip_count;
    }
    anim_clips[anim_clip_count++] = c;
    ++a->clips;
    return 1;
}

/* A copy of a word, NULL when there is no memory. */
static char *copied(const char *s)
{
    char *c = (char *)malloc(strlen(s) + 1);
    if (c != NULL) {
        strcpy(c, s);
    }
    return c;
}

void khdays_composition_set_prompt(const char *action, const char *const *inputs, int count)
{
    for (int i = 0; i < prompt_input_count; ++i) {
        free(prompt_inputs[i]);
    }
    prompt_input_count = 0;
    free(prompt_action);
    prompt_action = action != NULL ? copied(action) : NULL;
    for (int i = 0; i < count; ++i) {
        char *c = copied(inputs[i]);
        if (c == NULL || !grow((void **)&prompt_inputs, &prompt_input_room, prompt_input_count, sizeof(char *))) {
            free(c);
            return;
        }
        prompt_inputs[prompt_input_count++] = c;
    }
}

/* Reads the animated picture `name` (a mod's layouts/<name>) into *a, its clips, pieces and keys
 * into the arrays; says in the session log what keeps it from being used. */
static int read_animation(const char *name, Animation *a, int ahead)
{
    char relative[600], path[1024], atlas[600];
    const char *slash;
    char *text;
    int line = 0, piece = -1;
    snprintf(relative, sizeof(relative), "layouts/%s", name);
    if (!khdays_mods_find(relative, path, (int)sizeof(path))) {
        khdays_log("one screen: no mod gives %s", relative);
        return 0;
    }
    text = read_whole(path);
    if (text == NULL) {
        khdays_log("one screen: the animated picture %s cannot be read", path);
        return 0;
    }
    atlas[0] = '\0';
    for (char *at = text, *next; at != NULL; at = next) {
        const char *why = NULL;
        char *hash;
        float v[16];
        int n;
        ++line;
        next = strchr(at, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if ((hash = strchr(at, '#')) != NULL) {
            *hash = '\0';
        }
        n = split(at, " \t\r");
        if (n == 0) {
            continue;
        }
        if (n < 0) {
            why = "no memory";
        } else if (strcmp(line_words[0], "picture") == 0) {
            if (!numbers(n, 1, 2, v) || v[0] < 1.0f || v[1] < 1.0f) {
                why = "picture <width> <height>";
            } else {
                a->picture_w = (int)v[0];
                a->picture_h = (int)v[1];
            }
        } else if (strcmp(line_words[0], "ground") == 0) {
            if (!numbers(n, 1, 3, v)) {
                why = "ground <r> <g> <b>";
            } else {
                a->ground = 0;
                for (int c = 0; c < 3; ++c) {
                    const float k = v[c] < 0.0f ? 0.0f : v[c] > 1.0f ? 1.0f : v[c];
                    a->ground |= (uint32_t)(k * 255.0f + 0.5f) << (8 * c);
                }
            }
        } else if (strcmp(line_words[0], "atlas") == 0 && n == 2) {
            snprintf(atlas, sizeof(atlas), "%s", line_words[1]);
        } else if (strcmp(line_words[0], "group") == 0 && n == 2) {
            free(a->group);
            a->group = (char *)malloc(strlen(line_words[1]) + 1);
            if (a->group == NULL) {
                why = "no memory";
            } else {
                strcpy(a->group, line_words[1]);
            }
        } else if (strcmp(line_words[0], "icon") == 0 || strcmp(line_words[0], "button") == 0) {
            const int is_icon = line_words[0][0] == 'i';
            char *word = n == 6 ? copied(line_words[1]) : NULL;
            if (n != 6 || !numbers(n, 2, 4, v)) {
                why = is_icon ? "icon <name> <x> <y> <width> <height>" : "button <action> <x0> <y0> <x1> <y1>";
            } else if (word == NULL ||
                       !(is_icon ? grow((void **)&anim_icons, &anim_icon_room, anim_icon_count, sizeof(AnimIcon))
                                 : grow((void **)&anim_buttons, &anim_button_room, anim_button_count,
                                        sizeof(AnimButton)))) {
                why = "no memory";
            } else if (is_icon) {
                if (a->icons == 0) {
                    a->first_icon = anim_icon_count;
                }
                anim_icons[anim_icon_count].name = word;
                memcpy(anim_icons[anim_icon_count++].texels, v, 4 * sizeof(float));
                ++a->icons;
                word = NULL;
            } else {
                if (a->buttons == 0) {
                    a->first_button = anim_button_count;
                }
                anim_buttons[anim_button_count].action = word;
                memcpy(anim_buttons[anim_button_count++].rect, v, 4 * sizeof(float));
                ++a->buttons;
                word = NULL;
            }
            free(word);
        } else if (strcmp(line_words[0], "prompt") == 0) {
            if (piece < 0 || n != 2) {
                why = "prompt <action>, after its piece";
            } else {
                free(anim_pieces[piece].prompt);
                if ((anim_pieces[piece].prompt = copied(line_words[1])) == NULL) {
                    why = "no memory";
                }
            }
        } else if (strcmp(line_words[0], "sway") == 0) {
            if (piece < 0 || !numbers(n, 1, 2, v) || v[1] < 1.0f) {
                why = "sway <units> <frames>, after its piece";
            } else {
                anim_pieces[piece].sway = v[0];
            }
        } else if (strcmp(line_words[0], "wander") == 0) {
            if (piece < 0 || !numbers(n, 1, 4, v) || v[3] < 1.0f) {
                why = "wander <units> <x swings> <y swings> <frames>, after its piece";
            } else {
                anim_pieces[piece].wander = v[0];
                anim_pieces[piece].wander_x = v[1];
                anim_pieces[piece].wander_y = v[2];
                anim_pieces[piece].wander_frames = v[3];
            }
        } else if (strcmp(line_words[0], "clip") == 0 || strcmp(line_words[0], "loop") == 0) {
            if (read_clip(n, line_words[0][0] == 'l', a, &why)) {
                piece = -1;
            }
        } else if (strcmp(line_words[0], "piece") == 0) {
            const int blend = n == 22 ? named(blend_names, 3, line_words[5]) : -1;
            float corners[16];
            if (a->clips == 0) {
                why = "a piece before its clip";
            } else if (blend < 0 || !numbers(5, 1, 4, v) || !numbers(22, 6, 16, corners)) {
                why = "piece <x> <y> <width> <height> over|add|take <16 colours>";
            } else if (!grow((void **)&anim_pieces, &anim_piece_room, anim_piece_count, sizeof(AnimPiece))) {
                why = "no memory";
            } else {
                AnimPiece *p = &anim_pieces[anim_piece_count];
                memcpy(p->texels, v, sizeof(p->texels));
                memcpy(p->corners, corners, sizeof(p->corners));
                p->blend = (uint32_t)blend;
                p->first_key = anim_key_count;
                p->keys = 0;
                p->prompt = NULL;
                p->sway = 0.0f;
                p->wander = p->wander_x = p->wander_y = p->wander_frames = 0.0f;
                piece = anim_piece_count++;
                ++anim_clips[anim_clip_count - 1].pieces;
            }
        } else if (strcmp(line_words[0], "key") == 0) {
            if (piece < 0) {
                why = "a key before its piece";
            } else if (!numbers(n, 1, 9, v)) {
                why = "key <frame> <x0> <y0> <x1> <y1> <r> <g> <b> <a>";
            } else if (anim_pieces[piece].keys != 0 &&
                       v[0] < anim_keys[anim_pieces[piece].first_key + anim_pieces[piece].keys - 1].frame) {
                why = "a key before the one above it";
            } else if (!grow((void **)&anim_keys, &anim_key_room, anim_key_count, sizeof(AnimKey))) {
                why = "no memory";
            } else {
                anim_keys[anim_key_count].frame = v[0];
                memcpy(anim_keys[anim_key_count].v, v + 1, sizeof(anim_keys[anim_key_count].v));
                ++anim_key_count;
                ++anim_pieces[piece].keys;
            }
        } else {
            why = "an unknown line";
        }
        if (why != NULL) {
            khdays_log("one screen: %s, line %d: %s; the picture is not used", path, line, why);
            free(text);
            return 0;
        }
    }
    free(text);
    for (int c = a->first_clip; c < a->first_clip + a->clips; ++c) {
        for (int p = 0; p < anim_clips[c].pieces; ++p) {
            if (anim_pieces[anim_clips[c].first_piece + p].keys == 0) {
                khdays_log("one screen: %s: a piece with no key; the picture is not used", path);
                return 0;
            }
        }
    }
    if (a->picture_w == 0 || atlas[0] == '\0') {
        khdays_log("one screen: %s says no picture size or no atlas; the picture is not used", path);
        return 0;
    }
    /* the atlas beside the file */
    slash = strrchr(name, '/');
    snprintf(relative, sizeof(relative), "%.*s%s", slash != NULL ? (int)(slash - name + 1) : 0, name, atlas);
    if ((a->atlas = picture_entry(relative, ahead)) < 0) {
        return 0;
    }
    khdays_log("one screen: the animated picture %s: %d clips", path, a->clips);
    return 1;
}

/* The animated picture of that name, read the first time it is asked for (its atlas read ahead
 * with `ahead`, picture_ready waiting for it); -1 when no mod gives it or it cannot be used. */
static int animation_named(const char *name, int ahead)
{
    Animation a;
    for (int i = 0; i < animation_count; ++i) {
        if (strcmp(animations[i].name, name) == 0) {
            return animations[i].ok && (ahead || picture_ready(animations[i].atlas) != NULL) ? i : -1;
        }
    }
    memset(&a, 0, sizeof(a));
    a.loop = -1;
    a.name = (char *)malloc(strlen(name) + 1);
    if (a.name == NULL || !grow((void **)&animations, &animation_room, animation_count, sizeof(Animation))) {
        free(a.name);
        return -1;
    }
    strcpy(a.name, name);
    a.ok = read_animation(name, &a, ahead);
    if (a.group == NULL) {
        a.group = (char *)calloc(1, 1);
        a.ok = a.ok && a.group != NULL;
    }
    animations[animation_count++] = a;
    return a.ok && (ahead || picture_ready(a.atlas) != NULL) ? animation_count - 1 : -1;
}

/* The icon this animated picture gives for the first of the player's inputs that has one, or
 * NULL. */
static const AnimIcon *prompt_icon(const Animation *an)
{
    for (int i = 0; i < prompt_input_count; ++i) {
        for (int k = an->first_icon; k < an->first_icon + an->icons; ++k) {
            if (strcmp(anim_icons[k].name, prompt_inputs[i]) == 0) {
                return &anim_icons[k];
            }
        }
    }
    return NULL;
}

/* The animated picture's pieces at the game's frame, into pieces_now; their count. After another
 * picture the clip naming that one's group plays, then the loop; after another of its own group (a
 * row selected) the clip and loop go on from where they were, not from their start (a loop's first
 * frame can be far from the one on show). docs/COMPOSITIONS.md. */
static int animation_now(int a, uint32_t frame)
{
    const Animation *an = &animations[a];
    const AnimClip *clip;
    uint32_t t;
    int c, count = 0;
    if (a != last_animation) {
        const Animation *was = last_animation >= 0 ? &animations[last_animation] : NULL;
        if (was != NULL && an->group[0] != '\0' && strcmp(was->group, an->group) == 0) {
            const char *after = clip_playing >= 0 ? anim_clips[clip_playing].after : NULL;
            clip_playing = -1;
            for (int i = an->first_clip; after != NULL && i < an->first_clip + an->clips; ++i) {
                if (anim_clips[i].after != NULL && strcmp(anim_clips[i].after, after) == 0) {
                    clip_playing = i;
                    break;
                }
            }
        } else {
            const char *before = was != NULL ? was->group : "none";
            clip_playing = -1;
            for (int i = an->first_clip; i < an->first_clip + an->clips; ++i) {
                if (anim_clips[i].after != NULL && listed(anim_clips[i].after, before)) {
                    clip_playing = i;
                    break;
                }
            }
            clip_began = frame;
            clip_fast = khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL) && before[0] != '\0' &&
                        strcmp(before, "none") != 0 && strcmp(before, "intro") != 0 && strcmp(before, "logo") != 0;
        }
    }
    t = frame - clip_began;
    c = clip_playing;
    if (c >= 0 && clip_fast) {
        /* its frames CLIP_FAST at a time, then the loop where the clip at its own pace would have left it */
        const uint32_t frames = (uint32_t)anim_clips[c].frames;
        t = t * CLIP_FAST < frames ? t * CLIP_FAST : t + frames - frames / CLIP_FAST;
    }
    if (c < 0 || (t >= (uint32_t)anim_clips[c].frames && an->loop >= 0)) {
        c = an->loop;
    }
    if (c < 0) {
        return 0;
    }
    clip = &anim_clips[c];
    t = c == an->loop ? t % (uint32_t)clip->frames : t < (uint32_t)clip->frames ? t : (uint32_t)clip->frames - 1;
    for (int i = 0; i < clip->pieces; ++i) {
        const AnimPiece *p = &anim_pieces[clip->first_piece + i];
        const AnimKey *k = &anim_keys[p->first_key];
        const AnimIcon *icon = p->prompt != NULL && prompt_action != NULL && strcmp(p->prompt, prompt_action) == 0
                                   ? prompt_icon(an)
                                   : NULL;
        KhdaysPiece *out;
        float v[8];
        int at = 0;
        while (at + 1 < p->keys && k[at + 1].frame <= (float)t) {
            ++at;
        }
        if (at + 1 < p->keys && (float)t > k[at].frame) {
            const float s = ((float)t - k[at].frame) / (k[at + 1].frame - k[at].frame);
            for (int j = 0; j < 8; ++j) {
                v[j] = k[at].v[j] + (k[at + 1].v[j] - k[at].v[j]) * s;
            }
        } else {
            memcpy(v, k[at].v, sizeof(v));
        }
        if (v[7] <= 0.0f || !grow((void **)&pieces_now, &pieces_now_room, count, sizeof(KhdaysPiece))) {
            continue;
        }
        out = &pieces_now[count++];
        memset(out, 0, sizeof(*out));
        memcpy(out->rect, v, sizeof(out->rect));
        if (p->sway != 0.0f) {
            /* the pointing glove, as every menu's (host/screens/layout_screen.h khdays_layout_glove_sway): KH2's
             * swing, about half `sway` and 1.04 times it each way, every 89 / 3 of the game's frames */
            const float swing = 89.0f / 3.0f;
            const float dx = p->sway * (0.5f - 1.04f * sinf(6.28318531f * fmodf((float)frame, swing) / swing));
            out->rect[0] += dx;
            out->rect[2] += dx;
        }
        if (p->wander != 0.0f) {
            /* sin(pi * swings * f / frames) across and down, as KH2's bounces */
            const float f = (float)(frame % (uint32_t)p->wander_frames) / p->wander_frames;
            const float dx = p->wander * sinf(3.14159265f * p->wander_x * f);
            const float dy = p->wander * sinf(3.14159265f * p->wander_y * f);
            out->rect[0] += dx;
            out->rect[2] += dx;
            out->rect[1] += dy;
            out->rect[3] += dy;
        }
        memcpy(out->texels, icon != NULL ? icon->texels : p->texels, sizeof(out->texels));
        if (icon != NULL && icon->texels[3] != 0.0f) {
            /* the icon at the piece's height, as wide as its own shape, its right edge kept */
            out->rect[0] = out->rect[2] - (out->rect[3] - out->rect[1]) * fabsf(icon->texels[2] / icon->texels[3]);
        }
        for (int j = 0; j < 16; ++j) {
            out->corners[j] = p->corners[j] * v[4 + (j & 3)];
        }
        out->blend = p->blend;
    }
    return count;
}

/* A section that begins with a backdrop no mod gives (this frame's name) is not used. */
static int backdrop_there(const Section *s, const KhdaysSituation *situation)
{
    char name[512];
    const Directive *d = &directives[s->first_directive];
    if (s->directives == 0 || d->kind != D_BACKDROP) {
        return 1;
    }
    backdrop_name(backdrops[d->value].name, situation, name, sizeof(name));
    return animated(name) ? animation_named(name, 0) >= 0 : picture_named(name) != NULL;
}

static void put_float(uint32_t *words, int at, float v)
{
    memcpy(&words[at], &v, sizeof(v));
}

/* Builds a shape for this frame in the C x 192 picture: its source's rectangle scaled (a DS dot
 * becomes `scale` picture dots), placed at its corner and moved by its margins. */
static void build_shape(const ShapeDef *d, int columns, KhdaysShape *out, int key_first, float push_x, float push_y)
{
    const float f = hud_factor();
    const float c = (float)columns;
    const float w = d->w, h = d->h;
    const float cover = d->cover ? ((float)columns / w > 192.0f / h ? (float)columns / w : 192.0f / h) : 0.0f;
    const float cap = d->capped ? d->cap * f + d->cap_fixed : 0.0f;
    float sx = d->cover ? cover : d->sx * f + d->sx_fixed, sy = d->cover ? cover : d->sy * f + d->sy_fixed;
    float margin[4];
    if (d->capped) {
        sx = sx < cap ? sx : cap;
        sy = sy < cap ? sy : cap;
    }
    const float width = w * sx, height = h * sy;
    float x1 = 0.0f, y1 = 0.0f;
    uint32_t *words = out->words;
    switch (d->corner) {
    case CORNER_CENTRE:
        x1 = (c - width) / 2.0f;
        y1 = (192.0f - height) / 2.0f;
        break;
    case CORNER_TOP_LEFT: break;
    case CORNER_TOP: x1 = (c - width) / 2.0f; break;
    case CORNER_TOP_RIGHT: x1 = c - width; break;
    case CORNER_RIGHT:
        x1 = c - width;
        y1 = (192.0f - height) / 2.0f;
        break;
    case CORNER_BOTTOM_RIGHT:
        x1 = c - width;
        y1 = 192.0f - height;
        break;
    case CORNER_BOTTOM:
        x1 = (c - width) / 2.0f;
        y1 = 192.0f - height;
        break;
    case CORNER_BOTTOM_LEFT: y1 = 192.0f - height; break;
    case CORNER_LEFT: y1 = (192.0f - height) / 2.0f; break;
    default: /* keep: the source's middle where it was, scaled */
        x1 = (d->x + w / 2.0f) * sx * c / 256.0f - width / 2.0f;
        y1 = (d->y + h / 2.0f) * sy - height / 2.0f;
        break;
    }
    for (int i = 0; i < 4; ++i) {
        margin[i] = d->margin[i] * f + d->margin_fixed[i];
    }
    x1 += margin[0] - margin[2] + push_x * f;
    y1 += margin[1] - margin[3] + push_y * f;
    memset(words, 0, sizeof(out->words));
    words[KO_FLAGS] = d->flags | (d->lower ? KOF_LOWER : 0u);
    put_float(words, KO_SRC_X, d->x);
    put_float(words, KO_SRC_Y, d->y);
    put_float(words, KO_SRC_W, w);
    put_float(words, KO_SRC_H, h);
    put_float(words, KO_DST_X1, x1);
    put_float(words, KO_DST_Y1, y1);
    put_float(words, KO_DST_X2, x1 + width);
    put_float(words, KO_DST_Y2, y1 + height);
    for (int i = 0; i < 4; ++i) {
        put_float(words, KO_FADE + i, d->fade[i] * f);
    }
    put_float(words, KO_OPACITY, d->opacity);
    put_float(words, KO_ROUND, d->round);
    words[KO_SEE_THROUGH] = d->see_through;
    words[KO_KEYS] = (uint32_t)key_first;
    words[KO_KEY_COUNT] = (uint32_t)d->key_count;
    put_float(words, KO_SCALE_X, sx);
    put_float(words, KO_SCALE_Y, sy);
}

static void build_shape3d(const Shape3dDef *d, int columns, KhdaysShape3d *out, float push_x, float push_y)
{
    /* at the HUD's scale, as the 2D shapes, unless fixed */
    const float u = d->fixed ? 1.0f : 1.0f / hud_factor();
    const float c = (float)columns;
    const float width = d->w * d->scale / u, height = d->h * d->scale / u;
    float x1 = 0.0f, y1 = 0.0f;
    switch (d->corner) {
    case CORNER_CENTRE:
        x1 = (c - width) / 2.0f;
        y1 = (192.0f - height) / 2.0f;
        break;
    case CORNER_TOP: x1 = (c - width) / 2.0f; break;
    case CORNER_TOP_RIGHT: x1 = c - width; break;
    case CORNER_RIGHT:
        x1 = c - width;
        y1 = (192.0f - height) / 2.0f;
        break;
    case CORNER_BOTTOM_RIGHT:
        x1 = c - width;
        y1 = 192.0f - height;
        break;
    case CORNER_BOTTOM:
        x1 = (c - width) / 2.0f;
        y1 = 192.0f - height;
        break;
    case CORNER_BOTTOM_LEFT: y1 = 192.0f - height; break;
    case CORNER_LEFT: y1 = (192.0f - height) / 2.0f; break;
    default: break;
    }
    out->group = d->group;
    out->hide = d->hide;
    out->x = x1 + (d->margin[0] - d->margin[2] + push_x) / u;
    out->y = y1 + (d->margin[1] - d->margin[3] + push_y) / u;
    out->scale = d->scale / u;
    out->src_x = d->x;
    out->src_y = d->y;
    out->src_w = d->w;
    out->src_h = d->h;
    out->whole = 0;
    out->world = 0;
}

/* `halves`: the two screens as one, joined in the middle -- the upper's left half and the lower's right
 * half, each where the DS has it -- the DS's 256 columns enlarged to the picture's width (its top and
 * bottom cut), at most as much as at 16:9 (a wider window keeps it in the middle). The upper's world's 3D
 * is enlarged as its screen (the HUD's left to its shapes); the lower's half comes after the section's
 * shapes, which show over it. */
#define HALVES_MOST (342.0f / 256.0f)

static void add_halves(int columns, int key_count, int *shape_count, int *shape3d_count)
{
    const float c = (float)columns, k = c / 256.0f < HALVES_MOST ? c / 256.0f : HALVES_MOST;
    const float top = (192.0f - 192.0f * k) / 2.0f;
    if (grow((void **)&shapes3d, &shape3d_room, *shape3d_count, sizeof(KhdaysShape3d))) {
        /* the 3D's x is the DS's (0 its left edge), the place's the picture's */
        KhdaysShape3d *s = &shapes3d[(*shape3d_count)++];
        memset(s, 0, sizeof(*s));
        s->group = KHDAYS_SHAPE3D_ALL;
        s->src_w = 256.0f;
        s->src_h = 192.0f;
        s->scale = k;
        s->x = (c - 256.0f * k) / 2.0f;
        s->y = top;
        s->whole = s->world = 1;
    }
    if (grow((void **)&shapes, &shape_room, *shape_count, sizeof(KhdaysShape))) {
        ShapeDef lower;
        memset(&lower, 0, sizeof(lower));
        lower.lower = 1;
        lower.x = 128.0f;
        lower.w = 128.0f;
        lower.h = 192.0f;
        lower.corner = CORNER_CENTRE;
        lower.margin_fixed[0] = 64.0f * k; /* its left edge in the picture's middle */
        lower.sx_fixed = lower.sy_fixed = k;
        lower.flags = KOF_FORCE;
        lower.opacity = 1.0f;
        build_shape(&lower, columns, &shapes[(*shape_count)++], key_count, 0.0f, 0.0f);
    }
}

void khdays_composition_for(const KhdaysSituation *situation, int columns, KhdaysComposition *out)
{
    int shape_count = 0, key_count = 0, shape3d_count = 0, stopped = 0, matched = 0, shown = -1, menu = 0, halves = 0;
    out->screens = -1;
    out->brightness = -1;
    out->backdrop = NULL;
    out->backdrop_w = out->backdrop_h = 0;
    out->backdrop_ax = out->backdrop_ay = 0.5f;
    out->backdrop_fit = 0;
    out->hide3d_all = 0;
    out->lower_whole = 0;
    for (int s = 0; s < 2; ++s) {
        out->shop_screens[s] = out->shop_cut_layers[s] = 0u;
        out->shop_fade[s] = out->shop_with3d[s] = out->shop_slide[s] = 0;
    }
    out->shop_scale = 0;
    out->animated = 0;
    out->picture_w = out->picture_h = 0;
    out->ground = 0;
    out->pieces = NULL;
    out->piece_count = 0;
    out->buttons = NULL;
    out->button_count = 0;
    if (!loaded) {
        khdays_composition_load();
    }
    current_columns = columns;
    for (int s = 0; s < section_count && !stopped; ++s) {
        if (!holds(&sections[s], situation) || !backdrop_there(&sections[s], situation)) {
            continue;
        }
        matched = 1;
        for (int i = 0; i < sections[s].directives && !stopped; ++i) {
            const Directive *d = &directives[sections[s].first_directive + i];
            switch (d->kind) {
            case D_SCREENS: out->screens = out->screens < 0 ? d->value : out->screens; break;
            case D_BRIGHTNESS: out->brightness = out->brightness < 0 ? d->value : out->brightness; break;
            case D_SHAPE: {
                ShapeDef *def = &shape_defs[d->value];
                const float pushed = push_now(&def->push, situation);
                if (!grow((void **)&shapes, &shape_room, shape_count, sizeof(KhdaysShape))) {
                    break;
                }
                for (int k = 0; k < def->key_count; ++k) {
                    if (grow((void **)&keys, &key_room, key_count + k, sizeof(uint32_t))) {
                        keys[key_count + k] = def_keys[def->key_first + k];
                    }
                }
                build_shape(def, columns, &shapes[shape_count++], key_count, def->push.dx * pushed,
                            def->push.dy * pushed);
                key_count += def->key_count;
                break;
            }
            case D_SHAPE3D:
                if (grow((void **)&shapes3d, &shape3d_room, shape3d_count, sizeof(KhdaysShape3d))) {
                    Shape3dDef *def3 = &shape3d_defs[d->value];
                    const float pushed = push_now(&def3->push, situation);
                    build_shape3d(def3, columns, &shapes3d[shape3d_count++], def3->push.dx * pushed,
                                  def3->push.dy * pushed);
                }
                break;
            case D_BACKDROP:
                if (out->backdrop == NULL) { /* the first that says it, as `screens` */
                    char name[512];
                    const Picture *p = NULL;
                    backdrop_name(backdrops[d->value].name, situation, name, sizeof(name));
                    if (animated(name)) {
                        const int a = animation_named(name, 0);
                        if (a >= 0) {
                            p = &pictures[animations[a].atlas];
                            out->animated = 1;
                            out->picture_w = animations[a].picture_w;
                            out->picture_h = animations[a].picture_h;
                            out->ground = animations[a].ground;
                            out->piece_count = animation_now(a, situation->frame);
                            out->pieces = pieces_now;
                            out->button_count = 0;
                            for (int b = 0; b < animations[a].buttons; ++b) {
                                const AnimButton *from = &anim_buttons[animations[a].first_button + b];
                                if (grow((void **)&buttons_now, &buttons_now_room, out->button_count,
                                         sizeof(KhdaysButton))) {
                                    buttons_now[out->button_count].action = from->action;
                                    memcpy(buttons_now[out->button_count++].rect, from->rect, sizeof(from->rect));
                                }
                            }
                            out->buttons = buttons_now;
                            shown = a;
                        }
                    } else if ((p = picture_named(name)) != NULL) {
                        out->picture_w = p->w;
                        out->picture_h = p->h;
                    }
                    if (p != NULL) {
                        out->backdrop = p->rgba;
                        out->backdrop_w = p->w;
                        out->backdrop_h = p->h;
                        out->backdrop_ax = backdrops[d->value].ax;
                        out->backdrop_ay = backdrops[d->value].ay;
                        out->backdrop_fit = backdrops[d->value].fit;
                    }
                }
                break;
            case D_PRELOAD: { /* read ahead, on threads of their own (a no-op once asked for) */
                char name[512];
                backdrop_name(backdrops[d->value].name, situation, name, sizeof(name));
                if (animated(name)) {
                    animation_named(name, 1);
                } else {
                    picture_entry(name, 1);
                }
                break;
            }
            case D_HIDE3D_ALL: out->hide3d_all = 1; break;
            case D_MENU: menu = 1; break;
            case D_HALVES: halves = 1; break;
            default: stopped = 1; break;
            }
        }
    }
    if ((menu || !matched) && out->screens < 0) {
        /* the port's common rule (`menu`, and a situation no section names): the screens that
         * show something, whole, as large as the picture allows -- both side by side (larger than
         * stacked in any picture 4:3 or wider), or the one alone at the DS's size */
        out->screens = situation->content == 2u   ? KHDAYS_SCREENS_LOWER
                       : situation->content == 3u ? KHDAYS_SCREENS_SIDE_BY_SIDE
                                                  : KHDAYS_SCREENS_UPPER;
        if (out->screens != KHDAYS_SCREENS_SIDE_BY_SIDE &&
            grow((void **)&shapes, &shape_room, shape_count, sizeof(KhdaysShape))) {
            ShapeDef whole;
            memset(&whole, 0, sizeof(whole));
            whole.lower = out->screens == KHDAYS_SCREENS_LOWER;
            whole.whole = 1;
            whole.w = 256.0f;
            whole.h = 192.0f;
            whole.corner = CORNER_CENTRE;
            whole.sx_fixed = whole.sy_fixed = 1.0f; /* fixed: the DS's size, whatever the HUD's */
            whole.opacity = 1.0f;
            build_shape(&whole, columns, &shapes[shape_count++], key_count, 0.0f, 0.0f);
        }
    }
    if (halves) {
        out->screens = KHDAYS_SCREENS_UPPER;
        add_halves(columns, key_count, &shape_count, &shape3d_count);
    }
    out->screens = out->screens < 0 ? KHDAYS_SCREENS_UPPER : out->screens;
    out->brightness = out->brightness < 0 ? (int)KOB_DEFAULT : out->brightness;
    out->shapes = shapes;
    out->shape_count = shape_count;
    out->keys = keys;
    out->key_count = key_count;
    out->shapes3d = shapes3d;
    out->shape3d_count = shape3d_count;
    last_animation = shown;
}

void khdays_composition_set_hud_size(int size)
{
    hud_size = size >= 1 && size <= 8 ? size : 4;
}

float khdays_composition_hud_factor(void)
{
    return hud_factor();
}
