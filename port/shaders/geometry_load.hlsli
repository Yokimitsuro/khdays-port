// Geometry list layouts shared with gfx/g3d.h.
struct Polygon {
    uint attr, tex, pltt;
    int first, count;
    float ymin, ymax;
    int translucent;
    int flags; // gfx/g3d.h KHDAYS_G3D_*: OVER_2D marks its pixels (bit 29 of the colour), the rest the host reads
    int replacement; // the replacement texture drawn instead (host/render/gpu_hd.h's entry), or -1
};

struct Vertex {
    float x, y, z, w, r, g, b, s, t, a;
};

// gfx/g3d.h's KhdaysG3dPolygon (40 bytes) and KhdaysG3dVertex (40 bytes), word by word
Polygon polygon_at(uint i)
{
    const uint4 a = polys.Load4(i * 40u), b = polys.Load4(i * 40u + 16u);
    const uint2 c = polys.Load2(i * 40u + 32u);
    Polygon p;
    p.attr = a.x;
    p.tex = a.y;
    p.pltt = a.z;
    p.first = (int)a.w;
    p.count = (int)b.x;
    p.ymin = asfloat(b.y);
    p.ymax = asfloat(b.z);
    p.translucent = (int)b.w;
    p.flags = (int)c.x;
    p.replacement = (int)c.y;
    return p;
}

Vertex vertex_at(int i)
{
    const uint at = (uint)i * 40u;
    const float4 a = asfloat(verts.Load4(at)), b = asfloat(verts.Load4(at + 16u));
    Vertex v;
    v.x = a.x;
    v.y = a.y;
    v.z = a.z;
    v.w = a.w;
    v.r = b.x;
    v.g = b.y;
    v.b = b.z;
    v.s = b.w;
    const uint2 c = verts.Load2(at + 32u);
    v.t = asfloat(c.x);
    v.a = asfloat(c.y);
    return v;
}
