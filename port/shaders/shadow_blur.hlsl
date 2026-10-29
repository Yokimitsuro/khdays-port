// Equal-weight twelve-tap silhouette softening, sampled bilinearly.
ByteAddressBuffer source : register(t0, space0);
RWByteAddressBuffer blurred : register(u0, space1);
cbuffer Params : register(b0, space2) { uint groups, unused0, unused1, unused2; };
float sample_mask(uint group,float2 at) {
    const int2 q=(int2)floor(at); const float2 f=frac(at);
    float value=0;
    for (int k=0;k<4;++k) {
        const int2 a=q+int2(k&1,k>>1);
        if (any(a<0) || any(a>=128)) continue;
        value+=(float)source.Load((group*16384u+(uint)a.y*128u+(uint)a.x)*4u)*
               ((k&1)!=0?f.x:1-f.x)*((k>>1)!=0?f.y:1-f.y);
    }
    return value;
}
[numthreads(8,8,1)]
void cs_main(uint3 id : SV_DispatchThreadID) {
    if (id.x>=128u || id.y>=128u || id.z>=groups) return;
    const float2 offsets[12]={float2(-.5,-.5),float2(-1.5,-1.5),float2(-2.5,0),float2(.5,.5),float2(1.5,1.5),float2(2.5,0),float2(.5,-.5),float2(1.5,-1.5),float2(0,-2.5),float2(-.5,.5),float2(-1.5,1.5),float2(0,2.5)};
    float value=0;
    for (int i=0;i<12;++i) value+=sample_mask(id.z,float2(id.xy)+offsets[i]);
    blurred.Store((id.z*16384u+id.y*128u+id.x)*4u,(uint)round(value/12.0f));
}
