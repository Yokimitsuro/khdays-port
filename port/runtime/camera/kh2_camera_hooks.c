/* The KH2 camera in the game (kh2_camera_hooks.h). The game's camera runs as ever each frame, keeping its
 * flags, shake and fades; these then put KH2's eye and look-at in its fields. KHDAYS_TRACE_KH2_CAMERA=1
 * reports each frame's numbers. docs/KH2_CAMERA.md has the whole. */
#include "kh2_camera_hooks.h"
#include "kh2_camera.h"
#include "../core/window.h"
#include "../input/controls_hooks.h"
#include "../input/input.h"
#include "../../host/app/host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern int GetFrameRateMode(void); /* 0: 30 fps, 1: 20 fps, 2: 60 fps */
extern int QueryActiveStateOrDelegate(void);
extern int GetEntryField20ByIndex(int idx);
extern int Ov022_GetEntryField66(int player);
extern int Ov002_GetSlotTableByte(int world);
extern int Ov002_GetPeerRecordValue(int slot);
extern int Ov002_CastRayAlongMotion(KhdaysRef start, KhdaysRef motion, KhdaysRef hit, KhdaysRef normal,
                                    KhdaysRef exclude);
extern int Ov002_CastRayForContactPoint(KhdaysRef origin, KhdaysRef dir, KhdaysRef hit, KhdaysRef exclude);

/* The game's held buttons (src/engine/Pad_Sample.c) */
#define PAD_HELD (*(volatile u16 *)KHDAYS_W(0x0204c18c))

/* The camera's fields (Ov002_TickCameraTransition's CameraState), as words */
enum {
    WORD_FOV_SIN = 0,
    WORD_FOV_COS = 1,
    WORD_FOCUS = 0x14 / 4,
    WORD_EYE = 0x20 / 4,
    WORD_FLAGS = 0x38 / 4,
    WORD_FLAGS2 = 0x3c / 4,
    WORD_DISTANCE = 0x54 / 4,
    WORD_YAW = 0x58 / 4,
    WORD_EYE_HEIGHT = 0x5c / 4,
    WORD_TARGET_DISTANCE = 0x7c / 4,
    WORD_TARGET_YAW = 0x80 / 4,
};

enum {
    FLAG_RESET = 0x8,
    FLAG_CONTROL = 0x2,
    FLAG_HELD_POINT = 0x100,
    FLAG_SELECT_VIEW = 0x2000,
    /* switched off (4), a script's hold (0x200), a pending reset (0x400), a script's camera (0x8000), the
     * heading tied to it (0x10000), its transition (0x20000), modes 9 and 10 */
    FLAGS_NOT_GAMEPLAY = 0x4 | 0x200 | 0x400 | 0x8000 | 0x10000 | 0x20000 | 0x08000000 | 0x20000000,
    /* Chase Cam's Config alone: Days' hold of R (flag 0x10000000) and KH2's R2 swap it, and neither is free
     * on the port's controls */
    FLAG2_MANUAL = 0x1,
    FLAG2_SLOW = 0x2,
    FLAG2_FAST = 0x4,
    FLAG2_INVERT_Y = 0x8,
    FLAG2_INVERT_X = 0x10,
    FLAG2_TYPE_B = 0x20,
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
/* A pause this long (a cutscene) starts again from the start radius. */
#define KEEP_RADIUS_FRAMES 30u

#define TWO_PI 6.28318530717958647692f
#define KH2_UNITS_A_DAYS_UNIT 100.0f

enum { WANT_NONE, WANT_FOLLOW, WANT_LOCK };

static struct {
    Kh2Camera camera;
    Kh2CameraInput in;
    int want;
    KhdaysRef target;
    int home, center, input_off, bits;
    int active;
    u32 frame;
    Kh2CameraVec last_player;
    int fov_held; /* the FOV is this camera's; Days' goes back when the game's camera takes over */
    int written_eye[3];
    int tracing;
} s = {.tracing = -1};

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
    return (in->enhancements & KHDAYS_ENH_KH2_CAMERA) != 0 && (flags2 & FLAG2_TYPE_B) == 0 &&
           (flags & FLAG_CONTROL) != 0 && (flags & FLAGS_NOT_GAMEPLAY) == 0 &&
           (mode == 0 || mode == 3 || mode == 8); /* 3 is a shake over the ordinary camera */
}

int khdays_kh2_camera_active(void)
{
    return s.active && khdays_input_game_frames() - s.frame <= 2u;
}

void khdays_kh2_camera_center(void)
{
    s.center = 1;
}

int khdays_kh2_camera_follow(int flags, int flags2, int mode, int input_off, int bits)
{
    if (!khdays_kh2_camera_claims(flags, flags2, mode)) {
        return 0;
    }
    s.want = WANT_FOLLOW;
    s.input_off = input_off;
    s.bits = bits;
    /* With lock-on on its own buttons the game's R presses are the lock's (input.c) */
    s.home = (flags & FLAG_RESET) != 0 && (khdays_input_snapshot()->enhancements & KHDAYS_ENH_SEPARATE_LOCK_ON) == 0;
    return (flags & FLAG_RESET) != 0;
}

void khdays_kh2_camera_lock(int target, int flags, int flags2, int mode, int input_off)
{
    if (khdays_kh2_camera_claims(flags, flags2, mode) && (flags & FLAG_HELD_POINT) == 0) {
        s.want = WANT_LOCK;
        s.target = (KhdaysRef)target;
        s.input_off = input_off;
        s.bits = 0;
        s.home = 0;
    }
}

/* The movement stick as KH2's Stick[1] has it: a unit direction, y down, and how far. */
static void read_movement(Kh2CameraInput *in)
{
    const KhdaysInputSnapshot *snapshot = khdays_input_previous_snapshot();
    float x = 0.0f, y = 0.0f, amount = 0.0f;
    if ((snapshot->enhancements & KHDAYS_ENH_ANALOG_MOVEMENT) != 0 &&
        (snapshot->move_x != 0 || snapshot->move_y != 0)) {
        x = (float)snapshot->move_x / 32767.0f;
        y = (float)-snapshot->move_y / 32767.0f;
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
    khdays_controls_camera_look(&x, &y);
    if (s.input_off) {
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

int khdays_kh2_camera_prepare(int camera, int player, int behind_yaw, int origin, int eye)
{
    int *cam = KHDAYS_W(camera);
    const u32 frame = khdays_input_game_frames();
    const int want = s.want;
    Kh2CameraInput *in = &s.in;
    int warp = 0;

    s.want = WANT_NONE;
    if (want == WANT_NONE) {
        if (s.fov_held) {
            /* from the angles Ov002_Camera_UpdateFollow keeps at +0xa0 and +0xa2 */
            const u16 *angles = (const u16 *)((const char *)cam + 0xa0);
            cam[WORD_FOV_SIN] = (int)lroundf(sinf(angle_radians(angles[0])) * 4096.0f);
            cam[WORD_FOV_COS] = (int)lroundf(cosf(angle_radians(angles[1])) * 4096.0f);
            s.fov_held = 0;
        }
        s.center = 0;
        s.active = 0;
        return 0;
    }

    in->player = read_vec(KHDAYS_W(player));
    in->ticks = (float)ticks_a_frame();
    if (!s.active || frame - s.frame > 2u) {
        const float radius = s.active || frame - s.frame <= KEEP_RADIUS_FRAMES ? s.camera.radius : 0.0f;
        s.fov_held = 1;
        /* outdoors everywhere: Days has no indoor maps that this could read (docs/KH2_CAMERA.md) */
        kh2_camera_start(&s.camera, &kh2_camera_outdoor, read_vec(cam + WORD_FOCUS), read_vec(cam + WORD_EYE),
                         angle_radians(cam[WORD_YAW]), atan2f((float)cam[WORD_FOV_SIN], (float)cam[WORD_FOV_COS]),
                         s.frame != 0u ? radius : 0.0f);
        s.last_player = in->player;
        s.active = 1;
    }
    {
        const float dx = in->player.x - s.last_player.x, dz = in->player.z - s.last_player.z;
        const float moved = sqrtf(dx * dx + dz * dz);
        warp = moved > WARP_DISTANCE;
        in->moved = moved > 0.0f;
    }
    s.last_player = in->player;
    s.frame = frame;
    in->behind_yaw = angle_radians(behind_yaw);
    in->zoomed = (cam[WORD_FLAGS] & FLAG_SELECT_VIEW) != 0;
    in->manual = (cam[WORD_FLAGS2] & FLAG2_MANUAL) != 0 || in->zoomed;
    in->home = s.home || s.center;
    s.home = s.center = 0;
    read_movement(in);
    read_look(in, cam[WORD_FLAGS2]);

    if (want == WANT_LOCK) {
        kh2_camera_lock(&s.camera, in, read_vec(KHDAYS_W(s.target)));
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

/* `from` moved `by` (fx32) along `direction`. */
static void fx_along(int *out, const int *from, const double *direction, double by)
{
    for (int i = 0; i < 3; ++i) {
        out[i] = from[i] + (int)lround(direction[i] * by);
    }
}

/* As dk::ObjCamera::collision: a line from the look-at, the eye a little off what it hits. Not
 * Ov002_ResolveCameraAgainstCollision: its sphere and its 0xc00 off the floor keep the eye from going as low as
 * KH2's. Ov002_CastRayAlongMotion skips upright walls when it gives a normal, so the plain cast takes those, and
 * the cast back from the eye is that function's, for an eye already behind a surface. The map's ceiling stays. */
void khdays_kh2_camera_collide(int origin, int eye, int dir, int hit, int normal)
{
    int *eye_words = KHDAYS_W(eye), *dir_words = KHDAYS_W(dir);
    const int *origin_words = KHDAYS_W(origin), *hit_words = KHDAYS_W(hit), *normal_words = KHDAYS_W(normal);
    const int player = QueryActiveStateOrDelegate();
    const int world = Ov022_GetEntryField66(player);
    const double reach = fx_distance(eye_words, origin_words);
    const double off = OFF_SURFACE / KH2_UNITS_A_DAYS_UNIT * 4096.0;
    KhdaysRef exclude = 0;
    if (player < 0 || world < 0) {
        return;
    }
    if (GetEntryField20ByIndex(player) != 0) {
        exclude = *(const KhdaysRef *)KHDAYS_W((KhdaysRef)GetEntryField20ByIndex(player) + 0x20);
    }
    if (reach > 0.0) {
        const double unit[3] = {(eye_words[0] - origin_words[0]) / reach, (eye_words[1] - origin_words[1]) / reach,
                                (eye_words[2] - origin_words[2]) / reach};
        double nearest = reach;
        int found = 0, best[3] = {0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            dir_words[i] = eye_words[i] - origin_words[i];
        }
        if (Ov002_CastRayAlongMotion((KhdaysRef)origin, (KhdaysRef)dir, (KhdaysRef)hit, (KhdaysRef)normal, exclude) !=
            0) {
            const double distance = fx_distance(hit_words, origin_words);
            if (distance < nearest) {
                const double facing = normal_words[0] * unit[0] + normal_words[1] * unit[1] + normal_words[2] * unit[2];
                const double surface[3] = {normal_words[0] / 4096.0, normal_words[1] / 4096.0,
                                           normal_words[2] / 4096.0};
                nearest = distance;
                found = 1;
                if (facing < 0.0) {
                    fx_along(best, hit_words, surface, off);
                } else {
                    fx_along(best, hit_words, unit, -off);
                }
            }
        }
        if (Ov002_CastRayForContactPoint((KhdaysRef)origin, (KhdaysRef)dir, (KhdaysRef)hit, exclude) != 0) {
            const double distance = fx_distance(hit_words, origin_words);
            if (distance < nearest) {
                nearest = distance;
                found = 1;
                fx_along(best, hit_words, unit, -off);
            }
        }
        if (!found) {
            for (int i = 0; i < 3; ++i) {
                dir_words[i] = origin_words[i] - eye_words[i];
            }
            if (Ov002_CastRayAlongMotion((KhdaysRef)eye, (KhdaysRef)dir, (KhdaysRef)hit, (KhdaysRef)normal, exclude) !=
                    0 &&
                fx_distance(hit_words, eye_words) < reach) {
                found = 1;
                fx_along(best, hit_words, unit, -off);
            }
        }
        if (found) {
            for (int i = 0; i < 3; ++i) {
                eye_words[i] = best[i];
            }
        }
    }
    {
        const int ceiling = Ov002_GetPeerRecordValue(Ov002_GetSlotTableByte(world));
        if (ceiling < eye_words[1]) {
            eye_words[1] = ceiling;
        }
    }
}

void khdays_kh2_camera_settle(int origin, int eye)
{
    int *words = KHDAYS_W(eye);
    if (moved_from(words, s.written_eye)) {
        s.camera.eye_target = read_vec(words); /* ObjCamera::collision moved it */
    }
    kh2_camera_settle(&s.camera, s.in.ticks);
    write_vec(KHDAYS_W(origin), kh2_camera_collision_origin(&s.camera, &s.in));
    write_vec(s.written_eye, s.camera.eye);
    write_vec(words, s.camera.eye);
}

void khdays_kh2_camera_publish(int camera, int eye)
{
    int *cam = KHDAYS_W(camera);
    const int *words = KHDAYS_W(eye);
    Kh2Camera *c = &s.camera;
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

    if (s.tracing < 0) {
        s.tracing = getenv("KHDAYS_TRACE_KH2_CAMERA") != NULL;
    }
    if (s.tracing) {
        fprintf(stderr,
                "kh2 camera: frame %u %s yaw %.3f target %.3f radius %.1f at %.1f %.1f %.1f eye %.1f %.1f %.1f "
                "player %.1f %.1f %.1f fov %.2f turn %.2f zoom %.2f manual %d home %d zoomed %d\n",
                s.frame, c->locked ? "lock" : "follow", (double)c->yaw, (double)c->yaw_target, (double)c->radius,
                (double)c->at.x, (double)c->at.y, (double)c->at.z, (double)c->eye.x, (double)c->eye.y, (double)c->eye.z,
                (double)s.in.player.x, (double)s.in.player.y, (double)s.in.player.z,
                (double)(c->fov_half * 2.0f * 360.0f / TWO_PI), (double)s.in.turn, (double)s.in.zoom, s.in.manual,
                s.in.home, s.in.zoomed);
    }
}
