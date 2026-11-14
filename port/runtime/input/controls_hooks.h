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
    int lock_candidate;       /* the game has a target it would lock on to (its selected one, -1 none) */
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

/* The KH2 camera's look: the right stick and the mouse as shares of its full turn, right and up positive.
 * KH2's stick has a dead zone on each axis and moves both axes by how far it is pushed overall. Normal is
 * KH2's own speed, so the percentage the stick has for Days' camera does not apply. */
void khdays_controls_camera_look(float *x, float *y);

/* In first person KH2 turns at its one rate once the stick leaves the dead zone (YS::PLAYER::_OVR_post_exec), and
 * tilts by the stick's up and down share of how far it is pushed (ObjCamera::subjective_control). */
void khdays_controls_first_person_look(float *x, float *y);

/* A command the menu chose this frame given at once (controls_hooks.c, after Ov022_UpdateCommandInput), and the
 * whole packet's landing in the row (MsgQueue_SendGate's hook): its pressed and held buttons. */
void khdays_controls_command_early(void *actor);
void khdays_controls_command_given(const unsigned short *packet, unsigned pressed, unsigned held);

/* How far the actor may turn this game frame, `apart` still to turn: the game's `limit`, for the local player a share
 * of what is left (KHDAYS_TURN_EASE, KHDAYS_TURN_SPEED); and whether its turns ease so (the game's snap round left
 * out). */
int khdays_controls_turn_limit(int actor, int limit, int apart);
int khdays_controls_turn_eased(int actor);

#endif
