# The one-screen compositions

On one screen the port composes a single picture from the DS's two screens.
For each situation of the game (`port/ui/situation.h`) a composition says which
screens it shows, whose brightness it takes, and the shapes that place pieces of
the screens in it. They are read from a mod's `layouts/compositions.txt`, else
from the port's own, `port/ui/compositions/port_days.txt` (`port/ui/composition.c`).

## compositions.txt

The file starts with `units port`, then sections
`[<situation> [if <condition> ...]]` -- a condition is a state, `!` before it for
its absence, `wide` (16:9 or wider) or `narrow` (4:3) -- taken in the file's
order while their conditions hold. Their lines:

| Line | What it does |
|---|---|
| `screens <upper\|lower\|stacked\|side-by-side>` | the picture's ground (the first section that says it) |
| `brightness <default\|upper\|lower\|halves\|own>` | whose master brightness the picture takes |
| `shape <upper\|lower> [<x> <y> <w> <h>] at <corner> [margin <l> <t> <r> <b>] [scale <s> [<sy>] [at-most <m>] \| scale cover] [effects]` | a piece of a screen (all of it without a rectangle), scaled (a DS dot becomes `s` picture dots, never more than `m`), at a corner of the picture (`centre`, `top-left`, `top`, ..., `left`), moved by its margins; the first shape that gives a texel shows it (the effects: `composition.c` `read_shape`) |
| `shape3d <hud\|all> [<x> <y>] [<w> <h>] at <corner> [margin ...] [scale <s>]` | the HUD's screen-space 3D (or all the 3D) in a rectangle, moved |
| `hide3d <hud\|all>` | not drawn |
| `menu` | the common rule: the screens that show something, whole, as large as the picture allows (a situation no section names takes it too) |
| `halves` | the two screens as one, joined in the middle: the upper's left half and the lower's right half where the DS has them, the DS's 256 columns enlarged to the picture's width (top and bottom cut when wider than 4:3); the upper's world's 3D enlarged with it (the HUD's left to its shapes), the lower's half under the section's shapes (Sora's memories in a battle) |
| `stop` | no later section |
| `backdrop <file> [at <corner>] [fit]` | a picture of the mod's `layouts/` folder (PNG) behind everything, covering the picture (with `fit`, inside it: all of it shows at any aspect ratio), its aspect kept, held at the corner |
| `preload <file>` | a picture named as a backdrop's is (and an animated one's atlas) read ahead on a thread of its own while its section holds, so showing it later holds nothing up |

A length is in the picture's DS dots at the HUD's scale (its factor, 1 at the
default HUD size); `fixed(N)` is one that does not follow it, `A+fixed(B)` both.

`information-mod` holds while a valid KH2 information replacement is enabled,
including gaps between messages. The port's native banner crops require
`!information-mod`; custom compositions that crop those banners can use the same
condition. The replacement is placed independently at the picture's top left
(`docs/KH2_SEQUENCES.md`), rather than sampled from either screen's banner crop.

A backdrop's name may hold the title menu's state: `{lang}` the game's language
(en, fr, de, it, es); `{menu}` root, story or mission (`intro`: its fade-in
while the lower screen is still white; `logo`: the boot logos, 3 of them, `{row}`
the one on show; `newgame`: the new game's dialogs, `KHDAYS_TITLE_NEWGAME`);
`{count}` the options the level shows; `{row}` the one selected. A section
whose picture no mod gives for this frame is not used.

## Animated pictures

A backdrop named `.txt` is an animated picture (`tools/hd/theater_title.py` and
the other HD tools make them): pieces of an atlas, each a rectangle of the
picture, moving and fading over the frames of its clips. Its lines:

| Line | What it does |
|---|---|
| `picture <w> <h>` | the picture's size, in its own texels |
| `ground <r> <g> <b>` | behind every piece (0 to 1) |
| `atlas <file>` | the pieces' texels (PNG), beside this file |
| `group <name>` | its group, which a clip of the next picture may name |
| `clip <frames> after <group> ...` | played when the picture comes after one of those groups (`none`: after no animated picture) |
| `loop <frames>` | after the clip, from the frame it reached, again and again; from its start when no clip names the group before |
| `piece <x> <y> <w> <h> <over\|add\|take> <16 numbers>` | its texels in the atlas (a negative width or height turns them over), how it goes on what is under it, the colours of its left top, right top, left bottom and right bottom corners (RGBA, 1 = the texel's own), blended across it |
| `key <frame> <x0> <y0> <x1> <y1> <r> <g> <b> <a>` | the piece's rectangle in the picture and its colour over all of it at a frame of its clip; between two keys, a line between them |
| `prompt <action>` | (after a piece) it shows the input the player has for the action on the device played, where an icon for it is given (`khdays_composition_set_prompt`), its height kept, its right edge where it is; its own texels otherwise |
| `icon <name> <x> <y> <w> <h>` | an input's icon in the atlas, named `<family>:<input>` (`xbox`, `playstation`, `pad`; `host/app/profile.c`'s gamepad names) |
| `button <action> <x0> <y0> <x1> <y1>` | a rectangle of the picture the mouse clicks to do the action (`open_settings`: the settings menu) |

After a picture of its own group (another row selected), the clip that follows
the same groups and the loop go on from the frame they were at, not from their
start: a loop's first frame can be far from the one on show (the new game's
selected button's glow). The clips' clock is the game's VBlanks, 60 a second,
whether or not the game's picture changes.
