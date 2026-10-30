#include "preview3d.h"

#include "core/models/bones.h"
#include "imgui.h"

#include "generated/preview_ps.h"
#include "generated/preview_vs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace studio {

namespace {

constexpr int kScale = 2;              // the picture is drawn at twice its shown size
constexpr std::uint32_t kFloats = 12;  // a vertex: position 3, normal 3, uv 2, colour 4

struct Clip {
    float x, y, z, w;
};

Clip to_clip(const Mat& m, const std::array<float, 3>& p) {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14], m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15]};
}

std::array<float, 3> normalized(std::array<float, 3> v) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-8f) {
        for (auto& c : v)
            c /= l;
    }
    return v;
}

std::array<float, 3> cross(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float dot(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// What every preview shares on the one device: the pipelines (culled or not,
// opaque or blended), the samplers (per axis: clamped, repeated, mirrored),
// a white texel for pictures with no texture.
struct Shared {
    SDL_GPUDevice* device = nullptr;
    SDL_GPUTextureFormat depth = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUGraphicsPipeline* pipeline[2][2] = {};  // [cull][blend]
    SDL_GPUSampler* sampler[9] = {};
    SDL_GPUTexture* white = nullptr;
    bool tried = false, ok = false;
};
Shared g;

SDL_GPUShader* shader(SDL_GPUDevice* d, SDL_GPUShaderStage stage, const unsigned char* dxil, unsigned dxil_size,
                      const unsigned char* spirv, unsigned spirv_size, const unsigned char* msl, unsigned msl_size,
                      const char* entry, Uint32 samplers, Uint32 uniforms) {
    SDL_GPUShaderCreateInfo info{};
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(d);
    if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.code = dxil, info.code_size = dxil_size, info.format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.code = spirv, info.code_size = spirv_size, info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.code = msl, info.code_size = msl_size - 1,
        info.format = SDL_GPU_SHADERFORMAT_MSL;  // the text, no terminator
    } else {
        return nullptr;
    }
    info.entrypoint = entry;
    info.stage = stage;
    info.num_samplers = samplers;
    info.num_uniform_buffers = uniforms;
    return SDL_CreateGPUShader(d, &info);
}

bool make_shared(SDL_GPUDevice* d) {
    if (g.tried)
        return g.ok;
    g.tried = true;
    g.device = d;
    g.depth = SDL_GPUTextureSupportsFormat(d, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                           SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                  ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
                  : SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    SDL_GPUShader* vs = shader(d, SDL_GPU_SHADERSTAGE_VERTEX, preview_vs_dxil, preview_vs_dxil_size, preview_vs_spirv,
                               preview_vs_spirv_size, preview_vs_msl, preview_vs_msl_size, PREVIEW_VS_ENTRY, 0, 1);
    SDL_GPUShader* ps = shader(d, SDL_GPU_SHADERSTAGE_FRAGMENT, preview_ps_dxil, preview_ps_dxil_size, preview_ps_spirv,
                               preview_ps_spirv_size, preview_ps_msl, preview_ps_msl_size, PREVIEW_PS_ENTRY, 1, 1);
    if (vs == nullptr || ps == nullptr) {
        SDL_Log("preview: shaders not made: %s", SDL_GetError());
        if (vs != nullptr)
            SDL_ReleaseGPUShader(d, vs);
        if (ps != nullptr)
            SDL_ReleaseGPUShader(d, ps);
        return false;
    }
    SDL_GPUVertexBufferDescription buffer{};
    buffer.slot = 0;
    buffer.pitch = kFloats * sizeof(float);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[4]{};
    const SDL_GPUVertexElementFormat formats[4] = {
        SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4};
    const Uint32 offsets[4] = {0, 3 * sizeof(float), 6 * sizeof(float), 8 * sizeof(float)};
    for (Uint32 i = 0; i < 4; ++i) {
        attributes[i].location = i;
        attributes[i].buffer_slot = 0;
        attributes[i].format = formats[i];
        attributes[i].offset = offsets[i];
    }
    for (int cull = 0; cull < 2; ++cull) {
        for (int blend = 0; blend < 2; ++blend) {
            SDL_GPUColorTargetDescription target{};
            target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            if (blend) {
                target.blend_state.enable_blend = true;
                target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
                target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            }
            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader = vs;
            info.fragment_shader = ps;
            info.vertex_input_state.vertex_buffer_descriptions = &buffer;
            info.vertex_input_state.num_vertex_buffers = 1;
            info.vertex_input_state.vertex_attributes = attributes;
            info.vertex_input_state.num_vertex_attributes = 4;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode = cull ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
            info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            info.depth_stencil_state.enable_depth_test = true;
            info.depth_stencil_state.enable_depth_write = !blend;
            info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            info.target_info.color_target_descriptions = &target;
            info.target_info.num_color_targets = 1;
            info.target_info.depth_stencil_format = g.depth;
            info.target_info.has_depth_stencil_target = true;
            g.pipeline[cull][blend] = SDL_CreateGPUGraphicsPipeline(d, &info);
            if (g.pipeline[cull][blend] == nullptr)
                SDL_Log("preview: pipeline not made: %s", SDL_GetError());
        }
    }
    SDL_ReleaseGPUShader(d, vs);
    SDL_ReleaseGPUShader(d, ps);
    const SDL_GPUSamplerAddressMode modes[3] = {SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                                                SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                                SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT};
    for (int s = 0; s < 3; ++s) {
        for (int t = 0; t < 3; ++t) {
            SDL_GPUSamplerCreateInfo info{};
            info.min_filter = SDL_GPU_FILTER_NEAREST;
            info.mag_filter = SDL_GPU_FILTER_NEAREST;
            info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            info.address_mode_u = modes[s];
            info.address_mode_v = modes[t];
            info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            g.sampler[s * 3 + t] = SDL_CreateGPUSampler(d, &info);
        }
    }
    g.ok = g.pipeline[0][0] != nullptr && g.pipeline[0][1] != nullptr && g.pipeline[1][0] != nullptr &&
           g.pipeline[1][1] != nullptr &&
           std::all_of(std::begin(g.sampler), std::end(g.sampler), [](SDL_GPUSampler* s) { return s != nullptr; });
    return g.ok;
}

int sampler_of(const PreviewImage& image) {
    auto mode = [&](int axis) {
        return !image.repeat[static_cast<std::size_t>(axis)] ? 0 : image.mirror[static_cast<std::size_t>(axis)] ? 2 : 1;
    };
    return mode(0) * 3 + mode(1);
}

std::uint64_t mark_of(const PreviewImage& image) {
    // the size and a few bytes from the start and the end: enough to tell
    // another picture now at the address of one forgotten
    std::uint64_t h = 1469598103934665603ull ^ (static_cast<std::uint64_t>(image.width) << 32) ^
                      static_cast<std::uint64_t>(image.height);
    const std::size_t bytes = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4;
    for (std::size_t i = 0; i < std::min<std::size_t>(64, bytes); ++i)
        h = (h ^ image.rgba[i]) * 1099511628211ull;
    for (std::size_t i = bytes > 64 ? bytes - 64 : 0; i < bytes; ++i)
        h = (h ^ image.rgba[i]) * 1099511628211ull;
    return h;
}

}  // namespace

Preview::~Preview() {
    collect();
    for (auto& [key, image] : images_) {
        if (image.texture != nullptr)
            SDL_ReleaseGPUTexture(device_, image.texture);
    }
    if (colour_ != nullptr)
        SDL_ReleaseGPUTexture(device_, colour_);
    if (depth_ != nullptr)
        SDL_ReleaseGPUTexture(device_, depth_);
    if (vertices_ != nullptr)
        SDL_ReleaseGPUBuffer(device_, vertices_);
}

void Preview::collect() {
    for (SDL_GPUTexture* t : released_)
        SDL_ReleaseGPUTexture(device_, t);
    released_.clear();
}

bool Preview::ready() {
    if (failed_)
        return false;
    if (!make_shared(device_)) {
        failed_ = true;
        return false;
    }
    if (g.white == nullptr) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = info.height = 1;
        info.layer_count_or_depth = info.num_levels = 1;
        g.white = SDL_CreateGPUTexture(device_, &info);
        static const std::uint8_t white[4] = {255, 255, 255, 255};
        new_images_.push_back({nullptr, PreviewImage{white, 1, 1}});
    }
    return g.white != nullptr;
}

Preview::Image* Preview::image_of(const PreviewImage& image) {
    if (image.rgba == nullptr || image.width <= 0 || image.height <= 0)
        return nullptr;
    auto found = images_.find(image.rgba);
    if (found != images_.end())
        return &found->second;
    Image made;
    made.mark = mark_of(image);
    const std::size_t pixels = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    // partial as the DS's 5-bit alpha sees it: 248 and above opaque (31), below 8 none (0);
    // a texture of 254s (an exporter's "opaque") is opaque
    for (std::size_t i = 0; i < pixels && !made.partial_alpha; ++i) {
        const std::uint8_t a = image.rgba[i * 4 + 3];
        made.partial_alpha = a >= 8 && a < 248;
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(image.width);
    info.height = static_cast<Uint32>(image.height);
    info.layer_count_or_depth = info.num_levels = 1;
    made.texture = SDL_CreateGPUTexture(device_, &info);
    if (made.texture == nullptr)
        return nullptr;
    made.seen = frame_;
    new_images_.push_back({image.rgba, image});
    return &images_.emplace(image.rgba, made).first->second;
}

void Preview::begin(int width, int height, const Mat& view_projection, std::array<float, 3> light) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    vp_ = view_projection;
    light_ = normalized(light);
    batches_.clear();
    ++frame_;
    // pictures no frame has drawn for a while (another model shown): let go
    for (auto it = images_.begin(); it != images_.end();) {
        if (it->second.seen + 120 < frame_ &&
            std::none_of(new_images_.begin(), new_images_.end(), [&](const auto& n) { return n.first == it->first; })) {
            released_.push_back(it->second.texture);
            it = images_.erase(it);
        } else {
            ++it;
        }
    }
}

bool Preview::project(const std::array<float, 3>& p, float* x, float* y) const {
    const Clip c = to_clip(vp_, p);
    if (c.w <= 1e-6f)
        return false;
    *x = (c.x / c.w * 0.5f + 0.5f) * static_cast<float>(width_);
    *y = (0.5f - c.y / c.w * 0.5f) * static_cast<float>(height_);
    return true;
}

void Preview::triangle(const PreviewVertex& a, const PreviewVertex& b, const PreviewVertex& c,
                       const PreviewImage& image, bool cull, std::array<float, 3> tint, bool translucent) {
    if (!ready())
        return;
    Batch key;
    Image* img = image_of(image);
    if (img != nullptr && img->seen != frame_) {
        if (img->mark != mark_of(image)) {
            // another picture at the address of a forgotten one: made again
            released_.push_back(img->texture);
            images_.erase(image.rgba);
            img = image_of(image);
        }
        if (img != nullptr)
            img->seen = frame_;
    }
    key.image = img != nullptr ? image.rgba : nullptr;
    key.sampler = img != nullptr ? sampler_of(image) : 0;
    key.cull = cull;
    key.translucent = translucent;
    key.blend = translucent || (img != nullptr && img->partial_alpha) || a.colour[3] < 0.999f || b.colour[3] < 0.999f ||
                c.colour[3] < 0.999f;
    auto& out = batches_[key];
    for (const PreviewVertex* v : {&a, &b, &c}) {
        out.insert(out.end(), v->position.begin(), v->position.end());
        out.insert(out.end(), v->normal.begin(), v->normal.end());
        out.insert(out.end(), v->uv.begin(), v->uv.end());
        out.push_back(v->colour[0] * tint[0]);
        out.push_back(v->colour[1] * tint[1]);
        out.push_back(v->colour[2] * tint[2]);
        out.push_back(v->colour[3]);
    }
}

SDL_GPUTexture* Preview::finish() {
    if (!ready())
        return nullptr;
    const int w = width_ * kScale, h = height_ * kScale;
    if (colour_ == nullptr || target_w_ != w || target_h_ != h) {
        if (colour_ != nullptr)
            released_.push_back(colour_);
        if (depth_ != nullptr)
            released_.push_back(depth_);
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<Uint32>(w);
        info.height = static_cast<Uint32>(h);
        info.layer_count_or_depth = info.num_levels = 1;
        colour_ = SDL_CreateGPUTexture(device_, &info);
        info.format = g.depth;
        info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        depth_ = SDL_CreateGPUTexture(device_, &info);
        target_w_ = w;
        target_h_ = h;
        if (colour_ == nullptr || depth_ == nullptr)
            return nullptr;
    }
    // every batch's vertices one after another
    std::uint32_t floats = 0;
    for (const auto& [key, v] : batches_)
        floats += static_cast<std::uint32_t>(v.size());
    const std::uint32_t vertex_bytes = floats * sizeof(float);
    if (vertex_bytes > vertex_capacity_) {
        if (vertices_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, vertices_);
        SDL_GPUBufferCreateInfo info{};
        info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        info.size = std::max<std::uint32_t>(vertex_bytes * 2, 1u << 16);
        vertices_ = SDL_CreateGPUBuffer(device_, &info);
        vertex_capacity_ = vertices_ != nullptr ? info.size : 0;
        if (vertices_ == nullptr)
            return nullptr;
    }
    std::uint32_t image_bytes = 0;
    for (const auto& [rgba, image] : new_images_)
        image_bytes += static_cast<std::uint32_t>(image.width * image.height * 4);
    SDL_GPUTransferBuffer* transfer = nullptr;
    if (vertex_bytes + image_bytes > 0) {
        SDL_GPUTransferBufferCreateInfo tinfo{};
        tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tinfo.size = vertex_bytes + image_bytes;
        transfer = SDL_CreateGPUTransferBuffer(device_, &tinfo);
        if (transfer == nullptr)
            return nullptr;
        auto* map = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(device_, transfer, false));
        if (map == nullptr) {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            return nullptr;
        }
        std::uint32_t at = 0;
        for (const auto& [key, v] : batches_) {
            std::memcpy(map + at, v.data(), v.size() * sizeof(float));
            at += static_cast<std::uint32_t>(v.size() * sizeof(float));
        }
        for (const auto& [rgba, image] : new_images_) {
            const std::size_t n = static_cast<std::size_t>(image.width * image.height * 4);
            std::memcpy(map + at, image.rgba, n);
            at += static_cast<std::uint32_t>(n);
        }
        SDL_UnmapGPUTransferBuffer(device_, transfer);
    }
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        if (transfer != nullptr)
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return nullptr;
    }
    if (transfer != nullptr) {
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        if (vertex_bytes > 0) {
            SDL_GPUTransferBufferLocation src{transfer, 0};
            SDL_GPUBufferRegion dst{vertices_, 0, vertex_bytes};
            SDL_UploadToGPUBuffer(copy, &src, &dst, true);
        }
        std::uint32_t at = vertex_bytes;
        for (const auto& [rgba, image] : new_images_) {
            const auto found = images_.find(rgba);
            SDL_GPUTexture* texture = rgba == nullptr          ? g.white
                                      : found != images_.end() ? found->second.texture
                                                               : nullptr;
            if (texture == nullptr) {
                at += static_cast<std::uint32_t>(image.width * image.height * 4);
                continue;
            }
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = transfer;
            src.offset = at;
            src.pixels_per_row = static_cast<Uint32>(image.width);
            src.rows_per_layer = static_cast<Uint32>(image.height);
            SDL_GPUTextureRegion dst{};
            dst.texture = texture;
            dst.w = static_cast<Uint32>(image.width);
            dst.h = static_cast<Uint32>(image.height);
            dst.d = 1;
            SDL_UploadToGPUTexture(copy, &src, &dst, false);
            at += static_cast<std::uint32_t>(image.width * image.height * 4);
        }
        SDL_EndGPUCopyPass(copy);
        new_images_.clear();
    }
    SDL_GPUColorTargetInfo target{};
    target.texture = colour_;
    target.clear_color = {34 / 255.0f, 35 / 255.0f, 40 / 255.0f, 1.0f};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = depth_;
    depth.clear_depth = 1.0f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, &depth);
    struct {
        Mat vp;
        float light[4];
    } vu{};
    vu.vp = vp_;
    vu.light[0] = light_[0], vu.light[1] = light_[1], vu.light[2] = light_[2];
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));
    // nothing to draw (no buffer made yet): the pass only clears
    if (vertices_ == nullptr || vertex_bytes == 0) {
        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
        if (transfer != nullptr)
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return colour_;
    }
    SDL_GPUBufferBinding binding{vertices_, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    // opaque first; then the blended ones twice: their solid texels (alpha 248 and above) as opaque,
    // writing depth -- a body hides what is behind it, its own far side included -- then their soft
    // texels blended over (std::map orders by `blend` first); a translucent one's all blended
    const auto draw = [&](const Batch& key, std::uint32_t first, std::uint32_t count, bool blend, float cut_below) {
        SDL_BindGPUGraphicsPipeline(pass, g.pipeline[key.cull ? 1 : 0][blend ? 1 : 0]);
        SDL_GPUTextureSamplerBinding ts{};
        ts.texture = key.image != nullptr ? images_[key.image].texture : g.white;
        ts.sampler = g.sampler[key.sampler];
        SDL_BindGPUFragmentSamplers(pass, 0, &ts, 1);
        const float cut[4] = {cut_below, 0, 0, 0};
        SDL_PushGPUFragmentUniformData(cmd, 0, cut, sizeof(cut));
        SDL_DrawGPUPrimitives(pass, count, 1, first, 0);
    };
    for (int step = 0; step < 3; ++step) {
        std::uint32_t first = 0;
        for (const auto& [key, v] : batches_) {
            const std::uint32_t count = static_cast<std::uint32_t>(v.size() / kFloats);
            if (step == 0 && !key.blend)
                draw(key, first, count, false, 0.5f);
            if (step == 1 && key.blend && !key.translucent)
                draw(key, first, count, false, 248.0f / 255.0f);
            if (step == 2 && key.blend)
                draw(key, first, count, true, 8.0f / 255.0f);
            first += count;
        }
    }
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    if (transfer != nullptr)
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return colour_;
}

std::array<float, 3> orbit_eye_direction(float yaw, float pitch) {
    const float y = yaw * 0.0174532925f, p = pitch * 0.0174532925f;
    return {std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y)};
}

void orbit_move(std::array<float, 3>& target, float yaw, float pitch, float distance, float view_height, bool hovered) {
    if (!hovered || view_height <= 0.0f)
        return;
    // the camera's own axes, as orbit_view_projection makes them
    const auto dir = orbit_eye_direction(yaw, pitch);
    const std::array<float, 3> forward{-dir[0], -dir[1], -dir[2]};
    const auto right = normalized(cross(forward, {0, 1, 0}));
    const auto up = cross(right, forward);
    auto add = [&](const std::array<float, 3>& v, float k) {
        for (std::size_t c = 0; c < 3; ++c)
            target[c] += v[c] * k;
    };
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        // a pixel at the point's distance: the 35-degree view over its height
        const float per_pixel = 2.0f * distance * std::tan(35.0f * 0.5f * 0.0174532925f) / view_height;
        add(right, -io.MouseDelta.x * per_pixel);
        add(up, io.MouseDelta.y * per_pixel);
    }
    if (io.WantTextInput)
        return;  // typing in a field, not moving
    const float step = distance * 0.5f * io.DeltaTime * (io.KeyShift ? 4.0f : 1.0f);
    if (ImGui::IsKeyDown(ImGuiKey_W))
        add(forward, step);
    if (ImGui::IsKeyDown(ImGuiKey_S))
        add(forward, -step);
    if (ImGui::IsKeyDown(ImGuiKey_D))
        add(right, step);
    if (ImGui::IsKeyDown(ImGuiKey_A))
        add(right, -step);
    if (ImGui::IsKeyDown(ImGuiKey_E))
        add({0, 1, 0}, step);
    if (ImGui::IsKeyDown(ImGuiKey_Q))
        add({0, 1, 0}, -step);
}

Mat orbit_view_projection(std::array<float, 3> target, float yaw, float pitch, float distance, float aspect) {
    const auto dir = orbit_eye_direction(yaw, pitch);
    const std::array<float, 3> eye{target[0] + dir[0] * distance, target[1] + dir[1] * distance,
                                   target[2] + dir[2] * distance};
    // look-at (right-handed, y up)
    const auto f = normalized({target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]});
    const auto s = normalized(cross(f, {0, 1, 0}));
    const auto u = cross(s, f);
    const Mat view{s[0], u[0], -f[0], 0, s[1],         u[1],         -f[1],       0,
                   s[2], u[2], -f[2], 0, -dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    const float fovy = 35.0f * 0.0174532925f, n = std::max(1e-3f, distance * 0.01f), fz = distance * 20.0f;
    const float k = 1.0f / std::tan(fovy * 0.5f);
    const Mat proj{k / aspect, 0, 0, 0, 0, k, 0, 0, 0, 0, (fz + n) / (n - fz), -1, 0, 0, 2 * fz * n / (n - fz), 0};
    return mat_mul(proj, view);
}

}  // namespace studio
