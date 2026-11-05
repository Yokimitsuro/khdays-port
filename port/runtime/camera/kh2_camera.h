/* Kingdom Hearts II's gameplay camera (dk::ObjCamera's objective mode, dk::TargetCamera), for the
 * KH2 camera option: its arithmetic alone, in KH2's units (Days' x 100), y up. docs/KH2_CAMERA.md
 * cites each number's source. kh2_camera_hooks.c joins it to the game. */
#ifndef KHDAYS_KH2_CAMERA_H
#define KHDAYS_KH2_CAMERA_H

typedef struct Kh2CameraVec {
    float x, y, z;
} Kh2CameraVec;

/* One row of dk::paramTable. */
typedef struct Kh2CameraParams {
    float fov; /* horizontal, radians, in a 4:3 frame */
    float start_radius, lock_radius, min_radius, max_radius, curve;
} Kh2CameraParams;

extern const Kh2CameraParams kh2_camera_outdoor;
extern const Kh2CameraParams kh2_camera_indoor;

/* What one game frame asks of the camera. */
typedef struct Kh2CameraInput {
    Kh2CameraVec player;               /* at the feet */
    float behind_yaw;                  /* the yaw that puts the eye behind the player */
    float move_x, move_y, move_amount; /* the movement stick: a unit direction, y down, and how far */
    float turn;                        /* the look stick's share of the full turn; positive raises the yaw */
    float zoom;                        /* its share of the full radius change; positive moves out (and up) */
    float ticks;                       /* KH2's 60 Hz ticks this game frame takes */
    int moved;                         /* the player moved in x or z */
    int manual;                        /* Chase Cam: Manual */
    int home;                          /* the camera reset was asked for */
    int zoomed;                        /* the Select view: the indoor row */
    int can_turn;                      /* first person: the stick turns the view (YS::OBJ::can_control) */
} Kh2CameraInput;

typedef struct Kh2Camera {
    float yaw, yaw_target, radius;
    float pitch;       /* first person, up positive */
    float zoom_radius; /* the Select view's own radius, in the indoor row's limits; `radius` waits meanwhile */
    float zoom_blend;  /* how far the eye is from `radius`'s place to `zoom_radius`'s, 0..1 */
    float zoom_ease;   /* where that is easing to, as fov_ease */
    float fov_half;    /* the vertical half angle now */
    float fov_ease;    /* where it is easing to, itself easing to the wanted one */
    Kh2CameraVec at, eye;
    Kh2CameraVec at_target, eye_target, at_target_prev, eye_target_prev;
    Kh2CameraVec pivot; /* where a collision is cast from at the target */
    int locked;         /* this frame's targets are the lock-on's */
    int snap;           /* the next settle goes straight to the targets */
    int zoomed;         /* this frame wants the indoor row */
} Kh2Camera;

/* What dk::TargetCamera::checkVisibleObject says of a point on KH2's 512x416 screen. */
enum {
    KH2_CAMERA_ABOVE = 1 << 0,
    KH2_CAMERA_BELOW = 1 << 1,
    KH2_CAMERA_LEFT = 1 << 2,
    KH2_CAMERA_RIGHT = 1 << 3,
    KH2_CAMERA_BEHIND = 1 << 4,
    KH2_CAMERA_BEYOND = 1 << 5,
};

/* The vertical half angle of a horizontal FOV in KH2's 4:3 frame. */
float kh2_camera_half_angle(float horizontal_fov);

/* How this camera comes back after frames that were not its own. `started`: it was running when they began;
 * `days_frames`: how many of them ran Days' camera; `same_place`: the player is in the room and about the place it
 * was. None, or a few at the same place (the camp's exit): it goes on as it was. A few elsewhere: Days' camera set
 * itself up in a new field, so it goes straight to its place. More: they were seen, so it blends from there. */
enum { KH2_CAMERA_RESUME, KH2_CAMERA_SNAP, KH2_CAMERA_BLEND };
int kh2_camera_takeover(int started, unsigned days_frames, int same_place);

/* Takes over from where the camera is, keeping the radius when `radius` is above 0. */
void kh2_camera_start(Kh2Camera *c, Kh2CameraVec at, Kh2CameraVec eye, float yaw, float fov_half, float radius);

/* A frame's targets, without a target (ObjCamera::objective) or locked on (TargetCamera::update). */
void kh2_camera_follow(Kh2Camera *c, const Kh2CameraInput *in);
void kh2_camera_lock(Kh2Camera *c, const Kh2CameraInput *in, Kh2CameraVec target);

/* A frame's targets in first person (ObjCamera::subjective_control): the eye at the player's eye height, looking
 * opposite `yaw`. KH2's view is the player's facing; Days steers that facing as it moves, so here the view has the
 * yaw and the caller sets the facing from it. Without control the game turns the player, an attack to its target,
 * and the view follows `behind_yaw`, which is that facing. */
void kh2_camera_first_person(Kh2Camera *c, const Kh2CameraInput *in);

/* Moves the eye and look-at towards the targets by KH2's per-update springs (ObjCamera::setCamera), for a
 * game frame of `ticks`: KH2's field updates every 2 ticks, so at 30 fps that is one update. */
void kh2_camera_settle(Kh2Camera *c, float ticks);

/* Where a collision is cast from at the settled eye: the look-at, or locked on the head. */
Kh2CameraVec kh2_camera_collision_origin(const Kh2Camera *c, const Kh2CameraInput *in);

unsigned kh2_camera_screen_flags(const Kh2Camera *c, Kh2CameraVec point);

#endif
