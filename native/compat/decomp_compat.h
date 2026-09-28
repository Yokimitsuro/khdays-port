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

/* Game data lives at its DS address (prepare.py): each initialised object's
 * initializer is kept as `<name>__khdays_init` and listed in section .khdi,
 * which the runtime walks to copy it into place when the object's module
 * (-1 static, -2 ITCM, -3 DTCM, else the overlay id) is loaded. */
struct khdays_data_init {
    void *address;
    const void *init;
    unsigned int size;
    int module;
};
#pragma section(".khdi$m", read, write)
#define KHDAYS_DATA_INIT(name, module)                                        \
    __declspec(allocate(".khdi$m")) struct khdays_data_init khdays_init_##name = { \
        (void *)&name, (const void *)&name##__khdays_init,                    \
        sizeof(name##__khdays_init), module};

/* A call the decomp's C makes with fewer arguments than the ROM passes, where
 * the missing value is not yet known (prepare.py, native/abi): reaching it
 * stops the game, naming the call. */
#ifdef __cplusplus
extern "C"
#endif
__declspec(noreturn) void khdays_abi_gap(const char *what);
#define KHDAYS_ABI_GAP(what) (khdays_abi_gap(what), 0)

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
