/* Which overlay occupies which address now.
 *
 * Overlays share address ranges: on the DS a call into such a range runs
 * whatever overlay was loaded there last, and the instruction only holds the
 * address. The decomp's relocations list every overlay a reference may reach
 * (module:overlays(a,b)); natively each overlay's code is its own function, so
 * those references go through thunks (prepare.py, absolute.asm) that ask here
 * which one is loaded and jump to its function. */
#include "../hal/hal.h"
#include "rom.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_OVERLAYS 512

static struct {
    u32 start, end;  /* RAM range, BSS included */
    u32 loaded;      /* load order, 0 = not loaded */
} overlays[MAX_OVERLAYS];
static int count = -1;
static u32 loads;

/* The ARM9 overlay table (header 0x50/0x54): 32-byte entries of id, RAM
 * address, RAM size, BSS size, static initializers and file id. */
static void read_table(void)
{
    const u8 *header = khdays_rom_header();
    const u32 offset = *(const u32 *)(header + 0x50);
    const u32 size = *(const u32 *)(header + 0x54);
    count = 0;
    for (u32 at = 0; at + 32 <= size && count < MAX_OVERLAYS; at += 32) {
        u32 entry[8];
        khdays_rom_read(offset + at, (u8 *)entry, sizeof(entry));
        if (entry[0] < MAX_OVERLAYS) {
            overlays[entry[0]].start = entry[1];
            overlays[entry[0]].end = entry[1] + entry[2] + entry[3];
            if ((int)entry[0] >= count) {
                count = (int)entry[0] + 1;
            }
        }
    }
}

/* FS_StartOverlay ran for overlay `id`: it now holds its range, and any
 * overlay it overlaps is gone. */
void khdays_overlay_started(int id)
{
    if (count < 0) {
        read_table();
    }
    if (id < 0 || id >= count) {
        return;
    }
    for (int n = 0; n < count; ++n) {
        if (n != id && overlays[n].loaded && overlays[n].start < overlays[id].end &&
            overlays[id].start < overlays[n].end) {
            overlays[n].loaded = 0;
        }
    }
    overlays[id].loaded = ++loads;
}

/* The overlay loaded at `address`, or -1. */
int khdays_overlay_at(u32 address)
{
    int best = -1;
    for (int n = 0; n < count; ++n) {
        if (overlays[n].loaded && overlays[n].start <= address && address < overlays[n].end &&
            (best < 0 || overlays[n].loaded > overlays[best].loaded)) {
            best = n;
        }
    }
    return best;
}

/* A thunk found none of its overlays loaded at its address. */
void khdays_overlay_call_missing(u32 address)
{
    fprintf(stderr, "khdays-native: a call into 0x%08x found none of the overlays the caller can "
                    "reach there loaded (loaded there: %d, -1 = none)\n",
            address, khdays_overlay_at(address));
    fflush(stderr);
    exit(11);
}
