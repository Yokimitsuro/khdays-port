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
/* KHDAYS_LOCK_AUTO: as KHDAYS_LOCK_ON, dropped if a target is locked by the time its turn comes: it only ever locks. */
enum { KHDAYS_LOCK_ON, KHDAYS_LOCK_NEXT, KHDAYS_LOCK_PREV, KHDAYS_LOCK_AUTO, KHDAYS_LOCK_ACTIONS };

/* Auto lock-on (Enhancements): a lock lost without the player letting go (the target defeated) locks on again. */
typedef struct KhdaysLockAuto {
    int was_locked, let_go, pending;
} KhdaysLockAuto;

/* One game frame: KHDAYS_LOCK_AUTO's bit once a lock lost without the player letting go has a target to lock on to
 * again (`candidate`: the game has one selected; right after a defeat it has none for a while). `let_go`: the lock-on
 * button let go (kept until the lock is lost); `pressed`: the player's R or Select just now (a loss now is theirs).
 * Without `field` it forgets. */
uint32_t khdays_lock_auto_frame(KhdaysLockAuto *s, int let_go, int pressed, int field, int locked, int candidate);

#define KHDAYS_LOCK_LET_GO_FRAMES 3 /* a lock lost this many frames after the player's R or Select: let go */

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

/* The presses of one action dropped, queued or being played. */
void khdays_lock_sequencer_cancel(KhdaysLockSequencer *s, int action);

/* Whether the action has a DS press sequence under this Controls type. */
int khdays_lock_action_exists(int action, int lockon_scheme);

/* One game frame. `actions`: a bit per action held. Without `field` (a menu, a tutorial, the pause) a
 * sequence stops and the buttons stand for the DS's R and L while held. `locked`: the game's "a
 * target is locked". `r_free`: an R press now counts as a single one (9 frames since the last R), so
 * an R sequence starts only then; it waits while the player holds R or L. Returns the DS buttons to
 * add. */
uint16_t khdays_lock_sequencer_frame(KhdaysLockSequencer *s, uint32_t actions, int field, int locked, int lockon_scheme,
                                     uint16_t held, int r_free);

/* The field's control packet (Ov022_SendControlPacket: the buttons held, the command chosen, the target):
 * the game builds it in the battle's network send (func_ov022_02089704), after the host's characters have
 * read the one before at the start of their update (Ov022_RefreshSlotRows), so they move by the buttons
 * of the game frame before -- a game frame late. Alone the port puts this frame's buttons in the local
 * player's row as they are about to read it, once a game frame; the game's own packet still brings the
 * rest (docs/RUNTIME_MAP.md, "The field's controls"). */
typedef struct KhdaysControlSender {
    uint32_t early; /* the game frame + 1 the buttons were put in before the characters read them, 0 never */
} KhdaysControlSender;

void khdays_control_sender_reset(KhdaysControlSender *s);

/* The host's characters are about to read the table in game frame `frame`: whether to put the buttons in
 * now (once a frame). `allowed`: alone, in the ordinary battle (controls_hooks.c says when). */
int khdays_control_sender_early(KhdaysControlSender *s, uint32_t frame, int allowed);

#endif
