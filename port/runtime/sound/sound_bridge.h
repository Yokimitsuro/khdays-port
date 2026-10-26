/* The bridge between the game's thread and the sound (sound_bridge.c): the ARM7's sound driver runs
 * on the DS clock under one lock, advanced by the sound thread every millisecond and by the game's
 * thread when it talks to it, so the sound goes on while the game is busy and a read-back is never
 * later than the ARM7 would make it. */
#ifndef KHDAYS_SOUND_BRIDGE_H
#define KHDAYS_SOUND_BRIDGE_H

#include "../../hal/hal.h"
#include "../../sound/archive.h"

/* Starts the native driver (port/sound/snd7_*.c) over the channels the
 * player hears -- or none with KHDAYS_SOUND=0, the command lists then taken
 * as run (arm7.c). Returns 0 with no driver. */
int khdays_sound_bridge_start(void);

/* The game's reset: the driver stopped, every channel silent, the replies
 * for the game that ended dropped; then, the static module loaded again,
 * started anew. The PC's output goes on. */
void khdays_sound_bridge_stop(void);
void khdays_sound_bridge_restart(void);

/* The game's thread: a word the ARM9 sent on the sound's tag. The driver is
 * brought up to now and takes it at once, as the ARM7 does. Returns 0 when
 * no driver runs (arm7.c then takes the list as run). */
int khdays_sound_bridge_word(u32 data, u32 err);

/* The sound thread: the driver and the channels up to `cycle`. */
void khdays_sound_bridge_advance(u64 cycle);

/* The game's thread, at its event points: the driver caught up to `cycle` if
 * one of its alarms is due by then and the sound thread has not run it yet,
 * and the driver's replies moved into the IPC receive FIFO (with its
 * interrupt), in order. */
void khdays_sound_bridge_event(u64 cycle);

/* Replies the game's thread has not taken yet (for the interrupt watcher). */
int khdays_sound_bridge_replies_waiting(void);

/* KHDAYS_PROFILE: the milliseconds the game's thread spent catching the
 * driver up since the last call. */
double khdays_sound_bridge_take_catchup_ms(void);

/* KHDAYS_PROFILE: the time the driver itself took since the last call, in
 * ms (the channels it runs not counted). */
double khdays_sound_bridge_take_driver_ms(void);

/* The host: the mixer -- the DS's (`ds_exact`: 32,728 Hz, 16 bits,
 * khdays_host_audio) or the PC's at `rate` (floats, khdays_host_audio_float)
 * -- from the end of the buffer being filled. */
void khdays_sound_bridge_set_output(int ds_exact, int rate);

/* A test's driver in the native one's place, for khdays_sound_bridge_start. */
struct KhdaysSnd7Driver;
void khdays_sound_bridge_use_driver(const struct KhdaysSnd7Driver *driver);

/* The host: the player's volumes, 0-100 (port/sound/bus.h), heard from
 * the next output sample. */
void khdays_sound_bridge_set_volumes(int volume, int music, int effects, int voices, int movies);

/* The game's thread: the channels a movie's sound plays on (`mask`), from Ov024_MobiClip_StartAudio's
 * lock; 0 from its release (prepare.py HOOKS). Their category is KHDAYS_SOUND_MOVIES. */
void khdays_sound_movie_channels(u32 mask);

/* A scene's HD videos coming (runtime/movies/movie_hd.c): while held, the music the game starts is not
 * heard (an event's own, begun as the game goes down to black before them); let go, it is heard again
 * where it has got to. */
void khdays_sound_bridge_hold_new_music(int hold);

/* The Walk mod (runtime/input/walk.c): while `owns` the game's footsteps (a FIELD_*FS_* archive's entry,
 * the run's, played under the walk) are started but not heard, the walk playing the one the game last asked
 * for at its own feet: khdays_sound_bridge_footstep gives that sound's archive and entry (0 when none yet),
 * khdays_sound_bridge_expect_footstep lets the next one through. */
void khdays_sound_bridge_walk_steps(int owns);
int khdays_sound_bridge_footstep(char *archive, int archive_size, char *entry, int entry_size);
void khdays_sound_bridge_expect_footstep(void);

/* The game's thread, from NNSi_SndPlayerStartSeq (prepare.py HOOKS): ARM7 sequence player
 * `arm7_player` now plays for the archive's player `archive_player`, whose category (music, effects,
 * voices) it takes (port/sound/categories.h). */
void khdays_sound_player_assigned(int arm7_player, int archive_player);

/* The game's thread, from PrepareStrm (prepare.py HOOKS): a stream player has prepared a stream: its
 * channels and their rings, the header the player read, where the stream's file is (in the archive
 * or in memory), its first sample and whether a channel past the file's is silent. Those rings then
 * take their samples from the port's own reading of the file (port/sound/stream_reader.h). */
/* StartSeq's entry: the game asks for a sequence (KHDAYS_TRACE_SOUND_COMMANDS times it). */
void khdays_sound_sequence_requested(void);

/* NNS_SndArcStrmPrepare: the game asks for stream `stream_number`
 * (KHDAYS_TRACE_SOUND_COMMANDS times it to its first sound). */
void khdays_sound_stream_requested(int stream_number);

void khdays_sound_stream_prepared(int player, int stream_number, int channels, const u8 *channel, u32 ring,
                                  u32 ring_bytes, const void *header, u32 file_at, int in_memory, u32 top, u32 bottom,
                                  u32 start_sample, int mono);

/* The game's thread, from StartSeq and StartSeqArc (prepare.py HOOKS): a sequence or a sequence
 * archive's entry starts on ARM7 player `arm7_player`, prepared by the command list `command_tag`
 * names; a mod's file may replace it (sound_mods.h). */
void khdays_sound_sequence_started(int arm7_player, int kind, int number, int entry, u32 command_tag);

/* The game's thread, from NNSi_SndArcLoadWaveArc (prepare.py HOOKS): wave archive `wave_archive` is at
 * `address` (NULL: not loaded); a mod's files may replace its waves. */
void khdays_sound_wave_archive_loaded(int wave_archive, const void *address);

/* What the sound reads of the game's, as the dump does: the sound archive the game opens, the ARM7
 * binary's driver segment for the native driver (0 and a message if the ROM's is not the one it was
 * written from), and the stream player's ADPCM tables in the static module. */
const KhdaysSoundArchive *khdays_sound_bridge_archive(void);
/* The path of the sound archive the game opens (gSndSoundDataPath, at its DS address). */
const char *khdays_sound_data_path(void);
int khdays_sound_bridge_native_segment(void);
#define KHDAYS_NNS_ADPCM_INDEX_TABLE 0x02041b04u
#define KHDAYS_NNS_ADPCM_STEP_TABLE 0x02041b14u

#endif
