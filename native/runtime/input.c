#include "input.h"
#include "../host/host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Step {
    u32 frame;
    char kind;  /* '+', '-' or '@' */
    u16 keys;
    int x, y;   /* '@': -1 lifts the stylus */
} Step;

static Step *script;
static int script_len;
static int script_next;
static u16 script_keys;
static int script_touch_x = -1, script_touch_y;

static const struct {
    const char *name;
    u16 bit;
} key_names[] = {
    {"A", KHDAYS_KEY_A},         {"B", KHDAYS_KEY_B},       {"SELECT", KHDAYS_KEY_SELECT},
    {"START", KHDAYS_KEY_START}, {"RIGHT", KHDAYS_KEY_RIGHT}, {"LEFT", KHDAYS_KEY_LEFT},
    {"UP", KHDAYS_KEY_UP},       {"DOWN", KHDAYS_KEY_DOWN}, {"R", KHDAYS_KEY_R},
    {"L", KHDAYS_KEY_L},         {"X", KHDAYS_KEY_X},       {"Y", KHDAYS_KEY_Y},
};

static void bad_script(const char *item)
{
    fprintf(stderr, "KHDAYS_INPUT: cannot read \"%s\"\n", item);
    exit(2);
}

void khdays_input_init(void)
{
    const char *text = getenv("KHDAYS_INPUT");
    char *copy, *item, *rest;
    if (text == NULL) {
        return;
    }
    copy = _strdup(text);
    script = (Step *)calloc(strlen(text) / 2 + 1, sizeof(Step));
    for (item = strtok_s(copy, " ", &rest); item != NULL; item = strtok_s(NULL, " ", &rest)) {
        Step step;
        char *end;
        memset(&step, 0, sizeof(step));
        step.frame = strtoul(item, &end, 10);
        step.kind = *end;
        if (step.kind == '@') {
            if (strcmp(end + 1, "-") == 0) {
                step.x = -1;
            } else if (sscanf_s(end + 1, "%d,%d", &step.x, &step.y) != 2) {
                bad_script(item);
            }
        } else if (step.kind == '+' || step.kind == '-') {
            for (size_t n = 0; n < sizeof(key_names) / sizeof(key_names[0]); ++n) {
                if (_stricmp(end + 1, key_names[n].name) == 0) {
                    step.keys = key_names[n].bit;
                }
            }
            if (step.keys == 0) {
                bad_script(item);
            }
        } else {
            bad_script(item);
        }
        script[script_len++] = step;
    }
    free(copy);
}

void khdays_input_frame(u32 frame)
{
    while (script_next < script_len && script[script_next].frame <= frame) {
        const Step *s = &script[script_next++];
        if (s->kind == '+') {
            script_keys |= s->keys;
        } else if (s->kind == '-') {
            script_keys &= (u16)~s->keys;
        } else {
            script_touch_x = s->x;
            script_touch_y = s->y;
        }
    }
}

static u16 pressed(void)
{
    return (u16)(script_keys | khdays_host_buttons());
}

u16 khdays_input_keyinput(void)
{
    return (u16)(~pressed() & 0x03ff);
}

u16 khdays_input_xy(void)
{
    return (u16)(0x2c00 & ~(pressed() & (KHDAYS_KEY_X | KHDAYS_KEY_Y)));
}

int khdays_input_touch(int *x, int *y)
{
    if (script_touch_x >= 0) {
        *x = script_touch_x;
        *y = script_touch_y;
        return 1;
    }
    return khdays_host_touch(x, y);
}
