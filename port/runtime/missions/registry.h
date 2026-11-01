/* The missions the mods add: each mod's missions/missions.txt read into descriptions, then given this run's
 * numbers -- the game's mission number and file number -- in the mods' order, so the same mods give the same
 * numbers on every copy. The numbers are never saved: a mission is known by its identity, "<mod id>/<key>".
 * docs/RUNTIME_MAP.md, "Missions and worlds of mods". Plain C: no game, no system calls. */
#ifndef KHDAYS_MISSIONS_REGISTRY_H
#define KHDAYS_MISSIONS_REGISTRY_H

#include "field_store.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_MISSION_LANGUAGES 5 /* the game's order (GetLanguage - 1): en fr de it es */

/* The game's missions are 1-94 (94 Mission Mode's tutorial, listed as 00; and the story's hidden 900-916); a new
 * mission's number puts its fields past the flag array (0x92b + 4 x 3510 = 0x4003) and fits the list record's
 * 16 bits. */
#define KHDAYS_MISSION_FIRST 3510
#define KHDAYS_MISSION_LAST 0xffff
#define KHDAYS_GAME_MISSIONS 94
/* Mission files "mi/mi/<file>": past the game's (to 10016); the field keeps the number in a signed 16 bits. */
#define KHDAYS_FILE_FIRST 20000
#define KHDAYS_FILE_LAST 32767

/* What a mission built by khdays-studio is made of (its file then the studio's: studio/core/missions/
 * mission_build.h); read here so that a description that does not read is said where the port reads it. */
typedef struct KhdaysMissionEnemies {
    int enemy_class, count;
} KhdaysMissionEnemies;

typedef struct KhdaysMissionSpawner {
    int wave; /* 1 at the start, each next once all before it are defeated */
    double at[3];
    KhdaysMissionEnemies *enemies;
    int enemy_count;
} KhdaysMissionSpawner;

/* The kinds a mission's places are built into (`kind = ...`). */
typedef enum KhdaysMissionKind {
    KHDAYS_MISSION_WAVES,     /* cleared once its last wave is defeated */
    KHDAYS_MISSION_ELIMINATE, /* cleared once `goal` enemies of its target class are defeated (all of them by default) */
    KHDAYS_MISSION_HEARTS,    /* cleared once `goal` hearts are collected (all its enemies' by default) */
    KHDAYS_MISSION_EMBLEMS,   /* cleared once its Organization emblems give `goal` points (8 each by default) */
    KHDAYS_MISSION_REACH,     /* cleared once a player reaches its place */
} KhdaysMissionKind;

typedef struct KhdaysMissionChest {
    int item; /* its contents: an item of the game */
    double at[3];
    int heading; /* degrees */
} KhdaysMissionChest;

/* The most chests a mission has: its rows' count is an s8 (trbox). */
#define KHDAYS_MOST_CHESTS 127

typedef struct KhdaysMissionEmblem {
    int kind; /* 0-2: which of the mission's three badges it is, one of each (their bits, 0x92b + 4n) */
    double at[3];
} KhdaysMissionEmblem;

/* An Organization emblem of an emblems mission (collect = x y z): 10 points picked up, 8 once it has shrunk. */
typedef struct KhdaysMissionCollect {
    double at[3];
} KhdaysMissionCollect;

typedef struct KhdaysMission {
    char *identity;  /* "<mod id>/<key>" */
    const char *key; /* inside identity */
    int mod;         /* in the registry's mods */
    int line;        /* where its block starts in missions.txt */
    /* UTF-8; NULL: the English one */
    char *name[KHDAYS_MISSION_LANGUAGES], *objective[KHDAYS_MISSION_LANGUAGES];
    char *description[KHDAYS_MISSION_LANGUAGES]; /* NULL: the objective */
    int category;                                /* the list's (0-11) */
    char world[16];
    int room;
    int level;          /* the enemies' level base, 0: the template's */
    int time_ms;        /* 0: none given */
    int unlock_number;  /* a game mission's number to clear first, 0 none */
    char *unlock_after; /* or a new mission's identity, NULL none */
    int template_file;  /* the game mission it was made from, 0 none */
    int has_start;      /* start = x y z facing */
    double start[3];
    int facing;
    int kind;       /* KhdaysMissionKind */
    int has_target; /* eliminate: target = <enemy class> (a waves mission keeps it unused) */
    int target;
    int goal; /* eliminate: how many of the target class; hearts: how many hearts; emblems: points; 0 all */
    KhdaysMissionSpawner *spawners; /* wave <n> = ... */
    int spawner_count;
    KhdaysMissionChest *chests;
    int chest_count;
    KhdaysMissionEmblem *emblems;
    int emblem_count;
    KhdaysMissionCollect *collects; /* emblems: collect = ... */
    int collect_count;
    int has_reach; /* reach: reach = x y z radius */
    double reach[3], reach_radius;
    /* this run's (khdays_missions_number); 0 not numbered */
    int number, file, shown; /* shown: the number the list and the results show (0-99) */
} KhdaysMission;

typedef struct KhdaysMissionMod {
    char *id, *folder;
} KhdaysMissionMod;

typedef struct KhdaysMissions {
    KhdaysMission *list;
    int count, capacity;
    KhdaysMissionMod *mods;
    int mod_count, mod_capacity;
} KhdaysMissions;

/* Where a mission that is left out is told of: its mod, the line (0 none) and why. */
typedef void (*KhdaysMissionsReport)(const char *mod, int line, const char *why, void *context);

/* The game's two letters of language `l` (0-4): "en" ... */
const char *khdays_mission_language(int l);

/* A kind's name in missions.txt (`kind = ...`), NULL past the last. */
const char *khdays_mission_kind_name(int kind);

/* Whether the port builds mission `m` from its description: it has waves, or its kind is played without them (an
 * emblems mission with its emblems, a reach mission with its place). */
int khdays_mission_built(const KhdaysMission *m);

/* How many of mission `m`'s enemies count toward an eliminate mission's goal: its waves' of the target class. */
int khdays_mission_counted(const KhdaysMission *m);

/* How many items the game has (db/db.p2 entry 0x15's count), set before missions are read: a chest's contents is an
 * item from 1 to it. 0 leaves it unknown (any item from 1). */
void khdays_missions_set_items(int count);

/* Adds mod `id`'s missions (its folder under mods/) from its missions.txt, `size` bytes. A block that does not
 * read is left out and reported; the others are kept. Returns how many were added. */
int khdays_missions_read(KhdaysMissions *r, const char *id, const char *folder, const char *text, size_t size,
                         KhdaysMissionsReport report, void *context);

/* Gives every mission its numbers, in the order they were read, and places their fields in `store`; one that
 * finds no number left (the formats' own limits) is reported and left unnumbered. Returns how many have
 * numbers. */
int khdays_missions_number(KhdaysMissions *r, KhdaysFieldStore *store, KhdaysMissionsReport report, void *context);

/* The numbered mission with that number, file number or identity; NULL none. */
const KhdaysMission *khdays_missions_by_number(const KhdaysMissions *r, int number);
const KhdaysMission *khdays_missions_by_file(const KhdaysMissions *r, int file);
int khdays_missions_index(const KhdaysMissions *r, const char *identity);

void khdays_missions_free(KhdaysMissions *r);

#ifdef __cplusplus
}
#endif

#endif
