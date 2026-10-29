// Shared SEQD fragment sampling. The pass supplies seq_word(program, word) and seq_hd_word(byte).
#ifndef KHDAYS_SEQUENCE_SAMPLE
#define KHDAYS_SEQUENCE_SAMPLE
#define KSI_SHADER
#include "../gfx/sequence_image.h"
float4 seq_unpack(uint t) { return float4(t & 255u, (t >> 8) & 255u, (t >> 16) & 255u, t >> 24); }
uint seq_pack(float4 c) {
    uint4 p = (uint4)(clamp(c, 0.0f, 255.0f) + 0.5f);
    return p.x | (p.y << 8) | (p.z << 16) | (p.w << 24);
}
float seq_number(uint program, uint at) { return asfloat(seq_word(program, at)); }
float2 seq_pair(uint program, uint at) { return float2(seq_number(program, at), seq_number(program, at+1u)); }
float4 seq_texel(uint image, int2 p) {
    uint first=seq_hd_word(image*16u), w=seq_hd_word(image*16u+4u), h=seq_hd_word(image*16u+8u);
    p=clamp(p,int2(0,0),int2(w,h)-1);
    return seq_unpack(seq_hd_word((first+(uint)p.y*w+(uint)p.x)*4u));
}
float4 seq_texture(uint image, float2 uv, bool filtered) {
    float2 size=float2(seq_hd_word(image*16u+4u),seq_hd_word(image*16u+8u));
    uv=clamp(uv,0.0f,size);
    if (!filtered) return seq_texel(image,(int2)floor(uv));
    uv-=0.5f;
    int2 p=(int2)floor(uv);
    float2 f=uv-(float2)p;
    return lerp(lerp(seq_texel(image,p),seq_texel(image,p+int2(1,0)),f.x),
                lerp(seq_texel(image,p+int2(0,1)),seq_texel(image,p+1),f.x),f.y);
}
float4 seq_over(float4 dst, float4 src) {
    float a=src.a/255.0f, b=dst.a/255.0f*(1.0f-a), total=a+b;
    return total==0.0f ? 0.0f : float4((src.rgb*a+dst.rgb*b)/total,total*255.0f);
}
float4 seq_blend(float4 dst, float4 src, uint blend) {
    float4 result=seq_over(dst,src);
    if (blend==1u) result.rgb=dst.rgb+src.rgb*src.a/255.0f;
    else if (blend==2u) result.rgb=dst.rgb-src.rgb*src.a/255.0f;
    return seq_unpack(seq_pack(result));
}
bool seq_sample(uint program, uint q, float2 at, inout uint depth, out float4 src, out uint blend) {
    src=0.0f; blend=0u;
    uint base=KSI_HEADER+q*KSI_QUAD_WORDS;
    float scale=seq_number(program,KSI_SCALE);
    float2 view=seq_pair(program,KSI_VIEW_X);
    precise float2 p0=seq_pair(program,base)*scale+view;
    precise float2 a=(seq_pair(program,base+5u)*scale+view)-p0;
    precise float2 b=(seq_pair(program,base+10u)*scale+view)-p0;
    precise float det=a.x*b.y-a.y*b.x;
    if (abs(det)<1e-8f) return false;
    precise float2 delta=at-p0;
    precise float sn=delta.x*b.y-delta.y*b.x, tn=a.x*delta.y-a.y*delta.x;
    // Test half-open edges before reciprocal approximation can round an exact 1 down.
    if (det>0.0f ? (sn<0.0f || sn>=det || tn<0.0f || tn>=det)
                 : (sn>0.0f || sn<=det || tn>0.0f || tn<=det)) return false;
    precise float s=sn/det, t=tn/det;
    uint flags=seq_word(program,base+KSI_FLAGS), z=seq_word(program,base+KSI_DEPTH);
    if ((flags&1u)!=0u && z<depth) return false;
    if ((flags&2u)!=0u) depth=z;
    if ((flags&4u)==0u) return false;
    float4 c0=seq_unpack(seq_word(program,base+4u)), c1=seq_unpack(seq_word(program,base+9u));
    float4 c2=seq_unpack(seq_word(program,base+14u)), c3=seq_unpack(seq_word(program,base+19u));
    float4 tint=s+t<=1.0f ? c0+(c1-c0)*s+(c2-c0)*t : c3+(c2-c3)*(1.0f-s)+(c1-c3)*(1.0f-t);
    src=255.0f;
    if ((flags&8u)!=0u) {
        uint atlas=seq_word(program,KSI_ATLAS);
        bool radar_icon=seq_word(program,KSI_RADAR_MASK)!=0u && q>0u;
        if(radar_icon) atlas=seq_word(program,KSI_RADAR_ICONS);
        float2 ratio=float2(seq_hd_word(atlas*16u+4u),seq_hd_word(atlas*16u+8u)) /
                     (radar_icon?float2(seq_word(program,KSI_RADAR_ICON_W),seq_word(program,KSI_RADAR_ICON_H)):
                      float2(seq_word(program,KSI_LOGICAL_W),seq_word(program,KSI_LOGICAL_H)));
        float2 uv0=seq_pair(program,base+2u), uv1=seq_pair(program,base+7u), uv2=seq_pair(program,base+12u);
        src=seq_texture(atlas,(uv0+(uv1-uv0)*s+(uv2-uv0)*t)*ratio,(flags&16u)!=0u);
    }
    src=clamp(src*tint/128.0f,0.0f,255.0f);
    blend=seq_word(program,base+KSI_BLEND);
    return true;
}
#endif
