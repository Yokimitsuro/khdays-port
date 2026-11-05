/* The command menu on the gamepad's D-pad (sequencers.h). */
#include "sequencers.h"

#include <stddef.h>

/* The DS buttons the sequences press (runtime/input/input.h has them all). */
#define DS_RIGHT (1u << 4)
#define DS_LEFT (1u << 5)
#define DS_UP (1u << 6)
#define DS_DOWN (1u << 7)
#define DS_R (1u << 8)
#define DS_L (1u << 9)
#define DS_X (1u << 10)
#define DS_DPAD (DS_RIGHT | DS_LEFT | DS_UP | DS_DOWN)

/* A step is a frame's buttons; each sequence ends with a frame of nothing,
 * so X is seen let go before the next sequence presses it again. */
#define MAX_STEPS 4

typedef struct Sequence {
    int length;
    uint16_t steps[MAX_STEPS];
} Sequence;

static const uint16_t direction_bit[KHDAYS_MENU_DIRECTIONS] = {DS_UP, DS_DOWN, DS_LEFT, DS_RIGHT};

/* Option 6 = 0 (Ov022_UpdateCommandInput): X pressed alone arms the menu
 * (with no direction held), then a direction pressed with X held moves it.
 * Let go, X does nothing more: moving cleared its "pressed alone" mark. */
static Sequence x_and_direction(int direction)
{
    Sequence s = {3, {DS_X, 0, 0}};
    s.steps[1] = (uint16_t)(DS_X | direction_bit[direction]);
    return s;
}

/* Option 6 = 1: a press of X moves the cursor down. */
static const Sequence x_press = {2, {DS_X, 0, 0}};

int khdays_menu_direction_exists(int direction, int command_scheme)
{
    if (direction < 0 || direction >= KHDAYS_MENU_DIRECTIONS) {
        return 0;
    }
    return command_scheme == 0 || direction == KHDAYS_MENU_DOWN;
}

static Sequence sequence_for(int direction, int command_scheme)
{
    const Sequence none = {0, {0, 0, 0}};
    if (!khdays_menu_direction_exists(direction, command_scheme)) {
        return none;
    }
    return command_scheme == 0 ? x_and_direction(direction) : x_press;
}

void khdays_menu_sequencer_reset(KhdaysMenuSequencer *s)
{
    s->direction = -1;
    s->step = 0;
    s->queued = -1;
    s->last_actions = 0;
}

/* The player holds X, or the buttons that turn X into a shortcut. */
static int x_taken(uint16_t held, int lockon_scheme)
{
    if (held & DS_X) {
        return 1;
    }
    if (lockon_scheme == 0) {
        return (held & DS_L) != 0;
    }
    return (held & (DS_L | DS_R)) == (DS_L | DS_R);
}

KhdaysMenuFrame khdays_menu_sequencer_frame(KhdaysMenuSequencer *s, uint32_t directions, int field, int command_scheme,
                                            int lockon_scheme, uint16_t held)
{
    KhdaysMenuFrame out = {0, 0};
    const uint32_t pressed = directions & ~s->last_actions;
    s->last_actions = directions;

    if (!field) {
        s->direction = s->queued = -1;
        s->step = 0;
        return out;
    }
    /* New presses: the first plays, one more waits its turn. */
    for (int d = 0; d < KHDAYS_MENU_DIRECTIONS; ++d) {
        if ((pressed & (1u << d)) == 0 || !khdays_menu_direction_exists(d, command_scheme)) {
            continue;
        }
        if (s->direction < 0) {
            s->direction = d;
            s->step = 0;
        } else if (s->queued < 0) {
            s->queued = d;
        }
    }
    if (s->direction < 0) {
        return out;
    }
    /* A sequence starts only when X is free; once started it runs out. */
    if (s->step == 0 && x_taken(held, lockon_scheme)) {
        return out;
    }
    {
        const Sequence seq = sequence_for(s->direction, command_scheme);
        if (s->step < seq.length) {
            out.buttons = seq.steps[s->step];
            /* The sequence's own D-pad, not the player's: arming wants no
             * direction held, a step wants exactly its own. */
            out.dpad_own = (seq.steps[s->step] & DS_X) != 0;
            ++s->step;
        }
        if (s->step >= seq.length) {
            s->direction = s->queued;
            s->queued = -1;
            s->step = 0;
        }
    }
    return out;
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
    s->let_go = 0;
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
    s->let_go = 0;
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
        /* Letting go: one R the game takes as a double press. The first of two would move to the next target. */
        if (s->action == KHDAYS_LOCK_ON && locked) {
            s->action = KHDAYS_LOCK_NEXT;
            s->let_go = 1;
        }
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
