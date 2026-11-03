// The native 3D renderer's rear plane (native3d.hlsl's targets): one triangle over the whole target, each
// pixel the clear colour, depth and polygon ID, or the clear image's (gpu3d.c rear_plane). The same
// triangle clears stencil bits between the list's polygons, with no target written.

ByteAddressBuffer polys : register(t0, space2); // native3d.hlsl's, bound for the whole pass
ByteAddressBuffer verts : register(t1, space2);
ByteAddressBuffer vram : register(t2, space2);
ByteAddressBuffer hd : register(t3, space2);

#include "native3d_params.hlsli"
#include "g3d_vram.hlsli"

float4 vs_main(uint id : SV_VertexID) : SV_Position
{
    const float2 at = float2((float)((id << 1) & 2u), (float)(id & 2u));
    return float4(at.x * 2.0f - 1.0f, 1.0f - at.y * 2.0f, 0.5f, 1.0f);
}

struct Targets {
    float4 colour : SV_Target0;
    float4 first : SV_Target1;
    float4 fog : SV_Target2;
    float4 opaque : SV_Target3;
    float4 flags : SV_Target4;
    uint depth24 : SV_Target5;
    float depth : SV_Depth;
};

Targets ps_main(float4 at : SV_Position)
{
    const RearPlane r = rear_plane_at((int)at.x / (int)scale + origin_x, (uint)at.y / scale);
    const bool shown = r.color.w > 0;
    Targets o;
    // an empty pixel (alpha 0) takes the first translucent colour drawn on it whole; a shown one is the
    // colour every later one blends with, as if that first colour were black
    o.colour = shown ? float4((float3)r.color.xyz / (fine != 0u ? 255.0f : 63.0f), 0.0f) : float4(0.0f, 0.0f, 0.0f, 1.0f);
    o.first = shown ? float4(0.0f, 0.0f, 0.0f, 1.0f) : float4(0.0f, 0.0f, 0.0f, 0.0f);
    o.fog = float4((float)r.fog, 0.0f, 0.0f, 1.0f);
    o.opaque = float4((float)((clear_color >> 24) & 63u) / 255.0f, 0.0f, 0.0f, 1.0f);
    o.flags = float4(0.0f, 0.0f, 0.0f, (float)r.color.w / 31.0f);
    o.depth24 = r.depth;
    o.depth = (float)r.depth / 16777216.0f;
    return o;
}
