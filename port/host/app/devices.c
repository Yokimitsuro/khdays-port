/* The keyboard, the mouse and the gamepads (devices.h): what each device holds, turned into the DS's
 * buttons and the port's actions through the player's profile (bindings.c) and published for the game.
 * Only one gamepad plays: the first connected, or the last used when it goes away. */
#include "devices.h"
#include "instance.h"
#include "../screens/ui_text.h"

#include <stdio.h>
#include <string.h>

typedef struct Pad {
    SDL_JoystickID id;
    SDL_Gamepad *gamepad;
    Uint64 last_used; /* SDL_GetTicks() of its last button press */
} Pad;

static KhdaysProfile profile;
static KhdaysPublish publish;
static KhdaysDeviceState state;
static KhdaysInputSnapshot snapshot;
static int game_input = 1;    /* 0 while the settings menu has the input */
static int settings_request;  /* the settings button was pressed */
static int hud_mode_requests; /* presses of the HUD mode button not taken yet */
/* A notch of the wheel is a press held this long, so the game, reading its buttons once a frame of
 * its own (30 a second in the field), sees it once */
#define WHEEL_PRESS_MS 100
static Uint64 wheel_until[2]; /* SDL_GetTicks() when each wheel direction's press ends, 0 none */

/* The connected gamepads, in the order they arrived; `active` is the index
 * of the one that plays, or -1. */
static Pad *pads;
static int pad_count, pad_room;
static int active = -1;

/* Messages for the settings menu's notice line, oldest first. */
typedef char Notice[160];
static Notice *notices;
static int notice_count, notice_room;
/* Controllers already reported as unusable (so each is reported once). */
static SDL_JoystickID *ignored;
static int ignored_count, ignored_room;

/* Makes room for `need` items of `item` bytes in *items. Returns 0 if memory
 * ran out (the caller then does without). */
static int make_room(void **items, int *room, int need, size_t item)
{
    void *bigger;
    int size;
    if (need <= *room) {
        return 1;
    }
    size = *room > 0 ? *room * 2 : 8;
    while (size < need) {
        size *= 2;
    }
    bigger = SDL_realloc(*items, (size_t)size * item);
    if (bigger == NULL) {
        return 0;
    }
    *items = bigger;
    *room = size;
    return 1;
}

/* Logs a problem and keeps it for the settings menu's notice line. */
static void notice(const char *text)
{
    fprintf(stderr, "host: %s\n", text);
    if (make_room((void **)&notices, &notice_room, notice_count + 1, sizeof(Notice))) {
        SDL_strlcpy(notices[notice_count++], text, sizeof(Notice));
    }
}

int khdays_devices_take_notice(char *text, int size)
{
    if (notice_count == 0) {
        return 0;
    }
    SDL_strlcpy(text, notices[0], (size_t)size);
    memmove(notices[0], notices[1], sizeof(Notice) * (size_t)(notice_count - 1));
    --notice_count;
    return 1;
}

/* Works out the snapshot from what is held, and publishes it for the game. */
static void send(void)
{
    snapshot.ui_open = 0;
    khdays_bindings_resolve(&profile, &state, &snapshot);
    if (!game_input) {
        /* The settings menu has the input: the game sees nothing held. */
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.ui_open = 1;
    }
    if (publish != NULL) {
        publish(&snapshot);
    }
}

void khdays_devices_init(const KhdaysProfile *p, KhdaysPublish to)
{
    profile = *p;
    khdays_hud_settings_publish(p->enh.hud);
    publish = to;
    khdays_bindings_reset(&state);
    send();
}

void khdays_devices_set_profile(const KhdaysProfile *p)
{
    profile = *p;
    khdays_hud_settings_publish(p->enh.hud);
    send();
}

const KhdaysProfile *khdays_devices_profile(void)
{
    return &profile;
}

const KhdaysDeviceState *khdays_devices_state(void)
{
    return &state;
}

void khdays_devices_set_game_input(int on)
{
    if (on && !game_input) {
        khdays_bindings_mask_held(&state);
    }
    game_input = on;
    send();
}

int khdays_devices_take_hud_mode_request(void)
{
    const int requests = hud_mode_requests;
    hud_mode_requests = 0;
    return requests;
}

int khdays_devices_take_settings_request(void)
{
    const int request = settings_request;
    settings_request = 0;
    return request;
}

const char *khdays_devices_gamepad_name(void)
{
    return active >= 0 ? SDL_GetGamepadName(pads[active].gamepad) : NULL;
}

/* Our input number for one of SDL's gamepad buttons (both are named by
 * position), or -1. The paddles follow the Xbox Elite's P1-P4: upper right,
 * lower right, upper left, lower left. */
static int pad_input(SDL_GamepadButton button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return KHDAYS_PAD_SOUTH;
    case SDL_GAMEPAD_BUTTON_EAST: return KHDAYS_PAD_EAST;
    case SDL_GAMEPAD_BUTTON_WEST: return KHDAYS_PAD_WEST;
    case SDL_GAMEPAD_BUTTON_NORTH: return KHDAYS_PAD_NORTH;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return KHDAYS_PAD_DPUP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return KHDAYS_PAD_DPDOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return KHDAYS_PAD_DPLEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return KHDAYS_PAD_DPRIGHT;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return KHDAYS_PAD_LEFTSHOULDER;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return KHDAYS_PAD_RIGHTSHOULDER;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return KHDAYS_PAD_LEFTSTICK;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return KHDAYS_PAD_RIGHTSTICK;
    case SDL_GAMEPAD_BUTTON_START: return KHDAYS_PAD_START;
    case SDL_GAMEPAD_BUTTON_BACK: return KHDAYS_PAD_BACK;
    case SDL_GAMEPAD_BUTTON_GUIDE: return KHDAYS_PAD_GUIDE;
    case SDL_GAMEPAD_BUTTON_MISC1: return KHDAYS_PAD_MISC1;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return KHDAYS_PAD_TOUCHPAD;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return KHDAYS_PAD_PADDLE1;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return KHDAYS_PAD_PADDLE2;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return KHDAYS_PAD_PADDLE3;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return KHDAYS_PAD_PADDLE4;
    default: return -1;
    }
}

SDL_Gamepad *khdays_devices_gamepad(void)
{
    return active >= 0 ? pads[active].gamepad : NULL;
}

/* SDL's button for one of our input numbers (the reverse of pad_input). */
SDL_GamepadButton khdays_devices_sdl_button(int input)
{
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        if (pad_input((SDL_GamepadButton)b) == input) {
            return (SDL_GamepadButton)b;
        }
    }
    return SDL_GAMEPAD_BUTTON_INVALID;
}

/* Forgets everything the active gamepad held. */
static void clear_pad_state(void)
{
    memset(state.pad, 0, sizeof(state.pad));
    memset(state.pad_masked, 0, sizeof(state.pad_masked));
    memset(state.trigger, 0, sizeof(state.trigger));
    memset(state.stick, 0, sizeof(state.stick));
    memset(state.stick_masked, 0, sizeof(state.stick_masked));
}

/* Reads the active gamepad as it is now (when the window gets the focus back). */
static void read_active_pad(void)
{
    clear_pad_state();
    if (active < 0) {
        return;
    }
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        const int input = pad_input((SDL_GamepadButton)b);
        if (input >= 0) {
            state.pad[input] = SDL_GetGamepadButton(pads[active].gamepad, (SDL_GamepadButton)b);
        }
    }
    state.stick[0][0] = SDL_GetGamepadAxis(pads[active].gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    state.stick[0][1] = SDL_GetGamepadAxis(pads[active].gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    state.stick[1][0] = SDL_GetGamepadAxis(pads[active].gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    state.stick[1][1] = SDL_GetGamepadAxis(pads[active].gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    state.trigger[0] = SDL_GetGamepadAxis(pads[active].gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    state.trigger[1] = SDL_GetGamepadAxis(pads[active].gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
}

/* Which connected gamepad this copy plays with: the first (0), or for player
 * N's copy on the same PC the Nth one (N-1; host/app/instance.h). */
static int own_pad(void)
{
    const int slot = khdays_host_player_slot_taken();
    return slot > 1 ? slot - 1 : 0;
}

/* The index of gamepad `id` in `pads`, or -1. */
static int find_pad(SDL_JoystickID id)
{
    for (int i = 0; i < pad_count; ++i) {
        if (pads[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void gamepad_added(SDL_JoystickID id)
{
    SDL_Gamepad *gamepad;
    if (find_pad(id) >= 0 || !make_room((void **)&pads, &pad_room, pad_count + 1, sizeof(Pad))) {
        return;
    }
    gamepad = SDL_OpenGamepad(id);
    if (gamepad == NULL) {
        char text[160];
        SDL_snprintf(text, sizeof(text), khdays_text(TXT_GAMEPAD_NOT_OPENED), SDL_GetError());
        notice(text);
        return;
    }
    pads[pad_count] = (Pad){id, gamepad, 0};
    if (active < 0 && pad_count >= own_pad()) {
        active = pad_count; /* the first one connected plays (for player N's copy, the Nth) */
        clear_pad_state();
    }
    ++pad_count;
    fprintf(stderr, "host: gamepad connected: %s%s\n", SDL_GetGamepadName(gamepad),
            active == pad_count - 1 ? " (playing)" : "");
}

static void gamepad_removed(SDL_JoystickID id)
{
    const int i = find_pad(id);
    int next = -1;
    if (i < 0) {
        return;
    }
    fprintf(stderr, "host: gamepad disconnected: %s\n", SDL_GetGamepadName(pads[i].gamepad));
    SDL_CloseGamepad(pads[i].gamepad);
    memmove(&pads[i], &pads[i + 1], sizeof(Pad) * (size_t)(pad_count - i - 1));
    --pad_count;
    if (active == i) {
        /* Release everything it held. From now on the most recently used of
         * the others plays, if any -- or, for another player's copy on the
         * same PC, the Nth one still connected (the others belong to the other
         * players). */
        clear_pad_state();
        if (own_pad() > 0) {
            next = pad_count > own_pad() ? own_pad() : -1;
        } else {
            for (int p = 0; p < pad_count; ++p) {
                if (next < 0 || pads[p].last_used > pads[next].last_used) {
                    next = p;
                }
            }
        }
        active = next;
        if (active >= 0) {
            fprintf(stderr, "host: now playing: %s\n", SDL_GetGamepadName(pads[active].gamepad));
        }
    } else if (active > i) {
        --active;
    }
}

/* A controller SDL does not see as a gamepad: tell the player once. */
static void joystick_added(SDL_JoystickID id)
{
    char text[160];
    if (SDL_IsGamepad(id)) {
        return; /* it arrives as a gamepad too */
    }
    for (int i = 0; i < ignored_count; ++i) {
        if (ignored[i] == id) {
            return; /* already reported */
        }
    }
    if (make_room((void **)&ignored, &ignored_room, ignored_count + 1, sizeof(SDL_JoystickID))) {
        ignored[ignored_count++] = id;
    }
    SDL_snprintf(text, sizeof(text), khdays_text(TXT_NOT_A_GAMEPAD),
                 SDL_GetJoystickNameForID(id) ? SDL_GetJoystickNameForID(id) : khdays_text(TXT_A_CONTROLLER));
    notice(text);
}

/* The gamepad used since the keyboard or the mouse last were (khdays_devices_pad_in_use). */
static int pad_in_use;

int khdays_devices_pad_in_use(void)
{
    return pad_in_use && khdays_devices_gamepad() != NULL;
}

static void pad_button(SDL_JoystickID id, SDL_GamepadButton button, int down)
{
    const int i = find_pad(id);
    const int input = pad_input(button);
    if (i < 0 || input < 0) {
        return;
    }
    if (down) {
        pads[i].last_used = SDL_GetTicks();
    }
    if (i != active) {
        return; /* only one gamepad plays */
    }
    pad_in_use = pad_in_use || down;
    state.pad[input] = (uint8_t)down;
    if (down &&
        khdays_profile_owner(&profile, KHDAYS_DEVICE_GAMEPAD, KHDAYS_LAYER_BASE, input) == KHDAYS_ACT_OPEN_SETTINGS) {
        settings_request = 1;
    }
    if (down &&
        khdays_profile_owner(&profile, KHDAYS_DEVICE_GAMEPAD, KHDAYS_LAYER_BASE, input) == KHDAYS_ACT_HUD_MODE) {
        ++hud_mode_requests;
    }
}

static void pad_axis(SDL_JoystickID id, SDL_GamepadAxis axis, int16_t value)
{
    if (find_pad(id) != active) {
        return;
    }
    if (value > 16384 || value < -16384) {
        pad_in_use = 1; /* past half its travel: the stick or trigger is being used */
    }
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: state.stick[0][0] = value; break;
    case SDL_GAMEPAD_AXIS_LEFTY: state.stick[0][1] = value; break;
    case SDL_GAMEPAD_AXIS_RIGHTX: state.stick[1][0] = value; break;
    case SDL_GAMEPAD_AXIS_RIGHTY: state.stick[1][1] = value; break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: state.trigger[0] = value; break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: state.trigger[1] = value; break;
    default: break;
    }
}

static void read_keyboard(void)
{
    int count = 0;
    const bool *keys = SDL_GetKeyboardState(&count);
    for (int k = 0; k < KHDAYS_KEY_COUNT; ++k) {
        state.key[k] = (uint8_t)(k < count && keys[k]);
    }
}

/* A mouse input pressed: its HUD mode request, if it is that button's */
static void mouse_pressed(int mouse)
{
    pad_in_use = 0;
    if (khdays_profile_owner(&profile, KHDAYS_DEVICE_KEYBOARD, KHDAYS_LAYER_BASE, KHDAYS_MOUSE_INPUT + mouse) ==
        KHDAYS_ACT_HUD_MODE) {
        ++hud_mode_requests;
    }
}

void khdays_devices_frame(void)
{
    const Uint64 now = SDL_GetTicks();
    int changed = 0;
    for (int w = 0; w < 2; ++w) {
        if (wheel_until[w] != 0 && now >= wheel_until[w]) {
            wheel_until[w] = 0;
            state.mouse[KHDAYS_MOUSE_WHEEL_UP + w] = 0;
            changed = 1;
        }
    }
    if (changed) {
        send();
    }
}

void khdays_devices_handle(const SDL_Event *event, SDL_Renderer *renderer)
{
    (void)renderer;
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        pad_in_use = event->type == SDL_EVENT_KEY_DOWN ? 0 : pad_in_use;
        read_keyboard();
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
            khdays_profile_owner(&profile, KHDAYS_DEVICE_KEYBOARD, KHDAYS_LAYER_BASE, (int)event->key.scancode) ==
                KHDAYS_ACT_HUD_MODE) {
            ++hud_mode_requests;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const int mouse = event->button.button - SDL_BUTTON_LEFT + KHDAYS_MOUSE_LEFT; /* SDL's 1-5 */
        if (mouse < KHDAYS_MOUSE_LEFT || mouse > KHDAYS_MOUSE_X2) {
            return;
        }
        state.mouse[mouse] = (uint8_t)(event->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        if (state.mouse[mouse]) {
            mouse_pressed(mouse);
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        const float y = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        const int w = y > 0.0f ? 0 : 1; /* up, down */
        if (y == 0.0f) {
            return;
        }
        state.mouse[KHDAYS_MOUSE_WHEEL_UP + w] = 1;
        wheel_until[w] = SDL_GetTicks() + WHEEL_PRESS_MS;
        mouse_pressed(KHDAYS_MOUSE_WHEEL_UP + w);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        /* the mouse camera: its motion summed, right and up positive, as the camera stick's (with
         * the camera's inversions); taken only while the window holds the mouse (window.c) */
        if (!SDL_GetWindowRelativeMouseMode(SDL_GetWindowFromID(event->motion.windowID))) {
            return;
        }
        state.mouse_x += profile.enh.camera_invert_x ? -event->motion.xrel : event->motion.xrel;
        state.mouse_y += profile.enh.camera_invert_y ? event->motion.yrel : -event->motion.yrel;
        break;
    case SDL_EVENT_GAMEPAD_ADDED: gamepad_added(event->gdevice.which); break;
    case SDL_EVENT_GAMEPAD_REMOVED: gamepad_removed(event->gdevice.which); break;
    case SDL_EVENT_JOYSTICK_ADDED: joystick_added(event->jdevice.which); return;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        pad_button(event->gbutton.which, (SDL_GamepadButton)event->gbutton.button,
                   event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        pad_axis(event->gaxis.which, (SDL_GamepadAxis)event->gaxis.axis, event->gaxis.value);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        /* With another copy of the port on the same PC, this
         * copy's player keeps playing with their gamepad while the other
         * window has the focus. */
        if (khdays_host_other_copies()) {
            SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        }
        /* SDL sends no gamepad events to an unfocused window, so a button
         * released meanwhile would stay held: release everything now. */
        memset(state.key, 0, sizeof(state.key));
        memset(state.mouse, 0, sizeof(state.mouse));
        wheel_until[0] = wheel_until[1] = 0;
        clear_pad_state();
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        /* What is held as the window comes back only counts once released. */
        read_keyboard();
        read_active_pad();
        khdays_bindings_mask_held(&state);
        break;
    default: return;
    }
    send();
}
