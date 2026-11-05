// Animated overhead silhouettes: union coverage, including material and texture alpha.
ByteAddressBuffer polys : register(t0, space0);
ByteAddressBuffer verts : register(t1, space0);
ByteAddressBuffer tiles : register(t2, space0);
ByteAddressBuffer vram : register(t3, space0);
ByteAddressBuffer hd : register(t4, space0);
RWByteAddressBuffer mask : register(u0, space1);
cbuffer Params : register(b0, space2) { uint groups, npolys, nverts, entries; };
#define TEX_PAL 0x80000u
#include "geometry_load.hlsli"
#include "texture_decode.hlsli"
float edge(float2 a,float2 b,float2 p) { return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x); }
[numthreads(8,8,1)]
void cs_main(uint3 id : SV_DispatchThreadID) {
    if (id.x>=128u || id.y>=128u || id.z>=groups) return;
    const uint tile=id.z*256u+(id.y/8u)*16u+id.x/8u;
    const uint begin=tiles.Load(tile*4u), end=tiles.Load((tile+1u)*4u);
    float coverage=0;
    const float2 sample_at=float2(id.xy)+0.5f;
    for (uint i=begin;i<min(end,entries);++i) {
        const uint index=tiles.Load(i*4u);
        if (index>=npolys) continue;
        const Polygon p=polygon_at(index);
        if (p.first<0 || p.count<3 || p.first+p.count>(int)nverts) continue;
        const Vertex a=vertex_at(p.first);
        for (int t=1;t<p.count-1;++t) {
            const Vertex b=vertex_at(p.first+t), c=vertex_at(p.first+t+1);
            const float2 aa=float2(a.x,a.y), bb=float2(b.x,b.y), cc=float2(c.x,c.y);
            const float area=edge(aa,bb,cc);
            if (abs(area)<1e-8f) continue;
            const float3 bary=float3(edge(bb,cc,sample_at),edge(cc,aa,sample_at),edge(aa,bb,sample_at))/area;
            if (any(bary<0.0f)) continue;
            const float2 uv=(bary.x*float2(a.s,a.t)+bary.y*float2(b.s,b.t)+bary.z*float2(c.s,c.t))/16.0f;
            float alpha=saturate(dot(bary,float3(a.a,b.a,c.a)))*(float)((p.attr>>16)&31u)/31.0f;
            if (((p.tex>>26)&7u)!=0u) {
                if (p.replacement>=0) {
                    const uint4 e=hd.Load4((uint)p.replacement*16u);
                    const int sw=8<<((p.tex>>20)&7u), sh=8<<((p.tex>>23)&7u);
                    const int x=wrap((int)floor(uv.x*(float)e.y/(float)sw),(int)e.y,p.tex&0x10000u,p.tex&0x40000u);
                    const int y=wrap((int)floor(uv.y*(float)e.z/(float)sh),(int)e.z,p.tex&0x20000u,p.tex&0x80000u);
                    alpha*=(float)(hd.Load((e.x+(uint)y*e.y+(uint)x)*4u)>>24)/255.0f;
                } else alpha*=(float)((texel(p.tex,p.pltt,(int)floor(uv.x),(int)floor(uv.y))>>16)&31u)/31.0f;
            }
            coverage=max(coverage,alpha);
        }
    }
    mask.Store((id.z*16384u+id.y*128u+id.x)*4u,(uint)round(coverage*255.0f));
}
