/* KINGDOM HEARTS II's object pictures a KH2 enemy's script shows (runtime/kh2_enemies: 1:285 YS::STDOBJ::show_picture,
 * 1:290 another layout, 1:315 moved; Xigbar's scope's sights): each a layout of its archive's LAYD -- its screens'
 * SEQD sequences on its IMGZ's pictures (their HD pages) -- played as KH2's YI::LAYOUT plays it, drawn over the 3D at
 * KH2's 512 x 416 screen's places (the 4:3 picture's; a wider one shows the pieces beyond, as KH2 PC does). */
#ifndef KHDAYS_UI_KH2_PICTURES_H
#define KHDAYS_UI_KH2_PICTURES_H

#include "../gfx/sequence_image.h"

#define KHDAYS_PICTURE_PROGRAMS 1 /* the frame package's 3D programs: every picture's quads in one */

#ifdef __cplusplus
extern "C" {
#endif

#include "../gfx/g3d.h" /* a C header */

/* While the game builds its 3D list for the field (each frame, kh2_reticles.cpp's end of the selection marker's
 * drawing): the pictures played on by the VBlanks since and drawn as one polygon in front of the 3D. */
void khdays_ui_kh2_pictures_draw(void);

/* The frame package's program: the pictures' canvas whose polygon the 3D list holds (or none). */
void khdays_ui_kh2_pictures_commands(const KhdaysG3dList *list, KhdaysSequenceImage *out);

#ifdef __cplusplus
}
#endif

#endif
