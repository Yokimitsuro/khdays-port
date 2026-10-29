#include "khdays/assets/kh2_mdlx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include "khdays/assets/kh2_bar.h"
#include "khdays/assets/kh2_map.h"

// A KH2 model's bones place its vertices: a bone's matrix is its parent's times
// T * Rz * Ry * Rx * S (as kh2_room.cpp's pieces). The rest pose is the file's;
// each vertex goes into the model's space there, the joints' palette is the
// identity at rest and moves with them (model_import.h).

namespace khdays::assets::kh2 {

namespace {

using Mat = std::array<float, 16>;  // column-major
constexpr Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr float kMetres = 0.01F;  // KH2's units are centimetres; mods' models are in metres

Mat mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0F;
            for (int k = 0; k < 4; ++k) {
                s += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
            }
            r[static_cast<std::size_t>(c * 4 + row)] = s;
        }
    }
    return r;
}

Mat local(const Bone& bone) {
    const float cx = std::cos(bone.rotation[0]), sx = std::sin(bone.rotation[0]);
    const float cy = std::cos(bone.rotation[1]), sy = std::sin(bone.rotation[1]);
    const float cz = std::cos(bone.rotation[2]), sz = std::sin(bone.rotation[2]);
    // R = Rz * Ry * Rx, its columns scaled by S
    const std::array<float, 9> r{cz * cy,
                                 sz * cy,
                                 -sy,
                                 cz * sy * sx - sz * cx,
                                 sz * sy * sx + cz * cx,
                                 cy * sx,
                                 cz * sy * cx + sz * sx,
                                 sz * sy * cx - cz * sx,
                                 cy * cx};
    Mat m = kIdentity;
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t row = 0; row < 3; ++row) {
            m[c * 4 + row] = r[c * 3 + row] * bone.scale[c];
        }
    }
    m[12] = bone.translation[0];
    m[13] = bone.translation[1];
    m[14] = bone.translation[2];
    return m;
}

// The inverse of a matrix whose last row is 0 0 0 1.
Mat inverse_affine(const Mat& m) {
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12F) {
        throw std::runtime_error("KH2 model: a bone with no inverse (a scale of 0)");
    }
    const float k = 1.0F / det;
    Mat r = kIdentity;
    r[0] = (e * i - f * h) * k;
    r[4] = (c * h - b * i) * k;
    r[8] = (b * f - c * e) * k;
    r[1] = (f * g - d * i) * k;
    r[5] = (a * i - c * g) * k;
    r[9] = (c * d - a * f) * k;
    r[2] = (d * h - e * g) * k;
    r[6] = (b * g - a * h) * k;
    r[10] = (a * e - b * d) * k;
    for (std::size_t row = 0; row < 3; ++row) {
        r[12 + row] = -(r[row] * m[12] + r[4 + row] * m[13] + r[8 + row] * m[14]);
    }
    return r;
}

std::array<float, 3> place(const Mat& m, const float x, const float y, const float z, const float w) {
    return {m[0] * x + m[4] * y + m[8] * z + m[12] * w, m[1] * x + m[5] * y + m[9] * z + m[13] * w,
            m[2] * x + m[6] * y + m[10] * z + m[14] * w};
}

// One corner as a mod's vertex: in the model's space at rest (metres), on up to
// four of its bones (the heaviest, weights summing to 1).
NeutralVertex corner(const Vertex& v, const std::vector<Mat>& world, const std::array<float, 2>& texels) {
    const auto bone_world = [&world](const int bone) -> const Mat& {
        if (bone < 0 || static_cast<std::size_t>(bone) >= world.size()) {
            throw std::runtime_error("KH2 model: a vertex on bone " + std::to_string(bone) + " of " +
                                     std::to_string(world.size()));
        }
        return world[static_cast<std::size_t>(bone)];
    };
    NeutralVertex out;
    std::vector<std::pair<float, int>> shares;  // (weight, bone)
    std::array<float, 3> p{};
    if (v.influences.empty()) {
        p = place(bone_world(v.bone), v.position[0], v.position[1], v.position[2], 1.0F);
        shares.emplace_back(1.0F, v.bone);
    } else {
        for (const Influence& in : v.influences) {
            const auto q = place(bone_world(in.bone), in.position[0], in.position[1], in.position[2], in.position[3]);
            for (std::size_t i = 0; i < 3; ++i) {
                p[i] += q[i];
            }
            shares.emplace_back(in.position[3], in.bone);
        }
        std::sort(shares.begin(), shares.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        shares.resize(std::min<std::size_t>(shares.size(), 4U));
    }
    float total = 0.0F;
    for (const auto& s : shares) {
        total += s.first;
    }
    out.weights = {0.0F, 0.0F, 0.0F, 0.0F};
    for (std::size_t i = 0; i < shares.size(); ++i) {
        out.joints[i] = static_cast<std::uint32_t>(shares[i].second + 1);  // palette 0 is the identity
        out.weights[i] = total > 0.0F ? shares[i].first / total : (i == 0 ? 1.0F : 0.0F);
    }
    out.position = {p[0] * kMetres, p[1] * kMetres, p[2] * kMetres};
    out.texcoord = {v.uv[0] * texels[0], v.uv[1] * texels[1]};
    for (std::size_t i = 0; i < 4; ++i) {
        out.color[i] = static_cast<std::uint8_t>(std::min(255, v.color[i] * 2));  // PS2's 0x80 = 1.0
    }
    return out;
}

}  // namespace

std::string mdlx_joint_name(const int bone) {
    return "kh2_" + std::to_string(bone);
}

ImportedModel import_mdlx(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return import_mdlx(bytes.data(), bytes.size(), path.stem().string());
}

ImportedModel import_mdlx(const std::uint8_t* data, const std::size_t size, const std::string& name) {
    const auto entries = bar_entries(data, size);
    const BarEntry* model_entry = nullptr;
    const BarEntry* texture_entry = nullptr;
    for (const BarEntry& e : entries) {
        if (e.type == kEntryModel && e.size != 0 && model_entry == nullptr) {
            model_entry = &e;
        } else if (e.type == kEntryTextures && e.size != 0 && texture_entry == nullptr) {
            texture_entry = &e;
        }
    }
    if (model_entry == nullptr) {
        throw std::runtime_error("KH2 model: the BAR has no model entry (type 4)");
    }
    const Model model = read_model(data, size, model_entry->offset);
    if (model.kind != 3) {
        throw std::runtime_error("KH2 model: a map model (kind " + std::to_string(model.kind) +
                                 "), not a character's or an object's");
    }

    ImportedModel result;
    result.model.name = name;
    result.model.palette.push_back(kIdentity);

    // the bones at rest, parents first as the file lists them
    std::vector<Mat> world;
    world.reserve(model.bones.size());
    for (std::size_t b = 0; b < model.bones.size(); ++b) {
        const int parent = model.bones[b].parent;
        if (parent >= static_cast<int>(b)) {
            throw std::runtime_error("KH2 model: bone " + std::to_string(b) + "'s parent " + std::to_string(parent) +
                                     " comes after it");
        }
        const Mat m = local(model.bones[b]);
        world.push_back(parent >= 0 ? mul(world[static_cast<std::size_t>(parent)], m) : m);
    }
    for (std::size_t b = 0; b < world.size(); ++b) {
        // in metres: the same turn, its place scaled
        Mat w = world[b];
        w[12] *= kMetres;
        w[13] *= kMetres;
        w[14] *= kMetres;
        ImportedJoint joint;
        joint.palette_index = static_cast<std::uint32_t>(result.model.palette.size());
        joint.name = mdlx_joint_name(static_cast<int>(b));
        joint.world = w;
        joint.inverse_bind = inverse_affine(w);
        joint.parent = model.bones[b].parent;
        result.joints.push_back(joint);
        result.model.palette.push_back(kIdentity);
    }

    // the textures: GS infos decoded, each named by its index
    TextureContainer container;
    if (texture_entry != nullptr) {
        container = read_textures(data, size, texture_entry->offset);
        for (std::size_t t = 0; t < container.textures.size(); ++t) {
            const std::string key = "tex" + std::to_string(t);
            try {
                DecodedTexture picture = read_texture_picture(data, size, texture_entry->offset, t);
                picture.name = key;
                result.textures.emplace_back(key, std::move(picture));
            } catch (const std::exception& e) {
                result.missing_textures.emplace_back(key, e.what());
            }
        }
    }

    // a mesh per subpart, and per face kind: one-sided, and seen from both sides
    for (std::size_t g = 0; g < model.groups.size(); ++g) {
        const Group& group = model.groups[g];
        const bool textured = group.texture >= 0 && static_cast<std::size_t>(group.texture) < container.textures.size();
        std::array<float, 2> texels{1.0F, 1.0F};
        if (textured) {
            const GsTexture& t = container.textures[static_cast<std::size_t>(group.texture)];
            texels = {static_cast<float>(t.width), static_cast<float>(t.height)};
        }
        for (const bool two_sided : {false, true}) {
            NeutralMesh mesh;
            mesh.name = name + "_" + std::to_string(g) + (two_sided ? "_both" : "");
            mesh.texture_name = textured ? "tex" + std::to_string(group.texture) : std::string{};
            mesh.material_name = mesh.name;
            mesh.double_sided = two_sided;
            // bit 1: translucent; otherwise drawn where the texture is opaque enough
            mesh.alpha_mode =
                (group.attribute & 2U) != 0 ? NeutralMesh::AlphaMode::blend : NeutralMesh::AlphaMode::mask;
            for (const Triangle& t : group.triangles) {
                if (t.two_sided != two_sided) {
                    continue;
                }
                for (const Vertex& v : t.corners) {
                    mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                    mesh.vertices.push_back(corner(v, world, texels));
                }
            }
            if (mesh.vertices.empty()) {
                continue;
            }
            result.model.header_vertex_count += static_cast<std::uint16_t>(mesh.vertices.size());
            result.model.meshes.push_back(std::move(mesh));
        }
    }
    if (result.model.meshes.empty()) {
        throw std::runtime_error("KH2 model: no triangles");
    }
    return result;
}

}  // namespace khdays::assets::kh2
