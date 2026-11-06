/* The mods' new missions in the running game (missions.h). */
#include "missions.h"
#include "../rooms/added.h"

#include "build.h"
#include "field_store.h"
#include "registry.h"
#include "room.h"
#include "save_part.h"
#include "tables.h"
#include "worlds.h"

#include "../../host/app/instance.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../core/sha256.h"
#include "../core/window.h"
#include "../mods/list.h"
#include "../net/net_session.h"
#include "../menus/savelist.h"
#include "../menus/saveslots.h"
#include "../scripts/folders.h"
#include "../scripts/manifest.h"
#include "../scripts/mods_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern KhdaysRef ExpHeap_AllocOrDefault(unsigned int size, int align, KhdaysRef heap);
extern void NNS_FndFreeToExpHeap(KhdaysRef heap, KhdaysRef block);
extern void NNSi_FndFreeFromDefaultHeap(KhdaysRef block);
extern int GameState_GetField(int field, int bits);

#define MODS "mods"

/* Mission Mode's choice (data_0204c240: its mode flags, then the mission's file number, the one
 * Ov002_ConstructGameplayScene formats mi/mi/%04d with); the mission's number (gMissionNumber) just before it. */
#define MISSION_CHOICE 0x0204c240u
#define MISSION_CHOICE_FILE 2u

static KhdaysMissions registry;
static KhdaysFieldStore store;
static KhdaysSavePart kept;
static int numbered;

/* What the start found, told once the logs are open. */
typedef struct Note {
    char *mod, *text;
    int line;
} Note;
static Note *notes;
static int note_count, note_capacity;

static char *copy(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (c != NULL) {
        memcpy(c, s, n);
    }
    return c;
}

static void note(const char *mod, int line, const char *why, void *context)
{
    (void)context;
    if (note_count == note_capacity) {
        const int grown = note_capacity == 0 ? 8 : note_capacity * 2;
        Note *n = realloc(notes, (size_t)grown * sizeof(*n));
        if (n == NULL) {
            return;
        }
        notes = n;
        note_capacity = grown;
    }
    notes[note_count].mod = copy(mod);
    notes[note_count].text = copy(why);
    notes[note_count].line = line;
    if (notes[note_count].mod != NULL && notes[note_count].text != NULL) {
        ++note_count;
    }
}

static char *joined(const char *a, const char *b)
{
    const size_t n = strlen(a) + 1 + strlen(b) + 1;
    char *s = malloc(n);
    if (s != NULL) {
        snprintf(s, n, "%s/%s", a, b);
    }
    return s;
}

/* A mod's id: its mod.ini's, else its folder's name. */
static char *mod_id(const char *folder, const char *name)
{
    char *ini = joined(folder, "mod.ini"), *text = ini != NULL ? khdays_folders_read(ini, NULL) : NULL, *id = NULL;
    free(ini);
    if (text != NULL) {
        /* its `id` whatever else it says: a mod with no Lua scripts (no `api`) is the same mod to every part */
        id = khdays_manifest_value(text, strlen(text), "id");
        free(text);
    }
    if (id != NULL && id[0] != '\0') {
        return id;
    }
    free(id);
    return copy(name);
}

static void read_mod(const char *name, void *context)
{
    (void)context;
    if (!khdays_mods_started_has(name)) {
        return; /* off on the Mods page as the game started (runtime/mods/list.h) */
    }
    char *folder = joined(MODS, name), *path = folder != NULL ? joined(folder, "missions/missions.txt") : NULL;
    size_t size = 0;
    char *text = path != NULL ? khdays_folders_read(path, &size) : NULL;
    if (text != NULL) {
        char *id = mod_id(folder, name);
        if (id != NULL) {
            khdays_missions_read(&registry, id, name, text, size, note, NULL);
        }
        free(id);
        free(text);
    }
    free(path);
    free(folder);
}

/* How many items the game has (db/db.p2 entry 0x15: a count, then a record an item from item 1); 0 when it cannot
 * be read. */
static int game_items(void)
{
    KhdaysRomReader *reader = khdays_rom_reader_new();
    u8 *data = NULL;
    u32 size = 0;
    int count = 0;
    if (reader != NULL && khdays_rom_file_data(reader, "db/db.p2", 0x15, &data, &size) && data != NULL && size >= 4u) {
#pragma warning(suppress : 6001) /* khdays_rom_file_data filled `size` bytes of it */
        const u32 n = (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
        count = n <= 0x7fffu ? (int)n : 0x7fff;
    }
    free(data);
    return count;
}

/* Mission `m`'s digest (KhdaysMission.digest), what a room's copies compare: its block's lines and, played from a
 * file of its own, that file's bytes. */
static void mission_digest(KhdaysMission *m, const char *own_file)
{
    KhdaysSha256 sha;
    khdays_sha256_begin(&sha);
    if (m->block != NULL) {
        khdays_sha256_add(&sha, m->block, m->block_size);
    }
    if (own_file != NULL) {
        size_t size = 0;
        char *bytes = khdays_folders_read(own_file, &size);
        if (bytes != NULL) {
            khdays_sha256_add(&sha, bytes, size);
        }
        free(bytes);
    }
    khdays_sha256_end(&sha, m->digest);
}

void khdays_missions_start(void)
{
    /* a chest's contents must be one of the game's items: the game reads its record as the mission starts */
    khdays_missions_set_items(game_items());
    khdays_folders_list(MODS, read_mod, NULL);
    if (registry.count == 0) {
        return;
    }
    numbered = khdays_missions_number(&registry, &store, note, NULL);
    for (int i = 0; i < registry.count; ++i) {
        KhdaysMission *m = &registry.list[i];
        char disk[1024], path[32];
        int own_file = 0;
        if (m->number == 0) {
            continue;
        }
        /* its chests' opened flags, past every mission's fields */
        unsigned first_chest;
        if (m->chest_count > 0 && !khdays_field_store_place_chests(&store, i, m->chest_count, &first_chest)) {
            note(registry.mods[m->mod].id, m->line, "its chests left out: no room for their flags", NULL);
        }
        /* a world the mod adds: played in its game world, listed in its own slot */
        if (khdays_world_index(m->world) < 0) {
            char mod_path[1024], like[3];
            snprintf(mod_path, sizeof(mod_path), "%s/%s", MODS, registry.mods[m->mod].folder);
            const int slot = khdays_mod_world(mod_path, m->world, like);
            if (slot < 0) {
                char why[160];
                snprintf(why, sizeof(why), "mission %s left out: its world %s is no game world nor one the mod adds",
                         m->key, m->world);
                note(registry.mods[m->mod].id, m->line, why, NULL);
                m->number = 0;
                --numbered;
                continue;
            }
            snprintf(m->world, sizeof(m->world), "%s", like);
            m->category = slot;
        }
        /* a room the mod adds: its index, given when its world's archive was read */
        if (m->room_key != NULL) {
            char mod_path[1024];
            snprintf(mod_path, sizeof(mod_path), "%s/%s", MODS, registry.mods[m->mod].folder);
            const int index = khdays_rooms_index(mod_path, m->room_key, m->world);
            if (index >= 0) {
                m->room = index;
            } else {
                char why[160];
                snprintf(why, sizeof(why), "its room %s is no room the mod adds to %s: played in the world's room 0",
                         m->room_key, m->world);
                note(registry.mods[m->mod].id, m->line, why, NULL);
            }
        }
        snprintf(disk, sizeof(disk), "%s/%s/missions/%s/mission.p2", MODS, registry.mods[m->mod].folder, m->key);
        snprintf(path, sizeof(path), "mi/mi/%04d", m->file);
        /* its waves built into its file; else its own file; else its template's as it is */
        char template_path[32], why[300] = "";
        snprintf(template_path, sizeof(template_path), "mi/mi/%04d", m->template_file);
        int served = 0;
        if (khdays_mission_built(m)) {
            unsigned char *built = NULL;
            size_t size = 0;
            char reason[200];
            served = khdays_missions_build(m, &built, &size, reason, sizeof(reason)) &&
                     khdays_rom_add_bytes(path, built, (unsigned)size);
            free(built);
            if (!served) {
                snprintf(why, sizeof(why), "mission %s left out: its waves do not build: %s", m->key, reason);
            }
        } else if (khdays_folders_is_file(disk)) {
            served = own_file = khdays_rom_add_file(path, disk);
        } else if (m->template_file != 0) {
            served = khdays_rom_add_alias(path, template_path);
        }
        if (!served) {
            if (why[0] == '\0') {
                snprintf(why, sizeof(why),
                         "mission %s left out: missions/%s/mission.p2 cannot be read (nor a template's)", m->key,
                         m->key);
            }
            note(registry.mods[m->mod].id, m->line, why, NULL);
            m->number = 0;
            --numbered;
        } else {
            mission_digest(m, own_file ? disk : NULL);
        }
    }
    /* what this copy brings to a multiplayer room (room.h) */
    size_t room_size = 0;
    unsigned char *room_list = khdays_missions_room_list(&registry, &room_size);
    khdays_net_set_missions(room_list, (unsigned)room_size);
    free(room_list);
}

void khdays_missions_report(void)
{
    if (registry.count == 0 && note_count == 0) {
        return;
    }
    khdays_mods_log_open(khdays_host_user_folder());
    for (int i = 0; i < note_count; ++i) {
        khdays_mods_log(notes[i].mod, "missions", "missions/missions.txt", notes[i].line, notes[i].text);
        free(notes[i].mod);
        free(notes[i].text);
    }
    free(notes);
    notes = NULL;
    note_count = note_capacity = 0;
    for (int i = 0; i < registry.count; ++i) {
        const KhdaysMission *m = &registry.list[i];
        if (m->number != 0) {
            khdays_log("missions: %s is mission %d (shown %02d), file mi/mi/%04d, category %d, %s room %d", m->identity,
                       m->number, m->shown, m->file, m->category, m->world, m->room);
        }
    }
    khdays_log("missions: %d of the mods' missions in Mission Mode's list", numbered);
}

/* ---- The game's progress fields ---------------------------------------------------------------- */

/* A field past the flag array the game reached that is no new mission's: told once each (the game's own
 * never reach there). */
static void reached(unsigned field, unsigned bits)
{
    static unsigned *told;
    static int told_count, told_capacity;
    for (int i = 0; i < told_count; ++i) {
        if (told[i] == field) {
            return;
        }
    }
    if (told_count == told_capacity) {
        const int grown = told_capacity == 0 ? 16 : told_capacity * 2;
        unsigned *t = realloc(told, (size_t)grown * sizeof(*t));
        if (t == NULL) {
            return;
        }
        told = t;
        told_capacity = grown;
    }
    told[told_count++] = field;
    khdays_log("missions: the game reached field 0x%x (%u bits) past its flags, no new mission's", field, bits);
}

long long khdays_missions_field_get(unsigned int field, unsigned int bits)
{
    unsigned value;
    if (!khdays_field_store_get(&store, field, bits, &value)) {
        reached(field, bits);
        return -1;
    }
    return value;
}

int khdays_missions_field_set(unsigned int field, unsigned int bits, unsigned int value)
{
    if (!khdays_field_store_set(&store, field, bits, value)) {
        reached(field, bits);
        return 0;
    }
    return 1;
}

int khdays_missions_flag_get(unsigned int flag)
{
    return (int)khdays_missions_field_get(flag, 1);
}

int khdays_missions_flag_set(unsigned int flag, int on)
{
    return khdays_missions_field_set(flag, 1, on != 0);
}

static KhdaysMissionProgress *progress_of(int number)
{
    const KhdaysMission *m = khdays_missions_by_number(&registry, number);
    const int i = m != NULL ? (int)(m - registry.list) : -1;
    return i >= 0 && i < store.count ? &store.progress[i] : NULL;
}

int khdays_missions_best(int number)
{
    const KhdaysMissionProgress *p = progress_of(number);
    return p != NULL ? p->best : -1;
}

void khdays_missions_set_best(int number, int value)
{
    KhdaysMissionProgress *p = progress_of(number);
    if (p != NULL) {
        p->best = value;
    }
}

/* ---- Mission Mode's list ------------------------------------------------------------------------- */

#define STATUS_CLEARED 2 /* a mission's status field: 1 seen, 2 cleared, 3 complete */

/* The flags Mission Mode's list asks its row test with: a bit for each kind of row it takes (1 << (kind - 1)), its
 * category above them. */
#define LIST_KINDS 0xfu
#define LIST_CATEGORY(flags) ((flags) >> 8 & 0xffu)

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

/* Whether mission number `number` has been cleared: in the story (its status) or in Mission Mode (a rank in its
 * transfer field, the only one Mission Mode's results write). */
static int cleared(int number)
{
    unsigned status_bits, rank_bits;
    const unsigned status = khdays_mission_field(number, KHDAYS_RECORD_STATUS, &status_bits);
    const unsigned rank = khdays_mission_field(number, KHDAYS_RECORD_TRANSFER, &rank_bits);
    return GameState_GetField((int)status, (int)status_bits) >= STATUS_CLEARED ||
           GameState_GetField((int)rank, (int)rank_bits) != 0;
}

/* Whether new mission `m` is open: from the start, or once the mission it names (a game's, or another new one) is
 * cleared. */
static int unlocked(const KhdaysMission *m)
{
    if (m->unlock_after != NULL) {
        const int i = khdays_missions_index(&registry, m->unlock_after);
        return i >= 0 && registry.list[i].number != 0 && cleared(registry.list[i].number);
    }
    return m->unlock_number == 0 || cleared(m->unlock_number);
}

/* ---- Multiplayer rooms (room.h) ------------------------------------------------------------------ */

typedef struct RoomCheck {
    const KhdaysMission *m;
    int all; /* 1 while every member has it */
} RoomCheck;

static void member_has(const unsigned char *list, unsigned int size, int known, void *context)
{
    RoomCheck *c = context;
    if (!known || !khdays_missions_room_holds(list, size, c->m)) {
        c->all = 0;
    }
}

/* Whether mission `m` is offered here: always out of a room and on a guest (only the host chooses); on a host, when
 * every member has it, the same. Each one left out is told once for each change of the room. */
static int room_offers(const KhdaysMission *m)
{
    static unsigned int told_version;
    static unsigned char *told; /* by the registry's index */
    RoomCheck c = {m, 1};
    if (khdays_net_room_missions(member_has, &c) != 1 || c.all) {
        return 1;
    }
    const unsigned int version = khdays_net_missions_version();
    if (told == NULL || version != told_version) {
        free(told);
        told = calloc((size_t)registry.count, 1);
        told_version = version;
    }
    const ptrdiff_t i = m - registry.list;
    if (told != NULL && !told[i]) {
        told[i] = 1;
        khdays_log("missions: %s left out of the room's list: not every member has it, the same", m->identity);
    }
    return 0;
}

/* The host's list (a guest's room has one other copy, the host), copied out of the session's lock. */
typedef struct HostList {
    unsigned char *list;
    unsigned int size;
    int known;
} HostList;

static void host_list(const unsigned char *list, unsigned int size, int known, void *context)
{
    HostList *h = context;
    h->known = known;
    if (known && list != NULL && size > 0 && (h->list = malloc(size)) != NULL) {
        memcpy(h->list, list, size);
        h->size = size;
    }
}

void khdays_missions_from_host(void)
{
    volatile u16 *number = (volatile u16 *)KHDAYS_W(MISSION_CHOICE - 4u);
    volatile u16 *file = (volatile u16 *)KHDAYS_W(MISSION_CHOICE + MISSION_CHOICE_FILE);
    const int host_number = *number, host_file = *file;
    HostList h = {NULL, 0, 0};
    if (host_number < KHDAYS_MISSION_FIRST || khdays_net_room_missions(host_list, &h) != 2) {
        free(h.list);
        return; /* the game's own mission, or not a guest */
    }
    int named = 0;
    const KhdaysMission *m = khdays_missions_room_local(&registry, h.list, h.size, host_number, &named);
    if (m != NULL) {
        *number = (u16)m->number;
        *file = (u16)m->file;
        khdays_log("missions: the host's mission %d (file %d) is %s: mission %d here (file %d)", host_number, host_file,
                   m->identity, m->number, m->file);
    } else if (!h.known) {
        khdays_log("missions: the host's mission %d: the host never said which it is (an older port)", host_number);
    } else {
        khdays_log("missions: the host's mission %d is %s this copy does not have, the same", host_number,
                   named ? "one" : "none");
    }
    free(h.list);
}

int khdays_missions_eligible(const void *record, unsigned int flags)
{
    const unsigned char *r = record;
    const KhdaysMission *m =
        numbered != 0 ? khdays_missions_by_number(&registry, (int)rd16(r + KHDAYS_LIST_NUMBER)) : NULL;
    if (m == NULL || (int)rd16(r + KHDAYS_LIST_FILE) != m->file) {
        return -1;
    }
    /* its own Mission Mode row, in its category, when the list takes that kind */
    const unsigned asked = flags & LIST_KINDS & 1u << (KHDAYS_ENTRY_MISSION - 1);
    if (r[KHDAYS_LIST_KIND] != KHDAYS_ENTRY_MISSION || asked == 0 ||
        rd16(r + KHDAYS_LIST_CATEGORY) != LIST_CATEGORY(flags)) {
        return 0;
    }
    return unlocked(m) && room_offers(m);
}

/* ---- The game's tables --------------------------------------------------------------------------- */

/* The list file's language by its name (UI/cm/msl_<xx>.msi.z), -1 when it is not one. */
static int list_language(const char *path)
{
    static const char head[] = "UI/cm/msl_";
    if (_strnicmp(path, head, sizeof(head) - 1) != 0 || _stricmp(path + sizeof(head) + 1, ".msi.z") != 0) {
        return -1;
    }
    for (int l = 0; l < KHDAYS_MISSION_LANGUAGES; ++l) {
        if (_strnicmp(path + sizeof(head) - 1, khdays_mission_language(l), 2) == 0) {
            return l;
        }
    }
    return -1;
}

void *khdays_missions_file_loaded(unsigned top, void *buffer, unsigned size, void *heap)
{
    char path[112];
    unsigned within;
    size_t made_size = 0;
    unsigned char *made = NULL;
    int language;
    if (numbered == 0 || !khdays_rom_file_at(top, path, (int)sizeof(path), &within) || within != 0) {
        return buffer;
    }
    if ((language = list_language(path)) >= 0) {
        made = khdays_tables_list(buffer, size, &registry, language, &made_size);
    } else if (_stricmp(path, "mi/mi/mdb.z") == 0) {
        made = khdays_tables_mdb(buffer, size, &registry, &made_size);
    } else if (_stricmp(path, "mi/mi/trbox") == 0 || _stricmp(path, "mi/mi/evi") == 0) {
        made = khdays_tables_placed(buffer, size, &registry, &store, _stricmp(path, "mi/mi/trbox") == 0, &made_size);
    }
    if (made == NULL) {
        return buffer;
    }
    /* in the heap the game loaded the file into, the game's freed: its own release frees this one */
    const KhdaysRef heap_ref = heap != NULL ? KHDAYS_REF(heap) : 0;
    const KhdaysRef block = ExpHeap_AllocOrDefault((unsigned)made_size, 0x20, heap_ref);
    if (!khdays_game_ram(block)) {
        khdays_log("missions: no room in the game's heap for %s with the new rows (%zu bytes)", path, made_size);
        free(made);
        return buffer;
    }
    memcpy(KHDAYS_W(block), made, made_size);
    free(made);
    if (heap_ref == 0) {
        NNSi_FndFreeFromDefaultHeap(KHDAYS_REF(buffer));
    } else {
        NNS_FndFreeToExpHeap(*(const KhdaysRef *)KHDAYS_W(heap_ref), KHDAYS_REF(buffer));
    }
    khdays_log("missions: %s with the new missions' rows (%u bytes, was %u)", path, (unsigned)made_size, size);
    return KHDAYS_W(block);
}

/* ---- The mission being played ------------------------------------------------------------------- */

/* The new mission being played, or NULL (a game's). */
static const KhdaysMission *playing(void)
{
    const int file = *(const volatile u16 *)KHDAYS_W(MISSION_CHOICE + MISSION_CHOICE_FILE);
    for (int i = 0; i < registry.count; ++i) {
        if (registry.list[i].number != 0 && registry.list[i].file == file) {
            return &registry.list[i];
        }
    }
    return NULL;
}

/* A node's state word (Ov107_InitNodeBase): bit 0 shown, bit 1 running (Ov107_Region_TickChildren ticks only
 * those), set on every member when a region is (Ov107_Region_SetEnabled, when a player joins it); its region
 * (the source it was made from) at +4. */
#define NODE_REGION 4u
#define NODE_STATE 0x40u
#define NODE_SHOWN_AND_RUNNING 3u

void khdays_missions_spawner_made(void *node)
{
    const KhdaysMission *m = playing();
    const KhdaysRef region = *(const KhdaysRef *)((const unsigned char *)node + NODE_REGION);
    if (m == NULL || m->spawner_count == 0 || region == 0) {
        return;
    }
    u32 *state = (u32 *)((unsigned char *)node + NODE_STATE);
    const u32 before = *state;
    *state |= *(const volatile u32 *)KHDAYS_W(region + NODE_STATE) & NODE_SHOWN_AND_RUNNING;
    if (*state != before) {
        khdays_log("missions: %s: a spawner made after its region started, running with it", m->identity);
    }
}

int khdays_missions_spots_shown(void)
{
    static const KhdaysMission *told;
    const KhdaysMission *m = playing();
    if (m != NULL && m != told) {
        const int number = *(const volatile u16 *)KHDAYS_W(MISSION_CHOICE - 4u);
        khdays_log("missions: %s: its chests and emblems as the story's (mission number %d)", m->identity, number);
    }
    told = m;
    return m != NULL;
}

int khdays_missions_playing(void)
{
    return playing() != NULL;
}

/* ---- The save part ------------------------------------------------------------------------------- */

static void part_path(int file, char *path, size_t size)
{
    snprintf(path, size, "%s/missions/file%02d.txt", khdays_host_user_folder(), file + 1);
}

void khdays_missions_loaded(int file)
{
    char path[320];
    size_t size = 0;
    char *text = NULL;
    if (file >= 0) {
        part_path(file, path, sizeof(path));
        text = khdays_folders_read(path, &size);
    }
    if (registry.count == 0 && text == NULL && kept.kept_count == 0) {
        return;
    }
    khdays_save_part_read(text != NULL ? text : "", text != NULL ? size : 0, &registry, &store, &kept);
    if (text != NULL) {
        khdays_log("missions: file %d's progress read (%s)", file + 1, path);
    }
    free(text);
}

void khdays_missions_saved(int file)
{
    char path[320];
    if (file < 0 || (numbered == 0 && kept.kept_count == 0)) {
        return;
    }
    part_path(file, path, sizeof(path));
    if (khdays_slots_state(file, NULL) != KHDAYS_SLOT_SAVED) {
        if (khdays_folders_is_file(path)) {
            khdays_folders_remove(path);
            khdays_log("missions: file %d deleted, %s with it", file + 1, path);
        }
        return;
    }
    size_t size = 0;
    char *text = khdays_save_part_text(&registry, &store, &kept, &size);
    if (text == NULL) {
        return;
    }
    if (khdays_folders_write(path, text, size)) {
        khdays_log("missions: file %d's progress written (%s)", file + 1, path);
    } else {
        khdays_log("missions: %s could not be written", path);
    }
    free(text);
}
