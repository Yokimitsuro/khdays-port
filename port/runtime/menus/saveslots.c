/* The save files beyond the DS's three (saveslots.h). */
#include "saveslots.h"

#include "../core/session_log.h"
#include "../core/sha1.h"
#include "../../host/app/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC 0xc8f592a6u
#define BLANK (-1)
#define PORT_FILES (KHDAYS_SLOT_COUNT - KHDAYS_GAME_SLOTS)

static u8 *chip;
static u32 chip_size;
static char folder[256];
static int port_view;
static int chosen = -1; /* the file game slot 0 was in the port's view, -1 before one */
static int page = -1;   /* the page view's first file, -1 when it is not on */
static volatile int view[KHDAYS_GAME_SLOTS];
static u8 *files[PORT_FILES]; /* the port's files, KHDAYS_SLOT_BYTES each; NULL until there is one */
static int changed[PORT_FILES];
static u8 blank[KHDAYS_SLOT_BYTES]; /* a slot never written: each copy's first 0x20 bytes zero, the rest erased */
static u8 dropped;                  /* where a write to a blank slot goes */
static int told_dropped;
static volatile long generation;      /* counts the writes to the files */
static u8 written[KHDAYS_SLOT_COUNT]; /* the files written since khdays_slots_next_written took them */

/* ---- The files ------------------------------------------------------------------------------ */

static void file_path(int file, char *path, size_t size)
{
    snprintf(path, size, "%s/slots/slot%02d.sav", folder, file + 1);
}

/* File `file`'s bytes: the chip's slot or a port's file (NULL: none yet). */
static u8 *file_bytes(int file)
{
    if (file < KHDAYS_GAME_SLOTS) {
        const u32 at = KHDAYS_SLOTS_START + (u32)file * KHDAYS_SLOT_BYTES;
        return chip != NULL && at + KHDAYS_SLOT_BYTES <= chip_size ? chip + at : NULL;
    }
    return files[file - KHDAYS_GAME_SLOTS];
}

/* Port file `f` (counted after the chip's) as it is on the disk: NULL when it is not there. */
static void read_file(int f)
{
    char path[320];
    long read;
    free(files[f]);
    files[f] = NULL;
    changed[f] = 0;
    file_path(f + KHDAYS_GAME_SLOTS, path, sizeof(path));
    files[f] = (u8 *)malloc(KHDAYS_SLOT_BYTES);
    if (files[f] == NULL) {
        return;
    }
    read = khdays_host_read_file(path, files[f], KHDAYS_SLOT_BYTES);
    if (read <= 0) {
        free(files[f]);
        files[f] = NULL;
    } else if ((u32)read < KHDAYS_SLOT_BYTES) {
        memcpy(files[f] + read, blank + read, KHDAYS_SLOT_BYTES - (u32)read);
    }
}

void khdays_slots_attach(u8 *bytes, u32 size, const char *where)
{
    chip = bytes;
    chip_size = size;
    snprintf(folder, sizeof(folder), "%s", where);
    port_view = 0;
    chosen = -1;
    page = -1;
    told_dropped = 0;
    memset(blank, 0xff, sizeof(blank));
    memset(blank, 0, 0x20);
    memset(blank + KHDAYS_SLOT_COPY, 0, 0x20);
    for (int f = 0; f < PORT_FILES; ++f) {
        read_file(f);
    }
    for (int g = 0; g < KHDAYS_GAME_SLOTS; ++g) {
        view[g] = g;
    }
}

void khdays_slots_set_port_view(int on)
{
    if ((on == port_view && page < 0) || chip == NULL) {
        return;
    }
    port_view = on;
    page = -1;
    if (on && (chosen < 0 || khdays_slots_state(chosen, NULL) != KHDAYS_SLOT_SAVED)) {
        /* a save under game slot 0 when there is one: the list the game builds then lets its
         * cursor take slot 0 (an empty one it skips) */
        const int latest = khdays_slots_latest();
        chosen = latest >= 0 ? latest : chosen >= 0 ? chosen : 0;
    }
    for (int g = 0; g < KHDAYS_GAME_SLOTS; ++g) {
        view[g] = on ? (g == 0 ? chosen : BLANK) : g;
    }
    khdays_log("save files: %s view, game slot 1 is file %d", on ? "the port's" : "the chip's own", view[0] + 1);
}

u8 *khdays_slots_byte(u32 offset, int writing, u32 *chip_at)
{
    u32 within;
    int g, file;
    u8 *bytes;
    *chip_at = 0xffffffffu;
    if (chip == NULL) {
        return NULL;
    }
    if (offset < KHDAYS_SLOTS_START || offset >= KHDAYS_SLOTS_START + KHDAYS_GAME_SLOTS * KHDAYS_SLOT_BYTES) {
        *chip_at = offset;
        return offset < chip_size ? chip + offset : NULL;
    }
    g = (int)((offset - KHDAYS_SLOTS_START) / KHDAYS_SLOT_BYTES);
    within = (offset - KHDAYS_SLOTS_START) % KHDAYS_SLOT_BYTES;
    file = view[g];
    if (file == BLANK) {
        if (!writing) {
            return &blank[within];
        }
        if (!told_dropped) {
            told_dropped = 1;
            khdays_log("save files: the game wrote its slot %d, blank in the port's view: dropped", g + 1);
        }
        return &dropped;
    }
    if (writing) {
        ++generation;
        written[file] = 1;
    }
    if (file < KHDAYS_GAME_SLOTS) {
        *chip_at = KHDAYS_SLOTS_START + (u32)file * KHDAYS_SLOT_BYTES + within;
        return *chip_at < chip_size ? chip + *chip_at : NULL;
    }
    bytes = files[file - KHDAYS_GAME_SLOTS];
    if (bytes == NULL) {
        if (!writing) {
            return &blank[within];
        }
        bytes = files[file - KHDAYS_GAME_SLOTS] = (u8 *)malloc(KHDAYS_SLOT_BYTES);
        if (bytes == NULL) {
            return &dropped;
        }
        memcpy(bytes, blank, KHDAYS_SLOT_BYTES);
    }
    if (writing) {
        changed[file - KHDAYS_GAME_SLOTS] = 1;
    }
    return bytes + within;
}

int khdays_slots_commit(void)
{
    int ok = 1;
    for (int f = 0; f < PORT_FILES; ++f) {
        char path[320];
        if (!changed[f] || files[f] == NULL) {
            continue;
        }
        file_path(f + KHDAYS_GAME_SLOTS, path, sizeof(path));
        if (khdays_host_write_file_atomic(path, files[f], KHDAYS_SLOT_BYTES)) {
            changed[f] = 0;
            khdays_log("save files: %s written whole", path);
        } else {
            ok = 0;
            khdays_log("save files: %s could not be written; the game is told its save failed", path);
            read_file(f); /* the file as it was, here too: nothing of it left for the next save to write */
        }
    }
    return ok;
}

int khdays_slots_next_written(void)
{
    for (int f = 0; f < KHDAYS_SLOT_COUNT; ++f) {
        if (written[f]) {
            written[f] = 0;
            return f;
        }
    }
    return -1;
}

int khdays_slots_changed(void)
{
    for (int f = 0; f < PORT_FILES; ++f) {
        if (changed[f] && files[f] != NULL) {
            return 1;
        }
    }
    return 0;
}

int khdays_slots_port_view(void)
{
    return port_view;
}

void khdays_slots_show_page(int top)
{
    if (chip == NULL) {
        return;
    }
    top = top < 0 ? 0 : top > KHDAYS_SLOT_COUNT - KHDAYS_GAME_SLOTS ? KHDAYS_SLOT_COUNT - KHDAYS_GAME_SLOTS : top;
    if (page == top) {
        return;
    }
    port_view = 0;
    page = top;
    for (int g = 0; g < KHDAYS_GAME_SLOTS; ++g) {
        view[g] = top + g;
    }
    khdays_log("save files: the page view, game slots 1-3 are files %d-%d", top + 1, top + KHDAYS_GAME_SLOTS);
}

int khdays_slots_page(void)
{
    return page;
}

u32 khdays_slots_generation(void)
{
    return (u32)generation;
}

int khdays_slots_shown(void)
{
    return view[0];
}

void khdays_slots_show(int file)
{
    if (port_view && file >= 0 && file < KHDAYS_SLOT_COUNT && view[0] != file) {
        view[0] = chosen = file;
        khdays_log("save files: game slot 1 is file %d", file + 1);
    }
}

/* Whether a copy is a save: the magic, and the SHA-1 of its state where the copy keeps it. */
static int signed_copy(const u8 *copy)
{
    u8 digest[20];
    const u32 magic = (u32)copy[0] | (u32)copy[1] << 8 | (u32)copy[2] << 16 | (u32)copy[3] << 24;
    if (magic != MAGIC) {
        return 0;
    }
    khdays_sha1(copy + 0x18, KHDAYS_STATE_BYTES, digest);
    return memcmp(digest, copy + 4, sizeof(digest)) == 0;
}

static int zero_head(const u8 *copy)
{
    for (int i = 0; i < 32; ++i) {
        if (copy[i] != 0) {
            return 0;
        }
    }
    return 1;
}

int khdays_slots_state(int file, const u8 **state)
{
    const u8 *bytes = file >= 0 && file < KHDAYS_SLOT_COUNT ? file_bytes(file) : NULL;
    if (state != NULL) {
        *state = NULL;
    }
    if (bytes == NULL) {
        return KHDAYS_SLOT_EMPTY;
    }
    for (int copy = 0; copy < 2; ++copy) {
        const u8 *c = bytes + (u32)copy * KHDAYS_SLOT_COPY;
        if (zero_head(c)) {
            return KHDAYS_SLOT_EMPTY;
        }
        if (signed_copy(c)) {
            if (state != NULL) {
                *state = c + 0x18;
            }
            return KHDAYS_SLOT_SAVED;
        }
    }
    return KHDAYS_SLOT_BROKEN;
}

u32 khdays_slots_field(const u8 *state, int bit, int bits)
{
    const u8 *words = state + 0x10;
    u32 result = 0;
    int word = bit / 32;
    bit %= 32;
    while (bits > 0) {
        const u32 w = (u32)words[4 * word] | (u32)words[4 * word + 1] << 8 | (u32)words[4 * word + 2] << 16 |
                      (u32)words[4 * word + 3] << 24;
        const int take = bits < 32 - bit ? bits : 32 - bit;
        result = (result << take) | ((w >> (32 - bit - take)) & (take == 32 ? 0xffffffffu : (1u << take) - 1u));
        bits -= take;
        bit = 0;
        ++word;
    }
    return result;
}

int khdays_slots_latest(void)
{
    int best = -1;
    u32 best_count = 0;
    for (int f = 0; f < KHDAYS_SLOT_COUNT; ++f) {
        const u8 *state;
        u32 count;
        if (khdays_slots_state(f, &state) != KHDAYS_SLOT_SAVED) {
            continue;
        }
        count = khdays_slots_field(state, 0xc77, 16) << 16 | khdays_slots_field(state, 0xc87, 16);
        if (best < 0 || count > best_count) {
            best = f;
            best_count = count;
        }
    }
    return best;
}

u32 khdays_slots_highest_counter(void)
{
    const int latest = khdays_slots_latest();
    const u8 *state;
    if (latest < 0 || khdays_slots_state(latest, &state) != KHDAYS_SLOT_SAVED) {
        return 0;
    }
    return khdays_slots_field(state, 0xc77, 16) << 16 | khdays_slots_field(state, 0xc87, 16);
}

int khdays_slots_file_of(int slot)
{
    return slot >= 0 && slot < KHDAYS_GAME_SLOTS && view[slot] != BLANK ? view[slot] : -1;
}

void khdays_slots_keep_view(KhdaysSlotsView *kept)
{
    kept->port_view = port_view;
    kept->chosen = chosen;
    kept->page = page;
    for (int g = 0; g < KHDAYS_GAME_SLOTS; ++g) {
        kept->view[g] = view[g];
    }
}

void khdays_slots_put_view(const KhdaysSlotsView *kept)
{
    port_view = kept->port_view;
    chosen = kept->chosen;
    page = kept->page;
    for (int g = 0; g < KHDAYS_GAME_SLOTS; ++g) {
        view[g] = kept->view[g];
    }
}
