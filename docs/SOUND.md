# How the port plays the game's sound

The game hands its sound to the ARM7 as ever -- NitroSDK's protocol: command
lists sent as words on the FIFO's sound tag, replies and alarms sent back, the
shared work the ARM9 reads. The port runs that part of the DS natively; what the
game does with its sound is in `RUNTIME_MAP.md` section 6.

## The driver

`port/sound/snd7.h` is the driver's interface. The native driver
(`port/sound/snd7_*.c`) is the ARM7 binary's own sound driver written in C from
its code, each function citing the ARM7 address it comes from (`RUNTIME_MAP.md`
section 6 maps them); it was checked against that code interpreted until the ARM
interpreter left the port. It keeps its state in C and reaches the DS's memory --
the ARM9's command lists, sequences, banks and waves, and the shared work it
writes -- through `ds_base`. The bridge between the game's thread and the sound
thread (`runtime/sound/sound_bridge.c`) owns the time and the lock.

- **The sequencer** (`snd7_seq.c`): the 16 sequence players and the 32 tracks
  they take, reading the sequences (SSEQ) the ARM9 loaded and playing their
  notes with the bank's instruments. The ARM9 sets their fields by offset
  (`SND_COMMAND_PLAYER_PARAM`, `TRACK_PARAM`), so both keep the ARM7's layout
  field for field.
- **The banks** (`snd7_bank.c`): a bank (SBNK) has at +0x18 its four wave
  archives, 8 bytes each, the archive's address first; at +0x38 its program
  count; at +0x3c a word per program, the instrument's type in the low byte and
  its place above. An instrument's settings are 10 bytes: the wave (2), the
  archive (2), the original key, attack, decay, sustain, release and pan. A wave
  archive (SWAR) has at +0x38 its wave count, at +0x3c a word per wave (from the
  archive below 0x02000000, else in memory); a wave is 12 bytes of parameters,
  then its data.
- **The channel registers** (`snd7_hw.c`): set up, stop, volume, timer and pan,
  the master pan and the surround decay laid over every channel but 1 and 3,
  written in the ARM7's order and access sizes (a CNT without its start bit
  stops a channel).
- **The alarms** (`snd7_alarm.c`, 8 of 0x40 bytes at 0x03808c98): the ARM9 sets
  one up (`SND_COMMAND_SETUP_ALARM`) and starts it with its channels
  (`SND_COMMAND_START_TIMER`); each time it fires the ARM9 hears its number and
  id on the sound's tag -- the streams' and the movies' refills.
- **The BIOS tables** (`snd7_tables.c`): GetPitchTable (SWI 1Bh) and
  GetVolumeTable (SWI 1Ch). GBATEK gives their ranges, not their contents: the
  pitch table is 0x10000 x (2^(i/768) - 1) rounded, which matches a dump of the
  BIOS table in all 768 entries; the volume table is the ARM9's own copy, byte
  for byte the BIOS's.

## The channels and the mixers

`port/sound/channels.c` is the DS's sixteen sound channels as GBATEK describes
them ("DS Sound Channels 0..15", "DS Sound Notes"), plain C so the tests, the
offline tools and the driver each keep a set:

- a channel plays PCM8, PCM16 or IMA-ADPCM from memory, or a PSG square wave
  (channels 8-13) or noise (14, 15), a sample per overflow of its timer, which
  counts at half the clock from SOUNDxTMR;
- a start takes effect after 3 samples (PCM), 11 (ADPCM: 3 and the header's 8)
  or 1 (PSG, noise); a one-shot sound clears its busy bit as its last sample
  begins and stops when it ends, keeping that sample out while Hold is set; a
  looped one plays the part before the loop start once and the rest forever,
  ADPCM taking back the state it had there;
- the capture units are not modelled: a capture that starts is reported.

A channel's samples can come from elsewhere than its data in memory: a music
stream the port reads itself, or a wave a mod replaces. At each start of a PCM
or ADPCM channel the source is asked whether that data (SAD, LEN, the format) is
its own; if so the channel's samples come from it until it answers 0 (a PCM
channel then reads its memory again; an ADPCM one keeps to its source, having
no decoder state of its own by then).

The mixers read the channels' current samples:

- **The DS's** (`mix_ds.c`): volume divider (16.4), volume factor (16.11),
  panning (16.18), rounded down to 16.8, summed (20.8), master volume, the
  fraction stripped (14.0), the bias added, clipped to 10 bits. The mixer runs
  faster inside the chip, but GBATEK does not say how the PWM takes its value,
  so the channels are read as each output sample is due.
- **The PC's** (`mix_pc.c`, the default): the same channels read at the
  instant of each output sample at the PC's rate, their divider (1, 2, 4 or
  16), volume and each side's share of the pan (N/128, 127 as 128/128) applied
  at full precision, channels 1 and 3 routed as SOUNDCNT says, mixed in floating
  point. A channel is read between its samples (`khdays_sound_hw_smooth_sample`:
  a Catmull-Rom cubic over its last four, so two of its samples late, from how
  far its timer is towards the next), where the DS holds each sample until the
  next -- whose steps a PC's output makes heard as a metallic edge; a square
  wave or noise stays as it is. A mix louder than full scale is turned down at
  once, both sides alike, just enough to stay under 0.98, and back up over a
  tenth of a second (`KhdaysSoundLimiter`), rather than clipped; below it
  nothing changes. Its scale is the DS mixer's before rounding, divided by
  32768. Seen: a walk's 148 seconds through the field peaked at the limiter's
  0.98 in 26 samples, none clipped.

## The categories

The volume settings tell music, effects and voices apart by the sound
archive's player each channel serves (`port/sound/categories.c`, from
`snd/sound_data.sdat`'s names): sequence players `PLAYER_BGM` and
`PLAYER_BGM_EVENT` carry all 37 music sequences; `PLAYER_SE`, `PLAYER_MAGIC`,
`PLAYER_WEAPON`, `PLAYER_GIMMICK` and `PLAYER_SE_EVENT` the effects;
`PLAYER_VO1-4` the voices; stream players `PLAYER_STRM_BGM` (the menus' music)
and `PLAYER_STRM` (the jingles) carry music, `PLAYER_STRM_V01` the 106 voice
lines. A channel the game sets up itself (the movies) counts as an effect; a
note whose track let it go keeps its category; a channel a mod's file plays in
place of gets 0.

The archive (`port/sound/archive.c`) is read as NitroSystem's `sndarc.c` reads
it: the file header ("SDAT") with the offsets and sizes of the symbols (SYMB,
+0x10), the records (INFO, +0x18), the FAT (+0x20) and the files (+0x28). SYMB
and INFO each start with a block header, then 8 offsets to tables -- sequences,
sequence archives, banks, wave archives, players, groups, stream players,
streams -- each a count and as many offsets (0: none): in SYMB to names, in
INFO to records. SYMB's sequence archive table has two words an entry, the
archive's name and a table of its entries' names.

## The music streams

NitroSystem's stream player loads each channel's ring (`RUNTIME_MAP.md`
section 6, "The music streams' ring"). The port makes the same samples from the
stream's file by the same steps (`NNSi_SndArcStrm_MakeWaveData`'s), with its
own state for each stream (`port/sound/stream_reader.c`), and is the channels'
source for them: a channel playing a stream's ring gets what the ring holds
when every load is in time, whether or not the game's stream thread has had its
turn. The game's own player goes on as before: its loads, its position and its
end are still what the game reads.

## Mods' sounds

A mod replaces a sequence, a sequence archive's entry, a stream or a wave with
`mods/<Mod>/sounds/<identifier>.<wav|flac|ogg>` (`port/sound/archive.h` names
the identifiers: `seq/<name>`, `seqarc/<archive>/<entry>`,
`wave/<archive>/<index>`, `strm/<name>`). The file is decoded once, on a thread
of its own, to 16 bits with its loop: a WAV's `smpl` loop, else the Vorbis
comments `LOOPSTART` and `LOOPLENGTH` of a FLAC or Ogg file, else the original's
loop scaled to the file's rate; a sound the original plays once plays once
(`port/sound/replace.c`). A file not decoded yet when its sound starts leaves
the original heard, so the short ones are decoded ahead: the sequence archives'
entries (sound effects, voices) from the game's first sound on, a wave archive's
replaced waves as it loads; a sequence or stream (music) when it is first
wanted, and what is wanted now goes before those. It plays at its own rate
(below). A file above 48,000 Hz (the PC's usual rate) is brought down to it as
it is decoded, through a low-pass filter (a sinc of 16 zero crossings each side
under a Blackman window), so that what it holds above half that rate does not
fold back as noise. The original goes on unheard (what the game reads back stays
the original's):

- a sequence or entry from when its ARM7 player starts, under that player's
  volume;
- a stream at the sample its channels play, through their volume and pan;
- a wave by each channel that plays it, at its pitch, through its envelope,
  volume and pan -- at the PC's output read from the file at each output
  sample, the channel itself silent (it would have brought the file down to
  its own rate, often 16,000-32,000 Hz, and held each sample); a stereo file
  mixed down, the channel being one side.

The file is read between its samples (cubic). Only for the DS's output, at the
original's rate, are they held as the channel holds its own. A file longer than
its original plays on to its end, at the volume and pan it had, once the
original ends by itself: a sequence whose player no stop reached
(`KhdaysSnd7Driver.player_stops`, SND_StopSeq's count), a stream that reached
its last block, a wave whose one-shot sample ran to its end (the channel's
`ended`; the rest plays apart from the channel, which the driver may give to
another note, until the game stops the note's player); stopped by the game, it
stops with it. Seen: a voice of 1.24 s over se_vex_WAVE's 0.69 s wave 1 was cut there;
a 2 s tone over a 0.08 s menu click now plays its 2 s. The voices' streams
(those of a voices' stream player, PLAYER_STRM_V01's lines) are decoded ahead
with the sequence archives' entries.

A mod's character gives its base's sounds files of its own (or silence) for its
own actor only (CHARACTERS.md, `[sounds]`): the game names the actor whose
sound it starts (its seat, from `Ov022_PlayEntityVoice` and
`Ov022_SpawnVoiceCue`), and a sequence archive's entry it starts then plays the
character's file instead of the original or a sound mod's; `none` plays nothing
as long as the original lasts. Its files are decoded ahead with the others.

## The originals, to make replacements from

`khdays-native --dump-sounds [DIR [NAME...]]` writes every sound of the archive as WAV
under the identifier a replacement takes, headless: `wave/<archive>/<index>.wav`
(each wave an instrument plays, as the DS's channel decodes it),
`strm/<name>.wav` (each stream, as NitroSystem's stream player makes it),
`seq/<name>.wav` and `seqarc/<archive>/<entry>.wav` (each sequence and entry,
played by the native driver and the PC's mixer at 48,000 Hz); with names after the folder, those sounds alone
(`seqarc/COMMON_SYS/se_sys_click`: the Menu Overhaul's menu cues, tools/mods/overhaul/menu_sounds.py). A
sequence's note of length 0 plays its wave through (its track waits for it): the render takes that wait for no
loop, up to 30 s. Loops go in
`smpl`. The dump is the game's data: only under `saves/`, never in a
repository.

A sequence is rendered offline as the game plays it (`port/sound/render.c`):
the native driver takes the commands StartSeq and StartSeqArc send, its time
runs as fast as the mixing goes, and the PC's mixer makes the output. The
player's volume stays at 0 dB (a replacement plays under the game's volume, so a
rendered sequence put back sounds as the original), the sequence's variables as
`SND_PrepareSeq` leaves them. A sequence that ends plays until its last note has
died away; one that loops plays its loop twice -- found where the sequencer's
state first comes back to one it had, the second time as the file's loop -- and
anything else up to a limit. The state compared leaves out the tempo's counter,
which only says where the tick fell between two of the driver's steps: with it,
a loop whose ticks are not a whole number of steps would come back only after
as many loops as it takes to fall the same way again (7 at 140 beats a minute);
without it the loop is found the second time round, joining within one step
(5.21 ms).
