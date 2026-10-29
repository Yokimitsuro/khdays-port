# The port's installer

What a player runs to install the port: a setup program made by
[Inno Setup](https://jrsoftware.org/isinfo.php) 6 from the script here. It carries its own Python,
so the repository's tools -- the ROM check, the extraction, the HD tools -- run for the player
without anything else installed. Nothing of the game or of the collections is in it: everything
is made on the player's computer from the player's own copies.

## What is here

| File | What it is |
|------|------------|
| `khdays.iss` | the Inno Setup script: the pages, the files, the steps after them, the uninstaller |
| `messages.iss` | the installer's own words in the game's five languages |
| `setup_steps.py` | the helper: each step the installer cannot do by itself (`--help` lists them) |
| `steam_library.py` | where Steam keeps the collections (its folder, libraries, app manifests) |
| `epic_library.py` | where the Epic Games Store keeps them (its launcher's manifests, the default folder) |
| `steam_shortcut.py` | the port added to Steam as a non-Steam game, and taken out again |
| `collection_archive.py` | the collections' archives (`.hed`/`.pkg`) read as their own code reads them |
| `hd_inputs.txt` | the collection files the HD tools read (traced: `setup_steps.py trace-inputs`) |
| `build_installer.py` | builds the installer |
| `pinned.json` | the Python and its libraries the installer carries, each pinned by SHA-256 |
| `tests/` | the installer's own tests, run by ctest as `khdays-installer-*` |

## The install

By default in `C:\Program Files\Kingdom Hearts 3582 Days`, so it installs with administrator
rights once; because the port keeps its saves, settings and game data beside itself, the install
folder is then made writable by the player (the Users group is granted Modify on it), so the game
writes there at runtime without administrator rights. `/PRIVILEGESREQUIRED=lowest` on the command
line installs per-user instead, in `%LOCALAPPDATA%\Programs\...`, with no administrator and no
grant needed. The folder has the repository's layout at its root -- `tools/`, `port/tools/`,
`installer/` beside `data/`, `mods/` and `saves/` -- because the tools find the game's data
and write the mods from their own place; `setup/` holds the Python and `state.json`, the steps
done. The port, started from a shortcut, takes its own folder as its working folder when the one
it was started in has no `data/extracted`.

Whatever a step makes is written under a `.partial` name and renamed when whole (the port never
takes a `.partial` extraction), so an interrupted install is finished by running the installer
again. Updating or repairing keeps the player's saves, settings and mods; uninstalling removes
what the installer made and asks whether the saves and settings go too (to the Recycle Bin).

The multiplayer server (`khdays-server.exe`) is always installed beside the game, for a player
who wants to host their own; the game's default server is the maintainer's.

The example gameplay mods (`port/mods_examples`) and their interface (`docs/SCRIPTING.md`) go to
`mods-examples/`, outside `mods/`: none runs until the player copies its folder into `mods/`.

The studio's task (off by default) brings the native mods' tools with it: the KH Mod SDK as the
repository has it (`sdk/`: its headers, CMake, docs and the examples' sources) and each of its
examples as a mod ready to copy, its plugin built (`mods-examples/HelloWorld`, `CustomCharacter`,
`CustomLimit`, `CustomEnemy`, `AdvancedNativeMod`). The build builds the SDK alone into
`build/sdk` (with `--no-build`, the one already there).

## The HD pieces

Optional, from the player's own copies, on the HD page: **HD menus and HUD** (both KINGDOM HEARTS
HD 1.5+2.5 ReMIX and HD 2.8 Final Chapter Prologue: the tools' `mods/HD PACK`) and **HD videos**
(1.5+2.5: the game's movies and the theater's scenes that `port/movies/hd_scenes.txt` names, with
their subtitles, in `data/cutscenes`, about 6.5 GB; the installer carries the scene map beside the
tools). The helper reads the entries the tools need straight from the collections' archives (`collection_archive.py`: the
format and its encryption found by our own reverse engineering of the collections' executables;
the cipher's table is read from the player's own `KINGDOM HEARTS II FINAL MIX.exe`, never kept),
runs the tools under a folder of their own and puts what they made in place whole. An update
leaves the pieces already made from the same collections by the same tools as they are (deleting
their folder makes them again). `state.json` keeps each menu tool apart, with the files it wrote:
an installer whose tools differ runs again only the tools that changed (themselves or a tool they
import) or whose files went missing, with any tool that wrote one of the same files, and keeps the
others' files. The videos
already installed are linked into the tools' folder, not copied, so an installer whose scene map
or video tool differs makes only the missing videos, and the subtitles and music again. Either folder may be left empty: a choice whose collection is not given cannot be
ticked. The collections are found in Steam's libraries, else in the Epic Games Store's folders (its
launcher's manifests in `ProgramData/Epic/EpicGamesLauncher/Data/Manifests`), and either store's copy
is read: Steam's keeps its language's archives in `Image/dt` and Days's videos in
`STEAM/Mare/MOVIE/Days/dt`, the Epic Games Store's its archives in `Image/en` and the videos in
`EPIC/Mare/MOVIE/Days/en` (any `*/Mare/MOVIE/Days/*` holding them is taken). (The page is a text
page with its own Browse buttons, because Inno's folder page refuses an empty folder.) A tool that
fails (a copy a mod changed may lack what it reads) leaves its screens with the DS's look and the
others are made all the same: the installer says which, keeps what each said whole in
`setup/hd-errors.log` for a report, and leaves the step undone so the next run tries again. The
pictures a tool cuts at known places are taken at their places scaled to the copy's picture, as a
mod may have put a smaller one there.

The menus step also generates the five European Days PNG font replacements in
`mods/HD PACK/fonts/text/` from KH2's SYS/EVT font sheets and advance tables,
matched to the player's Days NFTR character maps. The conversion and defaults
are described in [HD font rendering](../docs/HD_FONTS.md#generating-kh2-fonts).
Only the generator ships; all font images and JSON definitions are made locally.

HD menus and HUD also imports the native KH2 HP gauge. `kh2_gauge.py` reads the
five localized `field2d/*/zz0field.2dd` entries from `kh2_fifth` and their second
remastered PNG sheets. It adjusts the private output's sequence coordinates,
adds the Days limit overlay and divider, and creates per-language `gauge.json`
files under `mods/HD PACK/layouts/gauge/`. The installation's packages remain
untouched. Existing gauge animation/position settings are preserved when the
pack is rebuilt; generated resource names and centered origins are updated.
The converter scripts and default JSON are shipped with the installer and
included in its HD cache fingerprint. [Native sequence details](../docs/KH2_SEQUENCES.md).

The same localized archive supplies party/enemy gauges, field reticles,
INFORMATION, and the obtained-item box. Additional mod-authored layouts stay
disabled until their JSON entries and resources are provided. The minimap tool
exports Days' icon atlas and copies KH2's remastered radar mask; existing icon
settings and images are preserved. Replacements can be gated live in the game's
Enhancements menu without rewriting these JSON files.

## Add to Steam

Optional, off by default, on a page of its own shown only when Steam is found: the port added to
the player's Steam library as a non-Steam game (its name and icon), so it starts from Steam like
any other game. The entry goes in each local Steam user's `userdata/<account>/config/shortcuts.vdf`;
nothing of the collections is changed.

Steam reads those files when it starts and writes them when it closes, so they are changed only
while Steam is closed: if it is running, the installer asks before closing it, and offers to open
it again afterwards. The binary file is changed only when it reads back to exactly its own bytes
(another program's shortcuts are never lost), with a one-time `shortcuts.vdf.khdays-backup` kept
beside it. The entry's id is the port's own, made from its path, so the same number finds it to
remove it. Turning the option off on a later run, or uninstalling, takes the entry out again.

Under Proton or Wine (a Steam Deck, Linux) the installer runs in a Wine prefix, whose Steam is
not the computer's (told by Wine's `wine_get_version` in `ntdll.dll`, which Windows's has not; a
`Software\Wine` registry key is no sign, Windows programs leave one too): the option is shown off
and disabled, with a note, and the helper refuses it
even for a silent `/STEAM=yes`; the game is added to Steam by hand from Desktop Mode (a non-Steam
game pointing to `khdays-native.exe` in the install folder, Proton forced in its Compatibility
properties). The folder grant is skipped there too. `steam_shortcut.py` can also map a Proton tool
in Steam's `config/config.vdf` (the one the 1.5+2.5 collection uses), for when the installer
reaches a Linux Steam; it is not used yet.

## Building it

Needs the port's usual build tools (the repository's README, "Getting started") and Inno Setup 6
(its `ISCC.exe` found in its usual places, or named by `--iscc` or `KHDAYS_ISCC`). The files of
`pinned.json` are fetched once into `build/installer/cache/` and used only when their SHA-256
matches.

```powershell
py installer\build_installer.py            # builds the Release port first
py installer\build_installer.py --no-build # with the Release port already built
```

The setup lands in `build/installer/`, named by its version: the repository's `VERSION` file
("0.1.0") when the commit is that release's tag (`v0.1.0`) and nothing is changed, otherwise that
version with the commit after it (`0.1.0+e593a05`, `-modified` when files changed).

## Updates

A newer installer run over an install updates it in place: the port's files are replaced, and the
game's data (already made), the HD pieces (unless its HD tools differ), the saves, the settings and
the mods are kept. The port
itself tells the player when a newer release is out (at each start it asks GitHub for the latest
release, `host/app/updates.c`; a setting turns it off). Its Download fetches the release's
installer (the `KH358-2-Days-Port-Setup-*.exe` among the release's files), checks it against the
size and SHA-256 GitHub gives for it, and runs it as
`/SILENT /NORESTART /AUTOUPDATE /DIR=<the game's folder>` (`host/app/update_install_win.c`), the
game closing: Windows asks for the administrator's permission, the installer shows its progress
alone with the last install's choices, leaves the Steam entry as it is and opens the game again at
the end. An install that is not one (no uninstaller beside the game), or one under Proton or Wine,
gets the release's page instead, as does a download that fails or does not match.

## Without its pages

For a test or a tester: `/VERYSILENT /SUPPRESSMSGBOXES /DIR=<folder> /ROM=<rom file>` installs
without showing anything (add `/HD1525=<folder> /HD28=<folder> /HD=menus,videos` for the HD pieces,
`/STEAM=yes` to add the port to Steam -- a running Steam is then closed without asking and not
reopened); a file that is not the supported ROM stops it before anything is installed. The
uninstaller takes `/VERYSILENT /SUPPRESSMSGBOXES` too (the saves are then kept; the Steam entry is
taken out, closing Steam if it runs).

## How it works

**The helper** (`setup_steps.py`) runs each step the installer cannot do by itself, with the Python
the installer carries:

```
python -I setup_steps.py <command> [arguments] --result <file.json> [--language es|en|fr|de|it]
```

Every command writes its result to `--result` as one JSON object and exits 0 on success, 1 on a
refusal the player can fix (a wrong file, a missing folder), 2 on a failure (a bug, a full disk). A
result's `message` is in the language asked for. Progress goes to the standard output as lines
`progress <done> <total> <words>` for the installer's page.

**The collections' archives** (`collection_archive.py`) are read as the collections' own code
reads them, found by our own reverse engineering of their executables (the launcher's and KINGDOM
HEARTS II FINAL MIX's `Axa::PackageFile`), nothing taken from other tools:

- the `.hed` is a table of 0x20-byte entries: the MD5 of the entry's path as written (16 bytes),
  its offset in the `.pkg` (int64), two lengths (int32);
- at that offset a 16-byte header: the data's length, the count of remastered assets, the stored
  length (-2: stored as is; -1: encrypted only; above 0: compressed with zlib, then encrypted) and a
  date; then a 0x30-byte record per remastered asset (its name in 0x20 bytes, its length at +0x28,
  its stored length at +0x2c); then the data; then each asset's data in turn, each taking its stored
  length, or its length rounded up to 16 when not compressed;
- 16 bytes or fewer are read as they are; otherwise the first 0x100 bytes' whole 16-byte blocks are
  encrypted: each XORed with the XOR of eleven round keys made from the entry's header (its zero
  bytes replaced by their index) by an AES-128 key expansion whose S-box is a table of the game's
  own, taken from the player's KINGDOM HEARTS II FINAL MIX executable (never kept); an asset uses its
  entry's header.

`python collection_archive.py <archive without .hed/.pkg> <out folder> <kh2 exe> <path> [<path> ...]`
writes each path's data as `original/<path>` and its assets as `remastered/<path>/<asset>`; a path the
archive does not hold is named and left out (exit status 3 when it holds none of them).
`python collection_archive.py --has <archive without .hed/.pkg> <path> [<path> ...]` prints the paths
it holds. The studio takes KH2's maps out of the player's copy with both (studio/README.md, `--kh2-room`).

**Steam** (`steam_shortcut.py`) keeps each user's non-Steam games in
`userdata/<account>/config/shortcuts.vdf` (binary KeyValues) and a game's compatibility tool in
`config/config.vdf` (text KeyValues). Both are read when Steam starts and written when it closes, so
they are changed only while Steam is not running. The binary file is parsed into a plain list of
fields keeping every byte, and a change is written only when the parse serialises back to exactly
the original bytes. The port's `appid` is set here (Steam keeps an entry's own), so the same number
keys the Proton mapping and finds the entry again to remove it. In `config.vdf` the compatibility
tools live at `InstallConfigStore/Software/Valve/Steam/CompatToolMapping`, one block per app id; the
port's entry is inserted into or removed from that block as text, so every other byte stays as
Steam wrote it, and only when the block exists (the player has run a game under Proton).
The HD menu/HUD build also runs `heart_points.py` against the extracted Days ROM.
It assembles the heart counter's palettes into an editable PNG and creates its
`hart` BAR image/sequence entries. The installed native executable provides a
headless model-conversion command; the game is not started during conversion.
Existing edited counter files and `gauge.json` settings are preserved on rebuild.
