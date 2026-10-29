/* KH Mod SDK: the playable characters (kh.characters) -- the game's and the mods' (docs/CHARACTERS.md), and
 * behaviour given to them.
 *
 * Every character has a number: the game's are 0-18 (roxas, axel, xigbar, saix, xaldin, sora, demyx, larxene,
 * lexaeus, luxord, marluxia, riku, vexen, xemnas, xion, zexion, mickey, donald, goofy), a mod's come after them, given
 * as the game starts (find it by its key; never keep it between runs). A mod's character plays on the code of the
 * game's character it names as its base, as its description says; with no plugin it plays by its description alone.
 *
 * A plugin listens to a character and is called for the actors that play it, and for no other: as one enters the
 * field, each frame while it is there, as it starts an attack, as it leaves. Its actor is a kh.actors handle
 * (kh_actors.h), through which the plugin reads and changes it; hook points (kh_hooks.h) whose arguments name an
 * actor tell theirs with kh.actors' of(). On the game's thread. In a room every copy of the game calls its plugins
 * for every seat: what changes the game's state belongs to the host (docs/MODDING_NATIVE.md). */
#ifndef KH_CHARACTERS_H
#define KH_CHARACTERS_H

#include "kh_actors.h"
#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_CHARACTERS "kh.characters"
#define KH_CHARACTERS_VERSION 1

typedef struct KhCharacterInfo {
    uint32_t size;        /* the caller sets sizeof(KhCharacterInfo) */
    int32_t number;       /* its number this run */
    int32_t base;         /* the game's character it plays as (the game's: itself) */
    const char *key;      /* the game's: its name above; a mod's: its folder's name */
    const char *identity; /* a mod's "<mod id>/<key>"; NULL for the game's */
    const char *name;     /* a mod's name in the game's language (else its own); the game's: its key */
} KhCharacterInfo;

/* What a listener is called for; any may be NULL. `user` is listen()'s. */
typedef struct KhCharacterCalls {
    uint32_t size; /* the caller sets sizeof(KhCharacterCalls): later fields are not called for an older plugin */

    /* An actor playing the character is in the field, at seat `seat`. */
    void (*entered)(KhActor actor, int32_t seat, void *user);

    /* Each frame (60 a second) while it is there, after the game's own. */
    void (*frame)(KhActor actor, void *user);

    /* It starts an attack: `attack` is the attack's record in its combo files (cm.b), as a character's [moveset]
     * names them; each hit of a combo is one. */
    void (*attack)(KhActor actor, int32_t attack, void *user);

    /* It has left (its area ended, the field left): the handle no longer answers. */
    void (*left)(KhActor actor, void *user);
} KhCharacterCalls;

/* A listener: 0 is none. */
typedef uint64_t KhCharacterListener;

/* Flags of play(). */
#define KH_PLAY_LOOP 1u /* again from its start at its end, until stop_playing() */

typedef struct KhCharacters {
    uint32_t size;

    /* The characters: the game's 19 first (index = number), then the mods' in their numbers' order. */
    int32_t (*count)(void);
    KhResult (*info)(int32_t index, KhCharacterInfo *out);

    /* A character's number by its key: a mod's identity ("<mod id>/<key>"), else the key of a character of the
     * plugin's own mod, else one of the game's names; -1 when none. */
    int32_t (*find)(KhPlugin *plugin, const char *key);

    /* Calls `calls` (copied) for the actors that play character `number`; 0 when there is no such character (told).
     * Removed by unlisten() or when the plugin stops. */
    KhCharacterListener (*listen)(KhPlugin *plugin, int32_t number, const KhCharacterCalls *calls, void *user);
    void (*unlisten)(KhPlugin *plugin, KhCharacterListener listener);

    /* The picture of a mod's character with its own skeleton (`skeleton = own`): its actor drawn playing its clip
     * `action` -- a name of its [actions], one of its base's animations or a name of its own -- from the clip's
     * start at its own speed, instead of what the game plays, until stop_playing() or (without KH_PLAY_LOOP) the
     * clip's end. Only the picture: the game moves, times and hits the actor as before (a plugin's own move hides the
     * buttons the game would act on, kh_input.h). Every actor playing the character shows it. KH_UNSUPPORTED for
     * another character or an action it has not got; a plugin's clips stop with it and as the field is left. */
    KhResult (*play)(KhPlugin *plugin, KhActor actor, const char *action, uint32_t flags);
    void (*stop_playing)(KhPlugin *plugin, KhActor actor);
} KhCharacters;

#ifdef __cplusplus
}
#endif

#endif
