/* The game card's ROM as the card bus serves it, assembled from the port's
 * extracted data and mods (rom.cpp). */
#ifndef KHDAYS_ROM_H
#define KHDAYS_ROM_H

#include "../../hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Finds the extracted data (data/extracted/<hash>) and indexes it. Returns 0
 * and says why on failure. */
int khdays_rom_init(void);

/* The 0x200-byte cartridge header. */
const u8 *khdays_rom_header(void);

/* The US release (game code YKGE) is the EU build the port compiles, with its static module from
 * KHDAYS_US_SHIFT_FROM on and every overlay KHDAYS_US_SHIFT bytes lower. rom.cpp serves its overlay
 * table at the EU addresses; start.c moves its static module's image up once crt0 is done with it.
 * Nonzero for such a card. */
int khdays_rom_is_us(void);

/* `language` (the firmware's numbering, 1 English .. 5 Spanish) if the card has its text, else English:
 * a US card has English and French only, and its own code falls back to English as this does. */
int khdays_rom_language(int language);
#define KHDAYS_US_SHIFT 0x20u
#define KHDAYS_US_SHIFT_FROM 0x02000c20u /* the US card's address of the EU build's 0x02000c40 */

/* `size` bytes of the ROM at `offset`. */
void khdays_rom_read(u32 offset, u8 *destination, u32 size);

/* A file of the game's filesystem by its path ("/snd/sound_data.sdat", as
 * the FNT names it; a leading / is the root): where the ROM holds it -- the
 * FAT the game reads, a moved mod file's place. 0 if there is none. */
int khdays_rom_find(const char *path, u32 *offset, u32 *size);

/* The game's files by FAT id, 0 to khdays_rom_file_count() - 1: each one's
 * path as the FNT names it (no leading /) and where the ROM holds it. 0 for
 * an id with no file. */
int khdays_rom_file_count(void);
int khdays_rom_file_info(int id, char *path, int size, u32 *offset, u32 *bytes);

/* The game file the ROM holds at `offset`: its path as the FNT names it
 * ("UI/cm/cmo_es.p2.z", no leading /) into `path`, and how far into it
 * `offset` is. 0 if no file is there. */
int khdays_rom_file_at(u32 offset, char *path, int size, u32 *within);

/* What the image holds at `offset`, for a log: a game file's path as the FNT
 * names it, or the system part ("(the FAT)", "(the ARM9 binary)", ...), and
 * that part's range in the image (a moved mod file's place). 0 for padding
 * and the ranges the extraction does not keep. */
int khdays_rom_area_at(u32 offset, char *name, int size, u32 *start, u32 *end);

/* A file the game does not have (a mod's new mission), at `disk` (UTF-8), served as `path` ("mi/mi/20000",
 * no leading /) past everything else: the game opens it by that path (FS_OpenFile's join,
 * khdays_rom_added_file), the file table does not name it. Before the game starts. 0 when it cannot be read. */
int khdays_rom_add_file(const char *path, const char *disk);

/* The same for a file made by the port (a mission built from a mod's description): `size` bytes, copied. */
int khdays_rom_add_bytes(const char *path, const unsigned char *data, unsigned int size);

/* The same for a game file served under another path too ("mi/mi/0010" as "mi/mi/20000"). 0 when the game
 * has no `game_path`. */
int khdays_rom_add_alias(const char *path, const char *game_path);

/* For FS_OpenFile (port/tools/prepare.py): whether `path` is an added file, with where the image holds it
 * (bounds[0], bounds[1]) and its id past the table's (bounds[2]). */
int khdays_rom_added_file(const char *path, unsigned int *bounds);

/* A file a mod provides at mods/<Mod>/<relative> ("sounds/strm/X.ogg"), in
 * the mods' order (khdays::vfs::find_in_mods): its path, UTF-8, into `path`.
 * 0 if no mod has it. */
int khdays_mods_find(const char *relative, char *path, int size);

/* A reader of its own for a thread other than the game's (the sound's):
 * the same bytes as khdays_rom_read, through its own open files. */
typedef struct KhdaysRomReader KhdaysRomReader;
KhdaysRomReader *khdays_rom_reader_new(void);
void khdays_rom_reader_read(KhdaysRomReader *reader, u32 offset, u8 *destination, u32 size);

/* A game file's data as the game reads it, through `reader`: with `entry` below 0 the whole file
 * (LZ-decompressed for ".z"), else that entry of its "P2" archive (the header
 * Archive_OpenSubfileByHandle reads), decompressed when marked so. Malloc'd into *data, which the
 * caller frees. 0 if there is no such file or entry. */
int khdays_rom_file_data(KhdaysRomReader *reader, const char *path, int entry, u8 **data, u32 *size);

#ifdef __cplusplus
}
#endif

#endif
