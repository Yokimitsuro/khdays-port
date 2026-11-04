/* Animated overhead silhouettes retained in the ordinary 3D lists. */
#ifndef KHDAYS_CHARACTER_SHADOW_H
#define KHDAYS_CHARACTER_SHADOW_H
#include "g3d.h"

#define KHDAYS_SHADOW_SIZE 128
#define KHDAYS_SHADOW_MAX 32
#define KHDAYS_G3D_SHADOW_SOURCE 0x80
#define KHDAYS_G3D_SHADOW_RECEIVER 0x10000
#define KHDAYS_G3D_SHADOW_REPLACED 0x20000
#define KHDAYS_G3D_SHADOW_NATIVE 0x40000
#define KHDAYS_G3D_SHADOW_SPRITE 0x100
#define KHDAYS_SHADOW_INDEX(flags) (((unsigned)(flags) >> 24) & 31u)

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
/* Appends floating-point pre-geometry with its own identity and viewport. */
int khdays_g3d_shadow_polygon(const KhdaysG3dClipVertex *v, int n, const KhdaysG3dPolygon *polygon,
                              const KhdaysG3dPreInfo *identity);
#endif
