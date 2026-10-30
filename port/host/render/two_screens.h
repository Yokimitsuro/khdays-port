/* Where the DS's two screens go in the two-screen picture (gpu_present.c): one above the other, as the DS has
 * them, or side by side ([video] two_screens, profile.h). In DS pixels: the picture's size and each screen's
 * rectangle in it. */
#ifndef KHDAYS_TWO_SCREENS_H
#define KHDAYS_TWO_SCREENS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysScreenPlace {
    float x, y, w, h;
} KhdaysScreenPlace;

typedef struct KhdaysTwoScreensLayout {
    int width, height;       /* the picture */
    KhdaysScreenPlace upper; /* the upper screen's place */
    KhdaysScreenPlace lower; /* the lower screen's */
} KhdaysTwoScreensLayout;

/* The picture for two screens: stacked, `columns` wide (the DS's 256, or wider for the aspect) and two
 * screens tall, each screen as wide as it was drawn (`upper_w`, `lower_w`) in the middle; side by side,
 * two DS screens wide and one tall, the upper on the left, each the DS's 256 columns (a screen drawn wider
 * shows its middle 256). */
KhdaysTwoScreensLayout khdays_two_screens_layout(int side_by_side, int columns, float upper_w, float lower_w);

#ifdef __cplusplus
}
#endif

#endif
