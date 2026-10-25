# The native build: the decompilation's C, rewritten with every pointer a
# reference, compiled for Windows x64 and linked with the HAL and
# runtime into one executable. Driven by port/build.py, which first runs
# port/tools/prepare.py to generate KHDAYS_GEN.
cmake_minimum_required(VERSION 3.25)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)  # /Z7: no shared PDB to contend on
project(khdays_native C CXX ASM_MASM)
if(WIN32)
    enable_language(RC)  # the executable icon (resources/game.rc)
endif()

# The release's version (the repository's VERSION file; its tag is v<version>): the update check
# compares it with the latest release's (host/app/updates.c).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" KHDAYS_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")
# only the files that tell it (the update check, the settings menu's corner): a new version rebuilds them alone
set_source_files_properties(host/app/updates.c host/settings/settings_menu.cpp
                            PROPERTIES COMPILE_DEFINITIONS "KHDAYS_VERSION=\"${KHDAYS_VERSION}\"")

set(KHDAYS_GEN "" CACHE PATH "Output directory of port/tools/prepare.py")
if(NOT EXISTS "${KHDAYS_GEN}/sources.cmake")
    message(FATAL_ERROR "KHDAYS_GEN does not hold prepare.py's output: ${KHDAYS_GEN}")
endif()
include("${KHDAYS_GEN}/sources.cmake")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native build is for x64 Windows: use port/build.py")
endif()

set(DECOMP "${CMAKE_CURRENT_SOURCE_DIR}/../external/khdays-decomp")

# Optimized code keeps its frame pointers: the crash and stall reports walk
# the stack by them (diag.c).
add_compile_options($<$<AND:$<CONFIG:Release>,$<NOT:$<COMPILE_LANGUAGE:ASM_MASM>>>:/Oy->)
# The C library's portable functions (getenv, fopen, sscanf...), not MSVC's _s versions of them.
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

# The window: SDL3, static.
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    URL
        https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz
    URL_HASH
        SHA256=f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL3)

# PNG in and out (the HD replacements and the graphics dump): stb, at the
# commit THIRD_PARTY_NOTICES.md records (stb has no release tags; a shallow
# clone cannot fetch a bare SHA).
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)
FetchContent_MakeAvailable(stb)

# glTF in (a mod's models, runtime/mods/models.cpp): cgltf, at the commit
# THIRD_PARTY_NOTICES.md records, read through assets/src/gltf.cpp. Only its
# source is wanted.
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG 85cd62382dfea638278962690cf515023f33ed00
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(cgltf)

# FBX in (a mod's models): ufbx's three files, read through
# assets/src/fbx.cpp.
include("${CMAKE_CURRENT_SOURCE_DIR}/../assets/fetch_ufbx.cmake")

# FLAC for the replaced sounds (mods/<Mod>/sounds): dr_flac, one header, at
# the commit THIRD_PARTY_NOTICES.md records. Only its source is wanted
# (SOURCE_SUBDIR names no directory, so nothing of it is built).
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG dfe8377631000664666519fdb83da193fd8037f4
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(dr_libs)

# MPEG-1 video and MPEG-1 Layer II sound for the movies a mod puts in the
# game's place (mods/<Mod>/movies, runtime/movies/video_mod.c): pl_mpeg, one header, at the commit THIRD_PARTY_NOTICES.md records.
# Only its source is wanted.
FetchContent_Declare(
    pl_mpeg
    GIT_REPOSITORY https://github.com/phoboslab/pl_mpeg.git
    GIT_TAG c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(pl_mpeg)

# The settings menu's drawing: Dear ImGui (MIT, the release the studio pins) with its SDL_Renderer
# backend; the menu takes its input itself and has its own font, so no platform backend and none of
# ImGui's fonts. Only its sources are wanted.
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(imgui)
add_library(khdays-host-imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(khdays-host-imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(khdays-host-imgui PUBLIC IMGUI_DISABLE_DEFAULT_FONT IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(khdays-host-imgui PUBLIC SDL3::SDL3)
target_compile_options(khdays-host-imgui PRIVATE /std:c++20 /EHsc)

# Gameplay mods' scripts (runtime/scripts): Lua 5.4 (MIT), its core and the libraries the mods are
# given; not its interpreter, compiler, files, debug or modules.
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
    URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(lua)
file(GLOB khdays_lua_sources "${lua_SOURCE_DIR}/src/*.c")
list(FILTER khdays_lua_sources EXCLUDE REGEX "/(lua|luac|liolib|ldblib|loadlib|linit)\\.c$")
add_library(khdays-lua STATIC ${khdays_lua_sources})
target_include_directories(khdays-lua PUBLIC "${lua_SOURCE_DIR}/src")
target_compile_options(khdays-lua PRIVATE /W3 /wd4244 /wd4324)

# The menu's font, in the executable so that it alone still updates a
# release: Roboto Medium (Apache License 2.0), from ImGui's own archive.
set(KHDAYS_MENU_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/menu_font")
file(READ "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf" menu_font_hex HEX)
string(LENGTH "${menu_font_hex}" menu_font_digits)
math(EXPR menu_font_size "${menu_font_digits} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," menu_font_bytes "${menu_font_hex}")
string(REPEAT "0x[0-9a-f][0-9a-f]," 24 menu_font_line) # CMake's expressions have no {n}
string(REGEX REPLACE "(${menu_font_line})" "\\1\n" menu_font_bytes "${menu_font_bytes}")
string(CONCAT menu_font_text "/* Generated by port/CMakeLists.txt from ImGui's misc/fonts/Roboto-Medium.ttf\n"
              " * (Apache License 2.0, THIRD_PARTY_NOTICES.md). */\n"
              "static const unsigned char khdays_menu_font[${menu_font_size}] = {\n${menu_font_bytes}\n};\n")
if(EXISTS "${KHDAYS_MENU_FONT_DIR}/menu_font.h")
    file(READ "${KHDAYS_MENU_FONT_DIR}/menu_font.h" menu_font_old)
else()
    set(menu_font_old "")
endif()
if(NOT menu_font_old STREQUAL menu_font_text)
    file(WRITE "${KHDAYS_MENU_FONT_DIR}/menu_font.h" "${menu_font_text}")
endif()

# The gamepads' button icons and pictures in the settings menu: Kenney's Input Prompts 1.5, CC0 (THIRD_PARTY_NOTICES.md), the
# 128-pixel PNGs of the buttons the menu names and of the five gamepads, in
# the executable as the font is.
FetchContent_Declare(
    kenney_input_prompts
    URL https://kenney.nl/media/pages/assets/input-prompts/8de120163f-1783763952/kenney_input-prompts_1.5.zip
    URL_HASH SHA256=ac2fcf599080b0f3ba2d174c9474db6df1a0e96ff0662580e2da79a122ab78a1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(kenney_input_prompts)
set(KHDAYS_PAD_ICONS
    "Xbox Series:xbox_button_color_a" "Xbox Series:xbox_button_color_b" "Xbox Series:xbox_button_color_x"
    "Xbox Series:xbox_button_color_y" "Xbox Series:xbox_lb" "Xbox Series:xbox_rb" "Xbox Series:xbox_lt"
    "Xbox Series:xbox_rt" "Xbox Series:xbox_stick_l" "Xbox Series:xbox_stick_r" "Xbox Series:xbox_stick_l_press"
    "Xbox Series:xbox_stick_r_press" "Xbox Series:xbox_dpad_none" "Xbox Series:xbox_dpad_up"
    "Xbox Series:xbox_dpad_down" "Xbox Series:xbox_dpad_left" "Xbox Series:xbox_dpad_right"
    "Xbox Series:xbox_dpad_horizontal" "Xbox Series:xbox_dpad_vertical"
    "Xbox Series:xbox_button_view" "Xbox Series:xbox_button_menu" "Xbox Series:xbox_guide"
    "Xbox Series:xbox_button_share"
    "PlayStation Series:playstation_button_color_cross" "PlayStation Series:playstation_button_color_circle"
    "PlayStation Series:playstation_button_color_square" "PlayStation Series:playstation_button_color_triangle"
    "PlayStation Series:playstation_trigger_l1" "PlayStation Series:playstation_trigger_r1"
    "PlayStation Series:playstation_trigger_l2" "PlayStation Series:playstation_trigger_r2"
    "PlayStation Series:playstation_stick_l" "PlayStation Series:playstation_stick_r"
    "PlayStation Series:playstation_button_l3" "PlayStation Series:playstation_button_r3"
    "PlayStation Series:playstation_dpad_none" "PlayStation Series:playstation_dpad_up"
    "PlayStation Series:playstation_dpad_down" "PlayStation Series:playstation_dpad_left"
    "PlayStation Series:playstation_dpad_right" "PlayStation Series:playstation_dpad_horizontal"
    "PlayStation Series:playstation_dpad_vertical" "PlayStation Series:playstation5_button_options"
    "PlayStation Series:playstation5_button_create" "PlayStation Series:playstation5_button_mute"
    "PlayStation Series:playstation5_touchpad" "PlayStation Series:playstation4_button_options"
    "PlayStation Series:playstation4_button_share" "PlayStation Series:playstation4_touchpad"
    "PlayStation Series:playstation3_button_start" "PlayStation Series:playstation3_button_select"
    "Nintendo Switch:switch_button_a" "Nintendo Switch:switch_button_b" "Nintendo Switch:switch_button_x"
    "Nintendo Switch:switch_button_y" "Nintendo Switch:switch_button_l" "Nintendo Switch:switch_button_r"
    "Nintendo Switch:switch_button_zl" "Nintendo Switch:switch_button_zr" "Nintendo Switch:switch_stick_l"
    "Nintendo Switch:switch_stick_r" "Nintendo Switch:switch_stick_l_press" "Nintendo Switch:switch_stick_r_press"
    "Nintendo Switch:switch_dpad_none" "Nintendo Switch:switch_dpad_up" "Nintendo Switch:switch_dpad_down"
    "Nintendo Switch:switch_dpad_left" "Nintendo Switch:switch_dpad_right" "Nintendo Switch:switch_dpad_horizontal"
    "Nintendo Switch:switch_dpad_vertical" "Nintendo Switch:switch_button_plus"
    "Nintendo Switch:switch_button_minus" "Nintendo Switch:switch_button_home"
    "Xbox Series:controller_xboxseries" "PlayStation Series:controller_playstation5"
    "PlayStation Series:controller_playstation4" "PlayStation Series:controller_playstation3"
    "Nintendo Switch:controller_switch_pro")
# the sticks with a way marked and the mouse's inputs, for the buttons in the HD fonts' text
# (ui/button_icons.c)
foreach(pad "Xbox Series:xbox" "PlayStation Series:playstation" "Nintendo Switch:switch")
    string(REPLACE ":" ";" pad_parts "${pad}")
    list(GET pad_parts 0 pad_folder)
    list(GET pad_parts 1 pad_prefix)
    foreach(stick l r)
        foreach(way up down left right horizontal vertical)
            list(APPEND KHDAYS_PAD_ICONS "${pad_folder}:${pad_prefix}_stick_${stick}_${way}")
        endforeach()
    endforeach()
endforeach()
foreach(mouse left right scroll scroll_up scroll_down side_back side_forward)
    list(APPEND KHDAYS_PAD_ICONS "Keyboard & Mouse:mouse_${mouse}")
endforeach()
string(CONCAT pad_icons_text "/* Generated by port/CMakeLists.txt from Kenney's Input Prompts 1.5 (CC0,\n"
              " * THIRD_PARTY_NOTICES.md): the 128-pixel PNGs of the gamepads and their buttons. */\n")
set(pad_icons_table "")
set(pad_icon_n 0)
foreach(icon IN LISTS KHDAYS_PAD_ICONS)
    string(FIND "${icon}" ":" colon)
    string(SUBSTRING "${icon}" 0 ${colon} icon_folder)
    math(EXPR colon "${colon} + 1")
    string(SUBSTRING "${icon}" ${colon} -1 icon_name)
    file(READ "${kenney_input_prompts_SOURCE_DIR}/${icon_folder}/Double/${icon_name}.png" icon_hex HEX)
    string(LENGTH "${icon_hex}" icon_digits)
    math(EXPR icon_size "${icon_digits} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," icon_bytes "${icon_hex}")
    string(REGEX REPLACE "(${menu_font_line})" "\\1\n" icon_bytes "${icon_bytes}")
    string(APPEND pad_icons_text "static const unsigned char pad_icon_${pad_icon_n}[${icon_size}] = {\n${icon_bytes}\n};\n")
    string(APPEND pad_icons_table "    {\"${icon_name}\", pad_icon_${pad_icon_n}, ${icon_size}},\n")
    math(EXPR pad_icon_n "${pad_icon_n} + 1")
endforeach()
string(APPEND pad_icons_text "static const struct { const char *name; const unsigned char *png; unsigned size; } "
              "khdays_pad_icons[${pad_icon_n}] = {\n${pad_icons_table}};\n")
if(EXISTS "${KHDAYS_MENU_FONT_DIR}/pad_icons.h")
    file(READ "${KHDAYS_MENU_FONT_DIR}/pad_icons.h" pad_icons_old)
else()
    set(pad_icons_old "")
endif()
if(NOT pad_icons_old STREQUAL pad_icons_text)
    file(WRITE "${KHDAYS_MENU_FONT_DIR}/pad_icons.h" "${pad_icons_text}")
endif()

# The DS's 2D display engines and its 3D rendering engine in software, pure
# (they read the state they are given): the references the port's own
# graphics are checked against, shared by the game and the tests.
add_library(khdays-gpu STATIC gpu/gpu2d.c gpu/gpu3d.c gpu/texdecode.c gpu/vram_map.c)
target_compile_options(khdays-gpu PRIVATE /W4 /std:c17)

# The sound: the DS's channels and mixer, the port's own sound driver and
# mixer, the music streams' reader, the replacements and the offline render.
# Plain C with no SDL and no operating system calls, shared by the game and
# khdays-sound-test.
add_library(khdays-sound STATIC
    sound/archive.c
    sound/bus.c
    sound/categories.c
    sound/channels.c
    sound/mix_ds.c
    sound/mix_pc.c
    sound/render.c
    sound/replace.c
    sound/snd7_alarm.c
    sound/snd7_bank.c
    sound/snd7_channel.c
    sound/snd7_command.c
    sound/snd7_driver.c
    sound/snd7_hw.c
    sound/snd7_os.c
    sound/snd7_seq.c
    sound/snd7_tables.c
    sound/stream_reader.c
    sound/third_party_decoders.c)
target_compile_options(khdays-sound PRIVATE /W4 /std:c17)
# Ogg Vorbis (stb_vorbis.c) and FLAC (dr_flac.h), compiled in with the rest:
# no library loads with the process. Their own code keeps its warnings.
target_include_directories(khdays-sound PRIVATE "${stb_SOURCE_DIR}" "${dr_libs_SOURCE_DIR}")
set_source_files_properties(sound/third_party_decoders.c PROPERTIES COMPILE_OPTIONS "/W0")

# The port's own graphics: the frame description made from what
# the game asks for, and its composition -- shared with the GPU's shaders --
# and the 3D geometry. Plain C with no SDL and no operating system calls.
add_library(khdays-gfx STATIC
    gfx/compose_cpu.c
    gfx/describe.c
    gfx/g3d_geometry.c
    gfx/g3d_interp.c)
target_compile_options(khdays-gfx PRIVATE /W4 /std:c17)

# The input layer -- the control profile and its settings file, the
# bindings, the devices, the settings menu (its FPS and ping displays in the
# game's own font), and the runtime's pure parts of the enhancements (analog movement's
# arithmetic, the command-menu sequencer) -- shared by the game and
# khdays-input-test.
add_library(khdays-host-input STATIC
    host/app/profile.c
    host/app/bindings.c
    host/app/devices.c
    host/settings/settings_model.c        # the settings menu: its content,
    host/settings/settings_menu.cpp       # its screen in Dear ImGui,
    host/settings/settings_skin.cpp       # in the HD menu's pieces where a mod has them,
    host/settings/settings_corner.c       # and what it leaves when closed
    host/screens/ui_font.cpp
    host/screens/ui_text.c
    runtime/scripts/queue.c          # the Mods page: what it asks of the mods,
    runtime/scripts/view.c           # and what it shows of them
    host/render/png_io.c
    host/app/instance_win.c          # the player's slot on the PC
    runtime/input/analog.c
    runtime/input/sequencers.c
    ../assets/src/font.cpp       # the port's NFTR decoder
    ../assets/src/message.cpp)   # its LZ decompression
target_include_directories(khdays-host-input PRIVATE ../assets/include "${stb_SOURCE_DIR}" "${KHDAYS_MENU_FONT_DIR}")
target_link_libraries(khdays-host-input PUBLIC SDL3::SDL3 khdays-host-imgui)
target_compile_options(khdays-host-input PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/W4 /std:c17>
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /std:c++20 /EHsc>)

# The game: every source the DS build links and the HAL, rewritten with every pointer a reference
# (prepare.py, port/tools/references), each including only compat/khdays_references.h and the build's
# numbers. Struct members align to at most 4 bytes as mwcc lays them out (/Zp4); mwcc's -char signed
# -enum int are MSVC's own (docs/BUILD.md, "Compiler settings worth knowing").
add_library(khdays-game OBJECT ${KHDAYS_REWRITTEN})
target_include_directories(khdays-game PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat" "${KHDAYS_GEN}")
target_compile_options(khdays-game PRIVATE /W0 /Zp4 /std:c17)

# The MobiClip frame decoder as the decomp reconstructed it in portable C++
# (libs/mobiclip/video/portable; not in the DS build, which links the ARM
# payload): what the port calls for a movie's frame instead of the payload
# copied to the ITCM.
set(MOBICLIP_PORTABLE "${DECOMP}/libs/mobiclip/video/portable")
add_library(khdays-mobiclip OBJECT
    "${MOBICLIP_PORTABLE}/mobiclip_frame_core.cpp"
    "${MOBICLIP_PORTABLE}/mobiclip_reference.cpp")
target_include_directories(khdays-mobiclip PRIVATE "${MOBICLIP_PORTABLE}")
target_compile_options(khdays-mobiclip PRIVATE /std:c++20 /EHsc)

# The one-screen picture's compositions (ui/compositions/port_days.txt),
# compiled in as bytes so the executable needs no file beside it; edited, they
# are read again.
function(khdays_embed_text name file)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    file(READ "${file}" hex HEX)
    string(LENGTH "${hex}" digits)
    math(EXPR size "${digits} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${name}.c" CONTENT
"/* Generated by port/CMakeLists.txt from ${file}. */
const unsigned char khdays_${name}[] = {${bytes} 0};
const unsigned int khdays_${name}_size = ${size}u;
")
endfunction()
set(KHDAYS_PORT_COMPOSITIONS "${CMAKE_CURRENT_SOURCE_DIR}/ui/compositions/port_days.txt")
khdays_embed_text(compositions_port "${KHDAYS_PORT_COMPOSITIONS}")
# The theater's HD scenes in place of the game's (movies/hd_scenes.txt), compiled in the same way.
khdays_embed_text(scenes_port "${CMAKE_CURRENT_SOURCE_DIR}/movies/hd_scenes.txt")
# The Stop magic's numbers and words the game never made (runtime/battle/stop_magic.txt), the same way.
khdays_embed_text(stop_magic_text "${CMAKE_CURRENT_SOURCE_DIR}/runtime/battle/stop_magic.txt")

add_executable(khdays-native
    resources/game.rc   # the executable's icon (port/resources/game.ico)
    $<TARGET_OBJECTS:khdays-game>
    $<TARGET_OBJECTS:khdays-mobiclip>
    runtime/core/arm7.c
    runtime/core/pxi.c
    runtime/core/boot.c
    runtime/core/card.c
    runtime/core/data_init.c
    runtime/core/clock.c
    runtime/input/controls_hooks.c
    runtime/core/diag.c
    runtime/display/display.c
    runtime/display/field_wipe.c      # the field's room change being drawn (the window covers widescreen beside it)
    runtime/core/events.c
    runtime/input/input.c
    runtime/core/language.c           # the game's language changed while it runs
    runtime/menus/newgame.c            # the new game's dialogs and the port's subtitles question
    runtime/menus/saveslots.c          # the save files beyond the DS's three
    runtime/core/sha1.c               # SHA-1: the saves' signatures, the rooms' passwords
    runtime/menus/autosave.c           # the autosave after a cleared mission
    runtime/menus/savelist.c           # the port's save list over the game's
    runtime/menus/saveworld.c          # the world of each save (the save page's pictures)
    runtime/menus/cinelist.c           # MODO CINE's list, read for the port to draw
    runtime/menus/daycard.c            # the day's calendar card, read for the port to draw
    runtime/net/roomlist.c           # the port's list of Mission Mode's rooms over the game's
    runtime/menus/charselect.c         # Mission Mode's character select, read and steered for the port
    runtime/menus/camp.c               # Mission Mode's camp menu, read for the port
    runtime/menus/shop.c               # the Moogle Shop, read for the port (its moogle turned to the menu)
    runtime/menus/enemy_viewer.c       # Enemy Profiles' model viewer: the game's own 3D of the enemy
    runtime/menus/retry.c              # Mission Mode's Retry in the pause
    runtime/menus/mission_start.c      # a mod's mission.start: the camp's own presses
    runtime/scripts/scripts.c          # gameplay mods' scripts: found, loaded, run at the frame point
    runtime/scripts/sandbox.c          # ... each in its own Lua state
    runtime/scripts/manifest.c
    runtime/scripts/order.c
    runtime/scripts/folders.cpp
    runtime/scripts/mods_log.c
    runtime/scripts/api_events.c
    runtime/scripts/api_mod.c
    runtime/scripts/api_state.c
    runtime/scripts/api_menus.c        # ... their options in the settings menu's Mods page
    runtime/scripts/api_actions.c      # ... the game's actions they ask for
    runtime/scripts/api_v2.c           # ... the interface's version 2: a game mode's parts
    runtime/scripts/api_enemies.c      # ... the field's enemies they make
    runtime/scripts/api_run.c          # ... the end of a run: the player's defeat
    runtime/scripts/api_screens.c      # ... their title entries and screens
    runtime/scripts/api_deck.c         # ... the deck they read and change
    runtime/scripts/api_time.c         # ... their timers, music, draws and shares
    runtime/scripts/api_memory.c       # ... the game's memory they read and write
    runtime/scripts/screens.c          # ... shared with the window that draws them
    runtime/scripts/game_link.c        # ... the game's side of their interface
    runtime/scripts/deck_link.c        # ... the deck, the game's side
    runtime/scripts/time_link.c        # ... music, the room's seed and the shares, the game's side
    runtime/scripts/enemies_link.c     # ... the field's enemies, the game's side
    runtime/scripts/enemy_names.c
    runtime/battle/stop_data.c         # the Stop magic: its data file
    runtime/battle/stop_hold.c         # ... the enemies it holds and the damage they keep
    runtime/battle/stop_magic.c        # ... and its records, words and hold in the game
    runtime/core/sha256.c             # SHA-256: the mods' fingerprints
    runtime/core/io_regs.c
    runtime/core/threads.c
    runtime/core/window.c
    runtime/core/io_guard.c
    runtime/core/memory.c
    runtime/core/game_heap.c          # the game's two heaps over the port's region
    runtime/core/overlays.c
    runtime/core/rom.cpp
    runtime/core/text_patches.cpp       # the game's DS wording in the port's
    runtime/core/runtime.c
    runtime/sound/sound.c
    runtime/sound/sound_bridge.c
    runtime/sound/sound_dump.c
    runtime/sound/sound_mods.c
    runtime/core/start.c
    runtime/core/vram.c
    host/app/window.c
    host/app/updates.c                 # a newer release, told of on the title,
    host/app/release_version.c
    host/app/update_install_win.c      # its installer run to update this copy
    host/net/https_win.c               # ... asked of GitHub through WinHTTP
    host/app/context_win.c
    host/app/context_x64.asm             # the contexts' switch (host/app/context.h)
    host/app/files_win.c
    host/app/sound_output.c
    host/settings/settings_sample.cpp       # the settings menu's volume samples
    host/render/gpu_present.c
    host/render/gpu_raster3d.c
    host/render/gfx_render.c
    host/render/onescreen.c               # the one picture's situation and composition
    host/screens/layout_screen.c           # the port's screens drawn from a mod's pieces
    host/screens/save_screen.c             # the port's save list on the screen
    host/screens/cine_screen.c             # MODO CINE's list as the HD theater's chapters
    host/screens/day_screen.c              # the day's calendar card as the HD theater's chapter days
    host/screens/room_screen.c             # the port's list of Mission Mode's rooms on the screen
    host/screens/mod_screen.c              # a mod's screens and the title's mods' prompt
    host/screens/select_screen.c           # Mission Mode's character select on one screen
    host/screens/camp_screen.c             # Mission Mode's camp menu on one screen
    host/screens/overhaul_camp_screen.c    # ... and the story's, with its pages, in the Menu Overhaul's look
    host/screens/movie_pause.cpp           # the DS's scene pause over an HD movie
    host/screens/shop_screen.c             # the Moogle Shop on one screen
    host/screens/modern_pause.c            # the Menu Overhaul's pause (KH2's look) over HD movies and monologues
    host/screens/kh2_seq.c                 # KH2's 2D sequences (the pause's, the story menu's buttons)
    runtime/display/frame_package.c
    runtime/display/gfx_check.c
    runtime/core/origin.c
    runtime/mods/names.cpp
    runtime/mods/fonts.cpp
    runtime/mods/font_raster.cpp
    runtime/mods/font_runtime.cpp
    runtime/mods/textures.c
    runtime/mods/installed.c
    runtime/mods/models.cpp
    runtime/mods/room_animation.cpp     # a replaced room's own animation (its glTF's)
    runtime/mods/tiles2d.c
    runtime/mods/sheet_colours.c        # the colours each 2D sheet is drawn with, for the studio
    runtime/core/files.c
    runtime/core/session_log.c
    runtime/movies/video.c
    runtime/movies/video_mod.c
    runtime/movies/movie_hd.c             # an HD video in a movie's place
    runtime/movies/scene_trace.c          # KHDAYS_TRACE_SCENES: the field's event scripts
    runtime/movies/scene_map.c            # the theater's HD scenes: the map
    runtime/movies/scenes.c               # its files, and the scene the event runs
    host/movies/movie_hd_win.c            # the HD video through Media Foundation
    host/movies/movie_subtitles.c         # the theater's subtitles over the HD video
    host/movies/theater_music.c           # ... and its music
    host/movies/music_schedule.c
    runtime/net/net_session.c          # the multiplayer's session
    runtime/net/mp_predict.c           # a guest's prediction of its own character
    host/net/net_win.c                 # ... on Winsock
    ui/elements.c                  # the port's own presentation
    ui/layout.c
    ui/parts.c
    ui/bg.c
    ui/hud.c
    ui/badges.c                    # the badges' icons on the mission panel
    ui/crowns.c                    # the crowns on the story's day list
    ui/mission_list.c              # the camp's mission list from the game's files
    ui/autosave_notice.cpp         # the notice of an autosave, in the game's font
    ui/save_numbers.cpp            # the file numbers on the game's own save lists in pages
    ui/rooms2d.c                   # the port's list of rooms on the game's own screens (prepare.py's hooks)
    ui/room_icons.cpp              # each room's signal and lock on the game's own list
    ui/player_names.cpp            # the players' names in multiplayer
    ui/pause.cpp                   # the pause's Retry line in Mission Mode
    ui/text.c
    ui/button_icons.c              # the player's buttons drawn in the HD fonts' text
    ui/situation.c                 # the one-screen picture
    ui/situation_take.c
    ui/situation_decide.c          # what the game shows, from what it runs
    ui/composition.c
    "${CMAKE_CURRENT_BINARY_DIR}/compositions_port.c"
    "${CMAKE_CURRENT_BINARY_DIR}/scenes_port.c"
    "${CMAKE_CURRENT_BINARY_DIR}/stop_magic_text.c"
    host/render/gpu_hd.c
    "${KHDAYS_GEN}/ds_functions.c"  # the native function at each DS address
    ../assets/src/mesh.cpp         # DS models decoded, for the graphics dump's glTF
    ../assets/src/audio.cpp        # the sound archive's streams and waves, for the menu's samples
    ../assets/src/gltf_export.cpp
    ../assets/src/gltf.cpp         # a mod's models (runtime/mods/models.cpp)
    ../assets/src/fbx.cpp          # ... as FBX
    ../assets/src/image.cpp        # their textures in PNG, JPEG, TGA, BMP (its own stb_image)
    ../assets/src/graphics2d.cpp   # the game's 2D sheets, for the rooms' signal
    ../assets/src/cell.cpp         # their cells
    ../assets/src/filesystem.cpp)  # the port's VFS: files and mods
target_include_directories(khdays-native PRIVATE ../assets/include)
# the frame decoder's state (runtime/movies/video.c)
set_source_files_properties(runtime/movies/video.c PROPERTIES INCLUDE_DIRECTORIES "${MOBICLIP_PORTABLE}")
# the port's wording holds the game's own texts in UTF-16 literals, written in UTF-8
set_source_files_properties(runtime/core/text_patches.cpp PROPERTIES COMPILE_OPTIONS "/utf-8")
# the window's areas (runtime/core/memory.c), the function references' table
# (runtime/core/window.c), the DS addresses the port reads (khdays_ds_symbols.h)
target_include_directories(khdays-native PRIVATE "${KHDAYS_GEN}")
target_include_directories(khdays-native PRIVATE "${stb_SOURCE_DIR}" "${cgltf_SOURCE_DIR}" "${pl_mpeg_SOURCE_DIR}"
                                                  "${ufbx_SOURCE_DIR}")
target_compile_options(khdays-native PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/W4 /std:c17>
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /std:c++20 /EHsc>)

# Media Foundation (host/movies/movie_hd_win.c): Windows' own libraries.
# ufbx, as it is: third-party, not held to the port's warnings.
add_library(khdays-ufbx STATIC "${ufbx_SOURCE_DIR}/ufbx.c")
target_link_libraries(khdays-native PRIVATE khdays-ufbx)
target_link_libraries(khdays-native PRIVATE khdays-lua)
target_link_libraries(khdays-native PRIVATE khdays-gfx khdays-gpu khdays-sound khdays-host-input SDL3::SDL3 dbghelp winmm delayimp
                      mfplat mfreadwrite mfuuid ole32 ws2_32 iphlpapi advapi32 winhttp)
# The DS's own addresses are held, inaccessible, first thing in main
# (memory.c), so that an access at one stops the game. DLLs loaded with the
# process could take them first: user32 maps the 20 MB desktop heap at the
# lowest free address, right over main RAM's. So SDL's system DLLs (and
# everything that pulls in user32) load on first use.
foreach(dll user32 gdi32 imm32 ole32 oleaut32 version setupapi shell32 winmm dbghelp mfplat mfreadwrite ws2_32
            iphlpapi winhttp)
    target_link_options(khdays-native PRIVATE /DELAYLOAD:${dll}.dll)
endforeach()

target_link_options(khdays-native PRIVATE
    /DEBUG
    /INCREMENTAL:NO            # no padding between .khdi entries (data_init.c walks them)
    # a windowed program (no console window when started from Explorer or Steam); main() stays the
    # entry point, and start.c attaches to a terminal's console when it was started from one
    /SUBSYSTEM:WINDOWS
    /ENTRY:mainCRTStartup)

# Renders a DeSmuME savestate's display state and compares it with what
# DeSmuME showed (port/tools/savestate_gpu.py extracts the input).
add_executable(khdays-gpu-test tests/savestate_test.c)
target_link_libraries(khdays-gpu-test PRIVATE khdays-gpu)
target_compile_options(khdays-gpu-test PRIVATE /W4 /std:c17)

# The input layer's tests (controls, bindings, devices through SDL's virtual
# gamepads): ctest --test-dir build/port/obj-x64
enable_testing()
add_executable(khdays-input-test tests/input_test.c ../assets/src/filesystem.cpp)
target_include_directories(khdays-input-test PRIVATE ../assets/include)
target_link_libraries(khdays-input-test PRIVATE khdays-host-input)
target_compile_options(khdays-input-test PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/W4 /std:c17>
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /std:c++20 /EHsc>)
add_test(NAME khdays-input COMMAND khdays-input-test)
# The course of an HD movie against a scripted game and video.
add_executable(khdays-movie-hd-test tests/movie_hd_test.c runtime/movies/movie_hd.c runtime/movies/scene_map.c
               runtime/core/clock.c)
target_link_libraries(khdays-movie-hd-test PRIVATE winmm)
target_compile_options(khdays-movie-hd-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-movie-hd COMMAND khdays-movie-hd-test)
add_executable(khdays-camp-memory-test tests/camp_memory_test.c runtime/menus/camp.c)
target_compile_options(khdays-camp-memory-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-camp-memory COMMAND khdays-camp-memory-test)
add_executable(khdays-scenes-test tests/scenes_test.c runtime/movies/scene_map.c)
target_compile_options(khdays-scenes-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-scenes COMMAND khdays-scenes-test)
# The Stop magic (runtime/battle/): its data file as compiled in, the records, the hold and the stored damage.
add_executable(khdays-stop-magic-test tests/stop_magic_test.c runtime/battle/stop_data.c runtime/battle/stop_hold.c
               "${CMAKE_CURRENT_BINARY_DIR}/stop_magic_text.c")
target_compile_options(khdays-stop-magic-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-stop-magic COMMAND khdays-stop-magic-test)
# A replaced room's own animation (runtime/mods/room_animation.cpp): keys, loops, scrolls, frames.
add_executable(khdays-room-animation-test tests/room_animation_test.cpp runtime/mods/room_animation.cpp)
target_include_directories(khdays-room-animation-test PRIVATE ../assets/include)
target_compile_options(khdays-room-animation-test PRIVATE /W4 /std:c++20 /EHsc)
add_test(NAME khdays-room-animation COMMAND khdays-room-animation-test)
# The theater's music schedules (host/movies/music_schedule.c): cues, ramps, loops.
add_executable(khdays-music-schedule-test tests/music_schedule_test.c host/movies/music_schedule.c)
target_compile_options(khdays-music-schedule-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-music-schedule COMMAND khdays-music-schedule-test)
# ... and its decoder on one of the player's files, when they are there.
add_executable(khdays-movie-hd-decode-test tests/movie_hd_decode_test.c host/movies/movie_hd_win.c
               host/movies/theater_music.c host/movies/music_schedule.c)
target_include_directories(khdays-movie-hd-decode-test PRIVATE "${stb_SOURCE_DIR}")
target_link_libraries(khdays-movie-hd-decode-test PRIVATE khdays-sound SDL3::SDL3 mfplat mfreadwrite mfuuid ole32)
target_compile_options(khdays-movie-hd-decode-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-movie-hd-decode
         COMMAND khdays-movie-hd-decode-test "${CMAKE_CURRENT_SOURCE_DIR}/../data/cutscenes/cinematics/hd808.mp4" 7.19)
# ... a part of it, as the scene map plays a movie's video
add_test(NAME khdays-movie-hd-part
         COMMAND khdays-movie-hd-decode-test "${CMAKE_CURRENT_SOURCE_DIR}/../data/cutscenes/cinematics/hd808.mp4" 7.19 2.0 5.0)
# The port's elements in the composition: fixed screens composed on the CPU.
# The multiplayer session's delivery: two processes on this
# machine, a room found and joined, records through 30% loss.
add_executable(khdays-net-test tests/net_test.c runtime/net/net_session.c runtime/core/sha1.c host/net/net_win.c runtime/core/session_log.c
                                host/app/instance_win.c)
target_link_libraries(khdays-net-test PRIVATE SDL3::SDL3 ws2_32 iphlpapi advapi32)
target_compile_options(khdays-net-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-net COMMAND khdays-net-test)

# The multiplayer's meeting point over the internet (# server/README.md): a program of its own, next to khdays-native.exe; the
# same sources build on Linux with host/net/net_posix.c. Its test starts it and
# talks to it as a host and a guest would.
add_executable(khdays-server server/server.c server/server_core.c server/portmap.c host/net/net_win.c)
# the commit it says it is built from (server/version.h), as of this configure
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE KHDAYS_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(KHDAYS_COMMIT)
    target_compile_definitions(khdays-server PRIVATE "KHDAYS_SERVER_COMMIT=\"${KHDAYS_COMMIT}\"")
endif()
target_link_libraries(khdays-server PRIVATE ws2_32 iphlpapi advapi32)
target_compile_options(khdays-server PRIVATE /W4 /std:c17)
add_executable(khdays-server-test tests/server_test.c host/net/net_win.c)
target_link_libraries(khdays-server-test PRIVATE ws2_32 iphlpapi advapi32)
target_compile_options(khdays-server-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-server COMMAND khdays-server-test $<TARGET_FILE:khdays-server>)

add_executable(khdays-elements-test tests/elements_test.c)
target_link_libraries(khdays-elements-test PRIVATE khdays-gfx)
target_compile_options(khdays-elements-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-elements COMMAND khdays-elements-test)
# A mod's polygons into a list: the near plane only, a sky behind everything else.
add_executable(khdays-mod-polygon-test tests/mod_polygon_test.c)
target_link_libraries(khdays-mod-polygon-test PRIVATE khdays-gfx)
target_compile_options(khdays-mod-polygon-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-mod-polygon COMMAND khdays-mod-polygon-test)
# The one-screen picture: the compositions' places and the port's own file, the pass's steps and the 2D
# apart from the 3D, on the CPU.
add_executable(khdays-onescreen-test tests/onescreen_test.c ui/composition.c ui/situation.c ui/situation_decide.c
                                     host/render/png_io.c)
target_include_directories(khdays-onescreen-test PRIVATE "${stb_SOURCE_DIR}")
target_link_libraries(khdays-onescreen-test PRIVATE khdays-gfx SDL3::SDL3)
target_compile_definitions(khdays-onescreen-test PRIVATE "KHDAYS_PORT_COMPOSITIONS_FILE=\"${KHDAYS_PORT_COMPOSITIONS}\""
                           "KHDAYS_TEST_DIR=\"${CMAKE_CURRENT_BINARY_DIR}\"")
target_compile_options(khdays-onescreen-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-onescreen COMMAND khdays-onescreen-test)
# The rewrite of the game's C: its unit tests, in Python.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME khdays-references
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/references_test.py")
include(tests/hd_fonts.cmake)
# The installer's tools (installer/): one test each, in Python, named after its file.
file(GLOB KHDAYS_INSTALLER_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../installer/tests/*_test.py")
foreach(installer_test ${KHDAYS_INSTALLER_TESTS})
    get_filename_component(installer_test_name "${installer_test}" NAME_WE)
    string(REGEX REPLACE "_test$" "" installer_test_name "${installer_test_name}")
    string(REPLACE "_" "-" installer_test_name "${installer_test_name}")
    add_test(NAME khdays-installer-${installer_test_name} COMMAND "${Python3_EXECUTABLE}" "${installer_test}")
endforeach()
# The HD tools' readers (tools/hd/): one test each, in Python, named after its file.
file(GLOB KHDAYS_HD_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/hd/tests/*_test.py")
foreach(hd_test ${KHDAYS_HD_TESTS})
    get_filename_component(hd_test_name "${hd_test}" NAME_WE)
    string(REGEX REPLACE "_test$" "" hd_test_name "${hd_test_name}")
    string(REPLACE "_" "-" hd_test_name "${hd_test_name}")
    add_test(NAME khdays-hd-${hd_test_name} COMMAND "${Python3_EXECUTABLE}" "${hd_test}")
endforeach()

# The execution contexts the game's threads run on (host/app/context.h): more of
# them than the old fixed table held, each resumed in turn.
add_executable(khdays-thread-test tests/thread_test.c host/app/context_win.c host/app/context_x64.asm)
target_compile_options(khdays-thread-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-thread COMMAND khdays-thread-test)

# The save written so that a crash cannot corrupt it (host/app/files.h): a writer
# stopped at random 100 times, the file whole after each.
add_executable(khdays-save-test tests/save_test.c host/app/files_win.c)
target_link_libraries(khdays-save-test PRIVATE SDL3::SDL3)
target_compile_options(khdays-save-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-save COMMAND khdays-save-test)

# The save files beyond the DS's three (runtime/menus/saveslots.h): what each file
# holds as the game's read tells it, the two views, a port's file written and
# read back.
# The update check's versions and GitHub's answer (host/app/release_version.h).
add_executable(khdays-updates-test tests/updates_test.c host/app/release_version.c)
target_compile_options(khdays-updates-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-updates COMMAND khdays-updates-test)

add_executable(khdays-saveslots-test tests/saveslots_test.c runtime/menus/saveslots.c runtime/core/sha1.c host/app/files_win.c)
target_link_libraries(khdays-saveslots-test PRIVATE SDL3::SDL3)
target_compile_options(khdays-saveslots-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-saveslots COMMAND khdays-saveslots-test)

# Gameplay mods' scripts (runtime/scripts): manifests, the load order, the sandbox, the events and the
# state names, against a game of the test's own.
add_executable(khdays-script-test tests/script_test.c runtime/scripts/scripts.c runtime/scripts/sandbox.c
               runtime/scripts/manifest.c runtime/scripts/order.c runtime/scripts/folders.cpp
               runtime/scripts/mods_log.c runtime/scripts/queue.c runtime/scripts/api_events.c
               runtime/scripts/api_mod.c runtime/scripts/api_state.c runtime/scripts/api_menus.c
               runtime/scripts/api_actions.c runtime/scripts/api_v2.c runtime/scripts/api_enemies.c
               runtime/scripts/api_run.c runtime/scripts/api_screens.c runtime/scripts/api_deck.c
               runtime/scripts/api_time.c runtime/scripts/api_memory.c runtime/scripts/screens.c
               runtime/scripts/view.c runtime/core/sha256.c)
target_link_libraries(khdays-script-test PRIVATE khdays-lua SDL3::SDL3)
target_compile_options(khdays-script-test PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/W4 /std:c17>
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /std:c++20 /EHsc>)
add_test(NAME khdays-script COMMAND khdays-script-test)

# The sound's tests: the channels and mixers against fixed programs, the
# bridge's queues, and (to come) the drivers, the stream reader and the
# replacements; no game data needed.
add_executable(khdays-sound-test tests/sound_test.c runtime/sound/sound_bridge.c runtime/sound/sound_mods.c
    runtime/core/session_log.c host/app/instance_win.c)
target_link_libraries(khdays-sound-test PRIVATE khdays-sound)
target_compile_options(khdays-sound-test PRIVATE /W4 /std:c17)
# The native driver's part reads its tables from the ROM's ARM7 binary
# (the player's, extracted into data/; skipped without it).
file(GLOB KHDAYS_ARM7_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/*/system/arm7.bin")
list(GET KHDAYS_ARM7_BIN 0 KHDAYS_ARM7_BIN_FIRST)
add_test(NAME khdays-sound COMMAND khdays-sound-test "${KHDAYS_ARM7_BIN_FIRST}")

# Recorded frame packages (KHDAYS_RECORD_FRAMES, in saves/frames: the
# player's game data, never in the repository) checked against the pictures
# the game showed; skipped without recordings.
add_executable(khdays-render-test tests/render_test.c host/render/gpu_raster3d.c host/render/gpu_hd.c runtime/display/frame_package.c)
target_link_libraries(khdays-render-test PRIVATE khdays-gpu khdays-gfx SDL3::SDL3)
target_compile_options(khdays-render-test PRIVATE /W4 /std:c17)
add_test(NAME khdays-render COMMAND khdays-render-test "${CMAKE_CURRENT_SOURCE_DIR}/../saves/frames")
set_tests_properties(khdays-render PROPERTIES SKIP_RETURN_CODE 77)

# ---- The quality tools -------------------------------------------

# The fuzzers: libFuzzer through MSVC, with AddressSanitizer, over a reader of
# what arrives from outside (the server's datagrams, a router's answers, the
# game's session, a mod's mod.ini and the load order); not in Debug (MSVC's AddressSanitizer does not combine with
# its run-time checks). Each replays its kept inputs (port/fuzz/<name>/)
# and fuzzes 15 s in the test suite; port/tools/fuzz.py runs them longer.
set(KHDAYS_FUZZ_SECONDS 15)
function(khdays_fuzz_target name)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        return()
    endif()
    add_executable(khdays-fuzz-${name} ${ARGN})
    # /DEBUG: a program database beside it, for the reports' lines
    target_compile_options(khdays-fuzz-${name} PRIVATE /W4 /std:c17 /fsanitize=address /fsanitize=fuzzer)
    target_link_options(khdays-fuzz-${name} PRIVATE /DEBUG /INCREMENTAL:NO)
    # libFuzzer writes what it finds to the first directory: the tree's, so
    # the kept inputs (the second) change only through port/tools/fuzz.py
    set(corpus "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/${name}")
    set(found "${CMAKE_BINARY_DIR}/fuzz-${name}-corpus")
    file(MAKE_DIRECTORY "${found}")
    add_test(NAME khdays-fuzz-${name}
             COMMAND khdays-fuzz-${name} -max_total_time=${KHDAYS_FUZZ_SECONDS} -timeout=10
                     "-artifact_prefix=${CMAKE_BINARY_DIR}/fuzz-${name}-" "${found}" "${corpus}")
endfunction()

# The memory-checking build (port/build.py --asan, build/port/obj-asan-x64):
# AddressSanitizer on the port's own code -- not on the decompilation's
# rewritten files, whose memory and threads' stacks are the port's mapped
# window, which it does not track.
option(KHDAYS_ASAN "AddressSanitizer on the port's own code" OFF)
set(KHDAYS_ASAN_DLL "")
get_filename_component(KHDAYS_CL_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
if(EXISTS "${KHDAYS_CL_DIR}/clang_rt.asan_dynamic-x86_64.dll")
    set(KHDAYS_ASAN_DLL "${KHDAYS_CL_DIR}/clang_rt.asan_dynamic-x86_64.dll")
endif()
if(KHDAYS_ASAN)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR "KHDAYS_ASAN builds on the Release flags (port/build.py --asan)")
    endif()
    set(KHDAYS_PORT_TARGETS khdays-native khdays-gpu khdays-sound khdays-gfx khdays-host-input khdays-mobiclip
        khdays-gpu-test khdays-input-test khdays-movie-hd-test khdays-movie-hd-decode-test khdays-net-test
        khdays-server khdays-server-test khdays-elements-test khdays-onescreen-test khdays-thread-test
        khdays-save-test khdays-sound-test khdays-render-test khdays-music-schedule-test)
    foreach(target IN LISTS KHDAYS_PORT_TARGETS)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE $<$<NOT:$<COMPILE_LANGUAGE:ASM_MASM>>:/fsanitize=address>)
            target_link_options(${target} PRIVATE /DEBUG /INCREMENTAL:NO)  # a report's lines
        endif()
    endforeach()
endif()
# The runtime beside what uses it (the fuzzers in every tree that has them,
# everything in the memory-checking one).
if(KHDAYS_ASAN_DLL AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    file(COPY "${KHDAYS_ASAN_DLL}" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

khdays_fuzz_target(server fuzz/fuzz_server.c server/server_core.c host/net/net_stub.c)
khdays_fuzz_target(portmap fuzz/fuzz_portmap.c server/portmap.c host/net/net_stub.c)
khdays_fuzz_target(session fuzz/fuzz_session.c host/net/net_stub.c runtime/core/session_log.c host/app/instance_win.c
                   runtime/core/sha1.c)
if(TARGET khdays-fuzz-session)
    target_link_libraries(khdays-fuzz-session PRIVATE SDL3::SDL3)
endif()
khdays_fuzz_target(mods fuzz/fuzz_mods.c runtime/scripts/manifest.c runtime/scripts/order.c runtime/core/sha256.c)

# The static analysis (port/tools/analyze.py, in the Release tree's tests): the port's own sources of
# each target compiled again with /analyze and their own flags, not linked, built only when asked;
# not the decompilation's rewritten files, not the third parties'.
set(KHDAYS_ANALYZED khdays-native khdays-gpu khdays-sound khdays-gfx khdays-host-input khdays-server)
set(KHDAYS_ANALYZE_TARGETS "")
foreach(target IN LISTS KHDAYS_ANALYZED)
    get_target_property(sources ${target} SOURCES)
    get_target_property(source_dir ${target} SOURCE_DIR)
    set(own "")
    foreach(source IN LISTS sources)
        get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${source_dir}")
        string(FIND "${path}" "${CMAKE_CURRENT_SOURCE_DIR}/" inside)
        if(inside EQUAL 0 AND path MATCHES "\\.(c|cpp)$" AND NOT path MATCHES "third_party_decoders")
            list(APPEND own "${path}")
        endif()
    endforeach()
    if(own)
        add_library(khdays-analyze-${target} OBJECT EXCLUDE_FROM_ALL ${own})
        target_include_directories(khdays-analyze-${target} PRIVATE $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>)
        target_compile_definitions(khdays-analyze-${target} PRIVATE $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>)
        target_compile_options(khdays-analyze-${target} PRIVATE $<TARGET_PROPERTY:${target},COMPILE_OPTIONS> /analyze)
        target_link_libraries(khdays-analyze-${target} PRIVATE $<TARGET_PROPERTY:${target},LINK_LIBRARIES>)
        list(APPEND KHDAYS_ANALYZE_TARGETS khdays-analyze-${target})
    endif()
endforeach()
# The HAL as it is compiled: its rewritten copies, whose #line directives
# name port/hal's own lines. As written its pointers are DS pointers, not
# the references the build compiles, so a finding there would be about code
# never built.
set(KHDAYS_HAL_REWRITTEN ${KHDAYS_REWRITTEN})
list(FILTER KHDAYS_HAL_REWRITTEN INCLUDE REGEX "/rewritten/native_hal/[^/]+\\.c$")
if(KHDAYS_HAL_REWRITTEN)
    add_library(khdays-analyze-hal OBJECT EXCLUDE_FROM_ALL ${KHDAYS_HAL_REWRITTEN})
    target_include_directories(khdays-analyze-hal PRIVATE $<TARGET_PROPERTY:khdays-game,INCLUDE_DIRECTORIES>)
    target_compile_options(khdays-analyze-hal PRIVATE $<TARGET_PROPERTY:khdays-game,COMPILE_OPTIONS> /analyze)
    list(APPEND KHDAYS_ANALYZE_TARGETS khdays-analyze-hal)
endif()
add_custom_target(khdays-analyze DEPENDS ${KHDAYS_ANALYZE_TARGETS})
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT KHDAYS_ASAN)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME khdays-analyze
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/analyze.py" --tree "${CMAKE_BINARY_DIR}")
endif()
