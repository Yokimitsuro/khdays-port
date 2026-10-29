# Scripting gameplay mods

A mod can change how the game plays with scripts in Lua 5.4. The port runs each scripted mod in its
own sandboxed interpreter, on the game's thread, once per game frame and when the game does something
a script listens to. Scripts read and change the game's state by name: the names below stay the same
across port updates of the same interface version. For what the names do not cover, version 2 also
reaches the game's memory by address ("The game's memory", below).

Content mods (textures, models, 2D, sounds, texts, movies, rooms) are unchanged by this; a scripted
mod may carry content too.

## A mod's folder

```text
mods/<folder>/
├── mod.ini              # what makes it a scripted mod
├── scripts/
│   ├── main.lua         # run once when the mod is loaded
│   └── <name>.lua       # require("name"); require("a.b") is scripts/a/b.lua
├── data/                # text files the mod reads (optional)
└── ...                  # content, as any mod (optional)
```

`mod.ini`, `key = value` lines (`#` starts a comment):

```ini
id = example.infinite-hp
name = Infinite HP
version = 1.0
author = khdays-port
kind = gameplay
api = 1
description = Keeps the player's HP full.
```

| Key | | |
|---|---|---|
| `id` | required | letters, digits, `.`, `_`, `-`; no two mods may share one |
| `name`, `version` | required | shown to the player; versions are compared as text |
| `kind` | required | `gameplay` (it changes the game) or `visual` (it only listens: its changes are refused) |
| `api` | required | the interface version it needs: `1`, or `2` for version 1 and what a game mode of its own needs (below) |
| `memory` | optional | `read` (when not given) or `write`: whether its scripts may write in the game's memory (version 2's `memory`); the Mods page tells the player a mod that does |
| `main` | optional | the first script, under `scripts/` (default `scripts/main.lua`) |
| `author`, `description` | optional | shown to the player |

A folder whose `mod.ini` is missing a required key, asks for a newer interface, repeats another mod's
id or has no main script is not loaded, and the reason is logged.

Mods load in the order kept in `saves/mods/order.ini` (`<folder> = on|off`, one line each); a mod
not listed yet comes after the others, on. Events reach the mods in that order: when two mods change
the same thing in a frame, the later one's change stands. Every folder in `mods/` is listed there,
scripted or not: a mod of files alone (models, textures, sounds, texts) is turned on and off from the
settings menu's Mods page like a scripted one, and a file two mods both replace comes from the earlier
one. A mod is also off, whatever the file says, when its folder holds a file named `.ignore` or its
folder's name begins with a dot: a way to turn one off by hand, in a file manager. A scripted mod's
scripts follow a switch at once; the files, sounds, characters, missions and native plugins the game
reads are those of the mods on as it started, so turning or moving a mod changes them at the next start.

## What a script may use

`string`, `table`, `math`, `utf8`, `coroutine`, the base functions (`load` of text only, without
`dofile` and `loadfile`), `require` of the mod's own scripts, and `os.time`, `os.clock`, `os.date`,
`os.difftime`. Nothing else: no files beyond the mod's own, no programs, no native code, no network.
Metatables cannot have `__gc`.

Each mod may hold 64 MB; past that, an allocation fails (`not enough memory`). Each mod has a time
allowance per game frame (8 ms; its main script has 250 ms when it loads). A mod past its allowance
is paused, even inside its own `pcall`; an error in a script stops that mod only. Either is written
to the mods' log with the script's file and line, the player sees it over the game for five seconds
(and on the settings menu's notice line), and the game and the other mods go on. The Mods page
resumes a paused mod.

For a measure: ten mods each reading the player's HP, munny and 64 item counts and sorting a table
every frame (`port/tests/mods_load`) took 45 microseconds each a frame on average, 0.2 ms at most,
and the game kept its 30 frames a second.

A known limit: the time is looked at between the script's own instructions, so a single library call
that takes long (a string pattern over a very long text) is only stopped once it returns.

## The mods' log

`saves/logs/mods.log` holds a line per thing each mod printed or met (the last run's is
`mods.prev.log`):

```text
12.345 example.infinite-hp loaded - started, api 1
13.002 example.where print scripts/main.lua:8 mission 10 at 12.50 0.00 -3.25
20.410 example.broken error scripts/main.lua:3 attempt to index a nil value (global 'plyer')
20.410 example.broken stopped - the error above
```

The same lines go to the session log after `mods:`.

## Trying a change while the game runs

The game looks at each mod's files once a second: when `mod.ini` or a script changed, that mod is
loaded again (its state closed, its main script run again; its options' values and its own files
are kept). A stopped mod comes back the same way once its script is fixed. khdays-studio's Console
(the button beside Themes) follows `mods.log` as it grows, by mod and with errors only, and its
Reload asks the game to load a mod again (a line in `saves/mods/reload.txt`, read within a second).

## The interface, version 1

Calls that change the game return `true`, or `nil` and a reason (`"visual mod"`, `"guest"` in a
multiplayer session that the player does not host, `"no game"` before a game is started or loaded,
or the call's own); they never raise. A wrong argument type raises an error (the mod's own bug).

### `mod`

| Name | |
|---|---|
| `mod.id`, `mod.name`, `mod.version` | from `mod.ini` |
| `mod.on(event, function(e) ... end)` | listens to an event (below); returns a handle |
| `mod.off(handle)` | stops listening |
| `mod.frame()` | the game's frame count |
| `mod.notice(text)` | a line the player sees over the game for five seconds, with the mod's name |
| `mod.read(path)` | a text file of the mod's: its `data/<path>`, else what it wrote; `nil` and a reason if neither |
| `mod.write(path, text)` | keeps a text file in `saves/mods/<id>/<path>` (its folders made); paths stay inside: no `..`, no absolute path, no drive, no backslash |
| `print(...)` | a line in the mods' log |
| `mod.option{...}` | an option in the settings menu's Mods page (below) |

### Events

Each handler gets one table, `e`, with `e.event` its name and the fields below.

| Event | Fields | When |
|---|---|---|
| `frame` | `frame`, `paused` | every game frame, once the game's logic of the frame is done (the pause included: `paused` is true while the game's objects stand still) |
| `scene` | `from`, `to`, `argument` | the game changed scenes |
| `title` | | the title scene was entered |
| `mission_start` | `mission` | the field began: a mission (again after Retry), or in Story Mode the Grey Area, where `mission` is whatever the game holds then |
| `mission_end` | `mission`, `rank`, `given_up`, `complete` | the mission's results were recorded (after a give-up too: `given_up` true; a give-up from Mission Mode's pause had `rank` -1); `complete` only in Story Mode |
| `hp` | `who` (`"player"` or `"other"`), `before`, `after`, `max` | an HP changed in battle (`after < before`: damage) |
| `item` | `item`, `count`, `change` | an item's count changed (picked up, bought, used, given) |
| `munny` | `total`, `change` | the munny changed |
| `menu_open`, `menu_close` | `menu` (`"pause"`, `"camp"`, `"settings"`) | the pause, the camp or the port's settings menu |
| `save` | | the game saved |

What a handler changes is seen by the game's next frame. An event the game raises while a script runs
(a change a script made itself) reaches the mods once that script has returned.

### The game's state

| Name | | Limits |
|---|---|---|
| `game.munny`, `game.set_munny(n)` | munny | 0 to 999999 |
| `game.item(id)`, `game.set_item(id, n)` | an item's count | ids 0 to 2255 (`0x8cf`), counts 0 to 99; a count above 0 marks the item owned |
| `game.flag(n)`, `game.set_flag(n, on)` | a story or mission flag | 0 to 16383 (`0x3fff`) |
| `player.hp`, `player.max_hp`, `player.set_hp(n)` | the player's HP in the field | `set_hp` clamps to 0..`max_hp`, as the game's own hits and cures do (0 is a defeat); `nil` / `nil, "no player"` elsewhere, and `nil, "not now"` while the field is still loading (the HP is there before the gauge that shows it) |
| `player.position()` | `x, y, z` in the field | `nil, "no player"` elsewhere |
| `mission.id` | the current mission | `nil` outside the field |
| `scene.id` | the current scene | |

### Actions

| Name | Does | Refused |
|---|---|---|
| `game.give_item(id, n)`, `game.take_item(id, n)` | adds or takes `n` (default 1) of an item, its count kept within 0..99, as the game's rewards do | `"unknown item"`, `"no game"` |
| `game.play_sound(archive, entry)` | plays a sequence archive's entry by its names in the sound archive (`"COMMON_SYS"`, `"se_sys_open"`; the session log names each sound the game plays as `seqarc/<archive>/<entry>`) | `"unknown sound"` |
| `game.show_text(text)` | the field's notice (`Ov002_ShowNoticeText`); the game shows the first notice of a field only, so a mission that has shown its own refuses it | `"not in the field"`, `"busy"`, `"not a text the game can show"` (control characters, characters beyond the first Unicode plane) |

A visual mod and a guest may play sounds; the other actions are refused to them. Whether the game's
font has every character of a notice is not checked.

### Options in the Mods page

The settings menu has a Mods page while a scripted mod is present: each mod with its state (running,
off, stopped by an error, paused, not loaded and why), whether it writes in the game's memory, a
switch to turn it on or off, its place in the load order, Resume for a paused mod, and its options
while it runs. The pause and the camp have no
mod entries; the settings menu opens from anywhere with its own key.

```lua
local on = mod.option{ key = "on", kind = "toggle", default = true,
                       label = { en = "Infinite HP", es = "PV infinitos" },
                       description = "The player's HP back to its maximum every frame.",
                       on_change = function(v) print("now", v) end }
mod.option{ key = "speed", kind = "choice", label = "Speed", values = { "Slow", "Normal", "Fast" }, default = 2 }
mod.option{ key = "boost", kind = "number", label = "Boost", min = 0, max = 10, step = 2, default = 4 }
mod.option{ key = "hello", kind = "button", label = "Say hello", on_change = function() print("hello") end }
-- on.value: true or false, as the player left it
```

| Field | |
|---|---|
| `key` | the name its value is kept under; one per mod |
| `kind` | `toggle` (true / false), `choice` (the value's place in `values`, from 1), `number` (whole `min`, `max`, `step`), `button` |
| `label`, `description` | a text, or a table by language (`en`, `fr`, `de`, `it`, `es`); English is the fallback |
| `default` | the value until the player picks one |
| `on_change` | called with the new value when the player changes it (a button: when pressed) |

`mod.option` returns the option: `option.value` is its value now, `option:set(v)` changes it from the
script (kept like the player's change; `on_change` is not called). The values are kept in
`saves/mods/<id>/options.ini` and are back at the next start; a kept value the option no longer
takes goes back to its default. What the player changes in the menu reaches the mod when the game
runs again.

### Not in version 1

What the decompilation does not explain yet, or what was not yet seen working in the game, is left
out rather than guessed: the field's current area, a loaded save's event, the mission's chests.
Making the field's enemies, holding the player's defeat (with who dealt the hit), how a mission
ended, starting and leaving missions, placing the player, title entries and screens, the deck,
timers, the game's music and a room's shared draws are in version 2.

## The interface, version 2

A mod whose `mod.ini` says `api = 2` has everything of version 1 and the names below; a version 1 mod
has none of them (`enemies` is `nil`, `mod.on` refuses their events). Their calls answer as version
1's do, and also `"not in the field"` where they act only there.

### `enemies`

The field's enemies, made by the game's own routine from the mission's enemy table: an area that has
enemies gives some of the table's kinds a place, and a mod makes those kinds and any other of the game
whose code still has room. The game keeps the enemies' code in six places, and each kind fits only some
of them: a kind is loaded as its first enemy is made and keeps its place for the field, so the kinds a
mod can add depend on the area's and on those already made (`enemies.kinds("all")` says which are
ready); past the six the game would stop, so the port refuses first. A mod's enemies
fight as the mission's, give none of the mission's tallies or drops (the mod rewards), and are let go
with the area: a mission that moves on to its next area takes them away (`enemies.list()` empties)
and offers that area's kinds.

| Name | Does | Returns / refused |
|---|---|---|
| `enemies.kinds()` | the kinds this area makes | a list of `{kind = <id>, name = <the enemy's name>}`; empty where the area has no enemies |
| `enemies.kinds("all")` | every kind of the game, `ready` when one can be made here now | a list of `{kind, name, ready}` |
| `enemies.make{kind, at, count, spread, level, hp, strength}` | makes `count` (default 1) enemies of `kind` at `at` (`{x, y, z}` in the field's units, or `"player"`), round it at `spread` units (default 0), entering as the area's own of that kind do (once its spawners have run; else as the game makes them, a Shadow rising from the ground); their level the area's for that kind (the mission's for another kind) plus `level` (default 0; the game's levels are 1 to 120), their HP and the power of their attacks times `hp` and `strength` (default 1) | a list of handles (and the reason, when fewer than `count` were made); `"unknown kind"`, `"no room for its kind"`, `"no enemies here"` (an area without enemies), `"not in multiplayer yet"` |
| `enemies.remove(e)` | sends enemy `e` (a handle, or a list of them) away as the game does when the player leaves its spawner: no defeat | `true`; `"gone"` |
| `enemies.list()` | the handles of the mod's enemies still standing | |
| `enemies.info(e)` | `{kind, hp, max_hp, level, x, y, z}` | `nil, "gone"` |
| `enemies.hold_mission(on)` | while `true`, the mission's own enemies are not set going (their spawners wait), so its areas never end by their enemies and a mod's waves are the only ones; those already standing stay | `true`; `"held by <mod id>"` while another mod holds them |

A handle is the mod's own number for an enemy it made, unique for the session; once the enemy is
defeated, sent away or let go, it is gone. An enemy that was defeated or sent away is idle in the
area again and is reused for the next one of its kind, so waves cost no more of the game's memory
than the most that stood at once.

| Event | Fields | When |
|---|---|---|
| `enemy_appeared` | `enemy`, `kind` | an enemy the mod made is in the field (to that mod only) |
| `enemy_defeated` | `enemy` (the handle; `nil` for the mission's own and other mods'), `kind`, `mine` | any enemy is defeated |

```lua
-- a wave of three Shadows round the player, tougher, once the mission's first area is built
mod.on("frame", function()
  if #enemies.list() == 0 and #enemies.kinds() > 0 then
    enemies.make{ kind = enemies.kinds()[1].kind, at = "player", count = 3, spread = 4, hp = 2 }
  end
end)
mod.on("enemy_defeated", function(e)
  if e.mine then game.give_item(1) end
end)
```

### The run's end

| Name | Does | Returns / refused |
|---|---|---|
| `player.take_defeat(on)` | with `true`, the hit that would defeat the player leaves 1 HP and the mod hears `defeat` instead of the game over (what the game itself does for a host's partners in Mission Mode); `false` gives it back to the game | `true`; `"taken by <mod id>"` while another mod holds it |
| `player.set_position(x, y, z)` | puts the player there (the field's units, as `player.position()` gives them), the way the game's scripts place a player: its motion stops with it; the ground and the walls are the mod's to choose (an arena's edge keeps the player inside by putting it back) | `true`; `"not in the field"`, `"not now"` (before the battle begins), `"guest"` |

One mod holds the defeat at a time, the first that asks; a mod stopped by an error, turned off or
reloaded gives it back.

| Event | Fields | When |
|---|---|---|
| `defeat` | `damage`; `enemy` (the handle, when the mod made the attacker); `kind` (the attacker's kind when an enemy hit, `nil` for a fall or another damage of no enemy's) | a hit would have defeated the player while this mod holds the defeat (to that mod only) |
| `mission_end` | version 1's, and `ending`: `"cleared"`, `"abandoned"` or `"lost"` | as in version 1 |

`ending` is `"lost"` when the run ended in the game over after the player fell, `"abandoned"` after
a give-up from the pause, `"cleared"` when the results record the mission cleared (a story mission
with its objective done, a Mission Mode one ranked), `"lost"` otherwise. In Mission Mode the field
revives a fallen player a few times before the game over: a mod that wants every fall holds the
defeat.

### Missions

| Name | Does | Returns / refused |
|---|---|---|
| `mission.start(number)` | starts mission file `number` (`mi/mi/<number>`, as the studio numbers them: any mission, its field, its areas and enemies) from Mission Mode's camp: the port takes the camp's own way in for the player -- Holo-Missions, the first mission listed, its Yes, START -- holding the player's buttons back for those few seconds, and the field is built with the mod's mission | `true` (it begins a few seconds later: `mission_start` tells it); `"unknown mission"`, `"not in a camp"` (Mission Mode's), `"not now"` (a start already on its way), `"not in multiplayer yet"` |
| `mission.leave()` | ends the mission as the pause's Give Up does: to the results (`ending` `"abandoned"`), then the camp | `true`; `"not in the field"`, `"not now"` (paused, already leaving, or the mission's opening before its battle) |

The results of a mission a mod started count for the camp's mission it went in by (its name on the
results screen, its records); a mode of a mod's own shows its own end and leaves with
`mission.leave()`. A mission file the game made for the story may expect the story's state.

### Title entries and screens

| Name | Does | Returns / refused |
|---|---|---|
| `title.entry{label, on_select}` | an entry of the mod's at the title: a prompt in its bottom right corner, the player's input for Y and the label (several mods' entries: a list of them); Y calls `on_select` | a handle; `"only while loading"` (entries are made by the main script) |
| `screen.list{title, entries, on_choose, on_cancel, layout}` | a list to choose from; `on_choose(index)` (1-based) | a handle; `"another screen shows"` |
| `screen.page{title, text, picture, on_close, layout}` | a page of text (its lines apart by line breaks) and a picture of the mod's folder (PNG) | a handle; `"another screen shows"` |
| `screen.result{title, rows, on_close, layout}` | a result: `rows` are `{label, value}` pairs (or `{label = ..., value = ...}`) | a handle; `"another screen shows"` |
| `screen.close(h)` | closes the screen, no function called | `true`; `"not shown"` |

A title, label, entry, text or value is one text or a table of texts by language (`{en = ..., es =
...}`), taken in the game's language as the screen shows. One screen shows at a time, whichever mod
made it. While it shows the keys and the pad move it and the game gets none: UP and DOWN go over a
list, A chooses (or closes a page or a result), B leaves a list. Any answer closes the screen; the
mod's function is called at the next frame point, and may show the next screen. A mod that stops
takes its screens and entries with it.

`layout` names a layout of the mod's own (`layouts/<name>.txt` in its folder, in the port's layout
lines, docs/HD_SCREENS.md) with the pieces `veil`, `help_bar`, `row`, `row_selected`, `square`,
`glove`, `trough`, `thumb`, the line `rows TOP PITCH SHOWN`, an optional `art` picture, and the text
styles `heading`, `message`, `single`, `players` and `help`. Without one, the port draws the screen in
the HD theater's list when the HD PACK is there, else in the game's own font.

### The deck

| Name | Does | Returns / refused |
|---|---|---|
| `deck.read()` | the deck: `open` (its cells open: the Slot Releasers + 15, at most 120), `panels` (each set panel's `panel`, `page`, `row`, `col`, from 1, in the deck's order), `spare` (`[panel] = copies` owned and not set) | a table; `"no game"` |
| `deck.info(panel)` | a panel's `category`, `kind`, `link` (whether it is set on another panel's cells) and `cells` (the `{row, col}` it takes from its own cell) | a table; `"unknown panel"` |
| `deck.give(panel)` | a copy more, as the game's rewards give one: spare, for the camp's Panels | `true`; `"unknown panel"`, `"no room"` (99 copies) |
| `deck.give(panel, at)` | a copy more, set at `at` (`{page = , row = , col = }`), or with `true` at the first cell it may take | `true`; `"no room"` (the cell taken or closed, or nowhere it may go) |
| `deck.take(panel, n)` | `n` copies fewer (1): the spare ones first, then the set ones from the deck's last cell | `true`; `"not there"` (fewer owned) |
| `deck.save()` | the deck as it is: `grid` (its 120 cells' panels, 0 empty) and `owned` (`[panel] = copies`) | a table |
| `deck.restore(saved)` | puts a saved deck back, its panels' copies too | `true`; `"does not fit"`, `"unknown panel"` (refused whole) |

A panel is an item the camp's Panels list (items, magic, Level Up and its doublers, gears, abilities,
links, rings and charms; not the synthesis materials, the Slot Releaser or the badges), by its item
number; the port's Stop magic adds three, 628 Stop, 629 Stopra and 630 Stopga, magic panels as the
game's (`"unknown panel"` while the player has Stop turned off). The deck holds as the camp lets the player set it: a multi-cell panel within its page, on open
cells and over no other; a link (marked Ｌ) on a cell of a multi-cell panel of its category; one gear,
one ring or charm, one multi-cell ability of a kind. A change that would break that, or more copies set
than owned, is refused whole, nothing changed. The player's stats come from the deck as the camp
starts a mission: a deck changed in the field counts from the next one. While one of a camp's pages is
open (the Panels keep their own copy) every change answers `"not now"`; a guest's and a visual mod's
answer `"guest"` and `"visual mod"`.

### Time, music, chance and sharing

| Name | Does | Returns / refused |
|---|---|---|
| `timer.after(seconds, fn)` | calls `fn` once, `seconds` of the game's time later | a handle |
| `timer.every(seconds, fn)` | calls `fn` every `seconds` (above 0) of the game's time, until cancelled | a handle |
| `timer.cancel(h)` | stops a timer | `true`; `"no such timer"` |
| `music.play(track, fade)` | plays a track of the game in place of the game's own music: `track` its name in the sound archive (`"TwilightR_F"` or `"seq/TwilightR_F"`, as a mod's `sounds/` names it), `fade` seconds (0.5) | `true`; `"unknown track"` |
| `music.stop(fade)` | the game's own music again | `true`; `"not playing"` (none of this mod's) |
| `random(n)`, `random(a, b)` | a whole number, 1 to `n` or `a` to `b`, each as likely | the number |
| `mod.share(t)` | sends the table `t` (nil, booleans, numbers, texts and tables of them; any size) from the host's mod to the guests' copies of it | `true`; `"guest"` |

| Event | Fields | When |
|---|---|---|
| `shared` | `data` (the table) | a guest's copy of the mod: the host's shared one |

The game's time stands still while the game is paused (its pause, its menus that pause it); a timer
fires at the frame point it falls due, once even after a long frame. Timers of a mod that stops go with
it.

A track plays on the game's own music player, as the game switches its music (its fades, its sound
heap): while it plays, the game's own changes of music are kept back and the last of them is made when
the mod stops its track, else the music it took the place of comes back. It ends by itself as the scene
changes (the field's end, the title's next screen) and when its mod stops.

`random` draws from a seed every player of a room shares: chosen as the room opens and changed by the
host as each mission ends, so every copy starts each mission with the same one. Each mod has its own
run of draws, started again from that seed at each mission's start: the copies of a room draw the same
numbers in the same order from there, as long as their mods draw alike. Alone, the seed is the copy's
own.

### The game's memory

For what the names above do not reach, `memory` reads and writes the game's memory by address, as the
decompilation (khdays-decomp) describes it. Numbers are little-endian, as on the DS.

| Name | Does | Returns / refused |
|---|---|---|
| `memory.u8(a)`, `memory.u16(a)`, `memory.u32(a)` | the unsigned number of 1, 2 or 4 bytes at `a` | the number; `"not the game's memory"` |
| `memory.s8(a)`, `memory.s16(a)`, `memory.s32(a)` | the same, signed | the same |
| `memory.bytes(a, n)` | `n` bytes from `a` | a string; `"not the game's memory"` |
| `memory.set_u8(a, v)`, `memory.set_u16(a, v)`, `memory.set_u32(a, v)` | writes `v`, unsigned or signed (a value that fits neither is an error) | `true`; `"visual mod"`, `"guest"`, `"mod.ini has no memory = write"`, `"not the game's memory"` |
| `memory.set_bytes(a, s)` | writes the string `s`'s bytes from `a` | the same |
| `memory.loaded(n)` | whether overlay `n` is loaded and running | `true` or `false` |

```lua
local state = memory.u32(0x0204be18)          -- gGameState: where the game's state is
print(memory.u32(state + 0xc) == game.munny)  -- its munny (+0xc), as game.munny reads it
```

Where things are:

- **Main RAM** (0x02000000 to 0x02400000) holds the game's objects at the addresses the decompilation
  gives them: `gGameState` at 0x0204be18, `data_0204c23c` at 0x0204c23c. An overlay's objects are
  there only while it is loaded (`memory.loaded`); other overlays use the same addresses.
- **The game's heaps** are the port's own: the main heap from 0x20000000, the sub heap from 0x30000000,
  the headers' heaps from 0x02800000. A block is not where it would be on a DS or an emulator, but the
  game's pointers lead to it: a pointer read with `memory.u32` is an address to read from. Blocks may
  lie elsewhere in another run or another port version, so follow the game's pointers rather than
  keeping addresses. A part of the heaps the game never used reads as 0, and reading it costs the PC
  no memory.
- **The port's data area** (from 0x0b200000) holds what the DS names nowhere: a file's statics and
  texts.
- Anything else (the DS's I/O, VRAM, the stacks) is refused.

Writing needs a gameplay mod whose `mod.ini` says `memory = write`; the Mods page tells the player
such a mod writes in the game's memory. A write changes the game's memory as it is, with none of the
checks the names above make: a wrong one can stop the game or end up in a save. In a room, the host's
writes change the host's game, and reach the guests only where the game itself sends them (the
battle's actors); `mod.share` carries the rest.

## Multiplayer

The host's game decides for everyone in a room, so a room takes only players with the same gameplay
mods as its host: the same ids, versions and scripts (a fingerprint of them travels with the request
to join), and the same mods' characters with the same descriptions and data (CHARACTERS.md, "In a
room"). A player with other gameplay mods is not let in and the room list says which mods the room
plays with. Visual mods are each player's own and never keep anyone out.

In a room, a guest's scripts still run and hear the events, but what they would change in the game
is refused (`nil, "guest"`): the host's mods make those changes, with the host's option values, and
they reach the guests as the rest of the game does. A guest's gameplay mods' options are shown locked
in the Mods page. A mod turned on, off or moved while in a room counts from the next room. What a guest's
copy of a mod shows of the run (its screens, its timers, its music) is its own: the host's mod tells
it what it needs with `mod.share`, and `random` draws the same for both from each mission's start.

## Examples

`port/mods_examples/`: copy a folder into `mods/` to try it.

| Mod | Shows |
|---|---|
| `event-log` | every event, in the mods' log |
| `where` | the mission and the player's position, once a second |
| `double-munny` | the `munny` event and `game.set_munny` |
| `infinite-hp` | `frame`, the player's HP and a toggle |
| `options` | a choice, a number and a button in the Mods page |
| `actions` | Mods-page buttons giving a potion, playing a sound and showing the field's notice |
| `endless-arena` | a whole mode with version 2: its title entry and difficulty list, a loadout set in the deck at Mission Mode's camp and the mission started from there, the mission's own enemies held back and endless waves of the mod's (other missions' kinds among them), an arena's edge, music, a timer, a reward chosen between waves, the defeat held and a result screen, the deck put back; one player |
