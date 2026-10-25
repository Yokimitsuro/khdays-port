/* The one-screen picture, on the presenting side: for each frame package it decides the situation
 * (ui/situation.h), logs it when it changes, and turns its composition (ui/composition.h) into what the
 * one-screen pass draws (gfx_render.h KhdaysOneScreenDraw). */
#ifndef KHDAYS_HOST_ONESCREEN_H
#define KHDAYS_HOST_ONESCREEN_H

#include "gfx_render.h"
#include "../../ui/composition.h"

typedef struct KhdaysFramePackage KhdaysFramePackage; /* runtime/display/frame_package.h */

/* Works out the situation and the composition of `package` for a picture
 * `columns` DS columns wide. `draw` points into `composition`, valid until the
 * next call. A package without the inputs for the situation keeps the last
 * situation. */
void khdays_onescreen_prepare(const KhdaysFramePackage *package, int columns, KhdaysComposition *composition,
                              KhdaysOneScreenDraw *draw);

/* Whether `package`, decided last, is to be decided again as the game's VBlanks pass: its picture
 * stands still and the situation's history is still settling (ui/situation.h). */
int khdays_onescreen_settling(const KhdaysFramePackage *package);

/* Whether `package` (already prepared) makes a new picture. With the 3D on both screens the game moves
 * engine A to the other screen at every VBlank, each screen's 3D in turn; the picture is the upper
 * screen's, so it is made on the VBlanks engine A shows the upper screen and the others keep it. */
int khdays_onescreen_makes_picture(const KhdaysFramePackage *package);

/* The situation (ui/situation.h KHDAYS_SIT_*) the pictures were made for last; any thread. */
int khdays_onescreen_scene(void);
/* Its states (1 << KHDAYS_ST_*); any thread. */
unsigned khdays_onescreen_states(void);

/* The Moogle Shop's DS screens composed apart (its thread) since the last call: how many times, their
 * milliseconds in all and the longest, and the game's pictures alike to the last (not composed). */
void khdays_onescreen_shop_work(unsigned *jobs, double *ms, double *most, unsigned *alike);

#endif
