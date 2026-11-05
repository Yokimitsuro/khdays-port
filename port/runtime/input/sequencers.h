/* The command menu on the gamepad's D-pad (sequencers.c), pure C: a press of a menu direction becomes
 * the presses the game's Config option 6 wants (X, or X held with a direction), one game frame a step
 * (docs/PLATFORM.md, "The controls the DS did not have"). */
#ifndef KHDAYS_SEQUENCERS_H
#define KHDAYS_SEQUENCERS_H

#include <stdint.h>

enum { KHDAYS_MENU_UP, KHDAYS_MENU_DOWN, KHDAYS_MENU_LEFT, KHDAYS_MENU_RIGHT, KHDAYS_MENU_DIRECTIONS };

typedef struct KhdaysMenuSequencer {
    int direction;         /* the direction being played, -1 for none */
    int step;              /* its next step */
    int queued;            /* the direction pressed while one played, -1 for none (one at most) */
    uint32_t last_actions; /* the menu directions held last frame (to see new presses) */
} KhdaysMenuSequencer;

/* What the sequencer gives the game this frame. */
typedef struct KhdaysMenuFrame {
    uint16_t buttons; /* DS buttons to press (added to the player's) */
    int dpad_own;     /* 1: the frame's D-pad bits are these alone (the player's are dropped) */
} KhdaysMenuFrame;

void khdays_menu_sequencer_reset(KhdaysMenuSequencer *s);

/* Whether the game can move through the menu in this direction under this
 * option 6 at all (option 6 = 1 only moves down). */
int khdays_menu_direction_exists(int direction, int command_scheme);

/* One game frame. `directions`: a bit per menu direction held now. `field`: the player had field
 * control in the game's last frame (without it nothing plays and a sequence stops).
 * `command_scheme`, `lockon_scheme`: Config options 6 and 0. A sequence waits while the player holds
 * X, or what makes X a shortcut (L with option 0 = 0, L and R with option 0 = 1). */
KhdaysMenuFrame khdays_menu_sequencer_frame(KhdaysMenuSequencer *s, uint32_t directions, int field, int command_scheme,
                                            int lockon_scheme, uint16_t held);

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
