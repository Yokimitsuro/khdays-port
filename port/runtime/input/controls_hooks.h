/* Where the port's controls meet the game (controls_hooks.c): the game's functions call these through
 * prepare.py's HOOKS, and each changes nothing unless its enhancement is on. KHDAYS_TRACE_CONTROLS
 * reports what the game does with its controls. */
#ifndef KHDAYS_CONTROLS_HOOKS_H
#define KHDAYS_CONTROLS_HOOKS_H

#include "../../hal/hal.h"

/* What the hooks have learnt about the game's state, each with the game
 * frame it was seen in (khdays_input_game_frames()). */
typedef struct KhdaysGameControls {
    int lock_active;          /* a target is locked on (Ov022_ReadSelectionInput) */
    int lockon_scheme;        /* the game's Config option 0 (0x37c4) */
    u32 selection_frame;      /* when those were read */
    u32 selection_live_frame; /* the last frame the lock-on logic went on with the buttons (not a tutorial's pages) */
    int r_repeat;             /* the lock-on's R counter then (repeatAny: 0 at an R press, +0x1000 a frame) */
    int r_pressed;            /* R was newly pressed that frame */
    int command_scheme;       /* the game's Config option 6 (0x37c6) */
    u32 command_input_frame;  /* the last frame the player had field control, no HUD panel open */
} KhdaysGameControls;

const KhdaysGameControls *khdays_controls_state(void);

/* The lock-on's own button is letting go of the target: the game takes the coming R as its double press. */
void khdays_controls_lock_let_go(void);

#endif
