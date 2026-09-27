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
 *
 * Windows maps at 64 KB granularity, so each region is rounded out to it. */
#include "runtime.h"

#include <stdio.h>
#include <windows.h>

#define MAIN_RAM_SIZE 0x400000u
#define DTCM_BASE     0x027e0000u
#define GRANULE       0x10000u

static int fail(const char *what, unsigned address)
{
    fprintf(stderr, "memory map: %s at 0x%08x failed (error %lu)\n", what, address,
            GetLastError());
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
           plain("I/O", 0x04000000, 0x2000) &&
           plain("IPC/card ports", 0x04100000, 0x20) &&
           plain("palettes", 0x05000000, 0x800) &&
           plain("VRAM", 0x06000000, 0x008a4000) &&
           plain("OAM", 0x07000000, 0x800);
}
