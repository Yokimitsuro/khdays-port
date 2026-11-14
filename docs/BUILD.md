# How the port is built

The game is the decompilation's C (`external/khdays-decomp`, read-only),
compiled for x64 Windows. `port/build.py` runs `port/tools/prepare.py`, which
turns that C into the build's sources in `build/port/gen-x64`, then configures
`port/CMakeLists.txt` with Ninja inside Visual Studio's x64 environment and
builds: `build/port/obj-x64` (Debug), `obj-release-x64` (Release) or
`obj-asan-x64` (`--asan`: AddressSanitizer on the port's own code).

Ninja learns which headers each file includes from cl's `/showIncludes` lines,
which a Spanish install of Visual Studio writes as "Nota: inclusión del
archivo:" in the console's code page, matched byte for byte against the prefix
CMake found when it first configured the tree. A tree configured in one code
page and built in another knew no file's headers, so a change to a header alone
rebuilt nothing. `build.py` therefore configures and builds in the system's OEM
code page, whatever terminal runs it.

## What prepare.py makes

- **The sources.** Each module's `delinks.txt` names exactly the files the DS
  build links, so every function has one provider. A few files get patched
  copies for MSVC (the decomp's C++ turned into C, below).
- **What the DS linker provided.** `hal_symbols.txt`: files whose only source
  is ARM assembly, which `port/hal` implements. `duplicates.txt`: globals
  several split files defined; one keeps the definition, the rest become
  `extern`. `ds_functions.c`: the native function at each DS address.
- **The rewrite** (`port/tools/references`). The game's C and the HAL are
  rewritten with every pointer a 4-byte reference: the DS address it had. The
  rewritten copies go to `rewritten/`; the data area's and the function
  references' numbers to `khdays_data_area.h`; the DS addresses the port's side
  reads to `khdays_ds_symbols.h`; `sources.cmake` lists the rewritten files
  (`KHDAYS_REWRITTEN`). Symbols at fixed DS addresses (the BSS no C file
  defines, `OVERLAY_n_ID`, the decomp's linker-absolute symbols) are constants
  of the rewrite.

## The hooks

The port meets the game at a few hundred points, each a text replacement in
the decomp's C applied before the rewrite:

- `HOOKS`: by the function's module and DS address (`"main@0201cac0"`), a list
  of `(old, new)` replacements in its file. Each entry's comment says what the
  port does there and where its side lives (`runtime/...`); what the game does
  at those points is in `RUNTIME_MAP.md`.
- `FILE_HOOKS`: the same by the decomp's file, for functions `HOOKS` cannot
  name -- DS Protect's (ov028) are data in the symbols, their bodies encrypted
  in the ROM, and several share a file.
- `UI_HOOKS`: the routines whose drawing the port takes over, as
  `(function, part, the port's function, kind)`. The game's body stays, renamed
  `<function>__khdays_game`, and the routine asks its part's mode (the parts:
  `port/ui/parts.h`; `KHDAYS_UI`). A "replace" routine runs the game's body
  (off), the port's with the same parameters (on), or both, the port's first,
  so the game's drawing can be compared (check). A "follow" routine always
  runs the game's body, then (check, on) the port's, told what the game was
  handed and what it returned.
- `ABI_FIXES`: the places where the C's meaning differs from the ROM's and no
  rule below covers it, each with the ROM's evidence; the decomp is told in
  `DECOMP_FINDINGS.md`.

## The ABI repairs

The decomp's C matches the ROM byte for byte, yet some of it leans on the ARM
calling convention: a call passes fewer arguments than its callee reads (the
callee finds whatever r0-r3 held), or a caller uses the "result" of a function
defined `void` (whatever it left in r0). Natively those are garbage.
`port/abi/ghidra_abi.txt` records, from the ROM
(`port/tools/ghidra/KhdaysAbiScan.java`, `ghidra_abi_targets.py`), what those
registers hold at each such call and return, and `abi_repair.py` rewrites the
C where that source is mechanical:

- **returns**: r0 is the result of the function's last call, or of a call it
  jumps to at the end, its first argument untouched, or a constant -- the
  function returns that;
- **arguments**: the missing register still holds the caller's own argument,
  the previous call's result, a constant or a data address -- the call passes
  it.

Everything else becomes a stop that names the call (`KHDAYS_ABI_GAP`),
resolved from the ROM when the game reaches it. Besides:

- Calls passing fewer arguments than the definition takes pass zeros for every
  missing word. On the ARM9 those are registers; natively the words past the
  argument registers are slots of the caller's own stack, which the callee
  owns: one that assigns to such a parameter (as the ARM code uses the
  register) writes over its caller's frame, and so may an optimizing compiler
  once the parameter is dead (MSVC `/O2` does).
- Functions whose address the C takes (callbacks, handlers, tables) are called
  through pointers, often with fewer arguments, which the padding cannot see.
  Their files are compiled without optimization (`#pragma optimize`):
  unoptimized, a function writes a parameter's slot only where its C assigns
  it. (Optimized, casting Fire returned to address 0 with its saved registers
  zeroed.)
- A prototype declaring a parameter narrower (`char`, `short`) than the
  definition takes it: the ARM caller passes it extended in its register,
  which the definition reads whole; a native caller defines only the narrow
  part. The prototype takes an `int` and each call converts the argument
  itself. The same for a narrow result declared 32-bit (`FX_Atan2` returned
  negative angles 0x10000 too large).
- Locals the C never sets and reads only to add `(x - x)` -- the lever that
  makes mwcc copy a register as the ROM does (`RandNextScaled(n) + (v - v)`) --
  start at 0: the sum is the same, but reading an unset local is undefined
  behaviour an optimizer may act on.
- A struct argument loaded into r1-r3 (`ldmia`), a 64-bit parameter (two
  words) or a `VecFx32` by value (three) is passed as the DS laid it out (the
  rewrite's block, below). A caller expecting a struct by value where the
  definition takes the ARM ABI's result pointer gets that pointer, as MSVC
  passes it for a struct over 8 bytes; since it would be a host pointer, the
  rewrite stops on such declarations and `FILE_HOOKS`/`ABI_FIXES` fix them.

## The registers

`registers.py` rewrites the game's accesses to the DS's I/O registers so that
none faults. The I/O pages stay protected in the game's view: an access this
misses still faults, and the trap reports it. By the register's kind (where
each kind's effect is: `runtime/core/io_regs.c`):

- **Holds a value** (the 2D of both engines, the DMA's sources, destinations
  and fill, EXMEMCNT, POWCNT, the 3D's settings...): its macro, the casts of
  its address, a pointer set to it, an SDK function given it all reach
  `khdays_io_plain(offset)` -- plain memory, the bytes the port reads when it
  draws. Only where everything the access can reach holds values: a pointer's
  reach is read off its uses in the C, an SDK function's off its own C
  (`PLAIN_CALLEES`).
- **Acts when written** (DMA control, IF, VRAMCNT, the card's ROMCTRL, the
  geometry's ports, DISPSTAT) **or is computed when read** (KEYINPUT, the
  geometry's results, the card's data port): its statements become
  `khdays_io_store(address, width, value)` (a compound assignment loading
  first) and its reads `khdays_io_load(address, width)`.
- **Reached through an address computed at run time** (a DMA channel's, a
  timer's, a pointer the C moves): the pointer keeps the DS address and each
  access through it is routed at `&(lvalue)`, right whatever the kind.

Before that the C's spellings of an address are brought to one literal (its
macros, constant sums such as `HW_REG_BASE + 0x440`, accessor macros), and a
struct laid over registers is laid out from its definition. The math unit and
IME/IE are `prepare.py`'s `UNTRAPPED_REGISTER`; the geometry's word stores
through casts its `GEOMETRY_STORE`; the port's SDK routines (`HAL_CALLEES`)
and the DMA's (`VALUE_CALLEES`) take a register's DS address as it is; a few
files read by hand are `REVIEWED_PLAIN_FILES`. Whatever else names a register
is listed in `registers_left.txt` with its line, for a hook or a rule.

## The references

`port/tools/references` parses each file, types it (`types.py`) and emits it
again (`emit.py`) with every pointer of the game's side a 4-byte reference.
Each expression is emitted as a *value* (a pointer as its reference, an
integer as itself) or a *host lvalue* (`(*(T *)KHDAYS_W(r))` for an object
reached through a reference, a local by its name): the window is added only
where a reference is dereferenced.

Some functions take their arguments as the DS laid them out: one block of
4-byte slots in the caller's frame, in the window, its reference the one
parameter `khdays_va`; a call to one stores its arguments in such a block
first. They are those declared or defined with a variable argument list
anywhere (the decomp's `va_start` reaches the arguments from the last named
parameter's address, as mwcc's stack had them), and those a file declares
otherwise than their definition takes them: `Ov000_IsSelectionMoveCollisionFree`
is defined `(int, int, int, ...)` and called with a struct by value where it
reads two ints; `Ov006_MissionPlaceCell` is defined with a struct by value and
called through `(void *, int, ...)` with its four words. On the DS the words
land in the same places either way; in a block they do on any host
(`build.py` `block_abi`).

A declaration from the port's own headers (`runtime/`, `host/`, `gfx/`,
`sound/`, `compat/decomp_compat.h`) is the port's: its types keep host
pointers, and the game's side converts at its uses.

The game's own routines (`rewritten/src/`: not `libs/`, not the HAL) are
emitted *routed* for the native plugins (`emit.py` `routed`): a routine's body
becomes `khdays_impl_<name>`, a pointer `khdays_route_<name>` holds it, and
`<name>` itself calls through that pointer, so every call -- the game's, a
function reference's -- reaches whatever a plugin put there (kh.engine
`replace`, `docs/MODDING_NATIVE.md`). A record of the three goes to section
`.khrt$m`, which `runtime/plugins/engine.c` reads. Static routines, those taking
a variable argument list or their arguments in a block, and those with an
unnamed parameter stay as they were. The cost, measured with `KHDAYS_PROFILE`
in a mission (2026-10-31): the game thread's running time per second went from
30.8 to 31.5 ms (median), the menus' unchanged.

## Memory, linking and the ROM

- **Game data at its DS address.** Every object a C file defines that the DS
  linker placed (`symbols.txt`) becomes an absolute symbol at that address, so
  all code meets it where the DS had it, including code that reaches other
  memory relative to it. An initialised definition keeps its initializer under
  a second name and registers it (`KHDAYS_DATA_INIT`); the runtime copies it
  into place when its module loads, over the image the DS itself loaded there,
  so pointers to functions in it are native.
- **Functions at their DS addresses** (`ds_functions.c`), for a call through a
  DS address the ROM's own data holds, such as an overlay's static
  initializers (`runtime/core/window.c`, `overlays.c`). The ITCM's are listed
  with their sizes: the MobiClip decoder is copied over the ITCM while a movie
  plays, so one runs natively only while its bytes are still the game's
  (NitroSystem's G3D calls two through tables at 0x01ffa1f8 and 0x01ffa598).
- **Addresses several overlays share.** The decomp's relocations name them all;
  on the DS the one loaded runs, but the C names one of them, not always that
  one (ov012 opens its movie through `func_ov008_020846c0`, ov008's shop list,
  where the ROM reaches ov024's MobiClip open at the same address). Natively
  such a reference goes through a thunk to the loaded overlay's function.
- **Split statics** (`PHANTOMS`): globals some split files define that
  `symbols.txt` does not name, statics of one original translation unit
  repeated in each file the decomp split it into. On the DS they are fields of
  a block the module's symbol anchors (the decomp's `tools/share_bss.py`);
  natively they are references at the addresses the ROM's code reaches them
  by (base + offset from its disassembly). A used one missing fails the link.
- **Code only the ROM has.** What only an assembly file holds and `port/hal`
  does not implement is the ROM's bytes at its DS address: data is read there;
  a call into code there stops the game, naming it. `rom_code.txt` lists what
  may be code: MobiClip's routines the game never reaches (the audio decoders
  of kinds 1 and 2 -- every movie with sound is kind 3 -- and the deblocking
  filter, whose one caller passes mode 0) and a label inside
  `func_0200302c`, which the HAL has.
- **Replaced modules** (`REPLACED_MODULES`): ov105, NitroSDK's wireless
  library (WM, its ARM7 protocol, `wh.c`), which the game calls at 24
  functions; none of its files is built and `port/hal/sdk_wireless.c` defines
  them. A call to anything else in a replaced module stops the game, naming it.
- **The decomp's C++** (ov024's MobiClip stream layer) as C: its classes are
  only declared, to call through vtables that are the game's data. After
  `transform()` (mwcc's two destructor slots in place, `VIRTUAL_DTOR`; every
  member `__cdecl`; `delete` spelled out) a class is a struct with its vtable's
  reference first, and a virtual call a call through the vtable's slot with the
  object first -- the call the ROM makes (ov024 0x02084ec4:
  `ldr r1,[r0]; ldr r1,[r1,#n]; blx r1`).

## Multiplayer: who asks "am I the host"

The battle is host-simulated: a host runs every character by
`Ov022_ActorUpdate`'s host branch from the inputs `Ov022_RefreshSlotRows`
copies in, and a guest replays the host's states. With prediction on, a
guest's own character runs the host's branch from its own last control (copied
in by the game's `Ov022_LoadNodeFromResource`), the host's state only
correcting it, and what that branch sends is left to the host
(`runtime/net/mp_predict.c`).

Inside that update the game's "am I the host" checks -- the local player's
index against 0 (`ROLE_QUERY`, `ROLE_QUERY_VAR`) -- answer through the port
(`khdays_mp_role`) as a host's; at every other moment, as the game's. A query
compared with an owner or a slot ("is this my character") stays the game's.
Only a character's own run asks through the port (`ROLE_DIRS`: ov022's actors
and the characters' overlays); what the character does to the others and to
the world -- hits delivered or taken, HP, items, abilities, the mission's
tallies, the shared tables, the members' events, the network (`ROLE_KEPT`) --
stays the game's answer, so a guest never makes locally what the host decides
and sends.

## The regression walks

`port/tools/regress.py` plays the native build through scripted input without
touching anyone's save. Every run happens in a new temporary directory holding
a link to the repository's `data/` and a copy of the save.

```
python port/tools/regress.py [EXE] --walk-save OLD.sav [--player-save saves/khdays.sav]
                                   [--only NAME] [--record] [--env NAME=VALUE ...]
python port/tools/regress.py [EXE] --input FILE --save PATH --log PATH
                                   [--env NAME=VALUE ...] [--timeout SECONDS] [--window] [--keep]
```

The first form plays the walks: routes every change must still complete,
scene for scene (the Mission Mode loop, Story Mode, a mission in play,
challenge 07, a save from the camp menu into a new file, the camp's reset, and
multiplayer with two or four copies of the game on this PC, on the local
network and through a local `khdays-server`). The save stays untouched except
by the walk that saves, whose file must hold every copy it signed whole
(`port/tools/save_file.py`). Their scripts and the scene sequences they must
produce are in `port/tools/regress_walks/`. The second form runs one script,
for traces and experiments. `EXE` is the Release build's unless given;
`--settings FILE` starts the game with a copy of FILE as its settings, `--mods
DIR` with DIR as its mods (or, when DIR is one mod's own folder, with that mod
alone).

### The input scripts

`KHDAYS_INPUT` names a script of presses for runs without a window
(`runtime/input/input.c`; the walks' `.input` files): `FRAME+KEY` presses a key
and `FRAME-KEY` releases it (`A B X Y L R START SELECT UP DOWN LEFT RIGHT`, or
the port's `LOCK_ON TARGET_NEXT TARGET_PREV MENU_UP MENU_DOWN MENU_LEFT
MENU_RIGHT`); `FRAME@X,Y` and `FRAME@-` touch and lift; `FRAME~X,Y` and
`FRAME^X,Y` hold the movement and camera sticks (-32767..32767, y up; `0,0` lets
go). After `sN` the frames count from scene N's first frame, after `uName` from
the update `Name`'s first run, and wait for it.

## The port's tests

`ctest --test-dir build/port/obj-release-x64` runs them (`port/tests`, the
installer's tests, the fuzzers for 15 seconds each, the server under a flood,
and in the Release tree the static analysis). A test that needs the player's
data (recorded frames, the ARM7 binary, a video) is skipped without it: those
never belong in the repository.

- `elements_test.c`: the port's screen elements in the composition, on the
  CPU: fixed screens (a BG layer with a tile, and elements standing in for it)
  composed by `gfx/compose.h` as the game's frames are.
- `input_test.c`: the control profile and its presets, the bindings, the
  devices (SDL's virtual gamepads, no window), the settings menu, and the pure
  parts of the enhancements (analog movement's arithmetic, the command menu's
  steps on the D-pad). Each failing CHECK prints where and what.
- `movie_hd_test.c`: the HD movies' course (`runtime/movies/movie_hd.c`)
  against a scripted game and video: the ids from the game's paths, the lookup,
  every transition, the buttons the game is given, the clock's pause reasons.
- `movie_hd_decode_test.c`: the HD video decoder on one of the player's files,
  played to its end at its own pace with its sound, every picture taken as a
  60 Hz display would; the status must say it ended within a second of the
  file's length.
- `net_test.c`: the session's delivery between two processes on this machine
  (the test starts itself again as the host): they find each other's room,
  join, and the guest's records reach the host once each, in order, with 30% of
  the datagrams each way dropped (`KHDAYS_NET_LOSS`); waiting records are
  joined, never past the limit; a datagram that is not the port's is ignored.
  Then the controls: sent faster than the host's battle ticks, no hand-over
  holds two, every command and change of the buttons arrives in order; and at
  the battle's own pace, with the host's game sending more than once a tick
  (its state each tick and a reliable message every frame, as with enemies
  about), no control waits for the game longer than half a second (a store
  that took each send for a tick once ran dry on every extra one and left the
  guest's controls a minute late).
- `onescreen_test.c`: the one-screen picture on the CPU: the compositions'
  places by the port's rule at 4:3, 16:10 and 16:9 and HUD sizes 1, 4 and 8
  (lengths, `fixed`, scales, corners and margins, `cover`, the 3D shapes, the
  common rule, `wide`/`narrow`), the port's own file, the pass's steps on fixed
  texels, and `kd_split` giving today's pixel when nothing moves.
- `render_test.c`: recorded frame packages (`KHDAYS_RECORD_FRAMES=saves/frames`
  with `KHDAYS_SHOTS` or a windowed run; `khdays-render-test [DIR]`), each a
  package as `display.c` hands it over after a 16-byte header. 1: each must be
  whole, and engine A composed again from its 3D and composition records must
  be the 2D engine's picture, pixel for pixel. 2: with a graphics card, engine A
  is drawn by `gpu_raster3d.c` at 1x, 2x and 4x, by `raster3d.hlsl` and by the
  card's own rasterizer, and compared with the software's: where the 3D cannot
  show, every target pixel must be the 2D engine's exactly; where it takes part
  each DS pixel is compared with the average of its N x N (at 1x at most 0.01% of
  the pixels may differ by `raster3d.hlsl`, a channel by at most
  `MAX_MEAN_NATIVE_AT_1X` on average by the card's rasterizer; at N x a channel by
  at most `MAX_MEAN_AT_NX` on average). `KHDAYS_RENDER_TIMES=N` draws each package
  N times more at `KHDAYS_RENDER_SCALE` (8) and says the time a picture took, by
  the renderer `KHDAYS_3D` names. 3: packages close together
  whose lists differ are put through the interpolation at half way, and the
  matched polygons that moved more than a quarter of the view are reported by
  draw (said, not failed). `KHDAYS_RENDER_OUT=DIR` writes each frame the GPU
  drew. A package recorded wide is drawn as wide, with its polygons wholly
  beside the DS's columns counted.
- `save_test.c`: the save written so a crash cannot corrupt it: a child process
  writes two 64 KB images in turn as fast as it can and is stopped at a random
  moment, 100 times; the file must be one of the two, whole.
- `saveslots_test.c`: the save files beyond the DS's three: SHA-1 on its known
  answers, what each file holds as the game's read tells it, the chip's view and
  the port's, a save into a port's file committed and read back, the file saved
  last, the page view kept and put back (the autosave's write), the highest save
  counter, and a commit that fails (a read-only file): the game told so and the
  file as it is on the disk, nothing of it left for the next commit.
- `savestate_test.c` (`khdays-gpu-test STATE.bin OUT.ppm`): a DeSmuME
  savestate's display state (from `savestate_gpu.py`) rendered with the native
  2D engines and compared with the framebuffer DeSmuME showed; the upper screen
  is engine A or B by which matches better (the savestate does not keep
  POWCNT1's swap bit).
- `sound_test.c`: the sound without the game. The channels and the DS's mixer
  must give, sample for sample, a fixed program's output as the code gave it
  before it moved (a hash); the PC's mixer is within one 10-bit step of it but
  where the DS clips; the volumes scale as they should; the sound bridge never
  runs the driver past the clock and every reply reaches the FIFO in order; the
  stream reader gives what a ring filled in time would hold (PCM16 stereo
  looped, PCM8 mono, IMA-ADPCM started and looped inside a block); the native
  driver answers a movie-like command list with the right registers, shared
  work and alarm at the right cycle (needs the ARM7 binary); a replacement's
  loop is read from each source, a broken file refused, and a dumped original
  put back plays sample for sample like the original; the offline render ends a
  one-note sequence when its note dies and finds a loop as long as its jump.

## The tools

In `port/tools` (Python 3.11+ unless said):

- `prepare.py`, `abi_repair.py`, `registers.py`, `references/`: the build's
  preparation (above). `references/` runs in two passes across processes: what
  each file defines with external linkage (the game's side is what these define,
  the port's everything else), then the emission with that index and the
  decomp's symbols; then the data area is laid out (each object at its DS
  layout's size and alignment: pointers 4 bytes, members aligned to at most 4,
  bit-fields as MSVC packs them, every size checked by a `_Static_assert`) and
  written with the function references to `khdays_data_area.h`. A source's
  results are kept in `gen/references/` and redone only when its files, the
  index or the rewrite's code changed. Sources are preprocessed with MSVC's
  preprocessor as the build compiles them, MSVC's dialect mapped to ISO C for
  the parser; the type pass models what the game's C uses and stops, naming
  the file and line, on anything it cannot type (`TypeFailure`): nothing is
  rewritten by a guess. `references_test.py` checks each form it handles.
  `prepare.py` itself stops at once when nothing it reads changed since its last
  complete run into the same `--out` (the sizes and modification times of the
  decomp, `port/tools`, `port/abi`, `port/compat`, `port/hal` and every header of
  the port, kept in `prepare_inputs.json` with those of what it wrote); `--force`
  prepares anyway. Neither this nor the references cache follows Visual Studio's
  preprocessor: after changing Visual Studio, delete `--out` (`build/port/gen-x64`).
- `build_shaders.py [name ...]`: compiles `port/shaders/<name>.hlsl`'s stages
  (`vs_main`, `ps_main`, `cs_main`) to DXIL (`dxc -T <stage>_6_0`), SPIR-V
  (`dxc -spirv`) and MSL (`spirv-cross --msl`), written as C arrays to
  `port/host/render/generated/<name>_<stage>.h`. The build never runs it (the
  headers are committed); run it after changing a shader, with the Vulkan SDK
  installed. SDL3's GPU resource layout: vertex shaders' textures and samplers
  in space0, uniforms in space1; pixel shaders' in space2 and space3 (for
  SPIR-V a texture and its sampler one combined image sampler:
  `[[vk::combinedImageSampler]]` and the same `[[vk::binding(n, set)]]`);
  compute shaders' read-only textures then buffers in t registers of space0,
  read-write ones in u registers of space1, uniforms in b registers of space2
  (a storage image needs `[[vk::image_format(...)]]`). A storage buffer is a
  `ByteAddressBuffer`, never a `StructuredBuffer`: SDL's Direct3D 12 views
  every buffer raw (no stride), and AMD's cards and vkd3d index a structured
  one by the view's stride, so every element read is the first (SDL issue
  12200; NVIDIA's cards read it right, which hides it).
- `analyze.py [--tree ...]`: builds the `khdays-analyze` target -- the port's
  own sources (not the decompilation's, not the third parties') compiled again
  with MSVC's `/analyze` and their real flags, not linked -- and lists every
  finding (C6xxx, C26xxx, C28xxx, C33xxx); exit 0 only with none. A finding
  that is no defect is suppressed at its line with the reason:
  `#pragma warning(suppress: 6387) /* why */`.
- `fuzz.py [--tree ...] [--target NAME] [--minutes N] [--jobs N]`: runs a fuzzer
  from its kept inputs in `port/fuzz/<name>/` for N minutes on N processes, from
  a scratch folder (libFuzzer restarts its processes with an unquoted command
  line, and the repository's path has spaces). A crash's or hang's input is
  copied back to `port/fuzz/<name>/`; a run that ends well before its time
  fails.
- `regress.py`: the walks (above); `save_file.py A.sav [B.sav]` reads a save the
  way the game signs it: a slot s is two copies of 0x2018 bytes at
  (2 x s) x 0x2018 + 0x20, each the magic 0xc8f592a6 then its SHA-1 (of the
  0x1cac bytes from +0x18) at +4. With one file it says which copies are signed
  and still match; with two, whether they are the same save -- every byte equal
  but each copy's digest and the word at +0x1b0 (two saves of one script
  differ there, as measured; taken to be the counter the game adds the play
  time to before signing, not traced further).
- `catch_crash.py [EXE] [--out FILE] [-- ARGS...]` (Windows): runs the port under
  a small debugger of its own and says where it died. A function's stack guard
  (`/GS`: exception 0xc0000409, "fast fail") stops the process with nothing in
  its log and no handler; a debugger is told. This one starts the game (the
  Release build by default, from the repository's folder), passes back every
  exception the port takes on purpose (its I/O traps), and on a fast fail or
  an unhandled exception reads the failing thread's registers and stack's
  return addresses and names them with the build's symbols (`llvm-symbolizer`).
  The report goes to the console and to `crash.txt`.
- `check_prototypes.py`, `ghidra_abi_targets.py`, `ghidra/KhdaysAbiScan.java`:
  the ABI scan that makes `port/abi/ghidra_abi.txt`. `check_prototypes.py`
  lists every function whose declarations elsewhere take fewer parameters than
  its definition, or declare a value it does not return.
  `ghidra_abi_targets.py` turns those into ROM addresses (`A <function> <space>
  <address>`: a void function whose value callers use; `B <caller> <space>
  <address> <callee> <callee space> <callee address> <declared> <defined>`: a
  short call; the space `arm9_ovNNN` or `-`), with `--missed` (only the calls
  the scan found nothing for), `--next` (the calls of functions the last build
  widened) and `--merge` (a rescan's findings into the file). The Ghidra
  script, read-only, writes for each one line: `A <function> <return address>
  r0 <- <source>` or `B <caller> <callee> <call address> r<k> <- <source>`, the
  source the instruction that last wrote the register on that path (`call
  <name>`, `entry:rK` for the function's own argument, `tail <instruction>`).
- `decomp_findings.py`: refreshes `DECOMP_FINDINGS.md`'s generated sections
  (between `<!-- BEGIN generated:NAME -->` and `<!-- END ... -->`: `revision`,
  `ambiguous`, `repairs`, `gaps`, `narrowparams`) from `prepare.py`'s output.
- `savestate_gpu.py STATE.dst OUT.bin`: a DeSmuME savestate's display state for
  `savestate_test.c`. A `.dst` is "DeSmuME SState\0\0", the uncompressed size at
  0x18 and a zlib stream at 0x20; inside, chunks of (u32 id, u32 size, data),
  their fields (tag[4], u32 size, u32 count, data). Chunk 4 holds 9REG (the
  ARM9 I/O registers), VMEM (palettes), OAMS (OAM) and LCDM (all VRAM banks in
  LCDC order); chunk 7 a version word and the two 256x192 BGR555 framebuffers,
  upper first. OUT.bin: io (0x2000), palette (0x800), oam (0x800), vram
  (0xa4000), framebuffers.

## Compiler settings worth knowing

The game's rewritten sources (`khdays-game`) include only
`compat/khdays_references.h` and the build's numbers. Struct members align to
at most 4 bytes, as mwcc lays them out for the ARM9 (`/Zp4`: a u64 inside a
struct sits at a 4-byte offset; `OSContext`'s divider context puts the thread
state at +0x64, not +0x68). mwcc ran with `-lang c99 -char signed -enum int`;
MSVC's char is signed and its enums are int already.
