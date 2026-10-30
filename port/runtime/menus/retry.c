/* The Retry in the pause of Mission Mode's missions and the story's challenges (retry.h), for a mission
 * played alone: the port draws Continue, Retry and Give Up (ui/pause.cpp) and moves the game's cursor so
 * A on Retry opens the game's "Are you sure?", whose yes runs Give Up, which prepare.py hooks turn into
 * the field again.
 * docs/RUNTIME_MAP.md, "Mission Mode's pause and the port's Retry"; KHDAYS_TRACE_RETRY=1 logs it. */
#include "retry.h"

#include "../input/input.h"
#include "../core/io.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../../ui/bg.h"
#include "../../ui/elements.h"
#include "../../ui/parts.h"

#include "khdays_ds_symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int GetLanguage(void);
extern int Session_GetSetup(void);
extern int Session_IsActive(void);
extern void PlaySound(int bank, int id);
extern void GameState_ClearFlag(int flag);
extern int GameState_IsFlagSet(unsigned int flag);
int khdays_language_wanted(void); /* language.c */
u32 khdays_events_vblank_count(void);

#define FIELD_SCENE 2u
#define SCENE_CURRENT 0x8u
#define MISSION_MODE 0x4u             /* data_0204c240's first byte: a Mission Mode mission */
#define CHALLENGE 0x2u                /* ... a challenge (with bit 0, Ov025_MissionList_StartMission) */
#define CHALLENGE_TARGETS 0x0204c254u /* data_0204c254: a challenge's targets, copied as it starts */
#define TARGETS_KEPT 0xeu             /* its u16 that, when not 0, keeps them as they are */
#define TARGETS 0x10u                 /* its three targets */
#define PAUSE_SESSION 0x2u            /* the pause's kind: Mission Mode's three entries */
#define CTX_COUNT 0xd0u               /* the pause context's entries: 3 its first page, 2 "Are you sure?" */
#define CTX_CURSOR 0xd4u
#define SETUP_PLAYERS 0x4u /* the session's setup (Session_GetSetup): the players */
#define PAUSE_LAYER 2      /* engine A's BG2 */
#define LINE_X 10          /* the lines' first cell */
#define CONTINUE_ROW 11
#define GIVE_UP_ROW 13
#define QUIT_FLAG 0x2087 /* set by Ov002_RetireSubFlow on giving up */
#define GIVE_UP 2        /* the game's entry */

static struct {
    int shown;     /* the port's lines were up at the last latch */
    int asked;     /* the game's "Are you sure?" page was up since */
    int cursor;    /* the port's */
    int chosen;    /* A on Retry: the game's "Are you sure?" page is Retry's */
    int confirmed; /* yes there: the game's Give Up is a Retry */
    int rest;      /* a frame without the game's press after one */
    u16 held;      /* the player's buttons at the last latch */
} retry;
static int trace = -1;

static u32 rd32(u32 address)
{
    return *(const volatile u32 *)KHDAYS_W(address);
}

static u16 rd16(u32 address)
{
    return *(const volatile u16 *)KHDAYS_W(address);
}

static u8 rd8(u32 address)
{
    return *(const volatile u8 *)KHDAYS_W(address);
}

static int in_ram(u32 address)
{
    return khdays_game_ram(address); /* main RAM or the game's heaps */
}

static int tracing(void)
{
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_RETRY") != NULL;
    }
    return trace;
}

static void say(const char *what, int a, int b)
{
    if (tracing()) {
        khdays_log("retry: %s %d %d", what, a, b);
        fprintf(stderr, "retry: %s %d %d\n", what, a, b);
    }
}

/* The pause context, or 0. */
static u32 pause_context(void)
{
    const u32 ctx = rd32(KHDAYS_DS_data_0204be08 + 4u);
    return in_ram(ctx) ? ctx : 0u;
}

/* Whether BG2 shows a line from cell (LINE_X, row). */
static int line_there(int row)
{
    KhdaysBgLayer l;
    return khdays_bg_text_layer(0, PAUSE_LAYER, &l) &&
           khdays_bg_cell_shows(0, &l, khdays_bg_map_entry(0, &l, LINE_X + 1, row));
}

/* Whether the pause up is one the port changes (Mission Mode's or a challenge's, one player): its
 * context into `ctx_out`. */
static int pause_ours(u32 *ctx_out)
{
    const u32 setup = (u32)Session_GetSetup();
    if (khdays_ui_mode(KHDAYS_UI_PAUSE) != KHDAYS_UI_ON || rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT) != FIELD_SCENE ||
        !(rd8(KHDAYS_DS_data_0204c240) & (MISSION_MODE | CHALLENGE)) || !in_ram(setup) ||
        rd32(setup + SETUP_PLAYERS) != 1u || rd8(KHDAYS_DS_gPauseMode) == 0 ||
        !(rd16(KHDAYS_DS_data_0204bda4) & PAUSE_SESSION)) {
        return 0;
    }
    *ctx_out = pause_context();
    return *ctx_out != 0;
}

/* The first page is up: the port's cursor goes on Continue, or back on the line "Are you sure?" came
 * from. Called by whichever sees the page first (its picture, its lines or the buttons), so the page
 * is never drawn without Retry. */
static void latch(void)
{
    if (retry.shown) {
        return;
    }
    retry.cursor = retry.asked ? (retry.chosen ? 1 : GIVE_UP) : 0;
    retry.chosen = 0;
    retry.asked = 0;
    retry.shown = 1;
    khdays_ui_changed(); /* the port's lines: drawn again even if the game's picture is the same */
    say("first page at vblank, the port's cursor", (int)khdays_events_vblank_count(), retry.cursor);
}

/* The pause's page: 0 its first page drawn, 1 "Are you sure?", 2 the first page not drawn yet, -1 none
 * (or not a pause the port changes). */
static int pause_page(u32 *ctx_out)
{
    const u32 setup = (u32)Session_GetSetup();
    u32 ctx;
    if (tracing() && rd8(KHDAYS_DS_gPauseMode) != 0) {
        /* what decides whether the port changes this pause, once per change */
        static int last = -1;
        const int now = rd8(KHDAYS_DS_data_0204c240) << 16 |
                        (in_ram(setup) ? (int)rd32(setup + SETUP_PLAYERS) : 0xff) << 8 |
                        (rd16(KHDAYS_DS_data_0204bda4) & 0xff);
        static int last_pieces = -1;
        KhdaysPauseView v;
        int pieces = 0;
        if (khdays_pause_view(&v)) {
            for (int i = 0; i < v.count; ++i) {
                pieces = pieces * 16 + v.piece[i].key + 1;
            }
        }
        if (now != last) {
            last = now;
            say("a pause: the mission's kind and the players", now >> 16, (now >> 8) & 0xff);
            say("a pause: the game's mode word", now & 0xff, rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT));
        }
        static u32 last_target = 0xffffffffu;
        if (rd32(CHALLENGE_TARGETS + TARGETS) != last_target) {
            last_target = rd32(CHALLENGE_TARGETS + TARGETS);
            say("a pause: a challenge's first two targets", (int)last_target,
                (int)rd32(CHALLENGE_TARGETS + TARGETS + 4u));
        }
        if (pieces != last_pieces) {
            last_pieces = pieces;
            say("a pause: its pieces (KHDAYS_PAUSE_* + 1, a hex digit each) and its entries", pieces,
                pause_context() != 0 ? (int)rd32(pause_context() + CTX_COUNT) : -1);
        }
    }
    if (!pause_ours(&ctx)) {
        return -1;
    }
    *ctx_out = ctx;
    if (rd32(ctx + CTX_COUNT) != 3u) {
        return 1;
    }
    return line_there(CONTINUE_ROW) && line_there(GIVE_UP_ROW) ? 0 : 2;
}

/* --- The pause's pieces, for the pause part -------------------------------------------- */

/* The pause's pieces as its panel draws them (SubObject_SetDrawParams): slots 0-2 the lines, 3 the
 * title, 4 the question, each with its lit strip. The slots and strips: docs/RUNTIME_MAP.md, "What the
 * pause draws". */
#define ENTRY(slot) (0xcu + 0x18u + 0x18u * (u32)(slot))
#define ENTRY_SRC_X 0x0u
#define ENTRY_SRC_Y 0x4u
#define ENTRY_DST_X 0x8u
#define ENTRY_DST_Y 0xcu
#define ENTRY_W 0x10u
#define ENTRY_H 0x14u
#define CTX_LIT 0xb0u
#define TITLE_SLOT 3
#define QUESTION_SLOT 4
#define ONLY_TITLE_FLAG 0x2483u

static int strip_key(u32 x, u32 y)
{
    static const struct {
        u32 x, y;
        int key;
    } strips[] = {
        {0, 0, KHDAYS_PAUSE_CONTINUE}, {11, 0, KHDAYS_PAUSE_SKIP}, {0, 6, KHDAYS_PAUSE_WITHDRAW},
        {11, 6, KHDAYS_PAUSE_YES},     {22, 6, KHDAYS_PAUSE_NO},
    };
    for (size_t i = 0; i < sizeof(strips) / sizeof(strips[0]); ++i) {
        if (strips[i].x == x && strips[i].y == y) {
            return strips[i].key;
        }
    }
    return -1;
}

/* The slot's piece; 0 when its key is not one the port knows. */
static int piece_of(u32 ctx, int slot, KhdaysPausePiece *p)
{
    const u32 e = ctx + ENTRY(slot);
    p->key = slot == TITLE_SLOT      ? KHDAYS_PAUSE_TITLE
             : slot == QUESTION_SLOT ? KHDAYS_PAUSE_QUESTION
                                     : strip_key(rd32(e + ENTRY_SRC_X), rd32(e + ENTRY_SRC_Y));
    p->x = (int)rd32(e + ENTRY_DST_X);
    p->y = (int)rd32(e + ENTRY_DST_Y);
    p->w = (int)rd32(e + ENTRY_W);
    p->h = (int)rd32(e + ENTRY_H);
    p->lit = slot < TITLE_SLOT ? (int)rd32(ctx + CTX_LIT + 8u * (u32)slot) : 0;
    return p->key >= 0 && p->w > 0 && p->h > 0 && p->w <= 32 && p->h <= 32;
}

/* The port's last picture of the first page's lines (count 0: none yet) and its lit strip's phase,
 * shown again while the game redraws its own lines. */
static KhdaysPauseView last_view;
static int last_phase;

/* The port's lines as last drawn, lit as its cursor is now. */
static void last_lines(KhdaysPauseView *v)
{
    *v = last_view;
    for (int k = 0; k < 3; ++k) {
        v->piece[1 + k].lit = retry.cursor == k ? last_phase : 0;
    }
}

int khdays_pause_view(KhdaysPauseView *v)
{
    const u32 ctx = pause_context();
    const int session = (rd16(KHDAYS_DS_data_0204bda4) & PAUSE_SESSION) != 0;
    int slots[KHDAYS_PAUSE_PIECES], n = 0, readable = 1, first_page, its_lines;
    u32 ours = 0;
    KhdaysBgLayer l;
    v->shown = 0;
    v->count = 0;
    v->layer = session ? 2 : 3; /* PauseMenu_Open */
    v->language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
    if (rd8(KHDAYS_DS_gPauseMode) == 0 || ctx == 0 || !khdays_bg_text_layer(0, v->layer, &l)) {
        return 0;
    }
    slots[n++] = TITLE_SLOT;
    if (!GameState_IsFlagSet(ONLY_TITLE_FLAG)) {
        if (session && rd32(ctx + CTX_COUNT) == 2u) {
            slots[n++] = QUESTION_SLOT, slots[n++] = 0, slots[n++] = 1;
        } else {
            slots[n++] = 0, slots[n++] = session ? 2 : 1;
        }
    }
    for (int i = 0; i < n; ++i) {
        KhdaysPausePiece *p = &v->piece[v->count];
        if (!piece_of(ctx, slots[i], p) ||
            !khdays_bg_cell_shows(0, &l, khdays_bg_map_entry(0, &l, p->x + p->w / 2, p->y))) {
            readable = 0; /* not drawn yet, or drawn from a strip the port does not know */
            break;
        }
        ++v->count;
    }
    first_page = pause_ours(&ours) && ours == ctx && rd32(ctx + CTX_COUNT) == 3u;
    its_lines = readable && v->count == 3 && v->piece[1].key == KHDAYS_PAUSE_CONTINUE &&
                v->piece[2].key == KHDAYS_PAUSE_WITHDRAW;
    if (first_page && its_lines) {
        latch(); /* Retry from the page's first picture, not from the game's next read of the pad */
    }
    if (first_page && !its_lines && retry.shown && last_view.count == 4) {
        /* the game redrawing its lines as the port moves its cursor: the port's last picture of them,
         * not a frame of the game's own (its cursor on Give Up, where the port's is not) */
        last_lines(v);
        say("the game's lines redrawn at vblank: the port's last picture of them", (int)khdays_events_vblank_count(),
            retry.cursor);
        return 1;
    }
    if (!readable) {
        return 0;
    }
    if (retry.shown && its_lines) {
        /* the port's three lines: Retry where Give Up was, Give Up below, lit as its cursor is */
        const int phase = v->piece[1].lit != 0 ? v->piece[1].lit : v->piece[2].lit != 0 ? v->piece[2].lit : 1;
        v->piece[3] = v->piece[2];
        v->piece[3].y += v->piece[2].h;
        v->piece[2].key = KHDAYS_PAUSE_RETRY;
        v->count = 4;
        for (int k = 0; k < 3; ++k) {
            v->piece[1 + k].lit = retry.cursor == k ? phase : 0;
        }
        last_view = *v;
        last_view.shown = 1;
        last_phase = phase;
    }
    v->shown = 1;
    return 1;
}

/* The game's entry the port's line stands for. */
static int game_entry(int cursor)
{
    return cursor == 0 ? 0 : GIVE_UP;
}

int khdays_retry_view(KhdaysRetryView *v)
{
    u32 ctx = 0;
    v->shown = pause_page(&ctx) == 0;
    if (v->shown) {
        latch(); /* its lines drawn: Retry among them from then on */
    }
    v->cursor = retry.cursor;
    v->game_cursor = ctx != 0 ? (int)rd32(ctx + CTX_CURSOR) : 0;
    v->language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
    return v->shown;
}

uint16_t khdays_retry_buttons(uint16_t buttons)
{
    const u16 pressed = (u16)(buttons & ~retry.held);
    u32 ctx = 0;
    const int page = pause_page(&ctx);
    int game;
    u16 out;
    retry.held = buttons;
    if (retry.confirmed && rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT) != FIELD_SCENE) {
        retry.confirmed = 0; /* the field left another way: the next Give Up is one */
        say("the field left without the Retry", 0, 0);
    }
    if (page == 2) {
        retry.rest = 0;
        return buttons; /* the first page still being drawn */
    }
    if (page != 0) {
        if (page < 0 && (retry.shown || retry.asked)) {
            say("pause closed, chosen and confirmed:", retry.chosen, retry.confirmed);
            retry.chosen = 0; /* closed without its yes (a yes leaves `confirmed`) */
            retry.asked = 0;
        }
        retry.asked |= page == 1 && (retry.shown || retry.asked);
        retry.shown = 0;
        last_view.count = 0;
        retry.rest = 0;
        return buttons;
    }
    game = (int)rd32(ctx + CTX_CURSOR);
    latch();
    out = (u16)(buttons & ~(KHDAYS_KEY_UP | KHDAYS_KEY_DOWN));
    if (pressed & (KHDAYS_KEY_UP | KHDAYS_KEY_DOWN)) {
        const int to = (retry.cursor + ((pressed & KHDAYS_KEY_DOWN) ? 1 : 2)) % 3;
        if (game_entry(to) == game_entry(retry.cursor)) {
            PlaySound(0, 0); /* the game's cursor sound (TabPanel_HandleUpDown): its own stays */
        }
        retry.cursor = to;
        khdays_ui_changed(); /* its line lit at the next VBlank, though the game's picture may not change */
    }
    if (game != game_entry(retry.cursor)) {
        /* on the way: either direction takes the game's from one of its lines to the other */
        out = (u16)(out & ~KHDAYS_KEY_A);
        if (!retry.rest) {
            out |= KHDAYS_KEY_DOWN;
        }
        retry.rest = !retry.rest;
        return out;
    }
    retry.rest = 0;
    if (pressed & KHDAYS_KEY_A) {
        retry.chosen = retry.cursor == 1;
        if (retry.chosen) {
            say("A on Retry: the game's \"Are you sure?\" page", retry.cursor, game);
        }
    }
    return out;
}

void khdays_retry_confirmed(void)
{
    if (retry.chosen) {
        retry.confirmed = 1;
        say("yes: the game's Give Up is a Retry", 0, 0);
    }
    retry.chosen = 0;
}

int khdays_retry_pending(void)
{
    return retry.confirmed;
}

int khdays_retry_take(void)
{
    if (!retry.confirmed) {
        if (Session_IsActive() && getenv("KHDAYS_TEST_REVIEW") != NULL && GameState_IsFlagSet(QUIT_FLAG)) {
            /* a testing knob for scripted runs that cannot clear a mission: a give-up in a session goes on
             * as a cleared mission does, to the mission review (Ov002_SendSeatTally, scene 3), then the
             * results */
            GameState_ClearFlag(QUIT_FLAG);
            say("KHDAYS_TEST_REVIEW: the give-up goes on to the mission review", 0, 0);
        }
        return 0;
    }
    retry.confirmed = 0;
    GameState_ClearFlag(QUIT_FLAG);
    if ((rd8(KHDAYS_DS_data_0204c240) & (CHALLENGE | MISSION_MODE)) == CHALLENGE &&
        rd16(CHALLENGE_TARGETS + TARGETS_KEPT) == 0) {
        /* the field's construction multiplies a challenge's three targets by 100 (Ov002_ConstructGameplayScene),
         * and only the challenge's start copies them in: as they were before it, for the construction again */
        for (u32 k = 0; k < 3; ++k) {
            volatile int *target = (volatile int *)KHDAYS_W(CHALLENGE_TARGETS + TARGETS + 4u * k);
            *target /= 100;
        }
        say("a challenge's targets as they were before the field", rd32(CHALLENGE_TARGETS + TARGETS),
            rd32(CHALLENGE_TARGETS + TARGETS + 4u));
    }
    say("the field again instead of the results", 0, 0);
    return 1;
}

/* ---- The game over's menu (retry.h) ------------------------------------------------------------- */

#define OV_GAME_OVER 27
#define GO_SCENE 0x02084364u  /* data_ov027_02084364: the scene's work */
#define GO_CHOICE 0x02083ee0u /* data_ov027_02083ee0: the question's cursor, 0 Yes, 1 No */
#define GO_SESSION                                                                                                     \
    KHDAYS_DS_data_0204c240        /* its first byte's bit 2: a wireless session's (its waiting strip, no menu) */
#define GO_FLAGS 0x1cu             /* bit 0: still loading */
#define GO_MODE 0x24u              /* 1 the menu; 2 Continue, 4 Withdraw, bit 4 the question up */
#define GO_CURSOR 0x5d8u           /* the menu's line: 0 Continue, 1 Decrease Challenge, 2 Withdraw */
#define GO_PROMPT 0x5e0u           /* Decrease Challenge offered */
int khdays_overlay_loaded(int id); /* overlays.c */

static volatile int over_kh2;

int khdays_game_over_ours(void)
{
    return khdays_overlay_loaded(OV_GAME_OVER) && (rd8(GO_SESSION) & 4u) == 0;
}

void khdays_game_over_set_kh2(int on)
{
    over_kh2 = on;
}

int khdays_game_over_hides_orb(void)
{
    return over_kh2 && khdays_game_over_ours();
}

int khdays_game_over_view(KhdaysGameOverView *v)
{
    u32 scene, mode;
    int prompt, cursor;
    memset(v, 0, sizeof(*v));
    if (!khdays_overlay_loaded(OV_GAME_OVER) || !in_ram(scene = rd32(GO_SCENE)) || (rd8(GO_SESSION) & 4u) != 0 ||
        (rd32(scene + GO_FLAGS) & 1u) != 0) {
        return 0;
    }
    mode = rd32(scene + GO_MODE);
    prompt = rd32(scene + GO_PROMPT) != 0;
    cursor = (int)rd32(scene + GO_CURSOR);
    if (mode == 1u) {
        v->keys[v->rows++] = KHDAYS_PAUSE_GO_CONTINUE;
        if (prompt) {
            v->keys[v->rows++] = KHDAYS_PAUSE_GO_EASIER;
        }
        v->keys[v->rows++] = KHDAYS_PAUSE_GO_WITHDRAW;
        v->row = cursor <= 0 ? 0 : cursor == 1 ? (prompt ? 1 : 0) : v->rows - 1;
    } else if ((mode & 0x14u) == 0x14u) {
        /* Withdraw's question: Yes above No, as the DS's panel (its cursor starts on No) */
        v->keys[0] = KHDAYS_PAUSE_YES;
        v->keys[1] = KHDAYS_PAUSE_NO;
        v->rows = 2;
        v->row = rd32(GO_CHOICE) == 0 ? 0 : 1;
        v->question = 1;
    } else {
        return 0; /* chosen: the game fades it away */
    }
    v->language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
    v->shown = 1;
    return 1;
}
