/* The port's own decision of what the game shows, for the one-screen picture (situation.h): from its
 * scene, overlays, objects' updates and state by the decomp's names, and the display's registers where
 * a fact is the display's. Each rule says where it was measured (docs/RUNTIME_MAP.md section 15.6;
 * "play walk" is port/tools/regress_walks/play.input, frames from its start). */
#include "situation.h"
#include "../runtime/menus/camp.h"

#include <stddef.h>

/* The scenes (RUNTIME_MAP section 1). */
enum {
    SCENE_TITLE = 1,
    SCENE_FIELD = 2,
    SCENE_DAY_CARD = 5,
    SCENE_RESULTS = 6,
    SCENE_SELECT = 7,
    SCENE_MONOLOGUE = 10,
    SCENE_OPENING = 11,
    SCENE_CAMP = 19
};

/* The camp's screens (runtime/menus/camp.h khdays_camp_screen; KHDAYS_TRACE_CAMP): 0 the menu and
 * -1 for a frame between two (play walk f2475-2879, f3741 on); Mission Mode's 1 Holo-Missions (play
 * walk f2881-3739), 2 Panels (camp_panels script f3099-5297, the menu's layout), 5 Preferences
 * (camp_cfg script from f3501); the story's 4 Save (save walk f3431-5267). */
enum { CAMP_MENU = 0, CAMP_HOLO_MISSIONS = 1, CAMP_STORY_SAVE = 4, CAMP_PREFERENCES = 5, CAMP_BETWEEN = -1 };

static int previous = KHDAYS_SIT_UNKNOWN;
static int pause_over_both; /* the pause up was opened over the 3D on both screens */
/* the last camp the game had loaded was the story's (ov025), not Mission Mode's (ov008): the field
 * after it is the story's */
static int story_camp = 1;

/* The HUD's mode (complete, exploration, none), cycled by the player's binding. */
static int hud_mode = KHDAYS_HUD_COMPLETE;

int khdays_situation_hud_mode(void)
{
    return hud_mode;
}

void khdays_situation_hud_mode_next(void)
{
    hud_mode = (hud_mode + 1) % KHDAYS_HUD_MODES;
}

/* The decision keeps no history of pictures: a frame decided again decides the same. */
int khdays_situation_settling(void)
{
    return 0;
}

static int ran(const KhdaysSituationInputs *in, int which)
{
    return khdays_situation_ran(in, which);
}

static int title_screens(const KhdaysSituationInputs *in)
{
    /* the save list: its states run (play walk f1147-1945) */
    if (ran(in, KHDAYS_UPD_Ov000_TickLoadScene) || ran(in, KHDAYS_UPD_Ov000_TickSelectionScene)) {
        return KHDAYS_SIT_LOAD_MENU;
    }
    /* the boot logos (situation_take.c title_logo: Ov000_LogoFadeState..3 and the handoff) */
    if (in->title_logo != 0) {
        return KHDAYS_SIT_INTRO;
    }
    return KHDAYS_SIT_TITLE;
}

static int camp_screens(const KhdaysSituationInputs *in)
{
    /* Mission Mode's shop: no screen of the camp's (-1) while it is open */
    if (ran(in, KHDAYS_UPD_Ov008_OpenShopState) || ran(in, KHDAYS_UPD_Ov008_WaitShopClosed)) {
        return KHDAYS_SIT_SHOP;
    }
    switch (in->camp_screen) {
    case CAMP_MENU: return KHDAYS_SIT_CAMP_MENU;
    case CAMP_HOLO_MISSIONS: return KHDAYS_SIT_WORLD_SELECTOR;
    case CAMP_PREFERENCES: return KHDAYS_SIT_SETTINGS;
    case CAMP_STORY_SAVE: return in->camp_story ? KHDAYS_SIT_SAVE_MENU : KHDAYS_SIT_CAMP_MENU;
    case CAMP_BETWEEN:
        return previous; /* the camp's first frames keep the screen before it (multiplayer walk f5675-5701) */
    default: return KHDAYS_SIT_CAMP_MENU;
    }
}

/* The field's panel of a mission's start (ov002: data_ov002_0207f614, its state +0x18c): on the play
 * walk 1-8 from f4207 to f4582, the mission's information on the upper screen and no deck while
 * it is 3-6 (f4229-4562). */
static int mission_start_info(const KhdaysSituationInputs *in)
{
    return in->mission_start >= 3 && in->mission_start <= 6;
}

/* A person talking in the field: the same panel in state 10 shows the talk's box across the bottom
 * of the upper screen. States 9 and 11 come before and after it with the HUD still or again drawn,
 * so they are not a dialog (RUNTIME_MAP section 15.6, "dialog"). */
static int field_talk(const KhdaysSituationInputs *in)
{
    return in->mission_start == 10;
}

/* The overlays the rules name (RUNTIME_MAP section 1). */
#define OV_GAME_OVER 27 /* ov027_game_over: Ov027_GameOverFadeIn .. Ov027_GameOverIdle (mission walk f5571-6539) */
#define OV_SHOP 26      /* ov026_shop: the moogle's shop, a sub-scene of the field (Ov026_FillSceneHooks) */
#define OV_CREDITS                                                                                                     \
    11 /* ov011: the credits (Ov011_CreateScene .. Ov011_StartTitleExit, khdays_movie_hd_credits' hooks) */

static int loaded(const KhdaysSituationInputs *in, int overlay)
{
    return (int)((in->overlays >> overlay) & 1u);
}

static int field_screens(const KhdaysSituationInputs *in)
{
    /* the game over (the field's sub-flow of kind 9, RUNTIME_MAP section 15) */
    if (loaded(in, OV_GAME_OVER)) {
        return KHDAYS_SIT_GAME_OVER;
    }
    /* the moogle's shop, loaded over the field */
    if (loaded(in, OV_SHOP)) {
        return KHDAYS_SIT_SHOP;
    }
    /* the pause (gPauseMode, PauseMenu_SetMode: mission walk 2 from f5029 to f5529) */
    if (in->pause_mode != 0) {
        return KHDAYS_SIT_PAUSE;
    }
    /* a tutorial's page is open (data_ov002_0207f9fc: Ov002_OpenTutorialPage sets it,
     * Ov002_TeardownBriefing clears it; story walk from f12009) */
    if (in->tutorial != 0) {
        return KHDAYS_SIT_TUTORIAL;
    }
    /* the 3D on both screens: engine A swaps screens every VBlank. Two swaps in the last 8 start it
     * and one keeps it, so a swap the game skips does not change the layout */
    if (in->engine_swaps >= 2u || (previous == KHDAYS_SIT_FIELD_3D_BOTH && in->engine_swaps >= 1u)) {
        return KHDAYS_SIT_FIELD_3D_BOTH;
    }
    return KHDAYS_SIT_FIELD;
}

static int situation_of(const KhdaysSituationInputs *in)
{
    /* the game's own movie (the opening, scene 11; day 255's in the field: story walk
     * f2262-8729, Ov024_MobiClip_UpdatePlayback running) */
    if (in->movie) {
        return KHDAYS_SIT_MOVIE;
    }
    if (in->camp_screen != KHDAYS_CAMP_NONE && (in->scene == SCENE_CAMP || in->camp_story)) {
        return camp_screens(in);
    }
    /* the credits (no walk reaches them: from the decomp's names) */
    if (loaded(in, OV_CREDITS)) {
        return KHDAYS_SIT_THE_END;
    }
    switch (in->scene) {
    case 0:
        /* no scene: the boot, before the logos' (the intro), or a scene's end before the next
         * (mission walk f6555-6560, the field to the results): the screen before it stays */
        return previous == KHDAYS_SIT_UNKNOWN ? KHDAYS_SIT_INTRO : previous;
    case SCENE_TITLE: return title_screens(in);
    case SCENE_SELECT:
        /* Mission Mode's character select: the title's two screens (today's layout) */
        return KHDAYS_SIT_TITLE;
    case SCENE_DAY_CARD: return KHDAYS_SIT_DAY_COUNTER;
    case SCENE_MONOLOGUE: return KHDAYS_SIT_THOUGHTS;
    case SCENE_RESULTS:
        /* a mission's results (ov005: mission walk f6561-7730) */
        return KHDAYS_SIT_MISSION_REVIEW;
    case SCENE_OPENING: return KHDAYS_SIT_MOVIE;
    case SCENE_FIELD: return field_screens(in);
    default: return KHDAYS_SIT_UNKNOWN;
    }
}

/* The field's 3D world in every screen of the field (its pause, a tutorial's page, the game over)
 * and the title's logo over white. Elsewhere beside the DS's columns the wider 3D shows what the
 * game never drew there: on the mission walk, blue beside the camp menu's frame (f2469-2885,
 * f3754-4126) and the stars' end beside the world selector (f2886-3753). */
int khdays_situation_widens(const KhdaysSituationInputs *in)
{
    if (in->movie || loaded(in, OV_CREDITS) ||
        (in->camp_screen != KHDAYS_CAMP_NONE && (in->scene == SCENE_CAMP || in->camp_story))) {
        return 0;
    }
    return in->scene == SCENE_FIELD || in->scene == SCENE_TITLE;
}

static uint32_t states_of(const KhdaysSituationInputs *in, int scene)
{
    uint32_t s = 0;
#define ST(n) (1u << KHDAYS_ST_##n)
    if (scene == KHDAYS_SIT_FIELD || scene == KHDAYS_SIT_FIELD_3D_BOTH || scene == KHDAYS_SIT_TUTORIAL) {
        /* a dialog: an event's text (ov023's status bit 1) or a person talking. The event's status
         * counts only while Ov023_SceneRun runs and before the event closes (bit 3): a finished event
         * leaves its last status behind, which hid the day's panel and the map. */
        const int event_text = in->event_status >= 0 && (in->event_status & 2) != 0 && (in->event_status & 8) == 0 &&
                               ran(in, KHDAYS_UPD_Ov023_SceneRun);
        if (mission_start_info(in)) {
            s |= ST(INFO_UPPER);
        } else if (event_text || field_talk(in)) {
            s |= ST(DIALOG);
        } else if (hud_mode != KHDAYS_HUD_NONE) {
            s |= ST(HUD);
        }
        /* a challenge: the story's camp starts its missions with data_0204c240 3 for a challenge
         * (the challenge's thresholds copied, Ov025_MissionList_StartMission; run07 walk from
         * f5359), 1 otherwise; Mission Mode's byte is the session's (Ov008SessionInfo: 3 a
         * wireless host), so only after the story's camp */
        if (story_camp && (in->mission_flags & 2u) != 0u) {
            s |= ST(CHALLENGE);
        }
        /* a mission played again: the story's camp sets bit 0 (with bit 1 for a challenge), Mission Mode
         * bit 2 (7 on the host of the multiplayer walk); a story's own mission and the Grey Area 0 (the
         * results' exit clears it, Ov005_UpdateMenuExitTransition) */
        if ((in->mission_flags & 7u) != 0u) {
            s |= ST(HOLO);
        }
        /* the 3D on the lower screen alone: engine A there, no swap in the last 8 VBlanks */
        if ((in->powcnt1 & 0x8000u) == 0u && in->engine_swaps == 0u && scene != KHDAYS_SIT_FIELD_3D_BOTH) {
            s |= ST(LOWER_3D);
        }
        /* the port's: with four members the party's rows reach higher (ov022's member count) */
        if ((s & ST(HUD)) != 0u && in->party >= 4u) {
            s |= ST(PARTY_4);
        }
    }
    /* the pause opened in an event (ov023's scene there: a cutscene, its pause "Continue" and
     * "Skip"), where the lower screen shows none of the field's HUD; in a battle's pause no event
     * scene is there (mission walk f5029-5529) */
    if (scene == KHDAYS_SIT_PAUSE && (in->event_status >= 0 || pause_over_both)) {
        s |= ST(EVENT);
    }
#undef ST
    return s;
}

void khdays_situation_decide(const KhdaysSituationFrame *frame, KhdaysSituation *out)
{
    const KhdaysSituationInputs *in = frame->in;
    if (in->camp_screen != KHDAYS_CAMP_NONE) {
        story_camp = in->camp_story;
    } else if (in->scene == SCENE_TITLE) {
        story_camp = 1; /* a new game or a story's file follows, or Mission Mode's camp says otherwise */
    }
    out->scene = situation_of(in);
    /* a pause opened over the 3D on both screens (a boss with Sora's memories: mission 61's Guard
     * Armor): its lower screen holds his 3D, none of the field's HUD -- as an event's (else the
     * banner's, the map's and the panel's places are drawn black) */
    if (out->scene == KHDAYS_SIT_PAUSE && previous != KHDAYS_SIT_PAUSE) {
        pause_over_both = previous == KHDAYS_SIT_FIELD_3D_BOTH;
    } else if (out->scene != KHDAYS_SIT_PAUSE) {
        pause_over_both = 0;
    }
    out->states = states_of(in, out->scene);
    previous = out->scene;
}
