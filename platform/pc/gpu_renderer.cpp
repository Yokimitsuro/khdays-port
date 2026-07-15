#include "khdays/platform/gpu_renderer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "khdays/assets/mesh.h"
#include "khdays/assets/tex0.h"

namespace {

// Embedded SPIR-V produced from shaders/model.{vert,frag} by glslc. Regenerate
// with tools/build_shaders (see that script) after editing the GLSL.
const std::uint32_t kModelVertSpirv[] =
#include "generated/model.vert.spv.inc"
    ;
const std::uint32_t kModelFragSpirv[] =
#include "generated/model.frag.spv.inc"
    ;

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;

struct Vertex final {
    float px = 0.0F, py = 0.0F, pz = 0.0F;
    float u = 0.0F, v = 0.0F;
    float r = 1.0F, g = 1.0F, b = 1.0F, a = 1.0F;
    float nx = 0.0F, ny = 0.0F, nz = 0.0F;
};

struct Uniforms final {
    float mvp[16];
    float model[16];
    float light[4];
};

// ----- Column-major 4x4 matrix helpers (p' = M * p) -----

using Mat = std::array<float, 16>;

Mat identity() {
    return Mat{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Mat multiply(const Mat& a, const Mat& b) {
    Mat r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k) {
                sum += a[static_cast<std::size_t>(k * 4 + row)]
                    * b[static_cast<std::size_t>(col * 4 + k)];
            }
            r[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return r;
}

Mat translation(const float x, const float y, const float z) {
    Mat m = identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}

Mat rotation_y(const float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat m = identity();
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
    return m;
}

// Right-handed perspective with a [0, 1] depth range (SDL GPU / D3D style).
Mat perspective(
    const float fov_y,
    const float aspect,
    const float near_z,
    const float far_z) {
    const float f = 1.0F / std::tan(fov_y * 0.5F);
    Mat m{};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = far_z / (near_z - far_z);
    m[11] = -1.0F;
    m[14] = (far_z * near_z) / (near_z - far_z);
    return m;
}

void log_sdl(const char* what) {
    std::cerr << what << " failed: " << SDL_GetError() << '\n';
}

// Build an interleaved vertex/index buffer from the decoded model, using
// rest-pose positions and smooth per-vertex normals.
// A contiguous run of indices sharing one texture (one decoded mesh).
struct SubMesh final {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    std::string texture_name;
};

struct MeshData final {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SubMesh> submeshes;
    std::array<float, 3> center{0, 0, 0};
    float radius = 1.0F;
};

MeshData build_mesh(
    const khdays::assets::NeutralModel& model,
    const std::map<std::string, khdays::assets::DecodedTexture>& textures) {
    MeshData data;
    std::array<float, 3> lo{1e30F, 1e30F, 1e30F};
    std::array<float, 3> hi{-1e30F, -1e30F, -1e30F};

    for (const auto& mesh : model.meshes) {
        const auto base = static_cast<std::uint32_t>(data.vertices.size());
        const auto first_index = static_cast<std::uint32_t>(data.indices.size());

        // Normalize texel coordinates by the real texture size.
        float tex_w = 1.0F;
        float tex_h = 1.0F;
        const auto tex_it = textures.find(mesh.texture_name);
        if (tex_it != textures.end()) {
            if (tex_it->second.width > 0) {
                tex_w = static_cast<float>(tex_it->second.width);
            }
            if (tex_it->second.height > 0) {
                tex_h = static_cast<float>(tex_it->second.height);
            }
        }

        for (const auto& v : mesh.vertices) {
            const auto p = khdays::assets::posed_position(model, v);
            Vertex vertex;
            vertex.px = p[0];
            vertex.py = p[1];
            vertex.pz = p[2];
            vertex.u = v.texcoord[0] / tex_w;
            vertex.v = v.texcoord[1] / tex_h;
            vertex.r = static_cast<float>(v.color[0]) / 255.0F;
            vertex.g = static_cast<float>(v.color[1]) / 255.0F;
            vertex.b = static_cast<float>(v.color[2]) / 255.0F;
            vertex.a = static_cast<float>(v.color[3]) / 255.0F;
            data.vertices.push_back(vertex);
            for (int i = 0; i < 3; ++i) {
                lo[static_cast<std::size_t>(i)] =
                    std::min(lo[static_cast<std::size_t>(i)], p[static_cast<std::size_t>(i)]);
                hi[static_cast<std::size_t>(i)] =
                    std::max(hi[static_cast<std::size_t>(i)], p[static_cast<std::size_t>(i)]);
            }
        }
        for (const auto index : mesh.indices) {
            data.indices.push_back(base + index);
        }
        data.submeshes.push_back(SubMesh{
            first_index,
            static_cast<std::uint32_t>(mesh.indices.size()),
            mesh.texture_name});
    }

    // Smooth normals: accumulate face normals into each vertex, then normalize.
    for (std::size_t i = 0U; i + 2U < data.indices.size(); i += 3U) {
        const auto ia = data.indices[i];
        const auto ib = data.indices[i + 1U];
        const auto ic = data.indices[i + 2U];
        auto& a = data.vertices[ia];
        auto& b = data.vertices[ib];
        auto& c = data.vertices[ic];
        const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
        const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
        const float nx = uy * vz - uz * vy;
        const float ny = uz * vx - ux * vz;
        const float nz = ux * vy - uy * vx;
        for (auto* vert : {&a, &b, &c}) {
            vert->nx += nx;
            vert->ny += ny;
            vert->nz += nz;
        }
    }
    for (auto& vert : data.vertices) {
        const float len =
            std::sqrt(vert.nx * vert.nx + vert.ny * vert.ny + vert.nz * vert.nz);
        if (len > 1e-8F) {
            vert.nx /= len;
            vert.ny /= len;
            vert.nz /= len;
        }
    }

    for (int i = 0; i < 3; ++i) {
        data.center[static_cast<std::size_t>(i)] =
            (lo[static_cast<std::size_t>(i)] + hi[static_cast<std::size_t>(i)]) * 0.5F;
    }
    const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    data.radius = 0.5F * std::sqrt(dx * dx + dy * dy + dz * dz);
    if (data.radius < 1e-4F) {
        data.radius = 1.0F;
    }
    return data;
}

SDL_GPUShader* create_shader(
    SDL_GPUDevice* device,
    const std::uint32_t* code,
    const std::size_t size_bytes,
    const SDL_GPUShaderStage stage,
    const std::uint32_t num_uniform_buffers,
    const std::uint32_t num_samplers = 0) {
    SDL_GPUShaderCreateInfo info{};
    info.code = reinterpret_cast<const std::uint8_t*>(code);
    info.code_size = size_bytes;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = stage;
    info.num_uniform_buffers = num_uniform_buffers;
    info.num_samplers = num_samplers;
    return SDL_CreateGPUShader(device, &info);
}

SDL_GPUBuffer* upload_buffer(
    SDL_GPUDevice* device,
    const void* data,
    const std::uint32_t size,
    const SDL_GPUBufferUsageFlags usage) {
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = usage;
    bci.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &bci);
    if (buffer == nullptr) {
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tci);
    if (transfer == nullptr) {
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = transfer;
    src.offset = 0;
    SDL_GPUBufferRegion dst{};
    dst.buffer = buffer;
    dst.offset = 0;
    dst.size = size;
    SDL_UploadToGPUBuffer(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return buffer;
}

SDL_GPUTexture* create_texture_rgba(
    SDL_GPUDevice* device,
    const std::uint8_t* rgba,
    const std::uint32_t width,
    const std::uint32_t height) {
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = width;
    tci.height = height;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &tci);
    if (texture == nullptr) {
        return nullptr;
    }

    const std::uint32_t size = width * height * 4U;
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tbci);
    if (transfer == nullptr) {
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    std::memcpy(mapped, rgba, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = transfer;
    src.offset = 0;
    src.pixels_per_row = width;
    src.rows_per_layer = height;
    SDL_GPUTextureRegion dst{};
    dst.texture = texture;
    dst.w = width;
    dst.h = height;
    dst.d = 1;
    SDL_UploadToGPUTexture(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return texture;
}

SDL_GPUTextureFormat choose_depth_format(SDL_GPUDevice* device) {
    if (SDL_GPUTextureSupportsFormat(
            device,
            SDL_GPU_TEXTUREFORMAT_D24_UNORM,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    }
    return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
}

SDL_GPUTexture* create_depth_texture(
    SDL_GPUDevice* device,
    const SDL_GPUTextureFormat format,
    const std::uint32_t width,
    const std::uint32_t height) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(device, &info);
}

}  // namespace

namespace khdays::platform {

int render_model(const std::filesystem::path& model_path) {
    khdays::assets::NeutralModel model;
    try {
        model = khdays::assets::decode_model_geometry(model_path);
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    // Decode each material's TEX0 texture once (CPU). Sizes are needed to
    // normalize UVs; the RGBA is reused to upload the GPU textures below.
    std::map<std::string, khdays::assets::DecodedTexture> decoded_textures;
    for (const auto& m : model.meshes) {
        if (m.texture_name.empty()
            || decoded_textures.count(m.texture_name) != 0U) {
            continue;
        }
        try {
            decoded_textures.emplace(
                m.texture_name,
                khdays::assets::load_tex0_texture(model_path, m.texture_name));
        } catch (const std::exception& error) {
            std::cerr << "texture '" << m.texture_name << "': "
                      << error.what() << '\n';
        }
    }

    const auto mesh = build_mesh(model, decoded_textures);
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        std::cerr << "ERROR: model has no drawable geometry\n";
        return EXIT_FAILURE;
    }
    std::cout << "Rendering '" << model.name << "': "
              << mesh.vertices.size() << " vertices, "
              << mesh.indices.size() / 3U << " triangles\n";

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log_sdl("SDL_Init");
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "khdays-port - model viewer",
        kInitialWidth,
        kInitialHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        log_sdl("SDL_CreateWindow");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GPUDevice* device =
        SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (device == nullptr) {
        log_sdl("SDL_CreateGPUDevice");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    std::cout << "GPU backend: " << SDL_GetGPUDeviceDriver(device) << '\n';

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        log_sdl("SDL_ClaimWindowForGPUDevice");
        return EXIT_FAILURE;
    }

    SDL_GPUShader* vertex_shader = create_shader(
        device, kModelVertSpirv, sizeof(kModelVertSpirv),
        SDL_GPU_SHADERSTAGE_VERTEX, 1);
    SDL_GPUShader* fragment_shader = create_shader(
        device, kModelFragSpirv, sizeof(kModelFragSpirv),
        SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    if (vertex_shader == nullptr || fragment_shader == nullptr) {
        log_sdl("SDL_CreateGPUShader");
        return EXIT_FAILURE;
    }

    const SDL_GPUTextureFormat depth_format = choose_depth_format(device);

    // Vertex layout: position, uv, color, normal.
    SDL_GPUVertexBufferDescription vbuf_desc{};
    vbuf_desc.slot = 0;
    vbuf_desc.pitch = sizeof(Vertex);
    vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    std::array<SDL_GPUVertexAttribute, 4> attributes{};
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, px)};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, u)};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, r)};
    attributes[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, nx)};

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vertex_shader;
    pci.fragment_shader = fragment_shader;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.vertex_input_state.vertex_buffer_descriptions = &vbuf_desc;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attributes.data();
    pci.vertex_input_state.num_vertex_attributes = 4;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.target_info.color_target_descriptions = &color_target;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = depth_format;

    SDL_GPUGraphicsPipeline* pipeline =
        SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);
    if (pipeline == nullptr) {
        log_sdl("SDL_CreateGPUGraphicsPipeline");
        return EXIT_FAILURE;
    }

    SDL_GPUBuffer* vertex_buffer = upload_buffer(
        device, mesh.vertices.data(),
        static_cast<std::uint32_t>(mesh.vertices.size() * sizeof(Vertex)),
        SDL_GPU_BUFFERUSAGE_VERTEX);
    SDL_GPUBuffer* index_buffer = upload_buffer(
        device, mesh.indices.data(),
        static_cast<std::uint32_t>(mesh.indices.size() * sizeof(std::uint32_t)),
        SDL_GPU_BUFFERUSAGE_INDEX);
    if (vertex_buffer == nullptr || index_buffer == nullptr) {
        log_sdl("upload_buffer");
        return EXIT_FAILURE;
    }

    // Upload one GPU texture per decoded texture, plus a white 1x1 fallback for
    // meshes with no or unresolved texture.
    std::map<std::string, SDL_GPUTexture*> textures;
    for (const auto& [name, decoded] : decoded_textures) {
        textures[name] = create_texture_rgba(
            device, decoded.rgba.data(),
            static_cast<std::uint32_t>(decoded.width),
            static_cast<std::uint32_t>(decoded.height));
    }

    const std::uint8_t white_pixel[4] = {255U, 255U, 255U, 255U};
    SDL_GPUTexture* fallback_texture =
        create_texture_rgba(device, white_pixel, 1, 1);

    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &sampler_info);
    if (sampler == nullptr || fallback_texture == nullptr) {
        log_sdl("texture setup");
        return EXIT_FAILURE;
    }

    SDL_GPUTexture* depth_texture = nullptr;
    std::uint32_t depth_w = 0, depth_h = 0;

    const float distance = mesh.radius * 2.8F;
    const auto projection_near = std::max(0.01F, mesh.radius * 0.02F);
    const auto projection_far = mesh.radius * 20.0F;

    bool running = true;
    std::uint64_t start = SDL_GetTicks();

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN
                && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr) {
            log_sdl("SDL_AcquireGPUCommandBuffer");
            break;
        }

        SDL_GPUTexture* swapchain = nullptr;
        std::uint32_t width = 0, height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(
                cmd, window, &swapchain, &width, &height)) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }
        if (swapchain == nullptr) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }

        if (depth_texture == nullptr || width != depth_w || height != depth_h) {
            if (depth_texture != nullptr) {
                SDL_ReleaseGPUTexture(device, depth_texture);
            }
            depth_texture = create_depth_texture(device, depth_format, width, height);
            depth_w = width;
            depth_h = height;
        }

        // Camera and model transforms.
        const float seconds =
            static_cast<float>(SDL_GetTicks() - start) / 1000.0F;
        const Mat model_rot = rotation_y(seconds * 0.6F);
        const Mat model_mat = multiply(
            model_rot,
            translation(-mesh.center[0], -mesh.center[1], -mesh.center[2]));
        const Mat view = translation(0.0F, 0.0F, -distance);
        const float aspect = height > 0U
            ? static_cast<float>(width) / static_cast<float>(height)
            : 1.0F;
        const Mat proj = perspective(
            50.0F * 3.14159265F / 180.0F, aspect, projection_near, projection_far);
        const Mat mvp = multiply(proj, multiply(view, model_mat));

        Uniforms uniforms{};
        std::memcpy(uniforms.mvp, mvp.data(), sizeof(uniforms.mvp));
        std::memcpy(uniforms.model, model_rot.data(), sizeof(uniforms.model));
        uniforms.light[0] = 0.4F;
        uniforms.light[1] = 0.7F;
        uniforms.light[2] = 1.0F;
        uniforms.light[3] = 0.0F;

        SDL_GPUColorTargetInfo color_info{};
        color_info.texture = swapchain;
        color_info.clear_color = SDL_FColor{0.05F, 0.07F, 0.11F, 1.0F};
        color_info.load_op = SDL_GPU_LOADOP_CLEAR;
        color_info.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo depth_info{};
        depth_info.texture = depth_texture;
        depth_info.clear_depth = 1.0F;
        depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
        depth_info.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass* pass =
            SDL_BeginGPURenderPass(cmd, &color_info, 1, &depth_info);
        SDL_BindGPUGraphicsPipeline(pass, pipeline);

        SDL_GPUBufferBinding vertex_binding{};
        vertex_binding.buffer = vertex_buffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);

        SDL_GPUBufferBinding index_binding{};
        index_binding.buffer = index_buffer;
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));

        for (const auto& sub : mesh.submeshes) {
            SDL_GPUTexture* tex = fallback_texture;
            const auto it = textures.find(sub.texture_name);
            if (it != textures.end() && it->second != nullptr) {
                tex = it->second;
            }
            SDL_GPUTextureSamplerBinding binding{};
            binding.texture = tex;
            binding.sampler = sampler;
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            SDL_DrawGPUIndexedPrimitives(
                pass, sub.index_count, 1, sub.first_index, 0, 0);
        }

        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
    }

    if (depth_texture != nullptr) {
        SDL_ReleaseGPUTexture(device, depth_texture);
    }
    for (const auto& [name, tex] : textures) {
        if (tex != nullptr) {
            SDL_ReleaseGPUTexture(device, tex);
        }
    }
    SDL_ReleaseGPUTexture(device, fallback_texture);
    SDL_ReleaseGPUSampler(device, sampler);
    SDL_ReleaseGPUBuffer(device, vertex_buffer);
    SDL_ReleaseGPUBuffer(device, index_buffer);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

}  // namespace khdays::platform
