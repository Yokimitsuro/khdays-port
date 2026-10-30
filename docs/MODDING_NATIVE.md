# Mods' native plugins

A mod can carry a **native plugin**: code written in C++ (or C) and built for the player's computer, which the game
loads and runs inside itself. Plugins are for what data and Lua scripts cannot do -- new mechanics, combat, Limits,
AI, cameras -- and get the port's interfaces through the **KH Mod SDK** (`sdk/`). Everything mods did before
(content, Lua scripts, missions, characters by description) works as it did, without plugins.

## For players: turning native code on

A plugin can do anything a program on your computer can: read and write your files, crash the game, damage its
saves. The port does not isolate it. So the game loads no plugin until you turn native code on: in the settings
menu (Esc), on the Mods page, **Native code** -- its description says what it lets run -- or in the settings
file's `[mods]`:

```ini
[mods]
native_code = on
```

It is read as the game starts: a change counts from the next start. Only turn it on for mods you trust, from people
you trust. The Mods page lists each plugin with its mod, version and declared author, and what became of it as the
game started (running, waiting for native code or safe mode, not loaded and why); the mods' log
(`saves/logs/mods.log`) says the same as it happens. The game never downloads a plugin.

**Safe mode**: start the game with `--safe-mode` (or `KHDAYS_SAFE_MODE=1` in the environment) and no plugin is
loaded whatever the setting -- for when a plugin keeps the game from starting.

## For modders: a mod with a plugin

```text
mods/<Mod>/
  mod.ini
  native/windows-x64/<plugin>.dll
  native/linux-x64/<plugin>.so       (once the port runs on Linux)
  native/linux-arm64/<plugin>.so
  ...                                the mod's content, scripts, characters as any mod
```

`mod.ini`'s keys for a plugin, besides `id`, `name`, `version`, `author`:

| Key | |
|---|---|
| `sdk` | the SDK's ABI major the plugin was built for (`KH_ABI_MAJOR`, 1 today); the port loads only its own |
| `native.<platform>` | the plugin's library for `windows-x64`, `linux-x64` or `linux-arm64`, a path inside the mod |
| `requires` | mods (their ids, comma-separated) that must be in `mods/`; a required mod's plugin starts first |

A mod with `sdk` and no `scripts/` folder is not a Lua mod (it needs no `kind` or `api`); one with both runs its
scripts and its plugin.

### Building a plugin

The SDK builds on its own with CMake; its `cmake/KhModSdk.cmake` gives `kh_add_plugin(<name> <sources>)`:

```bash
cmake -S sdk -B build/sdk -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/sdk
```

`build/sdk/examples/HelloWorld/hello_world.dll` is the Hello World plugin: copy it into
`mods/HelloWorld/native/windows-x64/` beside `sdk/examples/HelloWorld/mod.ini`, turn native code on, start the game:
the mods' log says hello at the start and goodbye when the game closes. `sdk/docs/README.md` explains the interface.
Installed with KH Studio, the game's folder has the SDK in `sdk/` and the examples ready to copy into `mods/` (their
plugins built) in `mods-examples/`.

**Linux** (`native.linux-x64`, `native.linux-arm64`: `.so`) builds the same on Linux with gcc or clang, or from
another system with `sdk/cmake/toolchain-linux-x64-clang.cmake` (clang and lld; a Linux system's files as its
sysroot for plugins that use the C or C++ library). `sdk/tests/load_plugin.py` loads a plugin as the port does --
its descriptor, its ABI, its start with a host table, its stop -- on any system Python runs on, so a Linux plugin is
checked on Linux without the port there.

### What a plugin may count on

- `kh_plugin_query` is called once; the descriptor's `start` once with the host table, on the game's thread; if it
  answers anything but `KH_OK`, the plugin is released without `stop`.
- `stop` is called once on the game's thread as the game closes (the window's close asks; the game's thread does it
  at its next frame, the window waiting up to a second), the last plugin started first; then its library is
  released and nothing of it is called again. A game killed (its process ended) stops nothing.
- Interfaces are asked by name and version (`find`); an unknown one answers NULL.

## The universal core

The SDK is not built around one kind of mod: its core reaches every part of the game, and a kind of mod nobody
planned for is one plugin's service that others build on. Four interfaces, each in its header under `sdk/include/kh/`:

| Interface | What it gives |
|---|---|
| `kh.services` (`kh_services.h`) | publish a table of functions under a name and version (`<mod id>.<name>`) for other plugins, which find it with `find` as they find the port's; withdrawn at the plugin's stop |
| `kh.events` (`kh_events.h`) | subscribe to the game's events by name (frame, scene, title, mission start and end, HP, items, munny, menus, saving: the Lua mods' own) and to other plugins' events; raise events of one's own (`<mod id>.<name>`, a payload of bytes) |
| `kh.hooks` (`kh_hooks.h`) | functions before and after the game's routines at its hook points, with priorities: before-functions may change the arguments or skip the routine giving its result, after-functions may change the result; two plugins that both replace a routine are told |
| `kh.engine` (`kh_engine.h`) | the game's memory (a reference to a host address and back), every routine and every datum of the game by its decompilation name, whether an overlay is loaded, the frame, the game's thread |

**The hook points** are placed where the game is built (`port/tools/prepare.py`, `HOOK_POINTS`): the routine keeps its
name as a wrapper that calls it at once while no plugin hooks it. The catalogue grows with what plugins need --
`point_count`, `point_name` and `point_signature` list it -- and a plugin that needs a point asks for it. Arguments
and results travel as 64-bit slots, the game's pointers as references. The points so far:

| Point | Subsystem | What it is |
|---|---|---|
| `Ov022_ActorSetHp(ref entity, int value)` | combat | a player's HP set: a hit, a cure, a defeat (the enemies' HP goes the next way) |
| `Ov107_AiState_ApplyHit(ref enemy, ref source, ref hit)` | combat | an enemy takes a hit; its damage is worked out inside |
| `Ov022_ComputeStrengthDamage(ref attacker, int scale)` | combat | the damage of a player's hit: its strength times the hit's factor |
| `Ov022_ActorSetState(ref actor, int state)` | combat | a player enters a state by its number (its result: the state's routine, 0 none); on Roxas's code the Limit is 23 as it starts, then 34 and 24 at each hit |
| `Archive_LoadFile(ref name, uint kind)` | resources | a file of the game loaded whole |
| `PlaySound(int archive, int entry)` | audio | a sound effect: the menus' |
| `Slot_Spawn(uint handler, uint entry, ref place, uint flags)` | audio | a positioned sound: the field's, an actor's cues and voices |
| `Ov002_TickCamera()` | camera | the field's camera, once a frame |

**The engine by name** is the advanced level: a plugin can call any of the game's compiled routines (cast to its
decompilation's prototype, pointers passed as references, on the game's thread) and read or write any of its data.
It is the game's own, as the decompilation names and lays it out: a later port may change it, and the port checks
nothing a plugin does with it. An overlay's routines are always there, but its data and state only while it is
loaded (`module_loaded`).

`sdk/examples/AdvancedNativeMod` uses all four: it halves the damage the player takes through `Ov022_ActorSetHp`'s
hook, reading the actor's HP from the game's memory, logs the enemies' HP as they are hit, counts the menus' and the
field's sounds, logs scene changes, and calls `GetLanguage` and reads `gSceneCtl` by name.

## Actors and characters

Two interfaces built on the core give the field's bodies and the playable characters to plugins that would rather
not know the game's memory:

| Interface | What it gives |
|---|---|
| `kh.actors` (`kh_actors.h`) | the players (by seat) and the standing enemies as handles: their kind, character, place and HP, a player placed or given HP through the game's own routines, the actor a hook point's argument names (`of`) |
| `kh.characters` (`kh_characters.h`) | the game's 19 characters and the mods' ([CHARACTERS.md](CHARACTERS.md)), found by key, and listeners called for the actors that play one: as it enters the field, each frame, as it starts an attack, as it leaves, as its seat takes a form; the forms' conditions of plugins, each seat's form, a form given |

**A handle names one actor for its life.** An enemy's ends when it is defeated, retired or sent away, every actor's
when its area ends or the scene changes; after that every call with it is refused, and the actor made again in its
place (the game reuses its enemies) gets a new one. Each call also checks that the game still has the actor where it
was found.

**A character's behaviour from a plugin**: a mod with a character (its `characters/<key>/`) and a plugin finds its
character with `find(plugin, "<key>")` and listens to it: the plugin is called for that character's actors alone,
and the character plays by its description whenever native code is off. `sdk/examples/CustomCharacter` is such a mod:
a made-up character (three boxes on Roxas's code) whose plugin logs each attack and gives 1 HP back for it, and,
through `Ov022_ComputeStrengthDamage`'s hook and `of`, doubles its hits' damage while everyone else's stays.

In a room every copy of the game calls its plugins for every seat: a plugin that changes the game's state does it
on the host's copy (`kh.net`'s `host`, below).

**Forms** (`kh.characters` version 2; CHARACTERS.md, "Forms"): `add_condition(plugin, "<name>", test, user)` gives
the forms' rules a condition of the plugin's own, named `plugin <mod id>.<name>` in a `when`, asked with the seat and
the character's number each time a form is chosen; `form(seat)` is the form a seat plays (`"default"` for the
character's own); `set_form(plugin, seat, id)` gives a seat a form whatever its rules say, from the next camp (NULL
gives the choice back; refused in a room, where a seat's form is the game's own choice); a listener's `form` call
tells it each form taken. A plugin made for version 1 gets the same table and sees none of it.

## Moves, Limits and enemies of plugins

| Interface | What it gives |
|---|---|
| `kh.input` (`kh_input.h`) | the player's buttons each game frame (held, pressed, let go), and buttons hidden from the game while the plugin still reads them (all four directions: the analog stick's movement too) |
| `kh.combat` (`kh_combat.h`) | a hit's damage as the game works it out for a player (`Ov022_ComputeStrengthDamage`), and hits of the plugin's own dealt to the standing enemies through the routine the players' attacks reach (`Ov107_AiState_ApplyHit`): one enemy, or all within a reach; a status and its chance, no flinch. The host's in a room |
| `kh.characters`' `play` | a mod's character with its own skeleton drawn playing one of its clips (any name of its `[actions]`) instead of what the game plays, once or looped |
| `kh.actors`' `make_enemy`, `set_position` | an enemy of any kind made as the area makes its own, a standing enemy moved as the game moves its own |

**A Limit of its own** (`sdk/examples/CustomLimit`): its made-up character (three boxes on a skeleton of its own, its
clips made by the example's `make_files.py`) starts its Limit as the game's would -- the button held with the HP in
danger -- but the plugin skips the Limit's states for its actor at `Ov022_ActorSetState`, hides the buttons and
directions while its own lasts, plays its clips and makes eight spins whose hits reach the enemies around it. The
game's Limit never plays for it (none of its `b_ro_L00x` animations), any other character's is the game's.

**An enemy's behaviour** (`sdk/examples/CustomEnemy`): the Shadows blink away once from their attacker at half their
HP or less and split into two weaker ones when defeated -- after `Ov107_AiState_ApplyHit`'s hook, done at the next
frame (a hit comes while the game walks its enemies, which an enemy made or moved then would change under it).

## Its parameters

What a plugin's author lets a modder change without code -- a Limit's spins, a damage, a choice of sounds -- the mod
describes in `parameters.ini` beside its `mod.ini` (or the file `mod.ini`'s `parameters` names), a part a parameter:

```ini
[spins]                 # its name: letters, digits, _ - .
type = integer          # integer, number, flag, choice, text
default = 8
min = 1                 # integer, number: its bounds (either may be left out)
max = 30
label = Spins           # its words, and in a language: label es = Giros
help = How many times the Limit spins.

[status]
type = choice
choices = none, burn, freeze, jolt, toss
default = none
```

KH Studio's Plugin window (and `--plugin-parameters`, `--set-plugin-parameter`) shows them as controls, reading this
file, never the plugin, and saves the values chosen in `parameters.values.ini` beside it (`name = value` lines). The
port reads both as the plugin starts and answers it through `kh.parameters` (`kh_parameters.h`): an integer, number
or flag (a choice's index), a text (a choice's name), each with a fallback for a name it does not know. What is wrong
in either file -- an unknown type, a default or a value out of its bounds, a name not described -- is told in the
mods' log and left out. `sdk/examples/CustomLimit` reads its spins, their damage, reach and status so.

## In a room

A room plays the host's game: the host's copy works out the actors, the hits and the enemies; each guest's copy shows
what the host sends and sends the host its player's buttons. So:

- **Every copy plays with the same plugins.** The plugins started are part of the gameplay mods' fingerprint (each
  one's library and parameters' files, hashed): a copy with other plugins, other versions or other values chosen --
  or with native code off where the host has it on -- is refused as for other gameplay mods, the host's list of mods
  shown to it.
- **A mod says it is made for rooms** with `multiplayer = yes` in its `mod.ini`. In a room the port keeps a plugin
  whose mod does not say so from changing the game: its hooks are not called; its hits, HP, places, enemies made,
  buttons hidden and clips played are refused (`KH_UNSUPPORTED`); it still hears the game's events.
- **`kh.net`** (`kh_net.h`) says whether this copy is in a room and whether it decides the game (the host, or
  alone), and sends a plugin's own messages -- a name and a version the plugin chooses -- from the host's copy to every
  guest's (inside the mods' shared messages, marked as no Lua mod's). Every copy's plugins hear one as the event
  `<mod id>.<name>`, its version in `a`, the host's own as the guests'.

## Built into the port

A platform that loads no library (a console) gets plugins built into the port: the same source, its
`kh_plugin_query` renamed, listed by its mod's id --

```bash
cmake ... -DKHDAYS_STATIC_PLUGINS="example.custom-limit=sdk/examples/CustomLimit/custom_limit.cpp"
```

-- the plugin manager finds the mod as ever (its `mod.ini`, its `sdk`, its parameters) and starts the plugin without a
library; native code must still be on. Everything of the plugin but `kh_plugin_query` must be its own (static, or in
an unnamed namespace), as a library's hidden names are. The plugins' test builds one in so.

## What is next

More hook points across the game's subsystems as plugins ask for them (hits on the players from a plugin, an enemy's
own AI); the port itself on Linux (where plugins then run as on Windows) and on a platform without dynamic loading
(where they are built in); a guest's messages to the host.
