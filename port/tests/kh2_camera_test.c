/* The KH2 camera's arithmetic (runtime/camera/kh2_camera.c) against what KH2's code gives. */
#include "../runtime/camera/kh2_camera.h"

#include <math.h>
#include <stdio.h>

#define PI 3.14159265358979323846f

static int failures;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                                            \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static int near_value(float a, float b, float within)
{
    return fabsf(a - b) <= within;
}

static const Kh2CameraVec origin = {0.0f, 0.0f, 0.0f};

static Kh2CameraInput still(void)
{
    Kh2CameraInput in = {0};
    in.player = origin;
    in.ticks = 2.0f;
    return in;
}

static Kh2Camera started(float radius)
{
    Kh2Camera c;
    const Kh2CameraVec at = {0.0f, 170.0f, 0.0f}, eye = {0.0f, 251.0f, 420.0f};
    kh2_camera_start(&c, &kh2_camera_outdoor, at, eye, 0.0f, kh2_camera_half_angle(kh2_camera_outdoor.fov), radius);
    return c;
}

/* kn::Camera::init's frame and dk::paramTable: 69.9 degrees outdoors, 46.8 indoors. */
static void test_fov(void)
{
    CHECK(near_value(2.0f * kh2_camera_half_angle(kh2_camera_outdoor.fov) * 180.0f / PI, 69.87f, 0.02f));
    CHECK(near_value(2.0f * kh2_camera_half_angle(kh2_camera_indoor.fov) * 180.0f / PI, 46.83f, 0.02f));
}

/* calcEyeTarget's parabola: over the feet 20 at the nearest, 155 at 380, 251.2 at 420, 520 at the furthest. */
static void test_height_follows_radius(void)
{
    static const struct {
        float radius, height;
    } cases[] = {{250.0f, 20.0f}, {380.0f, 155.2f}, {420.0f, 251.2f}, {500.0f, 520.0f}};
    for (int i = 0; i < 4; ++i) {
        Kh2CameraInput in = still();
        Kh2Camera c = started(cases[i].radius);
        in.manual = 1;
        kh2_camera_follow(&c, &in);
        CHECK(near_value(c.eye_target.y, cases[i].height, 0.01f));
        CHECK(near_value(c.eye_target.z, cases[i].radius, 0.01f));
        CHECK(near_value(c.at_target.y, 170.0f, 0.001f));
    }
}

/* objective_control and objectiveUpDown: 3 degrees and 8 units a tick at full tilt, within the limits. */
static void test_look(void)
{
    Kh2CameraInput in = still();
    Kh2Camera c = started(0.0f);
    in.turn = 1.0f;
    in.zoom = 1.0f;
    kh2_camera_follow(&c, &in);
    CHECK(near_value(c.yaw, 6.0f * PI / 180.0f, 0.0001f));
    CHECK(c.yaw == c.yaw_target);
    CHECK(near_value(c.radius, 436.0f, 0.001f));
    for (int i = 0; i < 100; ++i) {
        kh2_camera_follow(&c, &in);
    }
    CHECK(near_value(c.radius, 500.0f, 0.001f));
    in.zoom = -1.0f;
    for (int i = 0; i < 100; ++i) {
        kh2_camera_follow(&c, &in);
    }
    CHECK(near_value(c.radius, 250.0f, 0.001f));
}

/* auto_camera: behind the player while moving; never in Manual, with the look stick on, or running at
 * the camera. Sideways it is 1 degree a tick. */
static void test_chase(void)
{
    Kh2CameraInput in = still();
    Kh2Camera c = started(0.0f);
    in.moved = 1;
    in.behind_yaw = PI * 0.5f;
    in.move_x = 1.0f;
    in.move_amount = 1.0f;
    kh2_camera_follow(&c, &in);
    CHECK(near_value(c.yaw, 2.0f * PI / 180.0f, 0.0001f));

    c = started(0.0f);
    in.manual = 1;
    kh2_camera_follow(&c, &in);
    CHECK(c.yaw == 0.0f);
    in.manual = 0;

    in.turn = 0.0f;
    in.zoom = 0.001f;
    c = started(0.0f);
    kh2_camera_follow(&c, &in);
    CHECK(c.yaw == 0.0f);
    in.zoom = 0.0f;

    in.move_x = 0.0f;
    in.move_y = 1.0f;
    c = started(0.0f);
    kh2_camera_follow(&c, &in);
    CHECK(c.yaw == 0.0f);

    /* the radius goes back to the start, at half a unit a tick */
    in.move_x = 1.0f;
    in.move_y = 0.0f;
    c = started(300.0f);
    kh2_camera_follow(&c, &in);
    CHECK(near_value(c.radius, 301.0f, 0.001f));
}

/* The reset: to behind the player and the start radius; at once when the player is moving. */
static void test_home(void)
{
    Kh2CameraInput in = still();
    Kh2Camera c = started(300.0f);
    in.home = 1;
    in.manual = 1;
    in.behind_yaw = 1.0f;
    kh2_camera_follow(&c, &in);
    CHECK(c.radius == kh2_camera_outdoor.start_radius);
    CHECK(c.yaw_target == 1.0f);
    CHECK(near_value(c.yaw, 20.0f * PI / 180.0f, 0.0001f)); /* 10 degrees a tick */

    c = started(300.0f);
    in.move_amount = 1.0f;
    kh2_camera_follow(&c, &in);
    CHECK(c.yaw == 1.0f);
    kh2_camera_settle(&c, 2.0f);
    CHECK(near_value(c.eye.x, c.eye_target.x, 2.0f) && near_value(c.eye.z, c.eye_target.z, 2.0f));
}

/* A 30 fps frame is one of KH2's updates: it closes 17.5 % of the eye's way and 25 % of the look-at's. */
static void test_springs(void)
{
    Kh2Camera c = started(0.0f);
    c.eye = (Kh2CameraVec){0.0f, 251.0f, 1420.0f};
    c.eye_target = c.eye_target_prev = (Kh2CameraVec){0.0f, 251.0f, 420.0f};
    c.at = (Kh2CameraVec){0.0f, 1170.0f, 0.0f};
    c.at_target = c.at_target_prev = (Kh2CameraVec){0.0f, 170.0f, 0.0f};
    kh2_camera_settle(&c, 2.0f);
    CHECK(near_value(c.eye.z, 1420.0f - 0.07f * 2.5f * 999.0f, 0.01f));
    CHECK(near_value(c.at.y, 1170.0f - 0.10f * 2.5f * 999.0f, 0.01f));
}

/* After frames that were not its own: nothing ran (a pause), a few of Days' (a new field), many (a cutscene). */
static void test_takeover(void)
{
    CHECK(kh2_camera_takeover(1, 0u) == KH2_CAMERA_RESUME);
    CHECK(kh2_camera_takeover(0, 0u) == KH2_CAMERA_SNAP);
    CHECK(kh2_camera_takeover(0, 2u) == KH2_CAMERA_SNAP);
    CHECK(kh2_camera_takeover(0, 300u) == KH2_CAMERA_BLEND);
}

/* A snap puts the eye and look-at on their targets at once, from anywhere. */
static void test_start_snap(void)
{
    Kh2CameraInput in = still();
    Kh2Camera c;
    const Kh2CameraVec at = {900.0f, 40.0f, -700.0f}, eye = {-500.0f, 800.0f, 300.0f};
    kh2_camera_start(&c, &kh2_camera_outdoor, at, eye, 0.0f, kh2_camera_half_angle(kh2_camera_outdoor.fov), 0.0f);
    c.snap = 1;
    in.manual = 1;
    kh2_camera_follow(&c, &in);
    kh2_camera_settle(&c, 2.0f);
    CHECK(near_value(c.at.y, 170.0f, 2.0f) && near_value(c.at.x, 0.0f, 2.0f));
    CHECK(near_value(c.eye.z, 420.0f, 2.0f) && near_value(c.eye.y, 251.2f, 2.0f));
}

/* At 20 fps a frame is one and a half of KH2's updates: more than one, less than two. */
static void test_springs_at_20_fps(void)
{
    Kh2Camera c = started(0.0f);
    c.eye = (Kh2CameraVec){0.0f, 251.0f, 1420.0f};
    c.eye_target = c.eye_target_prev = (Kh2CameraVec){0.0f, 251.0f, 420.0f};
    kh2_camera_settle(&c, 3.0f);
    CHECK(c.eye.z < 1420.0f - 175.0f && c.eye.z > 1420.0f - 2.0f * 175.0f);
}

/* The Select view: the indoor FOV, reached without a jump, and back. */
static void test_zoom_fov(void)
{
    const float outdoor = kh2_camera_half_angle(kh2_camera_outdoor.fov);
    const float indoor = kh2_camera_half_angle(kh2_camera_indoor.fov);
    Kh2CameraInput in = still();
    Kh2Camera c = started(0.0f);
    float last = c.fov_half;
    in.manual = 1;
    in.zoomed = 1;
    for (int i = 0; i < 60; ++i) {
        kh2_camera_follow(&c, &in);
        kh2_camera_settle(&c, 2.0f);
        CHECK(fabsf(c.fov_half - last) < 0.03f);
        CHECK(c.fov_half <= last);
        last = c.fov_half;
    }
    CHECK(near_value(c.fov_half, indoor, 0.001f));
    in.zoomed = 0;
    for (int i = 0; i < 60; ++i) {
        kh2_camera_follow(&c, &in);
        kh2_camera_settle(&c, 2.0f);
        CHECK(fabsf(c.fov_half - last) < 0.03f);
        last = c.fov_half;
    }
    CHECK(near_value(c.fov_half, outdoor, 0.001f));
}

/* Centring while locked on: the far side of the player from the look-at, at the lock-on radius. */
static void test_lock_home(void)
{
    Kh2CameraInput in = still();
    Kh2Camera c = started(420.0f);
    const Kh2CameraVec target = {0.0f, 170.0f, -100.0f};
    c.at_target = (Kh2CameraVec){100.0f, 170.0f, 0.0f};
    in.home = 1;
    kh2_camera_lock(&c, &in, target);
    CHECK(near_value(c.yaw_target, -PI * 0.5f, 0.0001f));
    CHECK(c.radius == kh2_camera_outdoor.lock_radius);
}

/* The lock-on: the look-at 30 % of the way to the target, the eye still round the head. */
static void test_lock_points(void)
{
    Kh2CameraInput in = still();
    Kh2Camera c = started(380.0f);
    const Kh2CameraVec target = {0.0f, 170.0f, -200.0f};
    kh2_camera_lock(&c, &in, target);
    CHECK(near_value(c.at_target.z, -60.0f, 0.001f));
    CHECK(near_value(c.pivot.y, 170.0f, 0.001f) && near_value(c.pivot.z, 0.0f, 0.001f));
    CHECK(near_value(c.eye_target.z, 380.0f, 0.01f));
    CHECK(c.locked);
}

/* A target far overhead: the look-at stays on the head. */
static void test_lock_steep(void)
{
    Kh2CameraInput in = still();
    Kh2Camera c = started(380.0f);
    const Kh2CameraVec target = {0.0f, 2000.0f, 0.0f};
    kh2_camera_lock(&c, &in, target);
    CHECK(near_value(c.at_target.y, 170.0f, 0.001f));
}

/* The stick works while locked on. */
static void test_lock_look(void)
{
    Kh2CameraInput in = still();
    Kh2Camera c = started(380.0f);
    const Kh2CameraVec target = {0.0f, 170.0f, -100.0f};
    in.turn = 1.0f;
    kh2_camera_lock(&c, &in, target);
    CHECK(near_value(c.yaw, 6.0f * PI / 180.0f, 0.0001f));
}

/* A target off one side: the wanted eye has it on that side of the centre, in Days' frame (y up,
 * right-handed), though KH2's sign is the other. In the end the target and the player are both on screen. */
static void test_lock_side(void)
{
    for (int side = 0; side < 2; ++side) {
        Kh2CameraInput in = still();
        Kh2Camera c = started(380.0f);
        const float x = side == 0 ? -2000.0f : 2000.0f;
        const Kh2CameraVec target = {x, 170.0f, -300.0f};
        const Kh2CameraVec body = {0.0f, 100.0f, 0.0f};
        CHECK((kh2_camera_screen_flags(&c, target) & (side == 0 ? KH2_CAMERA_LEFT : KH2_CAMERA_RIGHT)) != 0);
        kh2_camera_lock(&c, &in, target);
        CHECK(near_value(c.yaw_target, side == 0 ? -20.0f * PI / 180.0f : 20.0f * PI / 180.0f, 0.0001f));
        {
            const Kh2CameraVec eye = {380.0f * sinf(c.yaw_target), 0.0f, 380.0f * cosf(c.yaw_target)};
            const float fx = c.at_target.x - eye.x, fz = c.at_target.z - eye.z;
            const float dx = target.x - eye.x, dz = target.z - eye.z;
            const float across = fx * dz - fz * dx; /* below 0: to the left of the view */
            CHECK(side == 0 ? across < 0.0f : across > 0.0f);
        }
        for (int i = 0; i < 400; ++i) {
            kh2_camera_lock(&c, &in, target);
            kh2_camera_settle(&c, 2.0f);
        }
        CHECK(kh2_camera_screen_flags(&c, target) == 0);
        CHECK(kh2_camera_screen_flags(&c, body) == 0);
    }
}

/* A target low on screen brings the camera down, within the limit. */
static void test_lock_low(void)
{
    Kh2CameraInput in = still();
    Kh2Camera c = started(420.0f);
    const Kh2CameraVec target = {0.0f, -2000.0f, 100.0f};
    const float before = c.radius;
    CHECK((kh2_camera_screen_flags(&c, target) & KH2_CAMERA_BELOW) != 0);
    kh2_camera_lock(&c, &in, target);
    CHECK(c.radius < before);
}

int main(void)
{
    test_fov();
    test_height_follows_radius();
    test_look();
    test_chase();
    test_home();
    test_springs();
    test_takeover();
    test_start_snap();
    test_springs_at_20_fps();
    test_zoom_fov();
    test_lock_home();
    test_lock_points();
    test_lock_steep();
    test_lock_look();
    test_lock_side();
    test_lock_low();
    if (failures == 0) {
        puts("kh2 camera: ok");
    }
    return failures != 0;
}
