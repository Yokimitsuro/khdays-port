# khdays-studio

A visual tool for making mods for the port: it shows the game's graphics and
sounds as the port's dumps name them, takes a replacement by drag and drop,
checks it the way the port will read it, and saves it into `mods/<Mod>/` under
the exact name the port looks for. It is a program of its own: it never
changes the port, the extracted game data or the dumps, and writes only into
the mod you choose (and its own settings, `saves/studio.ini`).

The port reads mods when it starts: after saving, start the game again to see
or hear the change.

## Building

Windows x64, with the same tools as the port (Visual Studio with the C++
workload, CMake, Ninja, Python):

```powershell
py .\studio\build.py --config Release --test
```

The build fetches SDL3 (the port's release) and Dear ImGui 1.92.9b, both
checked by hash, and compiles some of the port's own files as they are -- where
the port decides something (which mod wins, how a sound or a PNG is read), the
tool asks the same code. The executable is
`build/studio/release/khdays-studio.exe` (`--config Debug`:
`build/studio/debug/`).

The 3D previews (models, rooms) are drawn by the graphics card through SDL3's
GPU API. Their shader, `studio/shaders/preview.hlsl`, is compiled once into
`studio/app/generated/` (committed), so building needs no shader tools; after
changing it, run `py .\studio\tools\build_shaders.py` with the Vulkan SDK
installed.

## Using it

Run it from the repository root (the folder with `data/`, `mods/`, `saves/`),
or give that folder with `--port <dir>`.

Whatever is saved or written -- a file into a mod, a new mod, a replacement
removed -- says how it went over the window: a green notice that goes after a
few seconds, or a red one with the reason that stays until you close it. The
last one stays in the line at the bottom too.

What the studio knows is shown by its name beside its code: the mission worlds
(`wd_tt` - Twilight Town, in the tool's language), the game's characters'
folders (`ba/ch/ro` - Roxas), and each mission file with the name the game's
own list gives it, read from the player's copy (nothing of it is kept but in
memory).

"Themes" (top right) chooses the studio's colour palette, in the colours of
KH1's, KH2's, KH3's, Days' or Melody of Memory's menus (MoM's unless another
is chosen). It changes at once and is kept in the settings.

1. **The catalog.** The left side lists every texture, 2D tile sheet, model,
   sound, text file and movie of the game. Graphics and sounds come from the
   port's dumps (`khdays-native --dump-graphics`, `--dump-sounds`); without
   them the tool offers to make them with the port. Search by name, filter by
   kind. "Folders" shows them as a tree -- each kind, then the folders of the
   game's files, each with how many it holds -- opened as you search and to
   whatever another view selects; "List" shows them one under the other. The
   choice is kept in the settings.
2. **A mod.** Choose one of your mods or make a new one (top bar). "In this
   mod" lists its files, whether the port uses each (another mod's file may
   come first: mods are taken in the order of their folder names) and any
   file whose name the game does not have; remove one from there.
3. **A replacement.** Choose an item: the original is shown (pictures) or
   played (sounds), with which mod's file the game uses now. Drop a file on
   the window or choose one:
   - a **3D texture**: any PNG (JPEG, TGA and BMP are converted); one that is
     not the same whole multiple of the original on both sides is drawn
     stretched by the port, and the tool says so. "For every palette of this
     texture" saves the form the port uses whatever the palette. To paint one
     in HD, "Export this texture..." writes it at 1 to 8 times its size (each
     pixel repeated: the model's UVs stay as they are), and "Export every 3D
     texture..." writes all of them at that size into a folder you choose, as
     `<texture>.png` where a mod's replacement of each goes under `graphics/`.
   - a **2D tile sheet**: a PNG of the sheet's size times a whole number (1 to
     255), as the port requires; indexed, it is drawn with the game's palettes,
     otherwise with its own colours. (Editing it as a picture: see 6.)
   - a **sound**: WAV, FLAC or Ogg Vorbis, accepted exactly when the port's own
     decoder reads it; without a loop of its own it loops where the original
     does.

   The original and the replacement are shown side by side with what the port
   will make of it; "Save to the mod" writes it, checked again as written.

4. **A model.** Choose one of the game's models: it is shown in 3D with its
   textures and skeleton (the 3D views, a model's and a map's: drag to turn,
   drag with the right or middle button to move across the view, wheel to
   zoom, W A S D to go forward, left, back and right and Q E down and up
   while the mouse is over the view, Shift for faster), and plays its animations: those of its folder
   (and, for the missions' models of `mi/ch`, the motions of `mi/mo`) made
   for its skeleton -- their bones keep its lengths -- listed first; other
   models' animations with as many bones come after, said apart, since they
   pull it out of shape. It opens moved by its first own animation, as the
   game shows it: its rest pose, how it was built, is first in the list and
   can look nothing like it (the Darkside, `DS01`, is built standing with its
   arms out). "Export this model..." writes it (as the port's dump writes it: a
   glTF with its skeleton and meshes, its textures copied beside it) where you
   choose, to change in Blender keeping its bones' names. When it has
   animations made for its skeleton the tool asks whether they go with it:
   each becomes one of the glTF's animations (an action in Blender) under the
   game's name (a name the list repeats -- the same animation in another of
   the folder's files -- followed by `_2`, `_3`...), every bone keyed at every
   frame, 30 a second as the tool plays them. Between two of a curve's samples
   the game blends their matrices, which shortens the bones there; glTF keys
   are rotations, so those frames are written as the rotation nearest the
   blend. Drop an FBX, glTF or glb model on the window (or choose one): it is
   read as the port reads it and shown beside the game's -- or over its
   skeleton -- moved by the game's animation exactly as the port will move it
   (its rules are mirrored from `port/runtime/mods/models.cpp`). A `bones.txt`
   beside your model is read with it.
   - **Joints**: those named like the game's nodes follow them; pair the others
     in the table (or click a joint of each model in the picture); the tool
     proposes, for each node nothing follows yet, the nearest joint. Joints
     left alone move with their parents, as in the port.
   - **Scale** sizes your model to the game's.
   - **Back faces**: as the file says (an FBX is drawn both sides, a glTF as
     its materials say), culled, or drawn -- written to `bones.txt` as
     `cull back` or `cull none`, which the port honours.

   "Save to the mod" writes the model, the files it needs beside it (a glTF's
   buffers and images, an FBX's textures) and its `bones.txt`, keeping that
   file's comments.
   Dropping another model on the same game model (a better version of yours)
   keeps the joints you had paired, its size and back faces wherever the new
   model has joints of the same names; the new file's own `bones.txt`, if it
   has one, comes first. A notice says how many were kept and names any that
   could not be.

5. **An animation.** With one of the model's animations playing, "Replace this
   animation..." takes a glTF or glb clip made on the game's skeleton -- the
   one the port's dump writes as `<model>.gltf` (import it in your 3D program,
   animate, export glTF) -- or the model exported with its animations, one
   of them changed: of a file with several animations, the one named as the
   export named the animation being replaced is taken (else the first), and
   the panel says which. The clip's nodes named like the game's are sampled
   at the original's frames, spread over the clip's length (the game times the
   animation by its frame count, which stays); the nodes it leaves alone keep
   the game's motion exactly. The preview shows the new animation as the game
   will read it back. Saving writes the whole game file that holds it to
   `mods/<Mod>/files/<game path>` (an animation may sit in a `.p.z` file or
   in an entry of a `.p2` archive: the tool rebuilds either, everything else
   in it as it was); when the mod already has that file, the animation is
   replaced in the mod's copy and its other changes stay. FBX clips are not
   taken: FBX exporters rewrite the bones' axes, so the clip's rotations would
   not land on the game's nodes. Adding animations the game does not have is
   not possible: the game picks them by number.

6. **A 2D sheet, assembled.** A tile sheet "as tiles" is the game's memory:
   its 8x8 tiles in rows, shown with the file's first palette -- most menus
   and logos are unreadable that way. "Assembled" shows it as the game puts
   it together: each screen or cell bank in the same game data whose tiles
   are all in the sheet is shown small; tick one or several and they are
   drawn one under the other, every tile in its place, flipped as the game
   flips it, with its palette row (a screen wider than 256 pixels is read in
   strips 32 tiles wide, as the DS holds it). Which screens and palette go
   with a sheet is in the game's code, not its data, so it is your choice
   (one that only fits by its number of tiles shows as noise: leave it
   unticked). The tool starts with its best guess: those that show most of
   the sheet with their tiles joined up come first, and the screens that
   share the sheet's tiles out between them are ticked together (the boot
   logos' sheets come up with exactly the screens the game's own table pairs
   them with); where the data holds as many screens as sheets, the sheet's
   own-numbered one first. "The sheet as it is" is always there: the sheet
   in its own rows, all drawn with one palette row you pick (it starts at the
   row with the most colours: a palette's unused rows are often all black).
   Where the game gives its parts rows of their own in its code (the HUD
   portraits: portrait k with row k + 1), tick "Paint this row on the
   picture" and drag over each part with the row its colours are right in:
   the picture then shows every part as the game does, and you export, edit
   and bring it back once. The rows are kept in the mod
   (`studio/rows/<sheet>.txt`, which the port ignores) and used by the
   command line too; a screen laying the tiles as the sheet does gives them
   its rows by itself. It comes
   first for a picture a screen only lays out tile after tile (a screen
   reaches 1024 tiles, the credits' pictures hold more), and when the screens
   together show under half the sheet and it joins up better. A sheet with no palette of
   its own is drawn with one of a nearby file's (the palette list says
   whose); a localized sheet with no screens of its own takes those of its
   twin, the sheet of the same size it shares most of its tiles with (the
   tool says where they come from), and a sheet in a file of its own those
   of its folder's screen files. A screen that also draws tiles of other
   sheets shows only this sheet's in place.
   **The colours seen in play.** Some sheets have nothing in their files to
   say which palette row each tile takes -- the field HUD's
   (`UI/btl/<lang>/main.p2#0/chr/0`), whose screen the game builds in its
   code: there one row for the whole sheet is a guess, and a wrong one. Play
   with the port's "Dump graphics" on (Settings, Video) and the port writes,
   next to the dump's pictures, `<sheet>.colours`: the row each tile of each
   sheet it shows was drawn with, by a layer or a sprite, and that row's
   colours as the game had them. The tool reads them from the graphics dump
   and starts with them -- "Palette N: the colours seen in play" and every
   tile in its own row; the tiles never seen are drawn in greys (row 16,
   after the game's), never in colours guessed for them, outlined in blue,
   and the tool says how many they are. Colours a row of the game had for
   some tiles and not others (the HUD gives each slot's row the palette of
   the portrait in it) come as rows of their own from 17 on. Sheets of 8-bit
   tiles (the dialogs' faces) are recorded too, with their 256 colours. What the port sees adds up from one play
   to the next, so the more of the game you play with the dump on (or the
   port's regression walks run with `KHDAYS_DUMP_GRAPHICS`), the more sheets
   come up in their real colours. Export the
   picture (at 1 to 8 times its size, to paint it in HD), edit it anywhere,
   and bring it back: the tool takes the pixels you changed back into the
   sheet and shows the result before saving. "Export every 2D sheet..."
   writes all of them, each as it shows when opened (the sheet as your mod
   has it, its rows, the colours seen in play), into a folder you choose,
   as `<sheet>.png`.
   - If every changed pixel is a colour of its palette there, the sheet is
     saved as the game's palette indices: its fades and palette changes still
     work. Otherwise choose your own colours (fixed, whatever palette the game
     picks) or "fitted" (each changed pixel the nearest palette colour).
   - A tile used in several places is one tile: changing it in one place
     changes it in all (marked in yellow, with how many places use it); two
     places edited differently keep the first (red). Tiles drawn flipped and
     tiles drawn with several palettes are listed too.

7. **A map.** The "Maps" kind lists each room of the mission worlds
   (`mi/wd/wd_<code>`, by its place in the world's table). A room is shown
   as the game draws it -- its models, their vertex colours and textures --
   and, when asked, its collision over them: the grounds a character stands
   on, the walls that stop it, the ceilings, the faces that carry a gate, a
   wall that opens or a surface tag (nohit, slide, sea...), and the placement
   markers (`pentKK_i`: where a character arriving through gate KK appears).
   Click a face or a marker to read what it is.
   - **Replacing a room**, in four steps under the picture:
     1. *Your model.* Drop it (FBX, glTF, glb, OBJ) on the window or choose
        it. To start from the game's room, "Export this room..." writes it as
        a glTF (its models with their textures, its floor and walls as meshes
        named ground, wall, ceiling) to change in Blender and drop back.
     2. *Where it sits.* A model made from the room (its export, or the port's
        dump, edited) is lined up by itself: the tool tries the quarter turns
        and the 100/10/0.1/0.01 scales a 3D program can add and keeps the one
        that puts the model's points on the room's, and says what it did; a
        model remade in the same place is left as it is. Any other model is
        placed by hand (scale, turn, move, in the game's units), the tool
        giving its size and the room's.
     3. *Floor and walls* -- what characters walk on and bump into: the
        game's own kept (best when your room is this one remade in the same
        place: only the look changes), made from your model, or made from
        another, simpler one.
     4. *Save.* (Below.) The rest -- the steepest ground, what the room's
        other models become, names, exits' models -- is under "Advanced".
     Floor and walls made from a model follow the game's rules: each face a ground
     when it faces up (up to the steepest ground, 78.5 degrees by default:
     where the game's movement steps onto a slope), a ceiling when it faces
     down, else a wall; a face stands on the side its corners turn
     counter-clockwise (glTF's front; a material drawn both sides stands both
     ways). A separate, simpler collision model can be given instead. Each of
     the room's own models becomes yours, nothing, or stays the game's.
   - "Save into the mod" writes `files/mi/wd/wd_<code>` (the world rebuilt
     from your copy of the game, only the replaced rooms' collision new) and
     `graphics/mi/wd/wd_<code>#<entry>/mdl/<model>.gltf`; the change itself
     and your model stay in `studio/rooms/<code>/`, which is what to share
     (never the game's files). The port draws a replaced room whether HD
     graphics are on or off.
   - **Exits, walls that open, surface tags.** The room's names -- its exits
     (`gate06`: walking into a face that carries it takes the exit), its walls
     that open (`col_wall..`) and the surface tags (`slide`: a slope that
     cannot be climbed; `nohit`, `nocatch`, `sea`, `nocam`, `sea2`, `sicon`, each
     described by the game code that reads it) -- are given to a material of
     your model, or to a face picked in the picture. An exit or a wall that
     opens that none of your faces takes stays where the game had it (its
     faces copied), so a replaced room keeps its ways out until you move them.
   - **Markers and exits' models.** Pick a marker (where a character arriving
     through an exit appears) to move or turn it, or drop it "onto the
     ground"; the exits' models (the gate and its props, yellow) are moved by
     their position.
   - Said before saving: an exit left on no face, a marker with no ground
     under it, other rooms that use the same collision or model (they change
     too), a collision larger than the game's largest room (77,624 bytes; the
     game's memory had about 1.6 MB free in the one field measured).

8. **A mission.** The "Missions" kind lists the game's mission files
   (`mi/mi/<number>`). Each holds archives of scripts the game's script
   machine runs; a script is shown step by step: each step by the function
   the game runs for it (the decompilation's command tables, by group) and its
   words as read -- a number, a 20.12 value, a field of the game's state, a
   variable. What each function does with its words is read one command at a
   time from the game; until a command is read, nothing here gives it a
   meaning. Above a script's steps, "Places" lists the world and rooms it
   loads and its spawners -- each an enemy group of the mission's table at a
   point, with the room whose ground is under it; "See in the room" shows
   that room with the script's spawners drawn as green rings.

   The "Summary" tab says what the mission is in plain words, each value the
   studio has read editable in place: where it happens (world and rooms),
   the texts it shows on the screen (a field per language; one text that
   several steps show is changed in all of them), where each player starts
   (point and direction), the time limit, the enemies -- each spawner's
   point, the group it makes and that group's enemies by name and how many,
   and the enemies a script puts one by one (the big ones) -- the chests
   (point, direction, contents' key) and the emblems (point). Each line says
   where its step is set ("area 11 · _i") and has a button showing its room
   with every point drawn. "Save into the mod" writes `files/mi/mi/<number>`
   and, when its chests or emblems changed, `files/mi/mi/trbox` / `evi`
   (every mission's, this one's set changed), each read back first; a notice
   says what was written. "Give the mission back to the game" removes them
   (only this mission's sets in trbox / evi). Nothing moves in the files: a
   step keeps its length and a table its size; a longer text is written past
   the script's data and pointed at. An enemy is chosen among the kinds the
   mission loads; adding a kind and adding steps (a time limit to a mission
   without one) come next.

   "Make another mission from this one..." puts the mission's content, as
   shown (unsaved changes included), into the slot of another mission in
   the mod: that slot keeps its place in the Mission Mode list, its name
   there, its unlock and its saved progress (kept by its number); its chests
   and emblems become this mission's when the two sets have as many rows. A
   mission in a slot of its own needs the port's list made larger (not done). The names of
   the enemies are those the decompilation gives the overlay each id loads
   (`studio/tools/enemy_names.py`).

9. **A text file.** The "Texts" kind lists the game's string tables
   (`*.s.z`, the menus' and screens' words) and its message databases
   (`db/db_<language>.p2`, in lists). Each text shows the game's beside
   yours; write in the right column (Enter starts a new line, as in the
   game; codes like `\x01` are the game's control codes, to keep). A text
   changed turns green, and a character the game's font cannot draw is
   said under it. "Save into the mod" writes the whole file to
   `files/<path>`; what you typed in other files stays while you look
   elsewhere. "Give it back to the game" removes the mod's copy.

10. **A movie.** The "Movies" kind lists the game's movies (`mv/<id>.mods`).
    The view says what plays now -- a mod's video, your own HD video
    (`data/cutscenes/cinematics/hd<id>.mp4`), or the game's -- and plays each
    of them here, with its sound: the game's own from its preview converted
    to MP4 (`data/preview/video/mobiclip/<id>.mp4`), yours, every mod's
    (through the port's own player, Media Foundation, which reads MP4 and
    MPEG-1 alike). The video stops when another item is chosen.
    "Choose a video" takes an MPEG-1 file (`.mpg`), saved as
    `movies/<id>.mpg` and played wherever the movie shows, in its frame; or
    an MP4, saved as `movies/hd<id>.mp4` and played full screen with the
    game's movies set to HD. A mod holds one of the two per movie (an
    `.mpg` would win over an `.mp4`).

11. **A plugin's parameters.** "Plugin" (top right) opens the chosen mod's
    native plugin's parameters, as the mod describes them in its
    `parameters.ini` (docs/MODDING_NATIVE.md, "Its parameters"): a slider for
    a number within its bounds, a box for a flag, a list for its choices, a
    field for a text, each with its label and help in the tool's language and
    its default. "Save the values" writes them beside the description
    (`parameters.values.ini`), which the game reads as it starts. The plugin
    itself is never loaded: what shows is the description's.

    To try one, `mods-examples/CustomLimit` is a mod with parameters (its
    Limit's spins, damage, reach and status).

12. **Characters.** "Characters" (top right) lists the chosen mod's playable
    characters (docs/CHARACTERS.md) and makes new ones: a folder name, a name
    and the game's character it plays as. Its tabs:
    - *Character*: its name and its weapon's, for every language or one; the
      character it plays as; when it is offered; its picture in the character
      select (any picture: one not in PNG is made one); its own gauges.
    - *Model*: its model (FBX, glTF or glb, chosen or dropped on the window:
      copied into `model/` with the files it needs); its own skeleton or its
      bones paired with its base's; its size, with **As tall as its base**;
      the bone holding its weapon and how the weapon turns on it.
    - *Animations*: its clips (**Add clips...**, or dropped: copied into
      `clips/`) and, for each animation the game plays for its base (read from
      the game, named where they are known: standing, running, jump, combo,
      Limit...), the clip shown instead; a row clicked plays the game's
      animation on the base and the character's clip on the character side by
      side; below the table, the chosen row's blend, its own step and the
      game's beat.
    - *Voice, moves and Limit*: its sounds (a file each, or none), its combos
      and its Limit's damage.

    **Save the character** writes `character.ini` when the port's own reader
    (port/runtime/characters/registry.c) takes it and every file it names is
    in its folder; what the game would refuse is listed above the tabs as it
    is changed. What the window does not change -- comments, parts it does not
    show -- stays as written. Another character chosen, or the studio closed,
    with changes not saved asks first.

13. **Missions.** "Missions" (top right) holds the chosen mod's own missions
    (docs/STUDIO.md, "New missions"): **New mission** makes one from a folder
    name, a world and a room. Each mission's **Places on the map** shows its
    room from above with its floor: choose what to add -- the players' start,
    enemies, a chest, an emblem -- and click the floor; drag a place to move
    it; click it to change it on the right (its wave and enemies by name, how
    many, its facing, a chest's contents, an emblem's badge) or remove it.
    **Save the places** writes them all at once when the mission builds as the
    game will build it. **Words and settings** holds its name, objective and
    description in each language; the world it is played in and its room; the
    world tab of Mission Mode's list it is listed under (`category`); its
    enemies' level and the time limit the list shows; and when it opens
    (`unlock`): from the start, after one of the game's missions (chosen by
    its number and name), or after another of the mod's.

Sound loops and old-format mods come next.

## Native plugins' examples

The installer brings them with the studio (its "KH Studio" choice; without it,
none is installed), beside the game:

- `mods-examples/`: five mods with a native plugin, each ready to copy into
  `mods/`, its Windows plugin built -- `HelloWorld` (a plugin's start, its
  lines in the mods' log, its stop), `CustomCharacter` (a character of its own,
  Block, whose attacks heal and hit twice as hard), `CustomLimit` (Spinner, a
  character whose Limit is the plugin's: its own clips, its spins' hits; its
  parameters in the Plugin window), `CustomEnemy` (the Shadows blink away and
  split in two) and `AdvancedNativeMod` (hooks at the game's routines, a
  routine and a datum by name).
- `sdk/`: the KH Mod SDK they are built with -- its headers, CMake, docs
  (`sdk/docs/README.md`) and the examples' sources -- to build plugins of your
  own.

A plugin runs only once the player turns native code on (the game's settings
menu, Mods page, **Native code**: read at the next start). What a plugin may
do, and its risks: docs/MODDING_NATIVE.md.

## Command line

```powershell
.\build\studio\release\khdays-studio.exe --list <Mod>
```

prints each file of a mod, the kind of item it replaces and whether the port
uses it.

```powershell
.\build\studio\release\khdays-studio.exe --replace-animation <Mod> <model> <animation> <clip.gltf>
```

replaces one animation of a model as the window does: `<model>` as the
catalog names it (`ba/ch/ro/def_hb.p.z/mdl/roxas_b_h`), `<animation>` by its
name (`b_ro_A000`), or `<file>:<name>` when several files of the folder name
one alike (`ba/ch/ro/def_hb.p.z:b_ro_A000`).

```powershell
.\build\studio\release\khdays-studio.exe --check-animations
```

rebuilds every animation of the game in memory -- with every node kept, then
replaced by its own motion -- and every file that holds one, reads each back
and reports any difference (about a minute and a half).

```powershell
.\build\studio\release\khdays-studio.exe --assemble <sheet> screen:N|cells:N|sheet|best... [row:N] [palette:N] [scale:K] [mod:<Mod>] <out.png>
.\build\studio\release\khdays-studio.exe --take-back <Mod> <sheet> screen:N|cells:N|sheet... [row:N] [palette:N] [colours:own|fit] <edited.png>
```

do what the assembled view does: `<sheet>` as the catalog names it
(`ttl/ttl.p2#1/chr/0`), the arrangements by number, `sheet` (with `row:N`)
for the sheet as it is, or `best` for those the window starts with
(several are stacked; a wrong one is refused with those that fit), `mod:`
draws the mod's sheet instead of the game's. `--take-back` writes `graphics/<sheet>.png` into the
mod and prints what it found (repeated, flipped, conflicting tiles).

```powershell
.\build\studio\release\khdays-studio.exe --arrangements <sheet>
```

lists the arrangements that fit a sheet in the window's order, with how well
each one's tiles join up and how much of the sheet it shows; those the window
starts with are marked.

```powershell
.\build\studio\release\khdays-studio.exe --export-sheets <folder> [mod:<Mod>]
```

writes every 2D sheet into `<folder>` as `<sheet>.png`, coloured as the window
first shows it (with `mod:`, the sheet as that mod has it and its rows).

```powershell
.\build\studio\release\khdays-studio.exe --export-textures <folder> [scale:<n>]
```

writes every 3D texture the graphics dump has into `<folder>` as `<texture>.png`,
`n` (1 to 8) times its size with each pixel repeated.

```powershell
.\build\studio\release\khdays-studio.exe --check-sheets
```

assembles every 2D sheet of the game with each screen and cell bank that fits
it (its twin's or its folder's too), at 1 and 2 times, takes the whole
picture back and draws it again (about a minute and a half).

```powershell
.\build\studio\release\khdays-studio.exe --world-info <code> [<room>]
.\build\studio\release\khdays-studio.exe --room-model <code> <room> <out.gltf>
```

print a world's rooms (their models, links, collision) or one room's faces'
extent, markers and names; write a room's models and collision (a mesh per
kind of face, its names in the material) to a glTF to build on.

```powershell
.\build\studio\release\khdays-studio.exe --replace-room <Mod> <code> <room> [<model> | kh2:<map> [<collision model> | game]]
.\build\studio\release\khdays-studio.exe --revert-room <Mod> <code> <room>
.\build\studio\release\khdays-studio.exe --rebuild-worlds <Mod>
```

replace a room by a model (placed as its change says, or as it is) or by a
KH2 map of the player's collection (`kh2:tt02`: taken out, made and placed
first by KH2's units; `kh2-collision` takes the map's own floors and walls), or make it again from the change the mod holds, and
print what was made and every warning (markers left in a wall or with no
ground under them named); give a room back to the game; write every world the mod changes
again from its changes (after a game update, or on another player's copy).

```powershell
.\build\studio\release\khdays-studio.exe --kh2-room <map> <out.gltf> [<folder>]
.\build\studio\release\khdays-studio.exe --kh2-maps [<world>]
```

makes a KINGDOM HEARTS II FINAL MIX map (`tt02`, the Usual Spot) of the
player's own KINGDOM HEARTS HD 1.5+2.5 ReMIX copy into a model to replace a
room with: its background, skies and pieces (placed, moving as in KH2), its
remastered pictures as PNG beside it, scrolling and changing textures; from
`<folder>/original/map/<map>.map` and `<folder>/remastered/map/<map>.map/`
as the collection's archives give them (`saves/collection/kh2` when not
given). What it makes is Square Enix's work from the player's copy: theirs to
play with, not to share. The window's Maps view does the same from the room's
Replace panel ("A KH2 room": the world's maps of the copy with their radar
pictures, or one by its name); the collection's folder is in the settings
(`collection_1525`, empty: the one the install used for its HD pieces).
`--kh2-maps` lists the maps of the player's copy of the worlds Days shares (or
one), each with the Days rooms made from it (`studio/data/kh2_rooms.txt`) and,
once taken out, its pieces and moving textures.

```powershell
.\build\studio\release\khdays-studio.exe --check-missions
.\build\studio\release\khdays-studio.exe --mission-info <number> [<script>|<entry>:<index>]
.\build\studio\release\khdays-studio.exe --mission-commands
```

read every mission file and the mission database's files (`mdb.z`,
`eid.z`, `evi`, `trbox`) and write each back, walking every script (2,777
in the game, under two seconds); list a mission's scripts or one script's
steps; list every command the missions use with the function the game runs
for it.

```powershell
.\build\studio\release\khdays-studio.exe --model-animations <model>
```

lists a model's animations (`Ms/46.p#0/mdl/DS01`, as the window names it),
those made for its skeleton apart from other models' with as many bones.

```powershell
.\build\studio\release\khdays-studio.exe --export-model <model> <out.gltf> [animations]
```

writes a model as "Export this model..." does; with `animations`, each is
read back as "Replace this animation..." reads a clip and compared with the
game's: at the frames where a node's rotation is one of its curve's samples
the matrices agree to float precision (Roxas, `ba/ch/ro/def.p.z/mdl/roxas_b`:
82 animations, 0.0005 at most; the Darkside's 24 the same), between them the
axes turn a few degrees from the blend's (Roxas's fastest swings 7.75).

```powershell
.\build\studio\release\khdays-studio.exe --check-texts
```

reads every text file of the game (142 files, 25,865 strings) and writes each
back, checking that it reads back the same.

```powershell
.\build\studio\release\khdays-studio.exe --check-worlds
```

rebuilds every mission world with nothing changed, writes every room's
collision back from what was read, checks the game's trees and rebuilds them
from their faces, and computes the faces' planes as the game does (under a
second).

```powershell
.\build\studio\release\khdays-studio.exe --plugin-parameters <Mod>
.\build\studio\release\khdays-studio.exe --set-plugin-parameter <Mod> <name> <value>
```

print a mod's native plugin's parameters with the values chosen (and what is
wrong in its files), or choose one and save it, as the Plugin window does.

Exit code 0, 1 on a problem, 2 on bad arguments.

## Tests

`ctest --test-dir build/studio/release` (or `build.py --test`) runs the tool's
tests, on data they make themselves -- nothing of the game.
