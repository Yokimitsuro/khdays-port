/* The native process entry: what the DS firmware and the game's crt0 (_start,
 * libs/nitro/init/asm_stubs/calls/Entry.c) do before the game's main, step
 * for step, minus what has no native counterpart (CPU modes, stacks, CP15).
 *
 * The static module's image is loaded at its address as the firmware loads
 * it, so memory holds what the DS holds; the native data initializers then
 * go over it (their function pointers are native). */
#include "../hal/hal.h"
#include "rom.h"
#include "runtime.h"
#include "snd_driver.h"

#include "../host/host.h"

#include <stdio.h>
#include <windows.h>

extern void khdays_NitroMain(void);  /* the decomp's main, renamed at build */
extern void func_01ff8148(void);     /* OS_IrqHandler */
extern void _fp_init(void);
extern void NitroStartUp(void);
extern void __call_static_initializers(void);
extern void AutoloadCallback(void);
extern void MIi_UncompressBackward(void *bottom);

/* The ARM9 static initializer table the DS linker built (.ctor): in the ROM
 * its first entry (0x02042288) is already the terminating null. */
void (*ARM9_CTOR_START[1])(void) = {0};

/* _start_ModuleParams (BuildInfo), in the static module's image: autoload
 * list, its end, the autoload data, static BSS start and end, and the end of
 * the compressed static module (0 when it is not compressed). */
#define MODULE_PARAMS ((volatile u32 *)0x02000b68)

static void clear32(u32 value, u32 address, u32 size)
{
    volatile u32 *d = (volatile u32 *)address;
    for (u32 i = 0; i < size / 4; ++i) {
        d[i] = value;
    }
}

/* The firmware: the ARM9 binary from the card to its RAM address. */
static void load_static_module(void)
{
    const u8 *header = khdays_rom_header();
    const u32 rom = *(const u32 *)(header + 0x20);
    const u32 ram = *(const u32 *)(header + 0x28);
    const u32 size = *(const u32 *)(header + 0x2c);
    khdays_rom_read(rom, (u8 *)ram, size);
}

/* crt0's do_autoload (func_020009fc): copy each autoload block (ITCM, DTCM)
 * from the static module to its address and clear the BSS after it. */
static void autoload(void)
{
    const volatile u32 *info = (const volatile u32 *)MODULE_PARAMS[0];
    const volatile u32 *const info_end = (const volatile u32 *)MODULE_PARAMS[1];
    const volatile u32 *source = (const volatile u32 *)MODULE_PARAMS[2];
    while (info != info_end) {
        volatile u32 *destination = (volatile u32 *)info[0];
        volatile u32 *const data_end = (volatile u32 *)(info[0] + info[1]);
        volatile u32 *bss_end;
        while (destination < data_end) {
            *destination++ = *source++;
        }
        bss_end = (volatile u32 *)((u32)destination + info[2]);
        while (destination < bss_end) {
            *destination++ = 0;
        }
        info += 3;
    }
    AutoloadCallback();
}

/* OS_ResetSystem: the game restarts the console -- Mission Mode's "main
 * menu" goes back to the title so -- leaving `parameter` at 0x027ffc20. The
 * DS reloads the game from the card; natively this process starts itself
 * again, which writes the parameter there at boot (boot.c), and ends with
 * its exit code. Its window closes first. */
void khdays_reset_system(u32 parameter)
{
    char value[16];
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    DWORD code = 1;
    fprintf(stderr, "khdays-native: the game resets the console (parameter 0x%08x): starting again\n",
            parameter);
    fflush(stderr);
    khdays_host_close();
    snprintf(value, sizeof(value), "0x%08x", parameter);
    SetEnvironmentVariableA("KHDAYS_RESET_PARAMETER", value);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(NULL, GetCommandLineW(), NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
        fprintf(stderr, "khdays-native: cannot start again (error %lu)\n", GetLastError());
        ExitProcess(1);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &code);
    ExitProcess(code);
}

int main(int argc, char **argv)
{
    /* First: the DS's address ranges must be claimed before anything else
     * (the symbol handler, SDL) allocates there. */
    if (!khdays_memory_map()) {
        return 1;
    }
    khdays_diag_init();
    if (!khdays_runtime_init(argc, argv)) {
        return 1;
    }
    load_static_module();

    /* crt0: IME off (the register base's low bit is 0). */
    *(volatile u32 *)0x04000208 = 0x04000000;
    /* crt0: clear the DTCM, the palettes and OAM (0x0200 = attr0 "disable"). */
    clear32(0, 0x027e0000, 0x4000);
    clear32(0, 0x05000000, 0x400);
    clear32(0x0200, 0x07000000, 0x400);
    /* crt0: decompress the static module, autoload, clear the static BSS. */
    if (MODULE_PARAMS[5] != 0) {
        MIi_UncompressBackward((void *)MODULE_PARAMS[5]);
    }
    autoload();
    clear32(0, MODULE_PARAMS[3], MODULE_PARAMS[4] - MODULE_PARAMS[3]);
    /* The native data of the static module, ITCM and DTCM, over their image. */
    khdays_data_init(-1);
    khdays_data_init(-2);
    khdays_data_init(-3);
    /* The ARM7, meanwhile, has started its sound driver (it reads a table
     * in the static module, so after the above). */
    khdays_snd_driver_init();
    /* crt0: HW_COMPONENT_PARAM = 0. */
    *(volatile u32 *)0x027fff9c = 0;
    /* crt0: the BIOS IRQ vector at DTCM + 0x3ffc points at OS_IrqHandler. */
    *(volatile u32 *)0x027e3ffc = (u32)func_01ff8148;

    _fp_init();
    NitroStartUp();
    __call_static_initializers();
    khdays_NitroMain();
    /* main never returns; crt0 left HW_RESET_VECTOR as its return address. */
    khdays_hal_unimplemented("main returned");
}
