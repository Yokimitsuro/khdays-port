/* The command menu's steps on the gamepad's D-pad, lock-on's presses on its own buttons (sequencers.h). */
#include "sequencers.h"

#include <stddef.h>

/* The DS buttons the sequences press (runtime/input/input.h has them all). */
#define DS_R (1u << 8)
#define DS_L (1u << 9)

/* A step is a frame's buttons; each sequence ends with a frame of nothing,
 * so R is seen let go before the next sequence presses it again. */
#define MAX_STEPS 4

typedef struct Sequence {
    int length;
    uint16_t steps[MAX_STEPS];
} Sequence;

int khdays_menu_direction_exists(int direction, int command_scheme)
{
    if (direction < 0 || direction >= KHDAYS_MENU_DIRECTIONS) {
        return 0;
    }
    return command_scheme == 0 || direction == KHDAYS_MENU_DOWN;
}

void khdays_menu_stepper_reset(KhdaysMenuStepper *s)
{
    s->held = -1;
    s->frames = 0;
}

int khdays_menu_stepper_frame(KhdaysMenuStepper *s, uint32_t directions, int field, int command_scheme)
{
    int d = -1;
    for (int k = 0; k < KHDAYS_MENU_DIRECTIONS && d < 0; ++k) {
        if ((directions & (1u << k)) && khdays_menu_direction_exists(k, command_scheme)) {
            d = k;
        }
    }
    if (d != s->held) {
        s->held = d;
        s->frames = 0;
        return field ? d : -1;
    }
    if (d < 0) {
        return -1;
    }
    ++s->frames;
    /* held: up and down go on through the list, left and right change column once */
    if (field && (d == KHDAYS_MENU_UP || d == KHDAYS_MENU_DOWN) && s->frames >= KHDAYS_MENU_REPEAT_AFTER &&
        (s->frames - KHDAYS_MENU_REPEAT_AFTER) % KHDAYS_MENU_REPEAT_EVERY == 0) {
        return d;
    }
    return -1;
}

/* ---- Lock-on ------------------------------------------------------------- */

/* R twice within the game's 9 frames: pressed, let go, pressed, let go. */
static const Sequence r_twice = {4, {DS_R, 0, DS_R, 0}};
static const Sequence r_once = {2, {DS_R, 0, 0}};
static const Sequence l_once = {2, {DS_L, 0, 0}};

int khdays_lock_action_exists(int action, int lockon_scheme)
{
    switch (action) {
    case KHDAYS_LOCK_ON:
    case KHDAYS_LOCK_NEXT: return 1;
    case KHDAYS_LOCK_PREV: return lockon_scheme != 0; /* Type A has no previous */
    default: return 0;
    }
}

void khdays_lock_sequencer_reset(KhdaysLockSequencer *s)
{
    s->action = -1;
    s->step = 0;
    s->queued = 0;
    s->last_actions = 0;
}

static const Sequence *lock_sequence(int action)
{
    switch (action) {
    case KHDAYS_LOCK_ON: return &r_twice;
    case KHDAYS_LOCK_NEXT: return &r_once;
    default: return &l_once;
    }
}

uint16_t khdays_lock_sequencer_frame(KhdaysLockSequencer *s, uint32_t actions, int field, int locked, int lockon_scheme,
                                     uint16_t held, int r_free)
{
    const uint32_t pressed = actions & ~s->last_actions;
    uint16_t out = 0;
    s->last_actions = actions;
    if (!field) {
        /* no lock-on here (a menu, a tutorial, the pause): the buttons are
         * the DS's R and L they stand in for, as long as they are held */
        s->action = -1;
        s->step = 0;
        s->queued = 0;
        return (uint16_t)(((actions & ((1u << KHDAYS_LOCK_ON) | (1u << KHDAYS_LOCK_NEXT))) ? DS_R : 0) |
                          ((actions & (1u << KHDAYS_LOCK_PREV)) ? DS_L : 0));
    }
    for (int a = 0; a < KHDAYS_LOCK_ACTIONS; ++a) {
        if ((pressed & (1u << a)) != 0 && khdays_lock_action_exists(a, lockon_scheme) &&
            s->queued < KHDAYS_LOCK_QUEUE) {
            s->queue[s->queued++] = a;
        }
    }
    if (s->action < 0) {
        /* The next one, when the game would take it: switching target needs
         * a target locked (the game does nothing with it otherwise), and R
         * must not come within the game's double-press window. */
        while (s->queued > 0 && s->queue[0] != KHDAYS_LOCK_ON && !locked) {
            for (int i = 1; i < s->queued; ++i) {
                s->queue[i - 1] = s->queue[i];
            }
            --s->queued;
        }
        if (s->queued == 0 || (held & (DS_R | DS_L)) != 0 || (s->queue[0] != KHDAYS_LOCK_PREV && !r_free)) {
            return 0;
        }
        s->action = s->queue[0];
        s->step = 0;
        for (int i = 1; i < s->queued; ++i) {
            s->queue[i - 1] = s->queue[i];
        }
        --s->queued;
    }
    {
        const Sequence *seq = lock_sequence(s->action);
        out = seq->steps[s->step];
        ++s->step;
        if (s->step >= seq->length) {
            s->action = -1;
            s->step = 0;
        }
    }
    return out;
}
