/* Turns what is held on the keyboard and the gamepad into what the game
 * should see -- the DS buttons and the port's own actions -- following the
 * player's profile (see bindings.h). */
#include "bindings.h"

#include <math.h>
#include <string.h>

/* DS button n is bit n of the word the game reads (profile.h). */
#define DS_BIT(action) ((uint16_t)(1u << (action)))

/* A trigger counts as a pressed button past half its travel. */
#define TRIGGER_PRESSED 16384

void khdays_bindings_reset(KhdaysDeviceState *s)
{
    memset(s, 0, sizeof(*s));
    s->sector[0] = s->sector[1] = -1;
}

static int trigger_held(const KhdaysDeviceState *s, int input)
{
    return s->trigger[input == KHDAYS_PAD_LEFTTRIGGER ? 0 : 1] > TRIGGER_PRESSED;
}

static int pad_held(const KhdaysDeviceState *s, int input)
{
    if (input == KHDAYS_PAD_LEFTTRIGGER || input == KHDAYS_PAD_RIGHTTRIGGER) {
        return trigger_held(s, input);
    }
    return s->pad[input];
}

void khdays_bindings_mask_held(KhdaysDeviceState *s)
{
    for (int k = 0; k < KHDAYS_KEY_COUNT; ++k) {
        s->key_masked[k] = s->key[k];
    }
    for (int m = 0; m < KHDAYS_MOUSE_COUNT; ++m) {
        s->mouse_masked[m] = s->mouse[m];
    }
    for (int i = 0; i < KHDAYS_PAD_COUNT; ++i) {
        s->pad_masked[i] = (uint8_t)pad_held(s, i);
    }
    for (int i = 0; i < 2; ++i) {
        /* A stick counts as held when it is off centre by more than a tenth
         * of its travel; it is let go once back inside its dead zone. */
        const int x = s->stick[i][0], y = s->stick[i][1];
        s->stick_masked[i] = (uint8_t)(x * x + y * y > 3277 * 3277);
    }
}

/* Whether an input counts as pressed: held, and not masked (waiting to be
 * released). Releasing a masked input removes its mask. */
static int key_counts(KhdaysDeviceState *s, int key)
{
    if (key >= KHDAYS_MOUSE_INPUT) {
        const int m = key - KHDAYS_MOUSE_INPUT;
        if (m >= KHDAYS_MOUSE_COUNT || !s->mouse[m]) {
            if (m < KHDAYS_MOUSE_COUNT) {
                s->mouse_masked[m] = 0;
            }
            return 0;
        }
        return !s->mouse_masked[m];
    }
    if (key >= KHDAYS_KEY_COUNT) {
        return 0;
    }
    if (!s->key[key]) {
        s->key_masked[key] = 0;
        return 0;
    }
    return !s->key_masked[key];
}

static int pad_counts(KhdaysDeviceState *s, int input)
{
    if (!pad_held(s, input)) {
        s->pad_masked[input] = 0;
        return 0;
    }
    return !s->pad_masked[input];
}

static int input_counts(KhdaysDeviceState *s, int device, int input)
{
    if (input == KHDAYS_NO_INPUT) {
        return 0;
    }
    return device == KHDAYS_DEVICE_KEYBOARD ? key_counts(s, input) : pad_counts(s, input);
}

/* How far a stick is pushed, in percent of its travel (can pass 100 in the
 * corners of a square gate). */
static double stick_percent(int x, int y)
{
    return sqrt((double)x * x + (double)y * y) * 100.0 / 32767.0;
}

/* The stick's angle in degrees, 0 = right, counterclockwise (up = 90). SDL's
 * y grows downwards, so it is negated. */
static double stick_degrees(int x, int y)
{
    double deg = atan2(-(double)y, (double)x) * 180.0 / 3.14159265358979323846;
    return deg < 0 ? deg + 360.0 : deg;
}

/* The D-pad bits of each of the eight directions, counterclockwise from right. */
static const uint16_t sector_bits[8] = {
    DS_BIT(KHDAYS_ACT_RIGHT), DS_BIT(KHDAYS_ACT_RIGHT) | DS_BIT(KHDAYS_ACT_UP),
    DS_BIT(KHDAYS_ACT_UP),    DS_BIT(KHDAYS_ACT_UP) | DS_BIT(KHDAYS_ACT_LEFT),
    DS_BIT(KHDAYS_ACT_LEFT),  DS_BIT(KHDAYS_ACT_LEFT) | DS_BIT(KHDAYS_ACT_DOWN),
    DS_BIT(KHDAYS_ACT_DOWN),  DS_BIT(KHDAYS_ACT_DOWN) | DS_BIT(KHDAYS_ACT_RIGHT),
};

uint16_t khdays_stick_dpad(int x, int y, const KhdaysStick *stick, int *active, int *sector)
{
    const double pushed = stick_percent(x, y);
    /* Past the dead zone to start moving; back below it, less the
     * hysteresis, to stop, so a stick resting on the edge does not flicker. */
    if (!*active && pushed >= stick->dead_zone) {
        *active = 1;
    } else if (*active && pushed < stick->dead_zone - stick->hysteresis) {
        *active = 0;
    }
    if (!*active) {
        *sector = -1;
        return 0;
    }
    {
        const double deg = stick_degrees(x, y);
        /* Each direction owns 45 degrees; the current one keeps the stick a
         * little past its edge (the hysteresis, in percent of those 45). */
        if (*sector >= 0) {
            double off = fabs(deg - *sector * 45.0);
            if (off > 180.0) {
                off = 360.0 - off;
            }
            if (off <= 22.5 + stick->hysteresis * 45.0 / 100.0) {
                return sector_bits[*sector];
            }
        }
        *sector = (int)floor((deg + 22.5) / 45.0) % 8;
        return sector_bits[*sector];
    }
}

/* A stick's position with y growing upwards, or 0 inside `dead_zone`.
 * With `rescale`, the edge of the dead zone becomes 0 and full travel 32767
 * (smooth for the camera); without it the position is the stick's own, so
 * its tilt can be compared with the run threshold (analog movement). */
static void stick_axes(int x, int y, int dead_zone, int rescale, int16_t *out_x, int16_t *out_y)
{
    const double pushed = stick_percent(x, y);
    double scale = 1.0;
    if (pushed < dead_zone || pushed == 0) {
        *out_x = *out_y = 0;
        return;
    }
    if (rescale) {
        const double reach = pushed > 100 ? 100 : pushed; /* a square gate's corners pass 100 */
        scale = (reach - dead_zone) / (100.0 - dead_zone) * 100.0 / pushed;
    }
    *out_x = (int16_t)fmax(-32767, fmin(32767, x * scale));
    *out_y = (int16_t)fmax(-32767, fmin(32767, -y * scale));
}

void khdays_bindings_resolve(const KhdaysProfile *p, KhdaysDeviceState *s, KhdaysInputSnapshot *out)
{
    const int field_on = p->enh.dpad_command_menu;
    uint16_t base = 0, field = 0;
    uint32_t actions = 0;
    const uint8_t ui_open = out->ui_open;

    memset(out, 0, sizeof(*out));
    out->ui_open = ui_open;

    for (int d = 0; d < KHDAYS_DEVICE_COUNT; ++d) {
        for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
            for (int slot = 0; slot < KHDAYS_INPUTS_PER_ACTION; ++slot) {
                const int input = p->input[d][KHDAYS_LAYER_BASE][a][slot];
                if (!input_counts(s, d, input)) {
                    continue;
                }
                if (a < KHDAYS_ACT_DS_COUNT) {
                    base |= DS_BIT(a);
                    /* In field control a `field` binding of the same input
                     * wins over this one. */
                    if (!field_on || khdays_profile_owner(p, d, KHDAYS_LAYER_FIELD, input) < 0) {
                        field |= DS_BIT(a);
                    }
                } else if (a >= KHDAYS_ACT_LOCK_ON && a <= KHDAYS_ACT_TARGET_PREV && p->enh.separate_lock_on) {
                    actions |= 1u << (a - KHDAYS_ACT_LOCK_ON);
                }
            }
            for (int slot = 0; field_on && slot < KHDAYS_INPUTS_PER_ACTION; ++slot) {
                const int input = p->input[d][KHDAYS_LAYER_FIELD][a][slot];
                if (a >= KHDAYS_ACT_MENU_UP && a <= KHDAYS_ACT_MENU_RIGHT && input_counts(s, d, input)) {
                    actions |= 1u << (a - KHDAYS_ACT_LOCK_ON);
                }
            }
        }
    }

    for (int i = 0; i < 2; ++i) {
        const int x = s->stick[i][0], y = s->stick[i][1];
        const KhdaysStick *stick = &p->stick[i];
        uint16_t bits;
        if (stick->role != KHDAYS_STICK_DPAD) {
            s->stick_active[i] = 0;
            s->sector[i] = -1;
            continue;
        }
        bits = khdays_stick_dpad(x, y, stick, &s->stick_active[i], &s->sector[i]);
        if (!s->stick_active[i]) {
            s->stick_masked[i] = 0; /* back at rest: no longer waiting to be let go */
        }
        if (s->stick_masked[i]) {
            continue;
        }
        base |= bits;
        field |= bits;
        /* Analog movement: the stick's own position whenever it gives a
         * direction (so, with the D-pad's dead zone and hysteresis), the
         * exact angle the D-pad bits round to one of eight. */
        if (p->enh.analog_movement && bits != 0 && out->move_x == 0 && out->move_y == 0) {
            stick_axes(x, y, 0, 0, &out->move_x, &out->move_y);
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (p->stick[i].role == KHDAYS_STICK_CAMERA && p->enh.analog_camera && out->camera_x == 0 &&
            out->camera_y == 0) {
            stick_axes(s->stick[i][0], s->stick[i][1], p->stick[i].dead_zone, 1, &out->camera_x, &out->camera_y);
            if (p->enh.camera_invert_x) {
                out->camera_x = (int16_t)-out->camera_x;
            }
            if (p->enh.camera_invert_y) {
                out->camera_y = (int16_t)-out->camera_y;
            }
        }
    }

    out->ds_buttons = base;
    out->ds_buttons_field = field;
    out->actions = actions;
    /* the mouse's motion as whole counts; the game takes what changed since it last looked */
    out->mouse_x = (int32_t)floor(s->mouse_x);
    out->mouse_y = (int32_t)floor(s->mouse_y);
    out->mouse_speed = (uint16_t)p->enh.mouse_speed;
    for (int i = 0; i < 2; ++i) {
        out->sticks[i][0] = s->stick[i][0];
        out->sticks[i][1] = s->stick[i][1];
    }
    out->enhancements = (uint16_t)((p->enh.analog_camera ? KHDAYS_ENH_ANALOG_CAMERA : 0) |
                                   (p->enh.mouse_camera ? KHDAYS_ENH_MOUSE_CAMERA : 0) |
                                   (p->enh.separate_lock_on ? KHDAYS_ENH_SEPARATE_LOCK_ON : 0) |
                                   (p->enh.dpad_command_menu ? KHDAYS_ENH_DPAD_COMMAND_MENU : 0) |
                                   (p->enh.analog_movement ? KHDAYS_ENH_ANALOG_MOVEMENT : 0));
    out->camera_speed = (uint16_t)p->enh.camera_speed;
    out->run_threshold = (uint8_t)p->enh.run_threshold;
    out->walk_speed = (uint8_t)p->enh.walk_speed;
}
