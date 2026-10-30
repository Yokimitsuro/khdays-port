/* The two-screen picture's places (two_screens.h). */
#include "two_screens.h"

#define W 256
#define H 192

KhdaysTwoScreensLayout khdays_two_screens_layout(int side_by_side, int columns, float upper_w, float lower_w)
{
    KhdaysTwoScreensLayout l;
    if (side_by_side) {
        l.width = 2 * W;
        l.height = H;
        l.upper = (KhdaysScreenPlace){0.0f, 0.0f, (float)W, (float)H};
        l.lower = (KhdaysScreenPlace){(float)W, 0.0f, (float)W, (float)H};
    } else {
        l.width = columns;
        l.height = 2 * H;
        l.upper = (KhdaysScreenPlace){((float)columns - upper_w) / 2.0f, 0.0f, upper_w, (float)H};
        l.lower = (KhdaysScreenPlace){((float)columns - lower_w) / 2.0f, (float)H, lower_w, (float)H};
    }
    return l;
}
