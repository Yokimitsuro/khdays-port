/* The situation's inputs, taken at VBlank on the game's thread (situation.h; docs/RUNTIME_MAP.md
 * section 15). */
#include "situation.h"
#include "../runtime/core/io.h"
#include "../runtime/core/window.h"
#include "../runtime/core/session_log.h"
#include "khdays_ds_symbols.h" /* the DS addresses of what the port reads (prepare.py PORT_DS_SYMBOLS) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int khdays_overlay_loaded(int id); /* runtime/core/overlays.c */
int khdays_language_wanted(void);  /* runtime/core/language.c */
#include "../runtime/menus/newgame.h"
#include "../runtime/menus/camp.h"
#include "../runtime/movies/movie_hd.h"
extern int GetLanguage(void); /* the game's: gLangPath's language */
/* ov000's states in which the title's menu shows (the decomp's Ov000_TickMenuLoop: what each
 * choice goes on to; seen in KHDAYS_TRACE_UPDATES: the way back from each screen after it) */
extern void Ov000_MenuFadeInState(void);
extern void Ov000_ReentryState(void);
/* and the boot logos' (RUNTIME_MAP section 2) */
extern void Ov000_LogoFadeState(void);
extern void Ov000_LogoFadeState2(void);
extern void Ov000_LogoFadeState3(void);
extern void Ov000_HandoffState(void);
extern void Ov000_TickBootFadeIn(void);
extern void Ov000_TickMenuLoop(void);
extern void Ov000_TickMenuLevelChange(void);
extern void Ov000_TickFadeOutToScene(void);
extern void Ov000_BootFadeAndSubScreenSetup(void);
extern void Ov000_TickFadeThenPublishContext(void);
extern void Ov000_FadeOutAndStartMovie(void);

/* The overlays whose own variables the inputs read (RUNTIME_MAP section 1). */
#define OV_TITLE 0   /* ov000_title: logos, title, its menus, the save-file list */
#define OV_FIELD 2   /* ov002_field */
#define OV_BATTLE 22 /* ov022_battle: the party's characters */
#define OV_EVENT 23  /* ov023_event_script: the field's events */

/* The fields read inside the decomp's structures: the scene controller (SceneCtl: obj, entry, curId,
 * pendId, pendArg) and NitroSystem's frame texture VRAM regions (NNSGfdFrmTexRegionState, 0x18 bytes
 * each, gGfdFrmTexRegions[5]). */
#define SCENE_CUR 0x8u
#define SCENE_PENDING 0xcu
/* the scene object's heap (NNSi_FndGetCurrentRootHeap: the running object's word 8), and the
 * title's menu in it (Ov000_RefreshMenuLayout's Ov000MenuContext) */
#define SCENE_OBJECT 0x0u
#define OBJECT_UPDATE 0x14u
#define OBJECT_HEAP 0x20u
#define TITLE_COUNTER 0x0u
#define TITLE_SHOWS_FROM 0x3c
#define TITLE_PAGE 0x4c2du
#define TITLE_ROWS 0x4c2eu
#define TITLE_ALT 0x4c31u
#define TITLE_LOAD 0x4c53u
#define TITLE_EXTRA 0x4c54u
#define TITLE_SCENE 1
/* the panel of a mission's start (ov002: data_ov002_0207f614, a reference) and its state */
#define MISSION_START_PANEL 0x0207f614u
#define MISSION_START_STATE 0x18cu
/* the event scene's root (ov023: data_ov023_0208a784, its second word the scene) and its fields */
#define EVENT_ROOT 0x0208a784u
/* the mission's kind (data_0204c240, see situation.h) */
#define MISSION_FLAGS 0x0204c240u
#define EVENT_STATUS 0x8758cu

/* The watched update functions (situation_updates.h), by their native addresses, and which ran
 * in the game's last frame: `ran` since the last VBlank taken, `last` the set the takes give. A
 * VBlank in which no object ran (the game steps every two VBlanks in its menus) keeps the set
 * before. */
#define KHDAYS_UPDATE(name) extern void name(void);
#include "situation_updates.h"
#undef KHDAYS_UPDATE
static const khdays_function_pointer watched[KHDAYS_UPDATES] = {
#define KHDAYS_UPDATE(name) name,
#include "situation_updates.h"
#undef KHDAYS_UPDATE
};
static const char *const watched_names[KHDAYS_UPDATES] = {
#define KHDAYS_UPDATE(name) #name,
#include "situation_updates.h"
#undef KHDAYS_UPDATE
};
static uint64_t ran[KHDAYS_UPDATE_WORDS], last[KHDAYS_UPDATE_WORDS];
static uint32_t last_frame = ~0u;
static int any_ran;
/* the event's runs (Ov023_SceneRun) over the last 8 VBlanks, and how many */
static uint32_t event_history, event_runs;

void khdays_situation_update_ran(uintptr_t update)
{
    any_ran = 1;
    for (int i = 0; i < KHDAYS_UPDATES; ++i) {
        if ((uintptr_t)watched[i] == update) {
            ran[i / 64] |= 1ull << (i % 64);
            return;
        }
    }
}

static uint8_t u8_at(u32 address)
{
    return *(const volatile uint8_t *)KHDAYS_W(address);
}
static uint16_t u16_at(u32 address)
{
    return *(const volatile uint16_t *)KHDAYS_W(address);
}
static uint32_t u32_at(u32 address)
{
    return *(const volatile uint32_t *)KHDAYS_W(address);
}

static int tracing = -1;
static KhdaysSituationInputs traced;

/* Which of the boot logos' screens shows (1-3), by the scene object's update, or 0. */
static uint8_t title_logo(u32 object)
{
    const khdays_function_pointer update = khdays_function_lookup((KhdaysRef)u32_at(object + OBJECT_UPDATE));
    return update == NULL                                                   ? 0
           : update == Ov000_LogoFadeState                                  ? 1
           : update == Ov000_LogoFadeState2                                 ? 2
           : update == Ov000_LogoFadeState3 || update == Ov000_HandoffState ? 3
                                                                            : 0;
}

/* The title's way out into NEW GAME's dialogs (1) or back from a screen after it (2), by the scene
 * object's update (the inputs' title_passing). */
static uint8_t title_passing(u32 object)
{
    const khdays_function_pointer update = khdays_function_lookup((KhdaysRef)u32_at(object + OBJECT_UPDATE));
    return update == NULL                                                   ? 0
           : update == Ov000_TickFadeOutToScene                             ? 1
           : update == Ov000_ReentryState || update == Ov000_TickBootFadeIn ? 2
                                                                            : 0;
}

/* Whether the title's menu shows (KHDAYS_TITLE_*), by the scene object's update. */
static uint8_t title_shown(u32 object, u32 context)
{
    static const khdays_function_pointer menu[] = {Ov000_TickMenuLoop,
                                                   Ov000_TickMenuLevelChange,
                                                   Ov000_TickFadeOutToScene,
                                                   Ov000_BootFadeAndSubScreenSetup,
                                                   Ov000_TickFadeThenPublishContext,
                                                   Ov000_FadeOutAndStartMovie,
                                                   Ov000_ReentryState,
                                                   Ov000_TickBootFadeIn};
    const khdays_function_pointer update = khdays_function_lookup((KhdaysRef)u32_at(object + OBJECT_UPDATE));
    if (update == NULL) {
        return KHDAYS_TITLE_HIDDEN;
    }
    if (update == Ov000_MenuFadeInState) {
        return (int)u32_at(context + TITLE_COUNTER) >= TITLE_SHOWS_FROM ? KHDAYS_TITLE_SHOWN : KHDAYS_TITLE_WHITE;
    }
    for (size_t i = 0; i < sizeof(menu) / sizeof(menu[0]); ++i) {
        if (update == menu[i]) {
            return KHDAYS_TITLE_SHOWN;
        }
    }
    return KHDAYS_TITLE_HIDDEN;
}

static void trace_change(const char *name, uint64_t was, uint64_t now, uint32_t frame)
{
    if (was != now) {
        fprintf(stderr, "situation: frame %u, %s 0x%llx -> 0x%llx\n", frame, name, (unsigned long long)was,
                (unsigned long long)now);
        khdays_log("situation: frame %u, %s 0x%llx -> 0x%llx", frame, name, (unsigned long long)was,
                   (unsigned long long)now);
    }
}

void khdays_situation_take(KhdaysSituationInputs *in, uint32_t frame)
{
    memset(in, 0, sizeof(*in));
    in->taken = 1;
    if (frame != last_frame) {
        /* the VBlank that ended: did the event run in it */
        event_runs = khdays_situation_recent(
            &event_history, (int)((ran[KHDAYS_UPD_Ov023_SceneRun / 64] >> (KHDAYS_UPD_Ov023_SceneRun % 64)) & 1u));
        if (any_ran) {
            memcpy(last, ran, sizeof(last));
            memset(ran, 0, sizeof(ran));
            any_ran = 0;
        }
        last_frame = frame;
    }
    memcpy(in->updates, last, sizeof(in->updates));
    in->event_runs = event_runs;
    in->scene = (int32_t)u32_at(KHDAYS_DS_gSceneCtl + SCENE_CUR);
    in->pending = (int32_t)u32_at(KHDAYS_DS_gSceneCtl + SCENE_PENDING);
    for (int n = 0; n < 64; ++n) {
        if (khdays_overlay_loaded(n)) {
            in->overlays |= 1ull << n;
        }
    }
    in->powcnt1 = IO16(0x304);
    for (int e = 0; e < 2; ++e) {
        in->bldcnt[e] = IO16(0x50 + 0x1000 * e);
        in->bldalpha[e] = IO16(0x52 + 0x1000 * e);
        in->master[e] = IO16(0x6c + 0x1000 * e);
    }
    in->pause_mode = u8_at(KHDAYS_DS_gPauseMode);
    if (khdays_overlay_loaded(OV_TITLE)) {
        if (in->scene == TITLE_SCENE) {
            const u32 object = u32_at(KHDAYS_DS_gSceneCtl + SCENE_OBJECT);
            const u32 context = object != 0u ? u32_at(object + OBJECT_HEAP) : 0u;
            if (khdays_game_ram(context)) { /* main RAM or the game's heaps */
                const int8_t page = (int8_t)u8_at(context + TITLE_PAGE);
                in->title_valid = page >= 0 && page <= 2;
                in->title_page = page;
                in->title_row = in->title_valid ? (int8_t)u8_at(context + TITLE_ROWS + (u32)page) : 0;
                in->title_alt = u8_at(context + TITLE_ALT);
                in->title_load = u8_at(context + TITLE_LOAD);
                in->title_extra = u32_at(context + TITLE_EXTRA) != 0u;
                /* a language just chosen at once, before the game's own text takes it */
                in->title_lang = (uint8_t)(khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage());
                in->title_shown = title_shown(object, context);
                in->title_logo = title_logo(object);
                in->title_passing = title_passing(object);
                {
                    KhdaysNewGame ng;
                    khdays_newgame_state(&ng);
                    in->title_newgame = (uint8_t)ng.step;
                    in->title_newgame_difficulty = (uint8_t)ng.difficulty;
                    in->title_newgame_button = (uint8_t)ng.button;
                    in->title_newgame_subtitles = (uint8_t)ng.subtitles;
                }
            }
        }
    }
    if (khdays_overlay_loaded(OV_FIELD)) {
        in->tutorial = u32_at(KHDAYS_DS_data_ov002_0207f9fc);
        {
            const u32 panel = u32_at(MISSION_START_PANEL);
            in->mission_start = khdays_game_ram(panel) ? (int32_t)u32_at(panel + MISSION_START_STATE) : -1;
        }
    }
    if (khdays_overlay_loaded(OV_BATTLE)) {
        /* func_ov022_020882f8: the battle's root, its member count at +0x34 */
        const u32 root = u32_at(KHDAYS_DS_data_ov022_020b2e78 + 4u);
        in->party = root != 0u ? u8_at(root + 0x34u) : 0u;
    }
    in->movie = (uint8_t)khdays_movie_game_playing();
    in->mission_flags = u8_at(MISSION_FLAGS);
    in->event_status = -1;
    if (khdays_overlay_loaded(OV_EVENT)) {
        const u32 scene = u32_at(EVENT_ROOT + 4u);
        if (khdays_game_ram(scene)) {
            in->event_status = (int32_t)u16_at(scene + EVENT_STATUS);
        }
    }
    {
        int story = 0;
        in->camp_screen = (int8_t)khdays_camp_screen(&story);
        in->camp_story = (uint8_t)story;
    }
    if (tracing < 0) {
        tracing = getenv("KHDAYS_TRACE_SITUATION") != NULL;
    }
    if (tracing) {
        trace_change("scene", (uint64_t)(uint32_t)traced.scene, (uint64_t)(uint32_t)in->scene, frame);
        trace_change("overlays", traced.overlays, in->overlays, frame);
        trace_change("powcnt1", traced.powcnt1, in->powcnt1, frame);
        trace_change("bldcnt_a", traced.bldcnt[0], in->bldcnt[0], frame);
        trace_change("bldcnt_b", traced.bldcnt[1], in->bldcnt[1], frame);
        trace_change("bldalpha_b", traced.bldalpha[1], in->bldalpha[1], frame);
        trace_change("master_b", traced.master[1], in->master[1], frame);
        trace_change("pause_mode", traced.pause_mode, in->pause_mode, frame);
        trace_change("tutorial", traced.tutorial, in->tutorial, frame);
        trace_change("event_status", (uint64_t)(uint32_t)traced.event_status, (uint64_t)(uint32_t)in->event_status,
                     frame);
        trace_change("movie", traced.movie, in->movie, frame);
        trace_change("mission_flags", traced.mission_flags, in->mission_flags, frame);
        trace_change("mission_start", (uint64_t)(uint32_t)traced.mission_start, (uint64_t)(uint32_t)in->mission_start,
                     frame);
        trace_change("camp_screen", (uint64_t)(uint8_t)traced.camp_screen, (uint64_t)(uint8_t)in->camp_screen, frame);
        trace_change("party", traced.party, in->party, frame);
        trace_change("title",
                     (uint64_t)traced.title_newgame << 56 | (uint64_t)traced.title_newgame_button << 52 |
                         (uint64_t)traced.title_logo << 48 | (uint64_t)traced.title_shown << 44 |
                         (uint64_t)traced.title_valid << 40 | (uint64_t)(uint8_t)traced.title_page << 32 |
                         (uint64_t)(uint8_t)traced.title_row << 24 | (uint64_t)traced.title_alt << 16 |
                         (uint64_t)traced.title_load << 8 | traced.title_extra,
                     (uint64_t)in->title_newgame << 56 | (uint64_t)in->title_newgame_button << 52 |
                         (uint64_t)in->title_logo << 48 | (uint64_t)in->title_shown << 44 |
                         (uint64_t)in->title_valid << 40 | (uint64_t)(uint8_t)in->title_page << 32 |
                         (uint64_t)(uint8_t)in->title_row << 24 | (uint64_t)in->title_alt << 16 |
                         (uint64_t)in->title_load << 8 | in->title_extra,
                     frame);
        for (int i = 0; i < KHDAYS_UPDATES; ++i) {
            const int was = khdays_situation_ran(&traced, i), now = khdays_situation_ran(in, i);
            if (was != now) {
                fprintf(stderr, "situation: frame %u, update %s %s\n", frame, watched_names[i],
                        now ? "runs" : "stopped");
                khdays_log("situation: frame %u, update %s %s", frame, watched_names[i], now ? "runs" : "stopped");
            }
        }
        traced = *in;
    }
}
