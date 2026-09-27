/* NitroSDK MI (memory interface) routines the decompilation keeps as ARM
 * assembly (libs/nitro/mi/asm_stubs). Argument order follows that assembly
 * (e.g. MI_Copy48B reads r0 and writes r1), not the parameter names some
 * callers gave their extern declarations. */
#include "hal.h"

void MI_CpuFill8(void *dest, u8 data, u32 size)
{
    u8 *d = (u8 *)dest;
    while (size-- != 0) {
        *d++ = data;
    }
}

void MI_CpuCopy8(const void *src, void *dest, u32 size)
{
    const u8 *s = (const u8 *)src;
    u8 *d = (u8 *)dest;
    /* Byte by byte, forward, like the assembly (overlap behaves the same). */
    while (size-- != 0) {
        *d++ = *s++;
    }
}

void MIi_CpuClear16(u16 data, void *dest, u32 size)
{
    volatile u16 *d = (volatile u16 *)dest;
    for (u32 i = 0; i < size / 2; ++i) {
        d[i] = data;
    }
}

void MIi_CpuCopy16(const void *src, void *dest, u32 size)
{
    const volatile u16 *s = (const volatile u16 *)src;
    volatile u16 *d = (volatile u16 *)dest;
    for (u32 i = 0; i < size / 2; ++i) {
        d[i] = s[i];
    }
}

void MIi_CpuCopy32(const void *src, void *dest, u32 size)
{
    const volatile u32 *s = (const volatile u32 *)src;
    volatile u32 *d = (volatile u32 *)dest;
    for (u32 i = 0; i < size / 4; ++i) {
        d[i] = s[i];
    }
}

/* Writes every word to the same destination (a FIFO port). */
void MIi_CpuSend32(const void *src, volatile void *dest, u32 size)
{
    const volatile u32 *s = (const volatile u32 *)src;
    volatile u32 *d = (volatile u32 *)dest;
    for (u32 i = 0; i < size / 4; ++i) {
        *d = s[i];
    }
}

void MIi_CpuClearFast(u32 data, void *dest, u32 size)
{
    volatile u32 *d = (volatile u32 *)dest;
    for (u32 i = 0; i < size / 4; ++i) {
        d[i] = data;
    }
}

void MIi_CpuCopyFast(const void *src, void *dest, u32 size)
{
    MIi_CpuCopy32(src, dest, size);
}

void MI_Copy36B(const void *src, void *dest)
{
    MIi_CpuCopy32(src, dest, 36);
}

void MI_Copy48B(const void *src, void *dest)
{
    MIi_CpuCopy32(src, dest, 48);
}

void MI_Copy64B(const void *src, void *dest)
{
    MIi_CpuCopy32(src, dest, 64);
}

void MI_Zero36B(void *dest)
{
    MIi_CpuClearFast(0, dest, 36);
}

/* `swp`: store the new value and return the old one, atomically. The native
 * game runs on one thread, so a plain exchange is atomic enough. */
u32 MI_SwapWord(u32 value, volatile u32 *destination)
{
    const u32 old = *destination;
    *destination = value;
    return old;
}

/* crt0's INITi_CpuClear32 and its two copies. */
static void clear32(u32 data, void *dest, u32 size)
{
    volatile u32 *d = (volatile u32 *)dest;
    for (u32 i = 0; i < size / 4; ++i) {
        d[i] = data;
    }
}

void INITi_CpuClear32(u32 data, void *dest, u32 size) { clear32(data, dest, size); }
void INITi_CpuClear32_0x01ff86fc(u32 data, void *dest, u32 size) { clear32(data, dest, size); }
void INITi_CpuClear32_0x0200093c(u32 data, void *dest, u32 size) { clear32(data, dest, size); }

/* MI_ReadUncompLZ8 (func_02004484): the streaming LZ decoder, fed one chunk
 * at a time; returns the bytes still to write (0 = done). A register-for-
 * register translation -- the labels are the assembly's, and the context
 * carries the decoder's registers between calls. */
typedef struct {
    u8 *destp;          /* r3 */
    s32 destCount;      /* r4 */
    s32 length;         /* r7 */
    u16 destTmp;
    u8 destTmpCnt;
    u8 flags;           /* r5 */
    u8 flagIndex;       /* r6 */
    u8 lengthFlg;       /* r8 */
    u8 exFormat;        /* r11 */
    u8 padding;
} MIUncompContextLZ;

s32 func_02004484(MIUncompContextLZ *context, const u8 *data, u32 len)
{
    u8 *dest = context->destp;
    s32 destCount = context->destCount;
    u32 flags = context->flags;
    u32 flagIndex = context->flagIndex;
    s32 length = context->length;
    u32 lengthFlg = context->lengthFlg;
    const u32 exFormat = context->exFormat;
    u32 offset;

l21:
    if (destCount <= 0) goto l29;
    if (flagIndex == 0) goto l28;
l22:
    if (len == 0) goto l29;
    if ((flags & 0x80) == 0) {
        --destCount;
        --len;
        *dest++ = *data++;
        goto l26;
    }
l23:
    if (lengthFlg == 0) goto l24;
    if (exFormat != 1) goto l23_9;
    if (--lengthFlg == 0) goto l23_7;
    if (lengthFlg == 1) goto l23_6;
    length = *data++;
    if ((length & 0xe0) == 0) goto l23_4;
    length += 0x10;
    lengthFlg = 0;
    goto l23_10;
l23_4:
    if ((length & 0x10) == 0) goto l23_5;
    length = 0x1110 + ((length & 0xf) << 16);
    goto l23_8;
l23_5:
    length = 0x110 + ((length & 0xf) << 8);
    lengthFlg = 1;
    goto l23_8;
l23_6:
    length += *data++ << 8;
    goto l23_8;
l23_7:
    length += *data++;
    goto l23_10;
l23_8:
    if (--len == 0) goto l29;
    goto l23;
l23_9:
    length = *data++ + 0x30;
    lengthFlg = 0;
l23_10:
    if (--len == 0) goto l29;
l24:
    offset = (((u32)length & 0xf) << 8 | *data++) + 1;
    lengthFlg = 3;
    --len;
    length >>= 4;  /* asr */
    if (length == 0) goto l26;
    do {
        const u8 value = *(dest - offset);
        --destCount;
        *dest++ = value;
    } while (--length > 0);
l26:
    if (destCount == 0) goto l29;
    flags <<= 1;
    if (--flagIndex != 0) goto l22;
l28:
    if (len == 0) goto l29;
    flags = *data++;
    flagIndex = 8;
    --len;
    goto l21;
l29:
    context->destp = dest;
    context->destCount = destCount;
    context->flags = (u8)flags;
    context->flagIndex = (u8)flagIndex;
    context->length = length;
    context->lengthFlg = (u8)lengthFlg;
    context->exFormat = (u8)exFormat;
    return destCount;
}
