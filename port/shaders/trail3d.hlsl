// A KINGDOM HEARTS II enemy's screen trail (an event sequence's command 0x17, as KH2's FUN_14015f250 draws it: the
// screen taken, then laid over the next frame as a quad and taken again), over the 3D just drawn
// (host/render/gpu_raster3d.c): the trail so far is laid over this frame's 3D at `alpha`, as a quad `size` of the view
// about its middle and turned `angle`, read between its texels; what comes out is the next frame's trail. A texel
// counts by its alpha, as fxaa3d.hlsl's blend: a trail over no 3D shows by its own; the new texel's flags (over the
// 2D, translucent) stay.

[[vk::image_format("r32ui")]] RWTexture2D<uint> source : register(u0, space1); // this frame's 3D (read only here)
[[vk::image_format("r32ui")]] RWTexture2D<uint> before : register(u1, space1); // the trail so far (read only here)
[[vk::image_format("r32ui")]] RWTexture2D<uint> result : register(u2, space1);

cbuffer Params : register(b0, space2) {
    uint width; // the targets', texels
    uint height;
    uint fine;  // 1: channels in 8 bits, else the DS's 6 (raster3d.hlsl's colour)
    uint first; // 1: no trail yet, this frame's 3D starts it
    float alpha; // the trail's share
    float size_x; // its quad's width and height over the view's
    float size_y;
    float angle; // its turn, radians
};

float4 rgba(uint c)
{
    const float most = fine != 0u ? 255.0f : 63.0f;
    const uint mask = fine != 0u ? 255u : 63u;
    return float4((float)(c & mask) / most, (float)((c >> 8) & mask) / most, (float)((c >> 16) & mask) / most,
                  (float)((c >> 24) & 31u) / 31.0f);
}

// The trail at a place of its own picture, its colour weighted by its alpha (0 outside it)
float4 premultiplied(int2 p)
{
    if (p.x < 0 || p.y < 0 || p.x >= (int)width || p.y >= (int)height) {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float4 c = rgba(before[uint2(p)]);
    return float4(c.rgb * c.a, c.a);
}

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) {
        return;
    }
    const uint now = source[id.xy];
    if (first != 0u) {
        result[id.xy] = now;
        return;
    }
    // this texel's place on the quad: back from the view's middle, turned back, shrunk by the quad's size
    const float2 middle = float2((float)width, (float)height) * 0.5f;
    const float2 d = float2((float)id.x + 0.5f, (float)id.y + 0.5f) - middle;
    const float c = cos(angle), s = sin(angle);
    const float2 q = float2(c * d.x + s * d.y, -s * d.x + c * d.y) / float2(size_x, size_y) + middle - 0.5f;
    const int2 q0 = (int2)floor(q);
    const float2 f = q - (float2)q0;
    const float4 t = premultiplied(q0) * (1.0f - f.x) * (1.0f - f.y) + premultiplied(q0 + int2(1, 0)) * f.x * (1.0f - f.y) +
                     premultiplied(q0 + int2(0, 1)) * (1.0f - f.x) * f.y + premultiplied(q0 + int2(1, 1)) * f.x * f.y;
    const float4 n = rgba(now);
    // the GS's (Cs - Cd) As + Cd: the trail the source, its share `alpha`
    const float wn = (1.0f - alpha) * n.a, wt = alpha;
    const float a = wn + wt * t.a;
    const float3 rgb = a > 0.0f ? (n.rgb * wn + t.rgb * wt) / a : n.rgb;
    const float most = fine != 0u ? 255.0f : 63.0f;
    const uint r = (uint)(saturate(rgb.r) * most + 0.5f), g = (uint)(saturate(rgb.g) * most + 0.5f),
               b = (uint)(saturate(rgb.b) * most + 0.5f);
    result[id.xy] = r | (g << 8) | (b << 16) | ((uint)(saturate(a) * 31.0f + 0.5f) << 24) | (now & 0xe0000000u);
}
