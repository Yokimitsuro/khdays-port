// A KH2 effect's draws at a frame, as text (to check the player against KH2's own pictures, and to measure): the
// player's file (a PAX the player has), the block and emitter, the frame, an optional stop frame; a camera looking
// at the effect. Prints each draw: its picture page, blend, depth state, triangle count and first vertex. With
// --extent the camera is none (the effect's own space: world, view and projection identities) and each draw's
// bounds are printed instead: what the port's fitting of an effect to a model is measured on; with --vertices every
// corner of each draw follows its line. --survey plays every emitter of every block of the files given for that many
// frames (a repeating one stopped half way; the game's points a weapon swung round) and prints a line for each:
// played (its most objects, the triangles drawn, any not-a-number corner) or refused and why. --pages lists each
// block's pictures and writes each of its pages as a TGA named with the HD picture the port draws in its place (the
// block's own pages in order, numbered on from the last block's; a .dpd -- texcommon.dpd -- read as one bare block):
// an HD picture told apart from the wrong one.
//
//   khdays-kh2-effect-dump [--extent | --vertices] <file.pax> <block> <emitter> <frame> [stop frame]
//   khdays-kh2-effect-dump --survey <frames> <file.pax>...
//   khdays-kh2-effect-dump --pages <file.pax | file.dpd> <folder>
#include "khdays/assets/kh2_effect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
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

EffectCamera looking()
{
    const float eye[3] = {0, -150, -1000}, target[3] = {0, -150, 0};
    EffectCamera cam;
    cam.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    cam.view = look_at(eye, target);
    cam.projection = perspective(40.0F * 3.14159265F / 180.0F, 1.0F, 10.0F, 10000.0F);
    return cam;
}

int survey(int frames, int count, char **files)
{
    const EffectCamera cam = looking();
    for (int i = 0; i < count; ++i) {
        std::ifstream in(files[i], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<EffectBlock> blocks;
        try {
            blocks = read_effect(bytes.data(), bytes.size());
        } catch (const std::exception &e) {
            std::printf("%s - - unread %s\n", files[i], e.what());
            continue;
        }
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            auto block = std::make_shared<const EffectBlock>(blocks[b]);
            for (std::size_t e = 0; e < block->emitters.size(); ++e) {
                try {
                    EffectInstance fx(block, e, 3);
                    std::size_t most = 0, triangles = 0, nan = 0;
                    std::vector<EffectDraw> draws;
                    for (int f = 0; f < frames && !fx.done(); ++f) {
                        if (f == frames / 2 && block->emitters[e].repeat)
                            fx.stop();
                        // the game's points: a weapon swung round (its base, its tip), for those that read them
                        const float a = 0.1F * float(f);
                        fx.set_points({{{0, -100, 0}, {}},
                                       {{100 * std::cos(a), -100 + 100 * std::sin(a), 0}, {}}});
                        draws.clear();
                        fx.tick(cam, &draws);
                        most = std::max(most, fx.objects());
                        for (const auto &d : draws) {
                            triangles += d.triangles.size() / 3;
                            for (const auto &v : d.triangles)
                                nan += std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z) || std::isnan(v.w);
                        }
                    }
                    std::printf("%s %zu %zu played %zu %zu%s%s\n", files[i], b, e, most, triangles,
                                nan ? " nan" : "", fx.done() ? "" : " running");
                } catch (const std::exception &x) {
                    std::printf("%s %zu %zu refused %s\n", files[i], b, e, x.what());
                }
            }
        }
    }
    return 0;
}

// --pages: each block's pictures and its pages as the port numbers their HD pictures (its distinct picture pages in
// order, numbered on from the last block's: runtime/kh2_enemies/shell.cpp), each page written to `folder` as
// <block>_<tbp>_<hd number>.tga
int pages(const char *file, const std::string &folder)
{
    std::ifstream in(file, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string named = file;
    const auto blocks = named.size() > 4 && named.substr(named.size() - 4) == ".dpd"
                            ? std::vector<EffectBlock>{read_effect_block(bytes.data(), bytes.size(), 0)}
                            : read_effect(bytes.data(), bytes.size());
    int first = 0;
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        std::vector<const EffectPicture *> all;
        std::map<std::uint16_t, const EffectPicture *> by_page;
        for (const auto &pic : blocks[b].pictures) {
            all.push_back(&pic);
            by_page.emplace(pic.tbp, &pic);
            std::printf("block %zu picture tbp %04x cbp %04x psm %02x tbw %u at %u,%u size %u x %u\n", b,
                        unsigned(pic.tbp), unsigned(pic.cbp), unsigned(pic.psm), unsigned(pic.tbw), unsigned(pic.x),
                        unsigned(pic.y), unsigned(pic.w), unsigned(pic.h));
        }
        for (const auto &[tbp, pic] : by_page) {
            const bool direct = pic->psm == 0;
            // the page's view: a page (128 x 64, a direct one's 64 x 32), or its picture's when larger
            const auto log2_up = [](unsigned v) {
                std::uint64_t l = 0;
                while ((1u << l) < v)
                    ++l;
                return l;
            };
            const std::uint64_t tw = std::max<std::uint64_t>(direct ? 6 : 7, log2_up(unsigned(pic->x + pic->w))),
                                th = std::max<std::uint64_t>(direct ? 5 : 6, log2_up(unsigned(pic->y + pic->h)));
            const std::uint64_t tex0 = std::uint64_t(tbp) | std::uint64_t(pic->tbw) << 14 | std::uint64_t(pic->psm) << 20 |
                                       tw << 26 | th << 30 | std::uint64_t(pic->cbp) << 37;
            const EffectPage page = effect_page(all, tex0);
            const std::string name = folder + "/" + std::to_string(b) + "_" + std::to_string(tbp) + "_hd" +
                                     std::to_string(first) + ".tga";
            std::printf("block %zu page %04x cbp %04x %d x %d: HD -%d.dds -> %s\n", b, unsigned(tbp), unsigned(pic->cbp),
                        page.width, page.height, first, name.c_str());
            ++first;
            std::ofstream out(name, std::ios::binary);
            std::uint8_t header[18] = {};
            header[2] = 2; // uncompressed true colour
            header[12] = std::uint8_t(page.width), header[13] = std::uint8_t(page.width >> 8);
            header[14] = std::uint8_t(page.height), header[15] = std::uint8_t(page.height >> 8);
            header[16] = 32;
            header[17] = 0x28; // top-left origin, 8 alpha bits
            out.write(reinterpret_cast<const char *>(header), sizeof(header));
            for (std::size_t i = 0; i + 3 < page.rgba.size(); i += 4) {
                const char bgra[4] = {char(page.rgba[i + 2]), char(page.rgba[i + 1]), char(page.rgba[i]),
                                      char(page.rgba[i + 3])};
                out.write(bgra, 4);
            }
        }
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc > 3 && std::string(argv[1]) == "--survey")
        return survey(std::atoi(argv[2]), argc - 3, argv + 3);
    if (argc > 3 && std::string(argv[1]) == "--pages")
        return pages(argv[2], argv[3]);
    const bool extent = argc > 1 && std::string(argv[1]) == "--extent";
    if (extent) {
        --argc;
        ++argv;
    }
    const bool vertices = argc > 1 && std::string(argv[1]) == "--vertices";
    if (vertices) {
        --argc;
        ++argv;
    }
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: khdays-kh2-effect-dump [--extent | --vertices] <file.pax> <block> <emitter> <frame> [stop "
                     "frame]\n       khdays-kh2-effect-dump --survey <frames> <file.pax>...\n"
                     "       khdays-kh2-effect-dump --pages <file.pax | file.dpd> <folder>\n");
        return 2;
    }
    try {
        std::ifstream in(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto blocks = read_effect(bytes.data(), bytes.size());
        auto block = std::make_shared<const EffectBlock>(blocks.at(std::size_t(std::atoi(argv[2]))));
        EffectInstance fx(block, std::size_t(std::atoi(argv[3])), 3);
        const int frame = std::atoi(argv[4]), stop = argc > 5 ? std::atoi(argv[5]) : -1;
        EffectCamera cam = looking();
        if (extent)
            cam.view = cam.projection = cam.world;
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
            std::printf("tex %04x alpha %02x%s%s%s tris %zu first %.4f %.4f %.4f %.4f colour %.3f %.3f %.3f %.3f uv %.4f "
                        "%.4f\n",
                        unsigned(d.tex0 & 0x3fff), unsigned(d.alpha), d.depth_only ? " depth-only" : "",
                        d.depth_write ? " depth-write" : "", d.depth_test ? "" : " no-test", d.triangles.size() / 3, v.x,
                        v.y, v.z, v.w, v.r, v.g, v.b, v.a, v.u, v.v);
            if (vertices)
                for (const auto &c : d.triangles)
                    std::printf("  %.4f %.4f %.4f %.4f colour %.3f %.3f %.3f %.3f uv %.4f %.4f\n", c.x, c.y, c.z, c.w,
                                c.r, c.g, c.b, c.a, c.u, c.v);
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "kh2 effect dump: %s\n", e.what());
        return 1;
    }
    return 0;
}
