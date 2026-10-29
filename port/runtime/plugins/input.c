/* kh.input for native plugins (sdk/include/kh/kh_input.h): the player's buttons as the game's pad read takes them
 * (runtime/input/input.c hands them over last, after the port's own screens), and the buttons plugins hide from the
 * game. */
#include "interfaces.h"
#include "plugins.h"

#include <kh/kh_input.h>

#include <stdlib.h>

unsigned khdays_input_game_frames(void); /* runtime/input/input.c */

static uint32_t held_now, pressed_now, released_now;

typedef struct Hide {
    KhPlugin *owner;
    uint32_t buttons;
} Hide;

static Hide *hides;
static int hide_count, hide_room;

unsigned khdays_plugins_hidden_buttons(void)
{
    uint32_t all = 0;
    for (int i = 0; i < hide_count; ++i) {
        if (khdays_plugins_may_change(hides[i].owner)) {
            all |= hides[i].buttons;
        }
    }
    return all;
}

unsigned khdays_plugins_buttons(unsigned buttons)
{
    pressed_now = buttons & ~held_now;
    released_now = held_now & ~buttons;
    held_now = buttons;
    return buttons & ~khdays_plugins_hidden_buttons();
}

static uint32_t held(void)
{
    return held_now;
}

static uint32_t pressed(void)
{
    return pressed_now;
}

static uint32_t released(void)
{
    return released_now;
}

static void hide(KhPlugin *plugin, uint32_t buttons)
{
    if (!khdays_plugins_may_change(plugin)) {
        buttons = 0; /* in a room, a plugin not made for rooms keeps nothing from the game */
    }
    for (int i = 0; i < hide_count; ++i) {
        if (hides[i].owner == plugin) {
            hides[i].buttons = buttons;
            return;
        }
    }
    if (buttons == 0 || plugin == NULL) {
        return;
    }
    if (hide_count == hide_room) {
        const int room = hide_room > 0 ? hide_room * 2 : 4;
        Hide *grown = realloc(hides, (size_t)room * sizeof(Hide));
        if (grown == NULL) {
            return;
        }
        hides = grown;
        hide_room = room;
    }
    hides[hide_count].owner = plugin;
    hides[hide_count].buttons = buttons;
    ++hide_count;
}

static uint32_t frame(void)
{
    return khdays_input_game_frames();
}

void khdays_plugins_input_forget(KhPlugin *plugin)
{
    for (int i = hide_count - 1; i >= 0; --i) {
        if (hides[i].owner == plugin) {
            hides[i] = hides[--hide_count];
        }
    }
}

static const KhInput input_table = {sizeof(KhInput), held, pressed, released, hide, frame};

const void *khdays_plugins_input(void)
{
    return &input_table;
}
