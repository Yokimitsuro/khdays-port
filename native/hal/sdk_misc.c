/* The remaining assembly-only routines: the RTC busy-wait, two NitroSDK
 * library kernels no decompiled code reaches, crt0 (replaced by the native
 * entry, native/runtime/start.c) and the MobiClip codec, which is ARM code the
 * game copies to ITCM and runs from there. */
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

/* DGTi_hash2_arm4_small (SHA-1 block transform) and MATH_QSort: no C source
 * of the game calls either. */
KHDAYS_HAL_TODO(DGTi_Hash2ProcessBlock)
KHDAYS_HAL_TODO(Util_QuickSortWithWork)

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

/* --- MobiClip (ov024) ---------------------------------------------------------
 * data_ov024_0208c8c4 is the 0x659c-byte position-independent video decoder
 * func_ov024_02086620 copies to ITCM and enters through pointers into the copy;
 * Ov024_MobiClip_AudioTransformDecode and func_ov024_02087318_unk are the two audio codecs
 * Ov024_MobiClip_StepAudio calls; Ov024_MobiClip_BlitRows is the colour converter. All
 * are ARM code. The native video path replaces the decoder as a whole; until
 * it exists the blob is an empty buffer of the right size, so the copy
 * succeeds and entering it fails. */
unsigned char data_ov024_0208c8c4[0x659c];

int Ov024_MobiClip_AudioTransformDecode(char *chan)
{
    (void)chan;
    khdays_hal_unimplemented("MobiClip audio: variable-length transform codec");
}

int func_ov024_02087318_unk(char *chan)
{
    (void)chan;
    khdays_hal_unimplemented("MobiClip audio: FastAudio");
}

KHDAYS_HAL_TODO(Ov024_MobiClip_BlitRows)

/* The deblocking post-filter (ARM, ov024 .rodata 0x02092e60), which
 * func_ov024_02085ab8 calls only in display modes 1 and 2; KH Days always uses
 * mode 0 (src/overlays/ov024/data/mobiclip_deblock.s). */
KHDAYS_HAL_TODO(func_ov024_02092e60_unk)
