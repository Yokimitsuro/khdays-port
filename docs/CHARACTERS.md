# Characters of mods

A mod can add playable characters to Mission Mode: each one is offered in the character select after the game's,
chosen alone or in a room, and played in a mission with its own model, name, picture, gauges and data, on the
behaviour of one of the game's characters, its **base**. The base stays in the select as it was; a character never
replaces one of the game's. How the port does it: [RUNTIME_MAP.md](RUNTIME_MAP.md) section 30.

Nothing here needs code or scripts: a folder of files, read when the game starts. Behaviour a description cannot
give comes from a native plugin of the same mod ([MODDING_NATIVE.md](MODDING_NATIVE.md), "Actors and characters").

KH Studio makes and changes all of it without writing a line by hand: its **Characters** window
([studio/README.md](../studio/README.md), "Characters") makes a character, copies its model, clips, picture and
voices into its folder, shows each of its base's animations beside the clip given to it, and saves the description
once the port's own reader takes it.

## The folder

```text
mods/<Mod>/
  mod.ini                       the mod's (SCRIPTING.md, "A mod's folder"); kind = gameplay
  characters/<key>/             one folder a character; its name is the character's key
    character.ini               its description (below)
    face.png                    its picture in the select (any size, drawn fitted)
    model/<name>.gltf           its model: .gltf, .glb or .fbx
    model/<name>.bones.txt      its bones paired with its base's (RENDERING.md, "Mods' graphics")
    data/<file>                 optional: one of the game's files of a character's folder (ba/ch/<code>/),
                                in the game's format, used instead of its base's (lv.b.z: its levels)
```

A key is letters, digits, `_` and `-`. A character is known by its mod's id and its key (`test.character/block`):
two mods may use the same key; the same identity twice keeps the first one read.

## character.ini

```ini
# a made-up character
[character]
format = 1
base = roxas
name = Block
name es = Bloque
weapon = Stick
face = face.png
locked = no
gauges = 2 2 2 2 4 2 2 2 2

[model]
file = model/block.gltf
skeleton = paired
```

- `format` -- the description's version: 1. A newer one is refused, naming both.
- `base` -- the game's character it plays as: roxas, axel, xigbar, saix, xaldin, sora, demyx, larxene, lexaeus,
  luxord, marluxia, riku, vexen, xemnas, xion, zexion, mickey, donald, goofy.
- `name`, `weapon` -- for every language; `name en`, `name fr`, `name de`, `name it`, `name es` (and `weapon ...`)
  for one.
- `face` -- its picture in the select.
- `locked` -- `no`: always offered; `base`: offered only when its base can be chosen.
- `gauges` -- the select's nine numbers, 0-10, in the order of the game's rows (RUNTIME_MAP section 16.3): strength,
  magic, defence, critical %, HP, two the select does not show, attack speed, critical bonus; without it, its
  base's.
- `[model] file` -- its model (glTF, GLB or FBX). `skeleton = paired`: its bones follow its base's (by name, or
  as `<name>.bones.txt` pairs them), moved by its base's animations. `skeleton = own`: its own skeleton, moved by
  its own clips (below); nothing of it is paired with the base's.
- `[model] scale` -- its size to the game's: 100 for an FBX made in centimetres (the game's characters are about
  1.4 tall in its units, a model in metres 1.4).
- `[model] weapon_bone` -- its bone the weapon follows (`R_buki`); without it the weapon stays where the base's
  hand is. `weapon_turn = x y z` -- the weapon held on the bone's own axes, turned by these degrees (a weapon
  bone made for its own weapon: the grip the model was made with); without it, the weapon is held as the base
  holds it. `base_weapon_node` -- the base model's node the game puts the weapon on, when not its `*_w_tg_R`.

`#` starts a comment (at a line's start or after a space). A name or part the port does not know is told in the
mods' log with its line and left out; a character with a missing `format`, `base`, `name` or `[model] file`, a bad
value or a file that cannot be read is left out, told in the mods' log, and the game plays on.

## Its own animations (`skeleton = own`)

The character plays on its base's code, so the game asks for its base's animations by their names; each one is
given a clip of the character's own in `[actions]`:

```ini
[actions]
fallback = clips/002.fbx                    # any animation with no clip of its own
b_ro_A000 = clips/002.fbx                   # standing
b_ro_A001 = clips/004.fbx blend 120         # running
b_ro_B009 = clips/200.fbx root keep time game  # the combo's first hit: its forward step kept, timed
                                               # to the game's
```

- `<the base's animation> = <clip> [blend <ms>] [root keep|remove] [time own|game]`: the clip (its file's first
  animation, its bones by name) plays while the game plays that animation. `time own` (the default): at the
  speed it was made with, standing while the game holds its animation (the pause, a hit's stop), and looping
  once the game's animation loops (standing, running); a clip longer than a one-off animation of the game's is
  cut where the game goes on. `time game`: at the same point as the game's animation, stretched to its own
  length, so that a hit lands when the game's does (for attacks). `blend` (default 100 ms) is how long it blends
  from the pose before; the root's own travel is removed (the game moves the character) unless `root keep`.
- The names: Roxas's standing `b_ro_A000`, running `b_ro_A001`, the ground combo `b_ro_B009`, `b_ro_B008`,
  `b_ro_B015` and `b_ro_A004` after it (seen; more as they are found). With `KHDAYS_TRACE_MODELS=1` and the session
  log on, the log names each animation as the game plays it and the clip shown; an animation with no clip is told
  once in it either way.
- Clips are drawn at every displayed frame between two of the game's (the port's 3D smoothing): the game's logic
  keeps its own pace.

## Its voice (`[sounds]`)

The game starts its base's sounds for the character (its attacks' shouts, its hurt and Limit voices); each one can
be given a file of the character's own, or silence, and only for the character: the base, played by anyone else in
the same room, keeps its voice.

```ini
[sounds]
seqarc/PLY_ROX/vo_rs02at07 = voices/attack1.wav   # one of Roxas's shouts
seqarc/PLY_ROX/vo_* = none                         # his other voices: not heard
```

- `<the base's sound> = <file>` (`.wav`, `.flac` or `.ogg`, decoded when the game starts) or `none`. A name ending in
  `*` is every sound it begins; a sound's own line wins over those, and the longest of those over the others.
- The names are a sound mod's (SOUND.md, "Mods' sounds"): `seqarc/<archive>/<sound>`. Roxas's voices are in
  `PLY_ROX`; his swings (`WEP_ROX26/...`) and steps are effects, not his voice, but can be given files the same
  way. With the session log on, it names each sound as it starts (`sound: seqarc/PLY_ROX/vo_rs02at07 starts on
  player 6`); `KHDAYS_TRACE_CHARACTERS=1` also names the character's line chosen for it.
- What is the character's: the sounds the game starts for its actor (`Ov022_PlayEntityVoice`,
  `Ov022_SpawnVoiceCue`). A file that cannot be read is told; the base's sound plays in its place.

## Its moveset (`[moveset]`)

The character fights with its base's combat: its combos are made of its base's attacks, each with its hits'
frames, reach and damage and the animation the game plays for it (which `[actions]` gives a clip). `[moveset]`
chains those attacks its own way and changes them; the port writes the result into the character's own copies of
its base's combo files (`ci.b.z`, `cm.b.z`) when the game starts, and the game's own combat runs them.

```ini
[moveset]
ground = 0 1 10                      # the ground combo: these attacks in order, then again
ground finisher = 2                  # and its finisher
air = 3 4
air finisher = 5
attack 1 = damage 1.5 reach 1.2      # this attack's hits: half again the damage, a fifth more reach
attack 10 = push 2                   # knocked twice as far
```

- `ground` / `air` -- the attacks' numbers (their records in the base's `cm.b.z`) in order. They replace every
  keyblade's combo of the base's (the game gives each keyblade its own), with no branches; the finisher is each
  keyblade's own unless `ground finisher` / `air finisher` names one.
- `attack <number> = [damage <times>] [reach <times>] [push <times>]` -- each of that attack's hits, times the
  base's: its damage; its radius and how far out it is (not its height); how far it knocks out and up. Only the
  attacks whose hits sweep (most of the combos'; RUNTIME_MAP section 30) can be changed.
- Roxas's first keyblade's combo is `0 1`, finisher `2`; in the air `3 4`, finisher `5`; the others' attacks are
  numbered by keyblade (1000s for the second, 2000s for the third, ...). Any of the base's attacks can be used:
  a keyblade's two combos may hold at most 64 attacks and 16 of their animations (the game's tables). With
  `KHDAYS_TRACE_CHARACTERS=1` and the session log on, the log names each attack the character hits with as it
  changes (`characters: local.ventus/ventus (seat 0) attacks with attack 1`).
- A moveset that names an attack its base does not have, or passes those tables, is told in the mods' log and left
  out: the base's moves play. A mod's own `data/ci.b.z` or `data/cm.b.z` wins over it (told).

## Its Limit (`[limit]`)

The character's Limit is its base's (the game's own code for it, in the base's overlay): with its HP in the
yellow, holding A starts it, and its attacks are its base's Limit's until the white line of its HP gauge runs out.
What it shows and says is the character's own, as for its other moves:

```ini
[actions]
b_ro_L000 = clips/212.fbx            # Roxas's Limit's start
b_ro_L001 = clips/300.fbx time game  # each of its hits

[limit]
damage = 1.5                         # its Limit's damage, times its base's
```

- Roxas's Limit plays `b_ro_L000` as it starts and `b_ro_L001` at each of its hits, with `b_ro_A003` and
  `b_ro_A004` between them (seen; `KHDAYS_TRACE_MODELS=1` names them for any base); its shouts are its base's
  attack shouts (`seqarc/PLY_ROX/vo_rs02at01`, `vo_rs02at03`), which `[sounds]` gives files of its own.
- `[limit] damage = <times>` -- the damage of the moves its base's own code makes (the Limit's hits, and any other
  move of the base's own: `Ov022_ScaleRowValues`), times the base's. The combos' attacks are `[moveset]`'s.
- Its effects and its animations' file are its base's, read from its base's folder (`ba/ch/ro/li*.p.z`: the
  overlay names them itself), so `data/` does not replace them.
- Its length, its button and the way it is started are its base's.

## In a room

Any player of a room may choose a mod's character, and every copy plays it as its own: the select tells
the room its place's number, which each copy knows as the same character (seen with two copies on one PC,
the host's and a guest's choice). So a room takes only copies with the same characters: each one's
identity, its `character.ini` and its `data/` files are in the gameplay mods' fingerprint (SCRIPTING.md,
"Multiplayer"), and a copy whose characters differ is not let in, the room list naming the room's
(`Ventus local.ventus/ventus`). Its model, clips, face and sounds are each player's own: they only change
what that player sees and hears.

## Limits

The select keeps a place's character in a signed byte: the mods' characters take the numbers 0x16-0x7f, 106 at
once (more are left out, told). The numbers follow the characters' identities, so copies with the same mods give
the same ones. The rest of the game -- and the saves -- only ever hold the base's: a save made with a mod's
character loads without the mod, its base standing in.

## Not yet

- The game's own select (two screens, without the select's layout of the HD pieces) does not list them.
- Characters are read from every folder in `mods/`, also one turned off in the Mods page (as the mods'
  missions are): a character is taken out by taking its mod's folder out.
- Attacks of their own (new kinds of moves, not the base's) and a Limit of their own (its length, its kind of
  hits: their base's code); the field's portrait and the results'
  model are still the base's.
