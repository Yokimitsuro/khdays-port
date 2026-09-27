/* Forced include for the decompilation's sources in the native (MSVC, x86)
 * build. It only papers over compiler dialect; nothing here changes what the
 * game code does. */
#ifndef KHDAYS_DECOMP_COMPAT_H
#define KHDAYS_DECOMP_COMPAT_H

/* GCC/mwcc attributes (alignment hints) have no MSVC C spelling here. */
#define __attribute__(x)

/* The ARM `clz` the decomp spells as `asm { clz x, x }` (prepare.py rewrites
 * it to this). */
static __inline unsigned int khdays_clz(unsigned int x)
{
    unsigned int n = 0;
    if (x == 0) {
        return 32;
    }
    while ((x & 0x80000000u) == 0) {
        x <<= 1;
        ++n;
    }
    return n;
}

/* `*(T (*)[n])dst = *(T (*)[n])src;` -- a whole-array assignment mwcc accepts
 * (prepare.py rewrites it to this). */
#define KHDAYS_ARRAY_ASSIGN(T, n, dst, src)                         \
    do {                                                            \
        T *khdays_d_ = (T *)(dst);                                  \
        const T *khdays_s_ = (const T *)(src);                      \
        for (int khdays_i_ = 0; khdays_i_ < (n); ++khdays_i_) {     \
            khdays_d_[khdays_i_] = khdays_s_[khdays_i_];            \
        }                                                           \
    } while (0)

#endif
