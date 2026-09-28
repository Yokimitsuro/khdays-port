/* The DS ARM9 address space, mapped at the same virtual addresses in the
 * native (32-bit) process, so the decompiled code's absolute addresses and
 * the absolute symbols the build gives its BSS reach real memory.
 *
 *   0x01ff8000  ITCM, 32 KB
 *   0x02000000  main RAM, 4 MB, mirrored every 4 MB up to 0x03000000 (the
 *               shared area at 0x027ff000 is the mirror of 0x023ff000)
 *   0x027e0000  DTCM, 16 KB, in front of the main RAM mirror
 *   0x03000000  shared WRAM
 *   0x04000000  I/O registers; 0x04100000 the IPC FIFO / card data ports
 *   0x05000000  palettes, 2 KB
 *   0x06000000  VRAM: BG A, BG B (0x06200000), OBJ A (0x06400000), OBJ B
 *               (0x06600000), LCDC (0x06800000-0x068a4000)
 *   0x07000000  OAM, 2 KB
 *   0x08000000  the GBA slot (empty)
 *
 * Windows maps at 64 KB granularity, so each region is rounded out to it. */
#include "io.h"
#include "runtime.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <psapi.h>

#define MAIN_RAM_SIZE 0x400000u
#define DTCM_BASE     0x027e0000u
#define GRANULE       0x10000u

static int fail(const char *what, unsigned address)
{
    MEMORY_BASIC_INFORMATION info;
    fprintf(stderr, "memory map: %s at 0x%08x failed (error %lu)\n", what, address,
            GetLastError());
    /* what holds the range already */
    for (unsigned at = 0x00010000u; at < address + 0x01000000u; at = (unsigned)info.BaseAddress + info.RegionSize) {
        char module[MAX_PATH] = "";
        if (VirtualQuery((const void *)at, &info, sizeof(info)) == 0) {
            break;
        }
        if (info.State != MEM_FREE) {
            if (info.Type == MEM_MAPPED) {
                K32GetMappedFileNameA(GetCurrentProcess(), info.BaseAddress, module, sizeof(module));
            } else {
                GetModuleFileNameA((HMODULE)info.AllocationBase, module, sizeof(module));
            }
            fprintf(stderr, "  0x%08x-0x%08x taken (allocation 0x%08x, type 0x%lx) %s\n",
                    (unsigned)info.BaseAddress, (unsigned)info.BaseAddress + (unsigned)info.RegionSize,
                    (unsigned)info.AllocationBase, info.Type, module);
        }
    }
    return 0;
}

static int plain(const char *what, unsigned address, unsigned size)
{
    const unsigned base = address & ~(GRANULE - 1);
    const unsigned end = (address + size + GRANULE - 1) & ~(GRANULE - 1);
    if (VirtualAlloc((void *)base, end - base, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) !=
        (void *)base) {
        return fail(what, base);
    }
    return 1;
}

static int view(HANDLE section, const char *what, unsigned address, unsigned offset,
                unsigned size)
{
    if (MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, offset, size, (void *)address) !=
        (void *)address) {
        return fail(what, address);
    }
    return 1;
}

u8 *khdays_io_host;
u8 *khdays_io2_host;

/* An I/O region: one section, viewed at its DS address (which io_trap.c
 * protects) and once more wherever Windows likes, for the runtime's own
 * accesses. */
static u8 *io_region(const char *what, unsigned address, unsigned size)
{
    HANDLE section = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, NULL);
    u8 *host;
    if (section == NULL || !view(section, what, address, 0, size)) {
        fail(what, address);
        return NULL;
    }
    host = (u8 *)MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (host == NULL) {
        fail(what, address);
    }
    return host;
}

/* The GBA slot, empty. On the original DS an empty slot reads as bus noise;
 * the DSi, which has no slot, returns FFFFh (GBATEK), and the game runs on
 * both -- so the defined value is used. ROM (0x08000000) and SRAM
 * (0x0a000000) each get one read-only granule; wider reads stop. */
static int empty_gba_slot(void)
{
    HANDLE section = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, GRANULE, NULL);
    u8 *fill;
    DWORD old;
    if (section == NULL || (fill = (u8 *)MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, GRANULE)) == NULL) {
        return fail("GBA slot section", 0x08000000);
    }
    memset(fill, 0xff, GRANULE);
    UnmapViewOfFile(fill);
    if (!view(section, "GBA slot ROM", 0x08000000, 0, GRANULE) ||
        !view(section, "GBA slot SRAM", 0x0a000000, 0, GRANULE)) {
        return 0;
    }
    VirtualProtect((void *)0x08000000, GRANULE, PAGE_READONLY, &old);
    VirtualProtect((void *)0x0a000000, GRANULE, PAGE_READONLY, &old);
    return 1;
}

int khdays_memory_map(void)
{
    HANDLE ram = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                    MAIN_RAM_SIZE, NULL);
    if (ram == NULL) {
        return fail("main RAM section", 0x02000000);
    }
    /* Main RAM and its mirrors; the second mirror leaves the DTCM's granule
     * out. */
    if (!view(ram, "main RAM", 0x02000000, 0, MAIN_RAM_SIZE) ||
        !view(ram, "main RAM mirror", 0x02400000, 0, DTCM_BASE - 0x02400000) ||
        !view(ram, "main RAM mirror (shared area)", DTCM_BASE + GRANULE,
              DTCM_BASE + GRANULE - 0x02400000, 0x02800000 - (DTCM_BASE + GRANULE)) ||
        !view(ram, "main RAM mirror", 0x02800000, 0, MAIN_RAM_SIZE) ||
        !view(ram, "main RAM mirror", 0x02c00000, 0, MAIN_RAM_SIZE)) {
        return 0;
    }
    return plain("ITCM", 0x01ff8000, 0x8000) &&
           plain("DTCM", DTCM_BASE, 0x4000) &&
           plain("shared WRAM", 0x03000000, 0x8000) &&
           (khdays_io_host = io_region("I/O", KHDAYS_IO_BASE, GRANULE)) != NULL &&
           (khdays_io2_host = io_region("IPC/card ports", KHDAYS_IO2_BASE, GRANULE)) != NULL &&
           plain("palettes", 0x05000000, 0x800) &&
           plain("VRAM", 0x06000000, 0x008a4000) &&
           plain("OAM", 0x07000000, 0x800) &&
           empty_gba_slot();
}
