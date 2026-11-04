// Finishes the 3D raster3d.hlsl drew, as port/gpu/gpu3d.c does: edge marking, then fog. The includer
// declares color3d and attr3d (raster3d's targets) and, in its constant buffer, width, height,
// disp3dcnt, swap_params, clear_color, clear_depth, fog_color, fog_offset, edge_colors, fog_table, fine.
// An includer whose 3D is at a scale of its own (the 2D finer than it) defines FINISH3D_SCALED and
// declares width3d, height3d (the targets' texels) and smooth3d: finished3d_at reads it there.

#ifdef FINISH3D_SCALED
#define FINISH3D_WIDTH width3d
#define FINISH3D_HEIGHT height3d
#else
#define FINISH3D_WIDTH width
#define FINISH3D_HEIGHT height
#endif

int expand5(int c)
{
    return c != 0 ? c * 2 + 1 : 0;
}

// A 5-bit colour channel at the 3D's precision.
int finish_channel5(int c)
{
    return fine != 0u ? (c << 3) | (c >> 2) : expand5(c);
}

uint edge_color(uint index)
{
    const uint word = edge_colors[index >> 1];
    return (index & 1u) != 0u ? word >> 16 : word & 0xffffu;
}

uint fog_density(uint index)
{
    return (fog_table[index >> 4][(index >> 2) & 3u] >> ((index & 3u) * 8u)) & 0x7fu;
}

// RGB 6 bits each (8 when fine) in bits 0-7/8-15/16-23, alpha 0-31 in 24-28: the texel of the targets
// `ctex` and `atex`, w x h.
uint finish3d_texel(RWTexture2D<uint> ctex, RWTexture2D<uint> atex, int w, int h, int x, int y)
{
    const uint c = ctex[uint2(x, y)];
    const uint att = atex[uint2(x, y)];
    const uint cmask = fine != 0u ? 255u : 63u;
    int r = (int)(c & cmask), g = (int)((c >> 8) & cmask), b = (int)((c >> 16) & cmask), a = (int)((c >> 24) & 31u);
    const uint translucent = c >> 31;
    const uint depth = att & 0xffffffu, id = (att >> 24) & 63u, edge = (att >> 30) & 1u, fog = att >> 31;
    if ((disp3dcnt & 0x20u) != 0u && edge != 0u && translucent == 0u) {
        const uint cd = clear_depth & 0x7fffu;
        const uint clear_d = cd * 0x200u + ((cd + 1u) / 0x8000u) * 0x1ffu;
        const uint clear_id = (clear_color >> 24) & 63u;
        const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            uint nid, nd;
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                nid = clear_id;
                nd = clear_d;
            } else {
                const uint natt = atex[uint2(nx, ny)];
                nid = (natt >> 24) & 63u;
                nd = natt & 0xffffffu;
            }
            if (nid != id && depth < nd) {
                const uint e = edge_color(id >> 3);
                r = finish_channel5((int)(e & 31u));
                g = finish_channel5((int)((e >> 5) & 31u));
                b = finish_channel5((int)((e >> 10) & 31u));
                break;
            }
        }
    }
    if ((disp3dcnt & 0x80u) != 0u && fog != 0u) {
        const uint wbuffer = (swap_params >> 1) & 1u;
        const uint offset = fog_offset & 0x7fffu;
        const uint shift = (disp3dcnt >> 8) & 15u;
        const uint step = 0x400u >> shift;
        const int fr = finish_channel5((int)(fog_color & 31u)), fg = finish_channel5((int)((fog_color >> 5) & 31u)),
                  fb = finish_channel5((int)((fog_color >> 10) & 31u));
        const int fa = (int)((fog_color >> 16) & 31u);
        const uint d = wbuffer != 0u ? depth : depth >> 9; // the 15-bit scale of FOG_OFFSET
        int density;
        if (step == 0u || d <= offset + step) {
            density = (int)fog_density(0u);
        } else if (d >= offset + step * 32u) {
            density = (int)fog_density(31u);
        } else {
            const uint n = (d - offset) / step - 1u;
            const uint lo = offset + step * (n + 1u);
            const int d0 = (int)fog_density(n), d1 = (int)fog_density(n + 1u);
            density = d0 + (d1 - d0) * (int)(d - lo) / (int)step;
        }
        if (density == 127) density = 128;
        if ((disp3dcnt & 0x40u) == 0u) {
            r = (fr * density + r * (128 - density)) / 128;
            g = (fg * density + g * (128 - density)) / 128;
            b = (fb * density + b * (128 - density)) / 128;
        }
        a = (fa * density + a * (128 - density)) / 128;
    }
    return (uint)r | ((uint)g << 8) | ((uint)b << 16) | ((uint)a << 24) | (c & 0x20000000u); // over the 2D
}

// The finished 3D of color3d and attr3d.
uint finished3d(int x, int y)
{
    return finish3d_texel(color3d, attr3d, (int)FINISH3D_WIDTH, (int)FINISH3D_HEIGHT, x, y);
}

// The HUD's 3D drawn apart (gpu_raster3d.h) over the rest's, as the DS draws a polygon over what is there:
// opaque, or with no 3D under it, its own; translucent, blended by its alpha, the larger alpha kept.
uint over3d(uint hud, uint world)
{
    const uint ha = (hud >> 24) & 31u, wa = (world >> 24) & 31u, k = ha + 1u;
    if (ha == 0u) {
        return world;
    }
    if (ha == 31u || wa == 0u) {
        return hud;
    }
    return (((hud & 255u) * k + (world & 255u) * (32u - k)) / 32u) |
           (((((hud >> 8) & 255u) * k + ((world >> 8) & 255u) * (32u - k)) / 32u) << 8) |
           (((((hud >> 16) & 255u) * k + ((world >> 16) & 255u) * (32u - k)) / 32u) << 16) | (max(ha, wa) << 24) |
           (hud & 0x20000000u);
}

#ifdef FINISH3D_SCALED
// The finished 3D at (px, py) of its targets' texels (their middles at .5): the nearest texel, or with
// smooth3d the four nearest weighed by place and by their alpha (a texel without 3D gives no colour),
// the flag "over the 2D" the nearest's. For a 2D drawn finer than the 3D.
uint finished3d_at(float px, float py)
{
    const int w = (int)FINISH3D_WIDTH, h = (int)FINISH3D_HEIGHT;
    if (smooth3d == 0u) {
        return finished3d(clamp((int)floor(px), 0, w - 1), clamp((int)floor(py), 0, h - 1));
    }
    const int x0 = (int)floor(px - 0.5f), y0 = (int)floor(py - 0.5f);
    const float fx = px - 0.5f - (float)x0, fy = py - 0.5f - (float)y0;
    const int ax = clamp(x0, 0, w - 1), bx = clamp(x0 + 1, 0, w - 1);
    const int ay = clamp(y0, 0, h - 1), by = clamp(y0 + 1, 0, h - 1);
    uint c[4];
    float weight[4];
    c[0] = finished3d(ax, ay);
    c[1] = finished3d(bx, ay);
    c[2] = finished3d(ax, by);
    c[3] = finished3d(bx, by);
    weight[0] = (1.0f - fx) * (1.0f - fy);
    weight[1] = fx * (1.0f - fy);
    weight[2] = (1.0f - fx) * fy;
    weight[3] = fx * fy;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f, best = -1.0f;
    uint nearest = c[0];
    for (int k = 0; k < 4; ++k) {
        const float ak = (float)((c[k] >> 24) & 31u) * weight[k];
        r += ak * (float)(c[k] & 255u);
        g += ak * (float)((c[k] >> 8) & 255u);
        b += ak * (float)((c[k] >> 16) & 255u);
        a += ak;
        if (weight[k] > best) {
            best = weight[k];
            nearest = c[k];
        }
    }
    const uint alpha = (uint)(a + 0.5f);
    if (alpha == 0u) {
        return nearest & 0x20ffffffu; // no 3D here
    }
    return (uint)(r / a + 0.5f) | ((uint)(g / a + 0.5f) << 8) | ((uint)(b / a + 0.5f) << 16) | (alpha << 24) |
           (nearest & 0x20000000u);
}
#endif
