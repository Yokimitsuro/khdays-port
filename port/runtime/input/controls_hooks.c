/* Where the port's controls meet the game (controls_hooks.h). prepare.py's HOOKS call these from the
 * game's camera, lock-on, command menu, movement heading and speed, actor states and frame-rate mode;
 * each hands back what the game had unless an enhancement that is on changes it.
 * KHDAYS_TRACE_CONTROLS=1 reports what the game does with its controls, only when something changes;
 * docs/RUNTIME_MAP.md records what those traces showed. */
#include "controls_hooks.h"
#include "../core/window.h"
#include "analog.h"
#include "input.h"
#include "walk.h"
#include "../net/net_session.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* The game's own functions for "the local player's actor"
 * (Ov022_SendControlPacket finds it this way). */
extern int QueryActiveStateOrDelegate(void);
extern int GetEntryField20ByIndex(int idx);

static KhdaysGameControls game;
static int tracing = -1;

const KhdaysGameControls *khdays_controls_state(void)
{
    return &game;
}

static int trace_on(void)
{
    if (tracing < 0) {
        tracing = getenv("KHDAYS_TRACE_CONTROLS") != NULL;
    }
    return tracing;
}

static void trace(const char *format, ...)
{
    va_list args;
    if (!trace_on()) {
        return;
    }
    fprintf(stderr, "controls: frame %u ", khdays_input_game_frames());
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

static int local_actor(void)
{
    return GetEntryField20ByIndex(QueryActiveStateOrDelegate());
}

/* `rate` times `axis` (-32767..32767) times `percent` / 100. */
static int scaled(int rate, int axis, int percent)
{
    return (int)((long long)rate * axis * percent / (32767LL * 100));
}

/* ---- Lock-on (Ov022_ReadSelectionInput) ---------------------------------- */

void khdays_controls_selection(int locked, int scheme, int held, int pressed, int flags, int active, int selected,
                               int repeat100, int repeat200, int repeat_any)
{
    static int last_flags = -1, last_active = -1, last_selected = -2;
    game.lock_active = locked != 0;
    game.lockon_scheme = scheme;
    game.selection_frame = khdays_input_game_frames();
    game.r_repeat = repeat_any;
    game.r_pressed = (pressed & 0x100) != 0;
    if (((held | pressed) & 0x300) != 0 || flags != last_flags || active != last_active || selected != last_selected) {
        trace("selection in: held %04x pressed %04x scheme %d flags %08x active %d selected %d repeat R %x L %x"
              " any %x",
              held & 0xffff, pressed & 0xffff, scheme, flags, active, selected, repeat100, repeat200, repeat_any);
    }
    last_flags = flags;
    last_active = active;
    last_selected = selected;
}

/* With the 3D on both screens (Sora's memories) the game runs the memory's battle on every other frame,
 * whose lock-on logic drops the buttons (data_0204be04, the screen shown, is not 0 there): an L or R
 * press read then is kept for the player's next frame, so none is lost. */
#define KEPT_FRAMES 4

int khdays_controls_selection_pressed(int pressed, int other_screen)
{
    static int kept;
    static uint32_t kept_at;
    const uint32_t now = khdays_input_game_frames();
    if (other_screen) {
        if ((pressed & 0x300) != 0) {
            kept |= pressed & 0x300;
            kept_at = now;
        }
        return pressed;
    }
    if (kept != 0 && now - kept_at <= KEPT_FRAMES) {
        pressed |= kept;
    }
    kept = 0;
    return pressed;
}

void khdays_controls_selection_live(int live)
{
    if (live) {
        game.selection_live_frame = khdays_input_game_frames();
    }
}

void khdays_trace_selection_after(int flags, int active, int selected, int value)
{
    static int last_flags = -1, last_active = -1, last_selected = -2, last_value = -1;
    if (flags != last_flags || active != last_active || selected != last_selected || value != last_value) {
        trace("selection out: flags %08x active %d selected %d value %d", flags, active, selected, value);
    }
    last_flags = flags;
    last_active = active;
    last_selected = selected;
    last_value = value;
}

/* ---- The command menu (Ov022_UpdateCommandInput) -------------------------- */

void khdays_controls_command_input(int scheme, int panel_open, int menu_state, int held, int pressed, int menu0,
                                   int menu1, int deck_word)
{
    static int last = -1, last_deck = -1;
    const int now = (panel_open << 16) | (menu_state << 8) | ((menu0 & 0xf) << 4) | (menu1 & 0xf);
    game.command_scheme = scheme;
    if (!panel_open) {
        game.command_input_frame = khdays_input_game_frames();
    }
    if (now != last || deck_word != last_deck || (pressed & 0xfff) != 0) {
        trace("command in: scheme %d panel %d menu state %d menu %d/%d held %04x pressed %04x deck word %04x", scheme,
              panel_open, menu_state, menu0, menu1, held & 0xffff, pressed & 0xffff, deck_word & 0xffff);
    }
    last = now;
    last_deck = deck_word;
}

/* Whether this call is the one reported last, again a frame later: a call made every frame is
 * reported once, until another comes. */
static int repeated(const char *what, int arg)
{
    static const char *last_what;
    static int last_arg;
    static u32 last_frame;
    const u32 frame = khdays_input_game_frames();
    const int same = what == last_what && arg == last_arg && frame - last_frame <= 1;
    last_what = what;
    last_arg = arg;
    last_frame = frame;
    return same;
}

/* A call the traced function makes (its name, and an argument if it has one;
 * or what it returned). */
void khdays_trace_call(const char *what)
{
    if (!repeated(what, 0)) {
        trace("call %s", what);
    }
}

void khdays_trace_call_arg(const char *what, int arg)
{
    if (!repeated(what, arg)) {
        trace("call %s(%d)", what, arg);
    }
}

void *khdays_trace_pointer(const char *what, void *result)
{
    if (!repeated(what, result != NULL)) {
        trace("call %s -> %s", what, result != NULL ? "found" : "none");
    }
    return result;
}

/* ---- The camera (Ov002_Camera_UpdateFollow) -------------------------------- */

/* The mouse camera: the mouse's motion since the camera last took it, as a share of the game's own
 * turn (or tilt) of a frame -- MOUSE_COUNTS_A_FRAME counts at 100 % a frame's -- so its scale is the
 * game's in both directions. What moved while the camera did not follow (another scene, a menu) is
 * not taken. */
#define MOUSE_COUNTS_A_FRAME 40
static int32_t mouse_seen[2];
static u32 mouse_frame[2];
static long long mouse_rest[2]; /* what the division left, for slow motions */

static int mouse_share(const KhdaysInputSnapshot *in, int axis, int rate)
{
    const int32_t total = axis ? in->mouse_y : in->mouse_x;
    const u32 frame = khdays_input_game_frames();
    const long long per = 100LL * MOUSE_COUNTS_A_FRAME;
    long long moved;
    if ((in->enhancements & KHDAYS_ENH_MOUSE_CAMERA) == 0 || in->ui_open) {
        mouse_frame[axis] = 0;
        return 0;
    }
    if (mouse_frame[axis] == 0 || frame - mouse_frame[axis] > 2u) {
        mouse_seen[axis] = total;
        mouse_rest[axis] = 0;
    }
    mouse_frame[axis] = frame != 0 ? frame : 1;
    moved = (long long)rate * (long long)(total - mouse_seen[axis]) * in->mouse_speed + mouse_rest[axis];
    mouse_seen[axis] = total;
    mouse_rest[axis] = moved % per;
    return (int)(moved / per);
}

/* Whether Chase Camera on Manual (the game's Config) keeps the camera's tilt while walking: the field camera's
 * join asks before the game pulls the tilt back to its preset's (port/tools/prepare.py). Off, the game's own. */
int khdays_controls_manual_tilt_kept(void)
{
    return (khdays_input_snapshot()->enhancements & KHDAYS_ENH_MANUAL_CAMERA_TILT) != 0;
}

/* Where the game applies its own turn input (bits 4 and 8 turn, 1 and 2 tilt; `rate` its turn a
 * frame; `inverted` its horizontal inversion, Config 0x37c3): returns the right stick's share of the
 * game's rate to add to the yaw target. Right on the stick turns as every "right" of the game's does
 * (bit 4). */
int khdays_controls_camera_turn(int rate, int inverted, int bits, int flags, int flags2, int yaw, int target, int pitch)
{
    static int last_target = -1, last_pitch = -1, last_bits = -1;
    const KhdaysInputSnapshot *in = khdays_input_snapshot();
    int turn = 0;
    if ((in->enhancements & KHDAYS_ENH_ANALOG_CAMERA) != 0 && !in->ui_open && in->camera_x != 0) {
        turn = -scaled(rate, in->camera_x, in->camera_speed);
    }
    turn -= mouse_share(in, 0, rate); /* right on the mouse as right on the stick */
    if (inverted) {
        turn = -turn;
    }
    if (bits != 0 || turn != 0 || bits != last_bits || target != last_target || pitch != last_pitch) {
        trace("camera: bits %x flags %08x flags2 %08x yaw %04x target %04x rate %x pitch %d inverted %d stick %d", bits,
              flags, flags2, yaw & 0xffff, target & 0xffff, rate, pitch, inverted, turn);
    }
    last_target = target;
    last_pitch = pitch;
    last_bits = bits;
    return turn;
}

/* After the game's own tilt (its tilt a frame, its vertical inversion, Config 0x37c2): returns what
 * to add to the pitch, which the game keeps within its limits. Up on the stick does what the look
 * view's Up does. */
int khdays_controls_camera_pitch(int rate, int inverted)
{
    const KhdaysInputSnapshot *in = khdays_input_snapshot();
    int tilt = 0;
    if ((in->enhancements & KHDAYS_ENH_ANALOG_CAMERA) != 0 && !in->ui_open && in->camera_y != 0) {
        tilt = -scaled(rate, in->camera_y, in->camera_speed);
    }
    tilt -= mouse_share(in, 1, rate); /* up on the mouse as up on the stick */
    if (inverted) {
        tilt = -tilt;
    }
    return tilt;
}

void khdays_trace_camera_request(int found, int x, int y, int kind, int state)
{
    static int last = -1;
    const int now = found ? (x & 0x3ff) | ((y & 0x3ff) << 10) | ((kind & 0xf) << 20) | ((state & 0xf) << 24) : -1;
    if (now != last) {
        trace("camera request: found %d x %d y %d kind %d state %d", found, x, y, kind, state);
    }
    last = now;
}

/* ---- Movement (Ov022_HeadingFromHeldDpad, Ov022_SteerIdleByAim) ---------------- */

/* A movement stick and its player's settings. */
typedef struct Stick {
    int x, y, run_threshold, walk_speed;
} Stick;

#define PARTY 4 /* the game's members: the host's and three guests' */

/* Whether analog movement drives this actor now, and its stick. The local actor's stick is the
 * snapshot before this frame's (its buttons reach the game a frame later); on the host a guest's actor
 * takes the stick that guest sent with its controls (net_session.c). */
static int analog_moving(int actor, Stick *s)
{
    const KhdaysInputSnapshot *in = khdays_input_previous_snapshot();
    if (actor == local_actor()) {
        if ((in->enhancements & KHDAYS_ENH_ANALOG_MOVEMENT) == 0 || (in->move_x == 0 && in->move_y == 0)) {
            return 0;
        }
        s->x = in->move_x;
        s->y = in->move_y;
        s->run_threshold = in->run_threshold;
        s->walk_speed = in->walk_speed;
        return 1;
    }
    for (int member = 1; member < PARTY; ++member) {
        KhdaysNetStick n;
        if (GetEntryField20ByIndex(member) == actor) {
            if (!khdays_net_stick((unsigned int)member, &n) || !n.on || (n.x == 0 && n.y == 0)) {
                return 0;
            }
            s->x = n.x;
            s->y = n.y;
            s->run_threshold = n.run_threshold;
            s->walk_speed = n.walk_speed;
            return 1;
        }
    }
    return 0;
}

/* The heading the game made from the D-pad (-1 for none). With analog movement it becomes the stick's
 * exact angle, only while the game sees a direction held. */
int khdays_controls_move_heading(int actor, int heading)
{
    static int last_heading = -2, last_buttons = -1;
    Stick in;
    int result = heading;
    if (heading != -1 && analog_moving(actor, &in)) {
        result = khdays_analog_heading_turned(khdays_analog_heading(in.x, in.y), heading);
    }
    if (trace_on() && actor == local_actor()) {
        const int buttons = *(u16 *)KHDAYS_W(actor + 0x18) | *(u16 *)KHDAYS_W(actor + 0x1a) << 16;
        if (result != last_heading || buttons != last_buttons) {
            trace("heading: %d (%04x) from the D-pad %04x buttons %04x buttons2 %04x", result, result & 0xffff,
                  heading & 0xffff, buttons & 0xffff, (buttons >> 16) & 0xffff);
        }
        last_heading = result;
        last_buttons = buttons;
    }
    return result;
}

/* A walk this frame: its actor, the game's speed and the walk's (the animation slows as much). */
static struct {
    int actor;
    u32 frame;
    int rate, walk;
} walking;

/* The speed the actor moves toward (its nWalkSpeed): with the stick pushed less than the run
 * threshold, the walk speed's share of it, never more than the game's own. */
int khdays_controls_move_rate(int actor, int rate)
{
    Stick in;
    walking.actor = 0;
    if (rate > 0 && analog_moving(actor, &in)) {
        const int tilt = khdays_analog_tilt(in.x, in.y);
        const int walk = khdays_analog_rate(rate, tilt, in.run_threshold, in.walk_speed);
        khdays_walk_movement(actor, walk, rate); /* (the Walk mod's animation: walk.h) */
        if (walk < rate) {
            walking.actor = actor;
            walking.frame = khdays_input_game_frames();
            walking.rate = rate;
            walking.walk = walk;
            return walk;
        }
    }
    khdays_walk_movement(actor, rate, rate);
    return rate;
}

/* The animation speed scale (0x1000 normal): while walking it slows as the speed did, so the steps
 * match the ground covered. The game resets it on every change of state. */
int khdays_controls_move_anim(int actor, int scale)
{
    static int last = -1;
    int result = scale;
    if (walking.actor == actor && walking.frame == khdays_input_game_frames()) {
        result = (int)((long long)scale * walking.walk / walking.rate);
    }
    if (trace_on() && actor == local_actor() && result != last) {
        trace("move animation scale %x (the game's %x)", result, scale);
        last = result;
    }
    return result;
}

void khdays_trace_steer(int actor, int aim, int bias, int angle, int step, int rate)
{
    static int last_aim = -2, last_bias = -1, last_step = -1, last_rate = -1;
    if (!trace_on() || actor != local_actor()) {
        return;
    }
    if (aim != last_aim || bias != last_bias || step != last_step || rate != last_rate) {
        trace("steer: aim %04x bias %04x angle %04x step %x rate %x", aim & 0xffff, bias & 0xffff, angle & 0xffff, step,
              rate);
    }
    last_aim = aim;
    last_bias = bias;
    last_step = step;
    last_rate = rate;
}

/* ---- The actor's states and the frame rate ------------------------------- */

void khdays_trace_actor_state(int actor, int state)
{
    if (trace_on() && actor == local_actor()) {
        trace("state %d (%#x)", state, state);
    }
}

void khdays_trace_frame_mode(int value)
{
    static int last = -1;
    if (value != last) {
        trace("frame-rate mode set to %d (heap+0x8b54)", value);
    }
    last = value;
}
