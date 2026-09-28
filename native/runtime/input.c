#include "input.h"
#include "runtime.h"
#include "../host/host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What a step's frame counts from: boot, or the first frame of a scene (sN)
 * or of an object update (uName). */
typedef struct Anchor {
    char type;   /* 's' or 'u' */
    u32 value;   /* the scene id, or the update's address */
    u32 start;   /* the frame it began + 1; 0 = not yet */
} Anchor;

typedef struct Step {
    u32 frame;
    int anchor; /* -1: from boot */
    char kind;  /* '+', '-' or '@' */
    u16 keys;
    int x, y;   /* '@': -1 lifts the stylus */
} Step;

static Step *script;
static int script_len;
static int script_next;
static u16 script_keys;
static int script_touch_x = -1, script_touch_y;

#define MAX_ANCHORS 64
static Anchor anchors[MAX_ANCHORS];
static int anchor_count;
static u32 last_frame;

/* The scene controller at 0x0204bda8, its current id at +8
 * (src/calls/func_0202099c.c). */
#define SCENE_CURRENT (*(volatile u32 *)(0x0204bda8 + 8))

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

static int add_anchor(const char *item)
{
    Anchor *a = &anchors[anchor_count];
    char *end;
    if (anchor_count == MAX_ANCHORS) {
        bad_script(item);
    }
    a->type = item[0];
    a->start = 0;
    if (a->type == 's') {
        a->value = (u32)strtoul(item + 1, &end, 10);
        if (*end != '\0') {
            bad_script(item);
        }
    } else {
        a->value = khdays_diag_symbol(item + 1);
        if (a->value == 0) {
            fprintf(stderr, "KHDAYS_INPUT: no function named %s\n", item + 1);
            exit(2);
        }
    }
    return anchor_count++;
}

void khdays_input_init(void)
{
    const char *text = getenv("KHDAYS_INPUT");
    char *copy, *item, *rest;
    int anchor = -1;
    if (text == NULL) {
        return;
    }
    copy = _strdup(text);
    script = (Step *)calloc(strlen(text) / 2 + 1, sizeof(Step));
    for (item = strtok_s(copy, " ", &rest); item != NULL; item = strtok_s(NULL, " ", &rest)) {
        Step step;
        char *end;
        if (item[0] == 's' || item[0] == 'u') {
            anchor = add_anchor(item);
            continue;
        }
        memset(&step, 0, sizeof(step));
        step.anchor = anchor;
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

void khdays_input_update_ran(u32 update)
{
    for (int i = 0; i < anchor_count; ++i) {
        if (anchors[i].type == 'u' && anchors[i].value == update && anchors[i].start == 0) {
            anchors[i].start = last_frame + 1;
        }
    }
}

void khdays_input_frame(u32 frame)
{
    const u32 scene = SCENE_CURRENT;
    last_frame = frame;
    for (int i = 0; i < anchor_count; ++i) {
        if (anchors[i].type == 's' && anchors[i].value == scene && anchors[i].start == 0) {
            anchors[i].start = frame + 1;
        }
    }
    while (script_next < script_len) {
        const Step *s = &script[script_next];
        if (s->anchor < 0 ? s->frame > frame
                          : anchors[s->anchor].start == 0 ||
                                anchors[s->anchor].start - 1 + s->frame > frame) {
            break;
        }
        ++script_next;
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
