/* Tests for the input layer: the control profile and its presets, the bindings, the devices (SDL's
 * virtual gamepads, no window), the settings menu, and the enhancements' pure parts. Each failing CHECK
 * prints where and what; the program returns non-zero if any did. */
#include "../host/app/bindings.h"
#include "../host/app/devices.h"
#include "../host/app/profile.h"
#include "../host/settings/settings_model.h"
#include "../host/settings/settings_ui.h"
#include "../host/screens/ui_font.h"
#include "../host/screens/ui_text.h"
#include "../runtime/input/analog.h"
#include "../runtime/input/sequencers.h"
#include "../runtime/scripts/queue.h"
#include "../runtime/scripts/view.h"
#include "../runtime/plugins/plugins.h"
#include "../runtime/plugins/shown.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

/* The mods (runtime/core/rom.cpp): none here, so the settings menu draws its own look (settings_skin.h). */
int khdays_mods_find(const char *relative, char *path, int size)
{
    (void)relative;
    (void)path;
    (void)size;
    return 0;
}

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition);                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

#define DS(action) ((uint16_t)(1u << (action)))
enum { KB = KHDAYS_DEVICE_KEYBOARD, PAD = KHDAYS_DEVICE_GAMEPAD, BASE = KHDAYS_LAYER_BASE };

static int first_input(const KhdaysProfile *p, int device, int action)
{
    return p->input[device][BASE][action][0];
}

/* The keyboard and the mouse as PC games have them; the gamepad as the
 * DS, by position. */
static void test_default_profile(void)
{
    const int mouse = KHDAYS_MOUSE_INPUT;
    KhdaysProfile p;
    khdays_profile_default(&p);
    CHECK(first_input(&p, KB, KHDAYS_ACT_UP) == SDL_SCANCODE_W);
    CHECK(first_input(&p, KB, KHDAYS_ACT_DOWN) == SDL_SCANCODE_S);
    CHECK(first_input(&p, KB, KHDAYS_ACT_LEFT) == SDL_SCANCODE_A);
    CHECK(first_input(&p, KB, KHDAYS_ACT_RIGHT) == SDL_SCANCODE_D);
    CHECK(first_input(&p, KB, KHDAYS_ACT_A) == mouse + KHDAYS_MOUSE_LEFT);
    CHECK(first_input(&p, KB, KHDAYS_ACT_B) == SDL_SCANCODE_SPACE);
    CHECK(first_input(&p, KB, KHDAYS_ACT_X) == SDL_SCANCODE_C);
    CHECK(first_input(&p, KB, KHDAYS_ACT_Y) == mouse + KHDAYS_MOUSE_RIGHT);
    CHECK(first_input(&p, KB, KHDAYS_ACT_L) == SDL_SCANCODE_LSHIFT &&
          p.input[KB][BASE][KHDAYS_ACT_L][1] == SDL_SCANCODE_LEFT);
    CHECK(first_input(&p, KB, KHDAYS_ACT_R) == mouse + KHDAYS_MOUSE_MIDDLE &&
          p.input[KB][BASE][KHDAYS_ACT_R][1] == SDL_SCANCODE_RIGHT);
    CHECK(first_input(&p, KB, KHDAYS_ACT_START) == SDL_SCANCODE_M &&
          p.input[KB][BASE][KHDAYS_ACT_START][1] == SDL_SCANCODE_RETURN);
    CHECK(first_input(&p, KB, KHDAYS_ACT_SELECT) == SDL_SCANCODE_N);
    CHECK(p.input[KB][KHDAYS_LAYER_FIELD][KHDAYS_ACT_MENU_UP][0] == mouse + KHDAYS_MOUSE_WHEEL_UP);
    CHECK(p.input[KB][KHDAYS_LAYER_FIELD][KHDAYS_ACT_MENU_DOWN][0] == mouse + KHDAYS_MOUSE_WHEEL_DOWN);
    CHECK(p.input[KB][KHDAYS_LAYER_FIELD][KHDAYS_ACT_MENU_LEFT][0] == SDL_SCANCODE_Q);
    CHECK(p.input[KB][KHDAYS_LAYER_FIELD][KHDAYS_ACT_MENU_RIGHT][0] == SDL_SCANCODE_E);

    CHECK(first_input(&p, PAD, KHDAYS_ACT_A) == KHDAYS_PAD_EAST);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_B) == KHDAYS_PAD_SOUTH);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_Y) == KHDAYS_PAD_WEST);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_X) == KHDAYS_PAD_NORTH);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_L) == KHDAYS_PAD_LEFTSHOULDER);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_R) == KHDAYS_PAD_RIGHTSHOULDER);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_START) == KHDAYS_PAD_START);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_SELECT) == KHDAYS_PAD_BACK);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_UP) == KHDAYS_PAD_DPUP);
    CHECK(p.input[PAD][BASE][KHDAYS_ACT_OPEN_SETTINGS][0] == KHDAYS_PAD_GUIDE);
    CHECK(p.input[PAD][BASE][KHDAYS_ACT_OPEN_SETTINGS][1] == KHDAYS_PAD_LEFTSTICK);
    CHECK(p.stick[0].role == KHDAYS_STICK_DPAD && p.stick[0].dead_zone == 30 && p.stick[0].hysteresis == 5);
    CHECK(!p.enh.analog_camera && !p.enh.separate_lock_on && !p.enh.dpad_command_menu && !p.enh.analog_movement);
    CHECK(khdays_profile_repair(&p) == 0); /* the default is valid as it is */
}

static KhdaysInputSnapshot resolve(const KhdaysProfile *p, KhdaysDeviceState *s)
{
    KhdaysInputSnapshot out;
    memset(&out, 0, sizeof(out));
    khdays_bindings_resolve(p, s, &out);
    return out;
}

/* A DS button is held while any input bound to it is. */
static void test_sources_add_up(void)
{
    KhdaysProfile p;
    KhdaysDeviceState s;
    khdays_profile_default(&p);
    khdays_bindings_reset(&s);
    s.key[SDL_SCANCODE_SPACE] = 1;
    s.pad[KHDAYS_PAD_SOUTH] = 1;
    CHECK(resolve(&p, &s).ds_buttons == DS(KHDAYS_ACT_B));
    s.key[SDL_SCANCODE_SPACE] = 0;
    CHECK(resolve(&p, &s).ds_buttons == DS(KHDAYS_ACT_B)); /* still held on the gamepad */
    s.pad[KHDAYS_PAD_SOUTH] = 0;
    CHECK(resolve(&p, &s).ds_buttons == 0);
    /* Enhancement inputs do nothing while their enhancement is off. */
    s.pad[KHDAYS_PAD_RIGHTSTICK] = 1;
    s.trigger[1] = 32767;
    CHECK(resolve(&p, &s).ds_buttons == 0 && resolve(&p, &s).actions == 0);
}

static uint16_t stick_at(double degrees, double percent, KhdaysStick *stick, int *active, int *sector)
{
    const double r = percent * 32767.0 / 100.0, a = degrees * 3.14159265358979323846 / 180.0;
    /* SDL's y grows downwards */
    return khdays_stick_dpad((int)(r * SDL_cos(a)), (int)(-r * SDL_sin(a)), stick, active, sector);
}

/* The left stick gives the DS D-pad's eight directions and nothing else. */
static void test_stick_directions(void)
{
    KhdaysStick stick = {KHDAYS_STICK_DPAD, 30, 5};
    const uint16_t right = DS(KHDAYS_ACT_RIGHT), up = DS(KHDAYS_ACT_UP), left = DS(KHDAYS_ACT_LEFT),
                   down = DS(KHDAYS_ACT_DOWN);
    const uint16_t expected[8] = {right, right | up, up, up | left, left, left | down, down, down | right};
    for (int d = 0; d < 8; ++d) {
        int active = 0, sector = -1;
        CHECK(stick_at(d * 45.0, 100, &stick, &active, &sector) == expected[d]);
    }
    {
        int active = 0, sector = -1;
        CHECK(stick_at(10, 20, &stick, &active, &sector) == 0); /* inside the dead zone */
        CHECK(stick_at(10, 35, &stick, &active, &sector) == right);
        CHECK(stick_at(10, 27, &stick, &active, &sector) == right); /* not yet back below 30 - 5 */
        CHECK(stick_at(10, 24, &stick, &active, &sector) == 0);
    }
    {
        /* Resting on the edge between right and up-right does not flicker. */
        int active = 0, sector = -1;
        CHECK(stick_at(20, 100, &stick, &active, &sector) == right);
        CHECK(stick_at(23, 100, &stick, &active, &sector) == right); /* past 22.5, within the hysteresis */
        CHECK(stick_at(21, 100, &stick, &active, &sector) == right);
        CHECK(stick_at(30, 100, &stick, &active, &sector) == (right | up)); /* clearly the next one */
    }
}

static void test_triggers(void)
{
    KhdaysProfile p;
    KhdaysDeviceState s;
    khdays_profile_default(&p);
    khdays_profile_set(&p, PAD, BASE, KHDAYS_ACT_R, 1, KHDAYS_PAD_RIGHTTRIGGER);
    khdays_bindings_reset(&s);
    s.trigger[1] = 16056; /* 49 % */
    CHECK(resolve(&p, &s).ds_buttons == 0);
    s.trigger[1] = 16711; /* 51 % */
    CHECK(resolve(&p, &s).ds_buttons == DS(KHDAYS_ACT_R));
}

static void test_binding_rules(void)
{
    KhdaysProfile p;
    khdays_profile_default(&p);
    /* One input, one action: binding it elsewhere moves it. */
    CHECK(khdays_profile_set(&p, KB, BASE, KHDAYS_ACT_A, 1, SDL_SCANCODE_SPACE) == 0);
    CHECK(khdays_profile_owner(&p, KB, BASE, SDL_SCANCODE_SPACE) == KHDAYS_ACT_A);
    CHECK(first_input(&p, KB, KHDAYS_ACT_B) == KHDAYS_NO_INPUT);
    /* Two inputs per action at most. */
    CHECK(khdays_profile_set(&p, KB, BASE, KHDAYS_ACT_B, 2, SDL_SCANCODE_C) == -1);
    /* Esc is the settings key and cannot be bound. */
    CHECK(khdays_profile_set(&p, KB, BASE, KHDAYS_ACT_START, 0, SDL_SCANCODE_ESCAPE) == -1);
    /* Repair: a doubled input keeps its first action; open_settings comes back. */
    p.input[PAD][BASE][KHDAYS_ACT_START][1] = KHDAYS_PAD_EAST; /* east is also DS A */
    p.input[PAD][BASE][KHDAYS_ACT_OPEN_SETTINGS][0] = KHDAYS_NO_INPUT;
    p.input[PAD][BASE][KHDAYS_ACT_OPEN_SETTINGS][1] = KHDAYS_NO_INPUT;
    CHECK(khdays_profile_repair(&p) == 2);
    CHECK(khdays_profile_owner(&p, PAD, BASE, KHDAYS_PAD_EAST) == KHDAYS_ACT_A);
    CHECK(p.input[PAD][BASE][KHDAYS_ACT_OPEN_SETTINGS][0] == KHDAYS_PAD_GUIDE);
}

/* What is held when a mask is set counts only once let go. */
static void test_masking(void)
{
    KhdaysProfile p;
    KhdaysDeviceState s;
    khdays_profile_default(&p);
    khdays_bindings_reset(&s);
    s.key[SDL_SCANCODE_RETURN] = 1;
    khdays_bindings_mask_held(&s);
    CHECK(resolve(&p, &s).ds_buttons == 0);
    s.key[SDL_SCANCODE_RETURN] = 0;
    CHECK(resolve(&p, &s).ds_buttons == 0);
    s.key[SDL_SCANCODE_RETURN] = 1;
    CHECK(resolve(&p, &s).ds_buttons == DS(KHDAYS_ACT_START));
}

/* The devices, with SDL's virtual gamepads. */
static KhdaysInputSnapshot last;
static int publishes;

static void capture(const KhdaysInputSnapshot *snapshot)
{
    last = *snapshot;
    ++publishes;
}

static SDL_JoystickID attach_pad(const char *name)
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT; /* in SDL_GamepadButton order */
    desc.name = name;
    return SDL_AttachVirtualJoystick(&desc);
}

static void pump(void)
{
    SDL_Event event;
    SDL_UpdateJoysticks();
    while (SDL_PollEvent(&event)) {
        khdays_devices_handle(&event, NULL);
    }
}

static void window_event(Uint32 type)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    khdays_devices_handle(&event, NULL);
}

static void test_devices(void)
{
    KhdaysProfile p;
    SDL_JoystickID one, two;
    SDL_Joystick *first, *second;
    char notice[160];
    /* Only SDL's virtual gamepads: a real controller plugged into the machine
     * running the tests would otherwise be the first one connected. */
    static const char *const real_drivers[] = {
        SDL_HINT_JOYSTICK_HIDAPI,      SDL_HINT_JOYSTICK_RAWINPUT, SDL_HINT_XINPUT_ENABLED,
        SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_WGI,      SDL_HINT_JOYSTICK_GAMEINPUT,
    };
    for (size_t i = 0; i < sizeof(real_drivers) / sizeof(real_drivers[0]); ++i) {
        SDL_SetHint(real_drivers[i], "0");
    }
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        ++failures;
        return;
    }
    khdays_profile_default(&p);
    khdays_devices_init(&p, capture);

    one = attach_pad("first pad");
    pump();
    CHECK(khdays_devices_gamepad_name() != NULL && strcmp(khdays_devices_gamepad_name(), "first pad") == 0);
    first = SDL_OpenJoystick(one);
    SDL_SetJoystickVirtualButton(first, SDL_GAMEPAD_BUTTON_EAST, true);
    pump();
    CHECK(last.ds_buttons == DS(KHDAYS_ACT_A));

    /* A second pad does not play while the first is there. */
    two = attach_pad("second pad");
    pump();
    second = SDL_OpenJoystick(two);
    SDL_SetJoystickVirtualButton(second, SDL_GAMEPAD_BUTTON_SOUTH, true);
    pump();
    CHECK(last.ds_buttons == DS(KHDAYS_ACT_A));

    /* Pulled out while holding A: A is let go at once, and the other pad plays. */
    SDL_CloseJoystick(first);
    SDL_DetachVirtualJoystick(one);
    pump();
    CHECK((last.ds_buttons & DS(KHDAYS_ACT_A)) == 0);
    CHECK(khdays_devices_gamepad_name() != NULL && strcmp(khdays_devices_gamepad_name(), "second pad") == 0);
    SDL_SetJoystickVirtualButton(second, SDL_GAMEPAD_BUTTON_SOUTH, false);
    pump();
    SDL_SetJoystickVirtualButton(second, SDL_GAMEPAD_BUTTON_SOUTH, true);
    pump();
    CHECK(last.ds_buttons == DS(KHDAYS_ACT_B));

    /* Losing the focus drops everything; what is still held when it comes
     * back counts only once let go. */
    window_event(SDL_EVENT_WINDOW_FOCUS_LOST);
    CHECK(last.ds_buttons == 0);
    window_event(SDL_EVENT_WINDOW_FOCUS_GAINED);
    CHECK(last.ds_buttons == 0);
    SDL_SetJoystickVirtualButton(second, SDL_GAMEPAD_BUTTON_SOUTH, false);
    pump();
    SDL_SetJoystickVirtualButton(second, SDL_GAMEPAD_BUTTON_SOUTH, true);
    pump();
    CHECK(last.ds_buttons == DS(KHDAYS_ACT_B));

    /* The gamepad's open-settings input asks for the settings menu. */
    SDL_SetJoystickVirtualButton(second, SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
    pump();
    CHECK(khdays_devices_take_settings_request() == 1);
    CHECK(khdays_devices_take_settings_request() == 0);

    /* While the settings menu has the input, the game sees nothing. */
    khdays_devices_set_game_input(0);
    CHECK(last.ui_open == 1 && last.ds_buttons == 0);
    khdays_devices_set_game_input(1);
    CHECK(last.ui_open == 0 && last.ds_buttons == 0); /* south still held: masked */

    CHECK(khdays_devices_take_notice(notice, sizeof(notice)) == 0);
    SDL_CloseJoystick(second);
    SDL_DetachVirtualJoystick(two);
    pump();
    CHECK(khdays_devices_gamepad_name() == NULL);
    CHECK(last.ds_buttons == 0);
    SDL_Quit();
}

/* ---- The settings file --------------------- */

static int notices;
static char last_notice[256];

static void count_notice(const char *text)
{
    ++notices;
    SDL_strlcpy(last_notice, text, sizeof(last_notice));
}

static char dir[512];

static void path_of(const char *name, char *path, size_t size)
{
    SDL_snprintf(path, size, "%s%s", dir, name);
}

static void write_text(const char *name, const char *text)
{
    char path[600];
    path_of(name, path, sizeof(path));
    SDL_SaveFile(path, text, strlen(text));
}

/* Loads `text` as a settings file; returns how many notices it gave. */
static int load_text(const char *text, KhdaysProfile *p)
{
    char path[600];
    write_text("s.ini", text);
    path_of("s.ini", path, sizeof(path));
    notices = 0;
    khdays_profile_load(p, path, count_notice);
    return notices;
}

static void test_settings_file(void)
{
    KhdaysProfile p, q;
    char path[600], again[600];
    size_t size_a = 0, size_b = 0;
    char *a, *b;
    const char *base = SDL_GetBasePath();
    SDL_snprintf(dir, sizeof(dir), "%sinput_test_files/", base != NULL ? base : "./");
    SDL_RemovePath(dir); /* leftovers of an earlier run, if empty */
    SDL_CreateDirectory(dir);

    /* The defaults go out and come back unchanged, byte for byte. */
    khdays_profile_default(&p);
    path_of("round.ini", path, sizeof(path));
    CHECK(khdays_profile_save(&p, path, count_notice) == 0);
    notices = 0;
    khdays_profile_load(&q, path, count_notice);
    CHECK(notices == 0);
    CHECK(memcmp(&p, &q, sizeof(p)) == 0);
    path_of("round2.ini", again, sizeof(again));
    CHECK(khdays_profile_save(&q, again, count_notice) == 0);
    a = (char *)SDL_LoadFile(path, &size_a);
    b = (char *)SDL_LoadFile(again, &size_b);
    CHECK(a != NULL && b != NULL && size_a == size_b && memcmp(a, b, size_a) == 0);
    SDL_free(a);
    SDL_free(b);

    /* No file: the defaults, silently. */
    path_of("missing.ini", path, sizeof(path));
    notices = 0;
    khdays_profile_load(&q, path, count_notice);
    CHECK(notices == 0 && memcmp(&p, &q, sizeof(p)) == 0);

    /* Not text at all: kept as .bad, the defaults, one notice. */
    {
        static const char junk[] = {'\x7f', 'E', 'L', 'F', '\0', '\x01', '\x02'};
        path_of("junk.ini", path, sizeof(path));
        SDL_SaveFile(path, junk, sizeof(junk));
        notices = 0;
        khdays_profile_load(&q, path, count_notice);
        CHECK(notices == 1 && memcmp(&p, &q, sizeof(p)) == 0);
        SDL_strlcat(path, ".bad", sizeof(path));
        CHECK(SDL_GetPathInfo(path, NULL));
    }

    /* A line that is no setting at all makes the file unreadable too. */
    CHECK(load_text("version = 1\nthis is not a setting\n", &q) == 1 && memcmp(&p, &q, sizeof(p)) == 0);

    /* CRLF, a byte order mark, comments, spaces, any case. */
    CHECK(load_text("\xef\xbb\xbf# mine\r\n[Keyboard]\r\n  A =  V  \r\n; also mine\r\n", &q) == 0);
    CHECK(q.input[KB][BASE][KHDAYS_ACT_A][0] == SDL_SCANCODE_V);

    /* Two inputs, and a comment after a value. */
    CHECK(load_text("[gamepad]\nb = south, north   # jump twice over\n", &q) == 0);
    CHECK(q.input[PAD][BASE][KHDAYS_ACT_B][0] == KHDAYS_PAD_SOUTH);
    CHECK(q.input[PAD][BASE][KHDAYS_ACT_B][1] == KHDAYS_PAD_NORTH);
    CHECK(khdays_profile_owner(&q, PAD, BASE, KHDAYS_PAD_NORTH) == KHDAYS_ACT_B); /* taken from X */

    /* Multiplayer: the nickname as the game can hold it (ten
     * printable characters), online, the server; one that is no address
     * gives the default, and all of it is written back. */
    CHECK(load_text("[network]\nnickname = Roxas13\nonline = off\nserver = example.org:4000\n", &q) == 0);
    CHECK(strcmp(q.network.nickname, "Roxas13") == 0 && !q.network.online &&
          strcmp(q.network.server, "example.org:4000") == 0);
    CHECK(load_text("[network]\nnickname = TwelveLetters\n", &q) == 1 && strcmp(q.network.nickname, "TwelveLett") == 0);
    CHECK(load_text("[network]\nserver = no address!\n", &q) == 1 && strcmp(q.network.server, "212.227.76.27") == 0);
    CHECK(load_text("", &q) == 0 && q.network.nickname[0] == '\0' && q.network.online && !q.network.show_ping);
    CHECK(load_text("[network]\nshow_ping = on\n", &q) == 0 && q.network.show_ping);
    CHECK(load_text("[network]\nshow_ping = maybe\n", &q) == 1 && !q.network.show_ping);
    /* The update check: on unless turned off; a bad value says so and is on. */
    CHECK(load_text("", &q) == 0 && q.network.check_updates);
    CHECK(load_text("[network]\ncheck_updates = off\n", &q) == 0 && !q.network.check_updates);
    CHECK(load_text("[network]\ncheck_updates = sometimes\n", &q) == 1 && q.network.check_updates);
    load_text("[network]\nnickname = Xion\nonline = off\nshow_ping = on\ncheck_updates = off\n", &q);
    path_of("network.ini", path, sizeof(path));
    khdays_profile_save(&q, path, count_notice);
    khdays_profile_load(&p, path, count_notice);
    CHECK(memcmp(&p.network, &q.network, sizeof(p.network)) == 0);

    /* The Stop magic: on unless turned off; a bad value says so and is on; written back. */
    CHECK(load_text("", &q) == 0 && q.gameplay.stop_magic);
    CHECK(load_text("[gameplay]\nstop_magic = off\n", &q) == 0 && !q.gameplay.stop_magic);
    CHECK(load_text("[gameplay]\nstop_magic = sometimes\n", &q) == 1 && q.gameplay.stop_magic);
    load_text("[gameplay]\nstop_magic = off\n", &q);
    path_of("gameplay.ini", path, sizeof(path));
    khdays_profile_save(&q, path, count_notice);
    khdays_profile_load(&p, path, count_notice);
    CHECK(!p.gameplay.stop_magic);

    /* Unknown sections, keys and inputs: ignored with a notice each. */
    CHECK(load_text("[future]\nport = 3\n", &q) == 1);
    CHECK(load_text("[video]\nscale = 3\n", &q) == 1);
    CHECK(load_text("[keyboard]\njump = Space\n", &q) == 1);
    CHECK(load_text("[gamepad]\na = button7\n", &q) == 1 && q.input[PAD][BASE][KHDAYS_ACT_A][0] == KHDAYS_NO_INPUT);
    /* ...and kept when the file is written again. */
    load_text("[future]\nport = 3\n[video]\nscale = 3\n[keyboard]\njump = Space\n", &q);
    path_of("kept.ini", path, sizeof(path));
    khdays_profile_save(&q, path, count_notice);
    a = (char *)SDL_LoadFile(path, &size_a);
    CHECK(a != NULL && strstr(a, "scale = 3") != NULL && strstr(a, "jump = Space") != NULL);
    CHECK(a != NULL && strstr(a, "[future]\nport = 3") != NULL);
    SDL_free(a);
    /* ...however many there are and however long (once only 64 lines of 199
     * characters were kept). */
    {
        static char text[64 + 100 * 32 + 320];
        char line[310];
        int at = SDL_snprintf(text, sizeof(text), "[future]\n");
        for (int i = 0; i < 100; ++i) {
            at += SDL_snprintf(text + at, sizeof(text) - (size_t)at, "key%d = %d\n", i, i);
        }
        SDL_memset(line, 'x', 300);
        line[300] = '\0';
        SDL_snprintf(text + at, sizeof(text) - (size_t)at, "long = %s\n", line);
        load_text(text, &q);
        path_of("kept_many.ini", path, sizeof(path));
        khdays_profile_save(&q, path, count_notice);
        a = (char *)SDL_LoadFile(path, &size_a);
        CHECK(a != NULL && strstr(a, "key0 = 0\n") != NULL && strstr(a, "key99 = 99\n") != NULL);
        CHECK(a != NULL && strstr(a, line) != NULL);
        SDL_free(a);
    }

    /* Numbers out of range are brought in, with a notice. */
    CHECK(load_text("[sticks]\nleft_dead_zone = 200\n", &q) == 1 && q.stick[0].dead_zone == 90);
    CHECK(load_text("[enhancements]\ncamera_speed = 1\nwalk_speed = 99\n", &q) == 2);
    CHECK(q.enh.camera_speed == 10 && q.enh.walk_speed == 90);
    CHECK(load_text("[sticks]\nright = sideways\n", &q) == 1 && q.stick[1].role == KHDAYS_STICK_CAMERA);

    /* One input for two actions: the first keeps it. */
    CHECK(load_text("[keyboard]\na = Z\nb = Z\n", &q) == 1);
    CHECK(khdays_profile_owner(&q, KB, BASE, SDL_SCANCODE_Z) == KHDAYS_ACT_A);

    /* The same action twice: the last one counts. */
    CHECK(load_text("[keyboard]\na = K\na = V\n", &q) == 1 && q.input[KB][BASE][KHDAYS_ACT_A][0] == SDL_SCANCODE_V);

    /* Escape is never bound; the gamepad never loses its way to the settings. */
    CHECK(load_text("[keyboard]\nstart = Escape\n", &q) == 1);
    CHECK(load_text("[gamepad]\nopen_settings =\n", &q) == 1);
    CHECK(q.input[PAD][BASE][KHDAYS_ACT_OPEN_SETTINGS][0] == KHDAYS_PAD_GUIDE);

    /* A preset this version does not know is read as ds; a version too. */
    CHECK(load_text("preset = kh9\n", &q) == 1 && q.preset == KHDAYS_PRESET_DS);
    CHECK(load_text("version = 2\n", &q) == 1);

    /* KHDAYS_SETTINGS chooses the file. */
    SDL_setenv_unsafe("KHDAYS_SETTINGS", "elsewhere/mine.ini", 1);
    khdays_settings_path(path, sizeof(path));
    CHECK(strcmp(path, "elsewhere/mine.ini") == 0);
    SDL_unsetenv_unsafe("KHDAYS_SETTINGS");
    khdays_settings_path(path, sizeof(path));
    CHECK(strcmp(path, "saves/settings.ini") == 0);
}

/* The [video] section: the DS's
 * picture by default, every value written back as read, a bad value
 * reported in the file's language and replaced by the default. */
/* The mouse's buttons and wheel, bound in the keyboard's column: read and written by name, held as keys
 * are, masked as keys are. */
static void test_mouse_bindings(void)
{
    KhdaysProfile p, q;
    KhdaysDeviceState s;
    char path[600];
    CHECK(load_text("[keyboard]\na = Mouse Left, Z\nb = Mouse X2\nl = mouse wheel up\n", &p) == 0);
    CHECK(p.input[KB][BASE][KHDAYS_ACT_A][0] == KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_LEFT &&
          p.input[KB][BASE][KHDAYS_ACT_A][1] == SDL_SCANCODE_Z);
    CHECK(p.input[KB][BASE][KHDAYS_ACT_B][0] == KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_X2);
    CHECK(p.input[KB][BASE][KHDAYS_ACT_L][0] == KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_WHEEL_UP);
    CHECK(SDL_strcmp(khdays_mouse_input_name(KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_WHEEL_DOWN), "Mouse Wheel Down") == 0);
    CHECK(khdays_mouse_input_name(SDL_SCANCODE_Z) == NULL && khdays_mouse_input_from_name("Mouse 9") < 0);
    /* written by name and read back the same */
    path_of("mouse.ini", path, sizeof(path));
    CHECK(khdays_profile_save(&p, path, count_notice) == 0);
    notices = 0;
    khdays_profile_load(&q, path, count_notice);
    CHECK(notices == 0 && memcmp(&p, &q, sizeof(p)) == 0);
    /* held as a key is; masked until let go */
    khdays_bindings_reset(&s);
    s.mouse[KHDAYS_MOUSE_X2] = 1;
    CHECK(resolve(&p, &s).ds_buttons == DS(KHDAYS_ACT_B));
    s.mouse[KHDAYS_MOUSE_X2] = 0;
    s.mouse[KHDAYS_MOUSE_LEFT] = 1;
    s.mouse[KHDAYS_MOUSE_WHEEL_UP] = 1;
    CHECK(resolve(&p, &s).ds_buttons == (DS(KHDAYS_ACT_A) | DS(KHDAYS_ACT_L)));
    khdays_bindings_mask_held(&s);
    CHECK(resolve(&p, &s).ds_buttons == 0);
    s.mouse[KHDAYS_MOUSE_LEFT] = 0;
    CHECK(resolve(&p, &s).ds_buttons == 0);
    s.mouse[KHDAYS_MOUSE_LEFT] = 1;
    CHECK(resolve(&p, &s).ds_buttons == DS(KHDAYS_ACT_A));
    /* an unbound mouse input does nothing */
    khdays_bindings_reset(&s);
    s.mouse[KHDAYS_MOUSE_X1] = 1;
    CHECK(resolve(&p, &s).ds_buttons == 0);
}

static void test_video_settings(void)
{
    KhdaysProfile p, q, r;
    char path[600];
    size_t size = 0;
    char *text;
    khdays_profile_default(&p);
    CHECK(p.video.internal_resolution == KHDAYS_RESOLUTION_WINDOW && p.video.aspect == KHDAYS_ASPECT_16_9 &&
          !p.video.smooth_scaling);
    CHECK(!p.video.texture_smoothing && p.video.hd_graphics && !p.video.dump_graphics);

    /* No [video]: the defaults, silently. */
    CHECK(load_text("version = 1\n", &q) == 0 && memcmp(&q.video, &p.video, sizeof(p.video)) == 0);

    /* Every value, and back through a save. */
    CHECK(load_text("[video]\ninternal_resolution = window\naspect = 16:10\nscaling = smooth\n"
                    "texture_smoothing = on\nhd_graphics = off\ndump_graphics = on\n",
                    &q) == 0);
    CHECK(q.video.internal_resolution == KHDAYS_RESOLUTION_WINDOW && q.video.aspect == KHDAYS_ASPECT_16_10);
    CHECK(q.video.smooth_scaling && q.video.texture_smoothing && !q.video.hd_graphics && q.video.dump_graphics);
    path_of("video.ini", path, sizeof(path));
    CHECK(khdays_profile_save(&q, path, count_notice) == 0);
    notices = 0;
    khdays_profile_load(&r, path, count_notice);
    CHECK(notices == 0 && memcmp(&q.video, &r.video, sizeof(q.video)) == 0);
    {
        static const int scales[] = {1, 2, 3, 4, 6, 8};
        for (size_t i = 0; i < sizeof(scales) / sizeof(scales[0]); ++i) {
            char line[64];
            SDL_snprintf(line, sizeof(line), "[video]\ninternal_resolution = %d\n", scales[i]);
            CHECK(load_text(line, &q) == 0 && q.video.internal_resolution == scales[i]);
        }
    }
    CHECK(load_text("[video]\naspect = 16:9\n", &q) == 0 && q.video.aspect == KHDAYS_ASPECT_16_9);
    CHECK(load_text("[video]\naspect = window\n", &q) == 0 && q.video.aspect == KHDAYS_ASPECT_WINDOW);

    /* Antialiasing: off by default; 2x to 4x, written back as read; a bad value says so and is off. */
    CHECK(p.video.antialiasing == 1);
    CHECK(load_text("[video]\nantialiasing = 3x\n", &q) == 0 && q.video.antialiasing == 3);
    CHECK(khdays_profile_save(&q, path, count_notice) == 0);
    khdays_profile_load(&r, path, count_notice);
    CHECK(r.video.antialiasing == 3);
    CHECK(load_text("[video]\nantialiasing = off\n", &q) == 0 && q.video.antialiasing == 1);
    CHECK(load_text("[video]\nantialiasing = 8x\n", &q) == 1 && q.video.antialiasing == 1);

    /* The screens: one by default; one with 16:9 when no aspect is given (before or after it), two with the DS's
     * 4:3; a bad value says so and keeps one; back through a save. */
    CHECK(p.video.one_screen && p.video.hud_size == 4);
    CHECK(load_text("[video]\nscreens = one\n", &q) == 0 && q.video.one_screen && q.video.aspect == KHDAYS_ASPECT_16_9);
    CHECK(load_text("[video]\naspect = 4:3\nscreens = one\n", &q) == 0 && q.video.one_screen &&
          q.video.aspect == KHDAYS_ASPECT_4_3);
    CHECK(load_text("[video]\nscreens = one\naspect = window\n", &q) == 0 && q.video.aspect == KHDAYS_ASPECT_WINDOW);
    CHECK(load_text("[video]\nscreens = Two\n", &q) == 0 && !q.video.one_screen && q.video.aspect == KHDAYS_ASPECT_4_3);
    CHECK(load_text("[video]\nscreens = three\n", &q) == 1 && q.video.one_screen);
    CHECK(load_text("[video]\nscreens = one\n", &q) == 0);
    CHECK(khdays_profile_save(&q, path, count_notice) == 0);
    khdays_profile_load(&r, path, count_notice);
    CHECK(r.video.one_screen && r.video.aspect == KHDAYS_ASPECT_16_9);
    /* The HUD's size: 1 to 8, 4 by default; a bad value says so. */
    CHECK(load_text("[video]\nhud_size = 6\n", &q) == 0 && q.video.hud_size == 6);
    CHECK(khdays_profile_save(&q, path, count_notice) == 0);
    khdays_profile_load(&r, path, count_notice);
    CHECK(r.video.hud_size == 6);
    CHECK(load_text("[video]\nhud_size = 9\n", &q) == 1 && q.video.hud_size == 4);
    CHECK(load_text("[video]\nhud_size = big\n", &q) == 1 && q.video.hud_size == 4);

    /* The graphics API: SDL's choice by default; each name, and back through a save. */
    CHECK(p.video.backend == KHDAYS_BACKEND_AUTO);
    CHECK(load_text("[video]\nbackend = direct3d12\n", &q) == 0 && q.video.backend == KHDAYS_BACKEND_D3D12);
    CHECK(load_text("[video]\nbackend = Vulkan\n", &q) == 0 && q.video.backend == KHDAYS_BACKEND_VULKAN);
    CHECK(khdays_profile_save(&q, path, count_notice) == 0);
    khdays_profile_load(&r, path, count_notice);
    CHECK(r.video.backend == KHDAYS_BACKEND_VULKAN);
    CHECK(load_text("[video]\nbackend = opengl\n", &q) == 1 && q.video.backend == KHDAYS_BACKEND_AUTO);

    /* Bad values: a notice each, and the default. */
    CHECK(load_text("[video]\ninternal_resolution = huge\naspect = 21:9\nscaling = blurry\nhd_graphics = maybe\n"
                    "internal_resolution = 0\n",
                    &q) == 5);
    CHECK(q.video.internal_resolution == KHDAYS_RESOLUTION_WINDOW && q.video.aspect == KHDAYS_ASPECT_16_9 &&
          !q.video.smooth_scaling && q.video.hd_graphics);
    if (SDL_getenv("KHDAYS_LANGUAGE") == NULL) {
        CHECK(load_text("language = es\n[video]\naspect = 21:9\n", &q) == 1);
        CHECK(strstr(last_notice, "no es un valor posible") != NULL && strstr(last_notice, "se usa 16:9") != NULL);
        CHECK(load_text("language = de\n[video]\nshader = crt\n", &q) == 1);
        CHECK(strstr(last_notice, "hat keine Einstellung") != NULL);
    }

    /* A key this version does not know stays in [video]. */
    CHECK(load_text("[video]\nshader = crt\n", &q) == 1);
    path_of("video_kept.ini", path, sizeof(path));
    khdays_profile_save(&q, path, count_notice);
    text = (char *)SDL_LoadFile(path, &size);
    {
        const char *video = text != NULL ? strstr(text, "dump_graphics = off\n") : NULL;
        const char *kept = video != NULL ? strstr(video, "shader = crt\n") : NULL;
        CHECK(kept != NULL && memchr(video, '[', (size_t)(kept - video)) == NULL); /* in [video] */
    }
    SDL_free(text);

    /* The repair brings a profile built in code back to valid values. */
    q = p;
    q.video.internal_resolution = -3;
    q.video.aspect = 9;
    q.video.one_screen = 5;
    q.video.hud_size = 0;
    CHECK(khdays_profile_repair(&q) == 4 && q.video.internal_resolution == KHDAYS_RESOLUTION_WINDOW &&
          q.video.aspect == KHDAYS_ASPECT_16_9);
    CHECK(q.video.one_screen == 1 && q.video.hud_size == 1);
    khdays_video_default(&q.video);
    CHECK(memcmp(&q.video, &p.video, sizeof(p.video)) == 0);
}

/* The [sound] section: every volume
 * at 100 and the PC's output by default, every value written back as read,
 * a bad value reported in the file's language and replaced by the default,
 * and a key this version does not know kept. */
static void test_sound_settings(void)
{
    KhdaysProfile p, q, r;
    char path[600];
    size_t size = 0;
    char *text;
    khdays_profile_default(&p);
    CHECK(p.sound.volume == 100 && p.sound.music == 100 && p.sound.effects == 100 && p.sound.voices == 100);
    CHECK(!p.sound.ds_exact);

    /* No [sound]: the defaults, silently. */
    CHECK(load_text("version = 1\n", &q) == 0 && memcmp(&q.sound, &p.sound, sizeof(p.sound)) == 0);

    /* Every value, and back through a save. */
    CHECK(load_text("[sound]\nvolume = 80\nmusic = 0\neffects = 55\nvoices = 100\nds_exact = on\n", &q) == 0);
    CHECK(q.sound.volume == 80 && q.sound.music == 0 && q.sound.effects == 55 && q.sound.voices == 100);
    CHECK(q.sound.ds_exact);
    path_of("sound.ini", path, sizeof(path));
    CHECK(khdays_profile_save(&q, path, count_notice) == 0);
    notices = 0;
    khdays_profile_load(&r, path, count_notice);
    CHECK(notices == 0 && memcmp(&q.sound, &r.sound, sizeof(q.sound)) == 0);

    /* Bad values: a notice each, and the default. */
    CHECK(load_text("[sound]\nvolume = 101\nmusic = -1\neffects = loud\nds_exact = maybe\n", &q) == 4);
    CHECK(memcmp(&q.sound, &p.sound, sizeof(p.sound)) == 0);
    if (SDL_getenv("KHDAYS_LANGUAGE") == NULL) {
        CHECK(load_text("language = es\n[sound]\nmusic = 200\n", &q) == 1);
        CHECK(strstr(last_notice, "no es un valor posible") != NULL && strstr(last_notice, "se usa 100") != NULL);
        CHECK(load_text("language = fr\n[sound]\nreverb = on\n", &q) == 1);
        CHECK(strstr(last_notice, "n'a pas de réglage") != NULL);
    }

    /* A key this version does not know stays in [sound]. */
    CHECK(load_text("[sound]\nreverb = on\n", &q) == 1);
    path_of("sound_kept.ini", path, sizeof(path));
    khdays_profile_save(&q, path, count_notice);
    text = (char *)SDL_LoadFile(path, &size);
    {
        const char *sound = text != NULL ? strstr(text, "ds_exact = off\n") : NULL;
        const char *kept = sound != NULL ? strstr(sound, "reverb = on\n") : NULL;
        CHECK(kept != NULL && memchr(sound, '[', (size_t)(kept - sound)) == NULL); /* in [sound] */
    }
    SDL_free(text);

    /* The repair brings a profile built in code back to valid values. */
    q = p;
    q.sound.volume = 150;
    q.sound.voices = -5;
    CHECK(khdays_profile_repair(&q) == 2 && q.sound.volume == 100 && q.sound.voices == 0);
}

static void test_video_page(void);
static void test_face_buttons(void);
static void test_sound_page(void);
static void test_network_page(void);
static void test_settings_mouse(void);
static void test_settings_changes(void);
static void test_settings_language(void);
static void test_settings_pad(void);

/* ---- The settings menu: its content, then the menu driven by
 * key presses, gamepad buttons and the mouse, drawn with SDL's software
 * renderer (no window). ---- */

static SDL_Renderer *screen;
static SDL_Surface *surface;

/* KHDAYS_TEST_SHOTS=<directory>: the menu as drawn, as <name>.bmp there. For
 * looking at it; nothing is checked. */
static void shot(const char *name)
{
    const char *folder = SDL_getenv("KHDAYS_TEST_SHOTS");
    char path[512];
    if (folder == NULL) {
        return;
    }
    SDL_SetRenderDrawColor(screen, 60, 90, 60, 255); /* a stand-in for the game's picture */
    SDL_RenderClear(screen);
    khdays_settings_draw(screen, NULL);
    SDL_FlushRenderer(screen); /* the software renderer draws when flushed */
    SDL_snprintf(path, sizeof(path), "%s/%s.bmp", folder, name);
    SDL_SaveBMP(surface, path);
}

/* A screen of `w` x `h` pixels to draw on. */
static void use_screen(int w, int h)
{
    if (screen != NULL) {
        khdays_settings_renderer_gone();
        SDL_DestroyRenderer(screen);
        SDL_DestroySurface(surface);
    }
    surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
    screen = surface != NULL ? SDL_CreateSoftwareRenderer(surface) : NULL;
    CHECK(screen != NULL);
}

static int handle(const SDL_Event *e)
{
    const int closed = khdays_settings_handle(e, NULL);
    khdays_settings_draw(screen, NULL); /* every state must draw */
    return closed;
}

static int key(SDL_Scancode code)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = code;
    return handle(&e);
}

static int pad_press(SDL_GamepadButton button)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.button = (Uint8)button;
    return handle(&e);
}

static int click_at(float x, float y, Uint8 button)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = button;
    e.button.x = x;
    e.button.y = y;
    return handle(&e);
}

/* A click in the middle of something the last drawing placed. */
static int click(int what, int index)
{
    SDL_FRect r;
    if (!khdays_settings_place(what, index, &r)) {
        CHECK(!"placed");
        return 0;
    }
    return click_at(r.x + r.w / 2, r.y + r.h / 2, SDL_BUTTON_LEFT);
}

static int focused(int *row_id, int *action)
{
    int section, in_rows, row, cell;
    khdays_settings_focus(&section, &in_rows, &row, &cell);
    if (!in_rows || !khdays_settings_row_info(row, row_id, action)) {
        *row_id = KHDAYS_SET_NONE;
        *action = -1;
    }
    return section;
}

/* From anywhere in the menu to the first row of section `s`. */
static void to_section(int s)
{
    int section, in_rows, row, cell;
    khdays_settings_focus(&section, &in_rows, &row, &cell);
    if (in_rows) {
        key(SDL_SCANCODE_ESCAPE);
    }
    for (int i = 0; i <= KHDAYS_SECTION_COUNT; ++i) {
        key(SDL_SCANCODE_UP);
    }
    for (int i = 0; i < s; ++i) {
        if (khdays_model_section_shown(i)) {
            key(SDL_SCANCODE_DOWN);
        }
    }
    key(SDL_SCANCODE_RETURN);
    khdays_settings_focus(&section, &in_rows, &row, &cell);
    CHECK(section == s && in_rows);
}

/* Down the rows to the setting `id` (a binding: the one of `action`). */
static void to_row(int id, int action)
{
    for (int i = 0; i < KHDAYS_MODEL_ROWS_MAX; ++i) {
        int row_id, row_action;
        focused(&row_id, &row_action);
        if (row_id == id && (action < 0 || row_action == action)) {
            return;
        }
        key(SDL_SCANCODE_DOWN);
    }
    CHECK(!"the row is in the section");
}

/* Left or right along a binding row to its cell `c` (keyboard 1, 2, gamepad
 * 1, 2). */
static void to_cell(int c)
{
    for (int i = 0; i < KHDAYS_CELLS; ++i) {
        int section, in_rows, row, cell;
        khdays_settings_focus(&section, &in_rows, &row, &cell);
        if (cell == c) {
            return;
        }
        key(cell < c ? SDL_SCANCODE_RIGHT : SDL_SCANCODE_LEFT);
    }
    CHECK(!"the cell is in the row");
}

/* Today's settings, with their ranges, steps and choices, in the
 * model, the conditional ones exactly when their enhancement is on; every
 * text of the menu in all five languages. */
static void test_settings_model(void)
{
    static const struct {
        int id, kind, low, high, step;
    } expected[] = {
        {KHDAYS_SET_PRESET_DS, KHDAYS_ROW_COMMAND, 0, 0, 0},
        {KHDAYS_SET_PRESET_KH2, KHDAYS_ROW_COMMAND, 0, 0, 0},
        {KHDAYS_SET_RESTORE_KEYBOARD, KHDAYS_ROW_COMMAND, 0, 0, 0},
        {KHDAYS_SET_RESTORE_GAMEPAD, KHDAYS_ROW_COMMAND, 0, 0, 0},
        {KHDAYS_SET_ENH_CAMERA, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_ENH_LOCK_ON, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_ENH_MENU, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_ENH_MOVEMENT, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_ENH_CHASE_FREE, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_LEFT_STICK, KHDAYS_ROW_CHOICE, 0, 2, 0},
        {KHDAYS_SET_RIGHT_STICK, KHDAYS_ROW_CHOICE, 0, 2, 0},
        {KHDAYS_SET_LEFT_DEAD_ZONE, KHDAYS_ROW_NUMBER, 5, 90, 5},
        {KHDAYS_SET_RIGHT_DEAD_ZONE, KHDAYS_ROW_NUMBER, 5, 90, 5},
        {KHDAYS_SET_LEFT_HYSTERESIS, KHDAYS_ROW_NUMBER, 0, 20, 1},
        {KHDAYS_SET_RIGHT_HYSTERESIS, KHDAYS_ROW_NUMBER, 0, 20, 1},
        {KHDAYS_SET_CAMERA_SPEED, KHDAYS_ROW_NUMBER, 10, 300, 10},
        {KHDAYS_SET_CAMERA_X, KHDAYS_ROW_CHOICE, 0, 1, 0},
        {KHDAYS_SET_CAMERA_Y, KHDAYS_ROW_CHOICE, 0, 1, 0},
        {KHDAYS_SET_RUN_FROM, KHDAYS_ROW_NUMBER, 50, 95, 5},
        {KHDAYS_SET_WALK_SPEED, KHDAYS_ROW_NUMBER, 20, 90, 5},
        {KHDAYS_SET_DISPLAY, KHDAYS_ROW_CHOICE, 0, KHDAYS_DISPLAY_COUNT - 1, 0},
        {KHDAYS_SET_RESOLUTION, KHDAYS_ROW_CHOICE, 0, 15, 0},  /* as the window allows, 2x-16x: 1x is not offered */
        {KHDAYS_SET_ANTIALIASING, KHDAYS_ROW_CHOICE, 0, 3, 0}, /* off, 2x, 3x, 4x */
        {KHDAYS_SET_ASPECT, KHDAYS_ROW_CHOICE, 0, KHDAYS_ASPECT_COUNT - 1, 0},
        {KHDAYS_SET_SCREENS, KHDAYS_ROW_CHOICE, 0, 1, 0},
        {KHDAYS_SET_HUD_SIZE, KHDAYS_ROW_NUMBER, 1, 8, 1},
        {KHDAYS_SET_SCALING, KHDAYS_ROW_CHOICE, 0, 1, 0},
        {KHDAYS_SET_TEXTURES, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_HD_GRAPHICS, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_DUMP_GRAPHICS, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_MOVIES, KHDAYS_ROW_CHOICE, 0, KHDAYS_MOVIES_COUNT - 1, 0},
        {KHDAYS_SET_SUBTITLES, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_PACE, KHDAYS_ROW_CHOICE, 0, 1, 0},
        {KHDAYS_SET_SHOW_FPS, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_BACKEND, KHDAYS_ROW_CHOICE, 0, KHDAYS_BACKEND_COUNT - 1, 0},
        {KHDAYS_SET_RESTORE_VIDEO, KHDAYS_ROW_COMMAND, 0, 0, 0},
        {KHDAYS_SET_VOLUME, KHDAYS_ROW_NUMBER, 0, 100, 5},
        {KHDAYS_SET_MUSIC, KHDAYS_ROW_NUMBER, 0, 100, 5},
        {KHDAYS_SET_EFFECTS, KHDAYS_ROW_NUMBER, 0, 100, 5},
        {KHDAYS_SET_VOICES, KHDAYS_ROW_NUMBER, 0, 100, 5},
        {KHDAYS_SET_MOVIES_VOLUME, KHDAYS_ROW_NUMBER, 0, 100, 5},
        {KHDAYS_SET_DS_EXACT, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_RESTORE_SOUND, KHDAYS_ROW_COMMAND, 0, 0, 0},
        {KHDAYS_SET_NICKNAME, KHDAYS_ROW_TEXT, 0, 0, 0},
        {KHDAYS_SET_ONLINE, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_SERVER, KHDAYS_ROW_TEXT, 0, 0, 0},
        {KHDAYS_SET_SHOW_PING, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_CHECK_UPDATES, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_LANGUAGE, KHDAYS_ROW_CHOICE, 0, KHDAYS_LANGUAGE_LAST, 0},
        {KHDAYS_SET_STOP_MAGIC, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_SESSION_LOG, KHDAYS_ROW_SWITCH, 0, 1, 0},
        {KHDAYS_SET_RESTORE_BOTH, KHDAYS_ROW_COMMAND, 0, 0, 0},
    };
    static KhdaysRow rows[KHDAYS_MODEL_ROWS_MAX];
    const KhdaysModelContext context = {1, 0, 0, NULL};
    for (int enhancements = 0; enhancements < 2; ++enhancements) {
        KhdaysProfile p;
        int found[KHDAYS_SET_COUNT] = {0};
        int bindings[KHDAYS_LAYER_COUNT][KHDAYS_ACT_COUNT] = {{0}};
        khdays_profile_default(&p);
        CHECK(p.enh.chase_free == 0); /* Chase Camera's Free: off unless chosen */
        p.enh.separate_lock_on = p.enh.dpad_command_menu = enhancements;
        khdays_model_refresh(&p);
        for (int s = 0; s < KHDAYS_SECTION_COUNT; ++s) {
            const int n = khdays_model_rows(s, &p, &context, rows);
            CHECK(n > 0 && n <= KHDAYS_MODEL_ROWS_MAX);
            CHECK(khdays_model_section_title(s)[0] != '\0');
            for (int i = 0; i < n; ++i) {
                const KhdaysRow *r = &rows[i];
                ++found[r->id];
                if (r->kind == KHDAYS_ROW_BINDING) {
                    ++bindings[r->layer][r->action];
                    /* the keyboard always has Esc for the settings */
                    CHECK(r->cells == (r->action == KHDAYS_ACT_OPEN_SETTINGS ? 0xcu : 0xfu));
                }
                for (size_t e = 0; e < sizeof(expected) / sizeof(expected[0]); ++e) {
                    if (expected[e].id == r->id) {
                        CHECK(r->kind == expected[e].kind);
                        if (r->kind == KHDAYS_ROW_NUMBER || r->kind == KHDAYS_ROW_CHOICE) {
                            CHECK(r->low == expected[e].low && r->high == expected[e].high);
                        }
                        if (r->kind == KHDAYS_ROW_NUMBER) {
                            CHECK(r->step == expected[e].step);
                        }
                        if (r->kind == KHDAYS_ROW_CHOICE) {
                            for (int c = 0; c <= r->high; ++c) {
                                CHECK(r->choices[c] != NULL && r->choices[c][0] != '\0');
                            }
                        }
                        CHECK(r->label != NULL && r->label[0] != '\0');
                    }
                }
                /* antialiasing warns while it is on: the internal resolution already scales the 3D */
                if (r->id == KHDAYS_SET_ANTIALIASING) {
                    CHECK(r->warning != NULL && r->warning[0] != '\0' && r->safe == 1 && *r->value == 1);
                }
            }
        }
        for (size_t e = 0; e < sizeof(expected) / sizeof(expected[0]); ++e) {
            CHECK(found[expected[e].id] == 1);
        }
        /* the DS buttons, open settings and the HUD mode; lock-on and the
         * command menu's directions with their enhancement */
        for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
            const int conditional = a >= KHDAYS_ACT_LOCK_ON && a <= KHDAYS_ACT_MENU_RIGHT;
            const int field = a >= KHDAYS_ACT_MENU_UP && a <= KHDAYS_ACT_MENU_RIGHT;
            CHECK(bindings[field ? KHDAYS_LAYER_FIELD : KHDAYS_LAYER_BASE][a] == (conditional ? enhancements : 1));
        }
    }
    /* the internal resolution: as the window allows first, then 2x to 16x; 1x, the DS's own,
     * only for a profile that holds it, shown first so the menu says what it is */
    {
        /* (the display mode first: a window, borderless or full screen; then the resolution) */
        KhdaysProfile p;
        khdays_profile_default(&p);
        khdays_model_rows(KHDAYS_SECTION_VIDEO, &p, &context, rows);
        CHECK(rows[0].id == KHDAYS_SET_DISPLAY && rows[0].high == KHDAYS_DISPLAY_COUNT - 1 &&
              khdays_model_choice_of(&rows[0]) == KHDAYS_DISPLAY_WINDOW);
        CHECK(rows[1].id == KHDAYS_SET_RESOLUTION && rows[1].high == 15);
        CHECK(khdays_model_choice_of(&rows[1]) == 0 && rows[1].values[0] == KHDAYS_RESOLUTION_WINDOW);
        CHECK(rows[1].values[1] == 2 && rows[1].values[15] == 16);
        p.video.internal_resolution = 1;
        khdays_model_rows(KHDAYS_SECTION_VIDEO, &p, &context, rows);
        CHECK(rows[1].high == 16 && khdays_model_choice_of(&rows[1]) == 0 &&
              strcmp(rows[1].choices[0], "1x (DS)") == 0);
        khdays_model_set_choice(&p, &rows[1], khdays_model_choice_of(&rows[1]) + 1);
        CHECK(p.video.internal_resolution == KHDAYS_RESOLUTION_WINDOW);
    }
    /* the texts the rows edit hold what the settings file does */
    {
        KhdaysProfile p;
        khdays_profile_default(&p);
        khdays_model_rows(KHDAYS_SECTION_MULTIPLAYER, &p, &context, rows);
        CHECK(rows[0].id == KHDAYS_SET_NICKNAME && rows[0].text_size == KHDAYS_NICKNAME_CHARS + 1);
        CHECK(rows[2].id == KHDAYS_SET_SERVER && rows[2].text_size == KHDAYS_SERVER_BYTES);
    }
    for (int id = 0; id < TXT_COUNT; ++id) {
        for (int l = KHDAYS_LANGUAGE_ENGLISH; l <= KHDAYS_LANGUAGE_LAST; ++l) {
            if (khdays_text_in(id, l) == NULL) {
                fprintf(stderr, "text %d has no language %d\n", id, l);
                CHECK(khdays_text_in(id, l) != NULL);
            }
        }
    }
}

/* At 1280x720, 1920x1080 and 3840x2160 every section and Close are in
 * sight without scrolling, the chosen section's rows beside them; in a small
 * window nothing overlaps. */
static void test_settings_layout(void)
{
    static const int sizes[][2] = {{1280, 720}, {1920, 1080}, {3840, 2160}, {640, 360}};
    KhdaysProfile p;
    khdays_profile_default(&p);
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        const int w = sizes[s][0], h = sizes[s][1];
        const int small = w < 1280;
        SDL_FRect first, entry, row;
        use_screen(w, h);
        if (screen == NULL) {
            return;
        }
        khdays_settings_open(&p);
        khdays_settings_draw(screen, NULL);
        khdays_settings_draw(screen, NULL);
        CHECK(khdays_settings_place(KHDAYS_PLACE_ENTRY, 0, &first));
        for (int e = 0; e <= KHDAYS_SECTION_COUNT; ++e) {
            if (e < KHDAYS_SECTION_COUNT && !khdays_model_section_shown(e)) {
                CHECK(!khdays_settings_place(KHDAYS_PLACE_ENTRY, e, &entry)); /* a hidden section has none */
                continue;
            }
            if (!khdays_settings_place(KHDAYS_PLACE_ENTRY, e, &entry)) {
                CHECK(small); /* scrolled out of sight only in a small window */
                continue;
            }
            CHECK(entry.x >= 0 && entry.y >= 0 && entry.x + entry.w <= (float)w && entry.y + entry.h <= (float)h);
            CHECK(small || entry.h == first.h); /* whole, not cut by the column's edge */
            /* a section's name in a readable size: the text grows with the window */
            CHECK(entry.h >= (float)h / 40.0f);
        }
        CHECK(khdays_settings_place(KHDAYS_PLACE_ROW, 2, &row));
        CHECK(row.x >= first.x + first.w); /* beside the column, not under it */
        for (int e = 0; e <= KHDAYS_SECTION_COUNT; ++e) {
            if (khdays_settings_place(KHDAYS_PLACE_ENTRY, e, &entry)) {
                CHECK(!SDL_HasRectIntersectionFloat(&entry, &row));
            }
        }
        {
            char name[32];
            SDL_snprintf(name, sizeof(name), "0-menu-%dx%d", w, h);
            shot(name);
        }
        CHECK(key(SDL_SCANCODE_ESCAPE) == 1);
    }
    use_screen(1280, 720);
}

/* The Mods section with two mods the game showed (runtime/scripts/view.h): it appears, a mod turned off,
 * a toggle, a choice, a number and a button asked of the game's thread, a mod moved later; gone again
 * with no mod. */
/* Mods with native plugins alone: the section shows, its native code switch (with its warning) changes the
 * settings, each plugin's state is told; safe mode said as such; gone again with no plugin. */
static void test_settings_native_code(void)
{
    static KhdaysRow rows[KHDAYS_MODEL_ROWS_MAX];
    const KhdaysModelContext context = {1, 0, 0, NULL};
    KhdaysProfile p;
    KhdaysPluginShown plugins[2] = {
        {"Spinner", "1.0", "khdays-port", "", KHDAYS_PLUGIN_WAITING},
        {"Broken", "2", "", "its library cannot be loaded (none)", KHDAYS_PLUGIN_REFUSED},
    };
    KhdaysPluginsShown shown = {0, 0, plugins, 2};
    int n, lines = 0;
    CHECK(!khdays_model_section_shown(KHDAYS_SECTION_MODS));
    khdays_plugins_shown_set(&shown);
    CHECK(khdays_model_section_shown(KHDAYS_SECTION_MODS));
    khdays_profile_default(&p);
    CHECK(p.mods.native_code == 0);
    n = khdays_model_rows(KHDAYS_SECTION_MODS, &p, &context, rows);
    CHECK(n == 5 && rows[0].id == KHDAYS_SET_NATIVE_CODE && rows[0].kind == KHDAYS_ROW_SWITCH);
    CHECK(rows[0].note == khdays_text(TXT_NATIVE_CODE_NOTE) && rows[0].takes == TXT_TAKES_NEXT_START);
    CHECK(strcmp(rows[1].label, "Spinner 1.0 -- khdays-port") == 0 && strcmp(rows[3].label, "Broken 2") == 0);
    for (int i = 0; i < n; ++i) {
        if (rows[i].id == KHDAYS_SET_PLUGIN_STATE) {
            ++lines;
            CHECK(i != 2 || strcmp(rows[i].label, khdays_text(TXT_PLUGIN_WAITING)) == 0);
            CHECK(i != 4 || strstr(rows[i].label, "its library cannot be loaded (none)") != NULL);
        }
    }
    CHECK(lines == 2);
    khdays_settings_open(&p);
    khdays_settings_draw(screen, NULL);
    to_section(KHDAYS_SECTION_MODS);
    shot("2-native-code");
    to_row(KHDAYS_SET_NATIVE_CODE, -1);
    key(SDL_SCANCODE_RETURN);
    CHECK(khdays_settings_profile()->mods.native_code == 1);
    khdays_settings_draw(screen, NULL);
    CHECK(key(SDL_SCANCODE_ESCAPE) == 0 && key(SDL_SCANCODE_ESCAPE) == 1);
    /* safe mode: the plugins wait for it, not for the setting */
    shown.safe_mode = 1;
    khdays_plugins_shown_set(&shown);
    khdays_model_rows(KHDAYS_SECTION_MODS, &p, &context, rows);
    CHECK(strcmp(rows[2].label, khdays_text(TXT_PLUGIN_SAFE_MODE)) == 0);
    shown.count = 0;
    khdays_plugins_shown_set(&shown);
    CHECK(!khdays_model_section_shown(KHDAYS_SECTION_MODS));
}

static void test_settings_mods(void)
{
    KhdaysProfile p;
    KhdaysModsView v;
    KhdaysModView mods[2];
    KhdaysOptionView options[4];
    char *values[3] = {"Slow", "Normal", "Fast"};
    KhdaysScriptsMessage msg;
    SDL_zero(v);
    SDL_zero(mods);
    SDL_zero(options);
    options[0].key = "hp";
    options[0].kind = KHDAYS_OPTION_TOGGLE;
    options[0].label[0] = "Infinite HP";
    options[1].key = "speed";
    options[1].kind = KHDAYS_OPTION_CHOICE;
    options[1].label[0] = "Speed";
    options[1].values = values;
    options[1].value_count = 3;
    options[1].value = 2;
    options[2].key = "boost";
    options[2].kind = KHDAYS_OPTION_NUMBER;
    options[2].label[0] = "Boost";
    options[2].low = 0;
    options[2].high = 10;
    options[2].step = 2;
    options[2].value = 4;
    options[3].key = "potion";
    options[3].kind = KHDAYS_OPTION_BUTTON;
    options[3].label[0] = "Give a potion";
    mods[0].folder = "First";
    mods[0].id = "test.first";
    mods[0].name = "First";
    mods[0].version = "1.0";
    mods[0].state = KHDAYS_MOD_LOADED;
    mods[0].enabled = 1;
    mods[0].options = options;
    mods[0].option_count = 4;
    mods[1].folder = "Second";
    mods[1].id = "test.second";
    mods[1].name = "Second";
    mods[1].version = "2";
    mods[1].state = KHDAYS_MOD_STOPPED;
    mods[1].reason = "scripts/main.lua:3: attempt to call a nil value";
    mods[1].enabled = 1;
    v.mods = mods;
    v.count = 2;
    while (khdays_scripts_take(&msg)) {
        free(msg.mod);
        free(msg.key);
    }
    CHECK(!khdays_model_section_shown(KHDAYS_SECTION_MODS));
    khdays_scripts_view_set(&v);
    CHECK(khdays_model_section_shown(KHDAYS_SECTION_MODS));
    khdays_profile_default(&p);
    khdays_settings_open(&p);
    khdays_settings_draw(screen, NULL);
    to_section(KHDAYS_SECTION_MODS);
    shot("2-mods");
    to_row(KHDAYS_SET_MOD_ENABLED, -1); /* the first mod's */
    key(SDL_SCANCODE_RETURN);
    CHECK(khdays_scripts_take(&msg) && msg.ask == KHDAYS_ASK_ENABLE && strcmp(msg.mod, "First") == 0 && msg.value == 0);
    free(msg.mod);
    key(SDL_SCANCODE_RETURN); /* on again */
    CHECK(khdays_scripts_take(&msg) && msg.ask == KHDAYS_ASK_ENABLE && msg.value == 1);
    free(msg.mod);
    to_row(KHDAYS_SET_MOD_OPTION, 0);
    key(SDL_SCANCODE_RETURN);
    CHECK(khdays_scripts_take(&msg) && msg.ask == KHDAYS_ASK_OPTION && strcmp(msg.mod, "test.first") == 0 &&
          strcmp(msg.key, "hp") == 0 && msg.value == 1);
    free(msg.mod);
    free(msg.key);
    to_row(KHDAYS_SET_MOD_OPTION, 1);
    key(SDL_SCANCODE_RIGHT);
    CHECK(khdays_scripts_take(&msg) && strcmp(msg.key, "speed") == 0 && msg.value == 3); /* 1-based */
    free(msg.mod);
    free(msg.key);
    to_row(KHDAYS_SET_MOD_OPTION, 2);
    key(SDL_SCANCODE_RIGHT);
    CHECK(khdays_scripts_take(&msg) && strcmp(msg.key, "boost") == 0 && msg.value == 6);
    free(msg.mod);
    free(msg.key);
    to_row(KHDAYS_SET_MOD_OPTION, 3);
    key(SDL_SCANCODE_RETURN);
    CHECK(khdays_scripts_take(&msg) && msg.ask == KHDAYS_ASK_BUTTON && strcmp(msg.key, "potion") == 0);
    free(msg.mod);
    free(msg.key);
    to_section(KHDAYS_SECTION_MODS); /* back to the top: the first mod's place */
    to_row(KHDAYS_SET_MOD_LATER, -1);
    key(SDL_SCANCODE_RETURN);
    CHECK(khdays_scripts_take(&msg) && msg.ask == KHDAYS_ASK_MOVE && strcmp(msg.mod, "First") == 0 && msg.value == 1);
    free(msg.mod);
    CHECK(!khdays_scripts_take(&msg));
    khdays_settings_draw(screen, NULL);
    CHECK(key(SDL_SCANCODE_ESCAPE) == 0 && key(SDL_SCANCODE_ESCAPE) == 1);
    /* no mod any more: no section */
    v.count = 0;
    khdays_scripts_view_set(&v);
    CHECK(!khdays_model_section_shown(KHDAYS_SECTION_MODS));
    test_settings_native_code();
}

static void test_settings_screen(void)
{
    KhdaysProfile p;
    int id, action;
    use_screen(1280, 720);
    if (screen == NULL) {
        return;
    }
    if (SDL_getenv("KHDAYS_TEST_SHOTS") != NULL) {
        khdays_ui_font_load();
    }
    khdays_profile_default(&p);
    khdays_devices_init(&p, capture);

    /* Every section opens, and every row of it draws. */
    khdays_settings_open(&p);
    CHECK(khdays_settings_is_open());
    for (int s = 0; s < KHDAYS_SECTION_COUNT; ++s) {
        char name[32];
        if (!khdays_model_section_shown(s)) {
            continue;
        }
        to_section(s);
        SDL_snprintf(name, sizeof(name), "1-section-%d", s);
        shot(name);
        for (int i = 0; i < KHDAYS_MODEL_ROWS_MAX; ++i) {
            key(SDL_SCANCODE_DOWN); /* walk every row */
        }
    }

    /* Bind D-pad Up on the keyboard to V (free), then its second cell to Space,
     * which B has: the menu asks, and "Move it here" moves it. */
    to_section(KHDAYS_SECTION_CONTROLS);
    to_row(KHDAYS_SET_BINDING, KHDAYS_ACT_UP);
    to_cell(0);
    key(SDL_SCANCODE_RETURN); /* D-pad Up, first cell: waits for a key */
    key(SDL_SCANCODE_V);
    CHECK(khdays_settings_profile()->input[KB][BASE][KHDAYS_ACT_UP][0] == SDL_SCANCODE_V);
    key(SDL_SCANCODE_RIGHT); /* second cell */
    key(SDL_SCANCODE_RETURN);
    key(SDL_SCANCODE_SPACE); /* B's: a question */
    shot("2-conflict");
    CHECK(khdays_settings_profile()->input[KB][BASE][KHDAYS_ACT_UP][1] == KHDAYS_NO_INPUT);
    key(SDL_SCANCODE_RETURN); /* "Move it here" */
    CHECK(khdays_settings_profile()->input[KB][BASE][KHDAYS_ACT_UP][1] == SDL_SCANCODE_SPACE);
    CHECK(khdays_settings_profile()->input[KB][BASE][KHDAYS_ACT_B][0] == KHDAYS_NO_INPUT);
    /* Esc while waiting for a key cancels. */
    key(SDL_SCANCODE_RETURN);
    key(SDL_SCANCODE_ESCAPE);
    CHECK(khdays_settings_profile()->input[KB][BASE][KHDAYS_ACT_UP][1] == SDL_SCANCODE_SPACE);
    CHECK(khdays_settings_is_open());

    /* B is now on nothing on the keyboard but still on the gamepad: closing
     * is fine. Clear the gamepad's too (its first cell), and closing asks
     * first. */
    to_row(KHDAYS_SET_BINDING, KHDAYS_ACT_B);
    to_cell(2);
    key(SDL_SCANCODE_DELETE);
    CHECK(khdays_settings_profile()->input[PAD][BASE][KHDAYS_ACT_B][0] == KHDAYS_NO_INPUT);
    key(SDL_SCANCODE_ESCAPE);             /* to the column */
    CHECK(key(SDL_SCANCODE_ESCAPE) == 0); /* "No input would press: B" */
    shot("3-unbound");
    CHECK(khdays_settings_is_open());
    CHECK(key(SDL_SCANCODE_RETURN) == 0); /* the safe answer, "Back", is chosen */
    CHECK(khdays_settings_is_open());

    /* Restore everything, then the KH II preset (every enhancement it uses
     * is built, so nothing falls back). */
    to_section(KHDAYS_SECTION_GENERAL);
    to_row(KHDAYS_SET_RESTORE_BOTH, -1);
    key(SDL_SCANCODE_RETURN);
    CHECK(memcmp(khdays_settings_profile(), &p, sizeof(p)) == 0);
    to_section(KHDAYS_SECTION_CONTROLS);
    to_row(KHDAYS_SET_PRESET_KH2, -1);
    key(SDL_SCANCODE_RETURN); /* Use the Kingdom Hearts II layout: a question */
    shot("4-preset-question");
    CHECK(khdays_settings_profile()->preset == KHDAYS_PRESET_DS);
    key(SDL_SCANCODE_RETURN); /* Apply */
    CHECK(khdays_settings_profile()->preset == KHDAYS_PRESET_KH2);
    CHECK(khdays_settings_profile()->input[PAD][BASE][KHDAYS_ACT_A][0] == KHDAYS_PAD_SOUTH);
    CHECK(khdays_settings_profile()->input[PAD][BASE][KHDAYS_ACT_LOCK_ON][0] == KHDAYS_PAD_RIGHTSHOULDER);
    CHECK(khdays_settings_profile()->input[PAD][BASE][KHDAYS_ACT_R][0] == KHDAYS_PAD_RIGHTSTICK);
    CHECK(khdays_settings_profile()->enh.separate_lock_on && khdays_settings_profile()->enh.analog_camera &&
          khdays_settings_profile()->enh.analog_movement && khdays_settings_profile()->enh.dpad_command_menu);
    /* now the lock-on and the command menu have their rows */
    to_row(KHDAYS_SET_BINDING, KHDAYS_ACT_MENU_LEFT);
    CHECK(focused(&id, &action) == KHDAYS_SECTION_CONTROLS && action == KHDAYS_ACT_MENU_LEFT);
    shot("5-controls-kh2");
    /* the test line says what a key does: Z is B's on the keyboard */
    key(SDL_SCANCODE_X);

    /* Closing now keeps the change. */
    key(SDL_SCANCODE_ESCAPE);
    CHECK(key(SDL_SCANCODE_ESCAPE) == 1);
    CHECK(!khdays_settings_is_open());
    CHECK(khdays_settings_changed());

    test_video_page();
    test_face_buttons();
    test_sound_page();
    test_network_page();
    test_settings_mouse();
    test_settings_changes();
    test_settings_language();
    test_settings_pad();
    test_settings_mods();
    khdays_settings_renderer_gone();
    SDL_DestroyRenderer(screen);
    SDL_DestroySurface(surface);
    screen = NULL;
}

/* The menu's accept and back buttons are the ones the player's DS A and B
 * are on: the right and bottom buttons with the DS's preset, the bottom and
 * right with KH II's (Cross accepts on a PlayStation pad). The shoulders move
 * between the sections. */
static void test_face_buttons(void)
{
    static const struct {
        int preset;
        SDL_GamepadButton accept, back;
    } cases[] = {{KHDAYS_PRESET_DS, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH},
                 {KHDAYS_PRESET_KH2, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST}};
    const KhdaysVideo *v = &khdays_settings_profile()->video;
    int id, action;
    khdays_settings_set_graphics_card(1);
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        KhdaysProfile p;
        khdays_profile_default(&p);
        khdays_profile_apply_preset(&p, cases[c].preset, ~0u);
        khdays_settings_open(&p);
        for (int i = 0; i < KHDAYS_SECTION_VIDEO; ++i) {
            pad_press(SDL_GAMEPAD_BUTTON_DPAD_DOWN); /* to Video */
        }
        CHECK(pad_press(cases[c].accept) == 0);  /* in */
        pad_press(SDL_GAMEPAD_BUTTON_DPAD_DOWN); /* past the display mode */
        pad_press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        CHECK(v->internal_resolution == 2); /* on the video section */
        pad_press(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        CHECK(focused(&id, &action) == KHDAYS_SECTION_SOUND && id == KHDAYS_SET_VOLUME);
        pad_press(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        CHECK(focused(&id, &action) == KHDAYS_SECTION_VIDEO && id == KHDAYS_SET_DISPLAY);
        CHECK(pad_press(cases[c].back) == 0); /* out */
        CHECK(pad_press(cases[c].back) == 1); /* and closed */
    }
}

static void test_video_page(void)
{
    KhdaysProfile p;
    KhdaysVideo defaults;
    const KhdaysVideo *v = &khdays_settings_profile()->video;
    khdays_profile_default(&p);
    khdays_video_default(&defaults);
    p.video.one_screen = 0; /* from the DS's two screens, so that each change of the screens shows */
    p.video.aspect = KHDAYS_ASPECT_4_3;

    khdays_settings_set_graphics_card(1);
    khdays_settings_open(&p);
    to_section(KHDAYS_SECTION_VIDEO);
    shot("6-video");
    key(SDL_SCANCODE_RIGHT); /* the display mode: borderless, full screen, a window again */
    CHECK(v->display == KHDAYS_DISPLAY_BORDERLESS);
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->display == KHDAYS_DISPLAY_FULLSCREEN);
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->display == KHDAYS_DISPLAY_WINDOW);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->internal_resolution == 2);
    for (int i = 0; i < 15; ++i) {
        key(SDL_SCANCODE_RIGHT); /* 3 to 16, then as the window allows */
    }
    CHECK(v->internal_resolution == KHDAYS_RESOLUTION_WINDOW);
    key(SDL_SCANCODE_RIGHT); /* round again: 2x, as 1x is not offered */
    CHECK(v->internal_resolution == 2);
    key(SDL_SCANCODE_LEFT);
    key(SDL_SCANCODE_LEFT);
    CHECK(v->internal_resolution == 16);
    key(SDL_SCANCODE_DOWN); /* the antialiasing: off, 2x, 3x, 4x, and round */
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->antialiasing == 2);
    key(SDL_SCANCODE_LEFT);
    key(SDL_SCANCODE_LEFT);
    CHECK(v->antialiasing == 4);
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->antialiasing == 1);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->aspect == KHDAYS_ASPECT_16_9);
    /* the screens: one, the aspect kept (it is not 4:3); back to 4:3 and one again
     * gives 16:9; then two */
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->one_screen == 1 && v->aspect == KHDAYS_ASPECT_16_9);
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->one_screen == 0);
    key(SDL_SCANCODE_UP);
    key(SDL_SCANCODE_LEFT);
    CHECK(v->aspect == KHDAYS_ASPECT_4_3);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->one_screen == 1 && v->aspect == KHDAYS_ASPECT_16_9);
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->one_screen == 0);
    key(SDL_SCANCODE_DOWN); /* the HUD's size, by one */
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->hud_size == 5);
    key(SDL_SCANCODE_LEFT);
    key(SDL_SCANCODE_LEFT);
    CHECK(v->hud_size == 3);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RETURN);
    CHECK(v->smooth_scaling == 1);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RETURN);
    CHECK(v->texture_smoothing == 1);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RETURN); /* HD graphics: not there yet, stays as it is */
    CHECK(v->hd_graphics == 1);
    shot("7-video-changed");

    /* Restore the video settings: only those. */
    to_row(KHDAYS_SET_RESTORE_VIDEO, -1);
    key(SDL_SCANCODE_RETURN);
    CHECK(memcmp(v, &defaults, sizeof(defaults)) == 0);
    key(SDL_SCANCODE_ESCAPE);
    CHECK(key(SDL_SCANCODE_ESCAPE) == 1);

    /* Without the graphics card what needs it cannot be changed; the scaling
     * still can. */
    khdays_settings_set_graphics_card(0);
    khdays_settings_open(&p);
    to_section(KHDAYS_SECTION_VIDEO);
    shot("8-video-no-card");
    key(SDL_SCANCODE_DOWN); /* past the display mode, which needs no card */
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->internal_resolution == KHDAYS_RESOLUTION_WINDOW);
    key(SDL_SCANCODE_DOWN); /* nor the antialiasing */
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->antialiasing == 1);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_DOWN); /* the aspect cannot be chosen either, nor the screens: on to scaling */
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->one_screen == 0);
    key(SDL_SCANCODE_DOWN); /* nor the HUD's size */
    key(SDL_SCANCODE_RIGHT);
    CHECK(v->hud_size == 4);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RETURN);
    CHECK(v->aspect == KHDAYS_ASPECT_4_3);
    CHECK(v->smooth_scaling == 1);
    key(SDL_SCANCODE_ESCAPE);
    CHECK(key(SDL_SCANCODE_ESCAPE) == 1);
    khdays_settings_set_graphics_card(1);
}

static int key_repeat(SDL_Scancode code)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = code;
    e.key.repeat = true;
    return khdays_settings_handle(&e, NULL);
}

static void test_sound_page(void)
{
    KhdaysProfile p;
    const KhdaysSound *s = &khdays_settings_profile()->sound;
    khdays_profile_default(&p);
    khdays_settings_open(&p);
    to_section(KHDAYS_SECTION_SOUND);
    shot("9-sound");
    /* Each slider moves by 5 within 0-100; a held key repeats. */
    key(SDL_SCANCODE_LEFT);
    CHECK(s->volume == 95);
    key_repeat(SDL_SCANCODE_LEFT);
    key_repeat(SDL_SCANCODE_LEFT);
    CHECK(s->volume == 85);
    for (int i = 0; i < 30; ++i) {
        key_repeat(SDL_SCANCODE_LEFT);
    }
    CHECK(s->volume == 0);
    key(SDL_SCANCODE_RIGHT);
    CHECK(s->volume == 5);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_LEFT);
    CHECK(s->music == 95);
    key(SDL_SCANCODE_RIGHT);
    key(SDL_SCANCODE_RIGHT);
    CHECK(s->music == 100);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_LEFT);
    CHECK(s->effects == 95);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_LEFT);
    key(SDL_SCANCODE_LEFT);
    CHECK(s->voices == 90);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_LEFT);
    CHECK(s->movies == 95); /* the movies' sound */
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RETURN);
    CHECK(s->ds_exact == 1);
    shot("10-sound-changed");
    /* Restore the sound settings: 100, 100, 100, 100 and off; nothing else. */
    to_row(KHDAYS_SET_RESTORE_SOUND, -1);
    key(SDL_SCANCODE_RETURN);
    CHECK(memcmp(s, &p.sound, sizeof(p.sound)) == 0);
    CHECK(memcmp(khdays_settings_profile(), &p, sizeof(p)) == 0);
    key(SDL_SCANCODE_ESCAPE);
    CHECK(key(SDL_SCANCODE_ESCAPE) == 1);
}

/* Typed text, as the system hands it to the menu. */
static void type_text(const char *text)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = text;
    handle(&e);
}

/* The multiplayer section: the nickname typed, only what the
 * game can show and as much as it holds, kept with Enter, left with Esc; the
 * server's address beside online play. */
static void test_network_page(void)
{
    KhdaysProfile p;
    const KhdaysNetwork *n = &khdays_settings_profile()->network;
    khdays_profile_default(&p);
    khdays_settings_open(&p);
    to_section(KHDAYS_SECTION_MULTIPLAYER);
    shot("11-network");
    key(SDL_SCANCODE_RETURN); /* the nickname: typing */
    type_text("Xion");
    type_text("\xc3\x91!"); /* an N with a tilde the game's font is not known to have, and a "!" */
    shot("12-nickname-typing");
    CHECK(n->nickname[0] == '\0'); /* nothing kept before Enter */
    key(SDL_SCANCODE_RETURN);
    CHECK(strcmp(n->nickname, "Xion!") == 0);
    key(SDL_SCANCODE_RETURN);
    type_text("-and-more-than-ten");
    key(SDL_SCANCODE_RETURN);
    CHECK(strcmp(n->nickname, "Xion!-and-") == 0);
    key(SDL_SCANCODE_RETURN);
    key(SDL_SCANCODE_BACKSPACE);
    key(SDL_SCANCODE_BACKSPACE);
    key(SDL_SCANCODE_ESCAPE); /* left as it was */
    CHECK(strcmp(n->nickname, "Xion!-and-") == 0);
    /* online off: its server cannot be typed in */
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RETURN);
    CHECK(!n->online);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RETURN);
    type_text("x");
    CHECK(strcmp(n->server, "212.227.76.27") == 0);
    key(SDL_SCANCODE_UP);
    key(SDL_SCANCODE_RETURN);
    CHECK(n->online);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RETURN);
    for (int i = 0; i < 13; ++i) {
        key(SDL_SCANCODE_BACKSPACE);
    }
    type_text("my server.example:35821"); /* the space is no address's */
    key(SDL_SCANCODE_RETURN);
    CHECK(strcmp(n->server, "myserver.example:35821") == 0);
    shot("13-network-changed");
    key(SDL_SCANCODE_ESCAPE);
    CHECK(key(SDL_SCANCODE_ESCAPE) == 1);
}

/* The mouse alone: a section's entry chooses it, a number's value's right
 * half raises it and its left half lowers it, a switch's row turns it over, a
 * binding's cell waits for an input, the right button goes back, Close closes. */
static void test_settings_mouse(void)
{
    KhdaysProfile p;
    const KhdaysProfile *w = khdays_settings_profile();
    SDL_FRect r;
    int id = 0, action = 0, hud = -1, fps = -1, up = -1;
    khdays_profile_default(&p);
    khdays_settings_set_graphics_card(1);
    khdays_settings_open(&p);
    khdays_settings_draw(screen, NULL);
    click(KHDAYS_PLACE_ENTRY, KHDAYS_SECTION_VIDEO);
    CHECK(focused(&id, &action) == KHDAYS_SECTION_VIDEO);
    for (int row = 0; khdays_settings_row_info(row, &id, &action); ++row) {
        hud = id == KHDAYS_SET_HUD_SIZE ? row : hud;
        fps = id == KHDAYS_SET_SHOW_FPS ? row : fps;
    }
    CHECK(khdays_settings_place(KHDAYS_PLACE_VALUE, hud, &r));
    click_at(r.x + r.w * 0.9f, r.y + r.h / 2, SDL_BUTTON_LEFT);
    CHECK(w->video.hud_size == p.video.hud_size + 1);
    click_at(r.x + r.w * 0.1f, r.y + r.h / 2, SDL_BUTTON_LEFT);
    click_at(r.x + r.w * 0.1f, r.y + r.h / 2, SDL_BUTTON_LEFT);
    CHECK(w->video.hud_size == p.video.hud_size - 1);
    click(KHDAYS_PLACE_ROW, fps);
    CHECK(w->video.show_fps == !p.video.show_fps);
    /* a binding's cell: the keyboard's first input of D-pad Up */
    click(KHDAYS_PLACE_ENTRY, KHDAYS_SECTION_CONTROLS);
    for (int row = 0; khdays_settings_row_info(row, &id, &action); ++row) {
        up = id == KHDAYS_SET_BINDING && action == KHDAYS_ACT_UP ? row : up;
    }
    click(KHDAYS_PLACE_CELL, up * KHDAYS_CELLS + 0);
    key(SDL_SCANCODE_W);
    CHECK(w->input[KB][BASE][KHDAYS_ACT_UP][0] == SDL_SCANCODE_W);
    /* the right button: from the rows to the column; the column's last entry asks whether to quit the
     * game (Cancel stays in the menu); the right button again leaves the menu */
    click_at(1, 1, SDL_BUTTON_RIGHT);
    CHECK(click(KHDAYS_PLACE_ENTRY, KHDAYS_SECTION_COUNT) == 0 && khdays_settings_is_open());
    khdays_settings_draw(screen, NULL); /* the question, its answers placed */
    CHECK(click(KHDAYS_PLACE_ANSWER, 1) == 0 && khdays_settings_is_open());
    khdays_settings_draw(screen, NULL);
    CHECK(click_at(1, 1, SDL_BUTTON_RIGHT) == 1);
    CHECK(!khdays_settings_is_open() && khdays_settings_changed());
}

/* The menu's own words are in the language chosen as soon as it is
 * chosen (KHDAYS_LANGUAGE, when set, wins: then they stay in its). */
static void test_settings_language(void)
{
    KhdaysProfile p;
    const int fixed = khdays_language_from_code(SDL_getenv("KHDAYS_LANGUAGE"));
    khdays_profile_default(&p);
    p.language = KHDAYS_LANGUAGE_ENGLISH;
    khdays_settings_open(&p);
    to_section(KHDAYS_SECTION_LANGUAGE);
    for (int l = KHDAYS_LANGUAGE_ENGLISH + 1; l <= KHDAYS_LANGUAGE_LAST; ++l) {
        char name[32];
        key(SDL_SCANCODE_RIGHT);
        CHECK(khdays_settings_profile()->language == l);
        CHECK(strcmp(khdays_model_section_title(KHDAYS_SECTION_CONTROLS),
                     khdays_text_in(TXT_CONTROLS, fixed != 0 ? fixed : l)) == 0);
        SDL_snprintf(name, sizeof(name), "14-language-%d", l);
        shot(name);
    }
    key(SDL_SCANCODE_ESCAPE);
    CHECK(key(SDL_SCANCODE_ESCAPE) == 1);
    CHECK(khdays_settings_changed());
}

/* The gamepad drawn in Controls, in the shape of the gamepad's family
 * (SDL's virtual gamepads with a PlayStation's, an Xbox's and a Switch Pro's
 * vendor and product), or the generic one with none. */
static void test_settings_pad(void)
{
    static const struct {
        Uint16 vendor, product;
        int family;
    } pads[] = {
        {0, 0, KHDAYS_PAD_GENERIC},
        {0x054c, 0x0ce6, KHDAYS_PAD_PLAYSTATION}, /* DualSense */
        {0x045e, 0x0b12, KHDAYS_PAD_XBOX},        /* Xbox Series */
        {0x057e, 0x2009, KHDAYS_PAD_NINTENDO},    /* Switch Pro */
    };
    static const char *const real_drivers[] = {
        SDL_HINT_JOYSTICK_HIDAPI,      SDL_HINT_JOYSTICK_RAWINPUT, SDL_HINT_XINPUT_ENABLED,
        SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_WGI,      SDL_HINT_JOYSTICK_GAMEINPUT,
    };
    KhdaysProfile p;
    khdays_profile_default(&p);
    /* only SDL's virtual gamepads, as in test_devices */
    for (size_t i = 0; i < sizeof(real_drivers) / sizeof(real_drivers[0]); ++i) {
        SDL_SetHint(real_drivers[i], "0");
    }
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        CHECK(!"the gamepads' subsystem starts");
        return;
    }
    khdays_devices_init(&p, capture);
    for (size_t i = 0; i < sizeof(pads) / sizeof(pads[0]); ++i) {
        SDL_JoystickID id = 0;
        int found = 0;
        char name[32];
        if (pads[i].vendor != 0) {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.vendor_id = pads[i].vendor;
            desc.product_id = pads[i].product;
            desc.name = "a virtual gamepad";
            id = SDL_AttachVirtualJoystick(&desc);
            pump();
        }
        khdays_settings_open(&p);
        to_section(KHDAYS_SECTION_CONTROLS);
        CHECK(khdays_settings_pad_family() == pads[i].family);
        for (int row = 0; khdays_settings_row_info(row, &(int){0}, &(int){0}); ++row) {
            int row_id, action;
            khdays_settings_row_info(row, &row_id, &action);
            if (row_id == KHDAYS_SET_PAD_VIEW) {
                SDL_FRect r;
                found = 1;
                CHECK(khdays_settings_place(KHDAYS_PLACE_ROW, row, &r) && r.h > 0);
            }
        }
        CHECK(found);
        SDL_snprintf(name, sizeof(name), "15-pad-%d", pads[i].family);
        shot(name);
        key(SDL_SCANCODE_ESCAPE);
        CHECK(key(SDL_SCANCODE_ESCAPE) == 1);
        if (id != 0) {
            SDL_DetachVirtualJoystick(id);
            pump();
        }
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

/* Each change is reported once, by its part of the profile. */
static void test_settings_changes(void)
{
    KhdaysProfile p;
    khdays_profile_default(&p);
    khdays_settings_open(&p);
    CHECK(khdays_settings_take_changes() == 0);
    to_section(KHDAYS_SECTION_SOUND);
    key(SDL_SCANCODE_LEFT);
    CHECK(khdays_settings_take_changes() == KHDAYS_CHANGE_SOUND);
    CHECK(khdays_settings_take_changes() == 0);
    to_section(KHDAYS_SECTION_VIDEO);
    key(SDL_SCANCODE_RIGHT);
    CHECK(khdays_settings_take_changes() == KHDAYS_CHANGE_VIDEO);
    to_section(KHDAYS_SECTION_LANGUAGE);
    key(SDL_SCANCODE_RIGHT);
    CHECK(khdays_settings_take_changes() == KHDAYS_CHANGE_LANGUAGE);
    to_section(KHDAYS_SECTION_GENERAL);
    key(SDL_SCANCODE_RETURN); /* the autosave: read by the save lists when the game goes on, no live change */
    CHECK(khdays_settings_take_changes() == 0);
    to_row(KHDAYS_SET_SESSION_LOG, -1);
    key(SDL_SCANCODE_RETURN);
    CHECK(khdays_settings_take_changes() == KHDAYS_CHANGE_LOG);
    to_section(KHDAYS_SECTION_ENHANCEMENTS);
    key(SDL_SCANCODE_RETURN);
    CHECK(khdays_settings_take_changes() == KHDAYS_CHANGE_CONTROLS);
    to_section(KHDAYS_SECTION_MULTIPLAYER);
    key(SDL_SCANCODE_DOWN);
    key(SDL_SCANCODE_RETURN);
    CHECK(khdays_settings_take_changes() == KHDAYS_CHANGE_NETWORK);
    {
        /* what the menu changed is kept in the file as it is */
        char path[512];
        KhdaysProfile back;
        path_of("menu_live.ini", path, sizeof(path));
        CHECK(khdays_profile_save(khdays_settings_profile(), path, count_notice) == 0);
        khdays_profile_load(&back, path, count_notice);
        CHECK(memcmp(&back, khdays_settings_profile(), sizeof(back)) == 0);
    }
    to_section(KHDAYS_SECTION_GENERAL);
    to_row(KHDAYS_SET_RESTORE_BOTH, -1);
    key(SDL_SCANCODE_RETURN);
    CHECK(khdays_settings_take_changes() == (KHDAYS_CHANGE_SOUND | KHDAYS_CHANGE_VIDEO | KHDAYS_CHANGE_LANGUAGE |
                                             KHDAYS_CHANGE_LOG | KHDAYS_CHANGE_CONTROLS | KHDAYS_CHANGE_NETWORK));
    key(SDL_SCANCODE_ESCAPE);
    CHECK(key(SDL_SCANCODE_ESCAPE) == 1);
    CHECK(!khdays_settings_changed());
}

/* The game's own heading from the D-pad bits, as Ov022_HeadingFromHeldDpad makes
 * it (a copy of its arithmetic, to hold analog movement to it). */
static int game_heading(unsigned f)
{
    int r = -1, cnt = 0;
    if (f & 0x40) {
        r = 0;
        cnt++;
    } else if (f & 0x80) {
        cnt++;
        r = 0x8000;
    }
    if (f & 0x20) {
        r += 0x4000;
        cnt++;
    } else if (f & 0x10) {
        r += (f & 0x40) ? 0x1c000 : 0xc000;
        cnt++;
    }
    if (cnt > 1) {
        r >>= 1;
    }
    return r;
}

/* The stick's heading in the game's convention, exactly the
 * game's own on the eight D-pad directions; walk below the run threshold,
 * never faster than the game. */
static void test_analog(void)
{
    enum { RIGHT = 0x10, LEFT = 0x20, UP = 0x40, DOWN = 0x80, FULL = 32767 };
    static const struct {
        int x, y;
        unsigned dpad;
    } directions[] = {
        {0, FULL, UP},    {-FULL, FULL, UP | LEFT},    {-FULL, 0, LEFT}, {-FULL, -FULL, DOWN | LEFT},
        {0, -FULL, DOWN}, {FULL, -FULL, DOWN | RIGHT}, {FULL, 0, RIGHT}, {FULL, FULL, UP | RIGHT},
    };
    for (size_t i = 0; i < sizeof(directions) / sizeof(directions[0]); ++i) {
        CHECK(khdays_analog_heading(directions[i].x, directions[i].y) == game_heading(directions[i].dpad));
    }
    CHECK(khdays_analog_heading(0, 0) == -1);
    /* In between: 30 degrees left of up is a twelfth of a turn. */
    CHECK(khdays_analog_heading(-16384, 28378) == 0x1555);
    /* 30 degrees right of up. */
    CHECK(khdays_analog_heading(16384, 28378) == 0x10000 - 0x1555);
    /* Flip Foot: the game swapped the held Up/Down and Left/Right, so the stick turns around too;
     * the D-pad's own eighth of the stick's angle leaves it as it is */
    for (size_t i = 0; i < sizeof(directions) / sizeof(directions[0]); ++i) {
        const unsigned d = directions[i].dpad;
        const unsigned swapped =
            (d & UP ? DOWN : 0) | (d & DOWN ? UP : 0) | (d & LEFT ? RIGHT : 0) | (d & RIGHT ? LEFT : 0);
        const int stick = khdays_analog_heading(directions[i].x, directions[i].y);
        CHECK(khdays_analog_heading_turned(stick, game_heading(d)) == stick);
        CHECK(khdays_analog_heading_turned(stick, game_heading(swapped)) == game_heading(swapped));
    }
    CHECK(khdays_analog_heading_turned(0x1555, game_heading(UP | LEFT)) == 0x1555);
    CHECK(khdays_analog_heading_turned(0x1555, game_heading(UP)) == 0x1555);
    CHECK(khdays_analog_heading_turned(0x1555, game_heading(DOWN | RIGHT)) == 0x9555);
    CHECK(khdays_analog_heading_turned(0x1555, game_heading(DOWN)) == 0x9555);

    CHECK(khdays_analog_tilt(0, 0) == 0);
    CHECK(khdays_analog_tilt(FULL, 0) == 100);
    CHECK(khdays_analog_tilt(FULL, FULL) == 100); /* a square gate's corner */
    CHECK(khdays_analog_tilt(16384, 0) == 50);

    CHECK(khdays_analog_rate(0x35e, 100, 75, 50) == 0x35e);
    CHECK(khdays_analog_rate(0x35e, 75, 75, 50) == 0x35e);
    CHECK(khdays_analog_rate(0x35e, 74, 75, 50) == 0x35e * 50 / 100);
    CHECK(khdays_analog_rate(0x35e, 40, 75, 20) == 0x35e * 20 / 100);
    CHECK(khdays_analog_rate(0x35e, 40, 75, 150) == 0x35e); /* never faster */
}

/* The analog axes: only while their enhancement is on, y up, the camera
 * rescaled past its dead zone, the movement stick's own position. */
static void test_analog_axes(void)
{
    KhdaysProfile p;
    KhdaysDeviceState s;
    KhdaysInputSnapshot out;
    khdays_profile_default(&p);
    khdays_bindings_reset(&s);
    p.stick[1].role = KHDAYS_STICK_CAMERA;
    s.stick[0][0] = 16384; /* left stick: half right */
    s.stick[1][0] = 32767; /* right stick: full right */
    out = resolve(&p, &s);
    CHECK(out.move_x == 0 && out.move_y == 0 && out.camera_x == 0 && out.camera_y == 0);
    CHECK(out.ds_buttons == DS(KHDAYS_ACT_RIGHT));

    p.enh.analog_movement = p.enh.analog_camera = 1;
    out = resolve(&p, &s);
    CHECK(out.move_x == 16384 && out.move_y == 0);
    CHECK(out.ds_buttons == DS(KHDAYS_ACT_RIGHT)); /* the D-pad bits as before */
    CHECK(out.camera_x == 32767 && out.camera_y == 0);
    CHECK((out.enhancements & (KHDAYS_ENH_ANALOG_MOVEMENT | KHDAYS_ENH_ANALOG_CAMERA)) ==
          (KHDAYS_ENH_ANALOG_MOVEMENT | KHDAYS_ENH_ANALOG_CAMERA));
    s.stick[1][0] = 0;
    s.stick[1][1] = -32767; /* full up (SDL's y grows downwards) */
    out = resolve(&p, &s);
    CHECK(out.camera_x == 0 && out.camera_y == 32767);
    s.stick[1][0] = 32767; /* a full diagonal keeps the stick's full reach */
    out = resolve(&p, &s);
    CHECK(out.camera_x > 23000 && out.camera_x < 23400 && out.camera_y > 23000 && out.camera_y < 23400);

    p.enh.camera_invert_x = 1;
    out = resolve(&p, &s);
    CHECK(out.camera_x < -23000 && out.camera_y > 23000);

    s.stick[1][0] = 9000; /* 27 %: inside the camera's 30 % dead zone */
    s.stick[1][1] = 0;
    s.stick[0][0] = 6000; /* 18 %: no direction, so no movement */
    out = resolve(&p, &s);
    CHECK(out.camera_x == 0 && out.camera_y == 0);
    CHECK(out.move_x == 0 && out.move_y == 0 && out.ds_buttons == 0);
}

static KhdaysMenuFrame menu_frame(KhdaysMenuSequencer *s, int direction, int scheme, uint16_t held)
{
    return khdays_menu_sequencer_frame(s, direction >= 0 ? 1u << direction : 0, 1, scheme, 0, held);
}

/* The D-pad's command menu is the DS's own presses for it. */
static void test_menu_sequencer(void)
{
    const uint16_t X = DS(KHDAYS_ACT_X), DOWN = DS(KHDAYS_ACT_DOWN), UP = DS(KHDAYS_ACT_UP);
    KhdaysMenuSequencer s;
    KhdaysMenuFrame f;

    /* Option 6 = 0: X alone, then X with the direction, then let go. */
    khdays_menu_sequencer_reset(&s);
    f = menu_frame(&s, KHDAYS_MENU_DOWN, 0, 0);
    CHECK(f.buttons == X && f.dpad_own);
    f = menu_frame(&s, KHDAYS_MENU_DOWN, 0, 0); /* still held: no second press */
    CHECK(f.buttons == (X | DOWN) && f.dpad_own);
    f = menu_frame(&s, KHDAYS_MENU_DOWN, 0, 0);
    CHECK(f.buttons == 0 && !f.dpad_own);
    f = menu_frame(&s, KHDAYS_MENU_DOWN, 0, 0);
    CHECK(f.buttons == 0);

    /* Option 6 = 1: a press of X moves down; up has no DS press. */
    khdays_menu_sequencer_reset(&s);
    f = menu_frame(&s, KHDAYS_MENU_DOWN, 1, 0);
    CHECK(f.buttons == X);
    f = menu_frame(&s, -1, 1, 0);
    CHECK(f.buttons == 0);
    f = menu_frame(&s, KHDAYS_MENU_UP, 1, 0);
    CHECK(f.buttons == 0 && !f.dpad_own);
    CHECK(!khdays_menu_direction_exists(KHDAYS_MENU_UP, 1) && khdays_menu_direction_exists(KHDAYS_MENU_UP, 0));

    /* A second press waits for the first sequence. */
    khdays_menu_sequencer_reset(&s);
    menu_frame(&s, KHDAYS_MENU_DOWN, 0, 0);
    f = khdays_menu_sequencer_frame(&s, 1u << KHDAYS_MENU_UP, 1, 0, 0, 0);
    CHECK(f.buttons == (X | DOWN));
    f = menu_frame(&s, -1, 0, 0);
    CHECK(f.buttons == 0);
    f = menu_frame(&s, -1, 0, 0);
    CHECK(f.buttons == X);
    f = menu_frame(&s, -1, 0, 0);
    CHECK(f.buttons == (X | UP));

    /* The player's own X, or L (a shortcut with option 0 = 0), holds it back. */
    khdays_menu_sequencer_reset(&s);
    f = menu_frame(&s, KHDAYS_MENU_DOWN, 0, X);
    CHECK(f.buttons == 0);
    f = menu_frame(&s, KHDAYS_MENU_DOWN, 0, DS(KHDAYS_ACT_L));
    CHECK(f.buttons == 0);
    f = menu_frame(&s, KHDAYS_MENU_DOWN, 0, 0);
    CHECK(f.buttons == X);

    /* Out of field control nothing plays, and a sequence stops. */
    khdays_menu_sequencer_reset(&s);
    f = khdays_menu_sequencer_frame(&s, 1u << KHDAYS_MENU_DOWN, 0, 0, 0, 0);
    CHECK(f.buttons == 0);
    khdays_menu_sequencer_reset(&s);
    menu_frame(&s, KHDAYS_MENU_DOWN, 0, 0);
    f = khdays_menu_sequencer_frame(&s, 1u << KHDAYS_MENU_DOWN, 0, 0, 0, 0);
    CHECK(f.buttons == 0);
    f = menu_frame(&s, KHDAYS_MENU_DOWN, 0, 0);
    CHECK(f.buttons == 0); /* held all along: not a new press */
}

/* Lock-on on its own buttons is the DS's own R presses for it. */
static uint16_t lock_frame(KhdaysLockSequencer *s, uint32_t actions, int locked, int scheme, uint16_t held, int r_free)
{
    return khdays_lock_sequencer_frame(s, actions, 1, locked, scheme, held, r_free);
}

static void test_lock_sequencer(void)
{
    const uint16_t R = DS(KHDAYS_ACT_R), L = DS(KHDAYS_ACT_L);
    const uint32_t on = 1u << KHDAYS_LOCK_ON, next = 1u << KHDAYS_LOCK_NEXT, prev = 1u << KHDAYS_LOCK_PREV;
    KhdaysLockSequencer s;

    /* Lock on (or let go): R pressed twice within the game's 9 frames. */
    khdays_lock_sequencer_reset(&s);
    CHECK(lock_frame(&s, on, 0, 0, 0, 1) == R);
    CHECK(lock_frame(&s, on, 0, 0, 0, 0) == 0);
    CHECK(lock_frame(&s, on, 0, 0, 0, 0) == R); /* the second R does not wait: it must be quick */
    CHECK(lock_frame(&s, on, 0, 0, 0, 0) == 0);
    CHECK(lock_frame(&s, on, 1, 0, 0, 1) == 0); /* held all along: once */

    /* Next target: one R, only with a target locked. */
    khdays_lock_sequencer_reset(&s);
    CHECK(lock_frame(&s, next, 0, 0, 0, 1) == 0);
    CHECK(lock_frame(&s, 0, 1, 0, 0, 1) == 0);
    CHECK(lock_frame(&s, next, 1, 0, 0, 1) == R);
    CHECK(lock_frame(&s, next, 1, 0, 0, 1) == 0);

    /* Switching quickly: the second R waits until the game would not take
     * it as the double press that lets go. */
    khdays_lock_sequencer_reset(&s);
    CHECK(lock_frame(&s, next, 1, 0, 0, 1) == R);
    CHECK(lock_frame(&s, 0, 1, 0, 0, 0) == 0);
    CHECK(lock_frame(&s, next, 1, 0, 0, 0) == 0); /* pressed again: waits */
    CHECK(lock_frame(&s, 0, 1, 0, 0, 0) == 0);
    CHECK(lock_frame(&s, 0, 1, 0, 0, 1) == R); /* the game's window has passed */
    CHECK(lock_frame(&s, 0, 1, 0, 0, 1) == 0);
    /* Locking on waits for the window too. */
    khdays_lock_sequencer_reset(&s);
    CHECK(lock_frame(&s, on, 0, 0, 0, 0) == 0);
    CHECK(lock_frame(&s, on, 0, 0, 0, 1) == R);

    /* Previous target: L tapped, with Type B only. */
    khdays_lock_sequencer_reset(&s);
    CHECK(lock_frame(&s, prev, 1, 0, 0, 1) == 0);
    CHECK(!khdays_lock_action_exists(KHDAYS_LOCK_PREV, 0) && khdays_lock_action_exists(KHDAYS_LOCK_PREV, 1));
    khdays_lock_sequencer_reset(&s);
    CHECK(lock_frame(&s, prev, 1, 1, 0, 0) == L); /* L is not R: no window to wait for */

    /* The player's own R holds it back; out of the field the sequence stops
     * and the buttons are the DS's R and L while held (a tutorial's pages). */
    khdays_lock_sequencer_reset(&s);
    CHECK(lock_frame(&s, on, 0, 0, R, 1) == 0);
    CHECK(lock_frame(&s, on, 0, 0, 0, 1) == R);
    CHECK(khdays_lock_sequencer_frame(&s, on, 0, 0, 0, 0, 1) == R);
    CHECK(khdays_lock_sequencer_frame(&s, on, 0, 0, 0, 0, 1) == R); /* held */
    CHECK(khdays_lock_sequencer_frame(&s, 0, 0, 0, 0, 0, 1) == 0);  /* let go */
    CHECK(khdays_lock_sequencer_frame(&s, next, 0, 0, 0, 0, 1) == R);
    CHECK(khdays_lock_sequencer_frame(&s, prev, 0, 0, 1, 0, 1) == L);
    CHECK(lock_frame(&s, 0, 0, 0, 0, 1) == 0);
}

/* The KH II preset, and its fallbacks. */
static void test_presets(void)
{
    const unsigned all = KHDAYS_ENH_ANALOG_CAMERA | KHDAYS_ENH_SEPARATE_LOCK_ON | KHDAYS_ENH_DPAD_COMMAND_MENU |
                         KHDAYS_ENH_ANALOG_MOVEMENT;
    enum { FIELD = KHDAYS_LAYER_FIELD };
    KhdaysProfile p, defaults;
    khdays_profile_default(&defaults);
    p = defaults;
    CHECK(khdays_profile_apply_preset(&p, KHDAYS_PRESET_KH2, all) == 0);
    CHECK(p.preset == KHDAYS_PRESET_KH2);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_A) == KHDAYS_PAD_SOUTH);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_B) == KHDAYS_PAD_EAST);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_Y) == KHDAYS_PAD_WEST);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_X) == KHDAYS_PAD_NORTH);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_L) == KHDAYS_PAD_LEFTSHOULDER);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_START) == KHDAYS_PAD_START);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_SELECT) == KHDAYS_PAD_BACK);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_LOCK_ON) == KHDAYS_PAD_RIGHTSHOULDER);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_TARGET_NEXT) == KHDAYS_PAD_RIGHTTRIGGER);
    CHECK(khdays_profile_owner(&p, PAD, BASE, KHDAYS_PAD_RIGHTSTICK) == KHDAYS_ACT_R); /* the camera's reset */
    CHECK(khdays_profile_owner(&p, PAD, BASE, KHDAYS_PAD_LEFTTRIGGER) < 0);
    CHECK(khdays_profile_owner(&p, PAD, BASE, KHDAYS_PAD_RIGHTSHOULDER) == KHDAYS_ACT_LOCK_ON);
    CHECK(p.input[PAD][FIELD][KHDAYS_ACT_MENU_UP][0] == KHDAYS_PAD_DPUP);
    CHECK(p.input[PAD][FIELD][KHDAYS_ACT_MENU_RIGHT][0] == KHDAYS_PAD_DPRIGHT);
    CHECK(first_input(&p, PAD, KHDAYS_ACT_UP) == KHDAYS_PAD_DPUP); /* the D-pad outside field control */
    CHECK(p.stick[0].role == KHDAYS_STICK_DPAD && p.stick[1].role == KHDAYS_STICK_CAMERA);
    CHECK(p.enh.analog_camera && p.enh.separate_lock_on && p.enh.dpad_command_menu && p.enh.analog_movement);
    CHECK(memcmp(p.input[KB], defaults.input[KB], sizeof(p.input[KB])) == 0); /* the keyboard is untouched */
    CHECK(khdays_profile_matches_preset(&p, all));
    khdays_profile_set(&p, PAD, BASE, KHDAYS_ACT_X, 0, KHDAYS_PAD_LEFTTRIGGER);
    CHECK(!khdays_profile_matches_preset(&p, all));

    /* Without separate lock-on, R1 (and R3) are the DS's R, and the screen
     * is told. */
    p = defaults;
    CHECK(khdays_profile_apply_preset(&p, KHDAYS_PRESET_KH2, all & ~KHDAYS_ENH_SEPARATE_LOCK_ON) ==
          KHDAYS_ENH_SEPARATE_LOCK_ON);
    CHECK(p.input[PAD][BASE][KHDAYS_ACT_R][0] == KHDAYS_PAD_RIGHTSHOULDER);
    CHECK(p.input[PAD][BASE][KHDAYS_ACT_R][1] == KHDAYS_PAD_RIGHTSTICK);
    CHECK(!p.enh.separate_lock_on && p.enh.analog_camera && p.enh.analog_movement);

    /* With nothing available, every row falls back. */
    p = defaults;
    CHECK(khdays_profile_apply_preset(&p, KHDAYS_PRESET_KH2, 0) == all);
    CHECK(p.stick[1].role == KHDAYS_STICK_NONE && !p.enh.dpad_command_menu && !p.enh.analog_movement);

    /* DS gives the default gamepad back. */
    CHECK(khdays_profile_apply_preset(&p, KHDAYS_PRESET_DS, all) == 0);
    CHECK(memcmp(p.input[PAD], defaults.input[PAD], sizeof(p.input[PAD])) == 0);
    CHECK(!p.enh.analog_camera && !p.enh.separate_lock_on && !p.enh.dpad_command_menu && !p.enh.analog_movement);
}

/* Every text in the five languages; the language setting
 * read from its code. */
static void test_languages(void)
{
    for (int id = 0; id < TXT_COUNT; ++id) {
        for (int l = KHDAYS_LANGUAGE_ENGLISH; l <= KHDAYS_LANGUAGE_LAST; ++l) {
            const char *t = khdays_text_in(id, l);
            if (t == NULL || t[0] == '\0' || strchr(t, '@') != NULL) {
                fprintf(stderr, "text %d has no %s\n", id, khdays_language_code(l));
                ++failures;
            }
        }
    }
    CHECK(khdays_language_from_code("es-ES") == KHDAYS_LANGUAGE_SPANISH);
    CHECK(khdays_language_from_code("FR") == KHDAYS_LANGUAGE_FRENCH);
    CHECK(khdays_language_from_code("ja") == KHDAYS_LANGUAGE_SYSTEM);
    CHECK(khdays_language_choose(KHDAYS_LANGUAGE_GERMAN) == KHDAYS_LANGUAGE_GERMAN ||
          SDL_getenv("KHDAYS_LANGUAGE") != NULL);
    khdays_ui_set_language(KHDAYS_LANGUAGE_ITALIAN);
    CHECK(strcmp(khdays_text(TXT_CLOSE), "Chiudi") == 0);
    khdays_ui_set_language(KHDAYS_LANGUAGE_ENGLISH);
}

static void test_hud_settings(void)
{
    KhdaysProfile p, q;
    char path[512];
    khdays_profile_default(&p);
    CHECK(load_text("version = 1\n", &q) == 0);
    CHECK(p.enh.hud[KHDAYS_OPT_UI_PADDING] == 0);
    CHECK(memcmp(p.enh.hud, q.enh.hud, sizeof(p.enh.hud)) == 0);
    for (int i = 0; i < KHDAYS_OPT_COUNT; ++i)
        p.enh.hud[i] = i < KHDAYS_OPT_MISSION_VISIBILITY ? (i & 1) : i == KHDAYS_OPT_MISSION_VISIBILITY ? 1 : 9;
    path_of("hud.ini", path, sizeof(path));
    CHECK(khdays_profile_save(&p, path, count_notice) == 0);
    notices = 0;
    khdays_profile_load(&q, path, count_notice);
    CHECK(notices == 0 && memcmp(p.enh.hud, q.enh.hud, sizeof(p.enh.hud)) == 0);
    khdays_hud_settings_publish(q.enh.hud);
    const unsigned generation = khdays_hud_settings_generation();
    for (int i = 0; i < KHDAYS_OPT_COUNT; ++i)
        CHECK(khdays_hud_setting(i) == q.enh.hud[i]);
    khdays_hud_settings_publish(q.enh.hud);
    CHECK(khdays_hud_settings_generation() == generation);
    KhdaysModelContext context = {0};
    KhdaysRow rows[KHDAYS_MODEL_ROWS_MAX];
    khdays_model_refresh(&q);
    const int count = khdays_model_rows(KHDAYS_SECTION_ENHANCEMENTS, &q, &context, rows);
    int found = 0;
    for (int i = 0; i < count; ++i)
        if (rows[i].id >= KHDAYS_SET_HUD_FONT && rows[i].id <= KHDAYS_SET_HUD_MISSION_PAUSE_ANCHOR) {
            const int option = rows[i].id - KHDAYS_SET_HUD_FONT;
            ++found;
            CHECK(rows[i].value == &q.enh.hud[option] && rows[i].available);
            CHECK(rows[i].apply == KHDAYS_APPLY_NOW);
            const int old = *rows[i].value;
            CHECK(khdays_model_adjust(&q, &rows[i], rows[i].kind == KHDAYS_ROW_SWITCH && old ? -1 : 1));
            CHECK(*rows[i].value != old);
        }
    CHECK(found == KHDAYS_OPT_COUNT);
    CHECK(khdays_model_changes(&p, &q) & KHDAYS_CHANGE_CONTROLS);
    khdays_devices_set_profile(&q);
    CHECK(khdays_hud_settings_generation() != generation);
    for (int i = 0; i < KHDAYS_OPT_COUNT; ++i)
        CHECK(khdays_hud_setting(i) == q.enh.hud[i]);
    q.enh.hud[KHDAYS_OPT_MISSION_ANCHOR] = 999;
    q.enh.hud[KHDAYS_OPT_MISSION_VISIBILITY] = -1;
    CHECK(khdays_profile_repair(&q));
    CHECK(q.enh.hud[KHDAYS_OPT_MISSION_ANCHOR] == 9 && q.enh.hud[KHDAYS_OPT_MISSION_VISIBILITY] == 0);
}

int main(void)
{
    test_default_profile();
    test_analog();
    test_analog_axes();
    test_menu_sequencer();
    test_lock_sequencer();
    test_presets();
    test_languages();
    test_sources_add_up();
    test_stick_directions();
    test_triggers();
    test_binding_rules();
    test_masking();
    test_devices();
    test_settings_file();
    test_mouse_bindings();
    test_video_settings();
    test_sound_settings();
    test_settings_model();
    test_settings_layout();
    test_settings_screen();
    test_hud_settings();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all input checks passed\n");
    return 0;
}
