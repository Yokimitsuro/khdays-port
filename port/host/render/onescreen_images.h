/* Place independent images beside their source shapes, preserving the composition's order. */
#ifndef KHDAYS_ONESCREEN_IMAGES_H
#define KHDAYS_ONESCREEN_IMAGES_H

#include "../../ui/composition.h"

typedef struct KhdaysFramePackage KhdaysFramePackage;
/* Nonzero disables native banner crops/timing; native HUD visibility and placement stay the game's. */
int khdays_onescreen_information(const KhdaysFramePackage *package, KhdaysSituation *situation);

/* The resulting shapes stay valid until the next call; input is a fresh composition. */
void khdays_onescreen_images(const KhdaysFramePackage *package, const KhdaysSituation *situation,
                             KhdaysComposition *composition);

#endif
