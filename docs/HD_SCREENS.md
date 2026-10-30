# The HD screens' files

With the HD pack (`mods/HD PACK`, made by `tools/hd/` from the player's own
copies of the KINGDOM HEARTS HD collections) the port draws some of the game's
screens itself, in the collections' pieces: the save list, the camp menu, the
theater's chapter list (MODO CINE), the room list, Mission Mode's character
select and the settings menu, and subtitles over the HD videos. Each is a text
file of lines a tool writes, read by `port/host/screens` and
`port/host/settings`. Without the mod the game's own screens show.

## The shared lines

`port/host/screens/layout_screen.h` reads what every screen file has, and
draws pieces and lines of text in a 1920 x 1080 picture fitted to the window
(its aspect kept, the rest black):

| Line | What it is |
|---|---|
| `piece NAME U V W H` | a picture in the atlas |
| `icon NAME U V W H` | an input's icon: `xbox:<input>`, `playstation:<input>`, `pad:<input>`, `key:<SDL scancode>` |
| `place NAME X Y W H` | where a piece goes |
| `text NAME X Y SIZE ALIGN GREY [R G B]` | a line's place (Y its capitals' top; their middle centred), its size (the font's scale), `left`/`centre`/`right`, its grey or its colour |
| `atlas FILE` | the pieces' picture, beside the file |
| `font FILE CELL TOP BOTTOM [rgba]` | the font's picture: its grey made the text's alpha, or with `rgba` its own colours (an outlined font); its cells' height, where its capitals ink them |
| `glyph CODE X Y W LEFT ADVANCE` | a glyph: its cell's place and width, its bearing, its advance |

The game's own strings are used where the game has them. In them the DS's
buttons are characters of the game's font (U+3349 its A, U+3314 its B, U+3322
its X, U+3336 U+3351 U+3357 its START); each is drawn as the icon of the input
the player has for that button on the device played -- the file's icons of an
Xbox pad, a PlayStation's and the keyboard's keys, the port's Switch icons for
any other pad (the DS's letters in the same places), the input's name where
there is no icon.

## The save list (`layouts/save/<lang>/save.txt`)

Written by `tools/hd/save_screen.py`, in DDD's save screen pieces; drawn by
`save_screen.c` over the game's own list (`runtime/menus/savelist.c`).

| Line | What it is |
|---|---|
| `rows X Y W H PITCH SHOWN` | the first row, the rows' pitch, how many show |
| `scroll Y0 Y1` | the thumb's top at the list's first row and last |
| `pitch NAME N` | the lines' pitch of a text of several |
| `words empty WORDS...` | DDD's words for an empty file |
| `words guest WORDS...` | the guest's row's words |

Days's own words are read as the game reads them (`UI/load/lrd_&.s.z` for the
list, `UI/newgame/ngm_&.s.z` for the difficulties, `UI/btl/&/info.s.z` for the
day). The keys are the game's A and B prompt (`UI/mlt/mlt_&.s.z` record 63);
the glove points at the selected row or button. With the guest's row (Mission
Mode's) Y plays as the guest from anywhere in the list.

## The camp menu (`layouts/camp/<lang>/camp.txt`)

Written by `tools/hd/ddd_camp.py`, in DDD's camp pieces; drawn by
`camp_screen.c` from what `runtime/menus/camp.c` reads of the game's menu.

| Line | What it is |
|---|---|
| `backdrop FILE` | the picture behind the game's 3D |
| `bars X Y W PITCH H` | the options' bars: the first's left, top and size, their pitch (places `bar_selected`, `bar_body`) |
| `option N WORDS`, `story_option N WORDS` | an option's name; the story camp's |
| `story_sub N WORDS` | the story's Holo-Missions' two options (0, 1) and Files' (2, 3) |
| `story_model_source`, `story_model_view` | the story camp's model frame |
| `day_colour R G B` | the story's day's tag (place `day_tag`) |
| `panel X Y W H` | the status panel (its places and texts from its top left) |
| `trials X Y W H` | the trials' bar: label, symbols and counts centred in it |
| `menu_place WORDS` | where a member is on the menu |
| `mode_label WORDS`, `difficulty N WORDS`, `mission_mode WORDS` | the game mode's row |
| `model X Y W H` | the window for the game's model (the backdrop drawn around it) |
| `model_source X Y W H` | the DS's frame of the model, in upper-screen dots |
| `model_view X Y W H` | the part of it the window shows (all of it when not given) |
| `description_pitch`, `digit_overlap`, `members`, `munny_colour`, `hearts_colour` | the description's pitch, the digits' overlap, the members' pitch, the colours |
| `page_backdrop FILE` | the Preferences page's picture |
| `page_tabs X Y W H PITCH`, `page_rows X Y W H PITCH SHOWN`, `detail_pitch N` | its tabs and rows |

It shows the options and the one selected, its description, the character's
face, name and numbers, the trials, money and heart points, the group's members
and where each is, the game's line over the menu (the host choosing a mission,
START to join or begin), and the Preferences page in the model's and panel's
place, the game's fade over all. Title Screen's question is the port's "Return
to the main menu?" with DDD's Yes and No buttons and the game's words for them.

## MODO CINE's list (`layouts/cine/<lang>/cine.txt`)

Written by `tools/hd/theater_cine.py`, in the theater's pieces; drawn by
`cine_screen.c`.

| Line | What it is |
|---|---|
| `rows Y PITCH SHOWN` | the first row's top, the rows' pitch, how many show |
| `digits X Y H PITCH R G B` | the day's digits: the last's middle, top, height, pitch, colour |
| `scene MOVIE DAY ART` | each movie's chapter day and artwork (`layouts/cine/art/ART`) |

The rows are the game's page, each its scene's name and length
(`UI/thr/thr_&.s.z`); a day's first scene shows the theater's square and DAY.
Behind them the selected scene's artwork, read on a thread of its own, fades in
when the selection changes.

## The rooms (`layouts/rooms/<lang>/rooms.txt`)

Written by `tools/hd/theater_mission.py`, in the theater's pieces; drawn by
`room_screen.c` from `runtime/net/roomlist.c`'s list (`MULTIPLAYER.md`).

| Line | What it is |
|---|---|
| `fixed TOP PITCH` | the list's first rows, Create a Group and the search |
| `rows TOP PITCH SHOWN` | the rooms under them, SHOWN at a time with a scroll bar |
| `form TOP PITCH` | a new room's rows (name, password, Create) or a password asked for |
| `cards PITCH` | a room's places |
| `frame THERE NOBODY` | a card's ring |
| `signal PITCH H1 H2 H3 H4` | a room's signal, four bars |
| `signal_ms A B C` | the pings under which four, three or two bars are lit (one above C, none unmeasured) |
| `art FILE` | the background |

Each row is a theater chapter's: a room's signal, name, host, "Password",
players and ping; a field's text (a password as asterisks) and label; an
action's words. The members' screen shows a card for each of the four places,
ringed in the place's colour when a player is there, and the host's Proceed lit
when the game would go on. The game's words are its own list's
(`UI/mlt/mlt_&.s.z`), the rest the port's (`ui_text.h`). Typing: the keyboard
through the system's text input; a gamepad as the settings menu's text entry
(up and down change the last character, right adds one, left erases one).

## Mission Mode's character select (`layouts/select/<lang>/select.txt`)

Written by `tools/hd/theater_mission.py`, in the theater's pieces; drawn by
`select_screen.c` from what `runtime/menus/charselect.c` reads.

| Line | What it is |
|---|---|
| `grid COLUMNS GAP ROWS` | the grid's columns, the tiles' gap (from place `tile`) and a page's rows; without ROWS (layouts made before the pages) the rows that fit above the places' plates |
| `stats PITCH` | the gauges' rows' pitch (from place `stat`) |
| `gauge_pitch PITCH` | a gauge's bars' pitch (from place `gauge`) |
| `gauge_colour R G B` | the bars' colours, left to right (the game's) |
| `plates PITCH` | the places' plates' pitch (from place `plate`) |
| `frame PLACE OTHERS` | a tile's frame: with a place on it, without |
| `art FILE` | the background |

It shows the characters the game lets be chosen, in its order, grey unless a
place is on one (then in colour, framed in the place's colour); this copy's
character large with its name, weapon and gauges; each place's plate along the
bottom, ringed once its player has chosen; the game's message and help. The
grid ends with a random choice, a "?" with the player's Y in its corner: A on it
(or Y anywhere, also on the game's own screen without these pieces, whose help
line then says "Y Random" with the game's own Y, 0x334d in `text/font_eu_10.nftr`,
which its text never writes) moves to a character nobody is on, at random, and
chooses it.

When the characters and the random choice do not fit a page (`grid`'s COLUMNS x
ROWS: the game's 19 and the random choice fill four rows of five, so a mod's
characters past the first make more), the grid is paged
(`runtime/menus/select_pages.c`): the page shown is the one the cursor is on, L and
R turn it to the same place on the next or previous page (its last character when
the page is shorter), the directions cross pages (down from a page's last row onto
the next page's first, left and right along the whole order), and `text page`
(else the places' circles' style under the grid) says "2 / 3". The random choice
stays last, on the last page, and draws from every page. The places' plates show
every place's character whatever page is shown. With everything on one page the
screen is as it always was.

## The settings menu (`layouts/settings/<lang>/settings.txt`)

Written by `tools/hd/theater_settings.py`, in the HD collection's own menu's
pieces; drawn by `settings_skin.cpp` where the menu draws its own rectangles.
Places are the pieces' picture's (1920 x 1080), its width mapped onto the
window's width and its height onto the window's height.

| Line | What it is |
|---|---|
| `picture W H` | the picture the places are in (1920 x 1080) |
| `atlas FILE` | the pieces' pixels, beside this file |
| `font FILE CELL` | the font's picture (its glyphs' grey), its cells' height |
| `screen X Y W H U V` | the screen behind the menu: its place, its texels |
| `part PIECE STATE ROLE X Y W H U V` | a piece's part in a state: its role (left, middle, right, top, bottom, whole, title; the slider's track, fill, knob), its place in the picture, its texels |
| `glyph CODE X Y W` | a character's cell in the font's picture and its width |

A piece of a left, a middle and a right part has its ends scaled with the
rectangle's height and its middle stretched between them (its other states
placed as they lie against its shown one); a stacked one likewise upright; one
of a whole part is stretched over it.

## The HD videos' music

Written by `tools/hd/theater_movies.py --music` as
`data/cutscenes/cinematics/music/hd<id>.txt`, beside the tracks it names
(`bgm_<n>.ogg`, Ogg Vorbis); played with the HD video by
`host/movies/theater_music.c`, at the video's clock and the Music volume.

| Line | What it is |
|---|---|
| `rate R` | the video's frames a second (30) |
| `from field\|stop` | `field`: the music comes from the video before (it plays on when that one's track is this one's first, from frame 0) |
| `to field\|stop` | `field`: it goes on into the next video of the same course |
| `cue FIRST END TRACK LOOP` | `TRACK` plays from its start at frame `FIRST`, to before `END`; past its end it goes back to sample `LOOP` (-1: it plays once) |
| `volume FIRST END LEVEL` | the music's level moves to `LEVEL` percent between the two frames, from the level before (100 at first) |

A mod's video (`mods/<Mod>/movies/` or `cutscenes/cinematics/`) has music only
from a `music/` folder beside it.

## The HD videos' subtitles

Written by `tools/hd/theater_movies.py --subtitles` as
`data/cutscenes/cinematics/subtitles/<lang>/hd<id>.txt` with its atlas; drawn
over the HD video by `host/movies/movie_subtitles.c`.

| Line | What it is |
|---|---|
| `picture W H` | the picture the places are in (1920 x 1080) |
| `rate R` | the frames counted a second (the theater's videos: 30) |
| `atlas FILE` | the lines' pixels, beside this file |
| `line FIRST END X Y U V W H` | a line: shown from frame FIRST to before END, at X, Y of the picture, from the atlas's U, V, W x H |

A frame of the schedule is a picture of the theater's video: a video at that
rate, or at 30000/1001 (the same pictures, slower), counts its own pictures;
another counts the time at the schedule's rate.

## Making them (`tools/hd`)

Each tool reads the player's own copies -- nothing of the collections is in the
port -- as OpenKh's extractor writes them: MARE is the 358/2 Days theater of
KINGDOM HEARTS HD 1.5+2.5 ReMIX (`<collection>/Image/Mare.hed_out/original`;
Mare.pkg is the theater), SETTINGS the same collection's settings menu
(`SettingMenu.hed_out/original`), DDD Dream Drop Distance's files from KINGDOM
HEARTS HD 2.8 Final Chapter Prologue (`kh3d_first.hed_out`, from
`OpenKh.Command.IdxImg hed extract <2.8>/Image/dt/kh3d_first.hed`). `--data`
is the port's extracted game data (found by default), `--mod NAME` the mod
(`Theater title` by default), `--lang` the languages, `--preview DIR` draws
the states as the port does. The installer runs them for the HD pack. Where the
words are the game's, they are read from the game; the places, sizes and colours
not in the collections' layouts were measured on the maintainer's captures or
are the maintainer's choices from mock-ups.

The native KH2 gauge is made by `kh2_gauge.py KH2_FIFTH` (default mod: `HD PACK`).
Its input is the existing archive reader's `kh2_fifth.hed_out`, with `original/`
and `remastered/`. The installer extracts its five localized field archives,
creates the adjusted `.2dd` and combined player/party `gauge.json`, and copies the remastered atlas into
`layouts/gauge/<lang>/`. `--settings` names an existing gauge folder whose custom
animation and position settings should survive rebuilding, including migration of
an older `party.json`. The gauges are rendered
as native animated sequences; see [KH2 sequences](KH2_SEQUENCES.md).

The theater's layouts are L2D files. Their structures are those OpenKh documents
for Birth by Sleep (openkh.dev/bbs/file/type/l2d.html); this version (0500)
differs where its data says so: sequences of 16 bytes, animations of 28 bytes
with 12 key counts (status, base x, base y, -, offset x, offset y, -, rotation,
-, scale x, scale y, colour), and a node's parent counted from its layout's
first node. A node's control has a max frame, a return frame, a first animation,
`once` and its animations: past its max frame a control with `once` 0 plays
again from its return frame (the theater's options' light, 180 frames; DDD's
selected button's, 60), one with 1 holds its last frame (the appearing
controls; starting one again made the background blink).

### The title (`theater_title.py MARE`)

Into `layouts/title/<lang>/`: `atlas.png` and, for each state of the title's
menu, `<menu>_<count>_<row>.txt` (animated pictures, `COMPOSITIONS.md`), shown
on one screen while the game's title menu shows; `intro_0_0.txt`, the white
ground alone, for the menu's fade-in; `logo_3_0.txt` to `logo_3_2.txt`, the
theater's own boot screens (Disney, SQUARE ENIX, its copyright) for the game's
three boot logos. It draws the theater's main menu, layout 113 of
`<lang dir>/l2d/days_theatre.l2d` -- its logo, key art, "DEVELOPED BY SQUARE
ENIX", its options' bars, its settings key -- with the game's options in place
of the theater's, written in the theater's menu font (`<lang dir>/font/menufont`),
the option the game has selected drawn as the theater draws its selected one,
its light pulsing; the theater's own extras are left out. Layout 113's option
nodes each have two word parts (black and white, in the selected sprite) and a
word part when not selected; control 0 draws an option selected (the orange
square, its bar and black word, its light growing and shrinking over 180
frames), control 3 grey. The layout's groups appear with their control 1 (from
nothing over 44 frames: the key art's by frame 16, the others' from 12 to 44)
and stand still with control 0. On the title's arrival every group appears so,
on a change of level the options' bars, then the selected option's loop; which
animation goes with which moment is the port's choice (the data has the
animations, the theater's program the moments). The settings key's bar opens the
port's settings when clicked (`button`) and shows the gamepad's button for them
(`prompt`, from the theater's button icons, `<lang dir>/font/Icon.tm2`).

### The new game's dialogs (`newgame_dialog.py DDD MARE HAND`)

HAND is the pointing glove, from KINGDOM HEARTS II's title sheet
(`remastered/menu/fm/title.2ld/UK_title_2ld0.png`; DDD's own is not among
kh3d_first's textures). Into `layouts/title/<lang>/`: `newgame_atlas.png` and,
for each state, `newgame<options>_<step>_<row>.txt`: options 1 (NEW GAME alone)
or 2 (with CONTINUE); step 1 the difficulty (row: the one selected), 2 the
subtitles (row: 0 yes, 1 no), 3 the settings to confirm (row: difficulty x 4 +
subtitles x 2 + the button, 0 yes). Each opens as DDD opens its dialogs (the
window's control 1) and holds, the selected button's border pulsing (control 4,
60 frames), over the theater's menu as the story level showed it. They are DDD's
new game dialogs (`menu/en/title/title_menu/bin/title_menu_n.l2d`, its HD texture
`US_title_menu.dds` at twice the CTRT's 512 texels): layout 1 the shade over
the title, 2 the heading's tab, 3 the window with three buttons, 5 with two, 7
the settings with their rows; DDD's own words (`message/<lang>/system/bin/
ct00000.ctd`, `ct00200.ctd`) in its help font (`font/en/bin/helpfont.bcfnt`),
where its nodes anchor them; their sizes, colours and places measured on
captures of DDD's own dialogs.

### The settings menu (`theater_settings.py SETTINGS`)

Into `layouts/settings/<lang>/`: `settings_atlas.png`, `settings_font.png` and
`settings.txt`. The collection's menu (`<lang folder>/l2d/setting_1525.l2d`, the
folders `sp uk fr gr it` for es en fr de it): layout 100 the screen (101 its
ground, 102 the heading's tab with the menu's name and the band's lines, 103 the
faded pictures of its sections, of which the menu shows one), 150's node 181 the
line between sections and rows; 700 a section's tab, 750 a row's capsule, 850 a
switch's (862 its check), 950 a number's (962 its slider: track, ends, fill,
knob), 1020's 1027 and 1028 a choice's arrows, 650 the question (661 its window,
662 and 663 its answers), 1010's 340 the hints' bar, 400's 411 (control 4) the
gamepads' pictures, 350's 361 the keyboard's and 300's 329 the mouse's. A node's
controls: 0 shown, 3 dimmed, 4 selected (a section's tab: 4 moving right, 5
flashing, 6 chosen, 18 texels right of its place). The words in its menu font
(`<lang folder>/font/menufont.tm2` and `.cod`). Days's colours (the new game's
dialogs' tones): every colour as the grey of its brightness, the selected
pieces' gold red and their bright rim silver, the switch's check red as it is.

### The save list (`save_screen.py DDD MARE`)

Into `layouts/save/<lang>/`: `save.txt`, `save_atlas.png`, `save_font.png`.

- DDD's save screen (`menu/<lang>/camp/save/bin/save00_n.l2d`; its blocks' HD
  textures `<XX>_save00.dds` at the CTRT's size, `<XX>_save_gratext_00.dds` and
  `<XX>_save_cnt_00.dds` at twice it): layout 1's node 2 the screen behind
  everything and node 500 the heading's tab; layout 5's node 210 the list's
  window, 5555 its scroll bar's track and 5556 its thumb; layout 4's node 2 a
  file's row (control 0 shown, 4 selected) and node 14 the crown of a story seen
  to its end; layout 7's nodes 2 and 3 a question's yes and no (control 4
  selected); the gratext texture's words LOAD, SAVE, MUNNY and TIME in each
  language.
- In Days's tones: the screen in greys, the selected row's pink as red, the
  number's circle silver, the selected button's red kept darker, its ring silver.
- DDD's help font for the port's words (the game's own words are read from the
  game as it runs), and DDD's input icons (`font/<lang>/bin/iconfont_n.ctt`, its
  HD sheet): an Xbox pad's, a PlayStation's and the keyboard's keys, each
  language's key names its own; other pads show the port's Switch icons.
- The theater's hand at the selected row or button, as the theater's lists and
  the camp have it.
- Days's faces: the three plates the DS's list draws a file with
  (`UI/cm/cmo_&.p2` entry 3, cells 48-50: Roxas, Axel, Xion -- field 0xc4b, rolled
  at each save), their faces' end fading in; and the guest's row's words from
  the same characters.

The rows' and the screen's pieces are DDD's places in its 1920 x 1080 picture;
the words' places, sizes and colours are the port's (the mock-up's), and a mod
may move them (the file's `place` and `text` lines).

### The camp menu (`ddd_camp.py DDD MARE SETTINGS`)

Into `layouts/camp/<lang>/`: `camp.txt`, its atlas and font, `camp_backdrop.png`
(the picture behind the game's 3D) and `camp_page_backdrop.png` (the Preferences
page's: one panel in the place of the model's box, the trials and the status
panel). DDD's camp menu (`menu/<lang>/camp/comn/comn/bin/camp00_n.l2d` and its
HD textures) with its blues made Days's greys: its sides' shade (layout 3), its
top band (layout 1 nodes 1 and 43), its bottom band with the heart (layout 14
node 2), its title's pill in red (layout 1 node 10) with its word MENU (node 11),
its command bars (layout 11, node 100: control 0, and 4 for the one selected),
its portrait frame's inner panel (layout 23 node 16 without its checks) as large
as the frame, its pink edge red, where the game's own 3D model shows; a bar of
layout 11 node 700 for the trials; its status panel (layout 17 node 10, without
its scroll bar); its money's word in white (layout 14 node 401) and big digits
(`cnt_00`); its settings key (layout 1 node 2: Esc and the gear). The theater's
face of the character played; the DS's trial symbol (`UI/srslt/res.p2` entry 2
cell 45); DDD's save screen's crown; DDD's help font. The words are the game's
(`UI/cm/str/status_&.s`, `root_&.s`, `select_&.s`; `UI/mlt/mlt_&.s`) but for the
options, which the game writes only in pictures (`UI/cm/cmo_&.p2` entry 1, cells
14, 15, 22, 23, 24, 18 and 19 for Mission Mode's seven): OPTIONS transcribes them.
The Preferences page: the status panel stretched to one panel, its ends kept;
the settings menu's section tabs (layout 700 node 701, controls 0 and 6) and rows
(750 node 751, controls 0 and 4) with its arrows (1020 nodes 1027 and 1028),
each at its size as the settings menu draws them, in Days's tones; DDD's save
screen's scroll thumb when a section's settings do not fit. The words from
`UI/cm/str/cfg_&.s`.

### MODO CINE's list (`theater_cine.py MARE`)

Into `layouts/cine/<lang>/` (`cine.txt`, `cine_atlas.png`, `cine_font.png`) and
each scene's background into `layouts/cine/art/`. It draws the theater's chapter
list, layout 118 of `days_theatre.l2d`, at the size the theater draws it (its
texels are the 1920 x 1080 picture's pixels, matching a capture to the pixel),
with the game's own list in it (`Ov000_FillListRows`, `Ov000_DrawListScene`):
its 28 scenes, ten to a page, the game's cursor in the theater's rows (the first
in the place of its "play all" bar); each scene's name and length
(`UI/thr/thr_<lang>.s`, records 1-28 and 29-56; record 0 the help, in the
theater's help bar) in the theater's menu font; the day as the theater's
chapter has it (its DIA and digits, tinted as it tints them, on a day's first
scene, its dark square on those rows only); the collection's launcher's glove
(`launcher.l2d` part 65); and behind it the artwork of the scene's chapter
(`bg/artwork/<name>.tm2`, node 106) under the theater's veil (node 700). Sizes
and places not in the layout were measured on the capture (`MEASURED`).

The theater's own data, undocumented elsewhere: `data/DaysTheater.thd` ("THD@",
u16 count at 6, 28-byte records from 0x10, then strings; a record's u16 at 8,
12 and 16 are the offsets of its event's file, its video's and its music's)
lists the scenes; `data/DaysTheater.chp` ("CHP@", u16 count at 6, 96-byte
records from 8, then strings; a record's u16 at 2 its day, 8 its artwork's
offset, 32 its scenes' count and from 36 their THD indices) the chapters. A DS
scene is the THD scene whose event is `hd<movie>/` (the DS's movie numbers:
`thr.p2`'s entry 7); its chapter the first holding that scene at or after the
chapter of the scene before it (818 is in two); 843, the ending, is in none and
takes the one of the theater's ending (`DED.mp4`).

### Mission Mode's screens (`theater_mission.py MARE`)

Into `layouts/select/<lang>/select.txt` and `layouts/rooms/<lang>/rooms.txt`,
each with its atlas and font, and `select/select_art.png`, the background of
both. The pieces: each character's face (`bg/face/<name>.tm2`, 256 x 256;
`FACES` says whose is whose, as seen on them) in colour and in grey, as the
game's own grid shows the characters nobody is on in grey (`UI/mlt/res.p2` entry
2: cells 5-23 and 24-42); the theater's hooded figure (`UNKNOWN`) for a room's
players, who have no character yet; the chapter list's rows, square, scroll bar
and help bar and the launcher's glove; its veil over one of its artworks; its
event font and button icons; the game's gauge colours (`res.p2` entry 3, cell
14). Where they go is the port's own layout (`SELECT_*`, `ROOMS_*`), one screen
of 1920 x 1080, the maintainer's choices from mock-ups.

### The pause (`hd_pause.py PIECES FONTS`)

PIECES is a folder of the player's own pictures from their copies of the KH
games: `pause_label.png` (the title's two bars without a word, 1024 x 256),
`select2.png` (a line's capsule, 1024 x 193), `command2.png` (the same lit, red)
and `select.png` (the hand at the lit line); FONTS Kingdom Hearts Re:Collection's
Fonts folder (Televo's free fonts: KHGummi.otf for the title, KHMenu.otf for the
lines). Into `layouts/pause/`: `hand.png` and per language `title.png`,
`question.png` and each line's `<key>.png` and `<key>_lit.png`. The words are
the game's (its `pause/etcicon.NSCR` strips over `pause/pausebg_&.pbg`) and the
port's Retry.

### The videos, subtitles and music (`theater_movies.py MARE`)

`--videos`: the theater's videos as the port plays them in the game's movies'
place (`data/cutscenes/cinematics/hd<id>.mp4`): `hd8NN.mp4`, `DOP.mp4` as the
opening's (802: the same length, 299.7 s), `DED.mp4` as the ending's (843: the
ending and its staff roll, 382.4 s, played in place of the movie and the roll
both; `CED.mp4` is Re:coded's). Their first 0x100 bytes are XORed with a 16-byte
pattern (it shows where the movie header's bytes are zero); the rest is a plain
MP4. `--subtitles`: each language's subtitles as the theater draws them, from
the event file the theater keeps per video (`<lang folder>/event/<lang>/hd/
hd<id>/hd<id>.exia2`, XML: a `SCHEDULE_TEXT_MOVIE` per language, `DATA
FrameStart FrameEnd TextID`, 30 frames a second; the ending's is event hd844,
whose sound is `DED_MONO`), the words of its message tables (`<lang folder>/
message/<lang>/event/hd/bin/cthd*.ctd`, TextID = group << 16 | id) and its
event font (`eventfont.tm2` and `.cod`, the glyphs outlined in black). Their
size and places were measured on window captures of the theater playing its
videos with subtitles (hd818's two lines, the 1998 x 1125 pictures scaled to
1920 x 1080): the font's 32-texel cells drawn 1.445 times their size, a space
13.75 texels, each glyph advancing its width in the `.cod`; the lines 51 apart,
the last one's top at 956, the block centred on 964.
