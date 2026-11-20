/* Time, music, the room's seed and the mods' shares, the game's side (time_link.h). docs/RUNTIME_MAP.md,
 * "The game's music"; docs/MULTIPLAYER.md, "The mods' records". */
#include "time_link.h"

#include "game_link.h"

#include "../core/events.h"
#include "../net/net_session.h"
#include "../sound/sound_bridge.h"
#include "../core/game_memory.h"
#include "khdays_ds_symbols.h"

#include <SDL3/SDL.h>

#include <string.h>
#include <time.h>

#define MUSIC_TRACK 0xb46f6u /* gSoundMgr: the music player's track (a sequence), -1 none (SoundMgr_Update) */
#define MUSIC_STOP 3         /* the sound manager's request kinds (ScriptQueue_Push's callers) */
#define MUSIC_RESUME 5

extern int SoundMgr_SwitchBgm(int seq, int fade);
extern int SoundMgr_SwitchBgmResume(int seq, int fade);
extern int SoundMgr_QueueKind1(int arg);
extern int SetSelectionIfChanged(int param_1);
extern void RequestQueue_SetOrPushKind3(int arg);

static struct {
    int holding;    /* a mod's track plays: the game's requests are kept back */
    int letting;    /* a request of the port's goes through */
    unsigned scene; /* the scene it plays in */
    int kept;       /* the game's last request kept back: its kind, track and value */
    int kind, arg, value;
    int before; /* the track playing as the mod's took its place, -1 none */
} music;

static unsigned long long own_seed;

unsigned khdays_link_vblanks(void)
{
    return khdays_events_vblank_count();
}

int khdays_link_music_track(const char *name)
{
    const KhdaysSoundArchive *archive = khdays_sound_bridge_archive();
    const int count = archive != NULL ? khdays_sound_archive_count(archive, KHDAYS_SOUND_SEQUENCE) : 0;
    for (int n = 0; n < count; ++n) {
        char id[96];
        if (khdays_sound_archive_identifier(archive, KHDAYS_SOUND_SEQUENCE, n, 0, id, (int)sizeof(id)) &&
            (strcmp(id, name) == 0 || (strncmp(id, "seq/", 4) == 0 && strcmp(id + 4, name) == 0))) {
            return n;
        }
    }
    return -1;
}

int khdays_link_music_request(int kind, int arg, int value)
{
    if (!music.holding || music.letting) {
        return 0;
    }
    music.kept = 1;
    music.kind = kind;
    music.arg = arg;
    music.value = value;
    return 1;
}

const char *khdays_link_music_play(int track, int fade)
{
    const unsigned manager = rd32(KHDAYS_DS_gSoundMgr);
    if (!khdays_game_ram(manager)) {
        return "no game";
    }
    if (!music.holding) {
        music.before = (short)rd16(manager + MUSIC_TRACK);
        music.kept = 0;
        music.holding = 1;
    }
    music.scene = khdays_link_scene(NULL);
    music.letting = 1;
    SoundMgr_SwitchBgm(track, fade);
    music.letting = 0;
    return NULL;
}

void khdays_link_music_stop(int fade)
{
    if (!music.holding) {
        return;
    }
    music.holding = 0;
    if (!music.kept) {
        if (music.before >= 0) {
            SoundMgr_SwitchBgm(music.before, fade);
        } else {
            RequestQueue_SetOrPushKind3(fade);
        }
        return;
    }
    switch (music.kind) {
    case 1: SoundMgr_QueueKind1(music.arg); break;
    case 2: SetSelectionIfChanged(music.arg); break;
    case MUSIC_STOP: RequestQueue_SetOrPushKind3(music.value); break;
    case MUSIC_RESUME: SoundMgr_SwitchBgmResume(music.arg, music.value); break;
    default: SoundMgr_SwitchBgm(music.arg, music.value); break;
    }
}

int khdays_link_music_playing(void)
{
    return music.holding;
}

void khdays_link_music_frame(void)
{
    if (music.holding && khdays_link_scene(NULL) != music.scene) {
        khdays_link_music_stop(0);
    }
}

static unsigned long long fresh_seed(void)
{
    unsigned long long seed = SDL_GetPerformanceCounter() ^ (unsigned long long)time(NULL) << 32;
    return seed != 0 ? seed : 1;
}

unsigned long long khdays_link_seed(int *in_room)
{
    unsigned long long seed;
    *in_room = khdays_net_seed(&seed);
    if (*in_room) {
        return seed;
    }
    if (khdays_net_in_session()) {
        return 0; /* a guest, its host's seed not come yet */
    }
    if (own_seed == 0) {
        own_seed = fresh_seed();
    }
    return own_seed;
}

void khdays_link_next_seed(void)
{
    own_seed = fresh_seed();
    khdays_net_set_seed(fresh_seed()); /* the host's room; nothing on a guest or alone */
}

int khdays_link_share(const void *data, unsigned size)
{
    return khdays_net_share(data, size);
}

int khdays_link_shared(unsigned char **data, unsigned *size)
{
    return khdays_net_shared(data, size);
}
