// The native 3D renderer's targets (native3d.hlsl) as raster3d.hlsl's two, which finish3d.hlsli and the
// composition read: the colour with the first translucent one over an empty pixel added back, the rear
// plane's where nothing was drawn. The targets the picture did not write (`outputs`) are not read.

ByteAddressBuffer vram : register(t0, space0);

[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> colour : register(u0, space1); // read only here
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> first : register(u1, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> fog : register(u2, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> opaque : register(u3, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> flags : register(u4, space1);
[[vk::image_format("r32f")]] RWTexture2D<float> depth24 : register(u5, space1); // a whole number, exact
[[vk::image_format("r32ui")]] RWTexture2D<uint> out_color : register(u6, space1);
[[vk::image_format("r32ui")]] RWTexture2D<uint> out_attr : register(u7, space1);

#include "native3d_params.hlsli"
#include "g3d_vram.hlsli"

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) {
        return;
    }
    {
        const float4 c = colour[id.xy], fl = flags[id.xy];
        const float4 f = (outputs & 1u) != 0u ? first[id.xy] : float4(0.0f, 0.0f, 0.0f, 0.0f);
        const float4 ids = (outputs & 4u) != 0u ? opaque[id.xy] : float4((float)((clear_color >> 24) & 63u) / 255.0f, 0.0f, 0.0f, 0.0f);
        const uint fogged = (outputs & 2u) != 0u ? (uint)round(fog[id.xy].r) : 0u;
        const uint depth = (outputs & 8u) != 0u ? (uint)depth24[id.xy] & 0xffffffu : 0xffffffu;
        const int a = (int)round(fl.a * 31.0f);
        const float cmax = fine != 0u ? 255.0f : 63.0f;
        int3 rgb;
        if (a == 0) {
            rgb = rear_plane_at((int)id.x / (int)scale + origin_x, id.y / scale).color.xyz;
        } else {
            // the DS's blending truncates: so does this, a little over the stored colour's rounding (an
            // opaque colour comes out exact)
            rgb = (int3)clamp(floor((c.rgb + f.rgb * c.a) * cmax + 0.13f), 0.0f, cmax);
        }
        out_color[id.xy] = (uint)rgb.x | ((uint)rgb.y << 8) | ((uint)rgb.z << 16) | ((uint)a << 24) |
                           ((uint)round(fl.g) << 29) | ((uint)round(fl.r) << 31);
        out_attr[id.xy] = depth | ((uint)round(ids.r * 255.0f) << 24) | ((uint)round(ids.g) << 30) | (fogged << 31);
    }
}
