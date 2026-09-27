/* The DS BIOS calls (SWIs) behind NitroSDK's SVC_* veneers
 * (libs/nitro/os/asm_stubs/auto). Each follows the ARM9 BIOS behaviour the
 * veneer's SWI number selects; argument order is the veneer's r0, r1, r2. */
#include "hal.h"

/* SWI 0x0b. control: bits 0-20 = unit count, bit 24 = fill (repeat the first
 * source unit), bit 26 = 32-bit units (else 16-bit). */
void CpuSet(const void *source, void *destination, u32 control)
{
    const u32 count = control & 0x1fffff;
    const int fill = (control >> 24) & 1;
    if ((control >> 26) & 1) {
        const volatile u32 *s = (const volatile u32 *)((u32)source & ~3u);
        volatile u32 *d = (volatile u32 *)((u32)destination & ~3u);
        const u32 value = *s;
        for (u32 i = 0; i < count; ++i) {
            d[i] = fill ? value : s[i];
        }
    } else {
        const volatile u16 *s = (const volatile u16 *)((u32)source & ~1u);
        volatile u16 *d = (volatile u16 *)((u32)destination & ~1u);
        const u16 value = *s;
        for (u32 i = 0; i < count; ++i) {
            d[i] = fill ? value : s[i];
        }
    }
}

/* SWI 0x0c: 32-bit units in blocks of eight words (the count rounds up to a
 * multiple of 8); bit 24 = fill. */
void CpuFastSet(const void *source, void *destination, u32 control)
{
    const u32 count = ((control & 0x1fffff) + 7) & ~7u;
    const int fill = (control >> 24) & 1;
    const volatile u32 *s = (const volatile u32 *)((u32)source & ~3u);
    volatile u32 *d = (volatile u32 *)((u32)destination & ~3u);
    const u32 value = *s;
    for (u32 i = 0; i < count; ++i) {
        d[i] = fill ? value : s[i];
    }
}

/* SWI 0x09 returns the quotient in r0 and the remainder in r1; Div keeps r0,
 * Mod moves r1 into r0. Division by zero hangs the real BIOS. */
s32 Div(s32 numerator, s32 denominator)
{
    if (denominator == 0) {
        khdays_hal_unimplemented("SVC_Div by zero");
    }
    return numerator / denominator;
}

s32 Mod(s32 numerator, s32 denominator)
{
    if (denominator == 0) {
        khdays_hal_unimplemented("SVC_Mod by zero");
    }
    return numerator % denominator;
}

/* SWI 0x0d: floor square root of a 32-bit value. */
u16 Sqrt(u32 value)
{
    u32 root = 0;
    u32 bit = 1u << 30;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (u16)root;
}

/* SWI 0x0e: reflected CRC-16 (polynomial 0xa001) over `size` bytes, read as
 * halfwords. */
u16 GetCRC16(u16 initialValue, const void *data, u32 size)
{
    const u16 *p = (const u16 *)((u32)data & ~1u);
    u32 crc = initialValue;
    for (u32 i = 0; i < size / 2; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 16; ++bit) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
        }
    }
    return (u16)crc;
}

/* SWI 0x0f: 0 on retail hardware. */
int IsDebugger(void)
{
    return 0;
}

/* SWI 0x06: halt until an interrupt. */
void Halt(void)
{
    khdays_runtime_wait();
}

/* SWI 0x03: a timed busy loop; nothing to wait for natively. */
void WaitByLoop(s32 count)
{
    (void)count;
}

/* SWI 0x10. The parameter block is NitroSDK's MIUnpackBitsParam:
 * u16 srcNum (bytes), u8 srcBitNum, u8 destBitNum, u32 destOffset (bit 31 =
 * add the offset to zero units too). */
void BitUnPack(const void *source, void *destination, const void *parameters)
{
    const u8 *p = (const u8 *)parameters;
    const u32 src_bytes = *(const u16 *)p;
    const u32 src_bits = p[2];
    const u32 dst_bits = p[3];
    const u32 offset_word = *(const u32 *)(p + 4);
    const u32 offset = offset_word & 0x7fffffff;
    const int offset_zero = (offset_word >> 31) & 1;
    const u8 *s = (const u8 *)source;
    volatile u32 *d = (volatile u32 *)((u32)destination & ~3u);
    const u32 src_mask = (1u << src_bits) - 1;
    u32 out = 0;
    u32 out_bits = 0;
    for (u32 i = 0; i < src_bytes; ++i) {
        const u32 byte = s[i];
        for (u32 bit = 0; bit < 8; bit += src_bits) {
            u32 unit = (byte >> bit) & src_mask;
            if (unit != 0 || offset_zero) {
                unit += offset;
            }
            out |= (dst_bits == 32 ? unit : (unit & ((1u << dst_bits) - 1))) << out_bits;
            out_bits += dst_bits;
            if (out_bits >= 32) {
                *d++ = out;
                out = 0;
                out_bits = 0;
            }
        }
    }
}

/* SWI 0x11: LZ77, 8-bit writes. Header: type 0x10 in bits 0-7, the
 * decompressed size in bits 8-31. */
void LZ77UnCompReadNormalWrite8bit(const void *source, void *destination)
{
    const u8 *s = (const u8 *)source;
    u8 *d = (u8 *)destination;
    u32 remaining = (s[0] | (s[1] << 8) | (s[2] << 16) | ((u32)s[3] << 24)) >> 8;
    s += 4;
    while (remaining > 0) {
        u8 flags = *s++;
        for (int i = 0; i < 8 && remaining > 0; ++i, flags <<= 1) {
            if (flags & 0x80) {
                const u32 length = (s[0] >> 4) + 3;
                const u32 distance = (((s[0] & 0x0f) << 8) | s[1]) + 1;
                s += 2;
                for (u32 j = 0; j < length && remaining > 0; ++j, --remaining) {
                    *d = *(d - distance);
                    ++d;
                }
            } else {
                *d++ = *s++;
                --remaining;
            }
        }
    }
}

/* SWI 0x14: run-length, 8-bit writes. Header as LZ77 with type 0x30. */
void RLUnCompReadNormalWrite8bit(const void *source, void *destination)
{
    const u8 *s = (const u8 *)source;
    u8 *d = (u8 *)destination;
    u32 remaining = (s[0] | (s[1] << 8) | (s[2] << 16) | ((u32)s[3] << 24)) >> 8;
    s += 4;
    while (remaining > 0) {
        const u8 flag = *s++;
        if (flag & 0x80) {
            u32 length = (flag & 0x7f) + 3;
            const u8 value = *s++;
            for (; length > 0 && remaining > 0; --length, --remaining) {
                *d++ = value;
            }
        } else {
            u32 length = (flag & 0x7f) + 1;
            for (; length > 0 && remaining > 0; --length, --remaining) {
                *d++ = *s++;
            }
        }
    }
}

/* --- Not reached by the decompiled game: no C caller exists ------------------
 * The callback decompressors read through a MIReadStreamCallbacks block the
 * veneers' declarations do not carry, and the waits/reset belong to the
 * runtime; each stops loudly if something ever calls it. */
KHDAYS_HAL_TODO(LZ77UnCompReadByCallbackWrite16bit)
KHDAYS_HAL_TODO(RLUnCompReadByCallbackWrite16bit)
KHDAYS_HAL_TODO(HuffUnCompReadByCallback)
KHDAYS_HAL_TODO(IntrWait)
KHDAYS_HAL_TODO(VBlankIntrWait)
KHDAYS_HAL_TODO(SoftReset)
