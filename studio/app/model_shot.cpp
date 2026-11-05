// --render-model: a model as the studio's previews draw it (app/preview3d.h), from two sides, written as a PNG -- a room
// made of a collection's map, or the modder's own, looked at without the window (docs/STUDIO.md, "Rooms of the
// collections").
#include "preview3d.h"

#include "core/files/paths.h"
#include "core/models/bones.h"
#include "core/models/model_files.h"

extern "C" {
#include "render/png_io.h"
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace studio {

namespace {

namespace fs = std::filesystem;
namespace assets = khdays::assets;

// Each joint's matrix at the model's rest (its palette entry: its world times its inverse bind).
std::map<std::uint32_t, Mat> rest_of(const assets::ImportedModel& m) {
    std::map<std::uint32_t, Mat> out;
    for (const auto& j : m.joints)
        out[j.palette_index] = mat_mul(j.world, j.inverse_bind);
    return out;
}

std::array<float, 3> at_rest(const std::map<std::uint32_t, Mat>& rest, const assets::NeutralVertex& v) {
    std::array<float, 3> pos{0, 0, 0};
    float total = 0.0f;
    for (std::size_t k = 0; k < 4; ++k) {
        const auto found = rest.find(v.joints[k]);
        if (v.weights[k] <= 0.0f || found == rest.end())
            continue;
        const auto q = mat_point(found->second, v.position);
        for (std::size_t c = 0; c < 3; ++c)
            pos[c] += v.weights[k] * q[c];
        total += v.weights[k];
    }
    if (total <= 0.0f)
        return v.position;
    for (auto& c : pos)
        c /= total;
    return pos;
}

// The middle of the points and how far a camera stands to see them: their middle 96% on each axis (a far piece of
// backdrop left out of the framing).
void framing(std::vector<std::array<float, 3>> points, std::array<float, 3>* target, float* distance) {
    std::array<float, 3> lo{}, hi{};
    for (std::size_t c = 0; c < 3; ++c) {
        std::vector<float> axis;
        axis.reserve(points.size());
        for (const auto& p : points)
            axis.push_back(p[c]);
        std::sort(axis.begin(), axis.end());
        lo[c] = axis[axis.size() * 2 / 100];
        hi[c] = axis[axis.size() * 98 / 100];
    }
    *target = {(lo[0] + hi[0]) * 0.5f, lo[1], (lo[2] + hi[2]) * 0.5f};
    *distance = std::max({hi[0] - lo[0], hi[2] - lo[2], 2.0f}) * 1.1f;
}

// The picture the graphics card drew, `w` x `h`, into `rgba` at column `left` of rows `stride` pixels wide.
bool download(SDL_GPUDevice* device, SDL_GPUTexture* texture, int w, int h, std::vector<std::uint8_t>* rgba, int left,
              int stride) {
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = static_cast<Uint32>(w * h * 4);
    SDL_GPUTransferBuffer* buffer = SDL_CreateGPUTransferBuffer(device, &info);
    if (buffer == nullptr)
        return false;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion from{};
    from.texture = texture;
    from.w = static_cast<Uint32>(w);
    from.h = static_cast<Uint32>(h);
    from.d = 1;
    SDL_GPUTextureTransferInfo to{};
    to.transfer_buffer = buffer;
    SDL_DownloadFromGPUTexture(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence != nullptr) {
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);
    }
    const auto* pixels = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(device, buffer, false));
    if (pixels != nullptr) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t s = (static_cast<std::size_t>(y) * w + x) * 4;
                const std::size_t d = (static_cast<std::size_t>(y) * stride + left + x) * 4;
                std::copy(pixels + s, pixels + s + 3, rgba->begin() + static_cast<std::ptrdiff_t>(d));
                (*rgba)[d + 3] = 255;
            }
        }
        SDL_UnmapGPUTransferBuffer(device, buffer);
    }
    SDL_ReleaseGPUTransferBuffer(device, buffer);
    return pixels != nullptr;
}

}  // namespace

// --render-model <model> <png> [<collision model>]: the model from above at an angle and from the other side lower,
// side by side; its sky left out; the camera over the collision's floors when one is given, else over the model.
int render_model_command(const std::vector<std::string>& args) {
    assets::ImportedModel model, collision;
    std::string why;
    if (!import_model(path_u8(args[1]), &model, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    const bool framed_by_collision = args.size() > 3 && import_model(path_u8(args[3]), &collision, &why);
    const auto rest = rest_of(model);
    std::vector<std::array<float, 3>> points;
    for (const auto& mesh : (framed_by_collision ? collision : model).model.meshes)
        if (!mesh.sky)
            for (const auto& v : mesh.vertices)
                points.push_back(framed_by_collision ? at_rest(rest_of(collision), v) : at_rest(rest, v));
    if (points.empty()) {
        std::fprintf(stderr, "khdays-studio: %s has nothing to draw\n", args[1].c_str());
        return 1;
    }
    std::array<float, 3> target{};
    float distance = 1.0f;
    framing(std::move(points), &target, &distance);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "khdays-studio: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GPUDevice* device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (device == nullptr) {
        std::fprintf(stderr, "khdays-studio: no graphics device: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    constexpr int kWidth = 560, kHeight = 350;  // each side's (drawn at twice the size)
    struct View {
        float yaw, pitch, distance;
    };
    const View views[] = {{35.0f, 60.0f, 1.5f}, {215.0f, 30.0f, 1.0f}};
    const int w = kWidth * 2, h = kHeight * 2, stride = w * static_cast<int>(std::size(views));
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(stride) * h * 4, 0);
    bool ok = true;
    {
        Preview p(device);
        for (std::size_t n = 0; n < std::size(views) && ok; ++n) {
            const View& view = views[n];
            p.collect();
            p.begin(kWidth, kHeight,
                    orbit_view_projection(target, view.yaw, view.pitch, distance * view.distance,
                                          static_cast<float>(kWidth) / kHeight),
                    orbit_eye_direction(view.yaw, view.pitch));
            for (const auto& mesh : model.model.meshes) {
                if (mesh.sky)
                    continue;
                const assets::DecodedTexture* t = nullptr;
                for (const auto& [name, decoded] : model.textures)
                    if (name == mesh.texture_name && !decoded.rgba.empty())
                        t = &decoded;
                PreviewImage image;
                if (t != nullptr)
                    image = {t->rgba.data(), t->width, t->height};
                const float tw = t != nullptr ? static_cast<float>(t->width) : 1.0f;
                const float th = t != nullptr ? static_cast<float>(t->height) : 1.0f;
                auto vertex = [&](const assets::NeutralVertex& v) {
                    PreviewVertex o;
                    o.position = at_rest(rest, v);
                    o.normal = v.normal;
                    o.uv = {v.texcoord[0] / tw, v.texcoord[1] / th};
                    o.colour = {v.color[0] / 255.0f, v.color[1] / 255.0f, v.color[2] / 255.0f, v.color[3] / 255.0f};
                    return o;
                };
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                    p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                               vertex(mesh.vertices[mesh.indices[i + 2]]), image, !mesh.double_sided, {1, 1, 1});
            }
            SDL_GPUTexture* drawn = p.finish();
            ok = drawn != nullptr && download(device, drawn, w, h, &rgba, w * static_cast<int>(n), stride);
        }
    }
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    if (!ok) {
        std::fprintf(stderr, "khdays-studio: the picture could not be drawn\n");
        return 1;
    }
    if (const char* e = khdays_png_write(args[2].c_str(), rgba.data(), stride, h); e != nullptr) {
        std::fprintf(stderr, "khdays-studio: %s: %s\n", args[2].c_str(), e);
        return 1;
    }
    std::printf("%s: %s, from two sides (%d x %d)\n", args[2].c_str(), args[1].c_str(), stride, h);
    return 0;
}

}  // namespace studio
