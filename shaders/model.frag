#version 450

// Fragment shader for the native model viewer: samples the mesh's TEX0 texture
// (set 2 per the SDL3 GPU convention), tinted by the vertex color and shading.
layout(set = 2, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) in float in_shade;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 texel = texture(tex, in_uv);
    out_color = vec4(texel.rgb * in_color.rgb * in_shade, texel.a * in_color.a);
    if (out_color.a < 0.02) {
        discard;
    }
}
