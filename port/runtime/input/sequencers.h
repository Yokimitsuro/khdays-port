/* The command menu on the gamepad's D-pad (sequencers.c), pure C: a press of a menu direction is a step
 * of the menu, which Ov022_UpdateCommandInput takes to the game's own cursor routines that frame
 * (prepare.py; docs/PLATFORM.md, "The controls the DS did not have") -- not the DS's presses for it,
 * which hold X and so stop Roxas while they play. Lock-on on its own buttons plays the DS's presses. */
#ifndef KHDAYS_SEQUENCERS_H
#define KHDAYS_SEQUENCERS_H

#include <stdint.h>

enum { KHDAYS_MENU_UP, KHDAYS_MENU_DOWN, KHDAYS_MENU_LEFT, KHDAYS_MENU_RIGHT, KHDAYS_MENU_DIRECTIONS };

typedef struct KhdaysMenuStepper {
    int held;   /* the direction held last frame, -1 for none */
    int frames; /* the game frames it has been held since its press */
} KhdaysMenuStepper;

void khdays_menu_stepper_reset(KhdaysMenuStepper *s);

/* Whether the game's menu moves in this direction under this Config option 6 at all (option 6 = 1
 * only moves down). */
int khdays_menu_direction_exists(int direction, int command_scheme);

/* One game frame: the direction the menu steps (KHDAYS_MENU_*), -1 for none -- a new press, and up and
 * down again while held (after KHDAYS_MENU_REPEAT_AFTER game frames, every KHDAYS_MENU_REPEAT_EVERY).
 * `directions`: a bit per menu direction held. `field`: the player had field control in the game's last
 * frame (without it nothing steps). `command_scheme`: Config option 6. */
#define KHDAYS_MENU_REPEAT_AFTER 8
#define KHDAYS_MENU_REPEAT_EVERY 2
int khdays_menu_stepper_frame(KhdaysMenuStepper *s, uint32_t directions, int field, int command_scheme);

/* Lock-on on its own buttons: the presses of R (twice to lock, once to let go or for the next target) and
 * L (Type B's previous target) the game's Ov022_ReadSelectionInput takes (docs/PLATFORM.md). */
enum { KHDAYS_LOCK_ON, KHDAYS_LOCK_NEXT, KHDAYS_LOCK_PREV, KHDAYS_LOCK_ACTIONS };

#define KHDAYS_LOCK_QUEUE 4

typedef struct KhdaysLockSequencer {
    int action;                   /* the action being played, -1 for none */
    int step;                     /* its next step */
    int queue[KHDAYS_LOCK_QUEUE]; /* the ones pressed since, in order */
    int queued;                   /* how many */
    uint32_t last_actions;        /* held last frame (to see new presses) */
    int let_go;                   /* this frame's R is to let go of the target, not to move to the next */
} KhdaysLockSequencer;

void khdays_lock_sequencer_reset(KhdaysLockSequencer *s);

/* Whether the action has a DS press sequence under this Controls type. */
int khdays_lock_action_exists(int action, int lockon_scheme);

/* One game frame. `actions`: a bit per action held. Without `field` (a menu, a tutorial, the pause) a
 * sequence stops and the buttons stand for the DS's R and L while held. `locked`: the game's "a
 * target is locked". `r_free`: an R press now counts as a single one (9 frames since the last R), so
 * an R sequence starts only then; it waits while the player holds R or L. Returns the DS buttons to
 * add. */
uint16_t khdays_lock_sequencer_frame(KhdaysLockSequencer *s, uint32_t actions, int field, int locked, int lockon_scheme,
                                     uint16_t held, int r_free);

#endif
