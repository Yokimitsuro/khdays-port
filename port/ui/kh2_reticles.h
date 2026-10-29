/* Native selection markers: observe Days' draws, replace only the chosen sprites. */
#ifndef KHDAYS_KH2_RETICLES_H
#define KHDAYS_KH2_RETICLES_H
#ifdef __cplusplus
extern "C" {
#endif
#include "../gfx/g3d.h"
#include "../gfx/sequence_image.h"
#define KHDAYS_RETICLE_COUNT 5
void khdays_reticles_frame(unsigned frame);
void khdays_reticles_begin(void);
/* Copy the native target's fx32 world position; never retain a game object pointer. */
void khdays_reticles_target(const int *world);
/* The selection hook reprojects saved anchors through Ov022_ProjectToScreen before drawing. */
int khdays_reticles_saved_world(int kind, int *world);
void khdays_reticles_projected(int kind, int visible, int x, int y);
/* kind: soft, hard, interact, chain, error. Sprite and identity are DS references; position is fx32. */
int khdays_reticles_mark(int kind, unsigned identity, unsigned sprite, int x, int y);
void khdays_reticles_end(int cancel);
void khdays_reticles_commands(const KhdaysG3dList *list, KhdaysSequenceImage out[KHDAYS_RETICLE_COUNT]);
#ifdef __cplusplus
}
#endif
#endif
