/* The new missions' progress beside a save file: saves/missions/fileNN.txt (a further player's under
 * saves/player<N>/), so the game's own save stays as the DS's -- a save loaded without the mods, or by an
 * older port, loads as before. UTF-8 text, a line for each mission with progress:
 *   <mod id>/<key> <status> <stage> <transfer> <badges> <best> [<a 0 or 1 for each chest: opened>]
 * Lines of missions no mod adds now are kept as read and written back. Plain C: no game, no system calls. */
#ifndef KHDAYS_MISSIONS_SAVE_PART_H
#define KHDAYS_MISSIONS_SAVE_PART_H

#include "registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysSavePart {
    char **kept; /* the lines of missions this run does not have */
    int kept_count, kept_capacity;
} KhdaysSavePart;

/* Reads a save part's text: the store's missions (cleared first) get their progress, the other lines go to
 * `kept` (emptied first). */
void khdays_save_part_read(const char *text, size_t size, const KhdaysMissions *r, KhdaysFieldStore *s,
                           KhdaysSavePart *kept);

/* The text for the store's progress and the kept lines, malloc'd with a 0 after it, its length in *size; NULL
 * with no memory. */
char *khdays_save_part_text(const KhdaysMissions *r, const KhdaysFieldStore *s, const KhdaysSavePart *kept,
                            size_t *size);

void khdays_save_part_free(KhdaysSavePart *kept);

#ifdef __cplusplus
}
#endif

#endif
