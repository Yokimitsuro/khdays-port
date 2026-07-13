#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace khdays::assets {

// A single decoded vertex in a neutral, engine-independent form. Positions are
// in the model's local command space (skinning matrices are not yet applied),
// texture coordinates are normalized to 0..1 when the texture size is known,
// and colors are expanded to 8 bits per channel.
struct NeutralVertex final {
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 2> texcoord{0.0F, 0.0F};
    std::array<std::uint8_t, 4> color{255U, 255U, 255U, 255U};

    // Index of the matrix restored by the most recent MTX_RESTORE command, or
    // -1 when no matrix was selected. Retained for future skinning; it does not
    // affect the emitted position yet.
    int matrix_id = -1;
};

// One decoded mesh: a flat vertex list plus triangle indices. Vertices are not
// deduplicated; each GPU vertex command appends exactly one entry, so
// vertices.size() equals the mesh's vertex-command count.
struct NeutralMesh final {
    std::string name;
    std::vector<NeutralVertex> vertices;
    std::vector<std::uint32_t> indices;  // 3 per triangle
};

struct NeutralModel final {
    std::string name;
    std::vector<NeutralMesh> meshes;

    // Vertex count reported by the MDL0 model header, for cross-checking the
    // decoded geometry.
    std::uint16_t header_vertex_count = 0;
};

// Decode the geometry of one model inside a BMD0/NSBMD file into neutral
// meshes. Throws std::runtime_error on malformed data.
NeutralModel decode_model_geometry(
    const std::filesystem::path& input_path,
    std::size_t model_index = 0);

// Serialize a decoded model as a Wavefront OBJ document. Each mesh becomes a
// named group. UVs are written as decoded; vertex colors are not part of the
// core OBJ format and are omitted.
std::string to_wavefront_obj(const NeutralModel& model);

}  // namespace khdays::assets
