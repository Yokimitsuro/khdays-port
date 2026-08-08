#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

#include "khdays/game/software_renderer.h"

namespace {

int failures = 0;

void expect(const bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// A solid red WxH RGBA image.
std::vector<std::uint8_t> red(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4U);
    for (std::size_t i = 0; i < px.size(); i += 4U) {
        px[i] = 255;
        px[i + 1] = 0;
        px[i + 2] = 0;
        px[i + 3] = 255;
    }
    return px;
}

bool is_red(const khdays::assets::DecodedTexture& t, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * t.width + x) * 4U;
    return t.rgba[i] == 255 && t.rgba[i + 1] == 0 && t.rgba[i + 2] == 0;
}

}  // namespace

int main() {
    using khdays::game::SoftwareRenderer;

    // --- Identity + translation places the source at (e, f) ---------------
    {
        SoftwareRenderer r(32, 32);
        r.clear(khdays::game::Color{0, 0, 0, 255});
        const auto img = red(4, 4);
        const std::array<float, 6> m{1, 0, 0, 1, 10, 6};  // translate to (10,6)
        r.draw_image_affine(img.data(), 4, 4, m.data());
        const auto snap = r.snapshot();
        expect(is_red(snap, 11, 7), "translated image covers (11,7)");
        expect(!is_red(snap, 2, 2), "away from the image stays clear");
        expect(!is_red(snap, 20, 20), "past the image stays clear");
    }

    // --- 2x scale doubles the footprint -----------------------------------
    {
        SoftwareRenderer r(32, 32);
        r.clear(khdays::game::Color{0, 0, 0, 255});
        const auto img = red(4, 4);  // 4x4 -> 8x8 at 2x
        const std::array<float, 6> m{2, 0, 0, 2, 0, 0};
        r.draw_image_affine(img.data(), 4, 4, m.data());
        const auto snap = r.snapshot();
        expect(is_red(snap, 0, 0) && is_red(snap, 7, 7),
               "2x scale fills an 8x8 area");
        expect(!is_red(snap, 9, 9), "2x scale does not exceed 8x8");
    }

    // --- 90-degree rotation about the origin ------------------------------
    {
        SoftwareRenderer r(32, 32);
        r.clear(khdays::game::Color{0, 0, 0, 255});
        const auto img = red(8, 2);  // wide, short
        // Rotate +90: (sx,sy) -> (-sy, sx), then translate into view by +10 x.
        const std::array<float, 6> m{0, 1, -1, 0, 10, 0};
        r.draw_image_affine(img.data(), 8, 2, m.data());
        const auto snap = r.snapshot();
        // The 8-wide axis now runs vertically at x in [8,10), tall.
        expect(is_red(snap, 9, 3), "rotated image is tall where it was wide");
        expect(!is_red(snap, 15, 1), "rotated image is no longer wide");
    }

    if (failures == 0) {
        std::cout << "all affine tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
