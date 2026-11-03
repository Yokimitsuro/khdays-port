/* KH2's gameplay camera's arithmetic (kh2_camera.h). KH2's y points down; here it points up, so its
 * "pos.y - 170" is 170 above the position. Each function names the one of KH2's it follows. */
#include "kh2_camera.h"

#include <math.h>

#define PI 3.14159265358979323846f
#define DEGREES(d) ((d) * PI / 180.0f)

/* dk::paramTable; the indoor FOV from __sti___13_objcamera_cpp */
const Kh2CameraParams kh2_camera_outdoor = {1.5f, 420.0f, 380.0f, 250.0f, 500.0f, 0.008f};
const Kh2CameraParams kh2_camera_indoor = {1.04719758f, 600.0f, 520.0f, 400.0f, 700.0f, 0.005f};

#define AT_HEIGHT 170.0f         /* calcAtTarget */
#define EYE_BELOW_AT 150.0f      /* calcEyeTarget */
#define SUBJECTIVE_HEIGHT 150.0f /* the player's YS::PREF::Party SubjectiveOffset, by the user */
#define SUBJECTIVE_REACH 350.0f  /* calcSubjective */
#define SUBJECTIVE_PITCH DEGREES(70.0f)
#define BODY_HEIGHT 100.0f /* updateTargetCamera: the point of the player it keeps on screen */
#define HOME_TURN DEGREES(10.0f)
#define AUTO_TURN DEGREES(0.25f)
#define MOVE_TURN DEGREES(3.0f) /* OBJECTIVE_MOVE_SPEED */
#define MOVE_RADIUS 8.0f        /* objectiveUpDown */
#define AUTO_RADIUS 0.5f        /* auto_updown */
#define LOCK_RADIUS 1.0f        /* autoLock_updown */
#define LOCK_ZOOM 2.0f          /* updateTargetCamera */
#define LOCK_SHARE 0.3f         /* TargetCamera::update */
#define LOCK_SIDE DEGREES(20.0f)
#define LOCK_FAR 300.0f
#define LOCK_STEEP 0.5f
#define SPRING 2.5f
#define DAMP 0.5f
#define SPRING_LENGTH 1.0f
#define EYE_RATE 0.07f
#define AT_RATE_ADD 0.03f
#define DAYS_CAMERA_SEEN 10u /* the port's: game frames of Days' camera, a third of a second, that were seen */
#define UPDATE_TICKS 2.0f    /* dk::Vsync::setLimit(1): the field updates every other vsync */
#define FOV_EASE 0.25f       /* the port's: two of these in a row start and end the blend gently */

static Kh2CameraVec sub(Kh2CameraVec a, Kh2CameraVec b)
{
    Kh2CameraVec r = {a.x - b.x, a.y - b.y, a.z - b.z};
    return r;
}

static Kh2CameraVec add_scaled(Kh2CameraVec a, Kh2CameraVec b, float k)
{
    Kh2CameraVec r = {a.x + b.x * k, a.y + b.y * k, a.z + b.z * k};
    return r;
}

static float dot(Kh2CameraVec a, Kh2CameraVec b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float length(Kh2CameraVec a)
{
    return sqrtf(dot(a, a));
}

static Kh2CameraVec above(Kh2CameraVec p, float height)
{
    p.y += height;
    return p;
}

/* YS::MATH::FixRadian */
static float wrap(float a)
{
    a = fmodf(a + PI, 2.0f * PI);
    if (a < 0.0f) {
        a += 2.0f * PI;
    }
    return a - PI;
}

/* YS::MATH::TurnRadian */
static float turn_towards(float from, float to, float step)
{
    const float difference = wrap(to - from);
    if (fabsf(difference) <= step) {
        return wrap(to);
    }
    return wrap(from + (difference > 0.0f ? step : -step));
}

static float clamp_to(const Kh2CameraParams *params, float radius)
{
    if (radius < params->min_radius) {
        return params->min_radius;
    }
    return radius > params->max_radius ? params->max_radius : radius;
}

static float clamp_radius(const Kh2Camera *c, float radius)
{
    return clamp_to(c->params, radius);
}

/* calcEyeTarget's parabola: how far above its lowest place the eye is at a radius. */
static float height_at(const Kh2CameraParams *params, float radius)
{
    const float over = radius - params->min_radius;
    return over * over * params->curve;
}

/* Entering the Select view starts its radius at the row's start, as ObjCamera::setParam does for a row. */
static void set_zoomed(Kh2Camera *c, const Kh2CameraInput *in)
{
    if (in->zoomed && !c->zoomed) {
        c->zoom_radius = kh2_camera_indoor.start_radius;
    }
    c->zoomed = in->zoomed;
}

/* auto_updown, autoLock_updown */
static void ease_radius(Kh2Camera *c, float to, float rate)
{
    if (fabsf(c->radius - to) >= 1.0f) {
        c->radius = clamp_radius(c, c->radius + (c->radius > to ? -rate : rate));
    }
}

int kh2_camera_takeover(int started, unsigned days_frames, int same_place)
{
    if ((started && days_frames == 0u) || (days_frames < DAYS_CAMERA_SEEN && same_place)) {
        return KH2_CAMERA_RESUME;
    }
    return days_frames >= DAYS_CAMERA_SEEN ? KH2_CAMERA_BLEND : KH2_CAMERA_SNAP;
}

float kh2_camera_half_angle(float horizontal_fov)
{
    return atanf(tanf(horizontal_fov * 0.5f) * 0.75f); /* kn::Camera::init's frame: 640x480 */
}

void kh2_camera_start(Kh2Camera *c, const Kh2CameraParams *params, Kh2CameraVec at, Kh2CameraVec eye, float yaw,
                      float fov_half, float radius)
{
    c->params = params;
    c->yaw = c->yaw_target = wrap(yaw);
    c->radius = radius > 0.0f ? radius : params->start_radius;
    c->pitch = 0.0f;
    c->zoom_radius = kh2_camera_indoor.start_radius;
    c->zoom_blend = c->zoom_ease = 0.0f;
    c->fov_half = c->fov_ease = fov_half;
    c->zoomed = 0;
    c->at = c->at_target = c->at_target_prev = at;
    c->eye = c->eye_target = c->eye_target_prev = eye;
    c->pivot = at;
    c->locked = 0;
    c->snap = 0;
}

/* The stick, both in ObjCamera::objective_control and in TargetCamera::control. */
static void apply_look(Kh2Camera *c, const Kh2CameraInput *in)
{
    if (in->turn != 0.0f) {
        c->yaw = c->yaw_target = wrap(c->yaw + MOVE_TURN * in->turn * in->ticks);
    }
    if (in->zoom != 0.0f) {
        const float change = MOVE_RADIUS * in->zoom * in->ticks;
        if (c->zoomed) {
            c->zoom_radius = clamp_to(&kh2_camera_indoor, c->zoom_radius + change);
        } else {
            c->radius = clamp_radius(c, c->radius + change);
        }
    }
}

/* ObjCamera::calcEyeTarget: one number sets how far back and how high the eye is. */
static void eye_from(Kh2Camera *c, Kh2CameraVec base, float ticks)
{
    const float share = ticks / UPDATE_TICKS;
    float radius, height;
    /* the place between the two rows' moves as the FOV does (kh2_camera_settle), the same two easings */
    c->zoom_ease += ((c->zoomed ? 1.0f : 0.0f) - c->zoom_ease) * FOV_EASE * share;
    c->zoom_blend += (c->zoom_ease - c->zoom_blend) * FOV_EASE * share;
    radius = c->radius + (c->zoom_radius - c->radius) * c->zoom_blend;
    height = height_at(c->params, c->radius);
    height += (height_at(&kh2_camera_indoor, c->zoom_radius) - height) * c->zoom_blend;
    c->yaw = c->snap ? c->yaw_target : turn_towards(c->yaw, c->yaw_target, HOME_TURN * ticks);
    c->eye_target.x = base.x + radius * sinf(c->yaw);
    c->eye_target.y = base.y - EYE_BELOW_AT + height;
    c->eye_target.z = base.z + radius * cosf(c->yaw);
    c->pivot = base;
}

void kh2_camera_follow(Kh2Camera *c, const Kh2CameraInput *in)
{
    const int look_on = in->turn != 0.0f || in->zoom != 0.0f;
    c->at_target_prev = c->at_target;
    c->eye_target_prev = c->eye_target;
    c->locked = 0;
    set_zoomed(c, in);
    if (in->home) {
        c->yaw_target = in->behind_yaw;
        if (c->zoomed) {
            c->zoom_radius = kh2_camera_indoor.start_radius;
        } else {
            c->radius = c->params->start_radius;
        }
        c->eye = c->eye_target;
        c->snap = in->move_amount != 0.0f;
    } else {
        apply_look(c, in);
    }
    /* ObjCamera::auto_camera */
    if (!c->snap && !in->manual && in->moved && in->move_amount != 0.0f && !look_on && in->move_y < 0.9f) {
        /* x 0 is KH2's player running where the camera faces, nothing to turn to. The port's player keeps the
         * stick's own angle, a little off that: turning to it would bring the same angle back, a curve. */
        if (in->move_x != 0.0f) {
            const float step = (fabsf(in->move_x) + 1.0f) * 2.0f * (fabsf(in->move_y) + 1.0f) * in->move_amount *
                               AUTO_TURN * in->ticks;
            c->yaw = c->yaw_target = turn_towards(c->yaw, in->behind_yaw, step);
        }
        ease_radius(c, c->params->start_radius, AUTO_RADIUS * in->ticks);
    }
    c->at_target = above(in->player, AT_HEIGHT);
    eye_from(c, c->at_target, in->ticks);
}

void kh2_camera_lock(Kh2Camera *c, const Kh2CameraInput *in, Kh2CameraVec target)
{
    const Kh2CameraVec head = above(in->player, AT_HEIGHT);
    const Kh2CameraVec body = above(in->player, BODY_HEIGHT);
    const Kh2CameraVec between = add_scaled(head, sub(target, head), LOCK_SHARE);
    const Kh2CameraVec line = sub(in->player, c->at_target);
    const unsigned target_flags = kh2_camera_screen_flags(c, target);
    const unsigned body_flags = kh2_camera_screen_flags(c, body);
    const int target_low = (target_flags & KH2_CAMERA_ABOVE) == 0 && (target_flags & KH2_CAMERA_BELOW) != 0;
    const int body_low = (body_flags & KH2_CAMERA_ABOVE) == 0 && (body_flags & KH2_CAMERA_BELOW) != 0;
    const Kh2CameraVec reach = sub(between, body);
    const float reach_length = length(reach);

    c->at_target_prev = c->at_target;
    c->eye_target_prev = c->eye_target;
    c->locked = 1;
    set_zoomed(c, in);
    if (target_low) {
        c->radius -= LOCK_ZOOM * in->ticks;
    }
    if (body_low) {
        c->radius -= LOCK_ZOOM * in->ticks;
    }
    c->radius = clamp_radius(c, c->radius);
    if (reach_length <= LOCK_FAR || reach.y / reach_length <= LOCK_STEEP) {
        const unsigned off = target_flags != 0 ? target_flags : body_flags;
        if (!target_low && !body_low) {
            ease_radius(c, c->params->lock_radius, LOCK_RADIUS * in->ticks);
        }
        if (off != 0) {
            /* KH2 adds 20 degrees for a point off its left: its y down turns its yaw the other way round
             * on screen, so here it is taken off, the point kept on the side it left by. */
            float side = 0.0f;
            if ((off & KH2_CAMERA_LEFT) != 0) {
                side = -LOCK_SIDE;
            } else if ((off & KH2_CAMERA_RIGHT) != 0) {
                side = LOCK_SIDE;
            }
            c->yaw_target = wrap(atan2f(line.x, line.z) + side);
        }
        c->at_target = between;
    } else {
        c->at_target = head;
    }
    apply_look(c, in);
    if (in->home) { /* TargetCamera::control */
        c->yaw_target = wrap(atan2f(line.x, line.z));
        c->radius = c->params->lock_radius;
    }
    eye_from(c, head, in->ticks);
}

void kh2_camera_first_person(Kh2Camera *c, const Kh2CameraInput *in)
{
    float look;
    c->at_target_prev = c->at_target;
    c->eye_target_prev = c->eye_target;
    c->locked = 0;
    c->zoomed = 0;
    c->zoom_blend = c->zoom_ease = 0.0f;
    /* YS::PLAYER::_OVR_post_exec: only with control; without, what turned the player turns the view */
    c->yaw = wrap(in->can_turn ? c->yaw + MOVE_TURN * in->turn * in->ticks : in->behind_yaw);
    c->yaw_target = c->yaw;
    if (in->home) {
        c->pitch = 0.0f;
    }
    /* in->zoom: up on the stick is negative, as it brings the eye down, which looks up */
    c->pitch -= MOVE_TURN * in->zoom * in->ticks;
    if (c->pitch > SUBJECTIVE_PITCH) {
        c->pitch = SUBJECTIVE_PITCH;
    } else if (c->pitch < -SUBJECTIVE_PITCH) {
        c->pitch = -SUBJECTIVE_PITCH;
    }
    c->eye_target = above(in->player, SUBJECTIVE_HEIGHT);
    look = cosf(c->pitch) * SUBJECTIVE_REACH;
    c->at_target.x = c->eye_target.x - look * sinf(c->yaw);
    c->at_target.y = c->eye_target.y + sinf(c->pitch) * SUBJECTIVE_REACH;
    c->at_target.z = c->eye_target.z - look * cosf(c->yaw);
    c->pivot = c->eye_target;
}

Kh2CameraVec kh2_camera_collision_origin(const Kh2Camera *c, const Kh2CameraInput *in)
{
    return c->locked ? above(in->player, AT_HEIGHT) : c->at;
}

/* ObjCamera::springDamp */
static Kh2CameraVec spring(Kh2CameraVec current, Kh2CameraVec target, Kh2CameraVec previous, float rate)
{
    const Kh2CameraVec away = sub(current, target);
    const float distance = length(away);
    float force;
    if (distance == 0.0f) {
        return current;
    }
    {
        const Kh2CameraVec moved = sub(previous, target);
        force = SPRING * (SPRING_LENGTH - distance) + DAMP * (dot(away, moved) * rate / distance);
    }
    return add_scaled(current, away, force * rate / distance);
}

void kh2_camera_settle(Kh2Camera *c, float ticks)
{
    const float wanted_fov = kh2_camera_half_angle(c->zoomed ? kh2_camera_indoor.fov : c->params->fov);
    const float updates = ticks / UPDATE_TICKS;
    const int steps = updates > 1.0f ? (int)ceilf(updates) : 1;
    const float share = updates / (float)steps; /* a frame that is not a whole number of KH2's updates */
    const float eye_rate = EYE_RATE * share;
    const float at_rate = (EYE_RATE + AT_RATE_ADD) * share;
    Kh2CameraVec at_step, eye_step;
    if (c->snap) {
        c->at = c->at_target_prev = c->at_target;
        c->eye = c->eye_target_prev = c->eye_target;
        c->snap = 0;
    }
    at_step = sub(c->at_target, c->at_target_prev);
    eye_step = sub(c->eye_target, c->eye_target_prev);
    for (int i = 1; i <= steps; ++i) {
        const float now = (float)i / (float)steps, before = (float)(i - 1) / (float)steps;
        c->at = spring(c->at, add_scaled(c->at_target_prev, at_step, now),
                       add_scaled(c->at_target_prev, at_step, before), at_rate);
        c->eye = spring(c->eye, add_scaled(c->eye_target_prev, eye_step, now),
                        add_scaled(c->eye_target_prev, eye_step, before), eye_rate);
        c->fov_ease += (wanted_fov - c->fov_ease) * FOV_EASE * share;
        c->fov_half += (c->fov_ease - c->fov_half) * FOV_EASE * share;
    }
}

/* TargetCamera::checkVisibleObject against dk::rectTable (32..480, 8..408). */
unsigned kh2_camera_screen_flags(const Kh2Camera *c, Kh2CameraVec point)
{
    const float half_height = tanf(c->fov_half);
    const float half_width = half_height / 0.75f;
    const Kh2CameraVec look = sub(c->at, c->eye);
    const float look_length = length(look);
    const Kh2CameraVec to = sub(point, c->eye);
    Kh2CameraVec forward, right, up;
    float right_length, depth, x, y;
    unsigned flags = 0;
    if (look_length == 0.0f) {
        return 0;
    }
    forward = add_scaled((Kh2CameraVec){0.0f, 0.0f, 0.0f}, look, 1.0f / look_length);
    right = (Kh2CameraVec){-forward.z, 0.0f, forward.x};
    right_length = length(right);
    if (right_length == 0.0f) {
        return 0;
    }
    right = add_scaled((Kh2CameraVec){0.0f, 0.0f, 0.0f}, right, 1.0f / right_length);
    up = (Kh2CameraVec){right.y * forward.z - right.z * forward.y, right.z * forward.x - right.x * forward.z,
                        right.x * forward.y - right.y * forward.x};
    depth = dot(to, forward);
    if (depth == 0.0f) {
        return KH2_CAMERA_BEHIND;
    }
    x = 256.0f * (1.0f + dot(to, right) / depth / half_width);
    y = 208.0f * (1.0f - dot(to, up) / depth / half_height);
    flags |= x < 32.0f ? KH2_CAMERA_LEFT : 0u;
    flags |= x > 480.0f ? KH2_CAMERA_RIGHT : 0u;
    flags |= y < 8.0f ? KH2_CAMERA_ABOVE : 0u;
    flags |= y > 408.0f ? KH2_CAMERA_BELOW : 0u;
    flags |= depth < 1.0f ? KH2_CAMERA_BEHIND : 0u;
    flags |= depth > 4000000.0f ? KH2_CAMERA_BEYOND : 0u;
    return flags;
}
