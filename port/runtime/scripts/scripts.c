/* Gameplay mods' scripts (scripts.h): finding the mods, their load order, loading each into its own
 * state, and the frame point. */
#include "scripts.h"

#include "api.h"
#include "api_v2.h"
#include "events.h"
#include "folders.h"
#include "enemies_link.h"
#include "game_link.h"
#include "mod.h"
#include "mods_log.h"
#include "screens.h"
#include "time_link.h"
#include "queue.h"
#include "../mods/list.h"
#include "sandbox.h"
#include "view.h"
#include "../characters/characters.h"
#include "../plugins/plugins.h"
#include "../core/sha256.h"
#include "../menus/autosave.h"
#include "../net/net_session.h"

#include <lauxlib.h>
#include <lua.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static KhdaysMod **mods; /* each its own block: its state keeps its address (lua_getextraspace) */
static int count;
static int changed; /* the mods' view is to be published again (view.h) */
static void publish(void);
static void tell_rooms(void);
static KhdaysMod *by_folder(const char *folder);
static void reload(KhdaysMod *m);
static int started;
static int tracing = -1;
static int field_begun; /* the field was made: its first run is the mission's start */
/* The run lost: the game over's sub-flow (ov027) also takes a give-up from the pause, so a run is lost
 * when it comes up soon after the local player fell to 0 HP (in Mission Mode after the last of the
 * falls the field revives). */
#define FALL_TO_GAME_OVER 600 /* game frames */
static unsigned fell_at;      /* the game frame the local player last fell, + 1; 0 for none this run */
static int game_over_up, lost;
static unsigned looked; /* frame points since the mods' files were last looked at */
static char *user_folder;

int khdays_scripts_mod_count(void)
{
    return count;
}

KhdaysMod *khdays_scripts_mod(int index)
{
    return mods[index];
}

int khdays_scripts_any(void)
{
    return count > 0;
}

static char *copy(const char *s)
{
    char *out = malloc(strlen(s) + 1);
    if (out != NULL) {
        strcpy(out, s);
    }
    return out;
}

static char *joined(const char *a, const char *b)
{
    const size_t n = strlen(a) + strlen(b) + 2;
    char *out = malloc(n);
    if (out != NULL) {
        snprintf(out, n, "%s/%s", a, b);
    }
    return out;
}

void khdays_mod_set_state(KhdaysMod *m, int state, const char *reason)
{
    static const int told[] = {[KHDAYS_MOD_STOPPED] = KHDAYS_NOTICE_STOPPED + 1,
                               [KHDAYS_MOD_PAUSED] = KHDAYS_NOTICE_PAUSED + 1,
                               [KHDAYS_MOD_REFUSED] = KHDAYS_NOTICE_REFUSED + 1};
    changed = 1;
    m->state = state;
    free(m->reason);
    m->reason = reason != NULL ? copy(reason) : NULL;
    if (state >= 0 && state < (int)(sizeof(told) / sizeof(told[0])) && told[state] != 0) {
        khdays_scripts_notice(told[state] - 1, m->manifest.name != NULL ? m->manifest.name : m->folder, reason);
    }
}

static const char *name_of(const KhdaysMod *m)
{
    return m->manifest.id != NULL ? m->manifest.id : m->folder;
}

static void refused(KhdaysMod *m, const char *reason)
{
    khdays_mod_set_state(m, KHDAYS_MOD_REFUSED, reason);
    khdays_mods_log(name_of(m), "refused", NULL, 0, reason);
}

/* The newest write among the mod's mod.ini and scripts (its reload is wanted when it moves). */
typedef struct Newest {
    const char *scripts;
    long long time;
} Newest;

static void newer(const char *relative, void *context)
{
    Newest *n = context;
    char *path = joined(n->scripts, relative);
    if (path != NULL) {
        const long long t = khdays_folders_time(path);
        if (t > n->time) {
            n->time = t;
        }
        free(path);
    }
}

static long long newest_file(const KhdaysMod *m)
{
    char *ini = joined(m->path, "mod.ini");
    Newest n;
    n.scripts = joined(m->path, "scripts");
    n.time = ini != NULL ? khdays_folders_time(ini) : -1;
    if (n.scripts != NULL) {
        khdays_folders_files(n.scripts, newer, &n);
    }
    free(ini);
    free((char *)n.scripts);
    return n.time;
}

static void unload(KhdaysMod *m)
{
    khdays_api_v2_release(m);
    khdays_api_menus_free(m);
    khdays_sandbox_close(m);
    free(m->handlers);
    m->handlers = NULL;
    m->handler_count = m->handler_capacity = 0;
}

static void load(KhdaysMod *m)
{
    char note[64];
    int ran;
    if (!khdays_sandbox_open(m)) {
        khdays_mods_log(name_of(m), "refused", NULL, 0, m->reason);
        return;
    }
    khdays_api_mod_open(m);
    khdays_api_state_open(m);
    khdays_api_menus_open(m);
    khdays_api_actions_open(m);
    if (m->manifest.api >= 2) {
        khdays_api_v2_open(m);
    }
    m->stamp = newest_file(m);
    m->spent = 0;
    m->allowance = khdays_sandbox_frame_allowance;
    khdays_script_events_hold();
    ran = khdays_sandbox_run_main(m);
    if (m->v2 != NULL) {
        m->v2->loading = 0; /* its title entries made */
    }
    if (ran) {
        snprintf(note, sizeof(note), "started, api %d", m->manifest.api);
        khdays_mods_log(m->manifest.id, "loaded", NULL, 0, note);
    }
    khdays_script_events_release();
}

/* Reads a mod's folder: its manifest, its interface version, its id among the others, its
 * fingerprint. Returns 1 when it may be loaded. */
static int examine(KhdaysMod *m)
{
    char reason[300], *ini = joined(m->path, "mod.ini"), *text;
    size_t size = 0;
    text = ini != NULL ? khdays_folders_read(ini, &size) : NULL;
    free(ini);
    if (text == NULL) {
        refused(m, "mod.ini cannot be read");
        return 0;
    }
    if (!khdays_manifest_parse(text, size, &m->manifest, reason, sizeof(reason))) {
        free(text);
        refused(m, reason);
        return 0;
    }
    free(text);
    if (m->manifest.api < 1) {
        refused(m, "mod.ini: api missing or not valid");
        return 0;
    }
    if (m->manifest.api > KHDAYS_MODS_API) {
        snprintf(reason, sizeof(reason), "needs mod interface %d, this port has %d", m->manifest.api, KHDAYS_MODS_API);
        refused(m, reason);
        return 0;
    }
    for (int i = 0; i < count && mods[i] != m; ++i) {
        const KhdaysMod *other = mods[i];
        if (other->manifest.id != NULL && strcmp(other->manifest.id, m->manifest.id) == 0) {
            snprintf(reason, sizeof(reason), "same id as %s", other->folder);
            refused(m, reason);
            return 0;
        }
    }
    if (!khdays_manifest_fingerprint(m->path, m->fingerprint)) {
        refused(m, "its files cannot all be read");
        return 0;
    }
    {
        char *data_root = joined(user_folder, "mods");
        m->data = data_root != NULL ? joined(data_root, m->manifest.id) : NULL;
        free(data_root);
    }
    return 1;
}

void khdays_scripts_start_in(const char *mods_path, const char *user)
{
    int scripted = 0;
    khdays_scripts_stop();
    started = 1;
    khdays_mods_scan(mods_path, user);
    user_folder = copy(user);
    for (int i = 0; i < khdays_mods_count(); ++i) {
        scripted += khdays_mods_at(i)->scripted;
    }
    if (scripted == 0) {
        publish();    /* content mods alone: the Mods page still shows them */
        tell_rooms(); /* no scripted mod, but the mods' characters and native plugins still make the room's mods */
        return;
    }
    mods = calloc((size_t)scripted, sizeof(*mods));
    if (mods == NULL) {
        return;
    }
    khdays_mods_log_open(user);
    for (int i = 0; i < khdays_mods_count(); ++i) {
        const KhdaysModEntry *e = khdays_mods_at(i);
        KhdaysMod *m;
        if (!e->scripted) {
            continue;
        }
        m = calloc(1, sizeof(*m));
        if (m == NULL) {
            break;
        }
        mods[count++] = m;
        m->folder = copy(e->folder);
        m->path = copy(e->path);
        m->enabled = khdays_mods_active(e);
        m->state = KHDAYS_MOD_OFF;
        if (m->folder == NULL || m->path == NULL || !examine(m)) {
            if (m->state != KHDAYS_MOD_REFUSED) {
                refused(m, "not enough memory");
            }
            continue;
        }
        if (m->enabled) {
            load(m);
        }
    }
    publish();
    tell_rooms();
    changed = 0;
}

void khdays_scripts_stop(void)
{
#pragma warning(push)
#pragma warning(disable : 6001) /* the mods come from calloc: each field is NULL or the mod's own */
    for (int i = 0; i < count; ++i) {
        KhdaysMod *m = mods[i];
        unload(m);
        khdays_manifest_free(&m->manifest);
        free(m->folder);
        free(m->path);
        free(m->data);
        free(m->reason);
        free(m);
    }
#pragma warning(pop)
    free(mods);
    mods = NULL;
    count = 0;
    free(user_folder);
    user_folder = NULL;
    khdays_script_events_reset();
    khdays_mods_log_close();
    started = 0;
}

static void publish(void)
{
    KhdaysModsView now;
    const int n = khdays_mods_count();
    memset(&now, 0, sizeof(now));
    now.mods = n > 0 ? calloc((size_t)n, sizeof(*now.mods)) : NULL;
    now.count = now.mods != NULL ? n : 0;
    for (int i = 0; i < now.count; ++i) {
        const KhdaysModEntry *e = khdays_mods_at(i);
        const KhdaysMod *m = e->scripted ? by_folder(e->folder) : NULL;
        KhdaysModView *v = &now.mods[i];
        v->folder = (char *)e->folder;
        v->enabled = e->on;
        v->ignored = e->ignored;
        v->content = m == NULL;
        if (m == NULL) {
            v->kind = -1;
            v->state = khdays_mods_active(e) ? KHDAYS_MOD_LOADED : KHDAYS_MOD_OFF;
            continue;
        }
        v->id = m->manifest.id;
        v->name = m->manifest.name;
        v->version = m->manifest.version;
        v->author = m->manifest.author;
        v->description = m->manifest.description;
        v->reason = m->reason;
        v->kind = m->manifest.id != NULL ? m->manifest.kind : -1;
        v->state = m->state;
        v->memory_write = m->manifest.memory_write;
        v->options = m->option_count > 0 ? calloc((size_t)m->option_count, sizeof(*v->options)) : NULL;
        v->option_count = v->options != NULL ? m->option_count : 0;
        for (int o = 0; o < v->option_count; ++o) {
            const KhdaysOption *a = &m->options[o];
            KhdaysOptionView *b = &v->options[o];
            b->key = a->key;
            b->kind = a->kind;
            memcpy(b->label, a->label, sizeof(b->label));
            memcpy(b->description, a->description, sizeof(b->description));
            b->values = a->values;
            b->value_count = a->value_count;
            b->low = a->low;
            b->high = a->high;
            b->step = a->step;
            b->value = a->value;
        }
    }
    khdays_scripts_view_set(&now); /* copies the strings: the mods' own may go */
    for (int i = 0; i < now.count; ++i) {
#pragma warning(suppress : 6001) /* now.mods comes from calloc: each options NULL or its own */
        free(now.mods[i].options);
    }
    free(now.mods);
}

/* The gameplay mods running, for multiplayer rooms (runtime/net/net_session.h): their fingerprint, the
 * first four bytes of SHA-256 over each one's id, a 0, its version, a 0 and its own fingerprint, in
 * load order, then each of the mods' characters' identity, a 0 and its part (characters.h), in their numbers'
 * order (0 with none; a digest whose first four bytes are 0 counts as 1), and their names and versions (a
 * character's identity). Visual mods are not counted: each player's own. */
static void tell_rooms(void)
{
    KhdaysSha256 sha;
    unsigned char digest[32];
    unsigned char *list = NULL;
    size_t size = 0;
    unsigned int counted = 0, fingerprint = 0;
    khdays_sha256_begin(&sha);
    for (int i = 0; i < count; ++i) {
        const KhdaysMod *m = mods[i];
        size_t name, version;
        unsigned char *more;
        if (m->state != KHDAYS_MOD_LOADED || m->manifest.kind != KHDAYS_MOD_GAMEPLAY) {
            continue;
        }
        khdays_sha256_add(&sha, m->manifest.id, strlen(m->manifest.id) + 1);
        khdays_sha256_add(&sha, m->manifest.version, strlen(m->manifest.version) + 1);
        khdays_sha256_add(&sha, m->fingerprint, sizeof(m->fingerprint));
        name = strlen(m->manifest.name) < 255 ? strlen(m->manifest.name) : 255;
        version = strlen(m->manifest.version) < 255 ? strlen(m->manifest.version) : 255;
        more = realloc(list, size + 2 + name + version);
        if (more == NULL) {
            continue;
        }
        list = more;
        list[size++] = (unsigned char)name;
        memcpy(list + size, m->manifest.name, name);
        size += name;
        list[size++] = (unsigned char)version;
        memcpy(list + size, m->manifest.version, version);
        size += version;
        ++counted;
    }
    /* the mods' characters (a mod of characters alone has no scripts): the select numbers them by their
     * identities, and their descriptions and data play */
    for (int c = 0;; ++c) {
        unsigned char part[32];
        const char *name, *identity;
        size_t name_size, identity_size;
        unsigned char *more;
        if (!khdays_characters_room_entry(c, part, &name, &identity)) {
            break;
        }
        khdays_sha256_add(&sha, identity, strlen(identity) + 1);
        khdays_sha256_add(&sha, part, sizeof(part));
        name_size = strlen(name) < 255 ? strlen(name) : 255;
        identity_size = strlen(identity) < 255 ? strlen(identity) : 255;
        more = realloc(list, size + 2 + name_size + identity_size);
        if (more == NULL) {
            continue;
        }
        list = more;
        list[size++] = (unsigned char)name_size;
        memcpy(list + size, name, name_size);
        size += name_size;
        list[size++] = (unsigned char)identity_size;
        memcpy(list + size, identity, identity_size);
        size += identity_size;
        ++counted;
    }
    /* the native plugins started (a copy with native code off has none): their libraries and parameters play */
    for (int p = 0;; ++p) {
        unsigned char part[32];
        const char *name, *version;
        size_t name_size, version_size;
        unsigned char *more;
        if (!khdays_plugins_room_entry(p, part, &name, &version)) {
            break;
        }
        khdays_sha256_add(&sha, "native", 7);
        khdays_sha256_add(&sha, part, sizeof(part));
        name_size = strlen(name) < 255 ? strlen(name) : 255;
        version_size = strlen(version) < 255 ? strlen(version) : 255;
        more = realloc(list, size + 2 + name_size + version_size);
        if (more == NULL) {
            continue;
        }
        list = more;
        list[size++] = (unsigned char)name_size;
        memcpy(list + size, name, name_size);
        size += name_size;
        list[size++] = (unsigned char)version_size;
        memcpy(list + size, version, version_size);
        size += version_size;
        ++counted;
    }
    if (counted > 0) {
        khdays_sha256_end(&sha, digest);
        fingerprint = (unsigned int)digest[0] | (unsigned int)digest[1] << 8 | (unsigned int)digest[2] << 16 |
                      (unsigned int)digest[3] << 24;
        fingerprint = fingerprint != 0 ? fingerprint : 1;
    }
    khdays_net_set_mods(fingerprint, list, (unsigned int)size, counted);
    free(list);
}

static KhdaysMod *by_folder(const char *folder)
{
    for (int i = 0; folder != NULL && i < count; ++i) {
        if (strcmp(mods[i]->folder, folder) == 0) {
            return mods[i];
        }
    }
    return NULL;
}

static KhdaysMod *by_id(const char *id)
{
    for (int i = 0; id != NULL && i < count; ++i) {
        if (mods[i]->manifest.id != NULL && strcmp(mods[i]->manifest.id, id) == 0) {
            return mods[i];
        }
    }
    return NULL;
}

/* A mod turned on or off from the Mods page: loaded or closed (its options' values and data kept). The
 * list keeps order.ini. */
static void set_enabled(KhdaysMod *m, int on)
{
    khdays_mods_set_on(m->folder, on);
    m->enabled = on;
    changed = 1;
    if (on && (m->state == KHDAYS_MOD_OFF)) {
        load(m);
    } else if (!on && m->state != KHDAYS_MOD_REFUSED) {
        unload(m);
        khdays_mod_set_state(m, KHDAYS_MOD_OFF, NULL);
        khdays_mods_log(m->manifest.id, "stopped", NULL, 0, "turned off");
    }
}

/* The scripted mods in the list's order again (one moved among the content mods too). */
static void follow_order(void)
{
    int at = 0;
    for (int i = 0; i < khdays_mods_count() && at < count; ++i) {
        KhdaysMod *m = by_folder(khdays_mods_at(i)->folder);
        if (m != NULL) {
            int j = at;
            while (mods[j] != m) {
                ++j;
            }
            mods[j] = mods[at];
            mods[at++] = m;
        }
    }
}

/* A mod one place earlier or later in the list: the order file and the order events reach the mods in. */
static void move(const char *folder, int by)
{
    khdays_mods_move(folder, by);
    follow_order();
    changed = 1;
}

static void take_messages(void)
{
    KhdaysScriptsMessage msg;
    while (khdays_scripts_take(&msg)) {
        KhdaysEvent e;
        KhdaysMod *m;
        memset(&e, 0, sizeof(e));
        e.text = "settings";
        switch (msg.ask) {
        case KHDAYS_ASK_SETTINGS_OPENED:
            e.id = KHDAYS_EVENT_MENU_OPEN;
            khdays_script_events_raise(&e);
            break;
        case KHDAYS_ASK_SETTINGS_CLOSED:
            e.id = KHDAYS_EVENT_MENU_CLOSE;
            khdays_script_events_raise(&e);
            break;
        case KHDAYS_ASK_ENABLE:
            if ((m = by_folder(msg.mod)) != NULL) {
                if (m->enabled != (msg.value != 0)) {
                    set_enabled(m, msg.value != 0);
                }
            } else if (khdays_mods_by_folder(msg.mod) != NULL) { /* a content mod: its files only */
                khdays_mods_set_on(msg.mod, msg.value != 0);
                changed = 1;
            }
            break;
        case KHDAYS_ASK_MOVE:
            if (khdays_mods_by_folder(msg.mod) != NULL) {
                move(msg.mod, msg.value < 0 ? -1 : 1);
            }
            break;
        case KHDAYS_ASK_RELOAD:
            for (int i = 0; i < count; ++i) {
                if (msg.mod != NULL && (strcmp(msg.mod, "*") == 0 ||
                                        (mods[i]->manifest.id != NULL && strcmp(mods[i]->manifest.id, msg.mod) == 0))) {
                    reload(mods[i]);
                }
            }
            break;
        case KHDAYS_ASK_RESUME:
            if ((m = by_id(msg.mod)) != NULL && m->state == KHDAYS_MOD_PAUSED) {
                m->over = 0;
                khdays_mod_set_state(m, KHDAYS_MOD_LOADED, NULL);
                khdays_mods_log(m->manifest.id, "resumed", NULL, 0, "by the player");
            }
            break;
        case KHDAYS_ASK_OPTION:
        case KHDAYS_ASK_BUTTON:
            if ((m = by_id(msg.mod)) != NULL && msg.key != NULL) {
                khdays_api_menus_use(m, msg.key, msg.value);
                changed = 1;
            }
            break;
        default: break;
        }
        free(msg.mod);
        free(msg.key);
    }
}

/* A mod loaded again from its files: its state closed, its manifest read again, its main script run
 * again (its options' values and its own files are kept on disk). */
static void reload(KhdaysMod *m)
{
    unload(m);
    khdays_manifest_free(&m->manifest);
    free(m->data);
    m->data = NULL;
    khdays_mod_set_state(m, KHDAYS_MOD_OFF, NULL);
    if (!examine(m)) {
        return;
    }
    if (m->enabled) {
        load(m);
        if (m->state == KHDAYS_MOD_LOADED) {
            khdays_mods_log(m->manifest.id, "reloaded", NULL, 0, "its files changed or it was asked to");
        }
    }
    changed = 1;
}

/* Once a second: a mod whose files changed, or one saves/mods/reload.txt names (an id a line, or "*"),
 * is loaded again; the file is taken away once read (khdays-studio's console writes it). */
static void look_at_files(void)
{
    char *path = joined(user_folder, "mods/reload.txt"), *asked = NULL;
    if (path != NULL) {
        asked = khdays_folders_read(path, NULL);
        if (asked != NULL) {
            khdays_folders_remove(path);
        }
        free(path);
    }
    for (int i = 0; i < count; ++i) {
        KhdaysMod *m = mods[i];
        int named = 0;
        for (const char *line = asked; line != NULL && *line;) {
            const char *eol = strchr(line, '\n');
            size_t n = eol != NULL ? (size_t)(eol - line) : strlen(line);
            while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) {
                --n;
            }
            named = named || (n == 1 && line[0] == '*') ||
                    (m->manifest.id != NULL && n == strlen(m->manifest.id) && strncmp(line, m->manifest.id, n) == 0);
            line = eol != NULL ? eol + 1 : NULL;
        }
        if (m->enabled && (named || newest_file(m) != m->stamp)) {
            reload(m);
        }
    }
    free(asked);
}

/* KHDAYS_TRACE_MODS: each mod's time in its scripts per frame, every 600 frames. */
static void trace_time(void)
{
    for (int i = 0; i < count; ++i) {
        KhdaysMod *m = mods[i];
        if (m->state != KHDAYS_MOD_LOADED) {
            continue;
        }
        m->traced_sum += m->spent;
        m->traced_max = m->spent > m->traced_max ? m->spent : m->traced_max;
        if (++m->traced_frames == 600) {
            fprintf(stderr, "mods-trace: time: %s %.1f us a frame, at most %.1f us\n", m->manifest.id,
                    (double)m->traced_sum / 600e3, (double)m->traced_max / 1e3);
            m->traced_sum = m->traced_max = 0;
            m->traced_frames = 0;
        }
    }
}

/* Whether anything hears the game's events: a Lua mod, or a native plugin (runtime/plugins/plugins.h). */
static int heard(void)
{
    return count > 0 || khdays_plugins_listening();
}

void khdays_scripts_frame(void)
{
    if (tracing < 0) {
        tracing = getenv("KHDAYS_TRACE_MODS") != NULL;
    }
    {
        const int up = khdays_link_game_over();
        if (up && !game_over_up && fell_at != 0 && khdays_link_frame() + 1u - fell_at <= FALL_TO_GAME_OVER) {
            lost = 1;
        }
        game_over_up = up;
    }
    khdays_enemies_frame();
    khdays_api_screens_frame();
    if (!started) {
        const char *mods_path, *user;
        khdays_link_folders(&mods_path, &user);
        khdays_scripts_start_in(mods_path, user);
    }
    if (count == 0) {
        take_messages(); /* content mods turned on or off, or moved */
        if (changed) {
            changed = 0;
            publish();
        }
        if (khdays_plugins_listening()) {
            unsigned char *data;
            unsigned size;
            while (khdays_link_shared(&data, &size)) {
                khdays_plugins_shared(data, size); /* the host's plugins' messages */
                free(data);
            }
            khdays_script_events_frame(); /* the native plugins' events */
        }
        return;
    }
    for (int i = 0; i < count; ++i) {
        mods[i]->spent = 0;
        mods[i]->allowance = khdays_sandbox_frame_allowance;
    }
    take_messages();
    if (++looked >= 30) {
        looked = 0;
        look_at_files();
    }
    khdays_script_events_frame();
    khdays_api_time_frame();
    if (tracing > 0) {
        trace_time();
    }
    if (changed) {
        changed = 0;
        publish();
        tell_rooms(); /* the next room's: a session already joined keeps what it was joined with */
    }
}

void khdays_scripts_field_begins(void)
{
    field_begun = 1;
}

void khdays_scripts_mission_start(void)
{
    KhdaysEvent e;
    /* a mission runs again after each of its events: the first run since the field was made */
    if (!field_begun) {
        return;
    }
    field_begun = 0;
    lost = 0;
    fell_at = 0;
    if (tracing > 0) {
        fprintf(stderr, "mods-trace: mission start: mission %d\n", khdays_link_mission());
    }
    if (!heard()) {
        return;
    }
    if (count > 0) {
        khdays_api_time_mission_start(); /* every player of a room draws the same from here */
    }
    memset(&e, 0, sizeof(e));
    e.id = KHDAYS_EVENT_MISSION_START;
    e.a = khdays_link_mission();
    khdays_script_events_raise(&e);
}

/* How the run ended: lost after the game over of a fall (its give-up sets flag 0x2087 too), abandoned
 * with flag 0x2087, cleared as the results record it, lost otherwise. */
static const char *ending(int given_up)
{
    return lost ? "lost" : given_up ? "abandoned" : khdays_link_results_cleared() ? "cleared" : "lost";
}

void khdays_scripts_mission_end(int mission, int rank, int complete)
{
    KhdaysEvent e;
    const int given_up = khdays_link_flag(KHDAYS_LINK_GIVEN_UP);
    khdays_autosave_mission_end(); /* the port's autosave hears the same moment (runtime/menus/autosave.h) */
    khdays_link_next_seed();
    if (tracing > 0) {
        fprintf(stderr, "mods-trace: mission end: mission %d, rank %d, complete %d, given up %d, %s\n", mission, rank,
                complete, given_up, ending(given_up));
    }
    if (!heard()) {
        return;
    }
    memset(&e, 0, sizeof(e));
    e.id = KHDAYS_EVENT_MISSION_END;
    e.a = mission;
    e.b = rank;
    e.c = complete;
    e.d = given_up;
    e.text = ending(given_up); /* api 2's `ending` */
    khdays_script_events_raise(&e);
}

void khdays_scripts_hp(void *entity, int before)
{
    KhdaysEvent e;
    int hp, max;
    khdays_link_entity_hp(entity, &hp, &max);
    if (hp == 0 && before > 0 && khdays_link_is_player(entity)) {
        fell_at = khdays_link_frame() + 1u;
    }
    if (!heard()) {
        return;
    }
    if (hp == before) {
        return;
    }
    memset(&e, 0, sizeof(e));
    e.id = KHDAYS_EVENT_HP;
    e.a = before;
    e.b = hp;
    e.c = max;
    e.text = khdays_link_is_player(entity) ? "player" : "other";
    khdays_script_events_raise(&e);
}

uint16_t khdays_scripts_buttons(uint16_t buttons)
{
    if (khdays_screens_shown() == 0 && khdays_screens_entry_count() == 0) {
        return buttons;
    }
    return khdays_screens_buttons(buttons, khdays_link_title_idle(), khdays_link_language());
}

void khdays_scripts_saved(void)
{
    KhdaysEvent e;
    if (!heard()) {
        return;
    }
    memset(&e, 0, sizeof(e));
    e.id = KHDAYS_EVENT_SAVE;
    khdays_script_events_raise(&e);
}
