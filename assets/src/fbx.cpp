#include "khdays/assets/fbx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/image.h"

// ufbx is third-party; its header is warning-clean but kept apart as cgltf is.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "ufbx.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// FBX to the neutral form import_gltf gives, read by ufbx in glTF's frame: each mesh instance a set of
// meshes (one per material), each skin deformer a block of palette entries. ufbx turns the axes in the
// nodes' transforms, so skinned vertices are carried to where the bind pose puts them and each joint's
// inverse bind is its bind_to_world's inverse: identity at rest, in the model's Y-up space.

namespace {

constexpr std::array<float, 16> kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

std::string text(const ufbx_string& s) {
    return std::string(s.data, s.length);
}

std::string utf8(const std::filesystem::path& path) {
    const auto u = path.u8string();
    return std::string(u.begin(), u.end());
}

// ufbx's 3x4 affine matrix as the neutral palette's column-major 4x4.
std::array<float, 16> to_array(const ufbx_matrix& m) {
    return {static_cast<float>(m.m00), static_cast<float>(m.m10), static_cast<float>(m.m20), 0.0F,
            static_cast<float>(m.m01), static_cast<float>(m.m11), static_cast<float>(m.m21), 0.0F,
            static_cast<float>(m.m02), static_cast<float>(m.m12), static_cast<float>(m.m22), 0.0F,
            static_cast<float>(m.m03), static_cast<float>(m.m13), static_cast<float>(m.m23), 1.0F};
}

std::array<float, 16> multiply(const std::array<float, 16>& a, const std::array<float, 16>& b) {
    std::array<float, 16> result{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k) {
                sum += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(col * 4 + k)];
            }
            result[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return result;
}

// The texture a material shows: its diffuse colour's, else its PBR base colour's.
const ufbx_texture* material_texture(const ufbx_material* material) {
    if (material == nullptr) {
        return nullptr;
    }
    if (material->fbx.diffuse_color.texture != nullptr) {
        return material->fbx.diffuse_color.texture;
    }
    return material->pbr.base_color.texture;
}

// A node's local transform as its translation, rotation (x y z w), scale and their matrix.
void set_transform(khdays::assets::ImportedNode& node, const ufbx_transform& t) {
    node.translation = {static_cast<float>(t.translation.x), static_cast<float>(t.translation.y),
                        static_cast<float>(t.translation.z)};
    node.rotation = {static_cast<float>(t.rotation.x), static_cast<float>(t.rotation.y),
                     static_cast<float>(t.rotation.z), static_cast<float>(t.rotation.w)};
    node.scale = {static_cast<float>(t.scale.x), static_cast<float>(t.scale.y), static_cast<float>(t.scale.z)};
    node.local = to_array(ufbx_transform_to_matrix(&t));
}

// An animation stack as tracks of every node's translation, rotation and scale, a key each 1/`rate` s
// from its start to its end (ufbx_evaluate_transform: the scene's axes and units as the nodes').
khdays::assets::ImportedAnimation sample_stack(const ufbx_scene& scene, const ufbx_anim_stack& stack, double rate) {
    using khdays::assets::ImportedTrack;
    khdays::assets::ImportedAnimation out;
    out.name = text(stack.name);
    const double begin = stack.time_begin;
    const double end = stack.time_end > begin ? stack.time_end : begin;
    out.duration = static_cast<float>(end - begin);
    const auto keys = static_cast<std::size_t>(std::ceil((end - begin) * rate - 1e-6)) + 1U;
    for (std::size_t n = 0; n < scene.nodes.count; ++n) {
        const ufbx_node* node = scene.nodes.data[n];
        if (node->is_root) {
            continue;
        }
        ImportedTrack t, r, s;
        t.node = r.node = s.node = static_cast<int>(n);
        t.path = ImportedTrack::Path::translation;
        r.path = ImportedTrack::Path::rotation;
        s.path = ImportedTrack::Path::scale;
        for (std::size_t k = 0; k < keys; ++k) {
            const double at = std::min(begin + static_cast<double>(k) / rate, end);
            const ufbx_transform x = ufbx_evaluate_transform(stack.anim, node, at);
            const auto time = static_cast<float>(at - begin);
            t.times.push_back(time);
            r.times.push_back(time);
            s.times.push_back(time);
            t.values.insert(t.values.end(), {static_cast<float>(x.translation.x), static_cast<float>(x.translation.y),
                                             static_cast<float>(x.translation.z)});
            r.values.insert(r.values.end(), {static_cast<float>(x.rotation.x), static_cast<float>(x.rotation.y),
                                             static_cast<float>(x.rotation.z), static_cast<float>(x.rotation.w)});
            s.values.insert(s.values.end(), {static_cast<float>(x.scale.x), static_cast<float>(x.scale.y),
                                             static_cast<float>(x.scale.z)});
        }
        out.tracks.push_back(std::move(t));
        out.tracks.push_back(std::move(r));
        out.tracks.push_back(std::move(s));
    }
    return out;
}

}  // namespace

namespace khdays::assets {

ImportedModel import_fbx(const std::filesystem::path& input_path) {
    ufbx_load_opts options{};
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0F;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    ufbx_error error{};
    const std::string path = utf8(input_path);
    ufbx_scene* loaded_scene = ufbx_load_file_len(path.data(), path.size(), &options, &error);
    if (loaded_scene == nullptr) {
        char reason[1024];
        ufbx_format_error(reason, sizeof(reason), &error);
        throw std::runtime_error("cannot read FBX " + path + ": " + reason);
    }
    const std::unique_ptr<ufbx_scene, void (*)(ufbx_scene*)> scene(loaded_scene, ufbx_free_scene);

    ImportedModel result;
    result.model.name = utf8(input_path.stem());
    result.model.palette.push_back(kIdentity);  // entry 0: static meshes
    const auto folder = input_path.parent_path();

    // Each texture once, keyed by name: embedded, else a file the search finds
    // (the stored relative path, the absolute one, the file name beside the
    // model); one that cannot be had is drawn white and told to the caller.
    std::map<const ufbx_texture*, std::string> texture_keys;
    std::map<std::string, DecodedTexture> textures;
    const auto texture_key = [&](const ufbx_texture* texture) -> std::string {
        if (texture == nullptr) {
            return {};
        }
        const auto known = texture_keys.find(texture);
        if (known != texture_keys.end()) {
            return known->second;
        }
        std::string key = text(texture->name);
        if (key.empty()) {
            key = text(texture->relative_filename);
        }
        if (key.empty()) {
            key = "fbx_texture_" + std::to_string(texture->element_id);
        }
        texture_keys.emplace(texture, key);
        if (textures.count(key) != 0U) {
            return key;
        }
        try {
            DecodedTexture image;
            if (texture->content.size > 0U) {
                image = read_image(static_cast<const std::uint8_t*>(texture->content.data), texture->content.size, key);
            } else {
                auto file = find_texture(folder, text(texture->relative_filename));
                if (!file) {
                    file = find_texture(folder, text(texture->absolute_filename));
                }
                if (!file) {
                    file = find_texture(folder, text(texture->filename));
                }
                if (!file) {
                    throw std::runtime_error("not found (stored as " + text(texture->relative_filename) + ")");
                }
                image = read_image(*file);
            }
            image.name = key;
            textures.emplace(key, std::move(image));
        } catch (const std::exception& failure) {
            result.missing_textures.emplace_back(key, failure.what());
        }
        return key;
    };

    // The joints: one per bone (its node, at its bind pose), whichever skins
    // use it -- the vertices being carried to the bind pose, a joint's palette
    // entry depends on its bone alone; a bone bound at two poses gives two.
    // Each skin's clusters map to them.
    std::vector<const ufbx_node*> joint_bone;
    std::map<const ufbx_skin_deformer*, std::vector<std::uint32_t>> cluster_palette;
    const auto same = [](const ufbx_matrix& a, const ufbx_matrix& b) {
        for (int k = 0; k < 12; ++k) {
            if (std::abs(a.v[k] - b.v[k]) > 1e-5) {
                return false;
            }
        }
        return true;
    };
    std::vector<ufbx_matrix> joint_bind;
    const auto add_skin = [&](const ufbx_skin_deformer* skin) -> const std::vector<std::uint32_t>& {
        const auto known = cluster_palette.find(skin);
        if (known != cluster_palette.end()) {
            return known->second;
        }
        std::vector<std::uint32_t> palette_of(skin->clusters.count, 0U);
        for (std::size_t c = 0; c < skin->clusters.count; ++c) {
            const ufbx_skin_cluster* cluster = skin->clusters.data[c];
            std::size_t found = result.joints.size();
            for (std::size_t j = 0; j < result.joints.size(); ++j) {
                if (joint_bone[j] == cluster->bone_node && same(joint_bind[j], cluster->bind_to_world)) {
                    found = j;
                    break;
                }
            }
            if (found == result.joints.size()) {
                ImportedJoint joint;
                joint.palette_index = static_cast<std::uint32_t>(result.model.palette.size());
                if (cluster->bone_node != nullptr) {
                    joint.name = text(cluster->bone_node->name);
                }
                const ufbx_matrix inverse_bind = ufbx_matrix_invert(&cluster->bind_to_world);
                joint.inverse_bind = to_array(inverse_bind);
                joint.world = to_array(cluster->bind_to_world);
                result.model.palette.push_back(multiply(joint.world, joint.inverse_bind));
                result.joints.push_back(std::move(joint));
                joint_bone.push_back(cluster->bone_node);
                joint_bind.push_back(cluster->bind_to_world);
            }
            palette_of[c] = result.joints[found].palette_index;
        }
        return cluster_palette.emplace(skin, std::move(palette_of)).first->second;
    };

    std::vector<std::uint32_t> triangle;  // a face's triangles' corners
    for (std::size_t n = 0; n < scene->nodes.count; ++n) {
        const ufbx_node* node = scene->nodes.data[n];
        const ufbx_mesh* mesh = node->mesh;
        if (mesh == nullptr || mesh->num_faces == 0U) {
            continue;
        }
        const ufbx_skin_deformer* skin = mesh->skin_deformers.count > 0U ? mesh->skin_deformers.data[0] : nullptr;
        static const std::vector<std::uint32_t> no_clusters;
        const std::vector<std::uint32_t>& palette_of = skin != nullptr ? add_skin(skin) : no_clusters;
        // where the vertices go: a skinned mesh's bind pose (its first
        // cluster's bone at bind times the geometry in that bone), else its node
        ufbx_matrix to_world = node->geometry_to_world;
        if (skin != nullptr && skin->clusters.count > 0U) {
            const ufbx_skin_cluster* first = skin->clusters.data[0];
            to_world = ufbx_matrix_mul(&first->bind_to_world, &first->geometry_to_bone);
        }
        const ufbx_matrix normals_to_world = ufbx_matrix_for_normals(&to_world);

        // one neutral mesh per material the faces use
        const std::size_t parts = std::max<std::size_t>(mesh->materials.count, 1U);
        std::vector<NeutralMesh> out(parts);
        std::vector<float> tex_w(parts, 1.0F), tex_h(parts, 1.0F);
        for (std::size_t m = 0; m < parts; ++m) {
            const ufbx_material* material = m < mesh->materials.count ? mesh->materials.data[m] : nullptr;
            out[m].name = text(mesh->name) + (parts > 1U ? "_" + std::to_string(m) : std::string());
            if (out[m].name.empty()) {
                out[m].name = "mesh_" + std::to_string(n) + "_" + std::to_string(m);
            }
            out[m].texture_name = texture_key(material_texture(material));
            out[m].material_name = material != nullptr ? text(material->name) : std::string();
            // FBX has no standard two-sided flag (ufbx reads none): both faces,
            // since a face missing from a model is worse than one seen from behind
            // (the game's own models are two-sided in the dump's glTF)
            out[m].double_sided = true;
            const auto t = textures.find(out[m].texture_name);
            if (t != textures.end()) {
                tex_w[m] = static_cast<float>(t->second.width);
                tex_h[m] = static_cast<float>(t->second.height);
            }
        }

        triangle.resize(mesh->max_face_triangles * 3U);
        for (std::size_t f = 0; f < mesh->faces.count; ++f) {
            const ufbx_face face = mesh->faces.data[f];
            const std::size_t m = mesh->face_material.count > f ? mesh->face_material.data[f] : 0U;
            NeutralMesh& part = out[m < parts ? m : 0U];
            const std::uint32_t triangles = ufbx_triangulate_face(triangle.data(), triangle.size(), mesh, face);
            for (std::uint32_t corner = 0; corner < triangles * 3U; ++corner) {
                const std::size_t index = triangle[corner];
                NeutralVertex vertex;
                const ufbx_vec3 position = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                ufbx_vec3 normal{};
                if (mesh->vertex_normal.exists) {
                    normal = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                }
                const ufbx_vec3 p = ufbx_transform_position(&to_world, position);
                const ufbx_vec3 q = ufbx_transform_direction(&normals_to_world, normal);
                vertex.position = {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
                vertex.normal = {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z)};
                if (skin != nullptr) {
                    // the strongest four weights (ufbx sorts them), summing to one
                    const ufbx_skin_vertex& weights = skin->vertices.data[mesh->vertex_indices.data[index]];
                    const std::uint32_t count = std::min<std::uint32_t>(weights.num_weights, 4U);
                    double total = 0.0;
                    for (std::uint32_t w = 0; w < count; ++w) {
                        total += skin->weights.data[weights.weight_begin + w].weight;
                    }
                    if (count > 0U && total > 0.0) {
                        vertex.weights = {0.0F, 0.0F, 0.0F, 0.0F};
                        for (std::uint32_t w = 0; w < count; ++w) {
                            const ufbx_skin_weight& weight = skin->weights.data[weights.weight_begin + w];
                            vertex.joints[w] = palette_of[weight.cluster_index];
                            vertex.weights[w] = static_cast<float>(weight.weight / total);
                        }
                    }  // else unweighted: the identity entry, as a static vertex
                }
                if (mesh->vertex_uv.exists) {
                    // in texels; FBX counts V up from the bottom, glTF (and this) down from the top
                    const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                    const std::size_t which = static_cast<std::size_t>(&part - out.data());
                    vertex.texcoord = {static_cast<float>(uv.x) * tex_w[which],
                                       static_cast<float>(1.0 - uv.y) * tex_h[which]};
                }
                if (mesh->vertex_color.exists) {
                    const ufbx_vec4 c = ufbx_get_vertex_vec4(&mesh->vertex_color, index);
                    const auto byte = [](double v) {
                        return static_cast<std::uint8_t>(std::clamp(v, 0.0, 1.0) * 255.0);
                    };
                    vertex.color = {byte(c.x), byte(c.y), byte(c.z), byte(c.w)};
                }
                part.indices.push_back(static_cast<std::uint32_t>(part.vertices.size()));
                part.vertices.push_back(vertex);
            }
        }
        for (auto& part : out) {
            if (part.vertices.empty()) {
                continue;
            }
            result.model.header_vertex_count += static_cast<std::uint16_t>(part.vertices.size());
            result.model.meshes.push_back(std::move(part));
        }
    }

    // each joint's parent: its bone's nearest ancestor that is a joint's bone
    for (std::size_t j = 0; j < result.joints.size(); ++j) {
        for (const ufbx_node* up = joint_bone[j] != nullptr ? joint_bone[j]->parent : nullptr;
             up != nullptr && result.joints[j].parent < 0; up = up->parent) {
            for (std::size_t k = 0; k < result.joints.size(); ++k) {
                if (joint_bone[k] == up) {
                    result.joints[j].parent = static_cast<int>(k);
                    break;
                }
            }
        }
    }

    // the file's nodes at rest under their parents, each joint's node, and its animation stacks
    // sampled at the file's frame rate (a character's own clips)
    std::map<const ufbx_node*, int> node_index;
    for (std::size_t n = 0; n < scene->nodes.count; ++n) {
        node_index[scene->nodes.data[n]] = static_cast<int>(n);
    }
    for (std::size_t n = 0; n < scene->nodes.count; ++n) {
        const ufbx_node* node = scene->nodes.data[n];
        ImportedNode out;
        out.name = text(node->name);
        const auto parent = node->parent != nullptr ? node_index.find(node->parent) : node_index.end();
        out.parent = parent != node_index.end() ? parent->second : -1;
        set_transform(out, node->local_transform);
        result.nodes.push_back(std::move(out));
    }
    for (std::size_t j = 0; j < result.joints.size(); ++j) {
        const auto found = joint_bone[j] != nullptr ? node_index.find(joint_bone[j]) : node_index.end();
        result.joints[j].node = found != node_index.end() ? found->second : -1;
    }
    const double rate = scene->settings.frames_per_second > 0.0 ? scene->settings.frames_per_second : 30.0;
    for (std::size_t s = 0; s < scene->anim_stacks.count; ++s) {
        result.animations.push_back(sample_stack(*scene, *scene->anim_stacks.data[s], rate));
    }

    for (auto& [key, texture] : textures) {
        result.textures.emplace_back(key, std::move(texture));
    }
    return result;
}

}  // namespace khdays::assets
