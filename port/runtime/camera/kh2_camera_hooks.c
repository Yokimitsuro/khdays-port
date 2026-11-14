/* The KH2 camera in the game (kh2_camera_hooks.h). The game's camera runs as ever each frame, keeping its
 * flags, shake and fades; these then put KH2's eye and look-at in its fields. KHDAYS_TRACE_KH2_CAMERA=1
 * reports each frame's numbers. docs/KH2_CAMERA.md has the whole. */
#include "kh2_camera_hooks.h"
#include "kh2_camera.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../input/analog.h"
#include "../input/controls_hooks.h"
#include "../input/input.h"
#include "../../host/app/host.h"
#include "khdays_ds_symbols.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int GetFrameRateMode(void); /* 0: 30 fps, 1: 20 fps, 2: 60 fps */
extern int QueryActiveStateOrDelegate(void);
extern int GetEntryField20ByIndex(int idx);
extern int Ov022_GetEntryField66(int player);
extern void func_ov022_02088280(int player, short heading); /* sets a player's heading, as a camera's yaw */
extern int func_ov022_020881f8(int player);                 /* a player's position */
extern unsigned short func_ov022_02088254(int player);      /* the yaw behind a player */
extern short Session_GetLocalPlayerIndex(void);
extern int PauseMenu_GetMode(void);
extern int Ov002_GetSlotTableByte(int world);
extern int Ov002_GetPeerRecordValue(int slot);
extern KhdaysRef GetTrackEntryBase(int index);
extern KhdaysRef Collision_RunRayCast(KhdaysRef world, KhdaysRef params);
extern KhdaysRef Collision_RunSphereCast(KhdaysRef world, KhdaysRef params);
extern KhdaysRef Actor_GetRecord(KhdaysRef hit, int slot);
extern KhdaysRef Collision_CastNearest(KhdaysRef world, KhdaysRef params);

/* The game's held buttons (src/engine/Pad_Sample.c) */
#define PAD_HELD (*(volatile u16 *)KHDAYS_W(0x0204c18c))

/* The camera's fields (Ov002_TickCameraTransition's CameraState), as words */
enum {
    WORD_FOV_SIN = 0,
    WORD_FOV_COS = 1,
    WORD_NEAR = 0xc / 4,
    WORD_FOCUS = 0x14 / 4,
    WORD_EYE = 0x20 / 4,
    WORD_FLAGS = 0x38 / 4,
    WORD_FLAGS2 = 0x3c / 4,
    WORD_MODE = 0x40 / 4,
    WORD_DISTANCE = 0x54 / 4,
    WORD_YAW = 0x58 / 4,
    WORD_EYE_HEIGHT = 0x5c / 4,
    WORD_TARGET_DISTANCE = 0x7c / 4,
    WORD_TARGET_YAW = 0x80 / 4,
    WORD_STAGE = 0xe0 / 4,
};

enum {
    FLAG_RESET = 0x8,
    FLAG_CONTROL = 0x2,
    FLAG_HELD_POINT = 0x100,
    FLAG_SELECT_VIEW = 0x2000,
    FLAG_HOLD = 0x200, /* Ov002_SetOrClearFlag200: a talk, an event, a panel, the results: no control */
    /* switched off (4), a script's camera (0x8000), the heading tied to it (0x10000), its transition (0x20000),
     * modes 9 and 10 */
    FLAGS_NOT_GAMEPLAY = 0x4 | 0x8000 | 0x10000 | 0x20000 | 0x08000000 | 0x20000000,
    /* Chase Cam's Config alone: Days' hold of R (flag 0x10000000) and KH2's R2 swap it, and neither is free
     * on the port's controls */
    FLAG2_MANUAL = 0x1,
    FLAG2_SLOW = 0x2,
    FLAG2_FAST = 0x4,
    FLAG2_INVERT_Y = 0x8,
    FLAG2_INVERT_X = 0x10,
    FLAG2_TYPE_B = 0x20,
    /* what the session log follows: not the lock-on's on-screen bits, which change as the target moves */
    FLAGS_LOGGED = FLAG_CONTROL | FLAG_SELECT_VIEW | FLAG_HOLD | FLAGS_NOT_GAMEPLAY,
};

/* Days' turn bits (Ov002_Camera_UpdateFollow), from a drag on the touch screen */
enum { BIT_HIGHER = 1, BIT_LOWER = 2, BIT_RIGHT = 4, BIT_LEFT = 8 };

/* The port's choice for "slightly" (KH2 has one speed, Normal's) */
#define SLOW_SPEED 0.8f
#define FAST_SPEED 1.25f
/* What is further than this in a game frame was a warp (ObjCamera::setWarp's job in KH2). */
#define WARP_DISTANCE 500.0f
/* How far off what it hits the eye stays (dk::ObjCamera::collision) */
#define OFF_SURFACE 5.0f
/* The near plane while this camera runs, 2.5 of KH2's units. KH2's is 1 (kn::Camera::init), Days' 10, which would let
 * a wall 5 off the eye through at the screen's sides. 1 would leave the DS's 20.12 clip space little depth (fx32). */
#define NEAR_PLANE 0x66
#define DAYS_NEAR_PLANE 0x19a /* Ov002_InitWorldState */
#define PAST_FACE 0x40
#define CAST_TRIES 8
/* A face's plane's normal, and its slots: 1 and 2 name records whose first tag is their kind (Ov002_LoadPeerIntoSlot) */
#define FACE_NORMAL 0x14
#define FACE_SLOTS 0x80
#define RECORD_TAG 0xc
/* nohit: drawn on about 1 % of its area (the rooms' models), so the invisible walls; nocam is the map's own word;
 * sicon, a climb's mark, drawn on as little (data_ov002_0207e640) */
enum { TAG_NOHIT = 2, TAG_NOCAM = 5, TAG_SICON = 8 };
/* A pause this long (a cutscene) starts again from the start radius. */
#define KEEP_RADIUS_FRAMES 30u

#define TWO_PI 6.28318530717958647692f
#define KH2_UNITS_A_DAYS_UNIT 100.0f

/* YS::FADE::start(..., 30.0) in objective_control and subjective_control, 1 to 0 and back */
#define FADE_TICKS 30.0f

/* The actor's node */
enum { ACTOR_NODE = 0x20, NODE_SCENE = 4 };

enum { WANT_NONE, WANT_FOLLOW, WANT_LOCK };

/* CollCastParams. `map_only` is the decomp's directionIsUnit: what Coll_WalkTreeForHit and Coll_WalkSphereTreeForHit
 * read to leave the objects out (the actors, what moves), and nothing else reads. */
typedef struct CastParams {
    KhdaysRef origin, direction;
    int radius;
    u16 map_only, flags;
    KhdaysRef exclude;
} CastParams;

typedef KhdaysRef (*CastFunction)(KhdaysRef world, KhdaysRef params);

static struct {
    Kh2Camera camera;
    Kh2CameraInput in;
    int want;
    KhdaysRef target;
    int home, center, input_off, no_press, bits;
    int first_person, toggle;
    int player_state;
    int held;      /* this frame's camera flag 0x200: the player's controls are not the camera's */
    int committed; /* the last frame came from a commit, no tick before it (khdays_kh2_camera_commit) */
    int republish; /* Days re-placed its camera in the middle of this one's: only write this one back */
    int room;
    float fade; /* the player's, as YS::OBJ's +0x834: 1 seen, 0 hidden */
    int active;
    u32 frame;
    unsigned days_frames; /* Days' camera's frames since this camera's last */
    Kh2CameraVec last_player;
    int fov_held; /* the FOV is this camera's; Days' goes back when the game's camera takes over */
    int written_eye[3];
    int tracing;
} s = {.tracing = -1, .fade = 1.0f};

static float from_fx(int value)
{
    return (float)value / 4096.0f * KH2_UNITS_A_DAYS_UNIT;
}

static int to_fx(float value)
{
    return (int)lroundf(value / KH2_UNITS_A_DAYS_UNIT * 4096.0f);
}

static Kh2CameraVec read_vec(const int *words)
{
    Kh2CameraVec v = {from_fx(words[0]), from_fx(words[1]), from_fx(words[2])};
    return v;
}

static void write_vec(int *words, Kh2CameraVec v)
{
    words[0] = to_fx(v.x);
    words[1] = to_fx(v.y);
    words[2] = to_fx(v.z);
}

static float angle_radians(int angle)
{
    return (float)(angle & 0xffff) / 65536.0f * TWO_PI;
}

static int moved_from(const int *words, const int *written)
{
    /* the conversions' own rounding is not a hit */
    return abs(words[0] - written[0]) > 2 || abs(words[1] - written[1]) > 2 || abs(words[2] - written[2]) > 2;
}

int khdays_kh2_camera_claims(int flags, int flags2, int mode)
{
    const KhdaysInputSnapshot *in = khdays_input_snapshot();
    /* data_0204be04: the screen whose 3D runs this frame; the second's camera stays plain Days'. Modes: 1 a talk's, 3 a
     * shake over the ordinary camera, 8 what Ov002_Camera_SetMode treats as 0 (set by Ov032_ApplyMode and others). */
    return (in->enhancements & KHDAYS_ENH_KH2_CAMERA) != 0 && (flags2 & FLAG2_TYPE_B) == 0 &&
           (flags & FLAG_CONTROL) != 0 && (flags & FLAGS_NOT_GAMEPLAY) == 0 &&
           *(const volatile unsigned char *)KHDAYS_W(KHDAYS_DS_data_0204be04) == 0 &&
           (mode == 0 || mode == 1 || mode == 3 || mode == 8);
}

static int claims_camera(const int *cam)
{
    return khdays_kh2_camera_claims(cam[WORD_FLAGS], cam[WORD_FLAGS2], cam[WORD_MODE]);
}

int khdays_kh2_camera_active(void)
{
    return s.active && !s.held && !s.no_press && khdays_input_game_frames() - s.frame <= 2u;
}

void khdays_kh2_camera_center(void)
{
    s.center = 1;
}

void khdays_kh2_camera_toggle_first_person(void)
{
    s.toggle = 1;
}

unsigned khdays_kh2_camera_faded_node(float *fade)
{
    int actor;
    *fade = s.fade;
    /* With the 3D on both screens only the first's camera is this one; the other's player is another's actor */
    if (s.fade >= 1.0f || *(const volatile unsigned char *)KHDAYS_W(KHDAYS_DS_data_0204be04) != 0 ||
        (actor = GetEntryField20ByIndex(QueryActiveStateOrDelegate())) == 0) {
        return 0;
    }
    return *(const KhdaysRef *)KHDAYS_W((KhdaysRef)actor + ACTOR_NODE) + NODE_SCENE;
}

/* YS::FADE::exec */
static void step_fade(float ticks)
{
    const float step = ticks / FADE_TICKS;
    if (s.first_person) {
        s.fade = s.fade > step ? s.fade - step : 0.0f;
    } else {
        s.fade = s.fade + step < 1.0f ? s.fade + step : 1.0f;
    }
}

/* The states the local player has control in (Ov022_EnterActorState): standing or running (0), the jump's (1, 2,
 * 0x16), the glide (6), the aimed hop (0xd), the walk (0x1e). */
void khdays_kh2_camera_player_state(int state)
{
    s.player_state = state;
}

static int player_has_control(void)
{
    const int state = s.player_state;
    return state == 0 || state == 1 || state == 2 || state == 6 || state == 0xd || state == 0x16 || state == 0x1e;
}

/* Only the host moves its own actor: a guest's goes by the controls it sends, which have no such move. Without
 * control the steers are Days' own: an attack's turn to its target (Ov022_StepAnchorApproachState). */
int khdays_kh2_camera_strafing(int actor)
{
    return s.first_person && player_has_control() && actor == GetEntryField20ByIndex(QueryActiveStateOrDelegate()) &&
           Session_GetLocalPlayerIndex() == 0;
}

/* A session's pause runs the objects behind the menu, and takes the host's controls (Ov002_Camera_UpdateFollow). */
static int host_paused(void)
{
    return PauseMenu_GetMode() == 2 && Session_GetLocalPlayerIndex() == 0;
}

void khdays_kh2_camera_follow(int camera, int input_off, int bits)
{
    int *cam = KHDAYS_W(camera);
    if (!claims_camera(cam)) {
        return;
    }
    s.want = WANT_FOLLOW;
    s.no_press = host_paused();
    s.input_off = input_off || s.no_press;
    s.bits = bits;
    /* With lock-on on its own buttons the game's R presses are the lock's (input.c) */
    s.home = (cam[WORD_FLAGS] & FLAG_RESET) != 0 &&
             (khdays_input_snapshot()->enhancements & KHDAYS_ENH_SEPARATE_LOCK_ON) == 0;
    cam[WORD_FLAGS] &= ~FLAG_RESET; /* the reset is this camera's, not Ov002_TickLockedCamera's */
}

void khdays_kh2_camera_lock(int target, int camera)
{
    const int *cam = KHDAYS_W(camera);
    if (claims_camera(cam) && (cam[WORD_FLAGS] & FLAG_HELD_POINT) == 0) {
        s.want = WANT_LOCK;
        s.target = (KhdaysRef)target;
        s.input_off = s.no_press = host_paused();
        s.bits = 0;
        s.home = 0;
    }
}

/* A hold skips what marks a frame (Ov002_Camera_UpdateFollow): the camera stays, without control. */
void khdays_kh2_camera_held(int camera)
{
    const int *cam = KHDAYS_W(camera);
    if ((cam[WORD_FLAGS] & FLAG_HOLD) != 0 && claims_camera(cam)) {
        s.want = WANT_FOLLOW;
        s.input_off = s.no_press = 1;
        s.bits = 0;
        s.home = 0;
    }
}

/* The movement stick as KH2's Stick[1] has it: a unit direction, y down, and how far. */
static void read_movement(Kh2CameraInput *in)
{
    const KhdaysInputSnapshot *snapshot = khdays_input_movement_snapshot(); /* the one the player moves by */
    float x = 0.0f, y = 0.0f, amount = 0.0f;
    if ((snapshot->enhancements & KHDAYS_ENH_ANALOG_MOVEMENT) != 0 &&
        (snapshot->move_x != 0 || snapshot->move_y != 0)) {
        int stick_x = snapshot->move_x, stick_y = snapshot->move_y;
        khdays_analog_kh2_stick(&stick_x, &stick_y);
        x = (float)stick_x / 32767.0f;
        y = (float)-stick_y / 32767.0f;
        amount = sqrtf(x * x + y * y);
    } else {
        const u16 held = PAD_HELD;
        x = (float)(((held & 0x10) != 0) - ((held & 0x20) != 0));
        y = (float)(((held & 0x80) != 0) - ((held & 0x40) != 0));
        amount = x != 0.0f || y != 0.0f ? 1.0f : 0.0f;
    }
    if (amount > 0.0f) {
        const float length = sqrtf(x * x + y * y);
        x /= length;
        y /= length;
    }
    in->move_x = x;
    in->move_y = y;
    in->move_amount = amount > 1.0f ? 1.0f : amount;
}

/* The look stick and the mouse, with Days' Config: its speed and axes. Right on the stick lowers the yaw and
 * up lowers the eye, as everywhere in Days (khdays_controls_camera_turn, _pitch). */
static void read_look(Kh2CameraInput *in, int flags2)
{
    float x = 0.0f, y = 0.0f;
    float speed = 1.0f;
    if (s.first_person) {
        khdays_controls_first_person_look(&x, &y);
    } else {
        khdays_controls_camera_look(&x, &y);
    }
    if (s.input_off || s.held) {
        x = y = 0.0f;
    }
    x += (float)(((s.bits & BIT_RIGHT) != 0) - ((s.bits & BIT_LEFT) != 0));
    y += (float)(((s.bits & BIT_LOWER) != 0) - ((s.bits & BIT_HIGHER) != 0));
    if ((flags2 & FLAG2_SLOW) != 0) {
        speed = SLOW_SPEED;
    }
    if ((flags2 & FLAG2_FAST) != 0) {
        speed = FAST_SPEED;
    }
    in->turn = ((flags2 & FLAG2_INVERT_X) != 0 ? x : -x) * speed;
    in->zoom = ((flags2 & FLAG2_INVERT_Y) != 0 ? y : -y) * speed;
}

static int ticks_a_frame(void)
{
    const int mode = GetFrameRateMode();
    return mode == 1 ? 3 : mode == 2 ? 1 : 2;
}

/* The session log: each change of whose frame this is and what the game's flags say, to read beside an F12 mark. */
static void log_owner(const int *cam, int want)
{
    static int last_want = -1, last_flags, last_mode;
    const int flags = cam[WORD_FLAGS] & FLAGS_LOGGED;
    if (want != last_want || flags != last_flags || cam[WORD_MODE] != last_mode) {
        khdays_log("kh2 camera: frame %u %s flags %#x flags2 %#x mode %d", khdays_input_game_frames(),
                   want == WANT_NONE   ? "days"
                   : want == WANT_LOCK ? "lock"
                                       : "follow",
                   (unsigned)cam[WORD_FLAGS], (unsigned)cam[WORD_FLAGS2], cam[WORD_MODE]);
        last_want = want;
        last_flags = flags;
        last_mode = cam[WORD_MODE];
    }
}

static int same_place(Kh2CameraVec player)
{
    const float dx = player.x - s.last_player.x, dz = player.z - s.last_player.z;
    return s.frame != 0u && s.room == Ov022_GetEntryField66(QueryActiveStateOrDelegate()) &&
           dx * dx + dz * dz <= WARP_DISTANCE * WARP_DISTANCE;
}

/* The wanted eye, and where to cast from; 0 when the camera stays the game's. */
static int frame_prepare(int camera, int player, int behind_yaw, int origin, int eye)
{
    int *cam = KHDAYS_W(camera);
    const u32 frame = khdays_input_game_frames();
    const int want = s.want;
    Kh2CameraInput *in = &s.in;
    int warp = 0;

    s.want = WANT_NONE;
    /* Days' second camera (data_0204be04, the 3D on both screens): plain Days', leaving this one's state alone */
    if (cam[WORD_STAGE] != 0) {
        return 0;
    }
    log_owner(cam, want);
    /* Ov002_Camera_SetOrbitAngle, _SetAnchor (leaving a menu, an event's end), Ov002_ResetCamera: no frame of a camera
     * of Days', only its place set anew. Where the player has not moved, this camera stands. */
    if (want == WANT_NONE && s.active && claims_camera(cam) && same_place(read_vec(KHDAYS_W(player)))) {
        s.republish = 1;
        write_vec(KHDAYS_W(origin), s.camera.pivot);
        write_vec(s.written_eye, s.camera.eye);
        write_vec(KHDAYS_W(eye), s.camera.eye);
        return 1;
    }
    s.held = want != WANT_NONE && (cam[WORD_FLAGS] & FLAG_HOLD) != 0;
    if (want == WANT_NONE) {
        if (s.fov_held) {
            /* from the angles Ov002_Camera_UpdateFollow keeps at +0xa0 and +0xa2 */
            const u16 *angles = (const u16 *)((const char *)cam + 0xa0);
            cam[WORD_FOV_SIN] = (int)lroundf(sinf(angle_radians(angles[0])) * 4096.0f);
            cam[WORD_FOV_COS] = (int)lroundf(cosf(angle_radians(angles[1])) * 4096.0f);
            cam[WORD_NEAR] = DAYS_NEAR_PLANE;
            s.fov_held = 0;
        }
        s.center = 0;
        s.toggle = 0;
        s.first_person = 0; /* a cutscene: ObjCamera::cancelSubjective */
        step_fade((float)ticks_a_frame());
        s.active = 0;
        ++s.days_frames;
        return 0;
    }

    in->player = read_vec(KHDAYS_W(player));
    in->ticks = (float)ticks_a_frame();
    {
        const int takeover = kh2_camera_takeover(s.active, s.days_frames, same_place(in->player));
        if (takeover != KH2_CAMERA_RESUME) {
            const float radius = s.frame != 0u && frame - s.frame <= KEEP_RADIUS_FRAMES ? s.camera.radius : 0.0f;
            /* outdoors everywhere: Days has no indoor maps that this could read (docs/KH2_CAMERA.md) */
            const float fov = takeover == KH2_CAMERA_SNAP ? kh2_camera_half_angle(kh2_camera_outdoor.fov)
                                                          : atan2f((float)cam[WORD_FOV_SIN], (float)cam[WORD_FOV_COS]);
            /* a new field starts behind the player (YS::PLAYER::start_player_camera) */
            kh2_camera_start(&s.camera, read_vec(cam + WORD_FOCUS), read_vec(cam + WORD_EYE),
                             angle_radians(takeover == KH2_CAMERA_SNAP ? behind_yaw : cam[WORD_YAW]), fov, radius);
            s.camera.snap = takeover == KH2_CAMERA_SNAP;
            s.first_person = 0;
            if (takeover == KH2_CAMERA_SNAP) {
                s.fade = 1.0f; /* a new field */
            }
            s.last_player = in->player;
        }
        s.fov_held = 1;
        s.active = 1;
        s.days_frames = 0u;
    }
    {
        const float dx = in->player.x - s.last_player.x, dz = in->player.z - s.last_player.z;
        const float moved = sqrtf(dx * dx + dz * dz);
        warp = moved > WARP_DISTANCE;
        in->moved = moved > 0.0f;
    }
    s.last_player = in->player;
    s.frame = frame;
    s.committed = 0;
    s.room = Ov022_GetEntryField66(QueryActiveStateOrDelegate());
    in->behind_yaw = angle_radians(behind_yaw);
    /* mode 1: a talk, an examined object (Ov002_ElementChoiceStep, Ov002_SpareEntryStep) */
    in->zoomed = (cam[WORD_FLAGS] & FLAG_SELECT_VIEW) != 0 || cam[WORD_MODE] == 1;
    in->manual = (cam[WORD_FLAGS2] & FLAG2_MANUAL) != 0 || in->zoomed;
    /* A press made as a tutorial's page or a session's pause came is not this camera's */
    if (s.no_press) {
        s.center = 0;
        s.toggle = 0;
    }
    in->home = s.home || s.center;
    s.home = s.center = 0;
    /* Into first person only from following, as objective_control; locking on and talks leave it (cancelSubjective) */
    if (want == WANT_LOCK || s.held || (khdays_input_snapshot()->enhancements & KHDAYS_ENH_KH2_FIRST_PERSON) == 0) {
        s.first_person = 0;
    } else if (s.toggle) {
        s.first_person = !s.first_person;
        s.camera.pitch = 0.0f;
        if (s.first_person) {
            s.camera.yaw = s.camera.yaw_target = in->behind_yaw; /* the player's facing */
        }
    }
    s.toggle = 0;
    step_fade(in->ticks);
    read_movement(in);
    if (s.held) {
        in->move_amount = 0.0f; /* what moves the player now is not the stick: no chase */
    }
    read_look(in, cam[WORD_FLAGS2]);

    if (want == WANT_LOCK) {
        kh2_camera_lock(&s.camera, in, read_vec(KHDAYS_W(s.target)));
    } else if (s.first_person) {
        in->can_turn = player_has_control();
        kh2_camera_first_person(&s.camera, in);
        /* The host sends every actor's heading to the guests each frame (Ov022_SendActorStatePacket) */
        if (in->can_turn && Session_GetLocalPlayerIndex() == 0) {
            func_ov022_02088280(QueryActiveStateOrDelegate(),
                                (short)(lroundf(s.camera.yaw / TWO_PI * 65536.0f) & 0xffff));
        }
    } else {
        kh2_camera_follow(&s.camera, in);
    }
    if (warp) {
        s.camera.snap = 1;
    }
    write_vec(KHDAYS_W(origin), s.camera.pivot);
    write_vec(s.written_eye, s.camera.eye_target);
    write_vec(KHDAYS_W(eye), s.camera.eye_target);
    return 1;
}

static double fx_distance(const int *a, const int *b)
{
    const double x = (double)a[0] - b[0], y = (double)a[1] - b[1], z = (double)a[2] - b[2];
    return sqrt(x * x + y * y + z * z);
}

/* Whether the eye goes through this face of a hit (Ov002_CollCastResolvePassThrough has nocam alone). */
static int eye_passes(KhdaysRef hit, KhdaysRef face)
{
    const unsigned char *slots = KHDAYS_W(face + FACE_SLOTS);
    for (int i = 1; i < 3; ++i) {
        const KhdaysRef record = Actor_GetRecord(hit, slots[i]);
        if (record != 0) {
            const int tag = *(const unsigned char *)KHDAYS_W(record + RECORD_TAG);
            if (tag == TAG_NOHIT || tag == TAG_NOCAM || tag == TAG_SICON) {
                return 1;
            }
        }
    }
    return 0;
}

/* A cast along `motion` from `from`, of the map alone, past the faces the eye goes through: how far along it the first
 * other face is (0 to 1) and that face's normal, or 0. Nothing excludes a map's face, so the next try starts beyond it. */
static int cast_map(CastFunction cast, KhdaysRef world, const int *from, const double *motion, int radius,
                    double *fraction, double *normal)
{
    const double length = sqrt(motion[0] * motion[0] + motion[1] * motion[1] + motion[2] * motion[2]);
    const double step = (2.0 * radius + PAST_FACE) / length;
    double done = 0.0;
    int origin[3], direction[3];
    CastParams params = {KHDAYS_REF(origin), KHDAYS_REF(direction), radius, 1, 0xf, 0};
    for (int tries = 0; tries < CAST_TRIES && done < 1.0; ++tries) {
        KhdaysRef hit;
        for (int i = 0; i < 3; ++i) {
            origin[i] = from[i] + (int)lround(motion[i] * done);
            direction[i] = (int)lround(motion[i] * (1.0 - done));
        }
        hit = cast(world, KHDAYS_REF(&params));
        if (hit == 0) {
            return 0;
        }
        {
            const int *record = KHDAYS_W(hit);
            const KhdaysRef face = (KhdaysRef)record[1];
            const double at = done + record[3] / 134217728.0 * (1.0 - done); /* the hit's distance is Q27 */
            if (!eye_passes(hit, face)) {
                const short *plane = KHDAYS_W(face + FACE_NORMAL);
                const double size =
                    sqrt((double)plane[0] * plane[0] + (double)plane[1] * plane[1] + (double)plane[2] * plane[2]);
                *fraction = at;
                for (int i = 0; i < 3; ++i) {
                    normal[i] = size > 0.0 ? plane[i] / size : 0.0;
                }
                return 1;
            }
            done = at + step;
        }
    }
    return 0;
}

/* The first face of the map along a line from `from`, of the three lists: the ray's floors and slopes
 * (RoomPoly_BuildPlanesVertical), the sphere cast's walls, with no radius a line, and the plain cast's ceilings. As
 * dk::Ray::intersectPolygon they count only from their front. */
static int line_hit(KhdaysRef world, const int *from, const double *motion, double *fraction, double *normal,
                    int *ceiling)
{
    double at, other[3];
    int found = 0;
    *fraction = 2.0;
    *ceiling = 0;
    if (cast_map(Collision_RunRayCast, world, from, motion, 0, &at, other) && at < *fraction) {
        *fraction = at;
        memcpy(normal, other, sizeof(other));
        found = 1;
    }
    if (cast_map(Collision_RunSphereCast, world, from, motion, 0, &at, other) && at < *fraction) {
        *fraction = at;
        memcpy(normal, other, sizeof(other));
        found = 1;
    }
    if (cast_map(Collision_CastNearest, world, from, motion, 0, &at, other) && at < *fraction) {
        *fraction = at;
        memcpy(normal, other, sizeof(other));
        *ceiling = 1;
        found = 1;
    }
    return found;
}

/* dk::ObjCamera::collision (no BlindCollision: Days' objects have none of its spheres): one line from the look-at, and
 * the eye 5 off what it hits along that surface's normal. A ceiling sends the line to under the eye at the hit's height
 * first. Not Ov002_ResolveCameraAgainstCollision: its sphere and 0xc00 off the floor are Days'. The map's top stays. */
static void frame_collide(int origin, int eye)
{
    int *eye_words = KHDAYS_W(eye);
    const int *origin_words = KHDAYS_W(origin);
    const int room = Ov022_GetEntryField66(QueryActiveStateOrDelegate());
    const double off = OFF_SURFACE / KH2_UNITS_A_DAYS_UNIT * 4096.0;
    /* setCamera only moves the look-at along this line in first person, which turns nothing */
    if (s.republish || s.first_person || room < 0) {
        return;
    }
    /* a room not loaded yet: a field's first commit (TrackEntryGroup's wCount) */
    if (((const u16 *)KHDAYS_W(GetTrackEntryBase(room)))[1] == 0) {
        return;
    }
    if (fx_distance(eye_words, origin_words) > 0.0) {
        const KhdaysRef world = GetTrackEntryBase(room);
        const double motion[3] = {(double)eye_words[0] - origin_words[0], (double)eye_words[1] - origin_words[1],
                                  (double)eye_words[2] - origin_words[2]};
        double fraction, normal[3];
        int ceiling;
        if (line_hit(world, origin_words, motion, &fraction, normal, &ceiling) && fraction < 1.0) {
            double point[3] = {origin_words[0] + motion[0] * fraction, origin_words[1] + motion[1] * fraction,
                               origin_words[2] + motion[2] * fraction};
            if (ceiling) {
                const double under[3] = {motion[0], point[1] - origin_words[1], motion[2]};
                double again, ignored[3];
                int above;
                point[0] = eye_words[0];
                point[2] = eye_words[2];
                if (line_hit(world, origin_words, under, &again, ignored, &above) && again < 1.0) {
                    for (int i = 0; i < 3; ++i) {
                        point[i] = origin_words[i] + under[i] * again;
                    }
                }
            }
            for (int i = 0; i < 3; ++i) {
                eye_words[i] = (int)lround(point[i] + normal[i] * off);
            }
        }
    }
    {
        const int top = Ov002_GetPeerRecordValue(Ov002_GetSlotTableByte(room));
        if (top < eye_words[1]) {
            eye_words[1] = top;
        }
    }
}

/* What collision left of the wanted eye, and the eye the springs reached, to collide again. */
static void frame_settle(int origin, int eye)
{
    int *words = KHDAYS_W(eye);
    if (s.republish) {
        return;
    }
    if (moved_from(words, s.written_eye)) {
        s.camera.eye_target = read_vec(words); /* ObjCamera::collision moved it */
    }
    kh2_camera_settle(&s.camera, s.in.ticks);
    write_vec(KHDAYS_W(origin), kh2_camera_collision_origin(&s.camera, &s.in));
    write_vec(s.written_eye, s.camera.eye);
    write_vec(words, s.camera.eye);
}

/* The camera's fields from the result. */
static void frame_publish(int camera, int eye)
{
    int *cam = KHDAYS_W(camera);
    const int *words = KHDAYS_W(eye);
    Kh2Camera *c = &s.camera;
    s.republish = 0;
    if (moved_from(words, s.written_eye)) {
        c->eye = c->eye_target = read_vec(words); /* setCamera: the eye is where collision left it */
    }
    write_vec(cam + WORD_FOCUS, c->at);
    write_vec(cam + WORD_EYE, c->eye);
    {
        const float dx = c->eye.x - c->at.x, dy = c->eye.y - c->at.y, dz = c->eye.z - c->at.z;
        const int distance = to_fx(sqrtf(dx * dx + dy * dy + dz * dz));
        const int yaw = (int)lroundf(c->yaw / TWO_PI * 65536.0f) & 0xffff;
        /* the player's heading is relative to this yaw; the rest is where the game's camera takes up from */
        cam[WORD_YAW] = cam[WORD_TARGET_YAW] = yaw;
        cam[WORD_DISTANCE] = cam[WORD_TARGET_DISTANCE] = distance;
        cam[WORD_EYE_HEIGHT] = to_fx(dy);
    }
    cam[WORD_FOV_SIN] = (int)lroundf(sinf(c->fov_half) * 4096.0f);
    cam[WORD_FOV_COS] = (int)lroundf(cosf(c->fov_half) * 4096.0f);
    cam[WORD_NEAR] = NEAR_PLANE;

    if (s.tracing < 0) {
        s.tracing = getenv("KHDAYS_TRACE_KH2_CAMERA") != NULL;
    }
    if (s.tracing) {
        fprintf(stderr,
                "kh2 camera: frame %u %s yaw %.3f target %.3f radius %.1f at %.1f %.1f %.1f eye %.1f %.1f %.1f "
                "player %.1f %.1f %.1f fov %.2f turn %.2f zoom %.2f manual %d home %d zoomed %d select radius %.1f "
                "blend %.2f first person %d pitch %.2f fade %.2f\n",
                s.frame, c->locked ? "lock" : "follow", (double)c->yaw, (double)c->yaw_target, (double)c->radius,
                (double)c->at.x, (double)c->at.y, (double)c->at.z, (double)c->eye.x, (double)c->eye.y, (double)c->eye.z,
                (double)s.in.player.x, (double)s.in.player.y, (double)s.in.player.z,
                (double)(c->fov_half * 2.0f * 360.0f / TWO_PI), (double)s.in.turn, (double)s.in.zoom, s.in.manual,
                s.in.home, s.in.zoomed, (double)c->zoom_radius, (double)c->zoom_blend, s.first_person, (double)c->pitch,
                (double)s.fade);
    }
}

void khdays_kh2_camera_frame(int camera, int player, int behind_yaw, int origin, int eye)
{
    if (frame_prepare(camera, player, behind_yaw, origin, eye)) {
        frame_collide(origin, eye);
        frame_settle(origin, eye);
        frame_collide(origin, eye);
        frame_publish(camera, eye);
    }
}

/* Ov002_ReaimActor, before each commit of the camera to the matrices. While a mission sets up, a tutorial's page among
 * it, the game's tick returns early (Ov002_Camera_UpdateFollow's first gates) and what shows is what was last committed:
 * Days' first place. A commit no frame of this camera came before gets one, without control. A commit can come before
 * the tick of its game frame, so a tick's frame counts for the next game frame too; a commit's only for its own. */
void khdays_kh2_camera_commit(int camera)
{
    const int *cam = KHDAYS_W(camera);
    const int player = QueryActiveStateOrDelegate();
    if (s.frame != 0u && khdays_input_game_frames() - s.frame <= (s.committed ? 0u : 1u)) {
        return;
    }
    if (cam[WORD_STAGE] == 0 && claims_camera(cam) && GetEntryField20ByIndex(player) != 0) {
        int origin[3], eye[3];
        s.want = WANT_FOLLOW;
        s.input_off = 1;
        s.bits = 0;
        s.home = 0;
        khdays_kh2_camera_frame(camera, func_ov022_020881f8(player), func_ov022_02088254(player),
                                (int)KHDAYS_REF(origin), (int)KHDAYS_REF(eye));
        /* after that frame: a commit can come before its game frame's tick, and a press taken for this frame stays */
        s.no_press = 1;
        s.committed = 1;
    }
}
