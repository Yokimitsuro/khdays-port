# How the port stands in for the DS

The game's C runs natively, but it still expects a DS around it: its memory at
the DS's addresses, its threads and interrupts, its timers, its card, its
wireless. `port/runtime` is the game's side of that, `port/host` the PC's, and
`port/hal` the NitroSDK routines the decompilation keeps as assembly, written
in C. What the game does is in `RUNTIME_MAP.md`; how its C is made to run, in
`BUILD.md`.

## Memory

The DS's ARM9 address space lives in the game's memory window
(`runtime/core/window.h`, `memory.c`): each region at the window's start plus
its DS address. Windows maps 64 KB at a time, so each region is rounded out to
that; the rest of the window is inaccessible, the null range among it.

| DS address | What is there |
|---|---|
| 0x01ff8000 | ITCM, 32 KB |
| 0x02000000 | main RAM, 4 MB, mirrored up to 0x02800000 (0x027ff000 is 0x023ff000) |
| 0x027e0000 | DTCM, 16 KB |
| 0x02800000 | the headers' heaps (a header is named by 24 bits), or two more mirrors |
| 0x03000000 | shared WRAM; 0x037f8000 the ARM7's WRAM, for its sound driver |
| 0x04000000 | I/O registers and ports: inaccessible, the port reaches them (`BUILD.md`, "The registers") |
| 0x05000000 | palettes; 0x06000000 VRAM; 0x07000000 OAM; 0x08000000 the empty GBA slot |
| 0x0b100000 | the register model's memory (`io.h`); 0x0b200000 the data area |
| 0x0c000000 | the stacks of the game's threads (`host/app/context.h`) |
| 0x20000000 | the game's heaps, committed a granule at a time as the game touches them |

The game's main heap (0xc8000 bytes) and sub heap (the rest of the arena) are
made by the port over its region (`runtime/core/game_heap.c`). A packed
archive's header is named in a file handle by 24 bits from 0x01ff8000, so the
headers `Msg_OpenContainerAndReadHeader` reads come from heaps of their own
below 0x02ff8000. The game's resource cache (`ResCache_FindSlot`: `gFileLoader`'s
+8 points at 128 entries of 0x30 bytes, a reference count at +0 and the file's
name or handle at +0x10) keeps an entry while it is used, even after its header
is freed (`Ov022_BuildActorSlot` frees a character's weapons' header as soon as
their entry is made), so a header given out where a freed one was would be
found under the old one's entries -- a second character's weapons were the
first's. On the DS other blocks fill that place first; here the headers' heaps
hold headers alone, so a place still named in the cache is kept aside until no
entry names it.

Where the game's data comes from (`runtime/core/origin.h`): which game file,
and where in it, the bytes of main RAM and of each VRAM bank hold, followed
from the game's loads (`prepare.py` hooks) through the SDK's copies
(`hal/sdk_mi.c`) and DMA (`io_regs.c`). It names the graphics the game uploads,
for replacements and the dump. A store of the game's own C is not seen, so a
user of a name checks the bytes too.

## Threads, interrupts and time

The SDK's scheduler is decompiled C and runs unchanged. Each game thread runs
on an execution context of the port's (`host/app/context.h`) with a stack of
its own; the DS stack the game gave it stays unused. `hal/sdk_thread.c` keeps
the register images `OS_InitContext`, `OS_SaveContext` and `OS_LoadContext`
read and write, `runtime/core/threads.c` the contexts underneath:
`OS_SaveContext` returns 0 and records the current context, `OS_LoadContext`
switches to the next one's, and a thread scheduled again resumes inside that
call -- where the DS's "SaveContext returned 1" path goes.

An interrupt is taken where the runtime delivers it (`khdays_irq_deliver`):
`hal/sdk_irq.c` is what the BIOS's IRQ vector does, NitroSDK's dispatcher
(`OS_IrqHandler`) and its return path (`OS_IrqHandler_ThreadSwitch`). The
interrupted code is whatever context runs there, and a thread switch at the end
of the IRQ is a context switch, which returns when that thread is scheduled
again, as the DS resumes it from its saved context. The emulated CPSR
(`hal/sdk_os.c`, `khdays_cpsr`) matters only for the I/F state callers save and
restore (`OSIntrMode` = the I bit, 0x80; F is 0x40) and that thread switches
carry it; crt0 leaves the CPU in system mode with both enabled.

NitroSDK counts its tick (`OS_GetTick`) with hardware timer 0, 64 cycles a
tick, the overflows carried by `OSi_CountUpTick`, and wakes its alarms with
timer 1, which `OSi_SetTimer` programs to overflow when the alarm queue's head
is due. Natively (`hal/sdk_time.c`) the tick is the port's clock in the same
unit, and the alarm timer a deadline at which the frame loop raises timer 1's
interrupt (`runtime/core/events.c`); `prepare.py`'s hooks put these in place of
the timer registers. The alarm queue, its handler and their callers stay the
decomp's C.

## The display

The game's work for a frame ends long before the DS's line-by-line drawing
would, so the whole frame is drawn when VBlank begins, from the registers,
VRAM, palettes and OAM as the game left them (`runtime/display/display.c`), and
goes to the window as a frame package. With a window a VBlank is not drawn
again when nothing the display reads changed (about every other one: the game
runs at 30 frames a second), or when both screens are the same single colours
as the last frame (a fade or a load). How it is drawn: `RENDERING.md`.

## The card, the save and the language

The game card's ROM is rebuilt from the extracted data (`runtime/core/rom.cpp`):
the game reads its files through the card bus as on the DS, each file through
the VFS so a mod replaces it. The layout is the original's; a mod file of
another size goes past the ROM's end, the FAT patched to match. Gaps read as
0xff, as in this game's ROM. Three ranges the extraction does not keep
(0x200-0x4000, between the ARM9 binary and the overlay table, the banner) stop
the process when read.

The backup memory (the ARM7's tag 11, `CARDi_Request`: INIT, IDENTIFY, read,
write, program, verify on the shared `CARDiCommandArg`) lives in memory, loaded
from `saves/khdays.sav` and written back whole and atomically when a save ends
(`khdays_backup_commit`); a failed commit is the game's failed save. The game's
slots go through the port's view of them (`saveslots.h`).

The game's language can change while it runs (`runtime/core/language.c`). The
language is `gLangPath`'s first halfword (1-5), read each time a path holding
'&' is opened. A scene loads its localized files when it is built and keeps
them, so a switch waits for the scene on show to end: every scene is built in
one language. Message databases still loaded keep theirs until let go.
`KHDAYS_LANGUAGE_SWITCH=frame:language[,...]` (test runs only) requests switches
by itself.

## The HAL's translations

`port/hal` holds NitroSDK's and CodeWarrior's routines the decompilation keeps
as ARM assembly, translated to C with the same results:

- `sdk_runtime.c`: CodeWarrior's ARM runtime helpers (`libs/msl`). The 32-bit
  divisions return the quotient in r0 and the remainder in r1; natively both
  come back as one 64-bit value (the quotient in the low half), which serves
  callers that declared an int and callers that read both.
- `sdk_dgt.c`: DGT's SHA-1 block transform (0x0200bd4c), reached through the
  DGT library's known-answer table. Each 64-byte block, its words read
  big-endian, is folded into the five-word state with SHA-1's 80 rounds, the
  constants those held in the 0x14 bytes before the routine; the length is
  checked after each block, signed, so at least one block is hashed.
- `sdk_util.c`: `MATH_QSort` (0x0200fc00), called from `Ov003_RankPlayers`: a
  quicksort without recursion on a stack of ranges (the caller's, else 32 -
  clz(num) ranges of its own); a range of two is one comparison; a longer one
  takes as its pivot the element (right - left) >> (32 - clz(width)) from its
  left, swaps it to the left and is partitioned around it, the larger part
  stacked first; elements swapped a word at a time when the width is a multiple
  of 4, else a byte; the comparisons signed.
- `sdk_mobiclip.c`: ov024's MobiClip blit (0x02086004). Its view (+0x00 to
  +0x18): the luma plane, the chroma plane, the destination, its stride, the
  width, the height and the ramp table. Each 2x2 luma quad and its chroma pair
  (Co, and Cg 128 bytes further in the same 256-byte row) become four 15-bit
  pixels through one saturating ramp indexed signed (the table + 0x100): green
  at Cg, red at Co - Cg, blue at red - 2 Co, the quad's anti-diagonal pixels
  four levels lower (the checkerboard dither). A word holds two pixels,
  0x8000 | R + (G << 5) + (B << 10) each; a pass does eight quads and two rows,
  both counts tested after the step, signed.
- `sdk_misc.c`: the RTC busy-wait, and crt0, whose work the native entry
  (`runtime/core/start.c`) does. MobiClip's kind-1 and kind-2 audio decoders
  and its deblocking filter, which the game never reaches, are not translated;
  a call to one stops the game (`BUILD.md`, "Code only the ROM has").
- `sdk_wireless.c`: the wireless library (ov105: WM and its helper `wh.c`),
  replaced: the 24 functions the game calls in it, under the decompilation's
  names, run on the port's network (`MULTIPLAYER.md`). Nothing of the DS's radio
  is left (no channels, beacons, MP cycle, 512-byte limit or ARM7). What the
  game reads back keeps its shape: the helper's states and errors, the two DTCM
  words the engine reads (`sMyAid`, `sConnectBitmap`), the records it is handed
  (a `WMBssDesc` per room, the parent's connect record, the port records). What
  `wh.c` did in a WM callback -- an interrupt on the DS -- is done where the port
  takes the game's interrupts (`khdays_wireless_deliver`), never inside the call
  that asked for it. The engine's port (12, `Session_Init_2`) puts a record into
  the sender's packet slot, which holds two of its largest frames (0xc0 for a
  guest's on the host, 0x3c0 for the host's on a guest), and reads the slot as a
  run of messages up to its length: records that waited go together, up to the
  slot's size.

## HD movies

When the game opens a movie the player has an HD video for
(`data/cutscenes/cinematics/hd<id>.mp4`) and the Video setting is `hd`, the window
shows that video with its sound (`host/movies/movie_hd.h`) while the game plays
its own movie unseen, silent and without the player's buttons; at its movie's
end the game is held until the HD video ends (`runtime/movies/movie_hd.c`), as
a set of states:

| From | To | When |
|---|---|---|
| Idle | Playing | a movie's stream opens and its HD video plays |
| Playing | Held | the game's movie ends first (the clock holds the game) |
| Playing | Finishing | the video ends first (by at most 1.13 s) |
| Playing, Held | Paused | START where the DS lets the movie be skipped: the video paused under the port's pause, the DS's scene pause (`host/screens/movie_pause.h`), the game held |
| Paused | Playing, Held | its Continue, or START (as it was; Held when the game's movie ended as it opened) |
| Paused | Skipping, Idle | its Skip: from Playing the video closed, START given to the game until it marks the skip; from Held the course ends |
| Finishing | Skipping | START where the DS lets the movie be skipped (the video over already: no pause) |
| Held | Idle | the video ends |
| Finishing, Skipping | Idle | the game's movie ends |

The credits scene is held at the end of its staff roll instead, and ends with
its video (which carries the roll too). The game's thread brings the movie's
events, the window's thread the video's and START while the game is held; one
lock keeps them in order. The course was designed from the decompilation and
the measured files.

## The controls the DS did not have

The gamepad's enhancements play the DS's own presses (`runtime/input`):

- **The command menu on the D-pad** (`sequencers.c`, pure C): the DS moves
  through the menu with presses that depend on Config option 6
  (`Ov022_UpdateCommandInput`) -- with 1, a press of X moves the cursor down;
  with 0, X pressed alone, then a direction with X held (Up and Down step, Left
  and Right change column). The sequencer turns a press of a menu direction into
  those presses, one game frame per step.
- **Lock-on on its own buttons**: the game locks on with R
  (`Ov022_ReadSelectionInput`): not locked, R twice within 9 game frames locks
  on to the target the game picked; locked, R twice lets go, R once moves to the
  next target, and with Type B (option 0 = 1) L once to the previous one. The first
  of two R while locked moves to the next target as well (Type A at the press, Type
  B at the release), so the lock-on button lets go with one R, its `repeatAny` set
  to 0 to count as the second (`khdays_controls_selection_repeat`). R also works
  the camera; the presses played are a player's, so the camera does what it does
  for them.

## The PC's side

**The window** (`host/app/window.c`) runs on SDL3 on a thread of its own. The
game's thread hands over finished frames and reads the input; both cross
between the threads through SDL's locks and atomics. The picture is drawn by
`gpu_present.c` (`RENDERING.md`). The keyboard, the mouse and the gamepads are
read by `devices.c`, which turns what each holds into the DS's buttons and the
port's actions through the player's control profile (`profile.c`, saved in
`saves/settings.ini`); the mouse on the lower screen is the stylus. Only one
gamepad plays: the first connected, or the last used when it goes away. Esc,
the gamepad's settings button or a click on the title's settings key opens the
settings menu, which pauses the game while open. Closing the window ends the
process.

**The settings menu** (`host/settings`) covers the whole window, the game's
picture dimmed behind it: the sections in a column on the left, the chosen
section's rows on the right, the chosen row's note and how to use the menu at
the bottom. It works with only a keyboard, only a gamepad or only a mouse. Dear
ImGui lays it out and draws it, but the menu reads the input itself: every SDL
event comes through `khdays_settings_handle`, which moves the focus and changes
the working profile; clicks are matched against the places the last drawing
recorded. The rows come from `settings_model.c`, rebuilt from the working
profile every time, so they always show what it holds. Its accept, clear and
back buttons are whichever face buttons the player's gamepad gives the DS's A,
Y and B, so the menu answers like the game does. While it is open the game is
paused by stopping the DS clock (`runtime/core/clock.c`); a game loop that
spins without waiting (the opening movie's) keeps one core busy meanwhile.

Its sample sounds are the game's own (`settings_sample.cpp`): the title's stream
`Title_BGM_PCM8` for music, Roxas's `rox_warai_S01` for voices, and the first
wave of `SE_SYS_WAVE` that plays once and lasts long enough for effects. They
are decoded straight from the archive's bytes, not played by the sound driver,
which belongs to the paused game; a stream plays at its record's volume, then
the master and its kind's volume, as the mixer combines them.

**The sound output** (`host/app/sound_output.c`). The sound thread wakes every
millisecond and runs the sound bridge up to the DS clock's current time, so the
ARM7's driver and the channels keep their own time whatever the game's thread
does. What they make goes into the SDL stream as soon as it is made: floats at
the device's rate from the PC's mixer (the default), or with `ds_exact` the DS's
mixer's 16-bit samples at 32,728 Hz, which the stream converts. The sound is
made as the DS clock passes but played on the device's clock, so the stream
keeps a lead of sound ahead, put in as silence when it opens, resumes or runs
dry (counted with `KHDAYS_PROFILE`); anything queued beyond a tenth of a second
(when the whole port stalled, then caught up) is dropped.

**The HD videos** (`host/movies/movie_hd_win.c`, Media Foundation). One video
plays at a time, on a thread of its own (COM multithreaded, `MFStartup`), with
two Source Readers on the file so each reads ahead at its own pace: one for the
picture, decoded to NV12 (the H.264 decoder's own format) and kept a little
ahead of the clock with its times; one for the sound, decoded to 32-bit floats
into an SDL audio stream of its own. The clock is the sound played so far (the
frames put in minus those still queued); without sound, or once it has all
played, the performance counter minus the time paused. At each screen refresh
the host takes the newest picture due on that clock.

**Execution contexts** (`host/app/context_win.c`): the switch is the port's own
code (`context_x64.asm`). Each context runs on a stack the port allocates, in
the game's memory when the runtime provides it (so the game's locals can be
reached by reference); the stack bounds Windows keeps for the thread are
switched with it, as for a fiber.

**The player's name** (`khdays_host_player_name`), as the DS's firmware holds it
(at most `room` UTF-16 characters): `KHDAYS_NAME` (the walks'), else the settings'
`nickname`, else "Player" and four digits made from this machine; from player
2 on the slot's number is added; only printable ASCII is kept. Never the
system's user name: that can be a real name, and online anyone can see it.
