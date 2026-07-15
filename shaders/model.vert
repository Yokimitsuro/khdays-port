#version 450

// Vertex shader for the native model viewer. Resource sets follow the SDL3 GPU
// SPIR-V convention: uniform buffers live in set 1 for the vertex stage.
layout(set = 1, binding = 0) uniform UBO {
    mat4 mvp;
    mat4 model;      // rotation only, for transforming normals
    vec4 light_dir;  // world-space light direction (xyz)
} ubo;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec3 in_normal;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) out float out_shade;

void main() {
    gl_Position = ubo.mvp * vec4(in_position, 1.0);
    out_uv = in_uv;
    out_color = in_color;

    vec3 n = normalize(mat3(ubo.model) * in_normal);
    // Two-sided shading: the decoded meshes do not have consistent winding.
    float diffuse = abs(dot(n, normalize(ubo.light_dir.xyz)));
    out_shade = 0.35 + 0.65 * diffuse;
}
