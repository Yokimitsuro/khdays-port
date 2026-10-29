# khdays-port

[![Discord](https://img.shields.io/badge/Discord-Join%20the%20community-5865F2?logo=discord&logoColor=white)](https://discord.gg/rVeTPhPbsx)

A native PC port of **Kingdom Hearts 358/2 Days**, built on
[`khdays-decomp`](https://github.com/Yokimitsuro/khdays-decomp).

The game is the decompilation's C, compiled for the PC; the port supplies what
the Nintendo DS hardware did underneath it -- graphics, sound, input, the card,
interrupts and the system calls of its SDK -- and, around it, what a PC version
wants: modern controls, rendering, resolutions, a single-screen mode and
replaceable content. This is how other decompilation-based ports work: the
game code is the original's, readable and moddable; the platform layer is new.

> [!IMPORTANT]
> This repository contains no ROM, game assets, audio, video, text, SDK files or
> game binaries. You need your own legally obtained copy of the game.

> [!WARNING]
> The port is a work in progress and may have bugs. Reports are very welcome,
> in the [issues](https://github.com/Yokimitsuro/khdays-port/issues) or on
> [Discord](https://discord.gg/rVeTPhPbsx): what happened and what you were
> doing when it did.

AI-assisted coding tools were used to help make this possible.

## Status

The whole game runs natively from the decompilation: the title and its menus,
Story Mode (opening movie, days, cutscenes, the field), Mission Mode (character
select, camp, missions, results), combat and magic, the shop, and a player's
save loaded, played and saved again. Crashes found while playing are fixed as
classes in the build's source preparation, and what they show about the C goes
back to the decompilation ([`docs/DECOMP_FINDINGS.md`](docs/DECOMP_FINDINGS.md)).

Today it builds for **Windows x64 (64-bit)**. It plays with the keyboard,
the mouse or a gamepad, with remappable controls and optional enhancements
(an analog camera among them). The picture is the port's own: the 2D
composed by the port on the graphics card from what the game asks for, and
the 3D's geometry answered by the port and drawn there too -- at up to eight
times the DS's resolution, in widescreen, and with HD textures, models and
2D graphics from mods; the DS's own engines remain only as the reference the
port is checked against. The sound is the port's own: the ARM7's sound
driver in C, mixed at the PC's rate, with its own volumes and every sound
replaceable from mods. So are the movies: decoded by the port, shown at full
colour, their sound at its own volume, each replaceable by a video from a
mod. No machine code of the DS runs: the port has no ARM interpreter, and
what the decompilation keeps as assembly and the game calls is C the port
wrote from it. The game's files reach it from the port as well: each
read copied at once from a mod's file or the extracted data, with no card in
between (the emulated card stays as the reference). The game's time is the
port's too: its frames, its tick and alarms on the PC's clock, its interrupts
taken where the game waits or unmasks them, nothing simulating the DS's timers
or its display; and the 3D is drawn at the monitor's refresh rate, between the
game's frames, which still come 30 a second as on the DS. The game's memory
is the port's: a range Windows places, with each DS region at its DS address
from its start, the game's pointers kept as references into it -- the DS's own
addresses hold nothing. The HUD, the menus and the game's text are drawn
anew by the port, each element placeable. Underneath, the game still sees a
DS -- two screens one above the other, and the ARM7's services, which the port
answers on the PC: the clock from the PC's, the save from its files.

On top of the game, a new edition from content the user provides: HD
textures, models and 2D graphics, new rooms, sounds and videos, a
single-screen mode, and Mission Mode's multiplayer.

Restored from what the game left unfinished: **Stop**, its sixth magic
(Stop, Stopra and Stopga; in Spanish Paro, Paro+ and Paro++). Days kept its
effect, its animations and its names in two languages, but no panels, no
words and no code that casts it; the port casts it with the game's own
pieces and makes the rest: enemies in its reach stand still for a time that
grows with its level, and the damage they take meanwhile lands at once as
it ends. Its panels show in the deck and the camp with an unused clock
picture of the game's, take Magic LV and Doublecast like the other magics,
are made at the moogle's shop (dearer than any other magic) and work in
multiplayer, where with friendly fire on Stop also holds the other players.
Its numbers, prices, recipes and words are in an open file,
[`port/runtime/battle/stop_magic.txt`](port/runtime/battle/stop_magic.txt).
It is on unless turned off in **Settings → General → Stop magic** (`[gameplay]
stop_magic = off`, read as the game starts): the game is then the DS's, and a
save's Stop panels stay in it, unseen, until it is on again. In a room every
player must have it the same.

Planned: the port on other systems -- Linux first, then, from one ARM64
build, macOS, Windows on ARM and the Nintendo Switch.

## Installing

On Windows, the installer from the releases page is all you need besides your
own copy of the game: it asks for your ROM (the European or North American
release of KINGDOM HEARTS 358/2 Days; it finds it by itself where KH Melon Mix keeps one in your
KINGDOM HEARTS HD 1.5+2.5 ReMIX folder), prepares the game's data from it, and
can also make the HD pack from your HD collections, add the port to Steam (PC
and Steam Deck), and install the mod studio. Nothing of the game comes with
it. The port tells you when a newer release is out, and its Download updates
your copy by itself: it fetches the new installer, checks it, closes the game,
updates it with your choices (Windows asks for the administrator's permission)
and opens it again. More: [`installer/README.md`](installer/README.md).

## Getting started

To build the port from source instead:

1. Check and extract your copy of the game (Python 3.11+):

   ```powershell
   py .\tools\verify_rom.py "E:\path\game.nds"
   py .\tools\extract_data.py "E:\path\game.nds"
   ```

   The data goes to `data/extracted/<hash>/`, which is never committed.

2. Build (Windows, Visual Studio 2026 with the C++ workload, CMake, Ninja,
   and for the preparation Python's `pycparser`):

   ```powershell
   py -m pip install pycparser
   py .\port\build.py --config Release
   ```

   `port/tools/prepare.py` generates the build's sources from the
   decompilation -- the game's C rewritten with every pointer a reference
   (`build/port/gen-x64`); the executable is
   `build/port/obj-release-x64/khdays-native.exe` (`--config Debug`:
   `build/port/obj-x64/`).

3. Run it from the repository root, so it finds `data/` and keeps the save in
   `saves/khdays.sav`:

   ```powershell
   .\build\port\obj-release-x64\khdays-native.exe 2> khdays.log
   ```

   The runtime reports to stderr; `2>` keeps it for a bug report. Started
   without the data (or with it incomplete), it says so in a message box in
   the system's language, with the folder it looked in.

   The save is the DS's backup memory as a file, in the same format as a DS
   save (a 64 KB raw backup), so a save from the DS loads. It is written whole
   each time the game saves -- a new file beside it, then put in its place --
   so a crash of the game or the PC while saving leaves the save as it was
   before, or as it is after, never half of one.

4. When something seems wrong -- music late, the game stopping for a moment
   -- the session log says what the game was doing: the Debug build writes one
   for every session in `saves/logs/` (Release: turn on **Session log** in the
   settings, or `KHDAYS_LOG=1`): its
   scenes, the files it read and how long they took, when each music was
   asked for and when it was heard, and every stretch over 50 ms without a
   frame. Press **F12** when you see it happen: the log gets a mark there,
   and what the DS's two screens show then is saved in `saves/marks/`
   (`mark_<n>_frame_<frame>.bmp`, with the upper screen's layer registers and
   the pause's pieces in the log). Send the log and the pictures with the bug
   report.

## Controls

By default the game is played as on the DS.

- **Keyboard and mouse:** W A S D = D-pad, left button = A, Space = B,
  C = X, right button = Y, left Shift or Left = L, middle button or Right =
  R, M or Enter = START, N = SELECT; in a field, with the command menu on the
  D-pad, the wheel moves the command menu up and down and Q and E left and
  right. Any key or mouse button (left, middle, right, the side buttons X1 and
  X2, the wheel) can be bound to any action in Settings -> Controls; a notch of
  the wheel is a short press. The pointer hides over the game while its window
  has the focus (the touch screen is not used).
- **Gamepad** (Xbox, PlayStation, Switch Pro, Steam Deck, or any controller
  your system recognises as a gamepad): connect it whenever you like, even in
  the middle of a battle. The buttons follow the DS's layout by *position*,
  whatever is printed on them: the right-hand face button is A, the bottom one
  B, the left one Y and the top one X; the shoulders are L and R; Start or
  Options is START; Select, Share or View is SELECT. The left stick moves like
  the D-pad (the DS's eight directions). With several gamepads connected, the
  first one plays.

**Settings.** Press **Esc**, the gamepad's **Guide / Home** button or **L3**
(click the left stick in; Steam and the Steam Deck keep Guide for
themselves), or click the title's settings key where the title is the
theater's. The menu covers the window, the
game dimmed and paused behind it (in a multiplayer session the game goes on).
Every section is on one screen -- Controls, Enhancements, Sticks and camera,
Video, Sound, Multiplayer, Language, General -- and works with the mouse
alone, the keyboard alone or a gamepad alone (its shoulders move between the
sections). A change takes effect when you make it: the picture behind the
menu follows the video settings, each volume plays a sample of its kind (the
game is heard at it when it resumes), and a new binding works from the first
press after the menu closes. The Controls section shows every action with its
two keys and two gamepad buttons side by side: choose a cell and press the new
input; the Kingdom Hearts II layout and the defaults of each section are there
too. A setting that waits for something says so beside it (the movies' choice
from the next movie; the nickname, online play and the server from the next
multiplayer session while one is open).

**Language.** The game and the settings menu follow your system's
language when the game has it (English, French, German, Italian or Spanish;
English otherwise). Settings → Language chooses one of the five instead: the
menu changes at once, the game's own text when the screen you are on ends (a
mission, a menu of the game, the title) -- a screen of the game loads its text
when it is built, so every one is built whole in one language.
`KHDAYS_LANGUAGE=es` (or `en`, `fr`, `de`, `it`) wins over both. Where the
screen names one of the game's Config options, it uses the game's own words
in that language.

Your choices are kept in `saves/settings.ini`, a text file you may also edit
by hand (`KHDAYS_SETTINGS=<path>` uses another file). If the port cannot read
it, it starts with the defaults and keeps the old file as `settings.ini.bad`.

**Enhancements** (Settings → Enhancements). The control enhancements below
are off by default. Each uses the game's own controls through the same parts
of the game:

- **Analog camera:** the right stick turns the field camera and tilts it, as
  far as the tilt of the stick, at up to the game's own turning speed
  (adjustable, and invertible, under Sticks and camera). On the DS the camera
  is turned by dragging on the touch screen, by Select's look view, or with
  L/R when the game's Config sets them to the camera.
- **Mouse camera:** moving the mouse turns and tilts the field camera, through
  the same part of the game as the analog camera, at a speed of its own
  (Sticks and camera -> Mouse camera speed) and with the camera's inversions.
  While you play the window holds the mouse; Esc frees it with the settings
  menu.
- **Command menu on the D-pad:** in a field, the gamepad's D-pad moves
  through the command deck instead of moving Roxas, by making the presses the
  DS itself uses (X, or X held with a direction); menus, dialogs and open
  panels keep the D-pad. Which directions work depends on the game's own
  Config option for the command deck: with X alone moving it (the new-game
  default), only Down can.
- **Analog movement:** Roxas moves in the exact direction of the left stick;
  pushed less than the run threshold (75 % by default) he walks, at half his
  speed by default, his steps slowed to match. Fully pushed he runs at the
  game's own speed, never faster.
- **Separate lock-on:** lock on (and off) and switch to the next target on
  buttons of their own, by making the presses the game uses: its lock-on is
  R pressed twice quickly, a single R moves to the next target once locked
  (with Controls: Type B, L tapped goes to the previous one). The game takes
  two R presses within 9 of its frames (0.3 s) as the double press that lets
  go, so switching quickly on the DS drops the lock: here the next target
  waits for that window instead, and a quick series of switches keeps the
  lock. On the DS a single R press also resets the camera and a long one
  switches the chase cam; the DS's R stays available for those. Out of the
  field -- menus, tutorials, the pause -- there is no lock-on, and these
  buttons are the DS's R (lock on, next target) and L (previous target), so
  they turn pages as R and L do there.
- **Manual camera keeps its tilt:** with Chase Camera set to Manual in the
  game's Config, walking no longer tilts the camera back to its default angle
  (its distance still comes back); the events' cameras and the lock-on keep
  the game's own.

The same menu has live switches for HD fonts, KH2 minimap presentation, HUD
sequences, the pause-menu target, and HUD/control during INFORMATION messages.
All but the HD fonts are off until the player turns them on. These switches
require the corresponding enabled mod assets and preserve native fallback when
an asset is unavailable. Mission-panel visibility and its gameplay
and pause anchors are saved here too. **UI padding**, off by default, removes
the outer inset from an active HD HUD in single-screen mode. Changes use
`saves/settings.ini`; they never rewrite mod JSON. See the
[HUD preference reference](docs/KH2_SEQUENCES.md#live-enhancements-preferences)
for defaults, precedence, and the INFORMATION transition exception.

The **Kingdom Hearts II layout** (Settings → Gamepad preset; Kingdom Hearts
III uses the same buttons for these actions) puts attack on the bottom face
button, jump on the right, guard on the left, DS X on the top, the shortcuts
on L1, lock-on on R1, the next target on R2, the camera's reset on R3 (the
DS's R), the command menu on the D-pad, the camera on the right stick and
SELECT on View / the touch pad, and switches on the enhancements it uses.
The keyboard is not changed.

## Picture

By default the picture is the DS's: each screen 256x192, drawn exactly as the
DS draws it, its pixels scaled up sharp to fill the window at 4:3. The port
draws it through the graphics card (Direct3D 12 or Vulkan, whichever the
system offers), and needs one: without, it says why in a message box and its
log, and does not start.

**Settings → Video** changes it; the picture behind the screen follows at
once:

- **Display mode**: a window (the default) you can move and resize, a
  borderless window over the whole screen, or full screen (the screen for the
  game alone, at your desktop's resolution). **Alt+Enter** switches between
  the window and the full screen you chose last (the borderless window if
  none) while the settings menu is closed, and keeps it.
- **Internal resolution** (1x, 2x, 3x, 4x, 6x, 8x, or the window's own
  size): the 3D -- the field, the characters, the effects -- drawn at
  that many times the DS's pixels. "The window's" draws at the next whole
  multiple up and shrinks it on the graphics card to the window's own pixels
  (with sharp scaling, the largest that fits, its pixels square); a picture
  drawn larger than the window is always shrunk so, each pixel the average of
  what is under it, never stretched. The 2D -- the HUD, the menus, the text,
  the lower screen -- stays the DS's pixels, as the game drew them, unless a
  mod replaces it (below). It is the
  same picture, only sharper: the 3D follows the same rules as at 1x, and
  wherever the 2D covers or blends with it, it does so as on the DS.
- **Antialiasing** (off, 2x, 3x, 4x): the picture made that many times
  larger, then shrunk to the size shown, each pixel the average of what is
  under it -- the 3D's edges come out smooth instead of in steps. 2x is four times the graphics
  card's work, 4x sixteen; off by default, and the settings warn while it is on, as the internal
  resolution already draws the 3D at the window's size or larger. (A mod's textures, and the HD
  replacements, are read at the size they are drawn whatever this says:
  smaller copies of each are kept, so a model far away does not break into
  steps that flicker.)
- **Aspect ratio** (4:3, 16:9, 16:10, or the window's): the 3D view of the
  upper screen widens to it, and the field's HUD keeps to the corners of the
  wider picture as it does on the DS -- the command deck bottom left, the
  portrait and gauge bottom right, the hearts counter top left. What the game
  centres (dialogs, menus) and what follows the 3D (markers over targets)
  stays where it was, and the lower screen stays 4:3 in the middle. The game
  sees nothing different: it still computes
  its 4:3 view.
- **Screen scaling**: sharp (the DS's pixels as squares) or smooth.
- **3D texture smoothing**: off as on the DS, or blended between texels.
- **HD graphics**: the replacements in your mods (below), on by default.
- **Dump graphics**: each texture and 2D tile sheet written out the first
  time it is drawn, into `saves/dump/graphics`.
- **Movies**: HD, the default -- the HD collection's own videos where you
  have copied them (below), the other movies at full colour -- at full colour
  (the decoded frames as they are), or as on the DS (15-bit colour with its
  checkerboard dither).
- **Picture pace**: smooth, the default -- the 3D drawn at your monitor's
  refresh rate (60, 120, 144, 165 Hz...), placed between the game's last two
  frames (the game itself runs at 30 frames a second, as on the DS), the
  picture up to one game frame behind -- or as on the DS, a new picture each
  game frame. Camera cuts and scene changes are not smoothed across. The 2D
  (HUD, menus, text) and the movies change when the game changes them.
- **Show FPS**: a counter in the window's corner -- the pictures drawn a
  second and the game's own frames a second.
- **Graphics API**: what draws the picture -- automatic (Direct3D 12 on
  Windows, Vulkan on a Steam Deck or under Proton), Direct3D 12 or Vulkan,
  changed at once. If
  the graphics card stops answering on Direct3D 12, try Vulkan. Should the
  card stop answering anyway (Windows resets its driver), the game carries on:
  the picture is made again, and an internal resolution lost twice drops to
  1x for the session.

The choices are kept in the `[video]` section of `saves/settings.ini`:

```ini
[video]
internal_resolution = window   # the window's size (the default), or a multiple: 2 to 16 in the settings
antialiasing = off        # off, 2x, 3x or 4x: made that many times larger and shrunk
aspect = 16:9             # 4:3 (the DS's), 16:9, 16:10, or window
scaling = sharp           # sharp (the DS's) or smooth
texture_smoothing = off   # off (the DS's) or on
hd_graphics = on          # the mods' replacements
dump_graphics = off       # write what is drawn to saves/dump/graphics
movies = hd               # hd (your HD videos, the rest pc), pc (full colour) or ds (the DS's)
picture_pace = smooth     # smooth (the monitor's rate) or ds
show_fps = off            # the live counter
backend = auto            # auto, direct3d12 or vulkan
```

**The field on one screen.** With one screen (`[video] screens = one`, the
default) the field's HUD is laid out for a PC screen, made of the game's own
pieces: the mission's objective and gauge at the top left, its badges and
chests counted at the right of the objective, the hearts counter under it;
in the top right corner the map, drawn over the field as in Kingdom Hearts
II, with the enemy's health over it; the command deck at the bottom left and
the party at the bottom right. The panels' dark ground is half see-through,
their text and edges are not. The INFORMATION banner shows at the top left,
over everything, for five seconds each time its message changes, instead of
staying on screen; at a mission's start the mission's panel and map show at
once, before the game brings in the rest of its HUD.

**Badges on the mission panel.** In a story mission the lower screen's
mission panel shows, left of the chest icon, the mission's badges with the
camp's mission list's own icons -- the ordeal badge and the blazon, then the
unity badge -- grey until found, lit once your save holds them or you pick
one up; on one screen it shows how many are found of how many. The
DS shows them only in the list. Mission Mode puts out no badges, so it shows
none.

**The players' names in multiplayer.** In a Mission Mode session the field's
party shows each player's name left of their J label, in white in the game's
own font -- yours by your bar, the others' by theirs. The DS shows only the
labels.

**The title as the HD collection's theater menu.** If you have KINGDOM HEARTS
HD 1.5+2.5 ReMIX, `tools/hd/theater_title.py` makes a mod from your copy
(nothing of the collection is in the port) that shows the title's menu, on
one screen, as the collection's 358/2 Days theater draws its main menu -- its
logo, key art, bars and settings key, animated as there -- with the game's
own options in its font, in the five languages. It reads the theater's
files as OpenKh's extractor writes them (`Image/Mare.hed_out/original`):

```bash
python tools/hd/theater_title.py "<collection>/Image/Mare.hed_out/original"
```

The pictures go to `mods/Theater title/layouts/title/`; the screens after the
menu (the save list, the theater's list; the difficulty, unless the new game's
dialogs below are made) are the game's. Its
settings key opens the settings menu when clicked and, while you play with a
gamepad, shows your gamepad's button
for the settings in the collection's own icons (the left stick's click by
default: the collection has no icon for the Guide button). The boot logos are the
theater's too: its Disney, SQUARE ENIX and copyright screens in the DS's three
turns.

**The new game's dialogs as in Dream Drop Distance HD.** If you also have
KINGDOM HEARTS HD 2.8 Final Chapter Prologue, `tools/hd/newgame_dialog.py`
adds to the same mod the new game's steps as Dream Drop Distance HD draws its
own -- the game's difficulty, then the port's question whether the movies show
their subtitles, then the review of both ("Continue with these settings?") --
in Days's colours (greys, the selected button red; `--colours ddd` keeps DDD's
blue), with DDD's words in its font, in the five languages, over the theater's
menu. It reads DDD's files as OpenKh's extractor writes them
(`Image/dt/kh3d_first.hed_out`), the theater's as above, and the pointing
glove of KINGDOM HEARTS II's title sheet (DDD's own is not among those files;
OpenKh's KH2 data: `remastered/menu/fm/title.2ld/UK_title_2ld0.png`):

```bash
python tools/hd/newgame_dialog.py "<2.8>/Image/dt/kh3d_first.hed_out" "<collection>/Image/Mare.hed_out/original" "<kh2>/remastered/menu/fm/title.2ld/UK_title_2ld0.png"
```

**The settings menu as the HD collection's.** `tools/hd/theater_settings.py`
makes, from the same copy, the collection's own settings menu's pieces in the
new game's dialogs' tones -- its screen and heading's tab, a faded picture for
each section, the sections' tabs, the rows' capsules, a switch's check, a
number's slider, a choice's arrows, the question's window, the hints' bar --
and its font; the port's settings menu draws itself with them (its rows, keys
and mouse unchanged), the selected pieces red with a silver rim:

```bash
python tools/hd/theater_settings.py "<collection>/Image/SettingMenu.hed_out/original"
```

They go to `mods/Theater title/layouts/settings/<lang>/`; without them the
menu keeps its own look.

**The save list as Dream Drop Distance HD's, with 99 files.**
`tools/hd/save_screen.py` makes, from DDD's files (as above) and the
port's extracted game data, the pieces of DDD's own save screen in Days's
tones -- its screen, the list's rows (the selected one red), the scrollbar, the
crown, the questions' buttons -- with DDD's font and input icons, Days's
three faces and the theater's hand (the 1.5+2.5 collection's, as below):

```bash
python tools/hd/save_screen.py "<2.8>/Image/dt/kh3d_first.hed_out" "<collection>/Image/Mare.hed_out/original"
```

They go to `mods/Theater title/layouts/save/<lang>/`. With them, the lists of
files are the port's -- CONTINUE's and Mission Mode's on the title, and Save
in the story's and Mission Mode's camp menus: 99 files, each row with Days's
face and its day, level, munny, difficulty and time, the game's own questions
and words, and the keys shown as the icons of what you have for them (an Xbox
pad's, a PlayStation's, a Switch's for other pads, the keyboard's keys).
Files 1-3 are the DS's three, in `saves/khdays.sav` as always; files 4-99 go
to `saves/slots/slotNN.sav`. The save after the story's ending still shows the
DS's three.

Without those pieces the game's own lists reach the 99 files as well, in pages
of three: past the first or last row the list moves on, each row numbered, file
99 tagged AUTO (the autosave's), the game's questions naming the file. In
`saves/settings.ini`, `[save] files = 3` keeps the DS's three.

**Autosave.** After each cleared mission -- in the story, in Mission Mode alone
or with friends -- the game saves itself the first time it is back at a camp's
menu (Mission Mode's after the results, the story's that opens after a
mission), with the camp's own Save, into file 99 by default; a corner line in
the game's font says it was saved, or that it could not be. In the settings
menu's General page, "Autosave" chooses file 99, the file being played (a game
never saved: the first empty file) or off (`[save] autosave = file99 | played
| off`), and "Files in the game's lists" the 99 or the DS's three. A mission
given up or lost writes nothing, and Mission Mode's guest game, which has no
file, is never saved.

**THEATER MODE as the HD collection's theater.** `tools/hd/theater_cine.py`
makes, from the same copy of the collection and the port's extracted game
data, the theater's chapter list -- its heading, its rows (the selected one
red, the glove), its DAY and digits, its scrollbar and help bar, its font and
button and key icons -- and the artwork of each scene's chapter:

```bash
python tools/hd/theater_cine.py "<collection>/Image/Mare.hed_out/original"
```

They go to `mods/Theater title/layouts/cine/` (`<lang>/` and `art/`). With
them THEATER MODE's list is drawn as the theater draws its chapters. With
the Movies setting on HD and the theater's videos made (*HD scenes* above)
the list is the theater's own: its 53 chapters with videos, each with its
name, its length and its day when it is a day's first, the artwork of the
selected one behind; UP and DOWN move a row, LEFT and RIGHT a page, A plays
the chapter's videos in a row with their subtitles (START stops them), B
leaves as the game's list does. Otherwise the list is the game's 28 movies,
ten to a page, its own list running underneath (its keys, pages and
movies).

**Mission Mode's rooms and character select on one screen.**
`tools/hd/theater_mission.py` makes, from the same copy, the faces of the
game's nineteen characters as the theater has them (in colour and in grey)
and its hooded figure, the rows, scroll bar and help bar of its chapter list,
its veil, one of its artworks, its font and its icons:

```bash
python tools/hd/theater_mission.py "<collection>/Image/Mare.hed_out/original"
```

They go to `mods/Theater title/layouts/select/` and `layouts/rooms/`. With
them the list of rooms and a room (below, Multiplayer) are drawn as the
theater's chapters, and the character select, alone or in a group, is one
screen: the character you are on large,
with its name, weapon and gauges; the characters you can choose (the ones not
bought are not there, as in the game) in rows; each player's place along the
bottom, in its colour and label (J1-J4), ringed once it has chosen. The
directions move on that grid; A, B and START are the game's.

**The camp menus on one screen.** `tools/hd/ddd_camp.py` makes,
from your copy of KINGDOM HEARTS HD 2.8 Final Chapter Prologue (Dream Drop
Distance's camp menu) and the theater of the 1.5+2.5 collection, Mission Mode's
and the story's camp menus as one screen in DDD's pieces in Days's greys:

```bash
python tools/hd/ddd_camp.py "<2.8>/Image/dt/kh3d_first.hed_out" "<collection>/Image/Mare.hed_out/original" "<collection>/Image/SettingMenu.hed_out/original"
```

They go to `mods/Theater title/layouts/camp/<lang>/`. With them the camp's
menu shows its options in DDD's command bars (the selected one lit), the
character's own 3D model large in a grey box where DDD's portrait frame goes
(cut at its edge, as the DS's frame cuts it), its face, name and
numbers on the right, the trials with the DS's sigil and DDD's crown, the munny and
heart points in DDD's digits, the option's description and the settings key,
the hand at the option selected, the game mode (the story's difficulty, or
Mission Mode) on the panel, Title Screen's question with its two answers, and the
Preferences page in the menu -- all the game's settings in sections, in the
settings menu's tabs and rows, L and R between the sections; in the
story's, its day on the box;
in a group, a plate for each player with its face and where it is (an option,
the menu, or ready -- ringed in its colour -- once it accepted the mission).
The game's menu runs underneath (its cursor, sounds and pages); its other pages
(the holo-missions, the panels, the shop, ...) are still the game's screens.

**Retry.** In a Mission Mode mission or a story challenge played alone, the pause has a third line
between Continue and Give Up: Retry, drawn like the game's lines in its own
font. After the game's "Are you sure?", the mission starts again from its
beginning, without the results screen (`KHDAYS_UI=pause=off` gives the game's
two lines back).

**The pause in HD.** A mod can draw every pause (Mission Mode's, a scene's with
Skip, "Are you sure?", and Retry) in its own pictures. From your own pictures
of the KH games' pause (its title's bars, a line's capsule plain and lit, the
hand) and the free fonts of Televo's *Kingdom Hearts Re:Collection*
(KHGummi for the title, KHMenu for the lines), in the game's words in each
language:

```bash
python tools/hd/hd_pause.py <your pictures> "<Re:Collection>/Fonts" --mod "mods/HD HUD"
```

They go to `mods/HD HUD/layouts/pause/` (see the tool for the pictures' names).

**Subtitles.** `[video] subtitles = on|off` (Video in the settings menu,
and the new game's question where those dialogs show): off, the game's movies
show without the subtitles the DS draws over them. With `movies = hd`, the HD
videos show the theater's own subtitles when it is on -- in its event font, in
your language -- if you made them from your copy; `tools/hd/theater_movies.py`
also copies the theater's videos, which carry no subtitles in the picture (a
video with them drawn in: leave subtitles off). A video you already have is
kept unless `--force`:

```bash
python tools/hd/theater_movies.py "<collection>/Image/Mare.hed_out/original"
```

The videos go to `data/cutscenes/cinematics/hd<id>.mp4` (the opening's from
the theater's `DOP.mp4`; the ending's, 843, is not among them), the subtitles
to `data/cutscenes/cinematics/subtitles/<lang>/hd<id>.png` and `.txt`; a mod's
`movies/subtitles/<lang>/hd<id>.txt` comes first. The music the theater plays
over them goes to `data/cutscenes/cinematics/music/` (each video's schedule,
`hd<id>.txt`, and its tracks, `bgm_<n>.ogg`); a video of a mod has music only
from a `music/` folder beside it.

**HD graphics from mods.** A mod replaces any of the game's graphics, at
any resolution, with a file named after it:

```text
mods/<Mod>/graphics/<file>[#<entry>]/
├── tex/<texture>[@<palette>].png  # a 3D texture, any size (with any palette: <texture>.png)
├── chr/<n>.png                    # a 2D tile sheet, a whole multiple of its size
└── mdl/<model>.gltf               # a model -- or .glb, or .fbx (<model>.bones.txt beside it pairs its joints)
```

`<file>` is the game file the graphic is loaded from and `#<entry>` its
entry in a pack. A texture is drawn with the original's mapping at the
PNG's resolution. A 2D tile sheet's PNG holds the sheet's tiles in its rows:
indexed, its pixels are the game's palette indices and it is drawn with the
palettes the game draws the tiles with, so its fades and palette effects
stay; in true colour, with its own colours. Only tiles the game has not
drawn over itself are replaced, so the game's text stays its own. A model
keeps its own skeleton, weights, polygons and textures and is moved by the
game's animations: its joints named like the original's nodes follow them,
`<model>.bones.txt` pairs the others (a line `<joint> <node>`; `scale <k>`
sizes the model), the rest move with their parents, and a rest pose far from
the game's (a T-pose) is turned to match. A model may be glTF (`.gltf`, or
`.glb` with its textures inside) or FBX (`.fbx`, binary or ASCII), taken in
that order when more than one is there; an FBX in centimetres or with Z up is
converted to glTF's metres and Y up, so the same model measures the same in
either. Its textures are PNG, JPEG, TGA or BMP, embedded or beside it -- one
the file stores under another machine's path is found by its name beside the
model -- and one that is missing is drawn white and named once in the log.
The model's own animations are not used: the game's move it. While a mod's graphics are drawn,
the 3D and the 2D's blending and brightness work in 8 bits a channel instead
of the DS's 6 and 5. A file that cannot be used is named once in the log,
and the original is drawn.

**The originals** to make them from: `khdays-native --dump-graphics [DIR]`
writes, without starting the game, every texture (with each palette it is
drawn with), every model as glTF with its skeleton, and every 2D tile sheet
as an indexed PNG, under those names (default `saves/dump/graphics`). A
dumped 2D tile sheet put back unchanged is drawn as the original.

## Sound

The port plays the game's sound itself: the DS's sound driver, which the
game's ARM7 binary carries, written in C and run on a thread of its own, so
loading and busy frames no longer cut it; the music streams read by the
port as the game's stream player would; and the channels mixed at the PC's
output rate in floating point, without the DS's 10 bits, each read between its
samples where the DS holds each one until the next (DS-exact sound keeps the
DS's way), and a mix louder than full scale turned down for a moment rather
than clipped.

**Settings → Sound** sets the overall volume and those of the music, the
effects, the voices (what each sound is comes from the name of the
player the game's sound archive gives it) and the movies, kept in
`saves/settings.ini`:

```ini
[sound]
volume = 100    # 0 to 100, each
music = 100
effects = 100
voices = 100
movies = 100
ds_exact = off  # on: the DS's own output (32,728 Hz, 10 bits)
```

**Replacing sounds.** A mod replaces any sound with a WAV, FLAC or Ogg
Vorbis file at any rate, mono or stereo, named after the sound in the game's
sound archive:

```text
mods/<Mod>/sounds/
├── seq/<sequence>.<ext>              # a whole sequence (music)
├── seqarc/<archive>/<entry>.<ext>    # one effect or voice of a sequence archive
├── wave/<wave archive>/<index>.<ext> # one wave an instrument plays
└── strm/<stream>.<ext>               # a stream (music, voices)
```

The loop is a WAV's `smpl` loop, else the Vorbis comments `LOOPSTART` and
`LOOPLENGTH`, else the original's. The original keeps running unheard, so
the game waits for it and reads it as before; a file that cannot be read is
named once and the original plays. A file plays at the PC's output rate,
between its own samples, a wave's too (not at the rate of the DS channel
that plays it); one above 48,000 Hz is brought down to it as it loads,
filtered; one longer than its original plays on to its end once the original
ends by itself (not when the game stops it); the voices' streams are decoded
ahead, so even their first line is the mod's. Best: 48,000 Hz, 16 bits, about
as loud as the original (a dumped one shows how loud).

**The originals** to make them from: `khdays-native --dump-sounds [DIR]`
writes every wave, stream, sequence and sequence-archive entry as WAV under
those same names, loops included (default `saves/dump/sounds`, about
2.3 GB). It runs without starting the game; the sequences are played by the
port's driver and mixer at 48,000 Hz. A dumped file put back unchanged plays
as the original.

**HD movies.** If you own KINGDOM HEARTS HD 1.5+2.5 ReMIX, the HD videos of
this game's movies its 358/2 Days theater holds can play in their place: make
them from your copy with `tools/hd/theater_movies.py --videos` (see
*Subtitles* below), which writes them to the port's
`data/cutscenes/cinematics/` (`hd802.mp4` to `hd848.mp4`). They are yours:
nothing of them is in this repository, and `data/` is never committed. With the Movies setting on HD, a movie that has one shows it over
the whole window with its own sound, in both screen layouts; the game plays
its own movie behind it, unseen, and waits at its end until the video ends.
The video comes up from black and silence over its first half second and goes
down to them over its last, so a part cut out of a longer video (hd833's
first, below) ends without a cut. Where the DS lets you skip the movie, START pauses the video under the
DS's own scene pause (PAUSE, Continue, Skip), drawn from the game's pieces:
the game has no pause over its movies, but the HD scenes have the game's, so
the movies have the same. Windows decodes
the videos (Media Foundation); a file it cannot play, or a movie without an
HD video, plays the game's movie, and the log says why. A mod's movie for the
same movie comes first. The theater's subtitles show over them (see
*Subtitles*), and its music where the theater plays some (see *HD scenes*).

**HD scenes.** The theater also holds the game's event scenes the DS draws in
real time, made again in HD, and Roxas's monologues as a voice. The same tool
makes the ones the port plays: the scene map, `port/movies/hd_scenes.txt`
(95 scenes and 7 monologues, each paired by its lines or its pictures, every
pair checked side by side; `tools/hd/scene_report.py` says where each video
of your collection plays). With the Movies setting on HD, when the game
reaches a scene of the map its videos play over the whole window with their
sound and subtitles while the game waits; then the game takes its own skip,
so it goes on exactly as after the DS's Skip. Nothing is cut: the game's
picture and sound go down to black and silence over half a second before the
videos, the videos come up from them and go down to them at their end, and
the game comes up again after them. A day's monologue keeps the
game's page, and the theater's voice reads it. Without the video, or with
Movies on PC or DS, the DS's scene plays. START pauses the video and opens
the game's own pause over it, as on the DS: Continue goes on with the video,
Skip ends the scene (on two screens the pause shows over the game's screens).
Three of the theater's movies go on past the DS's: hd830 and hd836 also hold
the event scene that follows them (Riku and Mickey; Riku and Roxas before
their fight), so after them that scene is skipped (`skip`), and hd833 also
shows the fight against Xion the player fights next, so it is split along the
fight: it stops as the screen fades to black, the game's own scenes take
Roxas through Wonderland, Neverland and Agrabah, the scene that brings him
back to Twilight Town shows its next part (Roxas drawing his Keyblade and
leaping into the fight), and the rest of it, the fight to its end, plays once
the player has won, before hd848 (`mv#833 hd833[:128.4]`, `EV_S#182
hd833[129.4:173.2]`, `EV_S#183 hd833[173.2:]`).
A mod's `cutscenes/scenes.txt` adds or replaces entries (`<script> <videos>
; <evidence>`, `off` for the DS's scene, `skip <video>` for a scene that
video shows; `mv#<movie> <videos>`, or `off`, for a movie; a video may be a
part of it, `<video>[<from>:<until>]` in seconds), and its
`cutscenes/cinematics/<video>.mp4` comes before your own.

Most of the theater's videos, scenes and movies alike, hold the voices and
effects alone: the theater plays its music over them, each video with its own
cues and volume (hd848, Xion's end, among them). The same tool takes the music
and those schedules from your copy, and the port plays them with the video,
at the Music volume, paused with it and on from one video into the next where
the theater goes on.

**Replacing movies.** A mod replaces any of the game's 46 movies with an
MPEG-1 file of any size, with or without MPEG-1 Layer II sound:
`mods/<Mod>/movies/<id>.mpg`, `<id>` the movie's number in the game's `mv/`
folder (`802` is the opening). The game plays its movie as ever -- START
skips it, it ends when the game says, its subtitles show over it -- and the
port shows the mod's picture in its place, at the game's movie time and in
its shape (the rest black), and plays the mod's sound at the movies' volume
instead of the game's. A file the port cannot read: the game's movie plays,
and the log says why. Made from a copy you own, for example:

```text
ffmpeg -i source.mp4 -c:v mpeg1video -q:v 2 -c:a mp2 -b:a 256k -f mpeg mods/MyMod/movies/802.mpg
```

## Mods that change the game

Besides content, a mod can carry Lua scripts that change how the game plays: they hear what
happens (each frame, scenes, missions, HP, items, munny, the menus, saving), read and change the
game's state by name (munny, items, flags, the player's HP and position), add their own options
to the settings menu's Mods page, and ask the game to act (items, sounds, the field's notice). For
what the names do not cover, a script reads the game's memory by address, as the decompilation
describes it, and writes it if its mod declares so (the Mods page tells the player). Each
runs in its own sandbox: an error or an endless loop stops that mod only, and scripts reach no file
outside their own folder, nor any memory but the game's. `docs/SCRIPTING.md` describes it all; `port/mods_examples/` (installed
as `mods-examples/`) has examples to copy into `mods/`; khdays-studio's Console shows what the
mods print and reloads them while the game runs.

For what scripts cannot do, a mod can carry a native plugin: C++ built for the player's computer
against the KH Mod SDK (`sdk/`), loaded only once the player has turned native code on (the
settings menu's Mods page, read at the next start; it runs inside the game, unisolated).
`docs/MODDING_NATIVE.md` describes it. **Examples:** installed with KH Studio (the installer's
choice), `mods-examples/` has five mods with a native plugin ready to copy into `mods/`, their
plugins built -- `HelloWorld`, `CustomCharacter` (a character of its own), `CustomLimit` (a
character with a Limit of its own), `CustomEnemy` (Shadows that blink away and split) and
`AdvancedNativeMod` (hooks at the game's routines) -- and `sdk/` the KH Mod SDK to build your own
(`sdk/examples/` in the repository).

## Multiplayer

Mission Mode's multiplayer works over the PC's network: the game's own
menus (Create a Group, the group list, the character select, the camp, the
mission, the review), on the port's own networking -- nothing of the DS's
radio is involved. Answer Yes to "Begin network play?".

The game's words about the DS's hardware are the port's for a PC, in its
five languages: network play instead of DS Wireless Communications, "do not
close the game" instead of the power switch and the Game Card. A mod that
words those texts itself keeps its own.

- **Two players on one PC.** Start the port twice. The second copy is
  player 2: it uses `saves/player2/` for its save, settings (copied from
  yours the first time) and logs, and the second gamepad connected; each
  player keeps their gamepad when the other window has the focus. To play
  your own character as player 2, copy a save into `saves/player2/` (Guest
  Play needs none). A third and fourth copy are players 3 and 4.
- **One local network.** Groups made on the other PCs appear in the list by
  themselves (built for it; not yet tried between two PCs). The first time,
  Windows asks whether the port may use the network: allow it on private
  networks, or the others cannot join you (copies on the same PC need
  nothing).
- **Over the internet.** With *Online play* on (settings, Multiplayer; on by
  default), the group list also shows the groups of players anywhere, met
  through a matchmaking server -- the maintainer's by default, or any other
  `khdays-server` you write in *Server*. Nobody opens a port on their
  router: the games only connect outward, straight to each other when
  their networks allow it and through the server when they do not. Tried
  over the internet: two copies through the maintainer's server played a
  mission to its review.
- **Your own server.** `khdays-server.exe` sits next to the game: open it,
  and its window says the address the players write as the server. On a
  home PC it asks the router to open its UDP port 35821 by itself
  (NAT-PMP or UPnP), and says so when the router or the provider (a shared
  internet address) will not; on a Linux server, `port/server/README.md`
  says how to run it as a service.
- **Your name** in the group list is your *Nickname* (settings,
  Multiplayer; 10 characters, as the DS's), or else an automatic one
  ("Player" and four digits) -- never your Windows user name.
- **The list of rooms.** With the theater's pieces made (`theater_mission.py`,
  above), the group list and the group's members are the port's: Create a
  Group and a search by name always first, then every room there is (not
  the DS's four), each with its name, its host, its players and its ping --
  through the server, the round trip a session with it would take, or
  straight to the host on the local network -- as four signal bars and in
  milliseconds. Create a Group asks your room's name (your nickname by
  default) and an optional password. A password is never sent nor seen by
  the server: the others must know it (tell them), and their game proves it
  to yours. Inside the room, a card for each of the four places: its player,
  "Host" and "You", the ping; and the host's Proceed once two players are
  in. The game's own list underneath does the joining, as before; the
  server needs nothing new.
  Without those pieces the game's own group list does the same in its own
  look: its four rows go through every room (past the last one the list
  moves on), each with the room's name, its players, a lock when it asks for
  a password and the game's signal icon by its ping; the search on the
  first row; Create a Group asks the name and password on the same rows
  (A twice creates the room as before); a wrong password is said on the
  upper screen. Its members' screen adds the room's name and each one's
  ping.

The connection is the PC's: what the network delays the game waits for, and
nothing more -- a datagram lost is made good by the next one. Leaving or
losing a player ends the session for everyone, as on the DS (the game shows
its connection error).

Quicker than the DS's:

- **A guest's own character answers at once.** On the DS a guest's every
  action goes to the host and comes back before it shows; the port runs the
  guest's own character on the guest's PC as the host will, and the host's
  word still decides (a hit the host saw is shown as the host has it). A
  guest's attack -- and its shot's projectile -- shows as soon as the host's
  own does, whatever the round trip; what it does to the others -- a hit on
  an enemy or an ally -- still comes from the host.
- **Three and four players at full pace.** The DS slowed the battle to two
  thirds with three or more players; the port keeps it at the pace of two.
- **No press lost** when the network bunches two of a guest's controls into
  one of the host's ticks.
- **The fourth player on the HUD.** With four players the single-screen
  picture shows every ally's row whole.

Where the game restarts the console -- leaving Mission Mode for the title,
the connection error -- the port boots it again in the same window.

## Repository

| Path | What it is |
|---|---|
| `port/` | The port. `build.py` builds it; `tools/prepare.py` turns the decompilation's C into the game's PC sources |
| `port/runtime/` | The game's side: what stands in for the DS while the game runs. `core/` memory, I/O registers, events, clock, ARM7, threads, overlays, ROM and files, logs; `input/` the controls as the game reads them; `display/` each frame handed to the window; `sound/` the game's sound driver; `net/` the multiplayer session; `menus/` the port's menus over the game's; `movies/` the game's movies and their HD or mod replacements; `mods/` textures, models and 2D sheets mods replace; `scripts/` gameplay mods' Lua scripts, each in its own sandbox |
| `port/host/` | The PC's side. `app/` window, devices, profile, audio out; `render/` the graphics card and the one-screen picture; `screens/` the port's own screens; `settings/` the settings menu; `net/` sockets; `movies/` video decoding |
| `port/hal/` | The NitroSDK's hardware layer, in C |
| `port/gfx/`, `port/gpu/` | The 2D's description and composition and the 3D's geometry; the DS's graphics engines in software, the reference the picture is checked against |
| `port/ui/` | The port's parts over the game's picture: text, HUD, pause, the one-screen situations and compositions |
| `port/sound/` | The DS's sound (the ARM7's driver) in C, and mods' sounds |
| `port/server/` | The multiplayer's matchmaking server |
| `port/shaders/` | The graphics card's shaders (HLSL), and `port/host/render/generated/` what `port/tools/build_shaders.py` makes of them (committed, so building needs no shader tools) |
| `port/tests/`, `port/fuzz/` | The port's tests (`ctest`) and fuzz targets |
| `port/tools/` | Build, regression and research tools |
| `studio/` | khdays-studio, the mod tool. `app/` its window, `app/views/` each view; `core/` what it does, by subject: `files/`, `mods/`, `models/`, `rooms/`, `missions/`, `texts/`, `sheets/` |
| `assets/` | The library the port and the studio share: the game's data formats decoded to neutral forms (textures, 2D graphics, fonts, text, models and animation, sound, video), the open formats mods use (PNG, BMP, glTF, FBX), the mod layer and the game filesystem; `tests/` its tests (`ctest`) |
| `installer/` | The Windows installer (Inno Setup) and the helper that runs its steps |
| `tools/` | ROM verification, data extraction and decompression; `hd/` the HD mods made from the player's HD collections; `tests/` their tests (`python -m unittest`) |
| `docs/` | [`BUILD.md`](docs/BUILD.md), how the port is built from the decompilation and tested; [`PLATFORM.md`](docs/PLATFORM.md), how it stands in for the DS; [`RENDERING.md`](docs/RENDERING.md), how it draws the game; [`SOUND.md`](docs/SOUND.md), how it plays its sound; [`MULTIPLAYER.md`](docs/MULTIPLAYER.md), its multiplayer and server; [`COMPOSITIONS.md`](docs/COMPOSITIONS.md), the one-screen layouts' format; [`HD_SCREENS.md`](docs/HD_SCREENS.md), the HD screens' files; [`MENU_OVERHAUL.md`](docs/MENU_OVERHAUL.md), the Menu Overhaul mod's screens; [`WALK_MOD.md`](docs/WALK_MOD.md), the Walk mod's analog walk; [`HD_FONTS.md`](docs/HD_FONTS.md), PNG font definitions and rendering; [`FONT_MAPPING.md`](docs/FONT_MAPPING.md), scoped font choices; [`DECOMP_FINDINGS.md`](docs/DECOMP_FINDINGS.md), what the decompilation's C should say differently; [`RUNTIME_MAP.md`](docs/RUNTIME_MAP.md), what the running game does (scenes, overlays, menus, data); [`FORMATS.md`](docs/FORMATS.md), the game's data formats; [`STUDIO.md`](docs/STUDIO.md), how the studio works |
| `external/khdays-decomp` | The decompilation, pinned; updated deliberately and tested |

KH2's local-asset sequence viewer, renderer validation, and HP gauge mapping are
documented in [`docs/KH2_SEQUENCES.md`](docs/KH2_SEQUENCES.md).

### Checking the port

- `ctest --test-dir build/port/obj-release-x64` runs the port's tests. In
  the Release tree they include the static analysis
  (`port/tools/analyze.py`: the port's own sources and the HAL compiled
  again with MSVC's `/analyze`; any finding left fails it -- each is fixed,
  or suppressed at its line with the reason beside it), 15 seconds of each
  fuzzer (libFuzzer with AddressSanitizer over what arrives from outside:
  the server's datagrams, a router's answers, the game's session), and the
  server under a flood.
- `py port/tools/fuzz.py --target server --minutes 60` fuzzes one of them
  (`server`, `portmap`, `session`) for longer, on several cores; an input
  that breaks a reader is kept in `port/fuzz/<target>/` and replayed by
  the tests from then on.
- `py port/build.py --asan` makes the memory-checking build
  (`build/port/obj-asan-x64`): AddressSanitizer on the port's own code and
  the server -- not on the decompilation's, whose memory is the window the
  port maps itself, which AddressSanitizer does not track.
  `py port/tools/regress.py build/port/obj-asan-x64/khdays-native.exe --walk-save OLD.sav`
  plays the regression walks with it (from copies of the save given, never
  the save itself); a report fails the walk.

The format library and its tests build on any platform:

```bash
cmake -S assets -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Relationship to `khdays-decomp`

- **`khdays-decomp`** reconstructs the Nintendo DS code so that it compiles
  byte-for-byte to the original game.
- **`khdays-port`** compiles that code for the PC and provides the platform.

The port never edits the decompilation: what its C needs to run on another CPU
is rewritten at build time (`port/tools/prepare.py`), and what that reveals
about the C is reported back in `docs/DECOMP_FINDINGS.md`.

## Non-goals

- Distributing the game, a pre-patched ROM, extracted assets, or proprietary
  development tools.
- Weakening the byte-matching goals of `khdays-decomp`.
- Claiming affiliation with or endorsement by the original rights holders.
