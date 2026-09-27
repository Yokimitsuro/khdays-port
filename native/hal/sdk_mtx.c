/* NitroSDK MTX routines the decompilation keeps as ARM assembly
 * (libs/nitro/mtx/asm_stubs), translated store for store. Matrices are
 * row-major arrays of fx32 (Mtx22 = 4, Mtx33 = 9, Mtx43 = 12, Mtx44 = 16). */
#include "hal.h"

#define FX32_ONE 0x1000

void MTX_Identity22_(fx32 *m)
{
    m[0] = FX32_ONE; m[1] = 0;
    m[2] = 0;        m[3] = FX32_ONE;
}

void MTX_Identity33_(fx32 *m)
{
    for (int i = 0; i < 9; ++i) {
        m[i] = 0;
    }
    m[0] = m[4] = m[8] = FX32_ONE;
}

void MTX_Identity43_(fx32 *m)
{
    for (int i = 0; i < 12; ++i) {
        m[i] = 0;
    }
    m[0] = m[4] = m[8] = FX32_ONE;
}

void MTX_Identity44_(fx32 *m)
{
    for (int i = 0; i < 16; ++i) {
        m[i] = 0;
    }
    m[0] = m[5] = m[10] = m[15] = FX32_ONE;
}

/* Each 3-word row gains a fourth column: 0, and 1.0 on the last row. */
void MTX_Copy43To44_(const fx32 *src, fx32 *dst)
{
    for (int row = 0; row < 4; ++row) {
        dst[row * 4 + 0] = src[row * 3 + 0];
        dst[row * 4 + 1] = src[row * 3 + 1];
        dst[row * 4 + 2] = src[row * 3 + 2];
        dst[row * 4 + 3] = row == 3 ? FX32_ONE : 0;
    }
}

void MTX_Copy44To43_(const fx32 *src, fx32 *dst)
{
    for (int row = 0; row < 4; ++row) {
        dst[row * 3 + 0] = src[row * 4 + 0];
        dst[row * 3 + 1] = src[row * 4 + 1];
        dst[row * 3 + 2] = src[row * 4 + 2];
    }
}

void MTX_RotY33_(fx32 *m, fx32 s, fx32 c)
{
    m[0] = c; m[1] = 0;        m[2] = -s;
    m[3] = 0; m[4] = FX32_ONE; m[5] = 0;
    m[6] = s; m[7] = 0;        m[8] = c;
}

void MTX_RotZ33_(fx32 *m, fx32 s, fx32 c)
{
    m[0] = c;  m[1] = s; m[2] = 0;
    m[3] = -s; m[4] = c; m[5] = 0;
    m[6] = 0;  m[7] = 0; m[8] = FX32_ONE;
}

void MTX_RotX43_(fx32 *m, fx32 s, fx32 c)
{
    m[0] = FX32_ONE; m[1] = 0;  m[2] = 0;
    m[3] = 0;        m[4] = c;  m[5] = s;
    m[6] = 0;        m[7] = -s; m[8] = c;
    m[9] = 0;        m[10] = 0; m[11] = 0;
}

void MTX_RotY43_(fx32 *m, fx32 s, fx32 c)
{
    m[0] = c; m[1] = 0;        m[2] = -s;
    m[3] = 0; m[4] = FX32_ONE; m[5] = 0;
    m[6] = s; m[7] = 0;        m[8] = c;
    m[9] = 0; m[10] = 0;       m[11] = 0;
}
