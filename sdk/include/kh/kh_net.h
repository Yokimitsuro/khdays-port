/* KH Mod SDK: a plugin in a multiplayer room (kh.net).
 *
 * A room plays the host's game: the host's copy works out the actors, the hits and the enemies, each guest's copy
 * shows what the host sends and sends the host its player's buttons. A plugin's mod says it is made for rooms with
 * `multiplayer = yes` in its mod.ini; without it, in a room, the port keeps the plugin from changing the game (its
 * hooks are not called, its hits, HP, places, enemies, hidden buttons and clips refused) while it still hears its
 * events. Every copy of a room plays with the same plugins: they are part of the gameplay mods' fingerprint (their
 * libraries, their parameters), and a copy with others is refused as the room's mods differ.
 *
 * A message of the plugin's own goes from the host's copy to the guests', each with a name and a version the plugin
 * chooses (a later plugin changes the version when it changes what the message holds): every copy's plugins that
 * subscribe to `<mod id>.<name>` (kh_events.h) hear it, the version in the event's `a`, its bytes in its data. On the
 * game's thread. */
#ifndef KH_NET_H
#define KH_NET_H

#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_NET "kh.net"
#define KH_NET_VERSION 1

typedef struct KhNet {
    uint32_t size;

    /* 1 in a room (or joining one). 1 when this copy decides the game: the room's host, or alone. */
    int32_t (*in_room)(void);
    int32_t (*host)(void);

    /* Message `name` at `version` with `size` bytes, from the host's copy to every guest's; KH_UNSUPPORTED on a
     * guest's copy, outside a room, or for a plugin whose mod does not say `multiplayer = yes`. */
    KhResult (*send)(KhPlugin *plugin, const char *name, uint32_t version, const void *data, uint32_t size);
} KhNet;

#ifdef __cplusplus
}
#endif

#endif
