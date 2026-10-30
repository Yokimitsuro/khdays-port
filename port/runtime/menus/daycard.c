/* The day's calendar card (daycard.h), read while its object runs (Ov004_StepSceneFrame). Its
 * context (data_ov004_02051384, the object's heap, Ov004_CreateSceneObjects): +0xaf8 the phase,
 * +0x5558 the day it starts on and +0x555c the one it rolls to (0x190 both made 0xff: the prologue's
 * day 255), +0x5568 the rolling value (20.12 fixed point), +0x5570 the rolling's step (16.16 of the
 * whole a frame, Ov004_FadeInTransition's), +0x5574 the rolling done, +0x5578 the rolling's progress,
 * +0x5598 the day whose title it draws (Ov004_PrepareTransitionLabel, set before 0x190 became 0xff:
 * 400 the prologue's). The port's card has its titles by that day (tools/hd/theater_day.py). More:
 * docs/RUNTIME_MAP.md, scene 5. */
#include "daycard.h"

#include "../core/io.h"
#include "../core/window.h"

#include "khdays_ds_symbols.h"

#include <string.h>
#include <windows.h>

extern int GetLanguage(void);
extern int Ov004_StepSceneFrame(void);
int khdays_overlay_loaded(int id); /* overlays.c */
int khdays_language_wanted(void);  /* language.c */
u32 khdays_events_vblank_count(void);

#define OV_CALENDAR 4
#define CALENDAR_SCENE 5
#define SCENE_CURRENT 0x8u
#define PHASE 0xaf8u
#define FROM 0x5558u
#define TO 0x555cu
#define VALUE 0x5568u
#define STEP 0x5570u
#define ROLLED 0x5574u
#define PROGRESS 0x5578u
#define TITLE_DAY 0x5598u
#define SEEN_FRAMES 2u /* the card is gone when its update did not run for longer */
#define ROLLING 1      /* the phase rolling the counter */
#define FADE_IN 16     /* the game's fade in before it (Ov004_FadeInTransition: a level an update) */
#define HD_RECENT 500u /* ms: the port's card is up when drawn this lately */

static SRWLOCK lock = SRWLOCK_INIT;
static KhdaysDayCardView shown;
static u32 context, seen_at;
static volatile LONG64 hd_drawn_at;
static int paced;     /* this card's rolling was looked at (again from its fade in) */
static u32 fading_at; /* the VBlank its fade in was first seen at (0 not yet) */

static u32 rd32(u32 address)
{
    return *(const volatile u32 *)KHDAYS_W(address);
}

int khdays_daycard_fast_frames(int days)
{
    const int quick = days - 1;
    if (quick <= 0) {
        return 0;
    }
    return quick * KHDAYS_DAYCARD_FAST_PER_DAY < KHDAYS_DAYCARD_FAST_MOST ? quick * KHDAYS_DAYCARD_FAST_PER_DAY
                                                                          : KHDAYS_DAYCARD_FAST_MOST;
}

int khdays_daycard_arrival(int from, int to)
{
    const int days = from > to ? from - to : to - from;
    if (days == 0) {
        return 0;
    }
    if (days == 1) {
        return KHDAYS_DAYCARD_COUNT_AT + KHDAYS_DAYCARD_SLIDE_LAST;
    }
    return KHDAYS_DAYCARD_COUNT_AT + KHDAYS_DAYCARD_SLIDE_FIRST + khdays_daycard_fast_frames(days) -
           KHDAYS_DAYCARD_FINAL_EARLY + KHDAYS_DAYCARD_SLIDE_LAST;
}

void khdays_daycard_hd_drawn(void)
{
    InterlockedExchange64(&hd_drawn_at, (LONG64)GetTickCount64());
}

/* The rolling just begun (Ov004_FadeInTransition set its step this update; Ov004_UpdateRollingTransition
 * not yet run) while the port's card is up: its step made to end it as the theater's day arrives (its
 * own: 30 updates a day up to 2, 9 up to 10, 4 up to 30, then 1 -- 248 for the prologue's 255 to day 7,
 * the card standing seconds after the theater's was done). The card updates every other VBlank (30 a
 * second): the updates left are counted from how many VBlanks its 16 of fade in took. */
static void pace(void)
{
    const int from = (int)rd32(context + FROM), to = (int)rd32(context + TO);
    const u32 phase = rd32(context + PHASE), step = rd32(context + STEP);
    const u32 now = khdays_events_vblank_count();
    float per_update;
    int left;
    if (phase < ROLLING) {
        if (!paced && fading_at == 0) {
            fading_at = now != 0 ? now : 1;
        }
        paced = 0;
        return;
    }
    if (paced || phase != ROLLING || rd32(context + PROGRESS) != 0 || rd32(context + ROLLED) != 0 || step == 0 ||
        from < 0 || from >= 1000 || to < 0 || to >= 1000 ||
        GetTickCount64() - (ULONGLONG)InterlockedCompareExchange64(&hd_drawn_at, 0, 0) > HD_RECENT) {
        return;
    }
    paced = 1;
    per_update = fading_at != 0 ? (float)(now - fading_at) / (float)FADE_IN : 2.0f;
    fading_at = 0;
    if (per_update < 1.0f || per_update > 4.0f) {
        per_update = 2.0f;
    }
    left = (int)((float)(khdays_daycard_arrival(from, to) - (int)(FADE_IN * per_update)) / per_update + 0.5f);
    if (left < 1) {
        left = 1;
    }
    *(volatile u32 *)KHDAYS_W(context + STEP) = 0x10000u / (u32)left;
}

void khdays_daycard_update_ran(uintptr_t update)
{
    if (update == (uintptr_t)Ov004_StepSceneFrame) {
        context = rd32(0x02051384u); /* data_ov004_02051384 */
        seen_at = khdays_events_vblank_count();
        if (context != 0 && khdays_game_ram(context) && khdays_game_ram(context + 0x5600u)) {
            pace();
        }
    }
}

static int16_t upper_brightness(void)
{
    const u16 bright = IO16(0x006c); /* MASTER_BRIGHT, engine A: the card's screen */
    const int mode = bright >> 14, factor = (bright & 0x1f) > 16 ? 16 : bright & 0x1f;
    return (int16_t)(mode == 1 ? factor : mode == 2 ? -factor : 0);
}

void khdays_daycard_frame(void)
{
    KhdaysDayCardView v;
    memset(&v, 0, sizeof(v));
    v.title_day = -1;
    if (context != 0 && khdays_events_vblank_count() - seen_at <= SEEN_FRAMES && khdays_game_ram(context) &&
        khdays_game_ram(context + 0x5600u) && khdays_overlay_loaded(OV_CALENDAR) &&
        rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT) == CALENDAR_SCENE) {
        const int from = (int)rd32(context + FROM), to = (int)rd32(context + TO);
        if (from >= 0 && from < 1000 && to >= 0 && to < 1000) {
            v.shown = 1;
            v.from = from;
            v.to = to;
            v.value = (float)(int)rd32(context + VALUE) / 4096.0f;
            v.phase = (int)rd32(context + PHASE);
            v.rolled = rd32(context + ROLLED) != 0;
            v.title_day = (int)rd32(context + TITLE_DAY);
            v.master = upper_brightness();
            v.language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
        }
    }
    AcquireSRWLockExclusive(&lock);
    shown = v;
    ReleaseSRWLockExclusive(&lock);
}

void khdays_daycard_view(KhdaysDayCardView *out)
{
    AcquireSRWLockShared(&lock);
    *out = shown;
    ReleaseSRWLockShared(&lock);
}
