#version 450

// Fragment shader for the native model viewer. Vertex-color lit shading; the
// textured path (per-material TEX0 sampler in set 2) is a follow-up.
layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) in float in_shade;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(in_color.rgb * in_shade, in_color.a);
}
