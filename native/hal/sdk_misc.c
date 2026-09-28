/* The remaining assembly-only routines the native build replaces: the RTC
 * busy-wait and crt0 (the native entry, native/runtime/start.c, does its work).
 * Assembly-only code not implemented here -- the SHA-1 block transform, the
 * quicksort kernel, MobiClip's decoder and codecs -- runs as the ROM's own ARM
 * code in the interpreter (native/runtime/arm.c, prepare.py). */
#include "hal.h"

/* RtcWaitBusy (RTC): spin while the PXI reply flag reads 1. Natively the
 * reply only arrives if the runtime runs the pending interrupts meanwhile. */
extern volatile int data_02046444;

void RtcWaitBusy(void)
{
    while (data_02046444 == 1) {
        khdays_runtime_wait();
    }
}

/* crt0 (libs/nitro/init/asm_stubs): _start, its autoload, CP15 setup and the
 * static module decompressor. The native entry does what they do for the
 * native process; nothing else calls them. */
KHDAYS_HAL_TODO(Entry)
KHDAYS_HAL_TODO(func_020009fc)  /* do_autoload */
KHDAYS_HAL_TODO(func_02000a78)  /* init_cp15 */

/* MIi_UncompressBackward: in-place backward LZ, used by crt0 for the static
 * module and by FS_StartOverlay for compressed overlays. `bottom` is the end
 * of the compressed image; the two words before it give the compressed span
 * (low 24 bits) with the footer length (top 8 bits), and how far the output
 * extends past `bottom`. Translated from the assembly. */
void MIi_UncompressBackward(void *bottom)
{
    u8 *const end = (u8 *)bottom;
    u32 inp_top_word;
    u8 *outp, *inp, *inp_top;
    if (end == 0) {
        return;
    }
    inp_top_word = ((u32 *)end)[-2];
    outp = end + ((u32 *)end)[-1];
    inp = end - (inp_top_word >> 24);
    inp_top = end - (inp_top_word & 0x00ffffff);
    while (inp > inp_top) {
        u32 flag = *--inp;
        for (int count8 = 8; count8 > 0; --count8) {
            if (flag & 0x80) {
                u32 len = *--inp;
                u32 index = *--inp;
                int n;
                index = ((index | (len << 8)) & ~0xf000u) + 2;
                len += 0x20;
                /* copy, subtract 0x10, repeat while still >= 0: (byte >> 4) + 3 */
                for (n = (int)len; n >= 0; n -= 0x10) {
                    const u8 data = outp[index];
                    *--outp = data;
                }
            } else {
                *--outp = *--inp;
            }
            flag <<= 1;
            if (inp <= inp_top) {
                break;
            }
        }
    }
}
