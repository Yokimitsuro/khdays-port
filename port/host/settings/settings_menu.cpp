// The port's settings menu (settings_ui.h): ImGui lays it out and draws it, but the menu reads the
// input itself (khdays_settings_handle moves the focus and changes the working profile; clicks matched
// against the last drawing's places). The rows come from settings_model.c, rebuilt every time.
#include "settings_ui.h"
#include "settings_model.h"

extern "C" { // the host's C headers that say nothing of C++
#include "../app/devices.h"
#include "../app/updates.h"
#include "../screens/ui_text.h"
}

#include "imgui.h"
#include "imgui_impl_sdlrenderer3.h"
#include "menu_font.h"
#include "pad_icons.h"
#include "settings_skin.h"

#ifndef KHDAYS_VERSION
#define KHDAYS_VERSION "0.0.0" /* port/CMakeLists.txt gives the VERSION file's */
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr Uint64 kCaptureMs = 5000;         // how long a new binding waits for its key or button
constexpr Uint64 kRepeatDelayMs = 400;      // a held gamepad direction starts repeating after this,
constexpr Uint64 kRepeatEveryMs = 100;      // and then repeats this often
constexpr int kQuit = KHDAYS_SECTION_COUNT; // the column's last entry, Quit the game (B or Escape close the menu)

enum Mode { MODE_BROWSE, MODE_CAPTURE, MODE_CONFLICT, MODE_UNBOUND, MODE_PRESET, MODE_TEXT, MODE_UPDATE, MODE_QUIT };
enum { FACE_ACCEPT, FACE_CLEAR, FACE_BACK, FACE_COUNT };
constexpr int PLACE_VALUE = KHDAYS_PLACE_VALUE;

struct Place {
    int what, index;
    SDL_FRect rect;
};

struct Menu {
    bool open = false;
    bool changed = false;
    KhdaysProfile work{};     // what the menu edits
    KhdaysProfile original{}; // what it started from
    KhdaysProfile reported{}; // what khdays_settings_take_changes last reported
    int column = 0;           // the column's entry (kQuit: Quit the game)
    int section = 0;          // the section shown
    bool in_rows = false;     // the focus is on the section's rows
    int row = 0, cell = 0;
    Mode mode = MODE_BROWSE;
    int last_device = KHDAYS_DEVICE_KEYBOARD; // the device used last (2: the mouse), for the hints shown
    // A binding being made.
    int cap_device = 0, cap_layer = 0, cap_action = 0, cap_slot = 0;
    Uint64 cap_until = 0;
    int cap_input = 0, cap_owner = 0; // the input pressed, and who had it
    int choice = 0;                   // in a prompt: 0 = the first answer, 1 = the second
    char unbound[160] = {};           // DS buttons left without any input
    int preset = 0;                   // the preset being asked about
    unsigned fallbacks = 0;           // the enhancements it needs that this build does not have
    // A held gamepad direction, for repeating it.
    int held_dir = 0;
    Uint64 repeat_at = 0;
    // A text being typed: where it goes when done, how much it holds, and which characters it accepts.
    char edit[KHDAYS_SERVER_BYTES] = {};
    char *edit_target = nullptr;
    int edit_size = 0;
    int (*edit_accepts)(int c) = nullptr;
    SDL_Window *window = nullptr;
    // What the last key or button pressed does (the Controls section's test row).
    int test_action = -2; // -2 nothing pressed yet, -1 nothing
    // The rows of the section shown.
    std::vector<KhdaysRow> rows = std::vector<KhdaysRow>(KHDAYS_MODEL_ROWS_MAX);
    int count = 0;
    // The situation (khdays_settings_set_situation) and the graphics card.
    int in_session = 0, hd_videos = 0, graphics_card = 1, guest = 0;
    // Where the last drawing put things, in the renderer's pixels.
    std::vector<Place> places;
    float density = 1.0f;
    // For scrolling: each row's top and bottom in the list, as last drawn.
    std::vector<float> row_top, row_bottom;
    float rows_scroll = 0, rows_height = 0, wheel = 0;
    bool scroll_to_focus = false, scroll_to_top = false;
    // ImGui.
    ImGuiContext *imgui = nullptr;
    ImFont *font = nullptr;
    ImFont *skin_font = nullptr;   // the HD menu's (settings_skin.h), when its pieces are there
    bool skin = false;             // this frame is drawn in those pieces
    SDL_Renderer *bound = nullptr; // the renderer the backend draws with
    float styled_size = 0;
    Uint64 last_draw = 0;
    // The update notice (khdays_settings_offer_update): both versions and the player's answer
    // (KHDAYS_UPDATE_*, -1 until given).
    char update_have[64] = {}, update_new[64] = {};
    int update_answer = -1;
    int update_stage = 0;
    bool update_hooked = false;
    KhdaysUpdateHooks update_hooks{};
    char update_why[256] = {};
};

Menu m;

bool selectable(const KhdaysRow &r)
{
    switch (r.kind) {
    case KHDAYS_ROW_HEADING:
    case KHDAYS_ROW_LINE:
    case KHDAYS_ROW_STICKS:
    case KHDAYS_ROW_PAD:
    case KHDAYS_ROW_TEST: return false;
    default: return true;
    }
}

int first_cell(const KhdaysRow &r, int from, int dir)
{
    for (int c = from; c >= 0 && c < KHDAYS_CELLS; c += dir) {
        if (r.cells & (1u << c)) {
            return c;
        }
    }
    return -1;
}

// The connected gamepad's family (from SDL_GetGamepadType): which picture and icons to show.
int pad_family()
{
    SDL_Gamepad *pad = khdays_devices_gamepad();
    switch (pad != nullptr ? SDL_GetGamepadType(pad) : SDL_GAMEPAD_TYPE_UNKNOWN) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return KHDAYS_PAD_XBOX;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return KHDAYS_PAD_PLAYSTATION;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case SDL_GAMEPAD_TYPE_GAMECUBE: return KHDAYS_PAD_NINTENDO;
    default: return KHDAYS_PAD_GENERIC;
    }
}

// Rebuilds the rows of the section shown from the working profile, and keeps
// the focus on a row and a cell that exist.
void settle()
{
    KhdaysModelContext context{};
    context.graphics_card = m.graphics_card;
    context.in_session = m.in_session;
    context.guest = m.guest;
    context.hd_videos = m.hd_videos;
    context.gamepad = khdays_devices_gamepad_name();
    context.pad_family = pad_family();
    khdays_model_refresh(&m.work);
    m.count = khdays_model_rows_into(m.section, &m.work, &context, m.rows.data(), (int)m.rows.size());
    if (m.count > (int)m.rows.size()) {
        // a section with more rows than ever before (the Mods section grows with the mods)
        m.rows.resize((size_t)m.count);
        m.count = khdays_model_rows_into(m.section, &m.work, &context, m.rows.data(), (int)m.rows.size());
    }
    if (m.row >= m.count) {
        m.row = m.count - 1;
    }
    if (m.row < 0) {
        m.row = 0;
    }
    while (m.row < m.count - 1 && !selectable(m.rows[m.row])) {
        ++m.row;
    }
    while (m.row > 0 && !selectable(m.rows[m.row])) {
        --m.row;
    }
    if (m.count > 0 && m.rows[m.row].kind == KHDAYS_ROW_BINDING && !(m.rows[m.row].cells & (1u << m.cell))) {
        int c = first_cell(m.rows[m.row], m.cell, 1);
        m.cell = c >= 0 ? c : std::max(0, first_cell(m.rows[m.row], m.cell, -1));
    }
}

const KhdaysRow *focused_row()
{
    return m.in_rows && m.count > 0 && m.row < m.count ? &m.rows[m.row] : nullptr;
}

// ---- Input names, as the player reads them --------------------------------

void pad_input_label(int input, char *text, size_t size)
{
    // Names every controller family shares stay as they are.
    static const char *const names[KHDAYS_PAD_COUNT] = {
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "L1 / LB",
        "R1 / RB",
        "L2 / LT",
        "R2 / RT",
        "L3",
        "R3",
        "Start",
        "Select / View",
        "Guide / Home",
    };
    static const int ids[KHDAYS_PAD_COUNT] = {
        TXT_PAD_SOUTH,
        TXT_PAD_EAST,
        TXT_PAD_WEST,
        TXT_PAD_NORTH,
        TXT_PAD_DPUP,
        TXT_PAD_DPDOWN,
        TXT_PAD_DPLEFT,
        TXT_PAD_DPRIGHT,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        TXT_PAD_CAPTURE,
        TXT_PAD_TOUCHPAD,
    };
    static const int positions[] = {TXT_POS_BOTTOM, TXT_POS_RIGHT, TXT_POS_LEFT, TXT_POS_TOP}; // south .. north
    SDL_Gamepad *pad = khdays_devices_gamepad();
    const SDL_GamepadButton button = khdays_devices_sdl_button(input);
    if (pad != nullptr && button != SDL_GAMEPAD_BUTTON_INVALID && input <= KHDAYS_PAD_NORTH) {
        // a face button: what this controller prints on it, and where it is
        const SDL_GamepadButtonLabel label = SDL_GetGamepadButtonLabel(pad, button);
        static const char *const letters[] = {"", "A", "B", "X", "Y"};
        if (label >= SDL_GAMEPAD_BUTTON_LABEL_A && label <= SDL_GAMEPAD_BUTTON_LABEL_Y) {
            SDL_snprintf(text, size, "%s (%s)", letters[label], khdays_text(positions[input]));
            return;
        }
        if (label >= SDL_GAMEPAD_BUTTON_LABEL_CROSS && label <= SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE) {
            static const int shapes[] = {TXT_LABEL_CROSS, TXT_LABEL_CIRCLE, TXT_LABEL_SQUARE, TXT_LABEL_TRIANGLE};
            SDL_snprintf(text, size, "%s (%s)", khdays_text(shapes[label - SDL_GAMEPAD_BUTTON_LABEL_CROSS]),
                         khdays_text(positions[input]));
            return;
        }
    }
    {
        // shoulders, triggers, stick clicks and small buttons, as each family
        // prints or calls them
        struct FamilyName {
            int input;
            const char *names[4]; // generic (NULL: use the common name), Xbox, PlayStation, Nintendo
        };
        static const FamilyName family_names[] = {
            {KHDAYS_PAD_LEFTSHOULDER, {nullptr, "LB", "L1", "L"}},
            {KHDAYS_PAD_RIGHTSHOULDER, {nullptr, "RB", "R1", "R"}},
            {KHDAYS_PAD_LEFTTRIGGER, {nullptr, "LT", "L2", "ZL"}},
            {KHDAYS_PAD_RIGHTTRIGGER, {nullptr, "RT", "R2", "ZR"}},
            {KHDAYS_PAD_LEFTSTICK, {nullptr, "LS", "L3", "L3"}},
            {KHDAYS_PAD_RIGHTSTICK, {nullptr, "RS", "R3", "R3"}},
            {KHDAYS_PAD_START, {nullptr, "Menu", "Options", "+"}},
            {KHDAYS_PAD_BACK, {nullptr, "View", "Create / Share", "-"}},
            {KHDAYS_PAD_GUIDE, {nullptr, "Xbox", "PS", "Home"}},
        };
        const int family = pad != nullptr ? pad_family() : KHDAYS_PAD_GENERIC;
        for (const FamilyName &f : family_names) {
            if (f.input == input && f.names[family] != nullptr) {
                SDL_strlcpy(text, f.names[family], size);
                return;
            }
        }
    }
    if (input >= KHDAYS_PAD_PADDLE1 && input <= KHDAYS_PAD_PADDLE4) {
        SDL_snprintf(text, size, khdays_text(TXT_PAD_PADDLE), input - KHDAYS_PAD_PADDLE1 + 1);
    } else if (input >= 0 && input < KHDAYS_PAD_COUNT && names[input] != nullptr && names[input][0] != '\0') {
        SDL_strlcpy(text, names[input], size);
    } else if (input >= 0 && input < KHDAYS_PAD_COUNT && ids[input] != 0) {
        SDL_strlcpy(text, khdays_text(ids[input]), size);
    } else {
        SDL_strlcpy(text, "?", size);
    }
}

void input_label(int device, int input, char *text, size_t size)
{
    if (input == KHDAYS_NO_INPUT) {
        SDL_strlcpy(text, "-", size);
    } else if (device == KHDAYS_DEVICE_KEYBOARD && khdays_mouse_input_name(input) != nullptr) {
        SDL_strlcpy(text, khdays_mouse_input_name(input), size);
    } else if (device == KHDAYS_DEVICE_KEYBOARD) {
        // the label on this keyboard's key, for its layout
        const SDL_Keycode key = SDL_GetKeyFromScancode((SDL_Scancode)input, SDL_KMOD_NONE, false);
        const char *name = SDL_GetKeyName(key);
        SDL_strlcpy(text, name != nullptr && *name != '\0' ? name : SDL_GetScancodeName((SDL_Scancode)input), size);
    } else {
        pad_input_label(input, text, size);
    }
}

int pad_input_of_button(SDL_GamepadButton button)
{
    for (int i = 0; i < KHDAYS_PAD_COUNT; ++i) {
        if (khdays_devices_sdl_button(i) == button) {
            return i;
        }
    }
    return -1;
}

// The menu's accept, clear and back buttons: the face buttons the player's gamepad gives the DS's A,
// Y and B, so the menu answers like the game; one with no face button takes the DS's position unless
// another has it. Pad inputs (KHDAYS_PAD_*); -1 none.
void face_buttons(int out[FACE_COUNT])
{
    static const int actions[FACE_COUNT] = {KHDAYS_ACT_A, KHDAYS_ACT_Y, KHDAYS_ACT_B};
    static const int places[FACE_COUNT] = {KHDAYS_PAD_EAST, KHDAYS_PAD_WEST, KHDAYS_PAD_SOUTH};
    for (int f = 0; f < FACE_COUNT; ++f) {
        out[f] = -1;
        for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION && out[f] < 0; ++s) {
            const int input = m.original.input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][actions[f]][s];
            if (input >= KHDAYS_PAD_SOUTH && input <= KHDAYS_PAD_NORTH) {
                out[f] = input;
            }
        }
    }
    for (int f = 0; f < FACE_COUNT; ++f) {
        int taken = 0;
        for (int g = 0; g < FACE_COUNT; ++g) {
            taken |= out[g] == places[f];
        }
        if (out[f] < 0 && !taken) {
            out[f] = places[f];
        }
    }
}

int face_of(SDL_GamepadButton button)
{
    int face[FACE_COUNT];
    const int input = pad_input_of_button(button);
    face_buttons(face);
    for (int f = 0; f < FACE_COUNT && input >= 0; ++f) {
        if (face[f] == input) {
            return f;
        }
    }
    return -1;
}

int is_open_settings_input(int input)
{
    for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
        if (m.original.input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][KHDAYS_ACT_OPEN_SETTINGS][s] == input) {
            return 1;
        }
    }
    return 0;
}

int direction_of(SDL_GamepadButton button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 1;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 2;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 3;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 4;
    default: return 0;
    }
}

// ---- Closing -------------------------------------------------------------

// Returns true once the menu is closed.
bool try_close(bool even_if_unbound)
{
    if (!even_if_unbound && khdays_model_unbound(&m.work, m.unbound, (int)sizeof(m.unbound)) > 0) {
        m.mode = MODE_UNBOUND;
        m.choice = 1; // "Back" is the safe default
        return false;
    }
    khdays_profile_repair(&m.work);
    m.work.preset_modified = !khdays_profile_matches_preset(&m.work, KHDAYS_AVAILABLE_ENHANCEMENTS);
    m.changed = std::memcmp(&m.work, &m.original, sizeof(m.work)) != 0;
    m.open = false;
    m.mode = MODE_BROWSE;
    return true;
}

// Back: from the rows to the column, and from the column out of the menu.
// Returns true if the menu closed.
bool go_back()
{
    if (m.in_rows) {
        m.in_rows = false;
        m.column = m.section;
        return false;
    }
    return try_close(false);
}

// ---- The column ------------------------------------------------------------

void choose_section(int section)
{
    if (section != m.section) {
        m.section = section;
        m.row = m.cell = 0;
        m.scroll_to_top = true;
    }
    settle();
}

void column_move(int dir)
{
    int next = m.column + dir;
    while (next >= 0 && next < kQuit && !khdays_model_section_shown(next)) {
        next += dir;
    }
    if (next < 0) {
        return;
    }
    m.column = std::clamp(next, 0, kQuit);
    if (m.column < kQuit) {
        choose_section(m.column);
    }
}

// Quit the game: asked first, with "Cancel" chosen.
void ask_quit()
{
    m.mode = MODE_QUIT;
    m.choice = 1;
}

// Enter on a column entry: go into the section's rows, or ask whether to quit the game.
bool column_enter()
{
    if (m.column == kQuit) {
        ask_quit();
        return false;
    }
    choose_section(m.column);
    for (int i = 0; i < m.count; ++i) {
        if (selectable(m.rows[i])) {
            m.in_rows = true;
            m.row = i;
            settle();
            m.scroll_to_focus = true;
            return false;
        }
    }
    return false;
}

// The shoulder buttons move between the sections.
void switch_section(int dir)
{
    int next = m.section;
    do {
        next = (next + dir + KHDAYS_SECTION_COUNT) % KHDAYS_SECTION_COUNT;
    } while (!khdays_model_section_shown(next));
    const bool rows = m.in_rows;
    m.column = next;
    choose_section(next);
    if (rows) {
        m.in_rows = false;
        column_enter();
    }
}

// ---- The rows --------------------------------------------------------------

void start_capture(const KhdaysRow &r)
{
    m.mode = MODE_CAPTURE;
    m.cap_device = KHDAYS_CELL_DEVICE(m.cell);
    m.cap_slot = KHDAYS_CELL_SLOT(m.cell);
    m.cap_layer = r.layer;
    m.cap_action = r.action;
    m.cap_until = SDL_GetTicks() + kCaptureMs;
}

// Starts typing in a text row: with the keyboard (the system's text input, so
// any layout and input methods work), or with a gamepad like an arcade name
// entry (up and down change the last character, right adds one, left removes
// one).
void start_text(const KhdaysRow &r)
{
    SDL_strlcpy(m.edit, r.text, sizeof(m.edit));
    m.edit_target = r.text;
    m.edit_size = std::min(r.text_size, (int)sizeof(m.edit));
    m.edit_accepts = r.accepts;
    m.mode = MODE_TEXT;
    if (m.window != nullptr) {
        SDL_StartTextInput(m.window);
    }
}

// Stops typing: keeps the text if `keep`, else leaves the setting as it was.
void finish_text(bool keep)
{
    if (keep) {
        size_t length = SDL_strlen(m.edit);
        while (length > 0 && m.edit[length - 1] == ' ') {
            m.edit[--length] = '\0'; // trailing spaces mean nothing
        }
        // clear the whole field: a leftover tail would make an unchanged
        // profile look changed (try_close compares it byte for byte)
        SDL_memset(m.edit_target, 0, (size_t)m.edit_size);
        SDL_strlcpy(m.edit_target, m.edit, (size_t)m.edit_size);
    }
    if (m.window != nullptr) {
        SDL_StopTextInput(m.window);
    }
    m.mode = MODE_BROWSE;
}

void text_add(int c)
{
    const size_t length = SDL_strlen(m.edit);
    if (m.edit_accepts(c) && (int)length + 1 < m.edit_size) {
        m.edit[length] = (char)c;
        m.edit[length + 1] = '\0';
    }
}

void text_erase()
{
    const size_t length = SDL_strlen(m.edit);
    if (length > 0) {
        m.edit[length - 1] = '\0';
    }
}

// The next (`dir` 1) or previous printable character after `c` that the
// text accepts.
int next_char(int c, int dir)
{
    for (int i = 0; i < 0x5f; ++i) {
        c = 0x20 + (c - 0x20 + dir + 0x5f) % 0x5f;
        if (m.edit_accepts(c)) {
            return c;
        }
    }
    return c;
}

// A gamepad direction while typing: 1 up, 2 down, 3 left, 4 right.
void text_direction(int dir)
{
    const size_t length = SDL_strlen(m.edit);
    if (dir == 3) {
        text_erase();
        return;
    }
    if (dir == 4 || length == 0) {
        // a new character: a copy of the last one, or the first letter
        text_add(length > 0 ? m.edit[length - 1] : next_char('A' - 1, 1));
        return;
    }
    m.edit[length - 1] = (char)next_char((unsigned char)m.edit[length - 1], dir == 1 ? 1 : -1);
}

// Enter on the chosen row. Returns true if the menu closed.
bool activate()
{
    const KhdaysRow *r = focused_row();
    if (r == nullptr || !r->available) {
        return false;
    }
    switch (r->kind) {
    case KHDAYS_ROW_SWITCH:
    case KHDAYS_ROW_CHOICE: khdays_model_activate(&m.work, r); break;
    case KHDAYS_ROW_BINDING: start_capture(*r); break;
    case KHDAYS_ROW_TEXT: start_text(*r); break;
    case KHDAYS_ROW_COMMAND:
        if (r->command == KHDAYS_CMD_PRESET_DS || r->command == KHDAYS_CMD_PRESET_KH2) {
            // say what it will do (and what it cannot do yet), then ask
            KhdaysProfile scratch = m.work;
            m.preset = r->command == KHDAYS_CMD_PRESET_KH2 ? KHDAYS_PRESET_KH2 : KHDAYS_PRESET_DS;
            m.fallbacks = khdays_profile_apply_preset(&scratch, m.preset, KHDAYS_AVAILABLE_ENHANCEMENTS);
            m.mode = MODE_PRESET;
            m.choice = 0;
        } else {
            khdays_model_row_command(&m.work, r);
        }
        break;
    default: break;
    }
    settle();
    return false;
}

void row_move(int dir)
{
    int next = m.row;
    do {
        next += dir;
    } while (next >= 0 && next < m.count && !selectable(m.rows[next]));
    if (next >= 0 && next < m.count) {
        m.row = next;
        settle();
        m.scroll_to_focus = true;
    }
}

// Left or right in the rows: changes a value or moves between a binding's
// cells; going left past the first cell, or on a row without a value, goes
// back to the column.
void row_adjust(int dir)
{
    const KhdaysRow *r = focused_row();
    if (r == nullptr) {
        return;
    }
    if (r->kind == KHDAYS_ROW_BINDING) {
        const int c = first_cell(*r, m.cell + dir, dir);
        if (c >= 0) {
            m.cell = c;
        } else if (dir < 0) {
            go_back();
        }
        return;
    }
    if (!khdays_model_adjust(&m.work, r, dir) && dir < 0) {
        go_back();
    }
    settle();
}

void clear_cell()
{
    const KhdaysRow *r = focused_row();
    if (r != nullptr && r->kind == KHDAYS_ROW_BINDING) {
        khdays_profile_clear(&m.work, KHDAYS_CELL_DEVICE(m.cell), r->layer, r->action, KHDAYS_CELL_SLOT(m.cell));
        settle();
    }
}

void navigate(int dir)
{
    if (m.in_rows) {
        switch (dir) {
        case 1: row_move(-1); break;
        case 2: row_move(1); break;
        case 3: row_adjust(-1); break;
        case 4: row_adjust(1); break;
        default: break;
        }
        return;
    }
    switch (dir) {
    case 1: column_move(-1); break;
    case 2: column_move(1); break;
    case 4: column_enter(); break;
    default: break;
    }
}

// ---- Waiting for a key or button to bind -----------------------------------

void finish_capture(int input)
{
    const int owner = khdays_profile_owner(&m.work, m.cap_device, m.cap_layer, input);
    if (owner >= 0 && owner != m.cap_action) {
        // it already does something else: ask before moving it
        m.mode = MODE_CONFLICT;
        m.cap_input = input;
        m.cap_owner = owner;
        m.choice = 0;
        return;
    }
    khdays_profile_set(&m.work, m.cap_device, m.cap_layer, m.cap_action, m.cap_slot, input);
    m.mode = MODE_BROWSE;
}

void capture_event(const SDL_Event *e)
{
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        if (e->key.scancode == SDL_SCANCODE_ESCAPE) {
            m.mode = MODE_BROWSE; // cancelled
        } else if (m.cap_device == KHDAYS_DEVICE_KEYBOARD) {
            finish_capture((int)e->key.scancode);
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && m.cap_device == KHDAYS_DEVICE_GAMEPAD) {
        const int input = pad_input_of_button((SDL_GamepadButton)e->gbutton.button);
        if (input < 0) {
            return;
        }
        if (is_open_settings_input(input) && m.cap_action != KHDAYS_ACT_OPEN_SETTINGS) {
            m.mode = MODE_BROWSE; // the settings button cancels
        } else {
            finish_capture(input);
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && m.cap_device == KHDAYS_DEVICE_GAMEPAD &&
               e->gaxis.value > 16384) {
        if (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
            finish_capture(KHDAYS_PAD_LEFTTRIGGER);
        } else if (e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            finish_capture(KHDAYS_PAD_RIGHTTRIGGER);
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_REMOVED && m.cap_device == KHDAYS_DEVICE_GAMEPAD) {
        m.mode = MODE_BROWSE; // the gamepad was unplugged
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && m.cap_device == KHDAYS_DEVICE_KEYBOARD &&
               e->button.button >= SDL_BUTTON_LEFT && e->button.button <= SDL_BUTTON_X2) {
        // the mouse's buttons share the keyboard's column (SDL numbers them from 1: left, middle, right, X1, X2)
        finish_capture(KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_LEFT + (e->button.button - SDL_BUTTON_LEFT));
    } else if (e->type == SDL_EVENT_MOUSE_WHEEL && m.cap_device == KHDAYS_DEVICE_KEYBOARD && e->wheel.y != 0.0f) {
        finish_capture(KHDAYS_MOUSE_INPUT + (e->wheel.y > 0.0f ? KHDAYS_MOUSE_WHEEL_UP : KHDAYS_MOUSE_WHEEL_DOWN));
    }
}

// ---- Questions (a conflict, buttons left unbound, a preset) ---------------

// Returns true if the menu closed.
bool answer_prompt(bool yes)
{
    if (m.mode == MODE_CONFLICT) {
        if (yes) {
            khdays_profile_set(&m.work, m.cap_device, m.cap_layer, m.cap_action, m.cap_slot, m.cap_input);
        }
        m.mode = MODE_BROWSE;
        return false;
    }
    if (m.mode == MODE_PRESET) {
        if (yes) {
            khdays_model_command(&m.work, m.preset == KHDAYS_PRESET_KH2 ? KHDAYS_CMD_PRESET_KH2 : KHDAYS_CMD_PRESET_DS);
        }
        m.mode = MODE_BROWSE;
        return false;
    }
    if (m.mode == MODE_QUIT) {
        m.mode = MODE_BROWSE;
        if (yes) {
            // as closing the window: the settings are saved, then the game ends (window.c)
            SDL_Event quit;
            SDL_zero(quit);
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
        }
        return false;
    }
    m.mode = MODE_BROWSE;
    return yes ? try_close(true) : false;
}

bool prompt_event(const SDL_Event *e)
{
    if (e->type == SDL_EVENT_KEY_DOWN) {
        switch (e->key.scancode) {
        case SDL_SCANCODE_LEFT: m.choice = 0; break;
        case SDL_SCANCODE_RIGHT: m.choice = 1; break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE: return answer_prompt(m.choice == 0);
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_BACKSPACE: return answer_prompt(false);
        default: break;
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (e->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: m.choice = 0; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: m.choice = 1; break;
        default:
            switch (face_of((SDL_GamepadButton)e->gbutton.button)) {
            case FACE_ACCEPT: return answer_prompt(m.choice == 0);
            case FACE_BACK: return answer_prompt(false);
            default: break;
            }
            break;
        }
    }
    return false;
}

// ---- Typing ----------------------------------------------------------------

void text_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_EVENT_TEXT_INPUT:
        for (const char *c = e->text.text; *c != '\0'; ++c) {
            text_add((unsigned char)*c);
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        switch (e->key.scancode) {
        case SDL_SCANCODE_BACKSPACE: text_erase(); break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: finish_text(true); break;
        case SDL_SCANCODE_ESCAPE: finish_text(false); break;
        default: break;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        const SDL_GamepadButton button = (SDL_GamepadButton)e->gbutton.button;
        switch (face_of(button)) {
        case FACE_ACCEPT: finish_text(true); break;
        case FACE_BACK: finish_text(false); break;
        case FACE_CLEAR: m.edit[0] = '\0'; break;
        default:
            if (direction_of(button)) {
                m.held_dir = direction_of(button);
                m.repeat_at = SDL_GetTicks() + kRepeatDelayMs;
                text_direction(m.held_dir);
            }
            break;
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (direction_of((SDL_GamepadButton)e->gbutton.button) == m.held_dir) {
            m.held_dir = 0;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: finish_text(e->button.button == SDL_BUTTON_LEFT); break;
    default: break;
    }
}

// ---- The mouse -------------------------------------------------------------

const Place *place_at(int what, float x, float y)
{
    for (const Place &p : m.places) {
        if (p.what == what && x >= p.rect.x && x < p.rect.x + p.rect.w && y >= p.rect.y && y < p.rect.y + p.rect.h) {
            return &p;
        }
    }
    return nullptr;
}

const Place *place_of(int what, int index)
{
    for (const Place &p : m.places) {
        if (p.what == what && p.index == index) {
            return &p;
        }
    }
    return nullptr;
}

// Moving the mouse over a row chooses it (and a binding's cell).
void mouse_focus(float x, float y)
{
    const Place *row = place_at(KHDAYS_PLACE_ROW, x, y);
    if (row == nullptr || row->index >= m.count || !selectable(m.rows[row->index])) {
        return;
    }
    m.in_rows = true;
    m.column = m.section;
    m.row = row->index;
    if (const Place *cell = place_at(KHDAYS_PLACE_CELL, x, y)) {
        m.cell = cell->index % KHDAYS_CELLS;
    }
    settle();
}

bool mouse_click(float x, float y)
{
    if (const Place *p = place_at(KHDAYS_PLACE_ANSWER, x, y)) {
        return answer_prompt(p->index == 0);
    }
    if (m.mode != MODE_BROWSE) {
        return false;
    }
    if (const Place *entry = place_at(KHDAYS_PLACE_ENTRY, x, y)) {
        m.in_rows = false;
        m.column = entry->index;
        if (entry->index == kQuit) {
            ask_quit();
            return false;
        }
        choose_section(entry->index);
        return false;
    }
    const Place *row = place_at(KHDAYS_PLACE_ROW, x, y);
    if (row == nullptr || row->index >= m.count || !selectable(m.rows[row->index])) {
        return false;
    }
    mouse_focus(x, y);
    const KhdaysRow &r = m.rows[m.row];
    if (r.kind == KHDAYS_ROW_NUMBER || r.kind == KHDAYS_ROW_CHOICE) {
        // clicking the right half of a value raises it, the left half lowers it
        const Place *value = place_of(PLACE_VALUE, m.row);
        if (value != nullptr && x >= value->rect.x) {
            khdays_model_adjust(&m.work, &r, x >= value->rect.x + value->rect.w / 2 ? 1 : -1);
            settle();
        }
        return false;
    }
    if (r.kind == KHDAYS_ROW_BINDING && place_at(KHDAYS_PLACE_CELL, x, y) == nullptr) {
        return false; // on the action's name: nothing to do
    }
    return activate();
}

// ---- A newer release (the menu opened for its notice alone) -----------------

// The notice's stages: the offer (Download, Later, Don't remind me), downloading (Cancel), failed
// (Open its page, Later).
enum { UPDATE_OFFER, UPDATE_GETTING, UPDATE_FAILED };

int update_answers()
{
    return m.update_stage == UPDATE_OFFER ? 3 : m.update_stage == UPDATE_GETTING ? 1 : 2;
}

// The answer closes the notice, and with it the menu (opened for it alone): returns true.
bool answer_update(int answer)
{
    m.update_answer = answer;
    m.mode = MODE_BROWSE;
    m.open = false;
    return true;
}

void update_step(int by)
{
    m.choice = (m.choice + update_answers() + by) % update_answers();
}

// Answer `index` of the stage shown; returns true when it closed the notice.
bool update_choose(int index)
{
    switch (m.update_stage) {
    case UPDATE_OFFER:
        if (index == KHDAYS_UPDATE_DOWNLOAD && m.update_hooked && m.update_hooks.can_install()) {
            m.update_hooks.download();
            m.update_stage = UPDATE_GETTING;
            m.choice = 0;
            return false;
        }
        return answer_update(index);
    case UPDATE_GETTING: m.update_hooks.cancel(); return answer_update(KHDAYS_UPDATE_LATER);
    default: return answer_update(index == 0 ? KHDAYS_UPDATE_DOWNLOAD : KHDAYS_UPDATE_LATER);
    }
}

// Back (Escape, B, the right mouse button): later, the download given up.
bool update_back()
{
    if (m.update_stage == UPDATE_GETTING) {
        m.update_hooks.cancel();
    }
    return answer_update(KHDAYS_UPDATE_LATER);
}

// While the installer downloads: once checked it runs and the notice closes, or it says why not.
void update_tick()
{
    char why[256] = "";
    if (m.mode != MODE_UPDATE || m.update_stage != UPDATE_GETTING) {
        return;
    }
    const int state = m.update_hooks.state(nullptr, nullptr, why, sizeof(why));
    if (state == KHDAYS_DOWNLOAD_READY) {
        if (m.update_hooks.install(why, sizeof(why))) {
            answer_update(KHDAYS_UPDATE_INSTALL);
            return;
        }
    } else if (state != KHDAYS_DOWNLOAD_FAILED) {
        return;
    }
    SDL_strlcpy(m.update_why, why, sizeof(m.update_why));
    m.update_stage = UPDATE_FAILED;
    m.choice = 0;
}

// Returns true when the notice was answered (the menu closed).
bool update_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        switch (e->key.scancode) {
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_LEFT: update_step(-1); break;
        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_RIGHT: update_step(1); break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE: return update_choose(m.choice);
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_BACKSPACE: return update_back();
        default: break;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (e->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: update_step(-1); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: update_step(1); break;
        default:
            switch (face_of((SDL_GamepadButton)e->gbutton.button)) {
            case FACE_ACCEPT: return update_choose(m.choice);
            case FACE_BACK: return update_back();
            default: break;
            }
            break;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (const Place *p = place_at(KHDAYS_PLACE_ANSWER, e->motion.x * m.density, e->motion.y * m.density)) {
            m.choice = p->index;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button == SDL_BUTTON_LEFT) {
            if (const Place *p = place_at(KHDAYS_PLACE_ANSWER, e->button.x * m.density, e->button.y * m.density)) {
                return update_choose(p->index);
            }
        } else if (e->button.button == SDL_BUTTON_RIGHT) {
            return update_back();
        }
        break;
    default: break;
    }
    return false;
}

// Notes what the last key or button pressed does, for the test row.
void note_test_input(const SDL_Event *e)
{
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        m.test_action = khdays_profile_owner(&m.work, KHDAYS_DEVICE_KEYBOARD, KHDAYS_LAYER_BASE, (int)e->key.scancode);
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const int input = pad_input_of_button((SDL_GamepadButton)e->gbutton.button);
        m.test_action =
            input >= 0 ? khdays_profile_owner(&m.work, KHDAYS_DEVICE_GAMEPAD, KHDAYS_LAYER_BASE, input) : -1;
    }
}

// ---- Drawing -----------------------------------------------------------------

constexpr ImU32 kWhite = IM_COL32(240, 240, 240, 255), kGrey = IM_COL32(150, 150, 160, 255),
                kYellow = IM_COL32(255, 214, 100, 255), kBlue = IM_COL32(150, 190, 255, 255),
                kFocus = IM_COL32(40, 70, 140, 255), kCell = IM_COL32(70, 110, 200, 255),
                kChosen = IM_COL32(30, 40, 72, 255), kPanel = IM_COL32(12, 14, 24, 236);
// in the HD menu's pieces: its headings light grey (Days's greys, settings_skin.h)
constexpr ImU32 kSkinHeading = IM_COL32(205, 205, 205, 255);
// the HD menu's font's letters fill less of its cells than the default font's: drawn this much larger
constexpr float kSkinFontScale = 1.25f;
// where the menu goes in the pieces' picture: inside the band between the screen's lines
// (y 150 and 936 of 1080), the sections from its left
constexpr float kSkinArea[4] = {60, 178, 1880, 930};

ImU32 heading_colour()
{
    return m.skin ? kSkinHeading : kBlue;
}

void record(int what, int index, ImVec2 min, ImVec2 max)
{
    // only what can be seen: clipped to the window being drawn in
    ImVec2 lo = ImGui::GetWindowPos(), hi = ImVec2(lo.x + ImGui::GetWindowWidth(), lo.y + ImGui::GetWindowHeight());
    min.x = std::max(min.x, lo.x);
    min.y = std::max(min.y, lo.y);
    max.x = std::min(max.x, hi.x);
    max.y = std::min(max.y, hi.y);
    if (max.x > min.x && max.y > min.y) {
        m.places.push_back({what, index, {min.x, min.y, max.x - min.x, max.y - min.y}});
    }
}

void text_at(ImVec2 p, ImU32 colour, const char *s)
{
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), p, colour, s);
}

// Draws a text cut off where its column ends.
void text_clipped(ImVec2 p, ImU32 colour, const char *s, float right)
{
    const float size = ImGui::GetFontSize();
    const ImVec4 clip(p.x, p.y - size, right, p.y + 2 * size);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), size, p, colour, s, nullptr, 0.0f, &clip);
}

float width_of(const char *s)
{
    return ImGui::CalcTextSize(s).x;
}

std::string value_text(const KhdaysRow &r)
{
    char s[KHDAYS_SERVER_BYTES + 16];
    s[0] = '\0';
    switch (r.kind) {
    case KHDAYS_ROW_SWITCH: SDL_strlcpy(s, khdays_text(*r.value ? TXT_ON : TXT_OFF), sizeof(s)); break;
    // (in the HD menu's pieces the arrows and the slider are pieces of their own)
    case KHDAYS_ROW_NUMBER: SDL_snprintf(s, sizeof(s), m.skin ? "%d" : "<  %d  >", *r.value); break;
    case KHDAYS_ROW_CHOICE:
        if (r.values == nullptr || r.values[khdays_model_choice_of(&r)] == *r.value) {
            SDL_snprintf(s, sizeof(s), m.skin ? "%s" : "<  %s  >", r.choices[khdays_model_choice_of(&r)]);
        } else {
            SDL_snprintf(s, sizeof(s), m.skin ? "%dx" : "<  %dx  >",
                         *r.value); // a multiple from the file beyond the choices
        }
        break;
    case KHDAYS_ROW_TEXT:
        if (m.mode == MODE_TEXT && m.edit_target == r.text) {
            SDL_snprintf(s, sizeof(s), "%s_", m.edit); // being typed: _ marks where the next character goes
        } else if (r.text[0] == '\0' && r.placeholder != nullptr) {
            SDL_snprintf(s, sizeof(s), "(%s)", r.placeholder);
        } else {
            SDL_strlcpy(s, r.text, sizeof(s));
        }
        break;
    default: break;
    }
    return s;
}

const char *icon_name(int input);
SDL_Texture *icon(const char *name);

// The icon of a gamepad cell's input (NULL if none), and the width it takes.
const char *cell_icon(const KhdaysRow &r, int cell)
{
    if (!(r.cells & (1u << cell)) || KHDAYS_CELL_DEVICE(cell) != KHDAYS_DEVICE_GAMEPAD) {
        return nullptr;
    }
    const int input = m.work.input[KHDAYS_DEVICE_GAMEPAD][r.layer][r.action][KHDAYS_CELL_SLOT(cell)];
    return input != KHDAYS_NO_INPUT ? icon_name(input) : nullptr;
}

float cell_icon_room(const KhdaysRow &r, int cell)
{
    return icon(cell_icon(r, cell)) != nullptr ? ImGui::GetFontSize() * 1.3f : 0.0f;
}

std::string cell_text(const KhdaysRow &r, int cell)
{
    char s[96];
    if (!(r.cells & (1u << cell))) {
        return "";
    }
    const int device = KHDAYS_CELL_DEVICE(cell);
    input_label(device, m.work.input[device][r.layer][r.action][KHDAYS_CELL_SLOT(cell)], s, sizeof(s));
    return s;
}

const char *hints()
{
    static char pad[256];
    if (m.mode == MODE_TEXT && m.last_device != KHDAYS_DEVICE_GAMEPAD) {
        return khdays_text(TXT_HINTS_TEXT_KEYBOARD);
    }
    switch (m.last_device) {
    case KHDAYS_DEVICE_GAMEPAD: {
        int face[FACE_COUNT];
        char names[FACE_COUNT][48];
        face_buttons(face);
        for (int f = 0; f < FACE_COUNT; ++f) {
            if (face[f] >= 0) {
                pad_input_label(face[f], names[f], sizeof(names[f]));
            } else {
                SDL_strlcpy(names[f], "-", sizeof(names[f]));
            }
        }
        SDL_snprintf(pad, sizeof(pad), khdays_text(m.mode == MODE_TEXT ? TXT_HINTS_TEXT_GAMEPAD : TXT_HINTS_GAMEPAD),
                     names[FACE_ACCEPT], names[FACE_CLEAR], names[FACE_BACK]);
        return pad;
    }
    case 2: return khdays_text(TXT_HINTS_MOUSE);
    default: return khdays_text(TXT_HINTS_KEYBOARD);
    }
}

// The chosen row's note, plus when its change takes effect if not at once.
std::string footer_note()
{
    const KhdaysRow *r = focused_row();
    std::string note;
    if (r == nullptr || m.mode != MODE_BROWSE) {
        return note;
    }
    if (r->note != nullptr) {
        note = r->note;
    }
    if (r->apply != KHDAYS_APPLY_NOW && r->takes >= 0) {
        if (!note.empty()) {
            note += ' ';
        }
        note += khdays_text(r->takes);
    }
    return note;
}

// The chosen row's warning while its value is not the safe one (KhdaysRow `warning`), or empty.
std::string footer_warning()
{
    const KhdaysRow *r = focused_row();
    if (r == nullptr || m.mode != MODE_BROWSE || r->warning == nullptr || r->value == nullptr || *r->value == r->safe) {
        return std::string();
    }
    return r->warning;
}

void draw_column(float row_h, float pad)
{
    const float w = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i <= kQuit; ++i) {
        if (i < kQuit && !khdays_model_section_shown(i)) {
            continue;
        }
        if (i == kQuit) {
            ImGui::Dummy(ImVec2(w, row_h * 0.5f));
        }
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 q(p.x + w, p.y + row_h);
        const bool focus = !m.in_rows && i == m.column && m.mode == MODE_BROWSE;
        const bool chosen = focus || (i == m.section && m.in_rows);
        // the chosen tab's words move right with it (the HD menu's 18 texels of a 48-high tab)
        const float moved = m.skin && chosen ? row_h * 0.88f * 18.0f / 48.0f : 0.0f;
        if (m.skin) {
            // the HD menu's section tabs: every entry's, the chosen one (the focus, or the
            // section shown) moved right as the HD menu moves it
            const ImVec2 a(p.x, p.y + row_h * 0.06f), b(q.x - row_h * 0.4f, q.y - row_h * 0.06f);
            if (chosen) {
                khdays_skin_piece(ImGui::GetWindowDrawList(), "section", "selected", a, b);
            } else {
                khdays_skin_piece(ImGui::GetWindowDrawList(), "section", "shown", a, b);
            }
        } else if (focus) {
            ImGui::GetWindowDrawList()->AddRectFilled(p, q, kFocus);
        } else if (i == m.section) {
            ImGui::GetWindowDrawList()->AddRectFilled(p, q, kChosen);
        }
        text_at(ImVec2(p.x + pad + moved, p.y + (row_h - ImGui::GetFontSize()) / 2),
                i == m.section || i == m.column ? kWhite : kGrey,
                i == kQuit ? khdays_text(TXT_QUIT_GAME) : khdays_model_section_title(i));
        ImGui::Dummy(ImVec2(w, row_h));
        record(KHDAYS_PLACE_ENTRY, i, p, q);
    }
}

void draw_sticks(ImVec2 p, float w, float height)
{
    const KhdaysDeviceState *s = khdays_devices_state();
    const float radius = height * 0.32f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < 2; ++i) {
        const ImVec2 c(p.x + w * (i == 0 ? 0.3f : 0.7f), p.y + radius + height * 0.05f);
        const float dz = radius * (float)m.work.stick[i].dead_zone / 100.0f;
        dl->AddCircle(c, radius, kGrey, 32);
        dl->AddCircle(c, dz, kGrey, 32);
        const ImVec2 dot(c.x + radius * (float)s->stick[i][0] / 32767.0f,
                         c.y + radius * (float)s->stick[i][1] / 32767.0f);
        const float k = std::max(2.0f, radius * 0.08f);
        dl->AddRectFilled(ImVec2(dot.x - k, dot.y - k), ImVec2(dot.x + k, dot.y + k), kYellow);
        const char *label = khdays_text(i == 0 ? TXT_LEFT_STICK : TXT_RIGHT_STICK);
        text_at(ImVec2(c.x - width_of(label) / 2, c.y + radius + height * 0.05f), kGrey, label);
    }
}

// ---- The gamepads' button icons --------------------------------------------
//
// Kenney's Input Prompts (CC0), embedded in the executable (pad_icons.h).
// Each icon becomes a texture of the menu's renderer the first time it is
// drawn, and is freed with that renderer.

std::unordered_map<std::string, SDL_Texture *> icon_textures;

SDL_Texture *icon(const char *name)
{
    if (name == nullptr || m.bound == nullptr) {
        return nullptr;
    }
    const auto found = icon_textures.find(name);
    if (found != icon_textures.end()) {
        return found->second;
    }
    SDL_Texture *texture = nullptr;
    for (const auto &i : khdays_pad_icons) {
        if (std::strcmp(i.name, name) == 0) {
            SDL_IOStream *io = SDL_IOFromConstMem(i.png, i.size);
            SDL_Surface *surface = io != nullptr ? SDL_LoadPNG_IO(io, true) : nullptr;
            if (surface != nullptr) {
                texture = SDL_CreateTextureFromSurface(m.bound, surface);
                SDL_DestroySurface(surface);
            }
            if (texture != nullptr) {
                SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            }
            break;
        }
    }
    icon_textures[name] = texture; // a NULL is kept too, so a failed one is not tried again
    return texture;
}

void forget_icons()
{
    for (auto &entry : icon_textures) {
        if (entry.second != nullptr) {
            SDL_DestroyTexture(entry.second);
        }
    }
    icon_textures.clear();
}

SDL_GamepadType pad_type()
{
    SDL_Gamepad *pad = khdays_devices_gamepad();
    return pad != nullptr ? SDL_GetGamepadType(pad) : SDL_GAMEPAD_TYPE_UNKNOWN;
}

// The icon of an input on the connected gamepad, or NULL if there is none
// (its name is written instead). With no gamepad connected, or one SDL does
// not know, the Xbox icons are used.
const char *icon_name(int input)
{
    struct Icons {
        int input;
        const char *xbox, *ps5, *ps4, *ps3, *nintendo;
    };
    static const Icons icons[] = {
        {KHDAYS_PAD_DPUP, "xbox_dpad_up", "playstation_dpad_up", "playstation_dpad_up", "playstation_dpad_up",
         "switch_dpad_up"},
        {KHDAYS_PAD_DPDOWN, "xbox_dpad_down", "playstation_dpad_down", "playstation_dpad_down", "playstation_dpad_down",
         "switch_dpad_down"},
        {KHDAYS_PAD_DPLEFT, "xbox_dpad_left", "playstation_dpad_left", "playstation_dpad_left", "playstation_dpad_left",
         "switch_dpad_left"},
        {KHDAYS_PAD_DPRIGHT, "xbox_dpad_right", "playstation_dpad_right", "playstation_dpad_right",
         "playstation_dpad_right", "switch_dpad_right"},
        {KHDAYS_PAD_LEFTSHOULDER, "xbox_lb", "playstation_trigger_l1", "playstation_trigger_l1",
         "playstation_trigger_l1", "switch_button_l"},
        {KHDAYS_PAD_RIGHTSHOULDER, "xbox_rb", "playstation_trigger_r1", "playstation_trigger_r1",
         "playstation_trigger_r1", "switch_button_r"},
        {KHDAYS_PAD_LEFTTRIGGER, "xbox_lt", "playstation_trigger_l2", "playstation_trigger_l2",
         "playstation_trigger_l2", "switch_button_zl"},
        {KHDAYS_PAD_RIGHTTRIGGER, "xbox_rt", "playstation_trigger_r2", "playstation_trigger_r2",
         "playstation_trigger_r2", "switch_button_zr"},
        {KHDAYS_PAD_LEFTSTICK, "xbox_stick_l_press", "playstation_button_l3", "playstation_button_l3",
         "playstation_button_l3", "switch_stick_l_press"},
        {KHDAYS_PAD_RIGHTSTICK, "xbox_stick_r_press", "playstation_button_r3", "playstation_button_r3",
         "playstation_button_r3", "switch_stick_r_press"},
        {KHDAYS_PAD_START, "xbox_button_menu", "playstation5_button_options", "playstation4_button_options",
         "playstation3_button_start", "switch_button_plus"},
        {KHDAYS_PAD_BACK, "xbox_button_view", "playstation5_button_create", "playstation4_button_share",
         "playstation3_button_select", "switch_button_minus"},
        {KHDAYS_PAD_GUIDE, "xbox_guide", nullptr, nullptr, nullptr, "switch_button_home"},
        {KHDAYS_PAD_MISC1, "xbox_button_share", "playstation5_button_mute", nullptr, nullptr, nullptr},
        {KHDAYS_PAD_TOUCHPAD, nullptr, "playstation5_touchpad", "playstation4_touchpad", nullptr, nullptr},
    };
    const SDL_GamepadType type = pad_type();
    const int family = pad_family();
    if (input >= KHDAYS_PAD_SOUTH && input <= KHDAYS_PAD_NORTH) {
        // what is printed on it, as SDL knows it; else the Xbox letter in that place
        static const char *const xbox[] = {"xbox_button_color_a", "xbox_button_color_b", "xbox_button_color_x",
                                           "xbox_button_color_y"};
        static const char *const letters[][2] = {{"xbox_button_color_a", "switch_button_a"},
                                                 {"xbox_button_color_b", "switch_button_b"},
                                                 {"xbox_button_color_x", "switch_button_x"},
                                                 {"xbox_button_color_y", "switch_button_y"}};
        SDL_Gamepad *pad = khdays_devices_gamepad();
        const SDL_GamepadButtonLabel label = pad != nullptr
                                                 ? SDL_GetGamepadButtonLabel(pad, khdays_devices_sdl_button(input))
                                                 : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
        switch (label) {
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "playstation_button_color_cross";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "playstation_button_color_circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "playstation_button_color_square";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "playstation_button_color_triangle";
        case SDL_GAMEPAD_BUTTON_LABEL_A:
        case SDL_GAMEPAD_BUTTON_LABEL_B:
        case SDL_GAMEPAD_BUTTON_LABEL_X:
        case SDL_GAMEPAD_BUTTON_LABEL_Y:
            return letters[label - SDL_GAMEPAD_BUTTON_LABEL_A][family == KHDAYS_PAD_NINTENDO];
        default: return xbox[input];
        }
    }
    for (const Icons &i : icons) {
        if (i.input == input) {
            switch (family) {
            case KHDAYS_PAD_PLAYSTATION:
                return type == SDL_GAMEPAD_TYPE_PS3 ? i.ps3 : type == SDL_GAMEPAD_TYPE_PS4 ? i.ps4 : i.ps5;
            case KHDAYS_PAD_NINTENDO: return i.nintendo;
            default: return i.xbox;
            }
        }
    }
    return nullptr;
}

// The icon of a stick (for what moving it does), and of the D-pad with no
// direction held.
const char *stick_icon(int stick)
{
    static const char *const names[][2] = {{"xbox_stick_l", "xbox_stick_r"},
                                           {"playstation_stick_l", "playstation_stick_r"},
                                           {"switch_stick_l", "switch_stick_r"}};
    const int family = pad_family();
    return names[family == KHDAYS_PAD_PLAYSTATION ? 1 : family == KHDAYS_PAD_NINTENDO ? 2 : 0][stick];
}

void draw_icon(ImDrawList *dl, SDL_Texture *texture, ImVec2 c, float size)
{
    dl->AddImage((ImTextureID)(intptr_t)texture, ImVec2(c.x - size / 2, c.y - size / 2),
                 ImVec2(c.x + size / 2, c.y + size / 2));
}

// ---- The gamepad shown -----------------------------------------------------
//
// The connected gamepad's picture (Kenney's, for its family) and, beside it,
// one row per input: its icon and what it does in the working profile. The
// left hand's inputs are on the left, the right hand's on the right.

struct PadPart {
    int input; // KHDAYS_PAD_*; for a stick, its click
    int stick; // 0 or 1 for the left or right stick, -1 for none
    int side;  // 0 for the left column, 1 for the right one
};

// The rows, from the top down: the triggers and shoulders, then the D-pad or
// the face buttons, the stick, and the small buttons (the touchpad only on a
// PlayStation pad).
int pad_parts(int family, PadPart *out)
{
    static const PadPart parts[] = {
        {KHDAYS_PAD_LEFTTRIGGER, -1, 0},  {KHDAYS_PAD_LEFTSHOULDER, -1, 0},  {KHDAYS_PAD_DPUP, -1, 0},
        {KHDAYS_PAD_DPLEFT, -1, 0},       {KHDAYS_PAD_DPRIGHT, -1, 0},       {KHDAYS_PAD_DPDOWN, -1, 0},
        {KHDAYS_PAD_LEFTSTICK, 0, 0},     {KHDAYS_PAD_BACK, -1, 0},          {KHDAYS_PAD_MISC1, -1, 0},
        {KHDAYS_PAD_RIGHTTRIGGER, -1, 1}, {KHDAYS_PAD_RIGHTSHOULDER, -1, 1}, {KHDAYS_PAD_NORTH, -1, 1},
        {KHDAYS_PAD_WEST, -1, 1},         {KHDAYS_PAD_EAST, -1, 1},          {KHDAYS_PAD_SOUTH, -1, 1},
        {KHDAYS_PAD_RIGHTSTICK, 1, 1},    {KHDAYS_PAD_START, -1, 1},         {KHDAYS_PAD_GUIDE, -1, 1},
        {KHDAYS_PAD_TOUCHPAD, -1, 1},
    };
    int n = 0;
    for (const PadPart &part : parts) {
        if (part.input != KHDAYS_PAD_TOUCHPAD || family == KHDAYS_PAD_PLAYSTATION) {
            out[n++] = part;
        }
    }
    return n;
}

constexpr int kPadPartsMost = 19;

// The height of the gamepad's rows: as many rows as the longer side has.
float pad_height()
{
    PadPart parts[kPadPartsMost];
    const int n = pad_parts(pad_family(), parts);
    int side[2] = {0, 0};
    for (int i = 0; i < n; ++i) {
        ++side[parts[i].side];
    }
    return (float)std::max(side[0], side[1]) * std::round(ImGui::GetFontSize() * 1.3f) + ImGui::GetFontSize();
}

// What an input does: the actions it is bound to (and, while the D-pad
// command menu is on, the field layer's actions after them).
std::string pad_actions(int input)
{
    std::string text;
    const int layers = m.work.enh.dpad_command_menu ? KHDAYS_LAYER_COUNT : KHDAYS_LAYER_BASE + 1;
    for (int layer = 0; layer < layers; ++layer) {
        for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
            for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
                if (m.work.input[KHDAYS_DEVICE_GAMEPAD][layer][a][s] == input) {
                    if (!text.empty()) {
                        text += layer == KHDAYS_LAYER_FIELD ? " / " : ", ";
                    }
                    text += khdays_model_action_label(a);
                }
            }
        }
    }
    return text;
}

// Draws the gamepad's picture in the middle, with a column of its inputs on
// each side (top to bottom), each an icon and what it does. What is held right
// now is highlighted.
const char *controller_icon()
{
    switch (pad_family()) {
    case KHDAYS_PAD_PLAYSTATION:
        return pad_type() == SDL_GAMEPAD_TYPE_PS3   ? "controller_playstation3"
               : pad_type() == SDL_GAMEPAD_TYPE_PS4 ? "controller_playstation4"
                                                    : "controller_playstation5";
    case KHDAYS_PAD_NINTENDO: return "controller_switch_pro";
    default: return "controller_xboxseries";
    }
}

void draw_pad(ImVec2 p, float w, float h)
{
    PadPart parts[kPadPartsMost];
    const int n = pad_parts(pad_family(), parts);
    const KhdaysDeviceState *state = khdays_devices_state();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float font = ImGui::GetFontSize(), line = std::round(font * 1.3f);
    const float icon_size = line * 0.95f, space = font * 0.4f;
    const ImU32 lit = IM_COL32(255, 214, 100, 255);
    // the gamepad's picture, as tall as the columns; in the HD menu's pieces its own picture of
    // the family (a PlayStation, an Xbox, another), as wide as its shape
    const ImVec2 centre(p.x + w / 2, p.y + h / 2);
    const char *hd_pad = pad_family() == KHDAYS_PAD_PLAYSTATION ? "pad_playstation"
                         : pad_family() == KHDAYS_PAD_NINTENDO  ? "pad_other"
                                                                : "pad_xbox";
    float hd_w = 0, hd_h = 0;
    const bool hd = m.skin && khdays_skin_size(hd_pad, &hd_w, &hd_h);
    const float tall = std::min(h - font, 256.0f);
    const float picture = hd ? tall * hd_w / hd_h : tall; // its width, which the columns stand beside
    if (hd) {
        khdays_skin_piece(dl, hd_pad, "shown", ImVec2(centre.x - picture / 2, centre.y - tall / 2),
                          ImVec2(centre.x + picture / 2, centre.y + tall / 2));
    } else if (SDL_Texture *gamepad = icon(controller_icon())) {
        draw_icon(dl, gamepad, centre, picture);
    }
    auto held = [&](const PadPart &q) {
        if (q.input == KHDAYS_PAD_LEFTTRIGGER || q.input == KHDAYS_PAD_RIGHTTRIGGER) {
            return state->trigger[q.input == KHDAYS_PAD_RIGHTTRIGGER] > 16384;
        }
        if (q.stick >= 0 &&
            (std::abs((int)state->stick[q.stick][0]) > 16384 || std::abs((int)state->stick[q.stick][1]) > 16384)) {
            return true; // a stick pushed counts as held
        }
        return state->pad[q.input] != 0;
    };
    for (int side = 0; side < 2; ++side) {
        int order[kPadPartsMost], count = 0;
        for (int i = 0; i < n; ++i) {
            if (parts[i].side == side) {
                order[count++] = i;
            }
        }
        const float top = p.y + (h - (float)count * line) / 2;
        for (int k = 0; k < count; ++k) {
            const PadPart &q = parts[order[k]];
            // the row's pieces: an icon (or the input's name if there is no
            // icon) and what it does; for a stick, what moving it does, then
            // what its click does
            struct Piece {
                SDL_Texture *icon;
                std::string text;
            };
            std::vector<Piece> pieces;
            auto add_input = [&](int input) {
                const std::string does = pad_actions(input);
                SDL_Texture *texture = icon(icon_name(input));
                char name[96];
                pad_input_label(input, name, sizeof(name));
                pieces.push_back({texture, texture != nullptr
                                               ? (does.empty() ? "-" : does)
                                               : std::string(name) + ": " + (does.empty() ? "-" : does)});
            };
            if (q.stick >= 0) {
                static const int roles[] = {TXT_NOTHING, TXT_DPAD_MOVE, TXT_CAMERA};
                const int role = m.work.stick[q.stick].role;
                const std::string moving = khdays_text(roles[role >= 0 && role <= 2 ? role : 0]);
                SDL_Texture *texture = icon(stick_icon(q.stick));
                pieces.push_back(
                    {texture,
                     texture != nullptr
                         ? moving
                         : std::string(khdays_text(q.stick == 0 ? TXT_LEFT_STICK : TXT_RIGHT_STICK)) + ": " + moving});
            }
            add_input(q.input);
            float width = 0;
            for (const Piece &piece : pieces) {
                width += icon_size + space + width_of(piece.text.c_str()) + space * 2;
            }
            const float y = top + (float)k * line, mid = y + line / 2;
            const bool now = held(q);
            // the left column ends at the picture, the right one starts there
            float x = side == 0 ? std::max(p.x, centre.x - picture / 2 - font - width) : centre.x + picture / 2 + font;
            const float right_edge = side == 0 ? centre.x - picture / 2 - font : p.x + w;
            for (const Piece &piece : pieces) {
                if (piece.icon != nullptr) {
                    if (now) {
                        dl->AddCircleFilled(ImVec2(x + icon_size / 2, mid), icon_size * 0.6f, lit, 32);
                    }
                    draw_icon(dl, piece.icon, ImVec2(x + icon_size / 2, mid), icon_size);
                }
                x += icon_size + space; // keep the space even without an icon, so the texts line up
                text_clipped(ImVec2(x, mid - font / 2), now ? lit : kGrey, piece.text.c_str(), right_edge);
                x += width_of(piece.text.c_str()) + space * 2;
            }
        }
    }
}

// A row's value in the HD menu's pieces, from `at` (its place's left, the row's top) to the
// row's end `q`: a choice's between its arrows, a number's slider and the number at the row's
// end, a switch's check when on before its word. Returns false for the others (drawn as
// text); `*end` is where the value's place ends.
bool draw_skin_value(ImDrawList *dl, const KhdaysRow &r, const std::string &v, ImVec2 at, ImVec2 q, float row_h,
                     float pad, float ty, ImU32 colour, bool focus, float *end)
{
    float cw = 0, ch = 0;
    if (!khdays_skin_size("row", &cw, &ch)) {
        return false;
    }
    const float k = row_h * 0.88f / ch; // the pieces' size against the capsule's
    const float mid = at.y + row_h / 2;
    if (r.kind == KHDAYS_ROW_CHOICE) {
        float aw = 0, ah = 0;
        if (!khdays_skin_size("arrow_left", &aw, &ah)) {
            return false;
        }
        const float w = aw * k * 0.7f, h = ah * k * 0.7f, gap = row_h * 0.15f;
        khdays_skin_piece(dl, "arrow_left", "shown", ImVec2(at.x, mid - h / 2), ImVec2(at.x + w, mid + h / 2));
        const float tx = at.x + w + gap;
        text_at(ImVec2(tx, ty), colour, v.c_str());
        const float rx = tx + width_of(v.c_str()) + gap;
        khdays_skin_piece(dl, "arrow_right", "shown", ImVec2(rx, mid - h / 2), ImVec2(rx + w, mid + h / 2));
        *end = rx + w;
        return true;
    }
    if (r.kind == KHDAYS_ROW_NUMBER && r.high > r.low) {
        // the number as wide as the widest it can be, at the row's end; the slider before it
        char widest[32];
        SDL_snprintf(widest, sizeof(widest), "%d", std::abs(r.low) > std::abs(r.high) ? r.low : r.high);
        const float nw = width_of(widest), right = q.x - pad;
        const float th = row_h * 0.88f * 24.0f / ch; // the track's height against the capsule's
        const ImVec2 a(at.x, mid - th / 2), b(right - nw - pad, mid + th / 2);
        if (b.x - a.x < row_h || !khdays_skin_slider(dl, focus ? "selected" : "shown", a, b,
                                                     (float)(*r.value - r.low) / (float)(r.high - r.low))) {
            return false;
        }
        text_at(ImVec2(right - width_of(v.c_str()), ty), colour, v.c_str());
        *end = right;
        return true;
    }
    if (r.kind == KHDAYS_ROW_SWITCH) {
        float sw = 0, sh = 0;
        if (!khdays_skin_size("check", &sw, &sh)) {
            return false;
        }
        const float w = sw * k, h = sh * k;
        if (*r.value) {
            khdays_skin_piece(dl, "check", "shown", ImVec2(at.x, mid - h / 2), ImVec2(at.x + w, mid + h / 2));
        }
        text_at(ImVec2(at.x + w + row_h * 0.2f, ty), colour, v.c_str());
        *end = at.x + w + row_h * 0.2f + std::max(width_of(v.c_str()), row_h);
        return true;
    }
    return false;
}

void draw_rows(float row_h, float pad)
{
    const float w = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetFontSize();
    float label_w = 0, cell_w = 0;
    bool bindings = false;
    for (int i = 0; i < m.count; ++i) {
        const KhdaysRow &r = m.rows[i];
        if (r.kind == KHDAYS_ROW_BINDING) {
            bindings = true;
            label_w = std::max(label_w, width_of(r.label));
            for (int c = 0; c < KHDAYS_CELLS; ++c) {
                cell_w = std::max(cell_w, width_of(cell_text(r, c).c_str()) + cell_icon_room(r, c));
            }
        } else if (selectable(r) && r.kind != KHDAYS_ROW_COMMAND) { // a command's name spans the row
            label_w = std::max(label_w, width_of(r.label));
        }
    }
    if (bindings) {
        for (int d = 0; d < KHDAYS_DEVICE_COUNT; ++d) {
            char head[64];
            SDL_snprintf(head, sizeof(head), "%s %d", khdays_text(d == 0 ? TXT_KEYBOARD : TXT_GAMEPAD),
                         KHDAYS_INPUTS_PER_ACTION);
            cell_w = std::max(cell_w, width_of(head));
        }
        cell_w += gap;
        // the window is too narrow for the table: the names and the cells share
        // its width, each cut off at its end
        const float room = w - pad - gap;
        constexpr float cells = KHDAYS_CELLS;
        if (label_w + cells * cell_w > room) {
            label_w = std::min(label_w, room * 0.3f);
            cell_w = (room - label_w) / cells;
        }
    }
    const float value_x = pad + label_w + gap;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // where each row lies in the list, for scrolling to it
    const float scroll = ImGui::GetScrollY(), window_y = ImGui::GetWindowPos().y;
    m.row_top.assign((size_t)m.count, 0.0f);
    m.row_bottom.assign((size_t)m.count, 0.0f);

    // the section's name
    text_at(ImGui::GetCursorScreenPos(), heading_colour(), khdays_model_section_title(m.section));
    ImGui::Dummy(ImVec2(w, row_h));
    for (int i = 0; i < m.count; ++i) {
        const KhdaysRow &r = m.rows[i];
        if (r.kind == KHDAYS_ROW_BINDING && (i == 0 || m.rows[i - 1].kind != KHDAYS_ROW_BINDING)) {
            // the column headings, above the first binding of each group
            const ImVec2 p = ImGui::GetCursorScreenPos();
            for (int d = 0; d < KHDAYS_DEVICE_COUNT; ++d) {
                char head[64];
                for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
                    SDL_snprintf(head, sizeof(head), "%s %d", khdays_text(d == 0 ? TXT_KEYBOARD : TXT_GAMEPAD), s + 1);
                    text_at(ImVec2(p.x + value_x + (float)(d * KHDAYS_INPUTS_PER_ACTION + s) * cell_w, p.y),
                            heading_colour(), head);
                }
            }
            ImGui::Dummy(ImVec2(w, row_h));
        }
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = r.kind == KHDAYS_ROW_STICKS ? row_h * 4.5f : r.kind == KHDAYS_ROW_PAD ? pad_height() : row_h;
        const ImVec2 q(p.x + w, p.y + h);
        const bool focus = m.in_rows && i == m.row && (m.mode == MODE_BROWSE || m.mode == MODE_TEXT);
        const float ty = p.y + (row_h - ImGui::GetFontSize()) / 2;
        const ImU32 colour = !r.available ? kGrey : r.kind == KHDAYS_ROW_HEADING ? heading_colour() : kWhite;
        // in the HD menu's pieces: the row's capsule, a little inside its height
        const ImVec2 capsule_min(p.x, p.y + row_h * 0.06f), capsule_max(q.x, p.y + row_h * 0.94f);
        m.row_top[(size_t)i] = p.y - window_y + scroll;
        m.row_bottom[(size_t)i] = p.y - window_y + scroll + h;
        if (m.skin && selectable(r)) {
            // a switch's, a number's or a row's; dimmed when it cannot change, the selected
            // one over it for the focus (a binding's selected cell has its own)
            const char *piece = r.kind == KHDAYS_ROW_SWITCH ? "switch" : r.kind == KHDAYS_ROW_NUMBER ? "number" : "row";
            if (r.available || !khdays_skin_piece(dl, piece, "dimmed", capsule_min, capsule_max)) {
                khdays_skin_piece(dl, piece, "shown", capsule_min, capsule_max);
            }
            if (focus && r.kind != KHDAYS_ROW_BINDING) {
                khdays_skin_piece(dl, piece, "selected", capsule_min, capsule_max);
            }
        } else if (focus && selectable(r)) {
            dl->AddRectFilled(p, q, kFocus);
        }
        switch (r.kind) {
        case KHDAYS_ROW_LINE: text_at(ImVec2(p.x + pad, ty), kGrey, r.label); break;
        case KHDAYS_ROW_HEADING: text_at(ImVec2(p.x + pad, ty), heading_colour(), r.label); break;
        case KHDAYS_ROW_TEST: {
            char line[160];
            SDL_snprintf(line, sizeof(line), khdays_text(TXT_TEST_PRESS),
                         m.test_action >= 0    ? khdays_model_action_label(m.test_action)
                         : m.test_action == -1 ? khdays_text(TXT_NOTHING)
                                               : "...");
            text_at(ImVec2(p.x + pad, ty), kGrey, line);
            break;
        }
        case KHDAYS_ROW_STICKS: draw_sticks(p, w, h); break;
        case KHDAYS_ROW_PAD: draw_pad(p, w, h); break;
        case KHDAYS_ROW_BINDING:
            text_clipped(ImVec2(p.x + pad, ty), colour, r.label, p.x + value_x - gap / 2);
            for (int c = 0; c < KHDAYS_CELLS; ++c) {
                const ImVec2 cp(p.x + value_x + (float)c * cell_w - gap / 4, p.y);
                const ImVec2 cq(cp.x + cell_w - gap / 2, q.y);
                if (!(r.cells & (1u << c))) {
                    continue;
                }
                if (focus && c == m.cell && m.mode == MODE_BROWSE &&
                    !(m.skin && khdays_skin_piece(dl, "row", "selected", ImVec2(cp.x, capsule_min.y),
                                                  ImVec2(cq.x, capsule_max.y)))) {
                    dl->AddRectFilled(cp, cq, kCell);
                }
                {
                    // a gamepad input, with its icon first
                    const float room = cell_icon_room(r, c);
                    if (room > 0) {
                        draw_icon(dl, icon(cell_icon(r, c)), ImVec2(cp.x + gap / 4 + room * 0.45f, (cp.y + cq.y) / 2),
                                  room * 0.9f);
                    }
                    text_clipped(ImVec2(cp.x + gap / 4 + room, ty), colour, cell_text(r, c).c_str(), cq.x);
                }
                record(KHDAYS_PLACE_CELL, i * KHDAYS_CELLS + c, cp, cq);
            }
            break;
        default:
            if (r.kind == KHDAYS_ROW_COMMAND) {
                text_at(ImVec2(p.x + pad, ty), colour, r.label);
            } else {
                text_clipped(ImVec2(p.x + pad, ty), colour, r.label, p.x + value_x - gap / 2);
                const std::string v = value_text(r);
                if (focus && m.mode == MODE_TEXT &&
                    !(m.skin && khdays_skin_piece(dl, "row", "selected", ImVec2(p.x + value_x - gap / 4, capsule_min.y),
                                                  capsule_max))) {
                    dl->AddRectFilled(ImVec2(p.x + value_x - gap / 4, p.y), q, kCell);
                }
                float value_end = p.x + value_x + std::max(width_of(v.c_str()), gap * 4);
                if (!m.skin || !draw_skin_value(dl, r, v, ImVec2(p.x + value_x, p.y), q, row_h, pad, ty, colour, focus,
                                                &value_end)) {
                    text_at(ImVec2(p.x + value_x, ty), colour, v.c_str());
                }
                record(PLACE_VALUE, i, ImVec2(p.x + value_x - gap / 4, p.y), ImVec2(value_end, q.y));
                if (focus && m.mode == MODE_TEXT && m.window != nullptr) {
                    // an input method's candidate window goes next to the field
                    const SDL_Rect area = {(int)((p.x + value_x) / m.density), (int)(p.y / m.density),
                                           (int)((q.x - p.x - value_x) / m.density), (int)(h / m.density)};
                    SDL_SetTextInputArea(m.window, &area, 0);
                }
            }
            break;
        }
        ImGui::Dummy(ImVec2(w, h));
        record(KHDAYS_PLACE_ROW, i, p, q);
    }
}

// Draws a question over the menu: what is asked (white), what it means
// (grey), and, for questions, two answers.
void draw_prompt(float w, float h, float row_h, float pad)
{
    std::string question;
    std::vector<std::string> details;
    const char *yes = nullptr, *no = nullptr, *third = nullptr; // a third answer: the three stacked
    char line[512];
    if (m.mode == MODE_CAPTURE) {
        const Uint64 now = SDL_GetTicks();
        const int left = m.cap_until > now ? (int)((m.cap_until - now + 999) / 1000) : 0;
        SDL_snprintf(line, sizeof(line), khdays_text(TXT_PRESS_FOR),
                     khdays_text(m.cap_device == KHDAYS_DEVICE_KEYBOARD ? TXT_PRESS_KEY : TXT_PRESS_BUTTON),
                     khdays_model_action_label(m.cap_action));
        question = line;
        SDL_snprintf(line, sizeof(line), khdays_text(TXT_TO_CANCEL),
                     khdays_text(m.cap_device == KHDAYS_DEVICE_KEYBOARD ? TXT_ESC : TXT_ESC_OR_BUTTON), left);
        details.push_back(line);
    } else if (m.mode == MODE_CONFLICT) {
        char name[64];
        input_label(m.cap_device, m.cap_input, name, sizeof(name));
        SDL_snprintf(line, sizeof(line), khdays_text(TXT_ALREADY_DOES), name, khdays_model_action_label(m.cap_owner),
                     khdays_model_action_label(m.cap_action));
        question = line;
        yes = khdays_text(TXT_MOVE_IT_HERE);
        no = khdays_text(TXT_CANCEL);
    } else if (m.mode == MODE_PRESET) {
        static const struct {
            unsigned enhancement;
            int text;
        } fallback_texts[] = {
            {KHDAYS_ENH_SEPARATE_LOCK_ON, TXT_FALLBACK_LOCK_ON},
            {KHDAYS_ENH_DPAD_COMMAND_MENU, TXT_FALLBACK_MENU},
            {KHDAYS_ENH_ANALOG_CAMERA, TXT_FALLBACK_CAMERA},
            {KHDAYS_ENH_ANALOG_MOVEMENT, TXT_FALLBACK_MOVEMENT},
        };
        SDL_snprintf(line, sizeof(line), khdays_text(TXT_USE_LAYOUT),
                     m.preset == KHDAYS_PRESET_KH2 ? "Kingdom Hearts II" : "DS");
        question = line;
        if (m.fallbacks != 0) {
            // one line for each thing it cannot do yet
            details.push_back(khdays_text(TXT_UNTIL_AVAILABLE));
            for (const auto &f : fallback_texts) {
                if (m.fallbacks & f.enhancement) {
                    details.push_back(khdays_text(f.text));
                }
            }
        }
        yes = khdays_text(TXT_APPLY);
        no = khdays_text(TXT_CANCEL);
    } else if (m.mode == MODE_QUIT) {
        question = khdays_text(TXT_QUIT_GAME_Q);
        details.push_back(khdays_text(TXT_QUIT_GAME_DETAIL));
        yes = khdays_text(TXT_QUIT_GAME);
        no = khdays_text(TXT_CANCEL);
    } else if (m.mode == MODE_UPDATE && m.update_stage == UPDATE_GETTING) {
        long long done = 0, total = 0;
        m.update_hooks.state(&done, &total, nullptr, 0);
        SDL_snprintf(line, sizeof(line), khdays_text(TXT_UPDATE_GETTING), m.update_new);
        question = line;
        SDL_snprintf(line, sizeof(line), khdays_text(TXT_UPDATE_PROGRESS), total > 0 ? (int)(done * 100 / total) : 0,
                     (double)done / 1048576.0, (double)total / 1048576.0);
        details.push_back(line);
        yes = khdays_text(TXT_CANCEL);
    } else if (m.mode == MODE_UPDATE && m.update_stage == UPDATE_FAILED) {
        question = khdays_text(TXT_UPDATE_FAILED);
        SDL_snprintf(line, sizeof(line), khdays_text(TXT_UPDATE_FAILED_DETAIL), m.update_why);
        details.push_back(line);
        yes = khdays_text(TXT_UPDATE_OPEN_PAGE);
        no = khdays_text(TXT_UPDATE_LATER);
    } else if (m.mode == MODE_UPDATE) {
        question = khdays_text(TXT_UPDATE_TITLE);
        SDL_snprintf(
            line, sizeof(line),
            khdays_text(m.update_hooked && m.update_hooks.can_install() ? TXT_UPDATE_DETAIL_AUTO : TXT_UPDATE_DETAIL),
            m.update_have, m.update_new);
        details.push_back(line);
        yes = khdays_text(TXT_UPDATE_DOWNLOAD);
        no = khdays_text(TXT_UPDATE_LATER);
        third = khdays_text(TXT_UPDATE_SKIP);
    } else {
        SDL_snprintf(line, sizeof(line), khdays_text(TXT_NO_INPUT_PRESSES), m.unbound);
        question = line;
        details.push_back(khdays_text(TXT_CLOSE_ANYWAY_Q));
        yes = khdays_text(TXT_CLOSE_ANYWAY);
        no = khdays_text(TXT_BACK);
    }
    const float box_w = std::min(w * 0.8f, ImGui::GetFontSize() * 36);
    const float wrap = box_w - 2 * pad;
    float text_h = ImGui::CalcTextSize(question.c_str(), nullptr, false, wrap).y;
    for (const std::string &d : details) {
        text_h += ImGui::CalcTextSize(d.c_str(), nullptr, false, wrap).y;
    }
    // in the HD menu's pieces: its question's window, the words below its title
    const float title = m.skin ? row_h * 1.1f : 0.0f;
    const float gap = std::round(pad * 0.5f);
    // as with two answers, a quarter row above them and room for the spacing between the items
    const float answers_h = third != nullptr ? row_h * 0.5f + 3 * row_h + 2 * gap + 2 * ImGui::GetStyle().ItemSpacing.y
                            : yes != nullptr ? row_h * 1.5f
                                             : 0;
    const float box_h = text_h + 2 * pad + title + answers_h;
    const ImVec2 box_at((w - box_w) / 2, (h - box_h) / 2), box_end(box_at.x + box_w, box_at.y + box_h);
    ImGui::SetNextWindowPos(box_at);
    ImGui::SetNextWindowSize(ImVec2(box_w, box_h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m.skin ? IM_COL32(0, 0, 0, 0) : IM_COL32(20, 24, 40, 250));
    ImGui::PushStyleColor(ImGuiCol_Border, kBlue);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,
                        m.skin ? 0.0f : std::max(1.0f, std::round(ImGui::GetFontSize() / 12)));
    ImGui::Begin("##prompt", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav);
    if (m.skin) {
        // behind the words: drawn first, in the window's own list
        ImGui::PushClipRect(box_at, box_end, false);
        khdays_skin_question(ImGui::GetWindowDrawList(), box_at, box_end);
        ImGui::PopClipRect();
        ImGui::Dummy(ImVec2(0, title - ImGui::GetStyle().ItemSpacing.y));
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
    ImGui::TextUnformatted(question.c_str());
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
    for (const std::string &d : details) {
        ImGui::TextUnformatted(d.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    if (third != nullptr) {
        ImGui::Dummy(ImVec2(0, row_h * 0.25f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float bw = box_w - 2 * pad;
        const char *const labels[3] = {yes, no, third};
        for (int b = 0; b < 3; ++b) {
            const ImVec2 a(p.x, p.y + (float)b * (row_h + gap)), z(a.x + bw, a.y + row_h);
            if (m.skin) {
                khdays_skin_piece(ImGui::GetWindowDrawList(), "answer", "shown", a, z);
                if (m.choice == b) {
                    khdays_skin_piece(ImGui::GetWindowDrawList(), "answer", "selected", a, z);
                }
            } else {
                ImGui::GetWindowDrawList()->AddRectFilled(a, z, m.choice == b ? kFocus : kChosen);
            }
            text_at(ImVec2(a.x + (bw - width_of(labels[b])) / 2, a.y + (row_h - ImGui::GetFontSize()) / 2), kWhite,
                    labels[b]);
            record(KHDAYS_PLACE_ANSWER, b, a, z);
        }
        ImGui::Dummy(ImVec2(bw, 3 * row_h + 2 * gap));
    } else if (yes != nullptr) {
        ImGui::Dummy(ImVec2(0, row_h * 0.25f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float bw = (box_w - 3 * pad) / 2;
        const int count = no != nullptr ? 2 : 1; // one answer alone in the middle
        const float first_x = count == 2 ? p.x : p.x + (box_w - 2 * pad - bw) / 2;
        for (int b = 0; b < count; ++b) {
            const ImVec2 a(first_x + (float)b * (bw + pad), p.y), z(a.x + bw, a.y + row_h);
            if (m.skin) {
                khdays_skin_piece(ImGui::GetWindowDrawList(), "answer", "shown", a, z);
                if (m.choice == b) {
                    khdays_skin_piece(ImGui::GetWindowDrawList(), "answer", "selected", a, z);
                }
            } else {
                ImGui::GetWindowDrawList()->AddRectFilled(a, z, m.choice == b ? kFocus : kChosen);
            }
            const char *label = b == 0 ? yes : no;
            text_at(ImVec2(a.x + (bw - width_of(label)) / 2, a.y + (row_h - ImGui::GetFontSize()) / 2), kWhite, label);
            record(KHDAYS_PLACE_ANSWER, b, a, z);
        }
        ImGui::Dummy(ImVec2(box_w - 2 * pad, row_h));
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void start_imgui(SDL_Renderer *renderer)
{
    if (m.imgui == nullptr) {
        IMGUI_CHECKVERSION();
        m.imgui = ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr; // ImGui writes no file next to the game
        io.LogFilename = nullptr;
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false; // the font is the executable's own bytes
        SDL_strlcpy(config.Name, "Roboto Medium", sizeof(config.Name));
        m.font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(khdays_menu_font),
                                                (int)sizeof(khdays_menu_font), 18.0f, &config);
        io.FontDefault = m.font;
    }
    ImGui::SetCurrentContext(m.imgui);
    if (m.bound != renderer) {
        if (m.bound != nullptr) {
            ImGui_ImplSDLRenderer3_Shutdown();
        }
        ImGui_ImplSDLRenderer3_Init(renderer);
        m.bound = renderer;
    }
}

// ---- The menu's sounds (settings_ui.h) -------------------------------------------

void (*cue_player)(int which, const KhdaysSound *levels);

void cue(int which)
{
    if (cue_player != nullptr) {
        cue_player(which, &m.work.sound);
    }
}

// Where the focus is, to hear what an event or a moment changed.
struct Focus {
    bool open, in_rows;
    int mode, column, section, row, cell, choice;
    bool operator==(const Focus &) const = default;
};

Focus focus_now()
{
    return Focus{m.open, m.in_rows, (int)m.mode, m.column, m.section, m.row, m.cell, m.choice};
}

enum { INTENT_NONE, INTENT_ACCEPT, INTENT_BACK };

// What the player meant by a press: to choose, to go back, or neither (a move).
int intent_of(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        switch (e->key.scancode) {
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE: return INTENT_ACCEPT;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_BACKSPACE: return INTENT_BACK;
        default: return INTENT_NONE;
        }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        const SDL_GamepadButton button = (SDL_GamepadButton)e->gbutton.button;
        const int input = pad_input_of_button(button);
        if (input >= 0 && is_open_settings_input(input)) {
            return INTENT_BACK;
        }
        switch (face_of(button)) {
        case FACE_ACCEPT: return INTENT_ACCEPT;
        case FACE_BACK: return INTENT_BACK;
        default: return INTENT_NONE;
        }
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return e->button.button == SDL_BUTTON_LEFT    ? INTENT_ACCEPT
               : e->button.button == SDL_BUTTON_RIGHT ? INTENT_BACK
                                                      : INTENT_NONE;
    default: return INTENT_NONE;
    }
}

// The sound for what changed since `was` and `before`: the cursor for a move or a value changed, a choice
// made or going back as the press meant, nothing when nothing changed. A volume changed has its own sample.
void cue_after(const Focus &was, const KhdaysProfile &before, int intent)
{
    const Focus now = focus_now();
    if (was.open && !now.open) {
        cue(intent == INTENT_ACCEPT ? KHDAYS_SETTINGS_CUE_ACCEPT : KHDAYS_SETTINGS_CUE_BACK);
        return;
    }
    const bool moved = !(was == now);
    const bool edited = std::memcmp(&before, &m.work, sizeof(m.work)) != 0;
    if ((!moved && !edited) || (!moved && std::memcmp(&before.sound, &m.work.sound, sizeof(m.work.sound)) != 0)) {
        return;
    }
    if (was.mode == MODE_CAPTURE) {
        // a key or button taken for a binding
        cue(now.mode == MODE_CAPTURE ? KHDAYS_SETTINGS_CUE_CURSOR : KHDAYS_SETTINGS_CUE_ACCEPT);
        return;
    }
    cue(intent == INTENT_ACCEPT ? KHDAYS_SETTINGS_CUE_ACCEPT
        : intent == INTENT_BACK ? KHDAYS_SETTINGS_CUE_BACK
                                : KHDAYS_SETTINGS_CUE_CURSOR);
}

int handle_event(const SDL_Event *e, SDL_Window *window);

} // namespace

// ---- The interface -------------------------------------------------------------

extern "C" {

void khdays_settings_open(const KhdaysProfile *current)
{
    m.open = true;
    m.changed = false;
    m.work = m.original = m.reported = *current;
    m.column = m.section = 0;
    m.in_rows = false;
    m.row = m.cell = 0;
    m.mode = MODE_BROWSE;
    m.held_dir = 0;
    m.test_action = -2;
    m.scroll_to_top = true;
    settle();
    cue(KHDAYS_SETTINGS_CUE_OPEN);
}

void khdays_settings_set_cue(void (*player)(int which, const KhdaysSound *levels))
{
    cue_player = player;
}

void khdays_settings_offer_update(const KhdaysProfile *current, const char *have, const char *latest,
                                  const KhdaysUpdateHooks *hooks)
{
    khdays_settings_open(current);
    m.mode = MODE_UPDATE;
    m.choice = 0;
    m.update_answer = -1;
    m.update_stage = UPDATE_OFFER;
    m.update_hooked = hooks != nullptr;
    m.update_hooks = hooks != nullptr ? *hooks : KhdaysUpdateHooks{};
    m.update_why[0] = '\0';
    SDL_snprintf(m.update_have, sizeof(m.update_have), "%s%s", have[0] == 'v' ? "" : "v", have);
    SDL_strlcpy(m.update_new, latest, sizeof(m.update_new));
}

int khdays_settings_take_update_answer(void)
{
    const int answer = m.update_answer;
    m.update_answer = -1;
    return answer;
}

int khdays_settings_is_open(void)
{
    return m.open;
}

int khdays_settings_changed(void)
{
    return m.changed;
}

const KhdaysProfile *khdays_settings_profile(void)
{
    return &m.work;
}

int khdays_settings_profile_changed(void)
{
    return m.open && std::memcmp(&m.work, &m.original, sizeof(m.work)) != 0;
}

unsigned khdays_settings_take_changes(void)
{
    const unsigned changes = khdays_model_changes(&m.reported, &m.work);
    m.reported = m.work;
    return changes;
}

void khdays_settings_set_situation(int in_session, int hd_videos)
{
    m.in_session = in_session;
    m.hd_videos = hd_videos;
}

void khdays_settings_set_guest(int guest)
{
    m.guest = guest;
}

void khdays_settings_set_graphics_card(int available)
{
    m.graphics_card = available;
}

int khdays_settings_handle(const SDL_Event *e, SDL_Window *window)
{
    if (!m.open) {
        return 0;
    }
    const Focus was = focus_now();
    const KhdaysProfile before = m.work;
    const int intent = intent_of(e);
    const int closed = handle_event(e, window);
    cue_after(was, before, intent);
    return closed;
}

} // extern "C"

namespace {

int handle_event(const SDL_Event *e, SDL_Window *window)
{
    bool closed = false;
    if (!m.open) {
        return 0;
    }
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: m.last_device = KHDAYS_DEVICE_KEYBOARD; break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: m.last_device = KHDAYS_DEVICE_GAMEPAD; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL: m.last_device = 2; break;
    default: break;
    }
    m.window = window;
    m.density = window != nullptr && SDL_GetWindowPixelDensity(window) > 0 ? SDL_GetWindowPixelDensity(window) : 1.0f;
    if (m.mode == MODE_UPDATE) {
        return update_event(e);
    }
    if (m.mode == MODE_CAPTURE) {
        capture_event(e);
        settle();
        return 0;
    }
    if (m.mode == MODE_TEXT) {
        text_event(e);
        settle();
        return 0;
    }
    if (m.mode == MODE_CONFLICT || m.mode == MODE_UNBOUND || m.mode == MODE_PRESET || m.mode == MODE_QUIT) {
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (e->button.button == SDL_BUTTON_LEFT) {
                closed = mouse_click(e->button.x * m.density, e->button.y * m.density);
            } else if (e->button.button == SDL_BUTTON_RIGHT) {
                closed = answer_prompt(false);
            }
        } else {
            closed = prompt_event(e);
        }
        if (!closed) {
            settle();
        }
        return closed;
    }
    if (m.section == KHDAYS_SECTION_CONTROLS) {
        note_test_input(e);
    }

    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        switch (e->key.scancode) {
        case SDL_SCANCODE_UP: navigate(1); break;
        case SDL_SCANCODE_DOWN: navigate(2); break;
        case SDL_SCANCODE_LEFT: navigate(3); break;
        case SDL_SCANCODE_RIGHT: navigate(4); break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE: closed = m.in_rows ? activate() : column_enter(); break;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_BACKSPACE: closed = go_back(); break;
        case SDL_SCANCODE_DELETE: clear_cell(); break;
        default: break;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        const SDL_GamepadButton button = (SDL_GamepadButton)e->gbutton.button;
        const int input = pad_input_of_button(button);
        if (input >= 0 && is_open_settings_input(input)) {
            closed = try_close(false);
            break;
        }
        switch (face_of(button)) {
        case FACE_ACCEPT: closed = m.in_rows ? activate() : column_enter(); break;
        case FACE_BACK: closed = go_back(); break;
        case FACE_CLEAR: clear_cell(); break;
        default:
            switch (button) {
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: switch_section(-1); break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: switch_section(1); break;
            default:
                if (direction_of(button)) {
                    m.held_dir = direction_of(button);
                    m.repeat_at = SDL_GetTicks() + kRepeatDelayMs;
                    navigate(m.held_dir);
                }
                break;
            }
            break;
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (direction_of((SDL_GamepadButton)e->gbutton.button) == m.held_dir) {
            m.held_dir = 0;
        }
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            // The left stick navigates like the D-pad: one step each time an
            // axis passes half its travel.
            static int axis_dir[2]; // x, y: the direction each gave last
            const int vertical = e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY;
            const int v = e->gaxis.value;
            const int dir = v < -16384 ? (vertical ? 1 : 3) : v > 16384 ? (vertical ? 2 : 4) : 0;
            if (dir != 0 && dir != axis_dir[vertical]) {
                navigate(dir);
            }
            axis_dir[vertical] = dir;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        // only the mouse's own moves: when the window moves or grows under a still pointer (a
        // new display mode) SDL says the mouse moved too, and the focus would jump to the row
        // the pointer is over
        static float last_x = -1e9f, last_y = -1e9f;
        float gx = 0, gy = 0;
        SDL_GetGlobalMouseState(&gx, &gy);
        const bool moved = gx != last_x || gy != last_y;
        last_x = gx;
        last_y = gy;
        if (moved) {
            mouse_focus(e->motion.x * m.density, e->motion.y * m.density);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button == SDL_BUTTON_RIGHT) {
            closed = go_back();
        } else if (e->button.button == SDL_BUTTON_LEFT) {
            closed = mouse_click(e->button.x * m.density, e->button.y * m.density);
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: m.wheel += e->wheel.y; break;
    default: break;
    }
    if (!closed) {
        settle();
    }
    return closed;
}

} // namespace

extern "C" {

void khdays_settings_tick(void)
{
    const Uint64 now = SDL_GetTicks();
    if (!m.open) {
        return;
    }
    const Focus was = focus_now();
    const KhdaysProfile before = m.work;
    update_tick();
    if (m.mode == MODE_CAPTURE && now >= m.cap_until) {
        m.mode = MODE_BROWSE; // nothing pressed in time: unchanged
    }
    if (m.mode == MODE_BROWSE && m.held_dir != 0 && now >= m.repeat_at) {
        navigate(m.held_dir);
        m.repeat_at = now + kRepeatEveryMs;
        settle();
    }
    if (m.mode == MODE_TEXT && m.held_dir != 0 && now >= m.repeat_at) {
        text_direction(m.held_dir);
        m.repeat_at = now + kRepeatEveryMs;
    }
    if (m.mode == MODE_BROWSE || m.mode == MODE_TEXT) {
        cue_after(was, before, INTENT_NONE); // a held direction's steps
    }
}

void khdays_settings_renderer_gone(void)
{
    forget_icons(); /* textures of the renderer that goes */
    khdays_skin_renderer_gone();
    if (m.imgui != nullptr && m.bound != nullptr) {
        ImGui::SetCurrentContext(m.imgui);
        ImGui_ImplSDLRenderer3_Shutdown();
        m.bound = nullptr;
    }
}

void khdays_settings_draw(SDL_Renderer *renderer, SDL_Window *window)
{
    SDL_RendererLogicalPresentation mode;
    int lw = 0, lh = 0, w = 0, h = 0;
    if (!m.open || renderer == nullptr) {
        return;
    }
    if (window != nullptr) {
        m.window = window;
        m.density = SDL_GetWindowPixelDensity(window) > 0 ? SDL_GetWindowPixelDensity(window) : 1.0f;
    }
    // Drawn in the window's own pixels, over the letterboxed picture: the
    // output's size is the logical presentation's while that is on.
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) {
        SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
        return;
    }
    start_imgui(renderer);
    // the HD menu's pieces when a mod has them for the menu's language (settings_skin.h); its font
    // added before the frame begins
    m.skin = khdays_skin_use(renderer, khdays_ui_language());
    if (m.skin && m.skin_font == nullptr) {
        m.skin_font = khdays_skin_font(ImGui::GetIO().Fonts, khdays_menu_font, (int)sizeof(khdays_menu_font));
    }
    settle();

    // The text's size follows the window's height; the spacing with it.
    const float size = std::max(13.0f, std::min((float)h / 40.0f, (float)w / 64.0f));
    if (size != m.styled_size) {
        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);
        style.ScaleAllSizes(size / 13.0f);
        style.WindowBorderSize = 0;
        style.ChildBorderSize = 0;
        ImGui::GetStyle() = style;
        m.styled_size = size;
    }
    ImGuiIO &io = ImGui::GetIO();
    const Uint64 now = SDL_GetTicks();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DeltaTime = m.last_draw != 0 && now > m.last_draw ? (float)(now - m.last_draw) / 1000.0f : 1.0f / 60.0f;
    m.last_draw = now;
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui::NewFrame();
    // the HD menu's font draws its letters smaller in its cells: a little larger for the same reading
    ImGui::PushFont(m.skin && m.skin_font != nullptr ? m.skin_font : m.font,
                    m.skin && m.skin_font != nullptr ? size * kSkinFontScale : size);
    m.places.clear();

    const float row_h = std::round(ImGui::GetFontSize() * 1.6f);
    // in the pieces the words stay clear of the capsules' round ends
    const float pad = std::round(m.skin ? row_h * 0.45f : ImGui::GetFontSize() * 0.6f);
    const float margin = std::round(ImGui::GetFontSize());
    const ImVec2 screen((float)w, (float)h);

    if (m.mode == MODE_UPDATE) {
        // the notice alone, over the game dimmed: the menu itself is not shown
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(0, 0, 0, 170));
        draw_prompt((float)w, (float)h, row_h, pad);
        ImGui::PopFont();
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
        return;
    }

    if (m.skin) {
        // the HD menu's screen over the game; the menu inside its band, the hints on its bar
        khdays_skin_screen(ImGui::GetBackgroundDrawList(), screen);
        // the faded picture of the section shown (the HD menu's own five, the port's choice of one
        // for each of its sections: KHDAYS_SECTION_*)
        static const char *const emblems[KHDAYS_SECTION_COUNT] = {"emblem_keyboard", "emblem_pad",    "emblem_pad",
                                                                  "emblem_screen",   "emblem_sound",  "emblem_pad",
                                                                  "emblem_film",     "emblem_screen", "emblem_screen"};
        if (m.section >= 0 && m.section < KHDAYS_SECTION_COUNT) {
            khdays_skin_place(ImGui::GetBackgroundDrawList(), emblems[m.section], screen);
        }
        const ImVec2 a = khdays_skin_point(screen, kSkinArea[0], kSkinArea[1]);
        const ImVec2 b = khdays_skin_point(screen, kSkinArea[2], kSkinArea[3]);
        ImGui::SetNextWindowPos(a);
        ImGui::SetNextWindowSize(ImVec2(b.x - a.x, b.y - a.y));
    } else {
        // the game, dimmed
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(0, 0, 0, 170));
        ImGui::SetNextWindowPos(ImVec2(margin, margin));
        ImGui::SetNextWindowSize(ImVec2((float)w - 2 * margin, (float)h - 2 * margin));
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m.skin ? IM_COL32(0, 0, 0, 0) : kPanel);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    // in the pieces the scroll bar in their greys
    const int scroll_colours = m.skin ? 4 : 0;
    if (m.skin) {
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(110, 110, 110, 200));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(160, 160, 160, 220));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, IM_COL32(200, 200, 200, 240));
    }
    ImGui::Begin("##settings", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
    {
        const float inner = ImGui::GetContentRegionAvail().x;
        // the header: the menu's name (the pieces' tab has it), then the notices
        if (!m.skin) {
            text_at(ImGui::GetCursorScreenPos(), kWhite, khdays_text(TXT_SETTINGS));
            ImGui::Dummy(ImVec2(inner, row_h));
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
        ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
        for (int n = 0; n < khdays_settings_notice_count(); ++n) {
            ImGui::TextUnformatted(khdays_settings_notice(n));
        }
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        // the footer, measured first: the chosen row's note, then the hints (in the pieces on
        // their own bar, below)
        const std::string note = footer_note(), warning = footer_warning();
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        const float footer =
            (note.empty() ? 0 : ImGui::CalcTextSize(note.c_str(), nullptr, false, inner).y + spacing) +
            (warning.empty() ? 0 : ImGui::CalcTextSize(warning.c_str(), nullptr, false, inner).y + spacing) +
            (m.skin ? 0 : ImGui::CalcTextSize(hints(), nullptr, false, inner).y) + 2 * spacing;
        const float body = std::max(row_h, ImGui::GetContentRegionAvail().y - footer);

        // the column: as wide as its longest entry
        float column_w = width_of(khdays_text(TXT_QUIT_GAME));
        for (int s = 0; s < KHDAYS_SECTION_COUNT; ++s) {
            column_w = std::max(column_w, width_of(khdays_model_section_title(s)));
        }
        column_w += 3 * pad;
        ImGui::BeginChild("##column", ImVec2(column_w, body), ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
        draw_column(row_h, pad);
        ImGui::EndChild();
        if (m.skin) {
            // the pieces' line between the sections and the rows
            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            float line_w = 0, line_h = 0;
            if (khdays_skin_size("line", &line_w, &line_h)) {
                const float x = hi.x + pad / 2, half = khdays_skin_point(screen, line_w, 0).x / 2;
                khdays_skin_piece(ImGui::GetWindowDrawList(), "line", "shown", ImVec2(x - half, lo.y),
                                  ImVec2(x + half, hi.y));
            }
        }
        ImGui::SameLine(0, pad);

        // the rows: scrolled only when they do not fit (to the chosen one), at this frame's height --
        // and again when the footer's height changes it (a row's warning showing)
        float scroll = -1;
        const bool resized = m.rows_height > 0 && std::fabs(body - m.rows_height) > 0.5f;
        if (m.scroll_to_top) {
            scroll = 0;
            m.scroll_to_top = false;
            m.scroll_to_focus = false;
        } else if ((m.scroll_to_focus || resized) && m.in_rows && (size_t)m.row < m.row_top.size()) {
            const float t = m.row_top[(size_t)m.row] - pad, b = m.row_bottom[(size_t)m.row] + pad;
            if (t < m.rows_scroll) {
                scroll = std::max(0.0f, t);
            } else if (b > m.rows_scroll + body) {
                scroll = b - body;
            }
        }
        m.scroll_to_focus = false;
        if (m.wheel != 0) {
            scroll = std::max(0.0f, (scroll >= 0 ? scroll : m.rows_scroll) - m.wheel * row_h * 3);
            m.wheel = 0;
        }
        if (scroll >= 0) {
            ImGui::SetNextWindowScroll(ImVec2(-1.0f, scroll));
        }
        ImGui::BeginChild("##rows", ImVec2(0, body), ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
        draw_rows(row_h, pad);
        m.rows_scroll = ImGui::GetScrollY();
        m.rows_height = ImGui::GetWindowHeight();
        ImGui::EndChild();

        // the footer, clear of the version at its right end
        char version[64];
        SDL_snprintf(version, sizeof(version), khdays_text(TXT_VERSION), KHDAYS_VERSION);
        const float version_w = width_of(version) + pad;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(inner * 0.5f, inner - version_w));
        ImGui::PushStyleColor(ImGuiCol_Text, m.skin ? kSkinHeading : kGrey);
        if (!note.empty()) {
            ImGui::TextUnformatted(note.c_str());
        }
        if (!warning.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
            ImGui::TextUnformatted(warning.c_str());
            ImGui::PopStyleColor();
        }
        if (!m.skin) {
            ImGui::TextUnformatted(hints());
        }
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        {
            // the port's version, in the window's bottom right corner
            const ImVec2 lo = ImGui::GetWindowPos(), extent = ImGui::GetWindowSize();
            const ImVec2 at(lo.x + extent.x - ImGui::GetStyle().WindowPadding.x - width_of(version),
                            lo.y + extent.y - ImGui::GetStyle().WindowPadding.y - ImGui::GetFontSize());
            text_at(at, kGrey, version);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2 + scroll_colours);
    if (m.skin) {
        // the hints on the pieces' bar, in the middle of its height, cut off at its end
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        khdays_skin_bounds("hints", &x0, &y0, &x1, &y1);
        const ImVec2 a = khdays_skin_point(screen, x0, y0), b = khdays_skin_point(screen, x1, y1);
        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        khdays_skin_piece(dl, "hints", "shown", a, b);
        const float size_now = ImGui::GetFontSize();
        const ImVec4 clip(a.x, a.y, b.x - pad, b.y);
        dl->AddText(ImGui::GetFont(), size_now, ImVec2(a.x + pad * 2, (a.y + b.y - size_now) / 2), kGrey, hints(),
                    nullptr, 0.0f, &clip);
    }
    if (m.mode != MODE_BROWSE && m.mode != MODE_TEXT) {
        draw_prompt((float)w, (float)h, row_h, pad);
    }
    ImGui::PopFont();
    ImGui::Render();

    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
}

int khdays_settings_place(int what, int index, SDL_FRect *out)
{
    const Place *p = place_of(what, index);
    if (p == nullptr) {
        return 0;
    }
    *out = p->rect;
    return 1;
}

void khdays_settings_focus(int *section, int *in_rows, int *row, int *cell)
{
    *section = m.section;
    *in_rows = m.in_rows;
    *row = m.row;
    *cell = m.cell;
}

int khdays_settings_pad_family(void)
{
    return pad_family();
}

const unsigned char *khdays_pad_icon_png(const char *name, unsigned *size)
{
    for (const auto &i : khdays_pad_icons) {
        if (std::strcmp(i.name, name) == 0) {
            *size = i.size;
            return i.png;
        }
    }
    return nullptr;
}

const char *khdays_pad_icon_name(int input)
{
    return icon_name(input);
}

int khdays_settings_row_info(int row, int *id, int *action)
{
    if (row < 0 || row >= m.count) {
        return 0;
    }
    *id = m.rows[row].id;
    // a binding's action, a mod option's place among its mod's
    *action =
        m.rows[row].kind == KHDAYS_ROW_BINDING || m.rows[row].id == KHDAYS_SET_MOD_OPTION ? m.rows[row].action : -1;
    return 1;
}

} // extern "C"
