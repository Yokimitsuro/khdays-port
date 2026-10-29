/* What the game shows this frame, for the one-screen picture: a situation and its states, decided
 * by the port (situation_decide.c) from what the game runs -- its scene and overlays, its objects'
 * update functions, the state the decomp names -- and the display's registers; a few states are
 * the port's own checks of the picture (host/render/onescreen.c). */
#ifndef KHDAYS_UI_SITUATION_H
#define KHDAYS_UI_SITUATION_H

#include <stdint.h>

enum {
    KHDAYS_SIT_INTRO,
    KHDAYS_SIT_TITLE,
    KHDAYS_SIT_LOAD_MENU,
    KHDAYS_SIT_SAVE_MENU,
    KHDAYS_SIT_SETTINGS,
    KHDAYS_SIT_DAY_COUNTER,
    KHDAYS_SIT_MOVIE,
    KHDAYS_SIT_FIELD,
    KHDAYS_SIT_CAMP_MENU,
    KHDAYS_SIT_PAUSE,
    KHDAYS_SIT_TUTORIAL,
    KHDAYS_SIT_FIELD_3D_BOTH,
    KHDAYS_SIT_MISSION_REVIEW,
    KHDAYS_SIT_SHOP,
    KHDAYS_SIT_THOUGHTS,
    KHDAYS_SIT_WORLD_SELECTOR,
    KHDAYS_SIT_GAME_OVER,
    KHDAYS_SIT_THE_END,
    KHDAYS_SIT_UNKNOWN,
    KHDAYS_SITUATIONS
};

/* The states, a bit each. */
enum {
    KHDAYS_ST_HUD,
    KHDAYS_ST_DIALOG,
    KHDAYS_ST_CHALLENGE,
    KHDAYS_ST_INFO_UPPER,
    KHDAYS_ST_INFO_LOWER,
    KHDAYS_ST_LOWER_3D,      /* the 3D on the lower screen alone */
    KHDAYS_ST_PARTY_4,       /* four members in the battle's party */
    KHDAYS_ST_INFO_NEW,      /* the lower screen's INFORMATION line changed a moment ago (host/render/onescreen.c) */
    KHDAYS_ST_MISSION_PANEL, /* the lower screen shows the mission's panel (host/render/onescreen.c) */
    KHDAYS_ST_TITLE_LIT,     /* the title's lower-screen fades not shown -- NEW GAME's way into its
                           * dialogs, the dialogs, the way back from them (host/render/onescreen.c) */
    KHDAYS_ST_EVENT,         /* the pause opened in an event of the field (its scene, ov023, running) */
    KHDAYS_ST_HOLO,          /* a mission played again: the Holo-Missions, a challenge or Mission Mode
                              * (data_0204c240's bits 0-2); its events show their label at the upper
                              * screen's top left ("HOLOMEMORIA", a challenge's "RELOJ PARADO") */
    KHDAYS_ST_LOWER_SHOWN,   /* with the 3D on both screens, the lower screen shows a picture: Sora's
                              * memories under the game's noise (host/render/onescreen.c) */
    KHDAYS_ST_MISSION_PANEL_MOD,
    KHDAYS_ST_INFORMATION_MOD, /* a sequence replaces the native information crops */
    KHDAYS_STATES
};

/* The HUD's modes: complete, exploration (no INFORMATION banner) and none (no HUD). */
enum { KHDAYS_HUD_COMPLETE, KHDAYS_HUD_EXPLORATION, KHDAYS_HUD_NONE, KHDAYS_HUD_MODES };

/* The game's update functions the port's decision watches (situation_updates.h), a bit each. */
enum {
#define KHDAYS_UPDATE(name) KHDAYS_UPD_##name,
#include "situation_updates.h"
#undef KHDAYS_UPDATE
    KHDAYS_UPDATES
};
#define KHDAYS_UPDATE_WORDS ((KHDAYS_UPDATES + 63) / 64)

/* What the game's thread copies at VBlank (docs/RUNTIME_MAP.md section 15): the game's data the
 * decision reads, each through its decomp symbol (an overlay's only while
 * that overlay is loaded, else 0), and the display's power register. */
typedef struct KhdaysSituationInputs {
    uint32_t taken; /* 1 when filled for this package */
    /* which watched update functions ran since the VBlank before (bit KHDAYS_UPD_*): what the
     * game's objects are doing (khdays_situation_update_ran) */
    uint64_t updates[KHDAYS_UPDATE_WORDS];
    int32_t scene;         /* gSceneCtl's current scene id (+8), RUNTIME_MAP section 1 */
    int32_t pending;       /* and its pending one (+0xc) */
    uint64_t overlays;     /* bit n: overlay n loaded (the ones below 64; the decision names a few) */
    uint32_t powcnt1;      /* bit 15: engine A on the upper screen */
    uint16_t bldcnt[2];    /* each engine's BLDCNT (0x04000050, 0x04001050) */
    uint16_t bldalpha[2];  /* BLDALPHA (0x52) */
    uint16_t master[2];    /* MASTER_BRIGHT (0x6c) */
    uint8_t pause_mode;    /* gPauseMode (PauseMenu_SetMode): the pause */
    uint8_t movie;         /* the game's own movie plays (runtime/movies/movie_hd.h khdays_movie_game_playing:
                               * Ov024_MobiClip_OpenStreamSlot to Ov024_MobiClip_StopPlayback) */
    uint8_t mission_flags; /* data_0204c240: bit 1 a challenge after the story's camp (Ov025_MissionList_StartMission
                               * sets 3 with the challenge's thresholds, 1 otherwise; RUNTIME_MAP 15.6) */
    uint8_t party;         /* the battle's party members: data_ov022_020b2e78[1] + 0x34, ov022 loaded
                               * (func_ov022_020882f8; Ov022_Party_SyncAndUpdate walks that many) */
    int32_t mission_start; /* the panel of a mission's start (data_ov002_0207f614, ov002 loaded): its state +0x18c
                               * (Ov002_Hud_IsPanelOpen: 1-8 open; 9-11 a person's talk, its box in 10), -1 without the panel */
    int32_t event_status;  /* the field's event (ov023: data_ov023_0208a784[1]): its status halfword +0x8758c
                               * (bit 1 its dialogue raised, 2 text pending, 3 closing: Ov023_SceneRun,
                               * Ov023_SetPendingText, Ov023_BeginClose), -1 without the event */
    uint32_t engine_swaps; /* how many of the last 8 VBlanks engine A changed screens in (the 3D on both
                               * screens) */
    int8_t
        camp_screen; /* the loaded camp's screen (runtime/menus/camp.h khdays_camp_screen), KHDAYS_CAMP_NONE (-2) none */
    uint8_t camp_story; /* 1: the story's camp (ov025), 0 Mission Mode's (ov008) */
    uint32_t tutorial;  /* data_ov002_0207f9fc, ov002 loaded: a tutorial's page (Ov002_OpenTutorialPage sets it,
                               * Ov002_TeardownBriefing clears it) */
    /* the title's menu (ov000 loaded, scene 1): its scene object's context (the object's +0x20,
     * NNSi_FndGetCurrentRootHeap's while it runs; Ov000_RefreshMenuLayout's Ov000MenuContext) --
     * its level (+0x4c2d: 0 the first, else the story level or, altLayout, the mission's), each
     * level's row (+0x4c2e), altLayout (+0x4c31), loadAvailable (+0x4c53: a save, CONTINUE shown)
     * and extraOptionAvailable (+0x4c54: THEATER MODE shown) */
    uint8_t title_valid;
    int8_t title_page;
    int8_t title_row;
    uint8_t title_alt;
    uint8_t title_load;
    uint8_t title_extra;
    uint8_t title_lang; /* the language chosen (khdays_language_wanted), else GetLanguage(): the firmware's
                               * 1-5 (English, French, German, Italian, Spanish) */
    /* whether the menu shows: KHDAYS_TITLE_SHOWN from its fade-in's frame 0x3c on, in its loop, a
     * level's cross-fade and its fades out and back; KHDAYS_TITLE_WHITE in the fade-in before then;
     * 0 in the screens after it (difficulty, save list, theater, character select). Read from the
     * object's update (+0x14). */
    uint8_t title_shown;
    /* the boot logos' screen (1-3) while the object's update is one of their states
     * (Ov000_LogoFadeState, ...2, ...3; Ov000_HandoffState after them, the third), else 0 */
    uint8_t title_logo;
    /* the new game's dialogs (runtime/menus/newgame.h): KHDAYS_NEWGAME_*, the difficulty, the button
     * selected, whether the subtitles show */
    uint8_t title_newgame;
    uint8_t title_newgame_difficulty;
    uint8_t title_newgame_button;
    uint8_t title_newgame_subtitles;
    /* the title's way out and back: 1 while its update is Ov000_TickFadeOutToScene (NEW GAME's
     * fade of the lower screen to black, into its dialogs), 2 while it is Ov000_ReentryState or
     * Ov000_TickBootFadeIn (back from a screen after it: the lower screen from black), else 0 */
    uint8_t title_passing;
} KhdaysSituationInputs;

enum { KHDAYS_TITLE_HIDDEN, KHDAYS_TITLE_WHITE, KHDAYS_TITLE_SHOWN };

/* At VBlank, on the game's thread: copies the inputs above and, with
 * KHDAYS_TRACE_SITUATION, reports each one that changed (on stderr and in
 * the session log). */
void khdays_situation_take(KhdaysSituationInputs *inputs, uint32_t frame);

/* Called for every update an object runs (Obj_UpdateAll's check, runtime/core/diag.c), with its
 * native address: marks it if the decision watches it. */
void khdays_situation_update_ran(uintptr_t update);

/* Whether the watched update `which` (KHDAYS_UPD_*) ran in the inputs' VBlank. */
static inline int khdays_situation_ran(const KhdaysSituationInputs *in, int which)
{
    return (int)((in->updates[which / 64] >> (which % 64)) & 1u);
}

typedef struct KhdaysSituation {
    int scene;       /* KHDAYS_SIT_* */
    uint32_t states; /* 1 << KHDAYS_ST_* */
    /* the title's menu (the port's, host/render/onescreen.c, from the inputs' title fields): which level
     * (KHDAYS_TITLE_*), how many options it shows, the selected one; the game's language (1-5) */
    int title_menu;
    int title_count;
    int title_row;
    int title_lang;
    uint32_t frame;   /* the game's VBlanks: an animated backdrop's clock (ui/composition.h) */
    uint32_t content; /* the screens that show something (1 the upper, 2 the lower; host/render/onescreen.c) */
} KhdaysSituation;

/* the title's levels; INTRO its fade-in while the lower screen is white, LOGO the boot logos (count 3,
 * row the one on show), NEWGAME the new game's dialogs (NEWGAME_LOAD with CONTINUE too: count the
 * step -- 1 difficulty, 2 subtitles, 3 review -- and row the choice, for the review difficulty x 4 +
 * subtitles x 2 + its button) */
enum {
    KHDAYS_TITLE_NONE,
    KHDAYS_TITLE_ROOT,
    KHDAYS_TITLE_STORY,
    KHDAYS_TITLE_MISSION,
    KHDAYS_TITLE_INTRO,
    KHDAYS_TITLE_LOGO,
    KHDAYS_TITLE_NEWGAME,
    KHDAYS_TITLE_NEWGAME_LOAD
};

/* What the decision reads of a frame (the presenting side fills it,
 * host/render/onescreen.c): its inputs, its 3D list's counts, and a screen's pixel
 * as kd_split gives it (gfx/compose.h: K in 8-bit channels, its alpha 0 where
 * no 2D shows over the 3D), for `screen` 0 the upper, 1 the lower. */
typedef struct KhdaysSituationFrame {
    const KhdaysSituationInputs *in;
    uint32_t alike;         /* the VBlanks before it that showed the frame decided before again */
    int polygons, vertices; /* the 3D list shown: its polygons and vertices */
    uint32_t (*pixel)(void *ctx, int screen, int x, int y);
    void *ctx;
} KhdaysSituationFrame;

/* Records whether engine A `changed` screens at this VBlank (`history` bit n: n VBlanks ago) and
 * returns how many of the last 8 did. */
uint32_t khdays_situation_swaps(uint32_t *history, int changed);

/* This frame's situation (situation_decide.c). */
void khdays_situation_decide(const KhdaysSituationFrame *frame, KhdaysSituation *out);

/* Whether a picture wider than the DS's (two screens in widescreen) shows more of what the game
 * shows: where its 3D is the game's world, not a picture inside its 2D. From the inputs alone (any
 * thread that took them). */
int khdays_situation_widens(const KhdaysSituationInputs *in);

/* Whether a frame decided again may decide otherwise (a picture standing still is then decided
 * again as its VBlanks pass): never, as the decision reads the game's state, not its pictures'
 * history. */
int khdays_situation_settling(void);

/* The HUD's mode: complete at first; the binding cycles it. */
int khdays_situation_hud_mode(void);
void khdays_situation_hud_mode_next(void);

/* The names the composition file and the session log use. */
const char *khdays_situation_name(int scene);
const char *khdays_situation_state_name(int state);
int khdays_situation_named(const char *name, int length);
int khdays_situation_state_named(const char *name, int length);

#endif
