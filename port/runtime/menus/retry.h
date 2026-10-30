/* The Retry of Mission Mode and the story's challenges (retry.c): the pause of a mission played alone
 * gets a line between Continue and Give Up that starts the mission again from its beginning, after the
 * game's own "Are you sure?". The lines are drawn by the pause part (ui/pause.cpp). */
#ifndef KHDAYS_RETRY_H
#define KHDAYS_RETRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The pause's lines as the port shows them now. */
typedef struct KhdaysRetryView {
    int shown;       /* the pause's first page is up with the port's three lines */
    int cursor;      /* 0 Continue, 1 Retry, 2 Give Up */
    int game_cursor; /* the game's own: 0 Continue, 2 Give Up (its lines' states in VRAM follow it) */
    int language;    /* the game's (host/screens/ui_text.h KHDAYS_LANGUAGE_*) */
} KhdaysRetryView;

/* Fills `v`; returns v->shown. Read on the game's thread (the parts' VBlank). */
int khdays_retry_view(KhdaysRetryView *v);

/* The pause's pieces as the game draws them now (its panel's draw entries:
 * SubObject_SetDrawParams), with the port's Retry among its lines where it is shown: for the pause
 * part to draw them again in a mod's pictures. */
enum {
    KHDAYS_PAUSE_TITLE,
    KHDAYS_PAUSE_QUESTION, /* "Are you sure?" */
    KHDAYS_PAUSE_CONTINUE,
    KHDAYS_PAUSE_SKIP,     /* a scene's pause: Skip */
    KHDAYS_PAUSE_WITHDRAW, /* Mission Mode's: Give Up (the game's English says Withdraw) */
    KHDAYS_PAUSE_RETRY,    /* the port's */
    KHDAYS_PAUSE_YES,
    KHDAYS_PAUSE_NO,
    /* the game over's lines (its own words: Continue, Decrease Challenge, Withdraw) */
    KHDAYS_PAUSE_GO_CONTINUE,
    KHDAYS_PAUSE_GO_EASIER,
    KHDAYS_PAUSE_GO_WITHDRAW,
    KHDAYS_PAUSE_KEYS
};
#define KHDAYS_PAUSE_PIECES 5
typedef struct KhdaysPausePiece {
    int key;        /* KHDAYS_PAUSE_* */
    int x, y, w, h; /* its cells in the layer's map */
    int lit;        /* 0, or the lit strip shown: 1 or 2 (the game's blinks between two) */
} KhdaysPausePiece;
typedef struct KhdaysPauseView {
    int shown; /* the game's pause is up, drawn */
    int layer; /* the BG of engine A it is drawn in */
    int count; /* its pieces, the title first */
    KhdaysPausePiece piece[KHDAYS_PAUSE_PIECES];
    int language;
} KhdaysPauseView;

/* Fills `v`; returns v->shown (0 too for a piece the port does not know). Game's thread. */
int khdays_pause_view(KhdaysPauseView *v);

/* The game over's menu (ov027, a mission lost alone): its lines -- Continue, Decrease Challenge (where the
 * game offers it), Withdraw -- or its "Are you sure?" and Yes, No, for the window to draw as KH2's pause
 * (host/screens/game_pause.h); the game's own panel (engine A's BG1) the pause part leaves out. */
typedef struct KhdaysGameOverView {
    int shown;    /* the menu or its question is up */
    int rows;     /* 2 or 3 */
    int keys[3];  /* KHDAYS_PAUSE_* */
    int row;      /* the one lit */
    int question; /* "Are you sure?" over Yes and No */
    int language;
} KhdaysGameOverView;
#define KHDAYS_GAME_OVER_LAYER 1

/* Fills `v`; returns v->shown. Game's thread. */
int khdays_game_over_view(KhdaysGameOverView *v);
/* Whether the game over is one whose menu the window can draw (ov027 loaded, no session's), and whether
 * it does (the pause part tells it, ui/pause.cpp): then the game's own firefly, its floating orb, is
 * left out (port/tools/prepare.py's hook). */
int khdays_game_over_ours(void);
void khdays_game_over_set_kh2(int on);
int khdays_game_over_hides_orb(void);

/* The buttons the game reads (input.c): while the port's lines are up, the directions move the
 * port's cursor and the game's is taken to the line the port's stands for. */
uint16_t khdays_retry_buttons(uint16_t buttons);

/* What the game's hooks ask (port/tools/prepare.py HOOKS): yes on the "Are you sure?" page of
 * Give Up (PauseMenu_ConfirmFrame); whether that Give Up is a Retry (Ov027_GameOverFadeIn); and,
 * once, to start the mission again instead of the results (Ov002_StepSessionMenu). */
void khdays_retry_confirmed(void);
int khdays_retry_pending(void);
int khdays_retry_take(void);

#ifdef __cplusplus
}
#endif

#endif
