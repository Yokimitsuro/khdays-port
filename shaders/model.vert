#version 450

// Vertex shader for the native model viewer, with GPU skinning: each vertex
// carries a raw (bone-local) position and a palette index; the skinning matrix
// palette is a storage buffer updated per frame. Resource sets follow the SDL3
// GPU SPIR-V convention (storage buffers in set 0, uniform buffers in set 1 for
// the vertex stage).
layout(std430, set = 0, binding = 0) readonly buffer Palette {
    mat4 palette[];
};

layout(set = 1, binding = 0) uniform UBO {
    mat4 mvp;        // model(centering+camera) * view * projection
    mat4 model;      // camera-space rotation, for transforming normals
    vec4 light_dir;  // world-space light direction (xyz)
} ubo;

layout(location = 0) in vec3 in_position;   // raw, bone-local
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec3 in_normal;     // raw, bone-local
layout(location = 4) in uvec4 in_joints;    // up to 4 palette indices
layout(location = 5) in vec4 in_weights;    // their weights

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) out float out_shade;

void main() {
    mat4 skin = in_weights.x * palette[in_joints.x]
              + in_weights.y * palette[in_joints.y]
              + in_weights.z * palette[in_joints.z]
              + in_weights.w * palette[in_joints.w];
    vec4 world = skin * vec4(in_position, 1.0);
    gl_Position = ubo.mvp * world;

    out_uv = in_uv;
    out_color = in_color;

    vec3 n = normalize(mat3(ubo.model) * mat3(skin) * in_normal);
    // Two-sided shading: the decoded meshes have inconsistent winding.
    float diffuse = abs(dot(n, normalize(ubo.light_dir.xyz)));
    out_shade = 0.35 + 0.65 * diffuse;
}
