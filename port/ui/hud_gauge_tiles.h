/* Match an OAM copy of a live horizontal 4-bit gauge strip, independent of its screen position. */
#ifndef KHDAYS_HUD_GAUGE_TILES_H
#define KHDAYS_HUD_GAUGE_TILES_H

static inline int khdays_hud_gauge_strip(const unsigned char *tiles, unsigned bytes, const unsigned char *pixels,
                                         int width, int height)
{
    if (height != 8 || width < 8 || width > 64 || (width & 7))
        return 0;
    const unsigned size = (unsigned)(width / 8) * 32u;
    for (unsigned first = 0; first + size <= bytes; first += 32) {
        int same = 1, ink = 0;
        for (int y = 0; y < 8 && same; ++y)
            for (int x = 0; x < width && same; ++x) {
                const unsigned char byte =
                    tiles[first + (unsigned)(x / 8) * 32u + (unsigned)y * 4u + (unsigned)(x & 7) / 2u];
                const unsigned char v = (byte >> (4 * (x & 1))) & 15;
                ink |= v != 0;
                same = v == pixels[y * width + x];
            }
        if (same && ink)
            return 1;
    }
    return 0;
}
#endif
