/* Where the port's controls meet the game (controls_hooks.h). prepare.py's HOOKS call these from the
 * game's camera, lock-on, command menu, movement heading and speed, actor states and frame-rate mode;
 * each hands back what the game had unless an enhancement that is on changes it.
 * KHDAYS_TRACE_CONTROLS=1 reports what the game does with its controls, only when something changes;
 * docs/RUNTIME_MAP.md records what those traces showed. */
#include "controls_hooks.h"
#include "../camera/kh2_camera_hooks.h"
#include "../core/window.h"
#include "../mods/models.h"
#include "analog.h"
#include "input.h"
#include "sequencers.h"
#include "walk.h"
#include "../net/net_session.h"

#include <math.h>
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
    game.lock_candidate = selected >= 0;
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

static int let_go;
static uint32_t let_go_at;

void khdays_controls_lock_let_go(void)
{
    let_go = 1;
    let_go_at = khdays_input_game_frames();
}

/* Ov022_ReadSelectionInput, locked on: an R within 9 frames of the last lets go before it can move to the next target,
 * and that counter is repeatAny. Type A moves at the first R, Type B as it is released. */
int khdays_controls_selection_repeat(int repeat, int pressed)
{
    if ((pressed & 0x100) == 0) {
        return repeat;
    }
    const int letting_go = let_go && khdays_input_game_frames() - let_go_at <= KEPT_FRAMES;
    let_go = 0;
    return letting_go ? 0 : repeat;
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

/* Where the game applies its own turn input (bits 4 and 8 turn, 1 and 2 tilt; `rate` its turn a
 * frame; `inverted` its horizontal inversion, Config 0x37c3): returns the right stick's share of the
 * game's rate to add to the yaw target. Right on the stick turns as every "right" of the game's does
 * (bit 4). */
int khdays_controls_camera_turn(int rate, int inverted, int bits, int flags, int flags2, int mode, int yaw, int target,
                                int pitch)
{
    static int last_target = -1, last_pitch = -1, last_bits = -1;
    const KhdaysInputSnapshot *in = khdays_input_snapshot();
    int turn = 0;
    if (khdays_kh2_camera_claims(flags, flags2, mode)) {
        return 0; /* the KH2 camera reads them (khdays_controls_camera_look) */
    }
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
int khdays_controls_camera_pitch(int rate, int inverted, int flags, int flags2, int mode)
{
    const KhdaysInputSnapshot *in = khdays_input_snapshot();
    int tilt = 0;
    if (khdays_kh2_camera_claims(flags, flags2, mode)) {
        return 0;
    }
    if ((in->enhancements & KHDAYS_ENH_ANALOG_CAMERA) != 0 && !in->ui_open && in->camera_y != 0) {
        tilt = -scaled(rate, in->camera_y, in->camera_speed);
    }
    tilt -= mouse_share(in, 1, rate); /* up on the mouse as up on the stick */
    if (inverted) {
        tilt = -tilt;
    }
    return tilt;
}

/* YS::HARDPAD::get_stick_value's dead zone is 60 of 127.5 on each axis. The snapshot's axes have the port's
 * (30 % by default) taken out, so a quarter of them is about that. */
#define KH2_AXIS_DEAD_ZONE 0.25f

static float look_axis(int axis)
{
    const float pushed = fabsf((float)axis) / 32767.0f;
    return pushed > KH2_AXIS_DEAD_ZONE ? (pushed - KH2_AXIS_DEAD_ZONE) / (1.0f - KH2_AXIS_DEAD_ZONE) : 0.0f;
}

/* How far the look stick is pushed overall, past that dead zone. */
static float look_pushed(const KhdaysInputSnapshot *in)
{
    const float across = look_axis(in->camera_x), along = look_axis(in->camera_y);
    return sqrtf(across * across + along * along);
}

/* The stick's shares where its axis is past the dead zone, with the axis's sign, and the mouse's on top. */
static void look_shares(const KhdaysInputSnapshot *in, float across, float along, float *x, float *y)
{
    *x = *y = 0.0f;
    if ((in->enhancements & KHDAYS_ENH_ANALOG_CAMERA) != 0 && !in->ui_open) {
        if (look_axis(in->camera_x) > 0.0f) {
            *x = in->camera_x > 0 ? across : -across;
        }
        if (look_axis(in->camera_y) > 0.0f) {
            *y = in->camera_y > 0 ? along : -along;
        }
    }
    *x += (float)mouse_share(in, 0, 0x1000) / 4096.0f;
    *y += (float)mouse_share(in, 1, 0x1000) / 4096.0f;
}

void khdays_controls_camera_look(float *x, float *y)
{
    const KhdaysInputSnapshot *in = khdays_input_snapshot();
    const float pushed = look_pushed(in);
    const float amount = pushed < 1.0f ? pushed : 1.0f;
    look_shares(in, amount, amount, x, y);
}

void khdays_controls_first_person_look(float *x, float *y)
{
    const KhdaysInputSnapshot *in = khdays_input_snapshot();
    const float pushed = look_pushed(in);
    const float tilt = pushed > 0.0f ? look_axis(in->camera_y) / pushed * (pushed < 1.0f ? pushed : 1.0f) : 0.0f;
    look_shares(in, 1.0f, tilt, x, y);
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
 * snapshot its buttons came with (khdays_input_movement_snapshot: this frame's alone, the one before in
 * the game's order); on the host a guest's actor takes the stick that guest sent with its controls
 * (net_session.c). */
static int analog_moving(int actor, Stick *s)
{
    const KhdaysInputSnapshot *in = khdays_input_movement_snapshot();
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

/* Each actor's walk: the game frame of its last steer, the game's speed and the walk's (0 when it ran
 * or stood). One per actor: every actor steers each frame, and another's steer must not end this one's. */
typedef struct {
    int actor;
    u32 frame;
    int rate, walk;
} Walking;
static Walking *walking;
static int walking_count, walking_capacity;

/* The actor's walk; with `add`, a new one when it has none: the place of one not steered for two game frames (what
 * reads a walk wants this frame's or the one before), or one more. NULL: none (or no memory for it). */
static Walking *walking_of(int actor, int add)
{
    Walking *stale = NULL;
    for (int i = 0; i < walking_count; ++i) {
        if (walking[i].actor == actor) {
            return &walking[i];
        }
        if (stale == NULL && khdays_input_game_frames() - walking[i].frame > 1u) {
            stale = &walking[i];
        }
    }
    if (!add) {
        return NULL;
    }
    if (stale == NULL) {
        if (walking_count == walking_capacity) {
            const int n = walking_capacity ? walking_capacity * 2 : 8;
            Walking *grown = realloc(walking, (size_t)n * sizeof(*grown));
            if (grown == NULL) {
                return NULL;
            }
            walking = grown;
            walking_capacity = n;
        }
        stale = &walking[walking_count++];
    }
    stale->actor = actor;
    stale->frame = khdays_input_game_frames();
    stale->rate = stale->walk = 0;
    return stale;
}

/* The speed the actor moves toward (its nWalkSpeed): with the stick pushed less than the run
 * threshold, the walk speed's share of it, never more than the game's own. */
int khdays_controls_move_rate(int actor, int rate)
{
    Stick in;
    int walk = rate;
    if (rate > 0 && analog_moving(actor, &in)) {
        const int tilt = khdays_analog_tilt(in.x, in.y);
        walk = khdays_analog_rate(rate, tilt, in.run_threshold, in.walk_speed);
    }
    khdays_walk_movement(actor, walk, rate); /* (the Walk mod's animation: walk.h) */
    Walking *w = walking_of(actor, walk < rate);
    if (w != NULL) {
        w->frame = khdays_input_game_frames();
        w->rate = rate;
        w->walk = walk < rate ? walk : 0;
    }
    return walk;
}

/* The walk of the actor whose scene node (its +0x20) is `node`, steered this game frame or the one before
 * (its draw can follow the next frame's pad sample): its speed as a share of the game's, in 1/4096 (a KH2
 * model's `walk` lines, runtime/mods/models.cpp); 0 when it does not walk. */
int khdays_controls_walk_share(unsigned node)
{
    static int last = -1;
    int share = 0;
    for (int i = 0; i < walking_count; ++i) {
        const Walking *w = &walking[i];
        if (w->actor != 0 && w->walk > 0 && w->rate > 0 && khdays_input_game_frames() - w->frame <= 1u &&
            khdays_game_ram((KhdaysRef)w->actor + 0x20U) && *(u32 *)KHDAYS_W((KhdaysRef)w->actor + 0x20U) == node) {
            share = (int)((long long)w->walk * 4096 / w->rate);
            break;
        }
    }
    if (trace_on() && share != last) {
        trace("walk share %d/4096 at the draw of node %08x", share, node);
        last = share;
    }
    return share;
}

/* The animation speed scale (0x1000 normal): while walking it slows as the speed did, so the steps
 * match the ground covered. The game resets it on every change of state. */
int khdays_controls_move_anim(int actor, int scale)
{
    static int last = -1;
    int result = scale;
    const Walking *w = walking_of(actor, 0);
    if (w != NULL && w->walk > 0 && w->frame == khdays_input_game_frames()) {
        result = (int)((long long)scale * w->walk / w->rate);
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

/* Ov022_EnterActorState: tells the KH2 model and, for the local player, the KH2 camera what state an actor entered. */
void khdays_controls_actor_entered_state(int actor, int state)
{
    khdays_models_actor_state(actor, state); /* its KH2 model's `air` lines (runtime/mods/models.cpp) */
    if (actor == local_actor()) {
        khdays_kh2_camera_player_state(state);
    }
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

/* ---- The control packet's buttons (Ov022_RefreshSlotRows) ----------------- */

extern void Ov022_SendControlPacket(void);
extern int Session_GetLinkMode(void);

static KhdaysControlSender sender;
/* inside an early build: MsgQueue_SendGate hands gate 3 to its handler at once -- 1 its directions alone
 * (khdays_controls_send_early), 2 the whole packet (khdays_controls_command_early) */
static int delivering;
static u32 early_frame; /* the game frame the directions went early in (0: none yet) */

int khdays_controls_deliver_now(void)
{
    return delivering;
}

/* KHDAYS_DS_CONTROL_ORDER=1: the game's own order, the characters a game frame behind the buttons (to compare). */
static int ds_order(void)
{
    static int on = -1;
    if (on < 0) {
        on = getenv("KHDAYS_DS_CONTROL_ORDER") != NULL;
    }
    return on;
}

/* Alone (in a session a guest's packets come when they come, and its prediction of its own character
 * follows the host's order): the directions of a packet built as the host's characters are about to read
 * the table, put in the local player's row (MsgQueue_SendGate's hook, port/tools/prepare.py). The rest of
 * the row -- the other buttons, the command chosen, its target -- stays the game's own packet's, built
 * after the characters' update and queued as the game does: with them, the actor saw the A the command
 * menu took for Talk a frame before the Talk, and the player could not talk to anyone. `ready`: the game's network send builds one this frame (its object is
 * there, not paused), the battle is the ordinary one (not Sora's memory's, whose frames take turns with
 * it) and the game puts in no buttons of its own after it (the mode word 0x2a's saved masks,
 * Ov022_RefreshSlotRows). */
void khdays_controls_send_early(int ready)
{
    static int last = -1;
    const int early =
        khdays_control_sender_early(&sender, khdays_input_game_frames(), ready && !khdays_net_in_session() && !ds_order());
    if (early) {
        early_frame = khdays_input_game_frames();
        /* the packet's building clears the command chosen and bInput1 once sent (Ov022_SendControlPacket):
         * those stay for the game's own packet */
        const int actor = local_actor();
        unsigned char *chosen = actor != 0 ? (unsigned char *)KHDAYS_W(actor) + 0x4d0 : NULL;
        const unsigned char kind = chosen != NULL ? chosen[0] : 0, bits = chosen != NULL ? chosen[0x18] : 0;
        delivering = 1;
        Ov022_SendControlPacket();
        delivering = 0;
        if (chosen != NULL) {
            chosen[0] = kind;
            chosen[0x18] = bits;
        }
    }
    if (trace_on() && early != last) {
        trace("control packet: its directions put in %s (link mode %d)",
              early ? "as the characters are about to read them" : "by the game's network send, after them",
              Session_GetLinkMode());
    }
    last = early;
}

/* ---- Commands at once (Ov022_ActorUpdate, after Ov022_UpdateCommandInput) ----------------------------
 * The command menu chooses inside the local player's update (+0x4d0), after its row was read into it
 * (+0x4d6), so a command reached the actor a game frame late: the game's packet carried it to the next
 * frame's row. Right after the menu chose one with a press of this frame, the packet is built there and
 * given whole to the local player's row (gate 3's handler, MsgQueue_SendGate's hook), the pressed buttons
 * made again from the held of the frame before, and the actor's own copy set as Ov022_LoadNodeFromResource
 * sets it -- so Ov022_TranslateCommandToAction, next in the update, acts on it in this frame. Building it
 * clears the command chosen (Ov022_SendControlPacket), so the game's own packet, later, has none: no command
 * twice, and its buttons, held by then, are no new press either. Only commands nothing else waits on: an
 * attack (kind 9, the deck's Attack, with the weapon out: an attached part, func_ov022_0209fc78) and the
 * deck's magic and items (kinds 8, 11-13). The Grey Area's A chooses no command (7), and its Talk comes
 * through other objects a frame on: given early, the actor saw the A before the Talk (the first try,
 * 2026-11-10). Measured on the `play` walk's mission: the attack requested (+0x1c) a game frame sooner.
 * Alone, as the directions (khdays_controls_send_early);
 * KHDAYS_FAST_COMMANDS=0 keeps the game frame (the game's own order). */
#define PAD_PRESSED (*(volatile u16 *)KHDAYS_W(0x0204c190)) /* gPadPressed */

static int fast_commands(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("KHDAYS_FAST_COMMANDS");
        on = v == NULL || v[0] != '0';
    }
    return on;
}

static void *command_actor; /* the actor the whole packet is for (khdays_controls_command_given) */

void khdays_controls_command_early(void *actor_pointer)
{
    const int actor = local_actor();
    unsigned char *a;
    int kind, weapon;
    if (!fast_commands() || ds_order() || khdays_net_in_session() || early_frame == 0 ||
        early_frame != khdays_input_game_frames() || actor == 0) {
        return;
    }
    a = (unsigned char *)KHDAYS_W(actor);
    if ((void *)a != actor_pointer || (PAD_PRESSED & 0xc03u) == 0) {
        return;
    }
    kind = (signed char)a[0x4d0];
    weapon = a[0xda9] != 0 || a[0xda9 + 0x164] != 0;
    if (!((kind == 9 && weapon) || kind == 8 || (kind >= 11 && kind <= 13))) {
        if (trace_on()) {
            trace("command: kind %d chosen (%s): given as the game gives it", kind,
                  weapon ? "the weapon out" : "no weapon out");
        }
        return;
    }
    command_actor = a;
    delivering = 2;
    Ov022_SendControlPacket();
    delivering = 0;
    command_actor = NULL;
}

void khdays_controls_command_given(const unsigned short *packet, unsigned pressed, unsigned held)
{
    unsigned char *a = (unsigned char *)command_actor;
    unsigned int word;
    if (a == NULL) {
        return;
    }
    word = (unsigned int)packet[2] | ((unsigned int)packet[3] << 16);
    *(unsigned short *)(a + 0x18) = (unsigned short)pressed;
    *(unsigned short *)(a + 0x1a) = (unsigned short)held;
    a[0x4d6] = (unsigned char)(word & 0xfu);
    *(unsigned short *)(a + 0x4d8) = (unsigned short)((word >> 4) & 0x3ffu);
    *(unsigned short *)(a + 0x4da) = (unsigned short)((word >> 14) & 0x3ffu);
    a[0x4e8] = (unsigned char)((a[0x4e8] & ~0x14u) | (((word >> 24) & 1u) << 2) | (((word >> 25) & 1u) << 4));
    if (trace_on()) {
        trace("command: kind %u given in the frame it was chosen (pressed %03x, held %03x)", word & 0xfu, pressed,
              held);
    }
}

const KhdaysInputSnapshot *khdays_input_movement_snapshot(void)
{
    /* sender.early is the frame + 1 of the last packet built early: this frame's (0) or the last's (1) */
    const u32 since = khdays_input_game_frames() + 1u - sender.early;
    return sender.early != 0 && since <= 1u ? khdays_input_snapshot() : khdays_input_previous_snapshot();
}
