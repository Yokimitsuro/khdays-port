#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace khdays::assets {

struct GpuCommandSummary final {
    std::size_t packet_count = 0;
    std::size_t command_count = 0;
    std::size_t vertex_command_count = 0;
    std::map<std::string, std::size_t> opcode_counts;
};

struct MeshInfo final {
    std::string name;
    std::uint32_t command_offset = 0;
    std::uint32_t command_length = 0;
    GpuCommandSummary gpu_commands;
};

struct ModelInfo final {
    std::string name;
    std::uint32_t model_size = 0;
    std::uint32_t render_commands_offset = 0;
    std::uint32_t materials_offset = 0;
    std::uint32_t meshes_offset = 0;
    std::uint32_t inverse_bind_matrices_offset = 0;

    std::uint8_t bone_matrix_count = 0;
    std::uint8_t material_count = 0;
    std::uint8_t mesh_count = 0;

    float up_scale = 1.0F;
    float down_scale = 1.0F;

    std::uint16_t vertex_count = 0;
    std::uint16_t polygon_count = 0;
    std::uint16_t triangle_count = 0;
    std::uint16_t quad_count = 0;

    float bounding_x = 0.0F;
    float bounding_y = 0.0F;
    float bounding_z = 0.0F;
    float bounding_width = 0.0F;
    float bounding_height = 0.0F;
    float bounding_depth = 0.0F;

    std::vector<std::string> material_names;
    std::vector<MeshInfo> meshes;
};

struct ModelFileInfo final {
    std::filesystem::path source_path;
    std::vector<ModelInfo> models;
};

ModelFileInfo inspect_mdl0(const std::filesystem::path& input_path);

std::string format_model_report(const ModelFileInfo& file_info);

}  // namespace khdays::assets
