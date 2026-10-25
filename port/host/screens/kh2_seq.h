/* Kingdom Hearts II's 2D sequences (SEQD, a 2LD's LAYD's) played as the PS2 game plays them, over the
 * window: the Menu Overhaul's pause (modern_pause.c, pause.2ld's) and story menu (overhaul_camp_screen.c,
 * camp.2ld's). Sprites (a texture rectangle and four corner colours), sprite groups (sprites placed around
 * a point), animations (a group's motion between two frames) and animation groups; the fields' meaning
 * and the drawing after OpenKh's (OpenKh.Kh2/Sequence.cs, Engine/SequenceRenderer.cs; Apache License 2.0:
 * THIRD_PARTY_NOTICES.md). */
#ifndef KHDAYS_HOST_KH2_SEQ_H
#define KHDAYS_HOST_KH2_SEQ_H

#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct KhdaysKh2Sprite {
    int u0, v0, u1, v1;
    uint32_t color[4]; /* the corners: top-left, top-right, bottom-left, bottom-right */
} KhdaysKh2Sprite;

typedef struct KhdaysKh2Part {
    int left, top, right, bottom, sprite;
} KhdaysKh2Part;

typedef struct KhdaysKh2Group {
    int first, count;
} KhdaysKh2Group;

typedef struct KhdaysKh2Anim {
    int flags, group, frame_start, frame_end;
    int tx[2], ty[2], px[2], py[2];
    float rx[2], ry[2], rz[2], s[2], sx[2], sy[2], bx[2], by[2];
    int bx_count, by_count, blend;
    uint32_t color[2];
} KhdaysKh2Anim;

typedef struct KhdaysKh2AnimGroup {
    int first, count, no_loop, loop_start, loop_end;
} KhdaysKh2AnimGroup;

typedef struct KhdaysKh2Seq {
    KhdaysKh2Sprite *sprites;
    KhdaysKh2Part *parts;
    KhdaysKh2Group *groups;
    KhdaysKh2Anim *anims;
    KhdaysKh2AnimGroup *agroups;
    int sprite_count, part_count, group_count, anim_count, agroup_count;
    float sheet_units; /* the sheet's size in the PS2 game (its IMGZ picture's): the sprites' rectangles' unit */
} KhdaysKh2Seq;

/* A mod's sequence (`relative`: a path in the mods, a SEQD alone) read into `seq`, its sprites' and
 * animations' colours through `recolour` (NULL: as they are); 0 when it is missing or not one (`seq` then
 * empty). */
int khdays_kh2_seq_load(KhdaysKh2Seq *seq, const char *relative, float sheet_units, uint32_t (*recolour)(uint32_t));
void khdays_kh2_seq_free(KhdaysKh2Seq *seq);

/* A mod's picture (a path in the mods) as a texture, NULL if none; `bright` doubles its colours: a PS2 tint
 * goes to twice the texel (0x80 is 1.0), which a vertex colour cannot, so a sprite tinted past 1.0 is drawn
 * from the doubled sheet with half its tint. */
SDL_Texture *khdays_kh2_picture(SDL_Renderer *renderer, const char *relative, int bright);

/* Where a sequence is drawn: its sheet and the doubled one, the window's pixels a dot of KH2's screen
 * (`scale`) and where its (0, 0) is (`left`, `top`); a group drawn taller about its place (`stretch`, 1
 * none: the pause's band behind three rows); the parts' edges right of `widen_from` (dots) moved `widen`
 * further (a button made longer: its middle, a column of texels stretched, the longer); everything's
 * alpha times `alpha`. */
typedef struct KhdaysKh2Stage {
    SDL_Renderer *renderer;
    SDL_Texture *sheet, *bright;
    float scale, left, top;
    float stretch;
    float widen, widen_from;
    float alpha;
} KhdaysKh2Stage;

/* The stage's defaults: no stretch, no widening, its own alpha. */
void khdays_kh2_stage(KhdaysKh2Stage *st, SDL_Renderer *renderer, SDL_Texture *sheet, SDL_Texture *bright, float scale,
                      float left, float top);

/* Animation group `group` at `frame` (looped as the group says), at (x, y) of KH2's screen (its dots). */
void khdays_kh2_seq_draw(const KhdaysKh2Stage *st, const KhdaysKh2Seq *seq, int group, int frame, float x, float y);

#endif
