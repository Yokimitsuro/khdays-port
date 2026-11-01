/* The DS's ARM9 address space in the game's memory window (window.h): each region at the window's start
 * plus its DS address, rounded out to 64 KB; the rest inaccessible, the null range among it. The map:
 * docs/PLATFORM.md, "Memory". */
#include "enemy_overlays.h"
#include "game_heap.h"
#include "io.h"
#include "runtime.h"
#include "session_log.h"
#include "window.h"
#include "../../host/app/context.h"
#include "../../host/screens/ui_text.h"
#include "../../sound/snd7_native.h"
#include "khdays_data_area.h" /* the window's areas above the DS's ranges (build/port/gen-x64) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAIN_RAM_SIZE 0x400000u
#define DTCM_BASE 0x027e0000u
#define GRANULE 0x10000u
#define ARM7_WRAM_BASE 0x037f8000u
#define ARM7_WRAM_END 0x03810000u

/* The stacks area: 256 MB, each stack after an inaccessible granule. */
#define STACKS_BASE 0x0c000000u
#define STACKS_SIZE 0x10000000u
#define WINDOW_SIZE ((SIZE_T)KHDAYS_HEAPS_END)

_Static_assert(STACKS_BASE + STACKS_SIZE <= KHDAYS_HEAPS_BASE, "the stacks area runs into the game's heaps");

/* The window is one placeholder (Windows 10 1803 and later) whose regions are replaced by their
 * memory; nothing else can be placed in it. */
typedef PVOID(WINAPI *VirtualAlloc2Fn)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef PVOID(WINAPI *MapViewOfFile3Fn)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *,
                                        ULONG);
static VirtualAlloc2Fn virtual_alloc2;
static MapViewOfFile3Fn map_view_of_file3;

/* The host address of a DS address (window.h's W). */
static unsigned char *at(unsigned address)
{
    return khdays_window + address;
}

static int fail(const char *what, unsigned address)
{
    fprintf(stderr, "memory map: %s at 0x%08x (host %p) failed (error %lu)\n", what, address, (void *)at(address),
            GetLastError());
    return 0;
}

/* [address, address + size) split off the window's placeholder, to be
 * replaced. */
static int carve(const char *what, unsigned address, unsigned size)
{
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(at(address), &info, sizeof(info)) != 0 && info.AllocationBase == at(address) &&
        info.RegionSize == size) {
        return 1; /* a placeholder of its own already (what was split off around it) */
    }
    /* with MEM_PRESERVE_PLACEHOLDER the size is the part split off, never 0 (the SDK's annotation does
     * not know it) */
#pragma warning(suppress : 28160 6333)
    if (!VirtualFree(at(address), size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
        return fail(what, address);
    }
    return 1;
}

static int plain(const char *what, unsigned address, unsigned size)
{
    const unsigned base = address & ~(GRANULE - 1);
    const unsigned end = (address + size + GRANULE - 1) & ~(GRANULE - 1);
    if (!carve(what, base, end - base)) {
        return 0;
    }
    if (virtual_alloc2(GetCurrentProcess(), at(base), end - base, MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER,
                       PAGE_READWRITE, NULL, 0) != at(base)) {
        return fail(what, base);
    }
    return 1;
}

static int view(HANDLE section, const char *what, unsigned address, unsigned offset, unsigned size)
{
    if (!carve(what, address, size)) {
        return 0;
    }
    if (map_view_of_file3(section, GetCurrentProcess(), at(address), offset, size, MEM_REPLACE_PLACEHOLDER,
                          PAGE_READWRITE, NULL, 0) != at(address)) {
        return fail(what, address);
    }
    return 1;
}

/* Reserves every free granule in [start, end), inaccessible: the DS's own addresses outside the
 * window, so that an access there stops the game. */
static void keep_free(unsigned start, unsigned end)
{
    for (unsigned a = start; a < end; a += GRANULE) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery((const void *)(size_t)a, &info, sizeof(info)) != 0 && info.State == MEM_FREE) {
            VirtualAlloc((void *)(size_t)a, GRANULE, MEM_RESERVE, PAGE_NOACCESS);
        }
    }
}

/* The registers' own memory (io_regs.c, IO16/IO32, the C's plain registers), in the window at
 * KHDAYS_IO_VIEW. Their DS addresses stay inaccessible (io_guard.c). */
u8 *khdays_io_host;
u8 *khdays_io2_host;

/* The GBA slot, empty: it reads FFFFh, as on the DSi (GBATEK). ROM and SRAM get one read-only granule
 * each. */
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
    VirtualProtect(at(0x08000000), GRANULE, PAGE_READONLY, &old);
    VirtualProtect(at(0x0a000000), GRANULE, PAGE_READONLY, &old);
    return 1;
}

/* The contexts' stacks (host/app/context.h), from the stacks area; a stack given back is used again
 * for one of its size. */
static unsigned stacks_next = STACKS_BASE;
static struct {
    unsigned address;
    size_t size;
} *returned;
static size_t returned_count, returned_room;

static void *window_stack(size_t size)
{
    const unsigned rounded = (unsigned)((size + GRANULE - 1) & ~(size_t)(GRANULE - 1));
    unsigned address;
    for (size_t i = 0; i < returned_count; ++i) {
        if (returned[i].size == rounded) {
            address = returned[i].address;
            returned[i] = returned[--returned_count];
            return at(address);
        }
    }
    if ((size_t)stacks_next + GRANULE + rounded > (size_t)STACKS_BASE + STACKS_SIZE) {
        fprintf(stderr, "memory map: the window's stacks area (0x%x bytes from 0x%08x) is full\n", STACKS_SIZE,
                STACKS_BASE);
        return NULL;
    }
    address = stacks_next + GRANULE; /* the granule under it stays inaccessible */
    if (!plain("a context's stack", address, rounded)) {
        return NULL;
    }
    stacks_next = address + rounded;
    return at(address);
}

static void window_stack_release(void *stack, size_t size)
{
    if (returned_count == returned_room) {
        const size_t room = returned_room ? returned_room * 2 : 16;
        void *grown = realloc(returned, room * sizeof(*returned));
        if (grown == NULL) {
            return; /* kept, never used again */
        }
        returned = grown;
        returned_room = room;
    }
    returned[returned_count].address = (unsigned)((unsigned char *)stack - khdays_window);
    returned[returned_count].size = (size + GRANULE - 1) & ~(size_t)(GRANULE - 1);
    ++returned_count;
}

/* The game's heaps: reserved without memory; the first access to a granule commits it, so the PC
 * gives the game only what it touches. Committed memory stays committed. */
static SRWLOCK heaps_lock = SRWLOCK_INIT;
static volatile LONG64 heaps_committed;
static DWORD heaps_granule;
static volatile LONG *heaps_touched; /* a bit per granule: committed */

unsigned long long khdays_heaps_committed(void)
{
    return (unsigned long long)heaps_committed;
}

int khdays_heaps_touched(KhdaysRef reference, unsigned *rest)
{
    const unsigned offset = reference - KHDAYS_HEAPS_BASE, granule = offset / heaps_granule;
    *rest = heaps_granule - offset % heaps_granule;
    return (heaps_touched[granule / 32] >> (granule % 32) & 1) != 0;
}

static DWORD WINAPI refused_box(LPVOID text)
{
    MessageBoxW(NULL, (const wchar_t *)text, L"Kingdom Hearts 358/2 Days (native)",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    return 0;
}

/* The PC refused more memory: logged and told to the player, then the game stops (exit code 13). */
static void heaps_refused(unsigned address, DWORD error)
{
    fprintf(stderr,
            "memory: the PC refused %lu bytes at 0x%08x (%llu bytes of the game's heaps committed so far, "
            "error %lu)\n",
            heaps_granule, address, khdays_heaps_committed(), error);
    khdays_log("memory: the PC refused %lu bytes at 0x%08x (%llu bytes of the game's heaps committed so far, "
               "error %lu)",
               heaps_granule, address, khdays_heaps_committed(), error);
    khdays_log_close();
    fflush(stderr);
    /* told plainly, not as a crash: Windows' own box (SDL may hold a lock inside an exception handler),
     * from a thread of its own on an ordinary stack */
    if (getenv("KHDAYS_HEADLESS") == NULL) {
        static wchar_t text[512];
        if (MultiByteToWideChar(CP_UTF8, 0, khdays_text(TXT_OUT_OF_MEMORY), -1, text, 512) > 0) {
            HANDLE box = CreateThread(NULL, 0, refused_box, text, 0, NULL);
            if (box != NULL) {
                WaitForSingleObject(box, INFINITE);
            }
        }
    }
    ExitProcess(13);
}

static LONG CALLBACK heaps_commit(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    const unsigned char *host;
    unsigned address, granule;
    MEMORY_BASIC_INFORMATION state;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2 ||
        record->ExceptionInformation[0] == 8 || khdays_window == NULL) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    host = (const unsigned char *)record->ExceptionInformation[1];
    if (host < at(KHDAYS_HEAPS_BASE) || host >= at(KHDAYS_HEAPS_END)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    address = (unsigned)(host - khdays_window);
    granule = address & ~(heaps_granule - 1);
    AcquireSRWLockExclusive(&heaps_lock);
    /* another thread may have committed it in between */
    if (VirtualQuery(at(granule), &state, sizeof(state)) == 0 || state.State != MEM_COMMIT) {
        /* KHDAYS_TEST_HEAPS_LIMIT (testing only): the PC refusing past that many bytes */
        static long long limit = -2;
        if (limit == -2) {
            const char *v = getenv("KHDAYS_TEST_HEAPS_LIMIT");
            limit = v != NULL && v[0] != '\0' ? _atoi64(v) : -1;
        }
        if (limit >= 0 && heaps_committed + heaps_granule > limit) {
            ReleaseSRWLockExclusive(&heaps_lock);
            heaps_refused(address, ERROR_COMMITMENT_LIMIT);
        }
        if (VirtualAlloc(at(granule), heaps_granule, MEM_COMMIT, PAGE_READWRITE) != at(granule)) {
            const DWORD error = GetLastError();
            ReleaseSRWLockExclusive(&heaps_lock);
            heaps_refused(address, error);
        }
        heaps_committed += heaps_granule;
        {
            const unsigned n = (granule - KHDAYS_HEAPS_BASE) / heaps_granule;
            InterlockedOr(&heaps_touched[n / 32], (LONG)(1u << n % 32));
        }
    }
    ReleaseSRWLockExclusive(&heaps_lock);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static size_t heaps_touched_words(void)
{
    return ((KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE) / heaps_granule + 31) / 32;
}

void khdays_heaps_reset(void)
{
    AcquireSRWLockExclusive(&heaps_lock);
#pragma warning(suppress : 6250) /* decommitted on purpose: the range stays reserved */
    VirtualFree(at(KHDAYS_HEAPS_BASE), KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE, MEM_DECOMMIT);
    heaps_committed = 0;
    memset((void *)heaps_touched, 0, heaps_touched_words() * sizeof(*heaps_touched));
    ReleaseSRWLockExclusive(&heaps_lock);
}

static int reserve_heaps(void)
{
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    heaps_granule = system.dwAllocationGranularity;
    heaps_touched = calloc(heaps_touched_words(), sizeof(*heaps_touched));
    if (heaps_touched == NULL || !carve("the game's heaps", KHDAYS_HEAPS_BASE, KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE)) {
        return 0;
    }
    if (virtual_alloc2(GetCurrentProcess(), at(KHDAYS_HEAPS_BASE), KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE,
                       MEM_RESERVE | MEM_REPLACE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0) != at(KHDAYS_HEAPS_BASE)) {
        return fail("the game's heaps", KHDAYS_HEAPS_BASE);
    }
    /* first: ahead of the handlers that stop the game on an access fault */
    AddVectoredExceptionHandler(1, heaps_commit);
    return 1;
}

/* The window: the DS's own addresses held first, then the window reserved as one placeholder. */
static int reserve_window(void)
{
    HMODULE kernelbase = GetModuleHandleA("kernelbase.dll");
    virtual_alloc2 = kernelbase ? (VirtualAlloc2Fn)(void *)GetProcAddress(kernelbase, "VirtualAlloc2") : NULL;
    map_view_of_file3 = kernelbase ? (MapViewOfFile3Fn)(void *)GetProcAddress(kernelbase, "MapViewOfFile3") : NULL;
    if (virtual_alloc2 == NULL || map_view_of_file3 == NULL) {
        fprintf(stderr, "memory map: this Windows cannot hold the game's memory as one range "
                        "(VirtualAlloc2 and MapViewOfFile3: Windows 10 1803 and later)\n");
        return 0;
    }
    keep_free(0x01ff0000, 0x0b010000);
    khdays_window = (unsigned char *)virtual_alloc2(NULL, NULL, WINDOW_SIZE, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                                                    PAGE_NOACCESS, NULL, 0);
    if (khdays_window == NULL) {
        fprintf(stderr, "memory map: no room for the game's memory (0x%llx bytes, error %lu)\n",
                (unsigned long long)WINDOW_SIZE, GetLastError());
        return 0;
    }
    khdays_snd7_native_set_ds_base((uintptr_t)khdays_window);
    khdays_context_stack_source(window_stack, window_stack_release);
    return 1;
}

int khdays_memory_map(void)
{
    HANDLE ram;
    if (!reserve_window()) {
        return 0;
    }
    ram = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, MAIN_RAM_SIZE, NULL);
    if (ram == NULL) {
        return fail("main RAM section", 0x02000000);
    }
    /* Main RAM and its mirrors; the second leaves the DTCM's granule out. The two upper mirrors hold the
     * headers' heaps instead, unless the heaps are made as on the DS. */
    if (!view(ram, "main RAM", 0x02000000, 0, MAIN_RAM_SIZE) ||
        !view(ram, "main RAM mirror", 0x02400000, 0, DTCM_BASE - 0x02400000) ||
        !view(ram, "main RAM mirror (shared area)", DTCM_BASE + GRANULE, DTCM_BASE + GRANULE - 0x02400000,
              0x02800000 - (DTCM_BASE + GRANULE))) {
        return 0;
    }
    if (khdays_game_heap_ds_memory()) {
        if (!view(ram, "main RAM mirror", 0x02800000, 0, MAIN_RAM_SIZE) ||
            !view(ram, "main RAM mirror", 0x02c00000, 0, MAIN_RAM_SIZE)) {
            return 0;
        }
    } else if (!plain("the headers' heaps", KHDAYS_HEADERS_BASE, 0x03000000u - KHDAYS_HEADERS_BASE)) {
        return 0;
    }
    /* The I/O registers' and the IPC/card ports' DS ranges stay inaccessible: the port reaches them. */
    if (!(plain("ITCM", 0x01ff8000, 0x8000) && plain("DTCM", DTCM_BASE, 0x4000) &&
          plain("shared WRAM", 0x03000000, 0x8000) &&
          plain("ARM7 WRAM", ARM7_WRAM_BASE, ARM7_WRAM_END - ARM7_WRAM_BASE) && plain("palettes", 0x05000000, 0x800) &&
          plain("VRAM", 0x06000000, 0x008a4000) && plain("OAM", 0x07000000, 0x800) && empty_gba_slot())) {
        return 0;
    }
    /* The rewritten game's areas above the DS's ranges (port/tools/references/build.py): the register
     * model's memory and the data area. */
#pragma warning(suppress : 6326) /* the data area's bounds are the build's numbers: it may be empty */
    if (!(plain("the register model's memory", KHDAYS_IO_VIEW, KHDAYS_IO_SIZE + KHDAYS_IO2_SIZE) &&
          (KHDAYS_DATA_AREA_END == KHDAYS_DATA_AREA ||
           plain("the data area", KHDAYS_DATA_AREA, KHDAYS_DATA_AREA_END - KHDAYS_DATA_AREA)))) {
        return 0;
    }
    /* the enemies' overlays, each at its own place, and ov107's slot tables for every class (enemy_overlays.h) */
    if (!plain("the enemies' overlays", KHDAYS_ENEMY_OVERLAYS,
               (KHDAYS_ENEMY_OVERLAY_LAST - KHDAYS_ENEMY_OVERLAY_FIRST + 1) * KHDAYS_ENEMY_OVERLAY_SPAN) ||
        !plain("the enemies' slots", KHDAYS_ENEMY_HANDLERS, KHDAYS_ENEMY_SLOTS_END - KHDAYS_ENEMY_HANDLERS)) {
        return 0;
    }
    if (!reserve_heaps()) {
        return 0;
    }
    khdays_io_host = at(KHDAYS_IO_VIEW);
    khdays_io2_host = at(KHDAYS_IO2_VIEW);
    return 1;
}
