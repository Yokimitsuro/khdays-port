/* The sounds mods replace: a sequence, a sequence archive's entry, a stream or a wave, played from
 * mods/<Mod>/sounds/<identifier>.<wav|flac|ogg> while the original goes on unheard (what the game reads
 * back stays the original's), each decoded once on a thread of its own and played at its own rate
 * (docs/SOUND.md, "Mods' sounds"): the sequence archives' entries from the first sound on and a wave
 * archive's waves as it loads, ahead, the others when they are first wanted. All but the decoding runs
 * under the sound bridge's lock. */
#ifndef KHDAYS_SOUND_MODS_H
#define KHDAYS_SOUND_MODS_H

#include "../../hal/hal.h"
#include "../../sound/archive.h"
#include "../../sound/channels.h"
#include "../../sound/replace.h"
#include "../../sound/snd7.h"

/* Whether any mod has a sounds/ folder (looked at once); without one
 * nothing here does anything. */
int khdays_sound_mods_present(void);

/* KHDAYS_TRACE_SOUND: each replacement as it starts and ends. */
void khdays_sound_mods_trace(int on);

/* The game's thread: a sequence (KHDAYS_SOUND_SEQUENCE: `number`) or a
 * sequence archive's entry (KHDAYS_SOUND_SEQUENCE_ARCHIVE_ENTRY: `number`,
 * `entry`) starts on ARM7 player `arm7_player`, prepared by the command
 * list tagged `command_tag`: until the ARM7 has done that list, the player
 * shows what it played before. */
void khdays_sound_mods_sequence(const KhdaysSoundArchive *archive, int arm7_player, int kind, int number, int entry,
                                u32 command_tag);

/* The game's thread: stream `stream` prepared on stream player
 * `stream_player`, from its sample `start` at `rate`, looping as `loop`
 * (its end the original's samples), in blocks of `block` samples. */
void khdays_sound_mods_stream(const KhdaysSoundArchive *archive, int stream_player, int stream, uint32_t start,
                              uint32_t rate, const KhdaysOriginalLoop *loop, uint32_t block);

/* The game's thread: wave archive `number` of the archive is at `address`
 * (NNSi_SndArcLoadWaveArc): the channels that start on its waves may play
 * a mod's files for them. */
void khdays_sound_mods_wave_archive(const KhdaysSoundArchive *archive, int number, u32 address);

/* The channels' side (KhdaysSoundSource): a channel starting on a replaced
 * wave's data (SAD the wave's + 12) is bound to its file -- a handle with
 * KHDAYS_WAVE_HANDLE set, which a stream's never has -- and then takes its
 * index-th sample since its start from it. */
#define KHDAYS_WAVE_HANDLE 0x40000000
int khdays_sound_mods_bind_wave(int channel, uint32_t sad, uint32_t len, int format);
int khdays_sound_mods_wave_sample(int handle, uint64_t index, int32_t *sample);

/* Whether the sound of ARM7 player `p`, or of stream player `s`, is a
 * replacement's now (its channels are left out of the mix). */
int khdays_sound_mods_replaces_player(int p);
int khdays_sound_mods_replaces_stream(int s);

/* One output sample at `rate`: the replacements move on and add to `out` (the PC mixer's scale) under their
 * category's volume (`player_gain` per ARM7 player, `stream_gain` per stream player, `channel_gain` per channel for
 * the waves, NULL none) and SOUNDCNT's master volume. `finished_tag`: the last command list the ARM7 has done. A file
 * longer than its original plays on to its end once the original ends by itself (docs/SOUND.md). */
void khdays_sound_mods_mix(const KhdaysSoundHw *hw, const KhdaysSnd7Driver *driver, uint32_t rate,
                           uint32_t finished_tag, const float player_gain[16], const float stream_gain[4],
                           const float *channel_gain, float out[2]);

#endif
