#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace khdays::assets {

// Opaque render program (the model's SBC plus inverse binds and scales) that
// rebuilds the matrix palette from a set of bone matrices. Defined in mesh.cpp.
struct SkinningProgram;

// A single decoded vertex in a neutral, engine-independent form.
//
// The position is the *raw* coordinate emitted by the GPU command stream, in
// the local space of whichever matrix applies to it. To get the final rest-pose
// position, blend the model's palette matrices selected by `joints` with their
// `weights` and apply the result. Keeping the raw position plus the palette
// indices (instead of baking the transform in) is what lets the runtime re-pose
// the model for animation, exactly as the Nintendo DS does.
//
// Texture coordinates are in *texels* (not normalized): divide by the bound
// texture's width and height to get 0..1 UVs.
struct NeutralVertex final {
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 2> texcoord{0.0F, 0.0F};
    std::array<std::uint8_t, 4> color{255U, 255U, 255U, 255U};

    // Skinning: up to four palette matrices with weights. The final position is
    // sum(weights[i] * palette[joints[i]]) * position. A DS vertex uses a single
    // matrix (joints[0], weight 1); glTF vertices can blend up to four bones.
    std::array<std::uint32_t, 4> joints{0U, 0U, 0U, 0U};
    std::array<float, 4> weights{1.0F, 0.0F, 0.0F, 0.0F};
};

// One decoded mesh: a flat vertex list plus triangle indices. Vertices are not
// deduplicated; each GPU vertex command appends exactly one entry.
struct NeutralMesh final {
    std::string name;
    // Name of the TEX0 texture bound by the mesh's material, or empty if the
    // material has no texture. Resolve it with the TEX0 loader on the same file.
    std::string texture_name;
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

    // The program that rebuilds the palette from bone matrices (for animation).
    std::shared_ptr<const SkinningProgram> skinning;

    // Vertex count reported by the MDL0 model header, for cross-checking.
    std::uint16_t header_vertex_count = 0;
};

// Recompute the matrix palette from a set of bone (object) matrices, e.g. from
// an animation. The result lines up index-for-index with NeutralModel::palette,
// so NeutralVertex::joints stays valid.
std::vector<std::array<float, 16>> compute_palette(
    const SkinningProgram& program,
    const std::vector<std::array<float, 16>>& object_matrices);

// Compute each bone (object)'s world matrix by replaying the model's render
// program with the given bone matrices. The result is aligned index-for-index
// with NeutralModel::object_names / object_matrices (unreached bones stay
// identity). Use it to drive a glTF skin's joints (jointGlobal = bone world) so
// DS animations retarget onto glTF geometry.
std::vector<std::array<float, 16>> compute_bone_world_matrices(
    const SkinningProgram& program,
    const std::vector<std::array<float, 16>>& object_matrices);

// One glTF skin joint bound to a DS bone, for animation retargeting. The glTF
// geometry then animates from DS bone matrices: its palette entry becomes
// bone_world[ds_bone] * inverse_bind. An unmatched joint (ds_bone < 0) keeps
// its authored `rest` value.
struct RetargetJoint final {
    std::uint32_t palette_index;
    int ds_bone = -1;
    std::array<float, 16> inverse_bind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16> rest{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// Build a skinning program that re-poses a glTF palette from a DS skeleton.
// `ds_program` supplies the DS render stream (used to compute bone worlds from
// animated bone matrices); `joints` binds each glTF joint to a DS bone.
// compute_palette() on the result yields the glTF palette for a given set of DS
// bone matrices, so the same animation path drives DS and glTF geometry alike.
std::shared_ptr<const SkinningProgram> make_retarget_program(
    const SkinningProgram& ds_program,
    std::vector<RetargetJoint> joints,
    std::size_t palette_size);

// Decode the geometry of one model inside a BMD0/NSBMD file into neutral
// meshes, executing the render command stream so bones and skinning are
// applied. Throws std::runtime_error on malformed data.
NeutralModel decode_model_geometry(
    const std::filesystem::path& input_path,
    std::size_t model_index = 0);
// Decode from an in-memory BMD0/NSBMD (e.g. carved out of a KAPH/pack).
NeutralModel decode_model_geometry(
    const std::uint8_t* data, std::size_t size, std::size_t model_index = 0);

// Apply a column-major 4x4 matrix to a point (affine; the bottom row is assumed
// to be 0 0 0 1). Convenience for consumers that want rest-pose positions.
std::array<float, 3> transform_point(
    const std::array<float, 16>& matrix,
    const std::array<float, 3>& point);

// Rest-pose position of a vertex: the weighted blend of palette[joints[i]]
// applied to the raw position.
std::array<float, 3> posed_position(
    const NeutralModel& model,
    const NeutralVertex& vertex);

// Serialize a decoded model as a Wavefront OBJ document, writing rest-pose
// positions. Each mesh becomes a named group.
std::string to_wavefront_obj(const NeutralModel& model);

}  // namespace khdays::assets
