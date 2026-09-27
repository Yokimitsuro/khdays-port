/* The remaining assembly-only routines: the RTC busy-wait, two NitroSDK
 * library kernels no decompiled code reaches, crt0 (replaced by the native
 * entry, native/runtime/start.c) and the MobiClip codec, which is ARM code the
 * game copies to ITCM and runs from there. */
#include "hal.h"

/* func_0200dcf0 (RTC): spin while the PXI reply flag reads 1. Natively the
 * reply only arrives if the runtime runs the pending interrupts meanwhile. */
extern volatile int data_02046444;

void func_0200dcf0(void)
{
    while (data_02046444 == 1) {
        khdays_runtime_wait();
    }
}

/* DGTi_hash2_arm4_small (SHA-1 block transform) and MATH_QSort: no C source
 * of the game calls either. */
KHDAYS_HAL_TODO(func_0200bd4c)
KHDAYS_HAL_TODO(func_0200fc00)

/* crt0 (libs/nitro/init/asm_stubs): _start, its autoload, CP15 setup and the
 * static module decompressor. The native entry does what they do for the
 * native process; nothing else calls them. */
KHDAYS_HAL_TODO(Entry)
KHDAYS_HAL_TODO(func_020009fc)  /* do_autoload */
KHDAYS_HAL_TODO(func_02000a78)  /* init_cp15 */
KHDAYS_HAL_TODO(MIi_UncompressBackward)

/* --- MobiClip (ov024) ---------------------------------------------------------
 * data_ov024_0208c8c4 is the 0x659c-byte position-independent video decoder
 * func_ov024_02086620 copies to ITCM and enters through pointers into the copy;
 * func_ov024_02086958 and func_ov024_02087318_unk are the two audio codecs
 * func_ov024_02085c8c calls; func_ov024_02086004 is the colour converter. All
 * are ARM code. The native video path replaces the decoder as a whole; until
 * it exists the blob is an empty buffer of the right size, so the copy
 * succeeds and entering it fails. */
unsigned char data_ov024_0208c8c4[0x659c];

int func_ov024_02086958(char *chan)
{
    (void)chan;
    khdays_hal_unimplemented("MobiClip audio: variable-length transform codec");
}

int func_ov024_02087318_unk(char *chan)
{
    (void)chan;
    khdays_hal_unimplemented("MobiClip audio: FastAudio");
}

KHDAYS_HAL_TODO(func_ov024_02086004)

/* The deblocking post-filter (ARM, ov024 .rodata 0x02092e60), which
 * func_ov024_02085ab8 calls only in display modes 1 and 2; KH Days always uses
 * mode 0 (src/overlays/ov024/data/mobiclip_deblock.s). */
KHDAYS_HAL_TODO(func_ov024_02092e60_unk)
