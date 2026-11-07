// A KH2 effect's draws at a frame, as text (to check the player against KH2's own pictures, and to measure): the
// player's file (a PAX the player has), the block and emitter, the frame, an optional stop frame; a camera looking
// at the effect. Prints each draw: its picture page, blend, depth state, triangle count and first vertex. With
// --extent the camera is none (the effect's own space: world, view and projection identities) and each draw's
// bounds are printed instead: what the port's fitting of an effect to a model is measured on.
//
//   khdays-kh2-effect-dump [--extent] <file.pax> <block> <emitter> <frame> [stop frame]
#include "khdays/assets/kh2_effect.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace khdays::assets::kh2;

namespace {

EffectMatrix look_at(const float eye[3], const float target[3])
{
    // KH2's y points down: the camera's up is -y (as the research player's)
    float z[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float n = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (float &v : z)
        v /= n;
    const float up[3] = {0, -1, 0};
    float x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
    n = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (float &v : x)
        v /= n;
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    EffectMatrix m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (int i = 0; i < 3; ++i) {
        m[std::size_t(i * 4 + 0)] = x[i];
        m[std::size_t(i * 4 + 1)] = y[i];
        m[std::size_t(i * 4 + 2)] = z[i];
    }
    for (int j = 0; j < 3; ++j)
        m[std::size_t(12 + j)] = -(eye[0] * m[std::size_t(j)] + eye[1] * m[std::size_t(4 + j)] + eye[2] * m[std::size_t(8 + j)]);
    return m;
}

EffectMatrix perspective(float fovy, float aspect, float near, float far)
{
    const float f = 1.0F / std::tan(fovy / 2);
    EffectMatrix m{};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = far / (far - near);
    m[11] = 1;
    m[14] = -near * far / (far - near);
    return m;
}

} // namespace

int main(int argc, char **argv)
{
    const bool extent = argc > 1 && std::string(argv[1]) == "--extent";
    if (extent) {
        --argc;
        ++argv;
    }
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: khdays-kh2-effect-dump [--extent] <file.pax> <block> <emitter> <frame> [stop frame]\n");
        return 2;
    }
    try {
        std::ifstream in(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto blocks = read_effect(bytes.data(), bytes.size());
        auto block = std::make_shared<const EffectBlock>(blocks.at(std::size_t(std::atoi(argv[2]))));
        EffectInstance fx(block, std::size_t(std::atoi(argv[3])), 3);
        const int frame = std::atoi(argv[4]), stop = argc > 5 ? std::atoi(argv[5]) : -1;
        const float eye[3] = {0, -150, -1000}, target[3] = {0, -150, 0};
        EffectCamera cam;
        cam.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        cam.view = cam.world;
        cam.projection = cam.world;
        if (!extent) {
            cam.view = look_at(eye, target);
            cam.projection = perspective(40.0F * 3.14159265F / 180.0F, 1.0F, 10.0F, 10000.0F);
        }
        std::vector<EffectDraw> draws;
        for (int f = 0; f <= frame; ++f) {
            if (f == stop)
                fx.stop();
            draws.clear();
            fx.tick(cam, f == frame ? &draws : nullptr);
        }
        std::printf("frame %d: %zu objects, %zu draws%s\n", frame, fx.objects(), draws.size(), fx.done() ? ", done" : "");
        for (const auto &d : draws) {
            if (extent) {
                float lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
                for (const auto &t : d.triangles) {
                    const float p[3] = {t.x / t.w, t.y / t.w, t.z / t.w};
                    for (int i = 0; i < 3; ++i) {
                        lo[i] = std::fmin(lo[i], p[i]);
                        hi[i] = std::fmax(hi[i], p[i]);
                    }
                }
                std::printf("tex %04x alpha %02x%s tris %zu x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f\n",
                            unsigned(d.tex0 & 0x3fff), unsigned(d.alpha), d.depth_only ? " depth-only" : "",
                            d.triangles.size() / 3, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
                continue;
            }
            const auto &v = d.triangles.front();
            std::printf("tex %04x alpha %02x%s%s%s tris %zu first %.4f %.4f %.4f %.4f colour %.3f %.3f %.3f %.3f\n",
                        unsigned(d.tex0 & 0x3fff), unsigned(d.alpha), d.depth_only ? " depth-only" : "",
                        d.depth_write ? " depth-write" : "", d.depth_test ? "" : " no-test", d.triangles.size() / 3, v.x,
                        v.y, v.z, v.w, v.r, v.g, v.b, v.a);
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "kh2 effect dump: %s\n", e.what());
        return 1;
    }
    return 0;
}
