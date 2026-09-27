/* The native process entry: what the DS crt0 (_start, libs/nitro/init/
 * asm_stubs/calls/Entry.c) does before it enters the game's main, step for
 * step, minus what has no native counterpart (CPU modes, stacks, CP15, the
 * static module's decompression and autoload -- the linker placed the ITCM and
 * DTCM code and data already). */
#include "../hal/hal.h"
#include "runtime.h"

#include <stdio.h>

extern void khdays_NitroMain(void);  /* the decomp's main, renamed at build */
extern void func_01ff8148(void);     /* OS_IrqHandler */
extern void func_020207f0(void);     /* _fp_init */
extern void func_02000b60(void);     /* NitroStartUp */
extern void func_02020808(void);     /* __call_static_initializers */

/* The ARM9 static initializer table the DS linker built (.ctor): in the ROM
 * its first entry (0x02042288) is already the terminating null. */
void (*ARM9_CTOR_START[1])(void) = {0};

static void clear32(u32 value, u32 address, u32 size)
{
    volatile u32 *d = (volatile u32 *)address;
    for (u32 i = 0; i < size / 4; ++i) {
        d[i] = value;
    }
}

int main(int argc, char **argv)
{
    if (!khdays_memory_map()) {
        return 1;
    }
    if (!khdays_runtime_init(argc, argv)) {
        return 1;
    }

    /* crt0: IME off (the register base's low bit is 0). */
    *(volatile u32 *)0x04000208 = 0x04000000;
    /* crt0: clear the DTCM, the palettes and OAM (0x0200 = attr0 "disable"). */
    clear32(0, 0x027e0000, 0x4000);
    clear32(0, 0x05000000, 0x400);
    clear32(0x0200, 0x07000000, 0x400);
    /* crt0: the static .bss is zero already -- the memory map starts zeroed. */
    /* crt0: HW_COMPONENT_PARAM = 0. */
    *(volatile u32 *)0x027fff9c = 0;
    /* crt0: the BIOS IRQ vector at DTCM + 0x3ffc points at OS_IrqHandler. */
    *(volatile u32 *)0x027e3ffc = (u32)func_01ff8148;

    func_020207f0();
    func_02000b60();
    func_02020808();
    khdays_NitroMain();
    /* main never returns; crt0 left HW_RESET_VECTOR as its return address. */
    khdays_hal_unimplemented("main returned");
}
