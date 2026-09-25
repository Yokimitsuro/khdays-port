# The new-game difficulty select (ov000)

Everything below is read out of the decompiled ov000 (`khdays-decomp`,
`src/overlays/ov000`). Several decomp comments around these functions describe
them as "logo" code or a "save slot"; the code says otherwise, as noted.

## Where it sits in the flow

MODO HISTORIA > NUEVA PARTIDA confirms in `func_ov000_0204e5b0` →
`func_ov000_0204f51c` (sub-screen to black over 17 frames, then it creates the
class `data_ov000_0205ab0c` and returns) → `func_ov000_0204f610`, which each
frame runs the key repeat, stores the repeat mask into the sub-scene
(`func_ov000_020548c8`, +0x1c), renders the title node and polls the sub-scene's
result (`func_ov000_020548e0`, +0x4bc4):

| result | meaning  | next                                                   |
|--------|----------|--------------------------------------------------------|
| 4      | cancel   | reload the title art, back to `func_ov000_0204ea68`     |
| 5      | confirm  | raise ctx+0x4c40, `func_ov000_0204ebe4`, then the story start (`func_ov000_0204ef34` → `0204ee24`) |

## The sub-scene class

`data_ov000_0205ab0c`: class 8, group 14, constructor `func_ov000_02053e40`,
method (destructor) `func_ov000_02053f60`, a 0x4bcc-byte context published at
`data_ov000_0205ac28`. The constructor clears it, sets `+0x20 = 1`, allocates
four 0x800 screen buffers and starts loading `/UI/newgame/res.p2` and
`/text/font_eu_10all.NFTR`. `func_ov000_02053f08` waits for the load, then
builds the screen (`func_ov000_020532f0`, `02053524`, `020535b4`), stamps the
OS tick and hands over to the loop `func_ov000_02054108`.

The loop runs the mode function from `data_ov000_0205a86c` indexed by
`activeMode` (+0x4bc4), then the tracker (`func_ov000_020564f4`), the object
list (`func_ov000_0205577c`) and the dirty-BG flush (`func_ov000_02054668`).

| mode | function   | what it does |
|------|------------|--------------|
| 0    | `02054170` | sub master brightness `-(elapsed / 0x4cb5)` until elapsed > 0x4cb51 OS ticks (0.6 s), then `-16` and `activeMode = pendingMode` |
| 1    | `020541fc` | the same ramp up: `elapsed / 0x4cb5 - 16`, then 0 |
| 2    | `02054288` | the difficulty list |
| 3    | `020544b0` | the confirmation dialog |
| 4, 5 | `02054640`, `02054644` | empty -- the parent reads them as results |

Setup starts in mode 1 with `pendingMode = 2` (fade in, then the list).

## Mode 2: the list

- Input is the repeat mask (+0x1c): exactly `0x40` (Up) or `0x80` (Down) moves
  `selection` (+0x20) through 0..2 with wrap-around and SE 0.
- Otherwise the pad trigger: exactly `1` (A alone -- not Start) → SE 1,
  mode 3; exactly `2` (B) → SE 3, mode 0 with `pendingMode = 4` (fade out,
  cancel).
- `selection` is the **difficulty** (0 Principiante, 1 Normal, 2 Experto).

## Mode 3: the dialog

- The repeat mask selects the answer (+0x24): `0x20` (Left) → 0 (Sí), `0x10`
  (Right) → 1 (No), each change with SE 0.
- Trigger `1` (A): on Sí → mode 5 and `func_ov000_02054c50(selection)` -- the
  new-game state, with the difficulty in field 0x40a (the decomp calls the
  argument a save slot; it is the selected row); on No → mode 2 with SE 3.
  Trigger `2` (B) → mode 2 with SE 3.

## Resources

`/UI/newgame/res.p2`:

| sub-file | content | use |
|----------|---------|-----|
| 0 | D2KP: NCLR + NCGR + NSCR | sub BG palette and BG1 tiles (`func_ov000_020532f0`) |
| 1 | D2KP: NCLR + NCGR + NCER + NANR | the OBJ cells of the entry table |
| 2 | 1632 B, starts with 12 | the tile-region tracker's data (`func_ov000_02056544`) |
| 3 | 704 B = 8 × 0x58 | a `.ui` entry table (`func_ov000_020556c8`) |

For languages other than English, BG1's tiles come instead from
`/UI/newgame/res_i18n.p2`, whole (loaded at char offset 0): the firmware
language (`OSOwnerInfo`, 2 fr / 3 de / 4 it / 5 es) picks sub-file 2 / 0 / 3 / 1.

Text comes from `UI/newgame/ngm_&.s.z`: 0 the list header, 1 the dialog
header, 2-4 the difficulty names, 5-7 their descriptions, 8 the warning, 9
"Dificultad", 10 the question, 11 Sí, 12 No.

### Entry table (res.p2 sub-file 3)

| id | key | x, y | role |
|----|-----|------|------|
| 1 | 0 | 80, 64 | cursor; moved to the chosen entry + (8, 8) in the list, + (0, 8) in the dialog (`func_ov000_02053c3c`) |
| 2-4 | 3 | 72, 56 / 72 / 88 | the three difficulty plates; `selection + 2` is highlighted |
| 5, 6 | 1 | 40, 168 / 136, 168 | the dialog answers; `answer + 5` is highlighted |
| 7 | 5 | 56, 88 | dialog piece |
| 8 | 6 | 72, 56 | dialog piece |

`func_ov000_02053b0c` shows ids 2-4 and hides 5-8 in the list, and the reverse
in the dialog.

### Text (NitroSystem text canvas on sub BG3)

Three tile surfaces (`func_ov000_020535b4`, configs `data_ov000_0205a8d4`,
`0205a884`, `0205a8ac`; palette 15). Every string is drawn twice, first at
(+1, +1) in colour 1 as a shadow. Alignment flags: `0x209` left, `0x411`
centre, `0x821` right.

| surface | list (mode 2) | dialog (mode 3) |
|---------|---------------|-----------------|
| 0 (`02053740`) | string 0 at (0x8d, 2), right-aligned | string 1, same place |
| 1 (`020537fc`) | strings 2-4 at x 0x20 (+8 when selected), y 0xb + 16·i | string 9 at (0x40, 0xb) and the chosen name at (0x40, 0x2a), centred |
| 2 (`02053928`) | the description of the selection (5-7) at (0, -1) and string 8 at (0, 0x17), colour 4 | string 10 at (0, 4); 11 at (0x46, 0x19) and 12 at (0xaa, 0x19), centred |

## Not ported yet

The screen needs three engine pieces the port does not have: the NitroSystem
text canvas (`func_0202fec8` TileSurface, `func_0201449c` aligned text,
`func_02014024` glyphs), ov000's entry-table object manager
(`func_ov000_020556c8` and the `0205xxxx` accessors) and the tile-region
tracker behind sub-file 2 (`func_ov000_0205474c` blits into the four BG
buffers). Until they exist, NUEVA PARTIDA goes straight to the story start
with the default difficulty (1) that ov000 writes at boot.
