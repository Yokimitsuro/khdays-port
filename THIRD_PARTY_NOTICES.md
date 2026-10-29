# Third-party notices

This file records code and components that are not covered solely by this repository's MIT license.

## `khdays-decomp`

Repository: <https://github.com/Yokimitsuro/khdays-decomp>

The decompilation and tooling contributed to that repository are dedicated to the public domain under **CC0 1.0 Universal**, subject to the scope and disclaimers stated by that project.

Files copied or adapted from `khdays-decomp` should retain a clear source note containing:

- the original file or symbol;
- the source repository;
- the pinned source commit;
- substantial port-specific modifications.

## SDL3

Project: Simple DirectMedia Layer

Upstream repository: <https://github.com/libsdl-org/SDL>

Pinned release: **3.4.12** (`release-3.4.12`)

Usage: downloaded by CMake with `FetchContent`, compiled as a static library, and linked into the native runtime and into khdays-studio, the mod tool (`studio/`), with the same release and hash.

License: **zlib License**.

SDL3 is used for the native window, the picture (its GPU API -- Direct3D 12, Vulkan or Metal -- and its renderer), events, timing, sound output, keyboard, mouse and gamepad input, the host's threads and locks, and future platform abstractions.

## apicula (format reference and validation tool)

Project: apicula — Nintendo DS model tooling

Upstream repository: <https://github.com/scurest/apicula>

License: **0BSD** (BSD Zero Clause License).

Usage: **not linked or embedded**. apicula is used as an external command-line
tool to convert user-provided NSBMD models to glTF for cross-checking the native
mesh decoder, and its public source was read as a reference for the Nintendo DS
MDL0 binary layout (bones, inverse-bind matrices, render commands, and GPU
vertex commands). The decoder in `assets/src/mesh.cpp` is an independent
reimplementation; no apicula code was copied.

## stb_image and stb_image_write

Project: stb — `stb_image.h` and `stb_image_write.h`

Upstream: <https://github.com/nothings/stb>

Pinned commit: `2c980bb59875b0d32144a71867fbdebb2f77cd20` (`stb_image.h` v2.30)

License: public domain (Unlicense) or MIT, at your option.

Usage: fetched by CMake and compiled in. `khdays-assets` (via
`assets/src/image.cpp`) reads mods' textures with `stb_image.h`. The native port
(`port/host/render/png_io.c`) reads PNG files with `stb_image.h` and writes them
with `stb_image_write.h` (HD screenshots, and the graphics dump to come).
khdays-studio, the mod tool (`studio/`), compiles those same port
files (`port/host/render/png_io.c`, `assets/src/image.cpp`) as they are, at the same
commit.

## cgltf

Project: cgltf — single-file glTF 2.0 loader

Upstream: <https://github.com/jkuhlmann/cgltf>

Pinned commit: `85cd62382dfea638278962690cf515023f33ed00`

License: **MIT**.

Usage: fetched by CMake and compiled in. `khdays-assets` (via
`assets/src/gltf.cpp`) and the
native port (`port/runtime/mods/models.cpp`, through the same file) read the
glTF models mods bring; khdays-studio (`studio/`) compiles the same file to
read them as the port does, and lists a glTF's buffers and images with cgltf.

## stb_vorbis

Project: stb — `stb_vorbis.c` (Ogg Vorbis audio decoder v1.22)

Upstream: <https://github.com/nothings/stb>

Pinned commit: `2c980bb59875b0d32144a71867fbdebb2f77cd20`, the same as
stb_image above.

License: public domain (Unlicense) or MIT, at your option.

Usage: compiled into the native port's sound library
(`port/sound/third_party_decoders.c`) to decode Ogg Vorbis files that
replace the game's music, effects and voices from `mods/`; khdays-studio
(`studio/`) compiles the same file to check and play them as the port does.

## pl_mpeg

Project: PL_MPEG -- MPEG-1 video decoder, MP2 audio decoder, MPEG-PS demuxer

Upstream: <https://github.com/phoboslab/pl_mpeg>

Pinned commit: `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`

License: MIT (SPDX header in `pl_mpeg.h`), Copyright Dominic Szablewski.

Usage: fetched by CMake (only `pl_mpeg.h` is used) and compiled into the
native port (`port/runtime/movies/video_mod.c`) to decode the MPEG-1 movies a mod
puts in the place of the game's (`mods/<Mod>/movies/<id>.mpg`).

## dr_flac

Project: dr_libs — `dr_flac.h` (v0.13.4)

Upstream: <https://github.com/mackron/dr_libs>

Pinned commit: `dfe8377631000664666519fdb83da193fd8037f4`

License: public domain (Unlicense) or MIT No Attribution, at your option.

Usage: fetched by CMake (only this header is used) and compiled into the
native port's sound library (`port/sound/third_party_decoders.c`) to decode
FLAC files that replace the game's music, effects and voices from `mods/`;
khdays-studio (`studio/`) compiles the same file to check and play them as the
port does.

## Vulkan SDK (build tools)

Project: LunarG Vulkan SDK — `dxc` (the DirectX Shader Compiler) and
`spirv-cross`

Upstream: <https://vulkan.lunarg.com/>

Usage: **build tools only, not linked or redistributed**.
`port/tools/build_shaders.py` compiles the HLSL shaders in `port/shaders/`
with `dxc` to DXIL (Direct3D 12) and SPIR-V (Vulkan), and with `spirv-cross`
from the SPIR-V to MSL (Metal); the results are committed as C arrays in
`port/host/render/generated/` and compiled into the executable, so building the port
needs neither tool. At runtime SDL3's GPU API hands them to the system's
graphics driver.

## ufbx

Project: ufbx -- a single-source FBX loader, by Samuli Raivio

Upstream repository: <https://github.com/ufbx/ufbx>

Pinned version: **v0.23.1** (tag object `35a37776ee1163a74609f4ee75d162b7ada099ac`, commit
`26a482ae66871d7de36eb722aa060bce95bce274`, 2026-09-25). Only its three files are fetched, each
checked by its SHA-256 (`assets/fetch_ufbx.cmake`):

- `ufbx.c`, 1,181,511 bytes, `4a956c26a708e40d82ecb0aeaee54da709d5ba7c144c9642174970059bda7e1d`
- `ufbx.h`, 223,898 bytes, `f787be529af577efc04f3e3e735844a08556718dbb613d1211c2b313d72895dc`
- `LICENSE`, 2,631 bytes, `0dd48ebadf52273c736256325c8f078c03c8bb4facee22a4122de0ad3f615391`

Usage: downloaded by CMake, compiled unmodified as a static library and linked into the native
runtime and the asset library, to read a mod's FBX models (`assets/src/fbx.cpp`); also
into khdays-studio (`studio/`), which reads them as the port does and lists an FBX's textures.

License: **MIT or Unlicense (public domain)**, the user's choice (its `LICENSE`); this repository
takes it under the MIT License, copyright (c) 2020 Samuli Raivio.

## Dear ImGui

Project: Dear ImGui -- an immediate-mode graphical user interface library, by Omar Cornut

Upstream repository: <https://github.com/ocornut/imgui>

Pinned version: **v1.92.9b** (2026-07-31), fetched by CMake as
`https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz`, 2,123,074 bytes, checked by
its SHA-256 `21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99`
(`studio/CMakeLists.txt`, `port/CMakeLists.txt`).

Usage: compiled unmodified as static libraries into two programs:

- **khdays-studio** (the mod tool): `imgui.cpp` (979,959 bytes), `imgui_draw.cpp`
  (381,779), `imgui_tables.cpp` (270,549), `imgui_widgets.cpp` (555,245),
  `backends/imgui_impl_sdl3.cpp` (44,853), `backends/imgui_impl_sdlgpu3.cpp` (37,443) and
  `misc/cpp/imgui_stdlib.cpp`;
- **the port** (`khdays-host-imgui`, its settings menu): the same four core files and
  `backends/imgui_impl_sdlrenderer3.cpp` (17,414), built with `IMGUI_DISABLE_DEFAULT_FONT` (none
  of ImGui's embedded fonts is included).

License: **MIT**, copyright (c) 2014-2026 Omar Cornut.

## Roboto Medium (the settings menu's font)

Font: Roboto Medium, by Christian Robertson (Google), `misc/fonts/Roboto-Medium.ttf` (162,588 bytes)
of the same Dear ImGui archive as above.

Usage: embedded unmodified, as bytes generated at configure time (`port/CMakeLists.txt`), in
`khdays-native.exe`, for the port's settings menu.

License: **Apache License 2.0** (<https://www.apache.org/licenses/LICENSE-2.0>), as Dear ImGui's
`docs/FONTS.md` records it.

## Kenney's Input Prompts (the settings menu's gamepad icons and pictures)

Project: Input Prompts 1.5A, by Kenney (<https://kenney.nl/assets/input-prompts>)

Source archive: `kenney_input-prompts_1.5.zip` (5,070,075 bytes, SHA-256
`ac2fcf599080b0f3ba2d174c9474db6df1a0e96ff0662580e2da79a122ab78a1`), once at
`https://kenney.nl/media/pages/assets/input-prompts/8de120163f-1783763952/kenney_input-prompts_1.5.zip`; that
address no longer serves it, so the files used are kept in the repository, unmodified and at the archive's own
paths, with its `License.txt`: `port/resources/input_prompts/`.

Usage: 121 of its 128-pixel PNGs (the `Double` folders of `Xbox Series`, `PlayStation Series`,
`Nintendo Switch` and `Keyboard & Mouse`: the buttons, triggers, sticks, stick directions and D-pads the menu
and the HD fonts' text name, the mouse's inputs, and five gamepads' pictures: Xbox Series, PlayStation 5, 4
and 3, Switch Pro), embedded in `khdays-native.exe` as bytes generated at configure time, drawn in the settings
menu's Controls section and in the HD fonts' text (`port/ui/button_icons.c`).

License: **Creative Commons Zero (CC0 1.0)**, public domain (<http://creativecommons.org/publicdomain/zero/1.0/>),
as the archive's `License.txt` states; crediting Kenney is not required, and is done here.

## Lua

Project: Lua, the programming language, by R. Ierusalimschy, L. H. de Figueiredo and W. Celes
(<https://www.lua.org>)

Pinned release: **5.4.9** (2026-08-10), fetched by CMake as `https://www.lua.org/ftp/lua-5.4.9.tar.gz`,
373,429 bytes, checked by its SHA-256 `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`
(`port/CMakeLists.txt`).

Usage: compiled unmodified as a static library (`khdays-lua`) into `khdays-native.exe`, to run the
scripts of gameplay mods (`port/runtime/scripts/`); its stand-alone interpreter and compiler
(`lua.c`, `luac.c`) and the libraries the mods are not given (`liolib.c`, `ldblib.c`, `loadlib.c`,
`linit.c`) are left out.

License: **MIT**, copyright (C) 1994-2026 Lua.org, PUC-Rio.

## Windows Media Foundation (system library)

Component: Microsoft Media Foundation, part of Windows (`mfplat.dll`, `mfreadwrite.dll`), with
its import libraries from the Windows SDK (`mfplat.lib`, `mfreadwrite.lib`, `mfuuid.lib`).

Usage: linked by the native runtime and loaded from the system on first use
(`port/host/movies/movie_hd_win.c`) to decode the player's own HD videos (H.264 and AAC).
Nothing of it is distributed with this repository; it is the operating system's.

License: part of Windows, under the operating system's license.

## The installer's parts

The Windows installer (`installer/`, built by `installer/build_installer.py`) carries, besides the
port, the components below, each unmodified. Their own license files travel with them in the install
(`setup/python/`, and each wheel's `.dist-info` folder).

- **CPython 3.13.16**, the Windows embeddable package (<https://www.python.org>), in
  `setup/python/`: it runs the installer's helper and the tools it uses. License: the Python Software
  Foundation License Agreement.
- **numpy 2.2.2** (<https://numpy.org>), installed into `setup/python/` for the HD tools. License:
  BSD-3-Clause, with the licenses of the libraries it bundles in its `.dist-info`.
- **Pillow 11.2.1** (<https://python-pillow.org>), installed likewise. License: MIT-CMU (HPND), with
  its bundled libraries' licenses in its `.dist-info`.
- **The Microsoft Visual C++ runtime** (`vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`,
  `msvcp140_2.dll`), copied beside the executables from Visual Studio's redistributable folder, as
  Microsoft's Visual Studio license allows for its redistributable code.
- **Inno Setup 6.7.3** (<https://jrsoftware.org/isinfo.php>, Jordan Russell and Martijn Laan): the
  setup program is built with it and carries its runtime. License: the Inno Setup License.

`installer/pinned.json` pins each downloaded file by its SHA-256.

## KH Melon Mix (acknowledgement)

KH Melon Mix (by vitor251093, justedni, Kite2810 and contributors,
<https://github.com/vitor251093/KHMelonMix>) showed that the Kingdom Hearts DS games can be played
on one screen. Nothing of it is in this repository: the port's one-screen mode -- how it tells the
game's screens apart, its layouts, its effects and its tests -- was made by the port from the
decompilation, the DS's documented behaviour and measurements of the running game.

## Original game

No license in this repository applies to the original game or to copyrighted material extracted from it.

This repository must not distribute:

- Nintendo DS ROM images;
- extracted graphics, models, animations, maps, scripts, text, audio, or video;
- original game binaries or assembly dumps;
- proprietary Nintendo SDK files;
- proprietary Metrowerks/CodeWarrior files.

All original game content, trademarks, names, and characters remain the property of their respective owners.

## Future dependencies

Every added dependency must be recorded here with:

- project name;
- upstream repository or website;
- exact version or commit;
- license;
- whether it is linked, embedded, modified, or used only as a build tool.

Do not copy code from a repository merely because it is publicly visible. Confirm license compatibility before importing or adapting it.
