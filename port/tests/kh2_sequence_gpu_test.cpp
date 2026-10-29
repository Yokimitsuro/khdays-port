// The GPU sequence pass against the CPU reference, including large atlases and repeated draws.
#include "../gfx/kh2_sequence.h"
#include "../ui/kh2_hp_gauge.h"
#include "../ui/kh2_hp_config.h"
#include "../ui/heart_points.h"
#include "../host/render/kh2_sequence_asset.h"
extern "C" {
#include "../host/render/kh2_sequence_gpu.h"
#include "../host/render/gpu_hd.h"
#include "../host/render/gpu_raster3d.h"
#include "../runtime/display/frame_package.h"
#include "../runtime/mods/textures.h"
#include "../gpu/gpu3d.h"
#include "../ui/composition.h"
}
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
namespace seq = khdays::assets::kh2;
namespace render = khdays::render::kh2;
namespace gauge = khdays::ui::kh2;
std::vector<std::uint32_t> pixels[4];
KhdaysTextureReplacement images[4]{};
std::uint32_t image_versions[4]{};
SDL_GPUDevice *device;
std::string heart_settings;
unsigned heart_frame = 10;
void require(bool ok)
{
    if (!ok)
        throw std::runtime_error(SDL_GetError());
}
void set_image(int id, int w, int h, const std::vector<std::uint32_t> &p)
{
    pixels[id] = p;
    images[id] = {w, h, pixels[id].data(), 0};
    ++image_versions[id];
}
void number(KhdaysSequenceImage &p, unsigned at, float f)
{
    std::memcpy(p.words + at, &f, 4);
}
void initialize(KhdaysSequenceImage &p, int w, int h, int lw, int lh, const render::SequenceView &v)
{
    p = {};
    p.words[KSI_OUTPUT] = 0;
    p.words[KSI_ATLAS] = 1;
    p.words[KSI_LOGICAL_W] = unsigned(lw);
    p.words[KSI_LOGICAL_H] = unsigned(lh);
    p.words[KSI_WIDTH] = unsigned(w);
    p.words[KSI_HEIGHT] = unsigned(h);
    number(p, KSI_VIEW_X, v.x);
    number(p, KSI_VIEW_Y, v.y);
    number(p, KSI_SCALE, v.scale);
    p.words[KSI_FACE] = 2;
    p.words[KSI_HD_FACE] = 0xffffffffu;
    set_image(0, w, h, std::vector<std::uint32_t>(std::size_t(w * h)));
    khdays_gpu_hd_close();
    require(khdays_gpu_hd_open(device));
}
void finish(SDL_GPUCommandBuffer *cmd)
{
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    require(fence && SDL_WaitForGPUFences(device, true, &fence, 1));
    SDL_ReleaseGPUFence(device, fence);
}
void draw(const KhdaysSequenceImage &p)
{
    auto *cmd = SDL_AcquireGPUCommandBuffer(device);
    require(cmd != nullptr);
    require(khdays_gpu_hd_sync(cmd));
    require(khdays_sequence_gpu_draw(device, cmd, &p));
    finish(cmd);
}
std::vector<std::uint32_t> read(unsigned offset, unsigned bytes)
{
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = bytes;
    auto *transfer = SDL_CreateGPUTransferBuffer(device, &info);
    require(transfer != nullptr);
    auto *cmd = SDL_AcquireGPUCommandBuffer(device);
    require(cmd != nullptr);
    auto *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUBufferRegion from{khdays_gpu_hd_buffer(), offset, bytes};
    SDL_GPUTransferBufferLocation to{transfer, 0};
    SDL_DownloadFromGPUBuffer(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    finish(cmd);
    auto *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    require(mapped != nullptr);
    std::vector<std::uint32_t> out(bytes / 4);
    std::memcpy(out.data(), mapped, bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return out;
}
void compare(const KhdaysSequenceImage &p, const std::vector<seq::SequenceQuad> &quads,
             const khdays::assets::DecodedTexture &atlas, int lw, int lh, const render::SequenceView &view)
{
    const int w = int(p.words[KSI_WIDTH]), h = int(p.words[KSI_HEIGHT]);
    std::vector<std::uint32_t> expected(std::size_t(w * h));
    render::draw_sequence(quads, atlas, lw, lh, expected, w, h, view);
    draw(p);
    const unsigned first = read(0, 16)[0];
    const auto got = read(first * 4u, unsigned(w * h * 4));
    int maximum = 0;
    std::size_t bad = 0;
    std::size_t first_bad = got.size();
    for (std::size_t i = 0; i < got.size(); ++i)
        for (unsigned s = 0; s < 32; s += 8) {
            // Subpixel filtering can retain RGB beneath rounded-to-zero alpha.
            if (s < 24 && (got[i] >> 24) == 0 && (expected[i] >> 24) == 0)
                continue;
            const int error = std::abs(int((got[i] >> s) & 255) - int((expected[i] >> s) & 255));
            maximum = std::max(maximum, error);
            bad += error > 2;
            if (error > 2 && first_bad == got.size())
                first_bad = i;
        }
    if (bad) {
        std::cerr << "first mismatch " << first_bad % std::size_t(w) << ',' << first_bad / std::size_t(w) << ": "
                  << std::hex << got[first_bad] << " expected " << expected[first_bad] << std::dec << '\n';
        throw std::runtime_error("GPU/reference mismatch: " + std::to_string(bad) + " channels, max " +
                                 std::to_string(maximum));
    }
}

// Check the actual 3D destination, rather than comparing two flattened transparent images.
std::vector<std::uint32_t> scene_pixels(int resolution, bool attributes = false, int x = 16, int y = 24, int dots = 64)
{
    SDL_GPUTexture *color, *attr;
    int columns;
    require(khdays_raster3d_targets(&color, &attr, &columns));
    const unsigned side = unsigned(dots * resolution), bytes = side * side * 4;
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = bytes;
    auto *transfer = SDL_CreateGPUTransferBuffer(device, &info);
    require(transfer != nullptr);
    auto *cmd = SDL_AcquireGPUCommandBuffer(device);
    auto *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region{};
    region.texture = attributes ? attr : color;
    region.x = unsigned(x * resolution);
    region.y = unsigned(y * resolution);
    region.w = region.h = side;
    region.d = 1;
    SDL_GPUTextureTransferInfo to{};
    to.transfer_buffer = transfer;
    SDL_DownloadFromGPUTexture(copy, &region, &to);
    SDL_EndGPUCopyPass(copy);
    finish(cmd);
    auto *mapped = static_cast<const std::uint32_t *>(SDL_MapGPUTransferBuffer(device, transfer, false));
    require(mapped != nullptr);
    std::vector<std::uint32_t> out(mapped, mapped + side * side);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return out;
}

void compare_scene(const std::vector<seq::SequenceQuad> &quads, const khdays::assets::DecodedTexture &atlas,
                   int logical_w, int logical_h, int resolution, unsigned token = 0, bool occluded = false,
                   const render::SequenceView &view = {}, bool marker = false, bool wbuffer = false,
                   float clip_w = 4194304, unsigned clear_depth = 32767)
{
    auto p = std::make_unique<KhdaysFramePackage>();
    constexpr unsigned background = 0xff844a10u; // CLEAR_COLOR's 2/9/16 expanded to 8 bits.
    const unsigned clear = 2u | (9u << 5) | (16u << 10) | (31u << 16);
    std::memcpy(p->io3d + 0x50, &clear, 4);
    const unsigned depth = occluded ? 0 : clear_depth;
    std::memcpy(p->io3d + 0x54, &depth, 4);
    p->disp3dcnt = 13; // texture, alpha test and blending on; the DS cutoff must not affect SEQD.
    p->io3d[0x40] = 31;
    auto &program = p->sequence3d_images[token / 4];
    program.words[KSI_OUTPUT] = token;
    program.words[KSI_ATLAS] = 1;
    program.words[KSI_LOGICAL_W] = unsigned(logical_w);
    program.words[KSI_LOGICAL_H] = unsigned(logical_h);
    program.words[KSI_WIDTH] = program.words[KSI_HEIGHT] = 64;
    number(program, KSI_VIEW_X, view.x);
    number(program, KSI_VIEW_Y, view.y);
    number(program, KSI_SCALE, view.scale);
    render::encode_sequence_quads(program, quads);
    KhdaysG3dPolygon polygon{};
    polygon.flags = KHDAYS_G3D_MOD | KHDAYS_G3D_SEQUENCE;
    if (marker)
        polygon.flags |= KHDAYS_G3D_MARKER;
    polygon.pltt = token;
    polygon.replacement = 1;
    polygon.attr = 0xc0u | (31u << 16) | (62u << 24);
    polygon.count = 4;
    polygon.ymin = 24;
    polygon.ymax = 88;
    polygon.translucent = 1;
    KhdaysG3dVertex vertices[4]{};
    constexpr int corners[]{0, 1, 3, 2};
    for (int i = 0; i < 4; ++i) {
        auto &v = vertices[i];
        const auto c = corners[i];
        v.x = (c & 1) ? 80.0f : 16.0f;
        v.y = (c & 2) ? 88.0f : 24.0f;
        v.z = -1;
        v.w = clip_w;
        v.r = v.g = v.b = 63;
        v.a = 1;
        v.s = (c & 1) ? 128.0f : 0;
        v.t = (c & 2) ? 128.0f : 0;
    }
    p->list3d.polys = &polygon;
    p->list3d.npolys = 1;
    p->list3d.verts = vertices;
    p->list3d.nverts = 4;
    p->list3d.widen = 1;
    p->list3d.swap_params = wbuffer ? 2u : 0u;
    const int side = 64 * resolution;
    std::vector<std::uint32_t> expected(std::size_t(side * side), background);
    if (!occluded || marker)
        render::draw_sequence(quads, atlas, logical_w, logical_h, expected, side, side,
                              {view.x * resolution, view.y * resolution, view.scale * resolution});
    require(khdays_raster3d_rasterize(p.get(), resolution, 0));
    const auto got = scene_pixels(resolution);
    for (const auto pixel : got)
        if (pixel & (1u << 29))
            throw std::runtime_error("selection marker was promoted above the 2D layer");
    for (std::size_t i = 0; i < got.size(); ++i)
        for (unsigned shift = 0; shift < 24; shift += 8)
            if (std::abs(int((got[i] >> shift) & 255) - int((expected[i] >> shift) & 255)) > 2)
                throw std::runtime_error("scene blending mismatch at " + std::to_string(i % std::size_t(side)) + "," +
                                         std::to_string(i / std::size_t(side)) + " got " +
                                         std::to_string(got[i] & 0xffffffu) + " expected " +
                                         std::to_string(expected[i] & 0xffffffu));
}

void scene_blending()
{
    khdays::assets::DecodedTexture atlas;
    atlas.width = 256;
    atlas.height = 1;
    atlas.rgba.resize(1024);
    for (unsigned i = 0; i < 256; ++i) {
        atlas.rgba[i * 4] = atlas.rgba[i * 4 + 1] = atlas.rgba[i * 4 + 2] = 255;
        atlas.rgba[i * 4 + 3] = std::uint8_t(i);
    }
    std::vector<std::uint32_t> texels(256);
    std::memcpy(texels.data(), atlas.rgba.data(), 1024);
    set_image(1, 256, 1, texels);
    images[1].info = KHDAYS_REPLACEMENT_NO_MIPS;
    khdays_gpu_hd_close();
    require(khdays_raster3d_open(device));
    khdays_raster3d_set_fine(1);
    seq::SequenceQuad ramp;
    for (int i = 0; i < 4; ++i)
        ramp.vertices[std::size_t(i)] = {(i & 1) ? 64.0f : 0, (i & 2) ? 64.0f : 0, (i & 1) ? 256.0f : 0,
                                         (i & 2) ? 1.0f : 0, 0x80808080};
    for (int resolution : {1, 4, 8}) {
        for (unsigned blend : {0u, 1u, 2u}) {
            ramp.blend = blend;
            compare_scene({ramp}, atlas, 256, 1, resolution, 19);
        }
        auto add = ramp, subtract = ramp, normal = ramp;
        add.blend = 1;
        subtract.blend = 2;
        normal.blend = 0;
        for (auto &v : subtract.vertices)
            v.color = 0x80800000;
        for (auto &v : normal.vertices)
            v.color = 0x40008000;
        compare_scene({add, subtract, normal}, atlas, 256, 1, resolution);
        compare_scene({add, subtract, normal}, atlas, 256, 1, resolution, 35);
        compare_scene({normal}, atlas, 256, 1, resolution, 35, false, {}, false, true, 4096, 16);
        auto black = ramp;
        black.textured = false;
        black.blend = 1;
        for (auto &v : black.vertices)
            v.color = 0x80000000;
        compare_scene({black}, atlas, 256, 1, resolution);
        compare_scene({add}, atlas, 256, 1, resolution, 0, true);
        compare_scene({add}, atlas, 256, 1, resolution, 0, true, {}, true);
        compare_scene({add}, atlas, 256, 1, resolution, 0, true, {}, true, true);
        auto mask = ramp, clipped = add;
        mask.color_write = false;
        mask.depth_write = true;
        mask.depth = 100;
        clipped.depth_test = true;
        clipped.depth = 99;
        compare_scene({mask, clipped}, atlas, 256, 1, resolution);
    }
    khdays_raster3d_close();
    std::cout << "Scene normal/add/subtract, ordered mixtures, black glow, full alpha ramp and depth checks passed\n";
}

void native_marker_depth()
{
    set_image(1, 1, 1, {0xffffffffu});
    images[1].info = KHDAYS_REPLACEMENT_NO_MIPS;
    khdays_gpu_hd_close();
    require(khdays_raster3d_open(device));
    khdays_raster3d_set_fine(0);
    auto p = std::make_unique<KhdaysFramePackage>();
    const unsigned clear = (31u << 16), clear_depth = 32767;
    std::memcpy(p->io3d + 0x50, &clear, 4);
    std::memcpy(p->io3d + 0x54, &clear_depth, 4);
    p->disp3dcnt = 9;
    KhdaysG3dPolygon polygons[2]{};
    KhdaysG3dVertex vertices[8]{};
    constexpr int corners[]{0, 1, 3, 2};
    for (int i = 0; i < 2; ++i) {
        auto &poly = polygons[i];
        poly.first = i * 4;
        poly.count = 4;
        poly.ymin = 24;
        poly.ymax = 88;
        poly.attr = 0xc0u | (16u << 16) | (62u << 24) | 0x800u;
        poly.replacement = -1;
        poly.translucent = 1;
        for (int j = 0; j < 4; ++j) {
            auto &v = vertices[i * 4 + j];
            v.x = (corners[j] & 1) ? 80.0f : 16.0f;
            v.y = (corners[j] & 2) ? 88.0f : 24.0f;
            v.z = i ? 0.5f : -1.0f;
            v.w = i ? 4194304.0f : 4096.0f;
            v.r = i ? 0.0f : 63.0f;
            v.g = i ? 63.0f : 0.0f;
            v.a = 1;
            v.s = (corners[j] & 1) ? 128.0f : 0;
            v.t = (corners[j] & 2) ? 128.0f : 0;
        }
    }
    p->list3d.polys = polygons;
    p->list3d.npolys = 2;
    p->list3d.verts = vertices;
    p->list3d.nverts = 8;
    p->list3d.widen = 1;
    std::array<unsigned char, 0x400> io{};
    KhdaysVramPages vram{};
    std::vector<unsigned> expected(256 * 192);
    for (bool wbuffer : {false, true}) {
        p->list3d.swap_params = wbuffer ? 2u : 0u;
        for (bool marked : {false, true}) {
            polygons[1].flags = marked ? KHDAYS_G3D_MARKER : 0;
            io[0x60] = 9;
            std::memcpy(io.data() + 0x300, p->io3d, 0x100);
            khdays_gpu3d_draw(&p->list3d, io.data(), &vram, expected.data());
            require(khdays_raster3d_rasterize(p.get(), 1, 0));
            auto got = scene_pixels(1);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) {
                    const auto actual = got[std::size_t(y * 64 + x)];
                    const auto reference = expected[std::size_t((y + 24) * 256 + x + 16)];
                    require((actual & 0xffffffu) == (reference & 0xffffffu));
                    require((((actual >> 8) & 63u) != 0u) == marked);
                    require(!(actual & (1u << 29)));
                }
        }
        // The existing HD PNG fallback still goes through the native polygon's marker stage.
        polygons[1].tex = 7u << 26;
        polygons[1].replacement = 1;
        require(khdays_raster3d_rasterize(p.get(), 1, 0));
        for (const auto pixel : scene_pixels(1))
            require(((pixel >> 8) & 63u) > 0);
        for (const auto pixel : scene_pixels(1, true))
            require((pixel & 0xffffffu) == (wbuffer ? 4096u : 0u));
        polygons[1].attr = 0xc0u | (31u << 16) | (62u << 24);
        require(khdays_raster3d_rasterize(p.get(), 1, 0));
        for (const auto pixel : scene_pixels(1, true))
            require((pixel & 0xffffffu) == (wbuffer ? 4096u : 0u));
        polygons[1].attr = 0xc0u | (16u << 16) | (62u << 24) | 0x800u;
        polygons[1].tex = 0;
        polygons[1].replacement = -1;
    }
    for (unsigned role : {unsigned(KHDAYS_2D_3D_HIDDEN), unsigned(KHDAYS_2D_3D_SECOND)}) {
        for (auto &rec : p->composition) {
            rec.under = rec.other = 0x03e0;
            rec.role = static_cast<unsigned char>(role);
        }
        require(khdays_raster3d_draw(p.get(), 1, 0));
        int width, height;
        auto *composed = khdays_raster3d_read(&width, &height);
        require(composed && composed[56 * width + 48] == 0x00ff00u);
        SDL_free(composed);
    }
    khdays_raster3d_close();
    std::cout << "Native/PNG marker foreground, Z/W depth, translucent ID isolation and 2D occlusion passed\n";
}
void synthetic()
{
    khdays::assets::DecodedTexture atlas;
    atlas.width = atlas.height = 4;
    atlas.rgba.resize(64);
    for (unsigned i = 0; i < 16; ++i) {
        atlas.rgba[i * 4] = std::uint8_t(i * 13);
        atlas.rgba[i * 4 + 1] = std::uint8_t(255 - i * 11);
        atlas.rgba[i * 4 + 2] = std::uint8_t(i * 7);
        atlas.rgba[i * 4 + 3] = std::uint8_t(64 + i * 11);
    }
    std::vector<std::uint32_t> texels(16);
    std::memcpy(texels.data(), atlas.rgba.data(), 64);
    set_image(1, 4, 4, texels);
    set_image(2, 1, 1, {0});
    set_image(3, 1, 1, {0});
    seq::SequenceQuad q;
    q.vertices = {{{-16, -16, 0, 0}, {16, -16, 4, 0}, {-16, 16, 0, 4}, {16, 16, 4, 4}}};
    q.vertices[1].color = 0x80604080;
    q.vertices[2].color = 0x80408040;
    auto mask = q;
    mask.textured = false;
    mask.color_write = false;
    mask.depth_write = true;
    mask.depth = 200;
    mask.vertices[1].x = mask.vertices[3].x = 0;
    for (unsigned blend = 0; blend < 3; ++blend)
        for (bool filter : {false, true}) {
            auto foreground = q;
            foreground.depth_test = true;
            foreground.depth = 100;
            foreground.blend = blend;
            foreground.linear_filter = filter;
            std::vector<seq::SequenceQuad> quads{q, mask, foreground};
            KhdaysSequenceImage program{};
            render::SequenceView view{20, 20, 1};
            initialize(program, 40, 40, 4, 4, view);
            render::encode_sequence_quads(program, quads);
            compare(program, quads, atlas, 4, 4, view);
        }
    // An opaque face over the gauge must occlude it without a CPU copy.
    set_image(2, 1, 1, {0xff123456});
    KhdaysSequenceImage p{};
    initialize(p, 40, 40, 4, 4, {20, 20, 1});
    number(p, KSI_FACE_X, 20);
    number(p, KSI_FACE_Y, 20);
    number(p, KSI_FACE_W, 1);
    number(p, KSI_FACE_H, 1);
    render::encode_sequence_quads(p, std::span(&q, 1));
    draw(p);
    auto first = read(0, 16)[0];
    auto got = read(first * 4u, 40u * 40u * 4u);
    if (got[20 * 40 + 20] != 0xff123456)
        throw std::runtime_error("GPU gauge covered the face");
    set_image(3, 1, 1, {0x804080c0});
    p.words[KSI_HD_FACE] = 3;
    draw(p);
    got = read(first * 4u, 40u * 40u * 4u);
    const unsigned face = got[20 * 40 + 20];
    for (unsigned s = 0; s < 24; s += 8) {
        const float expected =
            float((0x804080c0u >> s) & 255) * (128.0f / 255) + float((0xff123456u >> s) & 255) * (127.0f / 255);
        if (std::abs(int((face >> s) & 255) - int(expected + 0.5f)) > 1)
            throw std::runtime_error("GPU HD face alpha mismatch");
    }
    std::cout << "GPU synthetic blending/filter/mask/face checks passed\n";
}
void large_atlas()
{
    // Synthetic high-resolution replacement: the same logical rectangle at eight times its atlas size.
    khdays::assets::DecodedTexture atlas;
    atlas.width = 4096;
    atlas.height = 2048;
    atlas.rgba.resize(std::size_t(atlas.width * atlas.height) * 4);
    std::vector<std::uint32_t> texels(std::size_t(atlas.width * atlas.height), 0xff40a080);
    std::memcpy(atlas.rgba.data(), texels.data(), atlas.rgba.size());
    set_image(1, atlas.width, atlas.height, texels);
    images[1].info = KHDAYS_REPLACEMENT_NO_MIPS;
    seq::SequenceQuad q;
    q.vertices = {{{-16, -16, 0, 0}, {16, -16, 512, 0}, {-16, 16, 0, 256}, {16, 16, 512, 256}}};
    KhdaysSequenceImage p{};
    initialize(p, 40, 40, 512, 256, {20, 20, 1});
    render::encode_sequence_quads(p, std::span(&q, 1));
    compare(p, {q}, atlas, 512, 256, {20, 20, 1});
    for (int i = 0; i < 100; ++i)
        draw(p);
    std::cout << "GPU 4096x2048 synthetic atlas checks passed\n";
}

void multiple_images()
{
    set_image(1, 1, 1, {0xffffffff});
    for (int id : {0, 2, 3})
        set_image(id, 16, 16, std::vector<std::uint32_t>(256, 0xffffffff));
    khdays_gpu_hd_close();
    require(khdays_gpu_hd_open(device));
    KhdaysSequenceImage programs[3]{};
    seq::SequenceQuad quad;
    quad.vertices = {{{0, 0, 0, 0}, {16, 0, 1, 0}, {0, 16, 0, 1}, {16, 16, 1, 1}}};
    for (unsigned i = 0; i < 3; ++i) {
        auto &p = programs[i];
        p.words[KSI_OUTPUT] = i == 0 ? 0 : i + 1;
        p.words[KSI_ATLAS] = 1;
        p.words[KSI_LOGICAL_W] = p.words[KSI_LOGICAL_H] = 1;
        p.words[KSI_WIDTH] = p.words[KSI_HEIGHT] = 16;
        p.words[KSI_CLEAR] = 1;
        number(p, KSI_SCALE, 1);
        for (auto &v : quad.vertices)
            v.color = i == 0 ? 0x80000080 : 0x80008000;
        if (i < 2)
            render::encode_sequence_quads(p, std::span(&quad, 1));
    }
    auto *cmd = SDL_AcquireGPUCommandBuffer(device);
    require(cmd && khdays_gpu_hd_sync(cmd));
    for (const auto &p : programs)
        require(khdays_sequence_gpu_draw(device, cmd, &p));
    finish(cmd);
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned target = programs[i].words[KSI_OUTPUT];
        const unsigned first = read(target * 16, 16)[0];
        const auto result = read(first * 4, 256 * 4);
        const unsigned expected = i == 0 ? 0xff0000ff : i == 1 ? 0xff00ff00 : 0;
        if (std::any_of(result.begin(), result.end(), [&](unsigned c) { return c != expected; }))
            throw std::runtime_error("multiple sequence passes mixed their commands or retained a blank frame");
    }
    std::cout << "GPU multiple-image command cycling and transparent-frame clearing passed\n";
}

void private_obtained(const std::filesystem::path &archive, const std::filesystem::path &texture)
{
    const auto a = render::load_sequence_asset(archive, "gaug", texture);
    std::vector<std::uint32_t> texels(a.texture.rgba.size() / 4);
    std::memcpy(texels.data(), a.texture.rgba.data(), a.texture.rgba.size());
    set_image(1, a.texture.width, a.texture.height, texels);
    images[1].info = KHDAYS_REPLACEMENT_NO_MIPS;
    for (int resolution : {1, 4, 8}) {
        const render::SequenceView view{80.0f * resolution, 20.0f * resolution, .65f * resolution};
        for (float frame : {0.0f, 8.0f, 16.0f, 26.0f, 27.0f, 86.0f, 87.0f, 97.0f, 106.0f}) {
            const auto evaluated = seq::evaluate_sequence(a.sequence, 76, frame);
            KhdaysSequenceImage p{};
            initialize(p, 260 * resolution, 100 * resolution, a.logical_size[0], a.logical_size[1], view);
            render::encode_sequence_quads(p, evaluated.quads);
            compare(p, evaluated.quads, a.texture, a.logical_size[0], a.logical_size[1], view);
        }
    }
    std::cout << "Obtained intro, hold and exit match GPU/reference at 1x/4x/8x\n";
}
void private_portraits(const std::filesystem::path &folder)
{
    for (const auto *name : {"P_EX110.a.us", "P_EX020.a.us"}) {
        const auto a =
            render::load_sequence_asset(folder / name, "face", "face", folder / (std::string(name) + "_face.dds"));
        std::vector<std::uint32_t> texels(a.texture.rgba.size() / 4);
        std::memcpy(texels.data(), a.texture.rgba.data(), a.texture.rgba.size());
        set_image(1, a.texture.width, a.texture.height, texels);
        images[1].info = KHDAYS_REPLACEMENT_NO_MIPS;
        for (unsigned clip = 0; clip < 4; ++clip)
            for (float frame : {0.0f, 15.0f, 29.0f, 30.0f, 59.0f}) {
                const auto evaluated = seq::evaluate_sequence(a.sequence, clip, frame);
                KhdaysSequenceImage p{};
                const render::SequenceView view{128, 128, 2};
                initialize(p, 256, 256, a.logical_size[0], a.logical_size[1], view);
                render::encode_sequence_quads(p, evaluated.quads);
                compare(p, evaluated.quads, a.texture, a.logical_size[0], a.logical_size[1], view);
            }
        std::cout << name << ": all four face states match the GPU/reference renderer\n";
    }
}
void private_assets(const char *archive, const char *texture, const gauge::HpGaugeStyle &style = {})
{
    auto asset = render::load_sequence_asset(archive, "gaug", texture);
    std::vector<std::uint32_t> texels(asset.texture.rgba.size() / 4);
    std::memcpy(texels.data(), asset.texture.rgba.data(), asset.texture.rgba.size());
    set_image(1, asset.texture.width, asset.texture.height, texels);
    images[1].info = KHDAYS_REPLACEMENT_NO_MIPS;
    for (float hp : {62.0f, 124.0f}) {
        gauge::HpGaugeSample sample;
        sample.hp = sample.max_hp = sample.previous_hp = hp;
        const auto frame = gauge::evaluate_hp_gauge(asset.sequence, sample, style);
        const render::SequenceView view{560, 256, 5.2f};
        KhdaysSequenceImage p{};
        initialize(p, 832, 512, 512, 256, view);
        render::encode_sequence_quads(p, frame.quads);
        compare(p, frame.quads, asset.texture, 512, 256, view);
    }
    for (float hp : {0.0f, 31.0f, 124.0f, 186.0f, 248.0f, 372.0f}) {
        gauge::HpGaugeSample sample;
        sample.hp = hp;
        sample.max_hp = sample.previous_hp = 372;
        sample.damage_frame = 30;
        sample.limit_hp = hp <= 186 ? 124.0f : 248.0f;
        auto frame = gauge::evaluate_hp_gauge(asset.sequence, sample, style);
        render::SequenceView view{155, 32, 0.65f};
        KhdaysSequenceImage p{};
        initialize(p, 197, 64, 512, 256, view);
        render::encode_sequence_quads(p, frame.quads);
        compare(p, frame.quads, asset.texture, 512, 256, view);
    }
    for (int scale : {1, 4, 8}) {
        gauge::HpGaugeSample sample;
        sample.hp = 248;
        sample.max_hp = sample.previous_hp = 372;
        sample.damage_frame = 30;
        sample.limit_hp = 248;
        sample.limit_marker_hp = 310;
        auto frame = gauge::evaluate_hp_gauge(asset.sequence, sample, style);
        KhdaysSequenceImage p{};
        initialize(p, 197 * scale, 64 * scale, 512, 256, {155.0f * scale, 32.0f * scale, 0.65f * scale});
        render::encode_sequence_quads(p, frame.quads);
        for (int i = 0; i < 3; ++i)
            draw(p);
        std::vector<double> timings;
        for (int i = 0; i < 100; ++i) {
            const auto start = std::chrono::steady_clock::now();
            draw(p);
            timings.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        std::sort(timings.begin(), timings.end());
        std::cout << "gaug GPU " << scale << "x, " << asset.texture.width << "x" << asset.texture.height
                  << " atlas: median " << timings[50] << " ms, p95 " << timings[95] << " ms, max " << timings.back()
                  << " ms (includes submission/wait)\n";
    }
    if (style.limit_marker >= 0)
        for (float marker : {0.0f, 62.0f, 124.0f, 185.9f, 186.0f, 186.1f, 248.0f, 372.0f}) {
            gauge::HpGaugeSample sample;
            sample.hp = 31;
            sample.max_hp = sample.previous_hp = 372;
            sample.limit_hp = 124;
            sample.limit_marker_hp = marker;
            const auto frame = gauge::evaluate_hp_gauge(asset.sequence, sample, style);
            const render::SequenceView view{560, 256, 5.2f};
            KhdaysSequenceImage p{};
            initialize(p, 832, 512, 512, 256, view);
            render::encode_sequence_quads(p, frame.quads);
            try {
                compare(p, frame.quads, asset.texture, 512, 256, view);
            } catch (const std::exception &e) {
                throw std::runtime_error("divider at " + std::to_string(marker) + " HP: " + e.what());
            }
        }
}

void private_enemy(const char *archive, const char *texture)
{
    const auto a = render::load_sequence_asset(archive, "gaug", texture);
    std::vector<std::uint32_t> texels(a.texture.rgba.size() / 4);
    std::memcpy(texels.data(), a.texture.rgba.data(), a.texture.rgba.size());
    set_image(1, a.texture.width, a.texture.height, texels);
    images[1].info = KHDAYS_REPLACEMENT_NO_MIPS;
    for (int resolution : {1, 4, 8}) {
        KhdaysSequenceImage p{};
        for (int capacity : {250, 750})
            for (int hp : {0, 1, capacity - 1, capacity, capacity + 1, capacity * 3 / 2})
                for (float damage : {0.0f, 10.0f, 60.0f, 75.0f, 91.0f, 100.0f}) {
                    const auto frame = gauge::evaluate_enemy_gauge(
                        a.sequence, {hp, capacity * 3 / 2, capacity * 3 / 2, capacity, 15, damage});
                    const render::SequenceView view{132.0f * resolution, 4.0f * resolution, 0.65f * resolution};
                    initialize(p, 135 * resolution, 14 * resolution, a.logical_size[0], a.logical_size[1], view);
                    p.words[KSI_FACE] = 0xffffffffu;
                    render::encode_sequence_quads(p, frame.quads);
                    compare(p, frame.quads, a.texture, a.logical_size[0], a.logical_size[1], view);
                }
        for (int i = 0; i < 3; ++i)
            draw(p);
        std::vector<double> timings;
        for (int i = 0; i < 100; ++i) {
            const auto start = std::chrono::steady_clock::now();
            draw(p);
            timings.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        std::sort(timings.begin(), timings.end());
        std::cout << "Enemy GPU/reference matched at " << resolution << "x; median " << timings[50] << " ms, p95 "
                  << timings[95] << " ms (includes submission/wait)\n";
    }
}

void private_hearts(const char *archive, const char *texture)
{
    const auto a = render::load_sequence_asset(archive, "hart", texture);
    std::vector<std::uint32_t> texels(a.texture.rgba.size() / 4);
    std::memcpy(texels.data(), a.texture.rgba.data(), a.texture.rgba.size());
    set_image(1, a.texture.width, a.texture.height, texels);
    require(a.sequence.clips.size() >= 41);
    for (int resolution : {1, 4}) {
        for (std::size_t clip = 0; clip < a.sequence.clips.size(); ++clip) {
            const int count = seq::sequence_frame_count(a.sequence, clip);
            for (int frame = 0; frame < count; ++frame) {
                const auto evaluated = seq::evaluate_sequence(a.sequence, clip, float(frame));
                const render::SequenceView view{64.0f * resolution, 64.0f * resolution, .65f * resolution};
                KhdaysSequenceImage program{};
                initialize(program, 280 * resolution, 160 * resolution, a.logical_size[0], a.logical_size[1], view);
                render::encode_sequence_quads(program, evaluated.quads);
                compare(program, evaluated.quads, a.texture, a.logical_size[0], a.logical_size[1], view);
            }
        }
        std::cout << "Heart counter clips matched GPU/reference at " << resolution << "x\n";
    }
}
void compare_heart_layouts(const char *original, const char *optimized, const char *texture)
{
    const auto before = render::load_sequence_asset(original, "hart", texture);
    const auto after = render::load_sequence_asset(optimized, "hart", texture);
    require(before.sequence.clips.size() == after.sequence.clips.size());
    for (std::size_t clip = 0; clip < before.sequence.clips.size(); ++clip) {
        const int frames = seq::sequence_frame_count(before.sequence, clip);
        require(frames == seq::sequence_frame_count(after.sequence, clip));
        for (int frame = 0; frame < frames; ++frame) {
            const auto a = seq::evaluate_sequence(before.sequence, clip, float(frame));
            const auto b = seq::evaluate_sequence(after.sequence, clip, float(frame));
            require(a.quads.size() == b.quads.size() && a.active.has_value() == b.active.has_value());
            for (std::size_t q = 0; q < a.quads.size(); ++q)
                for (unsigned v = 0; v < 4; ++v) {
                    const auto &x = a.quads[q].vertices[v], &y = b.quads[q].vertices[v];
                    if (x.x != y.x || x.y != y.y || x.u != y.u || x.v != y.v || x.color != y.color)
                        throw std::runtime_error("compaction changed clip " + std::to_string(clip) + " frame " +
                                                 std::to_string(frame) + " quad " + std::to_string(q) + " vertex " +
                                                 std::to_string(v) + " before " + std::to_string(x.x) + "," +
                                                 std::to_string(x.y) + "," + std::to_string(x.color) + " after " +
                                                 std::to_string(y.x) + "," + std::to_string(y.y) + "," +
                                                 std::to_string(y.color));
                }
            if (a.active) {
                require(a.active->x == b.active->x && a.active->y == b.active->y && a.active->color == b.active->color);
                require(std::abs(a.active->scale_x - b.active->scale_x) <= 1e-4f &&
                        std::abs(a.active->scale_y - b.active->scale_y) <= 1e-4f);
            }
        }
    }
    std::cout << "Compacted heart layout preserved every frame's geometry, UVs, colors, anchors and duration\n";
}
void heart_runtime(const char *settings, bool pickup_mode = false)
{
    heart_settings = settings;
    std::array<unsigned char, 0x1474> context{};
    const int total = 123;
    std::memcpy(context.data() + 0xd60, &total, 4);
    const int active = pickup_mode ? 0 : 1;
    std::memcpy(context.data() + 0xe4, &active, 4);
    if (pickup_mode) {
        const int held = 1;
        std::memcpy(context.data() + 0x48, &held, 4);
        std::memcpy(context.data() + 0xd58, &held, 4);
    }
    khdays_g3d_reset();
    khdays_g3d_set_hud(1);
    khdays_heart_begin(context.data());
    require(khdays_heart_node(context.data() + 0xe8) != 0);
    require(khdays_heart_node(context.data() + 0x8cc) != 0);
    khdays_heart_end();
    const unsigned swap = 0;
    khdays_g3d_command(0x50, &swap);
    khdays_g3d_vblank();
    for (int i = 0; i < (pickup_mode ? 14 : 2); ++i) {
        heart_frame += 3;
        khdays_heart_begin(context.data());
        khdays_heart_node(context.data() + 0xe8);
        khdays_heart_node(context.data() + 0x8cc);
        khdays_heart_end();
        khdays_g3d_command(0x50, &swap);
        khdays_g3d_vblank();
    }
    auto p = std::make_unique<KhdaysFramePackage>();
    p->list3d = *khdays_g3d_shown();
    khdays_heart_commands(&p->list3d, p->sequence3d_images + 5);
    if (p->list3d.npolys == 0)
        throw std::runtime_error("heart runtime polygon was rejected by geometry submission; pre=" +
                                 std::to_string(p->list3d.npre));
    const auto program_index = p->list3d.polys[0].pltt / 4u;
    if (p->sequence3d_images[program_index].words[KSI_COUNT] == 0)
        throw std::runtime_error("heart runtime lost its commands during frame capture");
    const unsigned clear = 31u << 16, depth = 32767;
    std::memcpy(p->io3d + 0x50, &clear, 4);
    std::memcpy(p->io3d + 0x54, &depth, 4);
    p->disp3dcnt = 9;
    khdays_raster3d_close();
    require(khdays_raster3d_open(device));
    KhdaysShape3d shape{};
    shape.group = KHDAYS_SHAPE3D_HUD;
    shape.x = shape.y = 20;
    shape.scale = 1;
    shape.src_w = 108;
    shape.src_h = 86;
    khdays_raster3d_set_one_screen(1);
    khdays_raster3d_set_hud_shapes(&shape, 1);
    const auto config = gauge::load_heart_points_config(settings);
    khdays_raster3d_set_fine(1);
    const auto folder = std::filesystem::path(settings).parent_path();
    const auto asset = render::load_sequence_asset(folder / config.archive, config.entry, folder / config.texture);
    const auto &program = p->sequence3d_images[program_index];
    if (pickup_mode &&
        (program.words[KSI_COUNT] != 11 || (program.words[KSI_HEADER + 4 * KSI_QUAD_WORDS + 4] >> 24) == 0))
        throw std::runtime_error("native count-up lost the cycling pickup light after its first loop");
    std::vector<seq::SequenceQuad> quads(program.words[KSI_COUNT]);
    for (std::size_t q = 0; q < quads.size(); ++q) {
        const auto *words = program.words + KSI_HEADER + q * KSI_QUAD_WORDS;
        for (unsigned v = 0; v < 4; ++v)
            std::memcpy(&quads[q].vertices[v], words + v * 5, 20);
        quads[q].linear_filter = (words[KSI_FLAGS] & 16) != 0;
        quads[q].blend = words[KSI_BLEND];
    }
    for (int resolution : {1, 4}) {
        for (unsigned buffering : {0u, 2u}) {
            p->list3d.swap_params = buffering;
            require(khdays_raster3d_rasterize(p.get(), resolution, 0));
            SDL_WaitForGPUIdle(device);
            const auto got = scene_pixels(resolution, false, 0, 0, 128);
            std::vector<unsigned> expected(std::size_t(128 * resolution * 128 * resolution), 0xff000000);
            render::draw_sequence(
                quads, asset.texture, asset.logical_size[0], asset.logical_size[1], expected, 128 * resolution,
                128 * resolution,
                {(20 + config.offset_x) * resolution, (20 + config.offset_y) * resolution, config.scale * resolution});
            for (std::size_t i = 0; i < got.size(); ++i)
                for (unsigned shift = 0; shift < 24; shift += 8)
                    if (std::abs(int((got[i] >> shift) & 255) - int((expected[i] >> shift) & 255)) > 2)
                        throw std::runtime_error(
                            "heart runtime pixel mismatch at " + std::to_string(i % (128 * resolution)) + "," +
                            std::to_string(i / (128 * resolution)) + " got " + std::to_string(got[i] & 0xffffffu) +
                            " expected " + std::to_string(expected[i] & 0xffffffu));
            const auto visible = std::count_if(got.begin(), got.end(), [](unsigned c) { return (c & 0xffffffu) != 0; });
            if (visible < 10)
                throw std::runtime_error("heart runtime submitted polygons but rendered no counter pixels");
            std::cout << "Heart runtime rendered " << visible << " pixels at " << resolution << "x, "
                      << (buffering ? "W" : "Z") << " buffer\n";
        }
    }
    khdays_raster3d_set_hud_shapes(nullptr, 0);
    khdays_raster3d_set_one_screen(0);
}
void private_reticles(const char *archive, const char *texture)
{
    const auto a = render::load_sequence_asset(archive, "fiel", texture);
    std::vector<std::uint32_t> texels(a.texture.rgba.size() / 4);
    std::memcpy(texels.data(), a.texture.rgba.data(), a.texture.rgba.size());
    set_image(1, a.texture.width, a.texture.height, texels);
    images[1].info = KHDAYS_REPLACEMENT_NO_MIPS;
    require(khdays_raster3d_open(device));
    khdays_raster3d_set_fine(1);
    for (int resolution : {1, 4, 8}) {
        KhdaysSequenceImage p{};
        for (int clip : {4, 5, 26, 27, 49, 50}) {
            const int end = seq::sequence_frame_count(a.sequence, std::size_t(clip));
            seq::SequencePlayer player;
            player.select(a.sequence, std::size_t(clip));
            for (int t : {0, 1, 5, 9, 10, 20, 39, 59, 70, 100, 189}) {
                if (t >= end)
                    continue;
                const auto frame = seq::evaluate_sequence(a.sequence, std::size_t(clip), float(t));
                const render::SequenceView view{52.0f * resolution, 52.0f * resolution, 0.65f * resolution};
                initialize(p, 104 * resolution, 104 * resolution, a.logical_size[0], a.logical_size[1], view);
                render::encode_sequence_quads(p, frame.quads);
                compare(p, frame.quads, a.texture, a.logical_size[0], a.logical_size[1], view);
            }
            if (clip == 5 || clip == 27 || clip == 50) {
                player.advance(a.sequence, float(end));
                require(player.playing() && player.frame() == float(a.sequence.clips[std::size_t(clip)].restart));
            }
            const auto scene = seq::evaluate_sequence(a.sequence, std::size_t(clip), float(std::min(end / 2, 32)));
            compare_scene(scene.quads, a.texture, a.logical_size[0], a.logical_size[1], resolution, 0, false,
                          {32, 32, 0.65f});
        }
        std::cout << "Reticle intros, loops and exits matched GPU/reference at " << resolution << "x\n";
    }
    khdays_raster3d_close();
}
void private_information(const char *archive, const char *texture)
{
    const auto a = render::load_sequence_asset(archive, "gaug", texture);
    set_image(1, a.texture.width, a.texture.height, [&] {
        std::vector<std::uint32_t> p(a.texture.rgba.size() / 4);
        std::memcpy(p.data(), a.texture.rgba.data(), a.texture.rgba.size());
        return p;
    }());
    for (int resolution : {1, 4, 8})
        for (int clip : {0, 1, 2, 3})
            for (float frame : {0.0f, 10.0f, 80.0f}) {
                if (frame >= seq::sequence_frame_count(a.sequence, std::size_t(clip)))
                    continue;
                const auto f = seq::evaluate_sequence(a.sequence, std::size_t(clip), frame);
                const render::SequenceView view{80.0f * resolution, 0, .65f * resolution};
                KhdaysSequenceImage p;
                initialize(p, 340 * resolution, 60 * resolution, a.logical_size[0], a.logical_size[1], view);
                render::encode_sequence_quads(p, f.quads);
                compare(p, f.quads, a.texture, a.logical_size[0], a.logical_size[1], view);
            }
    for (int resolution : {1, 4, 8}) {
        const int tw = 400 * resolution, th = 16 * resolution;
        std::vector<std::uint32_t> glyphs(std::size_t(tw) * th);
        for (int y = 3 * resolution; y < 11 * resolution; ++y)
            for (int x = 8 * resolution; x < tw; ++x)
                glyphs[std::size_t(y) * tw + x] = 0xffff00ffu;
        set_image(2, tw, th, glyphs);
        for (float frame : {0.0f, 120.0f}) {
            const auto window = seq::evaluate_sequence(a.sequence, 1, frame);
            const auto mask = seq::evaluate_sequence(a.sequence, 2, frame);
            auto quads = window.quads;
            quads.insert(quads.end(), mask.quads.begin(), mask.quads.end());
            const render::SequenceView view{80.0f * resolution, 0, .65f * resolution};
            KhdaysSequenceImage p;
            initialize(p, 340 * resolution, 60 * resolution, a.logical_size[0], a.logical_size[1], view);
            render::encode_sequence_quads(p, quads);
            const auto active = window.active.value();
            p.words[KSI_TEXT_INSERT] = unsigned(window.quads.size()) + 1;
            number(p, KSI_TEXT_SCALE, 1);
            number(p, KSI_FACE_X, view.x + ((active.x + 12) * .65f - 8) * resolution);
            number(p, KSI_FACE_Y, ((active.y - 12) * .65f - 3) * resolution);
            number(p, KSI_FACE_W, float(tw));
            number(p, KSI_FACE_H, float(th));
            number(p, KSI_TEXT_CLIP_X, view.x);
            number(p, KSI_TEXT_CLIP_Y, 0);
            number(p, KSI_TEXT_CLIP_W, 360 * .65f * resolution);
            number(p, KSI_TEXT_CLIP_H, 64 * .65f * resolution);
            number(p, KSI_TEXT_ALPHA, float(active.color >> 24) / 128);
            draw(p);
            const unsigned first = read(0, 16)[0];
            const auto got = read(first * 4, unsigned(340 * resolution * 60 * resolution * 4));
            const auto visible = std::count_if(got.begin(), got.end(), [](unsigned pixel) {
                return (pixel & 255u) > 220 && ((pixel >> 8) & 255u) < 30 && ((pixel >> 16) & 255u) > 220;
            });
            if ((frame == 0 && visible != 0) || (frame == 120 && visible < 100 * resolution * resolution))
                throw std::runtime_error("real information mask did not hide, then reveal, the glyph plane");
            if (frame == 120) {
                int right_visible = 0;
                for (int y = 0; y < 60 * resolution; ++y)
                    for (int x = 220 * resolution; x < 295 * resolution; ++x) {
                        const unsigned pixel = got[std::size_t(y) * 340 * resolution + x];
                        right_visible +=
                            (pixel & 255u) > 220 && ((pixel >> 8) & 255u) < 30 && ((pixel >> 16) & 255u) > 220;
                    }
                if (right_visible < 100 * resolution * resolution)
                    throw std::runtime_error("original PC information text was clipped in the middle of the box");
            }
        }
    }
    std::cout << "Original gaug information clips and combined text/reveal matched at 1x/4x/8x\n";
}
void information_text()
{
    // .65 at 4x puts x=7.5 exactly on pixel center 339.5; adjacent faded quads must not overlap.
    for (bool mirrored : {false, true}) {
        khdays::assets::DecodedTexture a;
        a.width = a.height = 1;
        a.rgba = {255, 255, 255, 255};
        set_image(1, 1, 1, {0xffffffffu});
        KhdaysSequenceImage p;
        const render::SequenceView view{320, 0, 2.6f};
        initialize(p, 400, 160, 1, 1, view);
        std::vector<seq::SequenceQuad> quads;
        for (const auto span : {std::array{-97.5f, 7.5f}, std::array{7.5f, 20.0f}}) {
            seq::SequenceQuad q;
            const float l = span[mirrored ? 1 : 0], r = span[mirrored ? 0 : 1];
            q.vertices = {{{l, 15.5f, .5f, .5f, 0x40808080},
                           {r, 15.5f, .5f, .5f, 0x40808080},
                           {l, 58.5f, .5f, .5f, 0x40808080},
                           {r, 58.5f, .5f, .5f, 0x40808080}}};
            quads.push_back(q);
        }
        render::encode_sequence_quads(p, quads);
        compare(p, quads, a, 1, 1, view);
    }
    for (int scale : {1, 4, 8}) {
        KhdaysSequenceImage p{};
        set_image(1, 1, 1, {0xffffffffu});
        set_image(2, 4 * scale, 4 * scale, std::vector<std::uint32_t>(std::size_t(16 * scale * scale), 0xffffffffu));
        initialize(p, 16 * scale, 16 * scale, 1, 1, {0, 0, float(scale)});
        const auto quad = [](float left, float right, unsigned color) {
            seq::SequenceQuad q;
            q.vertices = {{{left, 0, 0.5f, 0.5f, color},
                           {right, 0, 0.5f, 0.5f, color},
                           {left, 16, 0.5f, 0.5f, color},
                           {right, 16, 0.5f, 0.5f, color}}};
            return q;
        };
        const std::array quads{quad(0, 16, 0x80000080u), quad(8, 16, 0x80000000u)};
        render::encode_sequence_quads(p, quads);
        p.words[KSI_TEXT_INSERT] = 2;
        number(p, KSI_TEXT_SCALE, 2);
        number(p, KSI_FACE_X, float(2 * scale));
        number(p, KSI_FACE_Y, float(2 * scale));
        number(p, KSI_FACE_W, float(8 * scale));
        number(p, KSI_FACE_H, float(8 * scale));
        number(p, KSI_TEXT_CLIP_X, 0);
        number(p, KSI_TEXT_CLIP_Y, 0);
        number(p, KSI_TEXT_CLIP_W, float(7 * scale));
        number(p, KSI_TEXT_CLIP_H, float(16 * scale));
        number(p, KSI_TEXT_ALPHA, 1);
        draw(p);
        const unsigned first = read(0, 16)[0];
        const auto got = read(first * 4, unsigned(16 * scale * 16 * scale * 4));
        const auto pixel = [&](int x, int y) { return got[std::size_t(y * scale * 16 * scale + x * scale)]; };
        require(pixel(3, 3) == 0xffffffffu); // Native or HD glyph plane above the background.
        require(pixel(7, 3) == 0xff0000ffu); // Text clipped independently of the sequence artwork.
        require(pixel(9, 3) == 0xff000000u); // Reveal mask remains above the glyphs.
        number(p, KSI_TEXT_ALPHA, 0);
        draw(p);
        const auto hidden = read(first * 4, unsigned(16 * scale * 16 * scale * 4));
        require(hidden[std::size_t(3 * scale * 16 * scale + 3 * scale)] == 0xff0000ffu);
        // An exit can keep the old glyph bounds while its texture is intentionally absent.
        p.words[KSI_FACE] = ~0u;
        number(p, KSI_TEXT_ALPHA, 1);
        draw(p);
        require(read(first * 4, unsigned(16 * scale * 16 * scale * 4)) == hidden);
    }
    std::cout << "Information text insertion, scale, clipping and alpha passed at 1x/4x/8x\n";
}
void radar_mask()
{
    set_image(1, 1, 1, {0xff00ff00u});
    set_image(2, 1, 1, {0xffffffffu});
    set_image(3, 64, 16, std::vector<std::uint32_t>(64 * 16, 0));
    KhdaysSequenceImage p;
    initialize(p, 192, 192, 1, 1, {0, 0, 2});
    p.words[KSI_FACE] = ~0u;
    p.words[KSI_RADAR_MASK] = 4;
    p.words[KSI_RADAR_ICONS] = 2;
    p.words[KSI_RADAR_ICON_W] = p.words[KSI_RADAR_ICON_H] = 1;
    number(p, KSI_RADAR_ALPHA, .75f);
    std::array<seq::SequenceQuad, 2> quads;
    for (unsigned q = 0; q < 2; ++q)
        for (unsigned v = 0; v < 4; ++v) {
            float start = q ? 40.f : 0.f, side = q ? 16.f : 96.f;
            quads[q].vertices[v] = {start + (v & 1 ? side : 0), start + (v & 2 ? side : 0), float(v & 1 ? 1 : 0),
                                    float(v & 2 ? 1 : 0), 0x80808080u};
        }
    render::encode_sequence_quads(p, quads);
    draw(p);
    unsigned first = read(0, 16)[0];
    auto output = read(first * 4, 192 * 192 * 4);
    require(output[48 * 192 + 48] == 0xbf00ff00u);
    require(output[96 * 192 + 96] == 0xbfffffffu);
    require((output[4 * 192 + 4] >> 24) == 0);
    number(p, KSI_RADAR_ALPHA, .5f);
    set_image(3, 64, 16, std::vector<std::uint32_t>(64 * 16, 0xffffffffu));
    draw(p);
    output = read(first * 4, 192 * 192 * 4);
    require(output[4 * 192 + 4] == 0x8000ff00u);
    require(output[96 * 192 + 96] == 0x80ffffffu);
    // A radar near a map boundary must extend its edge colours, without wrapping room artwork.
    set_image(1, 2, 2, {0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xff00ffffu});
    p.words[KSI_LOGICAL_W] = p.words[KSI_LOGICAL_H] = 2;
    p.words[KSI_COUNT] = 1;
    for (unsigned v = 0; v < 4; ++v) {
        quads[0].vertices[v].u = v & 1 ? 3.f : -1.f;
        quads[0].vertices[v].v = v & 2 ? 3.f : -1.f;
    }
    quads[0].linear_filter = false;
    render::encode_sequence_quads(p, std::span(quads).first(1));
    khdays_gpu_hd_close();
    require(khdays_gpu_hd_open(device));
    draw(p);
    first = read(0, 16)[0];
    output = read(first * 4, 192 * 192 * 4);
    require(output[4 * 192 + 4] == 0x800000ffu);
    require(output[4 * 192 + 188] == 0x8000ff00u);
    require(output[188 * 192 + 4] == 0x80ff0000u);
    require(output[188 * 192 + 188] == 0x8000ffffu);
    std::cout << "Native radar terrain, independent icon colours, center opacity and textured edge mask passed\n";
}
} // namespace
extern "C" {
int khdays_mods_find(const char *, char *out, int capacity)
{
    std::snprintf(out, std::size_t(capacity), "%s", heart_settings.c_str());
    return !heart_settings.empty();
}
int khdays_settings_game_language(void)
{
    return 1;
}
const char *khdays_language_code(int)
{
    return "en";
}
void khdays_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
    va_end(args);
}
int khdays_textures_hd(void)
{
    return 1;
}
int khdays_host_frame_wants(void)
{
    return 1;
}
unsigned khdays_link_scene(unsigned *)
{
    return 2;
}
unsigned khdays_link_frame(void)
{
    return heart_frame;
}
int khdays_link_paused(void)
{
    return 0;
}
int khdays_textures_add_info(const char *, int w, int h, const unsigned *rgba, unsigned info)
{
    set_image(1, w, h, std::vector<unsigned>(rgba, rgba + w * h));
    images[1].info = info;
    return 1;
}
int khdays_textures_replacements(void)
{
    return 4;
}
const KhdaysTextureReplacement *khdays_textures_replacement(int i)
{
    return i >= 0 && i < 4 ? &images[i] : nullptr;
}
uint32_t khdays_textures_version(int i)
{
    return i == 1 ? 0 : image_versions[i];
}
void khdays_textures_read_begin(int) {}
void khdays_textures_read_end(int) {}
}
int main(int argc, char **argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 77;
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL,
                                 true, nullptr);
    if (!device) {
        std::cerr << SDL_GetError() << '\n';
        SDL_Quit();
        return 77;
    }
    int result = 0;
    try {
        require(khdays_gpu_hd_open(device));
        synthetic();
        large_atlas();
        multiple_images();
        information_text();
        radar_mask();
        scene_blending();
        native_marker_depth();
        if (argc == 5 && std::string(argv[1]) == "--heart-compare")
            compare_heart_layouts(argv[2], argv[3], argv[4]);
        else if (argc == 3 && std::string(argv[1]) == "--heart-pickup-runtime")
            heart_runtime(argv[2], true);
        else if (argc == 3 && std::string(argv[1]) == "--heart-runtime")
            heart_runtime(argv[2]);
        else if (argc == 4 && std::string(argv[1]) == "--hearts")
            private_hearts(argv[2], argv[3]);
        else if (argc == 4 && std::string(argv[1]) == "--information")
            private_information(argv[2], argv[3]);
        else if (argc == 4 && std::string(argv[1]) == "--obtained")
            private_obtained(argv[2], argv[3]);
        else if (argc == 4 && std::string(argv[1]) == "--reticles")
            private_reticles(argv[2], argv[3]);
        else if (argc == 4 && std::string(argv[1]) == "--enemy")
            private_enemy(argv[2], argv[3]);
        else if (argc == 3 && std::string(argv[1]) == "--portraits")
            private_portraits(argv[2]);
        else if (argc >= 3)
            private_assets(argv[1], argv[2],
                           argc >= 4
                               ? gauge::load_hp_gauge_configs(argv[3]).at(argc >= 5 ? std::stoul(argv[4]) : 0).style
                               : gauge::HpGaugeStyle{});
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    khdays_raster3d_close();
    khdays_sequence_gpu_close();
    khdays_gpu_hd_close();
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return result;
}
