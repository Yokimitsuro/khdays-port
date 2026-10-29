/* An animated HUD anchor in DS screen coordinates; identity when no replacement owns it. */
#ifndef KHDAYS_HUD_PARENT_H
#define KHDAYS_HUD_PARENT_H
typedef struct KhdaysHudParent {
    float x, y, scale_x, scale_y, alpha;
} KhdaysHudParent;
#endif
