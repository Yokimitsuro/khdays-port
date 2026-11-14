/* A still picture's smaller copies (mips.h). */
#include "mips.h"

#include <stddef.h>

uint32_t khdays_mips_words(uint32_t w, uint32_t h)
{
    uint32_t words = 0;
    while (w > 1 || h > 1) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        words += w * h;
    }
    return words;
}

void khdays_mips_make(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out)
{
    const uint8_t *from = rgba;
    while (w > 1 || h > 1) {
        const uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        for (uint32_t y = 0; y < nh; ++y) {
            for (uint32_t x = 0; x < nw; ++x) {
                uint32_t c[3] = {0, 0, 0}, plain[3] = {0, 0, 0}, a = 0;
                uint8_t *o = out + ((size_t)y * nw + x) * 4u;
                for (uint32_t k = 0; k < 4; ++k) {
                    const uint32_t tx = x * 2u + (k & 1u), ty = y * 2u + (k >> 1);
                    const uint32_t sx = tx < w - 1u ? tx : w - 1u, sy = ty < h - 1u ? ty : h - 1u;
                    const uint8_t *t = from + ((size_t)sy * w + sx) * 4u;
                    for (int i = 0; i < 3; ++i) {
                        c[i] += (uint32_t)t[i] * t[3];
                        plain[i] += t[i];
                    }
                    a += t[3];
                }
                for (int i = 0; i < 3; ++i) {
                    o[i] = (uint8_t)(a > 0 ? (c[i] + a / 2u) / a : (plain[i] + 2u) / 4u);
                }
                o[3] = (uint8_t)((a + 2u) / 4u);
            }
        }
        from = out;
        out += (size_t)nw * nh * 4u;
        w = nw;
        h = nh;
    }
}
