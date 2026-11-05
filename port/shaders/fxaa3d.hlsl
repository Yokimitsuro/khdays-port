// The antialiasing setting (FXAA, after Timothy Lottes' published method): once the 3D is drawn
// (host/render/gpu_raster3d.c), each texel of its colour whose neighbourhood shows an edge is blended
// with the texel across it, by where it lies along the edge's run, so stair steps come out smooth at
// the 3D's own resolution, without drawing it larger. A texel counts by its alpha: a model's outline
// over the 2D is smoothed into partial alpha, a texel without 3D gives no colour. The flags (over the
// 2D, translucent) stay the texel's own; edge marking and fog, read from the other target, come after.

[[vk::image_format("r32ui")]] RWTexture2D<uint> source : register(u0, space1); // read only here
[[vk::image_format("r32ui")]] RWTexture2D<uint> result : register(u1, space1);

cbuffer Params : register(b0, space2) {
    uint width; // the targets', texels
    uint height;
    uint fine;  // 1: channels in 8 bits, else the DS's 6 (raster3d.hlsl's colour)
    uint unused;
};

static const float EDGE_CONTRAST = 0.125f;     // the change an edge needs, of the brightest neighbour's
static const float EDGE_CONTRAST_LEAST = 0.03f; // and at least this (dark places left alone)
static const float SUBPIXEL = 0.75f;           // how much a texel finer than one is blended
// the steps along an edge, growing as the search goes further
static const int STEPS = 10;
static const int STEP[STEPS] = {1, 1, 1, 1, 2, 2, 2, 2, 4, 8};

uint texel(int2 p)
{
    return source[uint2(clamp(p.x, 0, (int)width - 1), clamp(p.y, 0, (int)height - 1))];
}

float4 rgba(uint c)
{
    const float most = fine != 0u ? 255.0f : 63.0f;
    const uint mask = fine != 0u ? 255u : 63u;
    return float4((float)(c & mask) / most, (float)((c >> 8) & mask) / most, (float)((c >> 16) & mask) / most,
                  (float)((c >> 24) & 31u) / 31.0f);
}

// The brightness the edges are found by: a texel's, and how much 3D it holds (so an outline over no 3D
// is an edge whatever its colour).
float luma(int2 p)
{
    const float4 c = rgba(texel(p));
    return (0.75f * dot(c.rgb, float3(0.299f, 0.587f, 0.114f)) + 0.25f) * c.a;
}

// `a` and `b` mixed, `t` of `b`, each by its alpha; a's flags kept.
uint blend(uint a, uint b, float t)
{
    const float most = fine != 0u ? 255.0f : 63.0f;
    const float4 ca = rgba(a), cb = rgba(b);
    const float wa = (1.0f - t) * ca.a, wb = t * cb.a, alpha = wa + wb;
    const float3 rgb = alpha > 0.0f ? (ca.rgb * wa + cb.rgb * wb) / alpha : ca.rgb;
    const uint r = (uint)(rgb.r * most + 0.5f), g = (uint)(rgb.g * most + 0.5f), bl = (uint)(rgb.b * most + 0.5f);
    return r | (g << 8) | (bl << 16) | ((uint)(alpha * 31.0f + 0.5f) << 24) | (a & 0xe0000000u);
}

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 p = int2(id.xy);
    const uint centre = texel(p);
    const float m = luma(p);
    const float n = luma(p + int2(0, -1)), s = luma(p + int2(0, 1)), w = luma(p + int2(-1, 0)),
                e = luma(p + int2(1, 0));
    const float least = min(m, min(min(n, s), min(w, e))), most = max(m, max(max(n, s), max(w, e)));
    const float range = most - least;
    if (range < max(EDGE_CONTRAST_LEAST, most * EDGE_CONTRAST)) {
        result[id.xy] = centre;
        return;
    }
    const float nw = luma(p + int2(-1, -1)), ne = luma(p + int2(1, -1)), sw = luma(p + int2(-1, 1)),
                se = luma(p + int2(1, 1));
    // an edge running across (the change going up or down) or down (the change going sideways)
    const float across_change = abs(nw + sw - 2.0f * w) + 2.0f * abs(n + s - 2.0f * m) + abs(ne + se - 2.0f * e);
    const float down_change = abs(nw + ne - 2.0f * n) + 2.0f * abs(w + e - 2.0f * m) + abs(sw + se - 2.0f * s);
    const bool runs_across = across_change >= down_change;
    // the side of the edge with the steeper change, and the texel there
    const float before = runs_across ? n : w, after = runs_across ? s : e;
    const bool to_before = abs(before - m) >= abs(after - m);
    const int2 side = runs_across ? int2(0, to_before ? -1 : 1) : int2(to_before ? -1 : 1, 0);
    const int2 along = runs_across ? int2(1, 0) : int2(0, 1);
    const float edge = 0.5f * (m + (to_before ? before : after));
    const float enough = 0.25f * max(abs(before - m), abs(after - m));
    // along the edge both ways, between this row and the side's, until its brightness leaves the edge's
    int back = 0, ahead = 0;
    float back_end = 0.0f, ahead_end = 0.0f;
    bool back_done = false, ahead_done = false;
    for (int i = 0; i < STEPS && !(back_done && ahead_done); ++i) {
        if (!back_done) {
            back += STEP[i];
            const int2 q = p - along * back;
            back_end = 0.5f * (luma(q) + luma(q + side)) - edge;
            back_done = abs(back_end) >= enough;
        }
        if (!ahead_done) {
            ahead += STEP[i];
            const int2 q = p + along * ahead;
            ahead_end = 0.5f * (luma(q) + luma(q + side)) - edge;
            ahead_done = abs(ahead_end) >= enough;
        }
    }
    // the nearer end: blended by how close to it the texel is, if the change there goes the centre's way
    const bool back_nearer = back < ahead;
    const float nearest = (float)min(back, ahead), run = (float)(back + ahead);
    const bool centre_darker = m < edge;
    const bool goes_right = ((back_nearer ? back_end : ahead_end) < 0.0f) != centre_darker;
    const float by_edge = goes_right ? 0.5f - nearest / run : 0.0f;
    // a texel finer than one (a lone point or line) blended by how far it stands from its neighbours
    const float average = (2.0f * (n + s + w + e) + nw + ne + sw + se) / 12.0f;
    const float apart = saturate(abs(average - m) / range);
    const float smooth = (-2.0f * apart + 3.0f) * apart * apart;
    const float t = max(by_edge, smooth * smooth * SUBPIXEL);
    result[id.xy] = blend(centre, texel(p + side), t);
}
