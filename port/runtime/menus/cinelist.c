/* MODO CINE's list (cinelist.h), read while its object runs (Ov000_EnterListScene,
 * Ov000_FadeTransitionTick, Ov000_TickListSceneInput). Its context is the object's heap: +0 the
 * page's first row, +2 the scene selected, +0x966a the mode, +0x9670 the 28 rows (+8 each one's
 * movie). Both screens fade together; the lower one's is read. More: docs/RUNTIME_MAP.md section 15.5. */
#include "cinelist.h"

#include "../core/io.h"
#include "../input/input.h"
#include "../core/game_memory.h"

#include "khdays_ds_symbols.h"

#include <string.h>
#include <windows.h>
#undef PlaySound /* windows.h's (winmm's), not the game's */

extern int GetLanguage(void);
extern int PlaySound(int bank, int sound);
extern KhdaysRef NNSi_FndGetCurrentRootHeap(void);
extern void Ov000_EnterListScene(void);
extern void Ov000_FadeTransitionTick(void);
extern void Ov000_TickListSceneInput(void);
int khdays_overlay_loaded(int id); /* overlays.c */
int khdays_language_wanted(void);  /* language.c */
u32 khdays_events_vblank_count(void);

#define OV_TITLE 0
#define TITLE_SCENE 1
#define SCENE_CURRENT 0x8u
#define FIRST_ROW 0x0u
#define CURSOR 0x2u
#define ROWS 0x9670u
#define ROW_SIZE 0x18u
#define ROW_MOVIE 0x8u
#define SEEN_FRAMES 2u /* the list is gone when none of its updates ran for longer */

static SRWLOCK lock = SRWLOCK_INIT;
static KhdaysCineView shown;
static u32 context, seen_at;

static int in_ram(u32 address)
{
    return khdays_game_ram(address); /* main RAM or the game's heaps */
}

void khdays_cinelist_update_ran(uintptr_t update)
{
    if (update == (uintptr_t)Ov000_EnterListScene || update == (uintptr_t)Ov000_FadeTransitionTick ||
        update == (uintptr_t)Ov000_TickListSceneInput) {
        context = (u32)NNSi_FndGetCurrentRootHeap();
        seen_at = khdays_events_vblank_count();
    }
}

/* Whether the context holds the theater's rows (each one's movie one of the 800s, thr.p2's numbers):
 * not yet while it is being made. */
static int rows_there(u32 ctx, KhdaysCineView *v)
{
    for (int i = 0; i < KHDAYS_CINE_SCENES; ++i) {
        const u32 movie = rd32(ctx + ROWS + (u32)i * ROW_SIZE + ROW_MOVIE);
        if (movie < 800u || movie >= 900u) {
            return 0;
        }
        v->movies[i] = (uint16_t)movie;
    }
    return 1;
}

void khdays_cinelist_frame(void)
{
    KhdaysCineView v;
    memset(&v, 0, sizeof(v));
    if (context != 0 && khdays_events_vblank_count() - seen_at <= SEEN_FRAMES && in_ram(context) &&
        khdays_overlay_loaded(OV_TITLE) && rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT) == TITLE_SCENE &&
        rows_there(context, &v)) {
        const u16 bright = IO16(0x106c); /* MASTER_BRIGHT, engine B: the lower screen's */
        const int mode = bright >> 14, factor = (bright & 0x1f) > 16 ? 16 : bright & 0x1f;
        const u32 pair = rd32(context + FIRST_ROW);
        const int first = (int)(int16_t)(pair & 0xffffu), cursor = (int)(int16_t)(pair >> 16);
        if (first >= 0 && first < KHDAYS_CINE_SCENES && cursor >= 0 && cursor < KHDAYS_CINE_SCENES) {
            v.shown = 1;
            v.first = first;
            v.cursor = cursor;
            v.master = (int16_t)(mode == 1 ? factor : mode == 2 ? -factor : 0);
            v.language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
        }
    }
    if (!v.shown) {
        memset(&v, 0, sizeof(v));
    }
    AcquireSRWLockExclusive(&lock);
    shown = v;
    ReleaseSRWLockExclusive(&lock);
}

void khdays_cinelist_view(KhdaysCineView *out)
{
    AcquireSRWLockShared(&lock);
    *out = shown;
    ReleaseSRWLockShared(&lock);
}

/* The port's list of chapters: it takes the buttons; the sound it asks for (+1, 0 none); the buttons
 * the game's list would get this frame. */
static volatile LONG taken, sound_wanted, seen;

void khdays_cinelist_take(int on)
{
    InterlockedExchange(&taken, on != 0);
}

void khdays_cinelist_sound(int sound)
{
    InterlockedExchange(&sound_wanted, sound + 1);
}

uint16_t khdays_cinelist_seen(void)
{
    return (uint16_t)InterlockedCompareExchange(&seen, 0, 0);
}

uint16_t khdays_cinelist_buttons(uint16_t buttons)
{
    const LONG sound = InterlockedExchange(&sound_wanted, 0);
    InterlockedExchange(&seen, buttons);
    if (sound != 0) {
        PlaySound(0, (int)sound - 1);
    }
    if (!InterlockedCompareExchange(&taken, 0, 0)) {
        return buttons;
    }
    return (uint16_t)(buttons & (KHDAYS_KEY_B | KHDAYS_KEY_START)); /* the game's list closes on them */
}
