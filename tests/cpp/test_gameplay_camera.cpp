#include <cmath>
#include <cstdlib>
#include <iostream>

#include "khdays/game/gameplay_camera.h"

namespace {

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(const float a, const float b, const float epsilon = 0.0001F) {
    return std::fabs(a - b) <= epsilon;
}

}  // namespace

int main() {
    khdays::game::GameplayCamera camera;
    camera.reset({0.0F, 0.0F, 0.0F});
    expect(camera.yaw() == 0x8000U && camera.target_yaw() == 0x8000U,
           "reset retains the original binary-angle unit");
    expect(camera.distance_fx() == 0x3000
               && camera.target_distance_fx() == 0x5c00,
           "reset and selector-zero distances match ov002");
    expect(camera.eye_height_fx() == 0x1a00,
           "selector-zero eye height matches the camera table");
    expect(near(camera.camera().fov_y, 3.14159265358979323846F / 3.0F)
               && near(camera.camera().near_z, 1.0F)
               && near(camera.camera().far_z, 1000.0F),
           "projection defaults match func_02023c60");

    khdays::game::Input right;
    right.down = static_cast<std::uint16_t>(khdays::game::Button::R);
    camera.update(right, {0.0F, 0.0F, 0.0F});
    expect(camera.target_yaw() == 0x7d00U,
           "DS R subtracts the exact 0x300 camera step");
    expect(camera.yaw() == 0x7e80U,
           "ordinary yaw settling uses the Q12 0x800 rate");

    camera.reset({0.0F, 0.0F, 0.0F});
    khdays::game::Input left;
    left.down = static_cast<std::uint16_t>(khdays::game::Button::L);
    camera.update(left, {0.0F, 0.0F, 0.0F});
    expect(camera.target_yaw() == 0x8300U,
           "DS L adds the exact 0x300 camera step");
    expect(camera.distance_fx() == 0x3b00,
           "distance opens by one quarter of the remaining delta");

    khdays::assets::CollisionModel collision;
    collision.valid = true;
    khdays::assets::CollisionFace wall;
    wall.vertex_count = 4U;
    wall.plane.z = -0x1000;
    wall.plane.distance = -0x1000;
    wall.vertices = {{
        {-0x4000, 0, 0x1000}, {0x4000, 0, 0x1000},
        {0x4000, 0x4000, 0x1000}, {-0x4000, 0x4000, 0x1000}}};
    collision.faces.push_back(wall);
    const auto resolved = khdays::game::resolve_gameplay_camera_collision(
        collision, {0.0F, 1.0F, 0.0F}, {0.0F, 2.0F, 4.0F});
    expect(resolved[2] > 0.72F && resolved[2] < 0.74F,
           "camera applies sphere contact then ov002's 0xc00 near-focus push");

    std::cout << "gameplay camera tests passed\n";
    return EXIT_SUCCESS;
}
