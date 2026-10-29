/* The heart counter's native events feed editable SEQD clips, inside the original 3D HUD layer. */
#ifndef KHDAYS_HEART_POINTS_H
#define KHDAYS_HEART_POINTS_H
#define KHDAYS_HEART_PROGRAMS 4
#ifdef __cplusplus
extern "C" {
#endif
#include "../gfx/g3d.h"
#include "../gfx/sequence_image.h"
void khdays_heart_begin(const void *context);
int khdays_heart_node(const void *node);
void khdays_heart_event(int kind, int row);
void khdays_heart_end(void);
void khdays_heart_commands(const KhdaysG3dList *list, KhdaysSequenceImage *out);
#ifdef __cplusplus
}
#endif
#endif
