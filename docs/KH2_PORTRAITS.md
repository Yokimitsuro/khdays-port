# Animated KH2 HUD portraits

Put `portraits.json`, the BAR archives, and their DDS or PNG atlases in an
enabled mod's `graphics/UI/btl/main.p2#1/chr/` directory. Launch normally and
restart after editing settings. The HP gauge replacement is optional.

[The example JSON](examples/kh2-portraits.json) lists all 21 character names,
with separate `player` and `party` mappings. It enables Roxas's player portrait
from `P_EX110.a.us`, and Luxord's party portrait from `P_EX020.a.us` (Donald art
in the supplied test file). Other mappings are disabled. Names are case sensitive,
including `Saix`, `Xion_Hood`, and `Roxas_Hood`.

| Setting | Meaning / default |
| --- | --- |
| `enabled` | Enable this character and role; `false`. |
| `archive`, `texture` | Filenames in the JSON's directory; required when enabled. Archive extensions are unrestricted. |
| `image_entry`, `sequence_entry` | Independent BAR entry names, 1–4 bytes; both `face`. Select type `0x18` IMGD and type `0x19` SEQD respectively. |
| `offset_x`, `offset_y` | DS pixels from the animated parent; `0`. |
| `scale` | DS pixels per sequence unit; `0.65` for either role, matching the HP gauge. |
| `source_origin_x`, `source_origin_y` | Logical origin subtracted from animated vertices; `0`. Relative translation remains animated. |
| `canvas_padding` | Extra DS pixels around evaluated geometry; `4`. |
| `frames_per_tick` | Source frames per Days display tick; `1`. Pause stops playback. |
| `normal`, `alert`, `dead` | Base clip indices; `3`, `0`, `2`. |
| `damage_small`, `damage_medium`, `damage_large` | Reaction indices; all `1`. `-1` disables a reaction. |
| `play_intro`, `intro` | Optional one-pass entry clip; `false`, `-1`. |
| `animation_override` | Force a clip for inspection; `-1` uses live states. |

The texture extension selects the existing decoder, case insensitively. DDS
supports BC3/DXT5 and uncompressed 32-bit; PNG uses the existing image decoder.
The logical IMGD size scales UVs to the actual replacement size without changing
geometry. The supplied Roxas atlas is logically 256x128 and physically 1024x512;
Donald's is logically 48x48 and physically 192x192. No archive edits are needed.

Damage plays a reaction and returns to the current base state. Losing at least
one quarter of maximum HP selects large, at least one eighth selects medium,
and less selects small. This classification is a port policy; all three default
to KH2's single face damage clip. Defeat interrupts damage. Low HP uses Days'
existing actor predicate, including its ability exception.

With the KH2 gauge, the local portrait follows its black-center translation,
scale and opacity, including the gauge's portrait offsets. Its own offsets apply
afterward. Party portraits follow their own small gauge's centered black backing
through the same slot-based parent interface. Without a replacement gauge, either
role uses its native portrait tile center. The default scales are initial fitting choices.

Disabled, missing or unsupported replacements leave their original tiles intact.
The usual HD sheet replacement then applies, followed by the DS sprite when no
HD replacement exists. Invalid JSON logs an error and leaves existing portraits.
Each mapping loads once; there is no repeated disk access during playback.

## Implementation and provenance

Days decompilation commit: `a2b0478b87e3102732094c34f6ca89595f98ab93`.
`Ov002_CreateAndRestoreHud` maps member kinds through `data_ov002_0207ef68`
and puts the local multiplayer member in slot 0. `Ov002_OpenPanelScreen`
retains mapped member ID/icon pairs at context `+0x130`. The adapter uses those
pairs rather than assuming actor IDs equal portrait rows.

`Ov002_LoadPanelSlots` copies portraits to offsets `0x1800`, `0x18c0`,
`0x1920`, and `0x24c0`, with `0x400` row stride. The player copies six rows of
`0xc0` bytes; party slots copy three rows of `0x60`. Only these tiles on engine A
BG3 are suppressed, after confirming their `UI/btl/.../main.p2` origin and
successfully preparing the replacement. Other HUD tiles keep their current paths.

KH2's `dk::GAUGE_PARTY::calcHPStatus()` at `0xa05dc` assigns normal/alert/dead
statuses 0/1/2. The supplied `dk::FACE::setFaceType(int)` at `0x99d28` maps
those statuses to clips 3/0/2. `dk::FACE::setFaceType(int,int)` at `0x99c30`
selects damage clip 1. The supplied PC resources have SEQD versions `0x160` (Roxas) and
`0x150` (Donald), both with 48-byte headers and record strides 44/20/4/144/36.
This verifies these resources' layouts, not every possible feature of `0x150`.

Slots have independent playback and reusable GPU images. Four portrait command
blocks accompany the existing gauge block. The live path uses the existing GPU
sequence renderer with cached atlases and no per-frame CPU pixel uploads. Images
grow only when necessary and are bounded to 512 DS pixels per dimension. The CPU
reference path remains available. No decompilation or generated Days sources change.

## Validation

`khdays-kh2-portrait-test` covers character/role selection, fallback, tile ranges,
DDS and PNG decoding, animation transitions, parenting without a gauge, and
absence of repeated live image allocations/uploads. Its optional argument is
the local `chr` directory, to evaluate every frame in both supplied archives.

`khdays-kh2-gpu-test --portraits <chr-directory>` compares representative frames
from all four supplied clips against the CPU renderer. Synthetic GPU tests also
check multiple images in one submission and transparent-frame clearing.

Manual game checks: Roxas player, Luxord party, damage, low HP, defeat/revival,
pause, mission mode, widescreen and both screen layouts; then disable a portrait
to check HD/DS fallback and disable the gauge to check independent placement.
Original-KH2 pixel matching retains the limitations in
[KH2 sequences](KH2_SEQUENCES.md).
