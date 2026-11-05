// native3d.hlsl's ps_main() with the depth the card's rasterizer interpolates (the vertex shader's, the DS's
// Z buffer's) instead of the shader's own, so that the card tests it before shading: for the pictures with
// the Z buffer, but the polygons with the depth test "equal" (gpu_native3d.c).
#define NATIVE3D_RASTER_DEPTH
#include "native3d.hlsl"
