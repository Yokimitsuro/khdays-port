#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace khdays::assets {

// Opaque render program (the model's SBC plus inverse binds and scales) that
// rebuilds the matrix palette from a set of bone matrices. Defined in mesh.cpp.
struct SkinningProgram;

// A decoded vertex: its *raw* position from the GPU command stream, in the local space of the matrix
// that applies to it; blending the model's palette matrices by `joints` and `weights` gives its rest
// pose, which is what lets the runtime re-pose it as the DS does. Texture coordinates in texels (divide
// by the texture's size for 0..1).
struct NeutralVertex final {
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 2> texcoord{0.0F, 0.0F};
    std::array<std::uint8_t, 4> color{255U, 255U, 255U, 255U};

    // The normal the GPU command stream gave (raw, like `position`), or 0 0 0
    // when it gave none.
    std::array<float, 3> normal{0.0F, 0.0F, 0.0F};

    // Skinning: up to four palette matrices with weights. The final position is
    // sum(weights[i] * palette[joints[i]]) * position. A DS vertex uses a single
    // matrix (joints[0], weight 1); glTF vertices can blend up to four bones.
    std::array<std::uint32_t, 4> joints{0U, 0U, 0U, 0U};
    std::array<float, 4> weights{1.0F, 0.0F, 0.0F, 0.0F};
};

// One decoded mesh: a flat vertex list plus triangle indices. Vertices are not
// deduplicated; each GPU vertex command appends exactly one entry.
struct NeutralMesh final {
    // Its GPU command list's place from the model's start: where the game's draw sends it from.
    std::uint32_t list_offset = 0;
    std::string name;
    // Name of the TEX0 texture bound by the mesh's material, or empty if the
    // material has no texture. Resolve it with the TEX0 loader on the same file.
    std::string texture_name;
    // The material's own name (what material animations address) and its base
    // polygon alpha in 0..1. The DS does not draw a mesh whose alpha is 0.
    std::string material_name;
    float material_alpha = 1.0F;
    // Whether the material draws both faces (glTF doubleSided); otherwise only
    // the front, wound counter-clockwise.
    bool double_sided = false;
    // How the material takes its texture's alpha (glTF alphaMode): `unknown`
    // where the file does not say (FBX, a DS model's), `opaque` (alpha
    // ignored), `mask` (a texel is drawn when its alpha reaches
    // `alpha_cutoff`, else not at all) or `blend` (translucent).
    enum class AlphaMode { unknown, opaque, mask, blend };
    AlphaMode alpha_mode = AlphaMode::unknown;
    float alpha_cutoff = 0.5F;
    // A DS model's texture coordinates past 0..1 on S and T: repeated, and
    // repeated mirrored (TEXIMAGE_PARAM), else clamped to the edge.
    std::array<bool, 2> repeat{true, true};
    std::array<bool, 2> mirror{false, false};
    // An imported room's moving textures (its glTF's extras): texture coordinates (texels) added each
    // game frame, and textures shown in turn (name, game frames), from texture_frames_loop_from again
    // after the last when looping. `sky`: drawn around the camera, behind everything.
    std::array<float, 2> scroll{0.0F, 0.0F};
    std::vector<std::pair<std::string, int>> texture_frames;
    bool texture_frames_loop = true;
    std::size_t texture_frames_loop_from = 0;
    bool sky = false;
    std::vector<NeutralVertex> vertices;
    std::vector<std::uint32_t> indices;  // 3 per triangle
};

struct NeutralModel final {
    std::string name;
    std::vector<NeutralMesh> meshes;

    // The rest-pose matrix palette produced by executing the model's render
    // command stream (SBC). Each matrix is column-major (m[col * 4 + row]).
    // Vertices reference palette entries through NeutralVertex::joints.
    std::vector<std::array<float, 16>> palette;

    // Rest-pose bone (object) matrices, column-major. Replace entries with
    // animated matrices and call compute_palette() to re-pose the model.
    std::vector<std::array<float, 16>> object_matrices;

    // Name of each bone (object), aligned with object_matrices. Used to map a
    // glTF skin's joints onto this skeleton by name for animation retargeting.
    std::vector<std::string> object_names;

    // Each bone's parent as the render stream's NODEDESC gives it, aligned
    // with object_names; -1 for a root (a node that is its own parent) or a
    // bone the stream never reaches.
    std::vector<int> object_parents;

    // What each palette entry stands for: the bones whose matrices it is made
    // of and their weights -- one bone with weight 1 for a bone's own matrix,
    // several for an envelope (NODEMIX); empty for the identity.
    std::vector<std::vector<std::pair<std::uint32_t, float>>> palette_bones;

    // The program that rebuilds the palette from bone matrices (for animation).
    std::shared_ptr<const SkinningProgram> skinning;

    // Vertex count reported by the MDL0 model header, for cross-checking.
    std::uint16_t header_vertex_count = 0;
};

// Recompute the matrix palette from a set of bone (object) matrices, e.g. from
// an animation. The result lines up index-for-index with NeutralModel::palette,
// so NeutralVertex::joints stays valid.
std::vector<std::array<float, 16>> compute_palette(const SkinningProgram& program,
                                                   const std::vector<std::array<float, 16>>& object_matrices);

// Compute each bone (object)'s world matrix by replaying the model's render
// program with the given bone matrices. The result is aligned index-for-index
// with NeutralModel::object_names / object_matrices (unreached bones stay
// identity). Use it to drive a glTF skin's joints (jointGlobal = bone world) so
// DS animations retarget onto glTF geometry.
std::vector<std::array<float, 16>>
compute_bone_world_matrices(const SkinningProgram& program, const std::vector<std::array<float, 16>>& object_matrices);

// Decode the geometry of one model inside a BMD0/NSBMD file into neutral
// meshes, executing the render command stream so bones and skinning are
// applied. Throws std::runtime_error on malformed data.
NeutralModel decode_model_geometry(const std::filesystem::path& input_path, std::size_t model_index = 0);
// Decode from an in-memory BMD0/NSBMD (e.g. carved out of a KAPH/pack).
NeutralModel decode_model_geometry(const std::uint8_t* data, std::size_t size, std::size_t model_index = 0);

}  // namespace khdays::assets
