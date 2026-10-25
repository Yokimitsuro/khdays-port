/* The day's calendar card (scene 5, ov004: the DS's rolling day counter), read for the port to draw
 * as the HD collection's 358/2 Days theater draws a chapter's day (host/screens/day_screen.c, a mod's
 * layouts/day/<lang>/day.txt). The game's own card runs underneath: its fades, its rolling, its end. */
#ifndef KHDAYS_DAYCARD_H
#define KHDAYS_DAYCARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysDayCardView {
    int shown;
    int language;   /* the game's (1-5) */
    int from, to;   /* the day the counter starts on and the one it rolls to */
    float value;    /* the day the counter shows now, rolling (from .. to) */
    int phase;      /* the card's (Ov004_StepSceneFrame's table): 0 fading in, 1 rolling, 2 and up after */
    int rolled;     /* the counter has reached `to` */
    int title_day;  /* the day whose title the card shows (Ov004_PrepareTransitionLabel's: the day it
                    * rolls to, 400 the prologue's -- shown as day 255) */
    int16_t master; /* the game's fade: the upper screen's brightness, -16 black to 16 white */
} KhdaysDayCardView;

/* The theater's pace for its card (measured frame by frame on the HD collection's movie, 60 frames
 * a second from the card's first frame). A day changing, the count's controls start COUNT_AT frames in:
 * one day is the slide (its keys moving from frame SLIDE_FIRST to SLIDE_LAST of its control); more days
 * count quickly to the day before (from frame SLIDE_FIRST, FAST_PER_DAY frames a day, FAST_MOST at most,
 * slowing to rest), then the last one slides, its control from FINAL_EARLY frames before the quick count
 * ends. The heart falls (its control's frame 50) as the day arrives; with no day changing, at once. */
#define KHDAYS_DAYCARD_COUNT_AT 49
#define KHDAYS_DAYCARD_SLIDE_FIRST 8
#define KHDAYS_DAYCARD_SLIDE_LAST 50
#define KHDAYS_DAYCARD_FAST_PER_DAY 12
#define KHDAYS_DAYCARD_FAST_MOST 232
#define KHDAYS_DAYCARD_FINAL_EARLY 2
/* How long the quick count of `days` days (all but the last) lasts; 0 for one day. */
int khdays_daycard_fast_frames(int days);
/* Frames from the card's first to the day's arrival (the last slide's end); 0 when no day changes. */
int khdays_daycard_arrival(int from, int to);

/* The game's thread: an object's update ran (diag.c, its native address). The game's own counter is
 * given the theater's pace while the port's card is drawn (its rolling then ends as the day arrives,
 * the card held on after it as long as the game holds it: the heart's fall and the title in it). */
void khdays_daycard_update_ran(uintptr_t update);
/* The game's thread, once a frame (input.c): what the card is, published. */
void khdays_daycard_frame(void);
/* Any thread: what the card is, as last published. */
void khdays_daycard_view(KhdaysDayCardView *out);
/* The port's thread drawing: the port's card was drawn now. */
void khdays_daycard_hd_drawn(void);

#ifdef __cplusplus
}
#endif

#endif
