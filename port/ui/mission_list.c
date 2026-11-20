/* The camp's mission list from the game's files (mission_list.h). The formats: docs/RUNTIME_MAP.md
 * section 14, "The badges on the mission's panel" and "The crowns on the story's day list". */
#include "mission_list.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIST_FILE "UI/cm/cm.p2"
#define LIST_CHARACTERS 17 /* its entries */
#define LIST_PALETTE 20
#define CHESTS_FILE "mi/mi/trbox" /* the ROM's name; the game asks for "mi/mi/trBox" (the DS ignores case) */
#define CHEST_FLAG_BASE 9         /* Ov025_CountGroupFlagsSet: the entry's number + 9 */

static KhdaysRomReader *reader; /* its own: this runs at VBlank, maybe inside the game's own reading */

static int read_file(const char *path, int entry, uint8_t **data, uint32_t *size)
{
    if (reader == NULL)
        reader = khdays_rom_reader_new();
    return khdays_rom_file_data(reader, path, entry, data, size);
}

/* --- The sheet ------------------------------------------------------------------------ */

static uint8_t *characters; /* its characters, 64 bytes each */
static uint32_t character_count;
static uint32_t colours[256]; /* the palette, as the picture's colours (RGBA) */
static int sheet;             /* 0: not read yet, 1: read, -1: not there */

/* The colour the composition makes of a DS colour (elements.c rgb_of). */
static uint32_t rgba_of(uint32_t bgr555)
{
    uint32_t out = 0xff000000u;
    for (int n = 0; n < 3; ++n) {
        const uint32_t c = (bgr555 >> (5 * n)) & 31u;
        out |= ((c << 3) | (c >> 2)) << (8 * n);
    }
    return out;
}

/* The characters, from the NCGR of entry 17: its CHAR block follows the header, its pixels start 8
 * + the offset at +0x1c into it. */
static int read_characters(const uint8_t *d, uint32_t size)
{
    uint32_t block, depth, bytes, at;
    if (size < 0x10 || memcmp(d, "RGCN", 4) != 0)
        return 0;
    block = le16(d + 0xc);
    if (block + 0x20 > size || memcmp(d + block, "RAHC", 4) != 0)
        return 0;
    depth = le32(d + block + 0xc);
    bytes = le32(d + block + 0x18);
    at = block + 8 + le32(d + block + 0x1c);
    if (depth != 4 || at > size || bytes > size - at || bytes < 64u)
        return 0; /* 4: 8 bits a pixel */
    characters = (uint8_t *)malloc(bytes);
    if (characters == NULL)
        return 0;
    memcpy(characters, d + at, bytes);
    character_count = bytes / 64u;
    return 1;
}

/* The palette, from the D2KP pack of entry 20: its palettes' table (kind 0) at the offset at +8,
 * the first one's offset at +4 into it (Archive_GetMember); an NCLR whose PLTT block follows the
 * header, its colours 8 + the offset at +0x14 into it. */
static int read_palette(const uint8_t *d, uint32_t size)
{
    uint32_t table, nclr, block, bytes, at;
    if (size < 0x10 || memcmp(d, "D2KP", 4) != 0)
        return 0;
    table = le32(d + 8);
    if (table > size - 8)
        return 0;
    nclr = le32(d + table + 4);
    if (nclr > size - 0x10 || memcmp(d + nclr, "RLCN", 4) != 0)
        return 0;
    block = nclr + le16(d + nclr + 0xc);
    if (block > size - 0x18 || memcmp(d + block, "TTLP", 4) != 0)
        return 0;
    bytes = le32(d + block + 0x10);
    at = block + 8 + le32(d + block + 0x14);
    if (at > size || bytes > size - at || bytes < 512)
        return 0;
    for (int i = 0; i < 256; ++i)
        colours[i] = i == 0 ? 0u : rgba_of(le16(d + at + 2 * i) & 0x7fffu);
    return 1;
}

static int read_sheet(void)
{
    uint8_t *chars = NULL, *set = NULL;
    uint32_t chars_size = 0, set_size = 0;
    int ok;
    ok = read_file(LIST_FILE, LIST_CHARACTERS, &chars, &chars_size) &&
         read_file(LIST_FILE, LIST_PALETTE, &set, &set_size) && read_characters(chars, chars_size) &&
         read_palette(set, set_size);
    free(chars);
    free(set);
    if (!ok)
        fprintf(stderr, "ui: the mission list's sheet is not in %s (entries %d and %d)\n", LIST_FILE, LIST_CHARACTERS,
                LIST_PALETTE);
    return ok;
}

int khdays_mission_list_character(unsigned c, uint8_t out[64])
{
    if (sheet == 0)
        sheet = read_sheet() ? 1 : -1;
    if (sheet < 0 || c >= character_count)
        return 0;
    memcpy(out, characters + (size_t)c * 64u, 64);
    return 1;
}

const uint32_t *khdays_mission_list_colours(void)
{
    if (sheet == 0)
        sheet = read_sheet() ? 1 : -1;
    return sheet > 0 ? colours : NULL;
}

/* --- The records ---------------------------------------------------------------------- */

static KhdaysMissionRecord *records;
static int record_count;
static int records_language = -1;

/* Each record's length first (u16), then its room (+2), mission (+4), days (+6, +8), hidden (+0xc)
 * and kind (+0x23); a u16 count before them. */
static void read_records(int language)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"}; /* the firmware's numbering */
    char path[64];
    uint8_t *d = NULL;
    uint32_t size = 0, at = 2, count;
    record_count = 0;
    if (language < 1 || language > 5)
        return;
    snprintf(path, sizeof(path), "UI/cm/msl_%s.msi.z", codes[language]);
    if (!read_file(path, -1, &d, &size) || size < 2) {
        free(d);
        return;
    }
    count = le16(d);
    free(records);
    records = (KhdaysMissionRecord *)malloc((size_t)(count ? count : 1) * sizeof(*records));
    for (uint32_t i = 0; records != NULL && i < count; ++i) {
        const uint32_t length = at + 2 <= size ? le16(d + at) : 0;
        KhdaysMissionRecord *r = &records[record_count];
        if (length < 0x24 || length > size - at)
            break;
        r->room = (uint16_t)le16(d + at + 2);
        r->mission = (uint16_t)le16(d + at + 4);
        r->first_day = (uint16_t)le16(d + at + 6);
        r->last_day = (uint16_t)le16(d + at + 8);
        r->counted = le16(d + at + 0xc) != 1;
        r->kind = d[at + 0x23];
        ++record_count;
        at += length;
    }
    free(d);
}

const KhdaysMissionRecord *khdays_mission_records(int language, int *count)
{
    if (language != records_language) {
        records_language = language;
        read_records(language);
    }
    *count = record_count;
    return record_count > 0 ? records : NULL;
}

unsigned khdays_mission_badges(int language, uint16_t room, uint16_t mission)
{
    int n;
    const KhdaysMissionRecord *all = khdays_mission_records(language, &n);
    unsigned kinds = 0;
    int listed = 0;
    for (int i = 0; i < n; ++i) {
        const KhdaysMissionRecord *r = &all[i];
        if (r->room != room || !r->counted)
            continue;
        if (r->kind == 0)
            listed |= r->mission == mission;
        else if (r->kind <= 8)
            kinds |= 1u << (r->kind - 1);
    }
    return listed ? kinds : 0u;
}

/* --- The chests ----------------------------------------------------------------------- */

typedef struct Room {
    uint16_t room;
    int first, count; /* its flags in flags[] */
} Room;

static Room *rooms;
static int room_count;
static int *flags;
static int chests; /* 0: not read yet, 1: read, -1: not there */

/* The groups (Ov025_RelocateOffsetTable2, Ov025_CountGroupFlagsSet): their count in the first
 * byte, each one's offset from the file's start at +4 + 4 x its number; a group's room (u16), its
 * chest count (s8) at +2, and from +8 its chests, 0x14 bytes apart, the number (s16) first (a
 * group's last one shorter in the file: the last group ends 4 bytes before 0x14 would). */
static int read_chests(void)
{
    uint8_t *d = NULL;
    uint32_t size = 0, groups, total = 0, room = 0;
    int ok = 1;
    if (!read_file(CHESTS_FILE, -1, &d, &size) || size < 4) {
        free(d);
        fprintf(stderr, "ui: the chests' table %s is not there\n", CHESTS_FILE);
        return 0;
    }
    groups = d[0];
    if (4u + 4u * groups > size)
        ok = 0;
    for (uint32_t g = 0; ok && g < groups; ++g) {
        const uint32_t at = le32(d + 4 + 4 * g);
        const int n = at + 8u <= size ? (int)(int8_t)d[at + 2] : -1;
        if (n < 0 || (n > 0 && at + 8u + (uint32_t)(n - 1) * 0x14u + 2u > size))
            ok = 0;
        else
            total += (uint32_t)n;
    }
    room = total; /* the flags' room */
    rooms = ok ? (Room *)malloc((size_t)(groups ? groups : 1) * sizeof(*rooms)) : NULL;
    flags = ok ? (int *)malloc((size_t)(room ? room : 1) * sizeof(*flags)) : NULL;
    if (rooms == NULL || flags == NULL) {
        free(rooms);
        free(flags);
        rooms = NULL;
        flags = NULL;
        free(d);
        fprintf(stderr, "ui: the chests' table %s cannot be read\n", CHESTS_FILE);
        return 0;
    }
    total = 0;
    for (uint32_t g = 0; g < groups; ++g) {
        const uint32_t at = le32(d + 4 + 4 * g);
        Room *r = &rooms[room_count++];
        r->room = (uint16_t)le16(d + at);
        r->first = (int)total;
        r->count = (int)(int8_t)d[at + 2];
        for (int k = 0; k < r->count && total < room; ++k) {
            flags[total++] = (int)(int16_t)le16(d + at + 8 + (uint32_t)k * 0x14u) + CHEST_FLAG_BASE;
        }
    }
    free(d);
    return 1;
}

const int *khdays_mission_chests(uint16_t room, int *count)
{
    if (chests == 0)
        chests = read_chests() ? 1 : -1;
    for (int i = 0; chests > 0 && i < room_count; ++i) {
        if (rooms[i].room == room) {
            *count = rooms[i].count;
            return rooms[i].count > 0 ? flags + rooms[i].first : NULL;
        }
    }
    *count = chests > 0 ? 0 : -1;
    return NULL;
}
