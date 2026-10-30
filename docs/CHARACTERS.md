# Characters of mods

A mod can add playable characters to Mission Mode: each one is offered in the character select after the game's,
chosen alone or in a room, and played in a mission with its own model, name, picture, gauges and data, on the
behaviour of one of the game's characters, its **base**. The base stays in the select as it was; a character never
replaces one of the game's. How the port does it: [RUNTIME_MAP.md](RUNTIME_MAP.md) section 30.

Nothing here needs code or scripts: a folder of files, read when the game starts. Behaviour a description cannot
give comes from a native plugin of the same mod ([MODDING_NATIVE.md](MODDING_NATIVE.md), "Actors and characters").

KH Studio makes and changes all of it without writing a line by hand: its **Characters** window
([studio/README.md](../studio/README.md), "Characters") makes a character, copies its model, clips, picture and
voices into its folder, shows each of its base's animations beside the clip given to it, gives it stats and forms
(each form's rule built from the game's gears, links and panels by name), and saves the description once the port's
own reader takes it.

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

- `format` -- the description's version: 1, or 2 for forms and `[stats]` (below; a description of format 1 is read
  as before). A newer one is refused, naming both.
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

## Its stats (`[stats]`, format 2)

The gauges are what the select shows; what the character plays with at each level is its level table, its base's
(`ba/ch/<code>/lv.b.z`) unless `[stats]` gives values of its own:

```ini
[stats]
1 = hp 70 strength 10 magic 30 defence 8
50 = hp 230 strength 50
100 = hp 400 strength 90 magic 200 defence 150
```

- `<level> = <stat> <value> ...` -- levels 1-100. A stat given at some levels has, between two of them, the value in
  between (rounded); before the first, the first's; after the last, the last's. A stat given at no level keeps its
  base's.
- The stats, as the game uses each level row: `hp` (the most HP, `Ov022_ApplyMissionLevelStats`), `strength` (an
  attack's damage, `Ov022_ComputeStrengthDamage`), `magic`, `defence` (`Ov022_ComputeDamageTaken`; `defense` is read
  too), and two whose use is not known yet, `unknown_8` and `unknown_12` (RUNTIME_MAP section 32).
- The port writes the table into the character's own folder as the game starts; a mod's own `data/lv.b.z` wins over
  it (told).

## Its weapons (`[weapons]`, format 2)

The weapons the character holds are its base's models (`ba/ch/<code>/w_.p2`: Roxas's keyblades, the dual-wield
Roxas's two); each one can be drawn as a model of the character's own:

```ini
[weapons]
ro_w00600 = weapons/stick.glb           # the game's weapon model it draws instead (its name in w_.p2)
```

- `<the base's weapon model> = <file>` (glTF, GLB or FBX). Its joints follow the game's model's nodes by name (a
  model exported from KH Studio keeps them: `ro_w13600_a` ...), as a paired model's do; drawn where and when the
  game draws its own. With `KHDAYS_TRACE_MODELS=1` the log names each weapon model as it is drawn
  (`ba/ch/c100/w_.p2#0/mdl/ro_w13600`); KH Studio lists the base's.
- A file that cannot be read is told; the game's weapon is drawn. Only the picture: the weapon's reach and hits are
  its base's.

## Forms (`[form <id>]`, format 2)

A character can have other forms: each one another model, skeleton, animations, voice, moveset, Limit, stats and
weapons, and even another of the game's characters to play as, chosen by what the player equips. The character's
own lines are its **default form**, played when no form's rule holds.

```ini
[form dual]
base = roxas_dual                       # the game's dual-wield Roxas: two weapons, his combos and Limit
priority = 100
when = gear 230 and links 241 >= 3      # the Zero Gear with three Ability Units
model = model/ventus_dual.glb
skeleton = own

[form dual weapons]
ro_w13600 = weapons/right.glb           # the dual-wield Roxas's two weapon models, drawn as these
ro_w14600 = weapons/left.glb

[form dual actions]
fallback = clips/dual_idle.glb

[form dual moveset]
moves = base                            # the dual-wield Roxas's own combos
```

- `[form <id>]` -- an id of letters, digits, `_` and `-` (not `default`), with `base` (any base above, or
  `roxas_dual`), `priority` (a whole number, 0 when not given), `when` (its rule, below) and the model's lines of
  `[model]` (`model` or `file`, `skeleton`, `scale`, `weapon_bone`, `weapon_turn`, `base_weapon_node`).
- `[form <id> actions]`, `[form <id> sounds]`, `[form <id> moveset]`, `[form <id> limit]`, `[form <id> stats]`,
  `[form <id> weapons]` -- the default's parts, for the form. A part a form does not give is its default's, whole
  (its weapons only on the same base: they name the base's models); so is each model line.
- A moveset names its base's attacks: a form on another base than its default's takes its default's `[moveset]` only
  if it gives its own (told otherwise, its base's moves playing); `moves = base` says so without a line of moves.
- Each form plays from a folder of its own (`ba/ch/c100/` on: the log names them), made from its base's.
- The choice: among the forms whose rule holds, the highest `priority`, then the first written; none, the default
  form. The same equipment always gives the same form.
- A form that cannot be taken (a bad line, a rule that does not parse, a base it has not got, a file that cannot
  be read, no `when`) is left out, told in the mods' log with its line; the character and its other forms are kept.

### Its rule (`when`)

```ini
when = gear 230 and links 241 >= 3
when = (panel 93 >= 2 or panel 94) and not dual
when = links >= 2 and hp < 30
```

- `gear <item>` -- the deck's gear is this one. `links [<item>] <op> <n>` -- how many links (of that one, or any)
  are set on the gear's cells. `panel <item> [<op> <n>]` -- that panel set in the deck (that many copies).
  `dual` -- the game's own dual-wield condition (its flag 0x2010, set by the camp from the deck). `level <op> <n>`
  -- its level (not read yet: false). `hp <op> <n>` -- its HP as a percent of its most, in the field only (false
  at the camp).
  `plugin <mod id>.<name>` -- a native plugin's condition (MODDING_NATIVE.md, "Actors and characters").
- `<op>`: `>=`, `<=`, `>`, `<`, `==`, `!=`. `not`, `and` (before `or`), `or` and parentheses; `#` a comment.
- Items are the game's numbers (KH Studio names them): the Zero Gear is 230, the Ability Unit 241.
- When: at the camp, where the game itself makes each seat's character (its dual-wield Roxas, RUNTIME_MAP section
  32), before the field loads it. A rule that asks what only the field knows (`hp`, a plugin's condition) is asked
  again each frame there; a different form is told in the log and played from the next mission (the field's actor
  is made from its form's base as it enters).

## In a room

Any player of a room may choose a mod's character, and every copy plays it as its own: the select tells
the room its place's number, which each copy knows as the same character (seen with two copies on one PC,
the host's and a guest's choice). So a room takes only copies with the same characters: each one's
identity, its `character.ini` and its `data/` files are in the gameplay mods' fingerprint (SCRIPTING.md,
"Multiplayer"), and a copy whose characters differ is not let in, the room list naming the room's
(`Ventus local.ventus/ventus`). Its model, clips, face and sounds are each player's own: they only change
what that player sees and hears.

In a room a seat's form is the game's own choice for it, which every copy makes alike from the room's records: its
first form on the base the game chose (the dual-wield Roxas for a Roxas whose deck makes it so), else its default.
The decks are each player's own and the room does not share them, so the rules are not asked there.

## Limits

The select keeps a place's character in a signed byte: the mods' characters take the numbers 0x16-0x7f, 106 at
once (more are left out, told). The select of the HD pieces shows them in pages when they do not fit one (L and R
turn the page; HD_SCREENS.md, "Mission Mode's character select"). The numbers follow the characters' identities, so copies with the same mods give
the same ones. The rest of the game -- and the saves -- only ever hold the base's: a save made with a mod's
character loads without the mod, its base standing in.

## Not yet

- The game's own select (two screens, without the select's layout of the HD pieces) does not list them.
- Characters are read from every folder in `mods/`, also one turned off in the Mods page (as the mods'
  missions are): a character is taken out by taking its mod's folder out.
- Attacks of their own (new kinds of moves, not the base's) and a Limit of their own (its length, its kind of
  hits: their base's code); the field's portrait and the results'
  model are still the base's.
- The weapons' swing trails and the hits' shapes are the base's.
- A form changes at the camp only: in the field it is told and waits for the next mission. `level` in a rule is
  false (the seat's level is not read yet).
- In a room, forms follow the game's own choice only (above).
