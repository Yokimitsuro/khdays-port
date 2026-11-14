/* Animated overhead silhouettes retained in the ordinary 3D lists. */
#ifndef KHDAYS_CHARACTER_SHADOW_H
#define KHDAYS_CHARACTER_SHADOW_H
#include "g3d.h"

/* Each caster's mask: KHDAYS_SHADOW_SIZE squared, one per shadow a list has (a polygon's `shadow` names it) */
#define KHDAYS_SHADOW_SIZE 128
#define KHDAYS_G3D_SHADOW_SOURCE 0x80
#define KHDAYS_G3D_SHADOW_RECEIVER 0x10000
#define KHDAYS_G3D_SHADOW_REPLACED 0x20000
#define KHDAYS_G3D_SHADOW_NATIVE 0x40000
#define KHDAYS_G3D_SHADOW_SPRITE 0x100

void khdays_shadow_reset(void);
void khdays_shadow_camera(const int32_t projection[16], const int32_t camera[12]);
void khdays_shadow_capture_begin(uintptr_t owner, int enabled);
void khdays_shadow_capture_end(void);
void khdays_shadow_volume_begin(uintptr_t owner, int enabled);
void khdays_shadow_volume_end(void);
void khdays_shadow_sprite_begin(void);
int khdays_shadow_sprite_active(void);
int khdays_shadow_has_sprite(void);
void khdays_shadow_record(const KhdaysG3dList *list);
int khdays_shadow_finish(KhdaysG3dList *list);
/* What a caster's replacement shadow came to in the last list that had one for it, for tracing why the game's own
 * shadow stayed: `outcome` one of KHDAYS_SHADOW_*, the band its receivers were sought in (world y, after any move to
 * the floor below), its own polygons' y range, and the polygons under its ground plan by why they did not receive */
enum {
    KHDAYS_SHADOW_REPLACED_IT,  /* replaced */
    KHDAYS_SHADOW_NO_SOURCES,   /* none of its own polygons captured */
    KHDAYS_SHADOW_NO_SHADE,     /* no flat shadow of its captured (or one with no attributes) */
    KHDAYS_SHADOW_NO_BAND,      /* the flat shadow had no height */
    KHDAYS_SHADOW_NO_RECEIVERS, /* nothing in the band (nor on the floor below) received it */
    KHDAYS_SHADOW_FAILED        /* the list had no room for the projected polygons */
};
typedef struct KhdaysShadowOutcome {
    uintptr_t owner;
    unsigned list; /* lists finished so far when it was made */
    int outcome, sources, receivers, moved_below;
    float band_lo, band_hi, own_lo, own_hi;
    int under, translucent, shadow_mode, steep, casters; /* under its ground plan: all, and those left out by each */
    float under_lo, under_hi;                            /* their y range */
    int translucent_alpha;                               /* the last left-out one's alpha */
} KhdaysShadowOutcome;
/* The last outcome for `owner`, NULL if none was made for it */
const KhdaysShadowOutcome *khdays_shadow_outcome(uintptr_t owner);
/* Lists finished so far (an outcome older than the last few was not made: the caster had no shadow entry) */
unsigned khdays_shadow_lists_finished(void);
/* Appends floating-point pre-geometry with its own identity and viewport. */
int khdays_g3d_shadow_polygon(const KhdaysG3dClipVertex *v, int n, const KhdaysG3dPolygon *polygon,
                              const KhdaysG3dPreInfo *identity);
#endif
