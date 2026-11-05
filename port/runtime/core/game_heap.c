/* The game's two heaps (game_heap.h). The game allocates everything from the two NitroSystem
 * expanded heaps Ov001_CreateMainAndSubHeaps makes from the DS's main arena (an 800 KB main heap and
 * a sub heap of the rest). Their creation comes here and makes them over the port's region of the
 * game's memory (window.h), which the PC gives as the game touches it. KHDAYS_DS_MEMORY makes them as
 * on the DS. */
#include "game_heap.h"
#include "runtime.h"
#include "session_log.h"
#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the game's (NitroSystem's and NitroSDK's), as the rewritten C has them */
extern KhdaysRef NNS_FndCreateExpHeapEx(KhdaysRef start, unsigned int size, unsigned short option);
extern void NNS_FndDestroyExpHeap(KhdaysRef heap);
extern KhdaysRef NNS_FndAllocFromExpHeapEx(KhdaysRef heap, unsigned int size, int align);
extern KhdaysRef NNS_FndAllocFromDefaultExpHeapEx(unsigned int size, int align);
extern void NNS_FndFreeToExpHeap(KhdaysRef heap, KhdaysRef block);
extern KhdaysRef OS_AllocFromArenaLo(int arena, unsigned int size, unsigned int align);
extern KhdaysRef OS_GetArenaLo(int arena);

#define OS_ARENA_MAIN 0
#define HEAP_ALIGN 0x10 /* Ov001_CreateMainAndSubHeaps's */

/* the handles Ov001_InitGameHeaps keeps; gCurrentHeap points at one of them */
#define MAIN_HEAP_HANDLE 0x0204c044u
#define SUB_HEAP_HANDLE 0x0204c030u
#define CURRENT_HEAP 0x0204c028u

static const char *const names[2] = {"main heap", "sub heap"};

/* The headers' heaps: a packed archive's header is named in the game's file handles by 24 bits from
 * 0x01ff8000, so it must lie below 0x02ff8000. Each game heap has a headers' heap there, made and
 * remade with it. */
static KhdaysRef headers[2];
static const KhdaysRef headers_span[2][2] = {{KHDAYS_HEADERS_MID, KHDAYS_HEADERS_END},
                                             {KHDAYS_HEADERS_BASE, KHDAYS_HEADERS_MID}};

/* The game's resource cache (ResCache_FindSlot): 128 entries of 0x30 bytes, a reference count and a
 * file's name or handle, kept while used even after its header is freed. A header given out where a
 * freed one was would be found under the old entries (a second character's weapons were the first's),
 * so a place still named in the cache is kept aside (`parked`) until none names it (docs/PLATFORM.md). */
#define FILE_LOADER 0x0204bbfcu /* gFileLoader */
#define CACHE_ENTRIES 0x80
#define CACHE_ENTRY 0x30u

/* The cache past its 128 entries: when every entry is in use (ResCache_FindSlot found none, and ResCache_Acquire read
 * through the null it gave: many enemy kinds, several players' characters), another table of 128 from the main heap,
 * searched after the game's. The entries never move: the game keeps pointers to them. */
static KhdaysRef *more_tables;
static int more_count, more_room;

static struct Parked {
    KhdaysRef block;
    int which;
} *parked;
static int parked_count, parked_room;

static unsigned rd32(KhdaysRef at)
{
    return *(const volatile unsigned *)KHDAYS_W(at);
}

void *khdays_res_cache_table(int n)
{
    if (n == 0) {
        const KhdaysRef table = rd32(FILE_LOADER + 8u);
        return khdays_game_ram(table) ? KHDAYS_W(table) : NULL;
    }
    return n - 1 < more_count ? KHDAYS_W(more_tables[n - 1]) : NULL;
}

void *khdays_res_cache_grow(void)
{
    const KhdaysRef heap = rd32(MAIN_HEAP_HANDLE);
    KhdaysRef table;
    if (more_count == more_room) {
        const int room = more_room ? more_room * 2 : 4;
        KhdaysRef *grown = (KhdaysRef *)realloc(more_tables, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        more_tables = grown;
        more_room = room;
    }
    table = heap != 0 ? NNS_FndAllocFromExpHeapEx(heap, CACHE_ENTRIES * CACHE_ENTRY, 4) : 0;
    if (table == 0) {
        return NULL;
    }
    memset(KHDAYS_W(table), 0, CACHE_ENTRIES * CACHE_ENTRY);
    more_tables[more_count++] = table;
    khdays_log("memory: the resource cache grown to %d entries", (more_count + 1) * CACHE_ENTRIES);
    return KHDAYS_W(table);
}

/* Whether an entry of the cache in use names a header at `at`. */
static int named_in_cache(KhdaysRef at)
{
    const unsigned key = ((at + 0x8000u) & 0xfffffcu) << 7;
    void *host;
    for (int n = 0; (host = khdays_res_cache_table(n)) != NULL; ++n) {
        const KhdaysRef table = KHDAYS_REF(host);
        for (unsigned i = 0; i < CACHE_ENTRIES; ++i) {
            const KhdaysRef entry = table + CACHE_ENTRY * i;
            const unsigned id = rd32(entry + 0x10u);
            if ((rd32(entry) & 0xffffu) != 0 && (id & 0x80000000u) && (id & 0x7ffffe00u) == key) {
                return 1;
            }
        }
    }
    return 0;
}

/* The places kept aside that no entry names any more go back to their heap. */
static void unpark(void)
{
    int kept = 0;
    for (int i = 0; i < parked_count; ++i) {
        if (named_in_cache(parked[i].block)) {
            parked[kept++] = parked[i];
        } else {
            NNS_FndFreeToExpHeap(headers[parked[i].which], parked[i].block);
        }
    }
    parked_count = kept;
}

static int park(KhdaysRef block, int which)
{
    static int said;
    if (parked_count == parked_room) {
        const int room = parked_room ? parked_room * 2 : 8;
        struct Parked *grown = (struct Parked *)realloc(parked, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return 0;
        }
        parked = grown;
        parked_room = room;
    }
    parked[parked_count].block = block;
    parked[parked_count++].which = which;
    if (!said) {
        said = 1;
        khdays_log("memory: a header's place still named in the resource cache kept aside (%08x)", block);
    }
    return 1;
}

static void make_headers(int which)
{
    const KhdaysRef start = headers_span[which][0], end = headers_span[which][1];
    headers[which] = NNS_FndCreateExpHeapEx(start, end - start, 0);
    if (headers[which] == 0) {
        fprintf(stderr, "memory: the %s's headers' heap could not be made\n", names[which]);
        khdays_log("memory: the %s's headers' heap could not be made", names[which]);
    }
}

/* Which of the game's heaps `handle` is: 0, 1, or -1 for neither. */
static int which_heap(KhdaysRef handle)
{
    if (handle != 0 && handle == *(const volatile unsigned *)KHDAYS_W(MAIN_HEAP_HANDLE)) {
        return 0;
    }
    if (handle != 0 && handle == *(const volatile unsigned *)KHDAYS_W(SUB_HEAP_HANDLE)) {
        return 1;
    }
    return -1;
}

int khdays_game_heap_ds_memory(void)
{
    static int ds = -1;
    if (ds < 0) {
        const char *value = getenv("KHDAYS_DS_MEMORY");
        ds = value != NULL && value[0] != '\0' && value[0] != '0';
    }
    return ds;
}

void *khdays_game_heap(int which, unsigned int ds_size)
{
    const char *name = names[which != 0];
    const char *how;
    KhdaysRef heap;
    if (khdays_game_heap_ds_memory()) {
        heap = NNS_FndCreateExpHeapEx(OS_AllocFromArenaLo(OS_ARENA_MAIN, ds_size, HEAP_ALIGN), ds_size, 0);
        how = "as on the DS";
    } else {
        const KhdaysRef start = which == 0 ? KHDAYS_HEAPS_BASE : KHDAYS_MAIN_HEAP_END;
        const KhdaysRef end = which == 0 ? KHDAYS_MAIN_HEAP_END : KHDAYS_HEAPS_END;
        heap = NNS_FndCreateExpHeapEx(start, end - start, 0);
        how = "over the port's region";
        make_headers(which != 0);
    }
    if (heap == 0) {
        fprintf(stderr, "memory: the %s could not be made (%s)\n", name, how);
        khdays_log("memory: the %s could not be made (%s)", name, how);
        return NULL;
    }
    /* the heap's head: its start and end at +0x18 and +0x1c (NNSi_FndInitHeapHead) */
    khdays_log("memory: the %s %s, %08x..%08x", name, how, *(const unsigned *)KHDAYS_W(heap + 0x18),
               *(const unsigned *)KHDAYS_W(heap + 0x1c));
    return KHDAYS_W(heap);
}

/* The heap's start and end at its head's +0x18 and +0x1c; its free blocks
 * listed from +0x24, each block's size at +4 and the next at +0xc, as
 * NNS_FndGetAllocatableSizeForExpHeapEx walks them. */
int khdays_game_heap_figures(unsigned heap, KhdaysHeapFigures *out)
{
    unsigned block;
    out->start = out->end = out->free = out->blocks = out->largest = 0;
    if (!khdays_game_ram(heap)) {
        return 0;
    }
    out->start = *(const volatile unsigned *)KHDAYS_W(heap + 0x18);
    out->end = *(const volatile unsigned *)KHDAYS_W(heap + 0x1c);
    for (block = *(const volatile unsigned *)KHDAYS_W(heap + 0x24); khdays_game_ram(block) && out->blocks < 1000000;
         ++out->blocks) {
        const unsigned size = *(const volatile unsigned *)KHDAYS_W(block + 4);
        out->free += size;
        if (size > out->largest) {
            out->largest = size;
        }
        block = *(const volatile unsigned *)KHDAYS_W(block + 0xc);
    }
    return 1;
}

void *khdays_header_alloc(unsigned int size)
{
    KhdaysRef block;
    int which = -1;
    if (!khdays_game_heap_ds_memory()) {
        /* the heap the game's default allocation would take it from */
        const KhdaysRef current = *(const volatile unsigned *)KHDAYS_W(CURRENT_HEAP);
        which = current == MAIN_HEAP_HANDLE ? 0 : current == SUB_HEAP_HANDLE ? 1 : -1;
    }
    if (which < 0 || headers[which] == 0) {
        block = NNS_FndAllocFromDefaultExpHeapEx(size, 4);
        return block != 0 ? KHDAYS_W(block) : NULL;
    }
    unpark();
    block = NNS_FndAllocFromExpHeapEx(headers[which], size, 4);
    while (block != 0 && named_in_cache(block) && park(block, which)) {
        block = NNS_FndAllocFromExpHeapEx(headers[which], size, 4);
    }
    return block != 0 ? KHDAYS_W(block) : NULL;
}

void *khdays_header_heap_of(void *heap, void *block)
{
    const KhdaysRef at = KHDAYS_REF(block);
    for (int k = 0; k < 2; ++k) {
        if (headers[k] != 0 && at >= headers_span[k][0] && at < headers_span[k][1]) {
            return KHDAYS_W(headers[k]);
        }
    }
    return heap;
}

void khdays_game_heap_recreated(void *heap)
{
    const int which = which_heap(KHDAYS_REF(heap));
    if (which == 0) {
        more_count = 0;  /* the cache's own tables went with the main heap */
    }
    if (which < 0 || headers[which] == 0 || khdays_game_heap_ds_memory()) {
        return;
    }
    {
        /* the places kept aside in it go with it */
        int kept = 0;
        for (int i = 0; i < parked_count; ++i) {
            if (parked[i].which != which) {
                parked[kept++] = parked[i];
            }
        }
        parked_count = kept;
    }
    NNS_FndDestroyExpHeap(headers[which]);
    make_headers(which);
}

void khdays_handle_check(unsigned int handle, unsigned int base)
{
    static unsigned said;
    if (khdays_game_heap_ds_memory() || (base >= KHDAYS_HEADERS_BASE && base < KHDAYS_HEADERS_END) ||
        (base >= 0x02000000u && base < OS_GetArenaLo(OS_ARENA_MAIN))) {
        return; /* a header in its heap, or in the game's own data */
    }
    if (said++ < 16) {
        fprintf(stderr, "memory: the file handle %08x names 0x%08x, outside the headers' heaps\n", handle, base);
        khdays_log("memory: the file handle %08x names 0x%08x, outside the headers' heaps", handle, base);
    }
}

const char *khdays_game_heap_name(unsigned heap, char *buffer, unsigned size)
{
    const int which = which_heap(heap);
    if (which >= 0) {
        return names[which];
    }
    for (int k = 0; k < 2; ++k) {
        if (heap != 0 && heap == headers[k]) {
            snprintf(buffer, size, "%s's headers' heap", names[k]);
            return buffer;
        }
    }
    snprintf(buffer, size, "heap at %08x", heap);
    return buffer;
}

/* Each heap and size reported once (the first 64); after that a line at each power of two of the
 * failures. The game handles the failure as ever. */
void khdays_game_heap_failed(void *heap, unsigned int size)
{
    static struct {
        unsigned heap, size;
    } said[64];
    static unsigned nsaid, failures;
    const unsigned ref = KHDAYS_REF(heap);
    KhdaysHeapFigures figures;
    char buffer[48];
    const char *name = khdays_game_heap_name(ref, buffer, sizeof(buffer));
    ++failures;
    for (unsigned i = 0; i < nsaid; ++i) {
        if (said[i].heap == ref && said[i].size == size) {
            return;
        }
    }
    if (nsaid == sizeof(said) / sizeof(said[0])) {
        if ((failures & (failures - 1)) == 0) {
            khdays_log("memory: %u allocations the game's heaps could not give so far", failures);
        }
        return;
    }
    said[nsaid].heap = ref;
    said[nsaid].size = size;
    ++nsaid;
    khdays_game_heap_figures(ref, &figures);
    fprintf(stderr, "memory: the %s could not give %u bytes (%u free, the largest %u)\n", name, size, figures.free,
            figures.largest);
    khdays_log("memory: the %s could not give %u bytes (%u free, the largest %u)", name, size, figures.free,
               figures.largest);
}
