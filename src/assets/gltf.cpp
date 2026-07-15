#include "khdays/assets/gltf.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

#include "khdays/assets/png.h"

// cgltf is third-party and not warning-clean; isolate it.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

const cgltf_accessor* find_attribute(
    const cgltf_primitive& primitive,
    const cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const auto& attr = primitive.attributes[i];
        if (attr.type == type && attr.index == 0) {
            return attr.data;
        }
    }
    return nullptr;
}

// The base-color image referenced by a primitive's material, or nullptr.
const cgltf_image* base_color_image(const cgltf_primitive& primitive) {
    const cgltf_material* material = primitive.material;
    if (material == nullptr || material->has_pbr_metallic_roughness == 0) {
        return nullptr;
    }
    const cgltf_texture* texture =
        material->pbr_metallic_roughness.base_color_texture.texture;
    if (texture == nullptr) {
        return nullptr;
    }
    return texture->image;
}

std::string image_key(const cgltf_image* image, std::size_t fallback_index) {
    if (image != nullptr && image->name != nullptr && image->name[0] != '\0') {
        return image->name;
    }
    if (image != nullptr && image->uri != nullptr) {
        return image->uri;
    }
    return "gltf_image_" + std::to_string(fallback_index);
}

}  // namespace

namespace khdays::assets {

GltfModel import_gltf(const std::filesystem::path& input_path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const auto path_string = input_path.string();

    if (cgltf_parse_file(&options, path_string.c_str(), &data)
        != cgltf_result_success) {
        throw std::runtime_error("cannot parse glTF: " + path_string);
    }
    if (cgltf_load_buffers(&options, data, path_string.c_str())
        != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("cannot load glTF buffers: " + path_string);
    }

    GltfModel result;
    result.model.name = input_path.stem().string();
    result.model.palette.push_back(
        std::array<float, 16>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});

    const auto base_dir = input_path.parent_path();

    // Load each referenced image once, keyed by name; remember its size.
    std::map<std::string, DecodedTexture> loaded;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image& image = data->images[i];
        if (image.uri == nullptr) {
            continue;  // embedded images are a later step
        }
        const auto key = image_key(&image, i);
        if (loaded.count(key) != 0U) {
            continue;
        }
        try {
            auto texture = load_png(base_dir / image.uri);
            texture.name = key;
            loaded.emplace(key, std::move(texture));
        } catch (const std::exception&) {
            // Missing/unsupported texture: the mesh falls back to white.
        }
    }

    std::array<float, 4> position{};
    std::array<float, 2> uv{};

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (node.mesh == nullptr) {
            continue;
        }
        cgltf_float world[16];
        cgltf_node_transform_world(&node, world);
        const cgltf_mesh& mesh = *node.mesh;
        const cgltf_size m = n;  // index used for names
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& primitive = mesh.primitives[p];
            if (primitive.type != cgltf_primitive_type_triangles) {
                continue;
            }
            const cgltf_accessor* positions =
                find_attribute(primitive, cgltf_attribute_type_position);
            if (positions == nullptr) {
                continue;
            }
            const cgltf_accessor* texcoords =
                find_attribute(primitive, cgltf_attribute_type_texcoord);
            const cgltf_accessor* colors =
                find_attribute(primitive, cgltf_attribute_type_color);

            const cgltf_image* image = base_color_image(primitive);
            const auto texture_key =
                image != nullptr ? image_key(image, m) : std::string{};
            float tex_w = 1.0F;
            float tex_h = 1.0F;
            const auto texture_it = loaded.find(texture_key);
            if (texture_it != loaded.end()) {
                tex_w = static_cast<float>(texture_it->second.width);
                tex_h = static_cast<float>(texture_it->second.height);
            }

            NeutralMesh out_mesh;
            out_mesh.name =
                mesh.name != nullptr ? mesh.name : ("mesh_" + std::to_string(m));
            out_mesh.texture_name = texture_key;

            const auto vertex_count = positions->count;
            out_mesh.vertices.reserve(vertex_count);
            for (cgltf_size v = 0; v < vertex_count; ++v) {
                NeutralVertex vertex;
                cgltf_accessor_read_float(positions, v, position.data(), 3);
                // Apply the node's world transform (glTF places geometry via
                // the node hierarchy).
                vertex.position = {
                    world[0] * position[0] + world[4] * position[1]
                        + world[8] * position[2] + world[12],
                    world[1] * position[0] + world[5] * position[1]
                        + world[9] * position[2] + world[13],
                    world[2] * position[0] + world[6] * position[1]
                        + world[10] * position[2] + world[14]};

                if (texcoords != nullptr
                    && cgltf_accessor_read_float(texcoords, v, uv.data(), 2)) {
                    // Store as texels so the renderer normalizes like DS UVs.
                    vertex.texcoord = {uv[0] * tex_w, uv[1] * tex_h};
                }

                std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
                if (colors != nullptr) {
                    cgltf_accessor_read_float(colors, v, color.data(), 4);
                }
                vertex.color = {
                    static_cast<std::uint8_t>(color[0] * 255.0F),
                    static_cast<std::uint8_t>(color[1] * 255.0F),
                    static_cast<std::uint8_t>(color[2] * 255.0F),
                    static_cast<std::uint8_t>(color[3] * 255.0F)};

                // Static import: single identity matrix (joints/weights default
                // to {0} / {1,0,0,0}).
                out_mesh.vertices.push_back(vertex);
            }

            if (primitive.indices != nullptr) {
                out_mesh.indices.reserve(primitive.indices->count);
                for (cgltf_size k = 0; k < primitive.indices->count; ++k) {
                    out_mesh.indices.push_back(static_cast<std::uint32_t>(
                        cgltf_accessor_read_index(primitive.indices, k)));
                }
            } else {
                for (cgltf_size k = 0; k < vertex_count; ++k) {
                    out_mesh.indices.push_back(static_cast<std::uint32_t>(k));
                }
            }

            result.model.header_vertex_count += static_cast<std::uint16_t>(
                out_mesh.vertices.size());
            result.model.meshes.push_back(std::move(out_mesh));
        }
    }

    for (auto& [key, texture] : loaded) {
        result.textures.emplace_back(key, std::move(texture));
    }

    cgltf_free(data);
    return result;
}

}  // namespace khdays::assets
