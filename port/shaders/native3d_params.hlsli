// The native 3D renderer's constant buffer (host/render/gpu_raster3d.c NativeParams): the same for every
// stage of native3d.hlsl, native3d_rear.hlsl and native3d_pack.hlsl, at each stage's register.

#if defined(STAGE_VS)
#define PARAMS_REGISTER register(b0, space1)
#elif defined(STAGE_PS)
#define PARAMS_REGISTER register(b0, space3)
#else
#define PARAMS_REGISTER register(b0, space2)
#endif

cbuffer Params : PARAMS_REGISTER {
    uint scale;       // N
    uint width;       // the target, pixels
    uint height;
    uint disp3dcnt;
    uint swap_params;
    uint alpha_ref;   // ALPHA_TEST_REF
    uint clear_color; // CLEAR_COLOR
    uint clear_depth; // CLEAR_DEPTH, CLRIMAGE_OFFSET << 16
    uint smooth;      // bit 0: texture smoothing (not the DS's); bit 1: screen space stretched to the target
    int origin_x;     // the DS column of the target's first column (0 at 4:3)
    uint fine;        // 1: colours in 8 bits a channel (HD textures drawn), not the DS's 6
    uint outputs;     // the targets written besides 0 and 4 (gpu_native3d.c OUT_*): 1 first, 2 fog, 4 IDs, 8 depth
    float4 to_clip;   // a vertex's x to the clip space's: x * to_clip.x + to_clip.y; its y: y * to_clip.z + to_clip.w
    uint4 toon[4];    // TOON_TABLE, two entries per uint
};
