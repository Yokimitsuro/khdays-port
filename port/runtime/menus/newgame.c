/* The new game's dialogs (newgame.h): ov000's new game waits in Ov000_WaitSubMenuResult, its context's
 * +0x4bc4 the step (0 fade out, 1 fade in, 2 difficulty, 3 review), +0x20 the difficulty, +0x24 the
 * review's button. The port's subtitles question comes between difficulty and review: UP/DOWN move, A
 * answers, B goes back; the game gets no buttons meanwhile. */
#include "newgame.h"

#include "../input/input.h"
#include "../core/rom.h"
#include "../core/game_memory.h"
#include "../../host/app/host.h"

#include "khdays_ds_symbols.h"

#include <stdio.h>
#include <windows.h>
#undef PlaySound /* windows.h's (winmm's), not the game's */

extern int GetLanguage(void);
extern int PlaySound(int bank, int sound);
extern void Ov000_WaitSubMenuResult(void);
extern void Ov000_TickBootFadeTransition(void);
int khdays_overlay_loaded(int id); /* overlays.c */
int khdays_language_wanted(void);  /* language.c */

#define OV_TITLE 0
#define TITLE_SCENE 1
#define SCENE_OBJECT 0x0u
#define SCENE_CURRENT 0x8u
#define OBJECT_UPDATE 0x14u
/* data_ov000_0205ac28, the context's pointer: the word after data_ov000_0205ac24 in ov000's
 * data (both ROM addresses: the DS symbols the port reads hold the first) */
#define NEWGAME_CONTEXT (KHDAYS_DS_data_ov000_0205ac24 + 4u)
#define STEP 0x4bc4u
#define DIFFICULTY 0x20u
#define BUTTON 0x24u
enum { MODE_DIFFICULTY = 2, MODE_REVIEW = 3, MODE_START = 5 };

static volatile LONG subtitles = 1; /* any thread */
static volatile LONG answered;      /* 1 + the answer, for the window's thread */
static int asking, cursor, last_mode = -1, held;
/* the buttons held when the question was answered: the game's pad would take a button it gets
 * back as a new press (an A on the review's no), so they stay the port's until let go */
static u16 swallowed;

void khdays_subtitles_set(int on)
{
    InterlockedExchange(&subtitles, on ? 1 : 0);
}

int khdays_subtitles_shown(void)
{
    return InterlockedCompareExchange(&subtitles, 0, 0) != 0;
}

int khdays_newgame_take_answer(int *on)
{
    const LONG a = InterlockedExchange(&answered, 0);
    if (a == 0) {
        return 0;
    }
    *on = (int)a - 1;
    return 1;
}

/* The title's object's update, or NULL out of the title. */
static khdays_function_pointer title_update(void)
{
    u32 object;
    if (!khdays_overlay_loaded(OV_TITLE) || rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT) != TITLE_SCENE) {
        return NULL;
    }
    object = rd32(KHDAYS_DS_gSceneCtl + SCENE_OBJECT);
    if (!khdays_game_ram(object)) { /* main RAM or the game's heaps */
        return NULL;
    }
    return khdays_function_lookup((KhdaysRef)rd32(object + OBJECT_UPDATE));
}

/* The new game's context, or 0 when its sub-scene is not on. */
static u32 context(void)
{
    u32 ctx;
    if (title_update() != Ov000_WaitSubMenuResult) {
        return 0;
    }
    ctx = rd32(NEWGAME_CONTEXT);
    return khdays_game_ram(ctx) ? ctx : 0;
}

/* Whether the question's pictures show: one screen and a mod's for the language (over the story
 * level with NEW GAME alone, newgame1_*, or with CONTINUE too, newgame2_*). */
static int question_shown(void)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    char relative[96], path[1024];
    const int language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
    if (!(khdays_host_graphics_flags() & KHDAYS_HOST_ONE_SCREEN) || language < 1 || language > 5) {
        return 0;
    }
    for (int options = 1; options <= 2; ++options) {
        snprintf(relative, sizeof(relative), "layouts/title/%s/newgame%d_2_0.txt", codes[language], options);
        if (!khdays_mods_find(relative, path, (int)sizeof(path))) {
            return 0;
        }
    }
    return 1;
}

/* Whether the question starts now: the review just came from the difficulty. */
static int question_starts(int mode)
{
    return mode == MODE_REVIEW && last_mode == MODE_DIFFICULTY && question_shown();
}

void khdays_newgame_state(KhdaysNewGame *out)
{
    static KhdaysNewGame started; /* the review a start came from, its step none before one */
    const u32 ctx = context();
    out->step = KHDAYS_NEWGAME_NONE;
    out->difficulty = out->button = 0;
    out->subtitles = khdays_subtitles_shown();
    if (ctx == 0) {
        /* the game's fade from the new game into the story (KHDAYS_TRACE_UPDATES: after
         * Ov000_WaitSubMenuResult) shows the review that started it */
        if (started.step != KHDAYS_NEWGAME_NONE && title_update() == Ov000_TickBootFadeTransition) {
            *out = started;
        } else {
            started.step = KHDAYS_NEWGAME_NONE;
        }
        return;
    }
    out->difficulty = (int)(rd32(ctx + DIFFICULTY) % 3u);
    if (rd32(ctx + STEP) == MODE_START) {
        out->step = KHDAYS_NEWGAME_REVIEW;
        out->button = 0;
        started = *out;
    } else if (rd32(ctx + STEP) == MODE_REVIEW) {
        /* the game's frame that chose the difficulty shows the question already: the buttons
         * that start it come with the next frame's pad */
        const int question = asking || question_starts(MODE_REVIEW);
        out->step = question ? KHDAYS_NEWGAME_SUBTITLES : KHDAYS_NEWGAME_REVIEW;
        out->button = asking ? cursor : question ? !out->subtitles : (int)(rd32(ctx + BUTTON) & 1u);
    } else {
        out->step = KHDAYS_NEWGAME_DIFFICULTY;
        out->button = out->difficulty;
    }
}

u16 khdays_newgame_buttons(u16 buttons)
{
    const u32 ctx = context();
    const int mode = ctx != 0 ? (int)rd32(ctx + STEP) : -1;
    u16 now;
    if (question_starts(mode)) {
        asking = 1;
        cursor = khdays_subtitles_shown() ? 0 : 1;
        held = buttons; /* the A that chose the difficulty is not an answer */
    }
    if (mode != MODE_REVIEW) {
        asking = 0;
    }
    last_mode = mode;
    swallowed &= buttons;
    if (!asking) {
        return (u16)(buttons & ~swallowed);
    }
    now = (u16)(buttons & ~held);
    held = buttons;
    if (now & (KHDAYS_KEY_UP | KHDAYS_KEY_DOWN)) {
        cursor ^= 1;
        PlaySound(0, 0);
    }
    if (now & KHDAYS_KEY_A) {
        asking = 0;
        swallowed = buttons;
        khdays_subtitles_set(cursor == 0);
        InterlockedExchange(&answered, 1 + (cursor == 0));
        PlaySound(0, 1);
        return 0;
    }
    if (now & KHDAYS_KEY_B) {
        asking = 0;
        return KHDAYS_KEY_B; /* the review's B: back to the difficulty, its own sound */
    }
    return 0;
}
