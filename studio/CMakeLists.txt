# khdays-studio: a mod authoring tool beside the port, built on its own (studio/build.py). It compiles
# some of the port's portable files as they are, by path, so it decides as the port does, and edits
# none of them (docs/STUDIO.md).
cmake_minimum_required(VERSION 3.25)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
project(khdays_studio C CXX)
if(WIN32)
    enable_language(RC)  # the executable icon (resources/studio.rc)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "khdays-studio is built for x64: use studio/build.py")
endif()

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
include(FetchContent)

# SDL3: the release the port pins (port/CMakeLists.txt), static.
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

# Dear ImGui, MIT, at the release THIRD_PARTY_NOTICES.md
# records. Only its sources are wanted.
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(imgui)

# What the reused port files need, at the commits the port pins
# (port/CMakeLists.txt; THIRD_PARTY_NOTICES.md).
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(stb)
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG dfe8377631000664666519fdb83da193fd8037f4
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(dr_libs)
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG 85cd62382dfea638278962690cf515023f33ed00
    SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(cgltf)
include("${ROOT}/assets/fetch_ufbx.cmake")  # sets ufbx_SOURCE_DIR (the port's pin and hashes)

# --- Dear ImGui, as it is -------------------------------------------------------------
add_library(khdays-studio-imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp")
target_include_directories(khdays-studio-imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends"
                                                      "${imgui_SOURCE_DIR}/misc/cpp")
target_link_libraries(khdays-studio-imgui PUBLIC SDL3::SDL3)
target_compile_options(khdays-studio-imgui PRIVATE /W0 /std:c++20)

# --- The port's files, compiled as they are -------------------------------------------
add_library(khdays-studio-port STATIC
    "${ROOT}/assets/src/filesystem.cpp"            # the game's files, and which mod wins
    "${ROOT}/assets/src/image.cpp"              # images in PNG, JPEG, TGA, BMP (its own stb_image)
    "${ROOT}/assets/src/gltf.cpp"               # a mod's models, as the port imports them
    "${ROOT}/assets/src/gltf_export.cpp"        # a room written to build on
    "${ROOT}/assets/src/fbx.cpp"
    "${ROOT}/assets/src/mesh.cpp"               # the game's models
    "${ROOT}/assets/src/animation.cpp"          # and their animations
    "${ROOT}/assets/src/message.cpp"            # lz_decompress
    "${ROOT}/assets/src/font.cpp"               # the game's font: which characters it draws
    "${ROOT}/assets/src/graphics2d.cpp"         # 2D: palettes, tile sheets, screens
    "${ROOT}/assets/src/cell.cpp"               # and sprite cells
    "${ROOT}/assets/src/kh2_bar.cpp"            # KH2's BAR archives, which its maps are read from
    "${ROOT}/assets/src/kh2_map.cpp"            # KH2's maps, from the player's 1.5+2.5 copy
    "${ROOT}/assets/src/kh2_room.cpp"           # made into a scene for glTF
    "${ROOT}/assets/src/dds.cpp"                # and their HD pictures
    "${ROOT}/assets/src/kh2_sequence.cpp"       # KH2's sequences' quads (the prizes' effects)
    "${ROOT}/port/gfx/kh2_sequence.cpp"         # ... drawn by the port's reference renderer
    "${ROOT}/port/ui/prize_effects_model.cpp"   # a prize's effect, as the port reads it
    "${ROOT}/port/sound/replace.c"            # a mod's sound read as the port reads it
    "${ROOT}/port/sound/third_party_decoders.c"
    "${ROOT}/port/host/render/png_io.c"              # PNG in and out, indexed included
    "${ROOT}/port/host/movies/movie_hd_win.c"        # videos played as the port plays them (Media Foundation)
    "${ROOT}/port/host/movies/theater_music.c"       # ... with the theater's music
    "${ROOT}/port/host/movies/music_schedule.c"
    "${ROOT}/port/runtime/mods/room_animation.cpp"   # a room that moves by itself, as the port moves it
    "${ROOT}/port/runtime/missions/registry.c"       # a mod's new missions, as the port reads them
    "${ROOT}/port/runtime/missions/field_store.c"
    "${ROOT}/port/runtime/missions/tables.c"         # ... and the rows it adds for them
    "${ROOT}/port/runtime/missions/worlds.c"
    "${ROOT}/port/runtime/plugins/parameters.c"      # a mod's native plugin's parameters, as the port reads them
    "${ROOT}/port/runtime/characters/registry.c"     # a mod's characters, as the port reads them
    "${ROOT}/port/runtime/characters/rules.c"        # ... their forms' rules
    "${ROOT}/port/runtime/characters/stats.c")       # ... their level tables
target_include_directories(khdays-studio-port PUBLIC "${ROOT}/assets/include")
target_include_directories(khdays-studio-port PUBLIC "${cgltf_SOURCE_DIR}" "${ufbx_SOURCE_DIR}")
target_include_directories(khdays-studio-port PRIVATE "${stb_SOURCE_DIR}" "${dr_libs_SOURCE_DIR}")
# ufbx as it is: third-party, not held to these warnings (as port/CMakeLists.txt)
add_library(khdays-studio-ufbx STATIC "${ufbx_SOURCE_DIR}/ufbx.c")
target_compile_options(khdays-studio-ufbx PRIVATE /W0)
target_link_libraries(khdays-studio-port PUBLIC SDL3::SDL3 khdays-studio-ufbx mfplat mfreadwrite mfuuid ole32)
target_compile_options(khdays-studio-port PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/W4 /std:c17>
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /std:c++20 /EHsc>)
set_source_files_properties("${ROOT}/port/sound/third_party_decoders.c" PROPERTIES COMPILE_OPTIONS "/W0")
# The port's files as they are: their C library and C++20 deprecation notes
# are the port's to decide on, not this build's.
target_compile_definitions(khdays-studio-port PRIVATE _CRT_SECURE_NO_WARNINGS)
# The tool's own code reaches these by their directories.
set(KHDAYS_STUDIO_PORT_HEADERS "${ROOT}/port/sound" "${ROOT}/port/host" "${ROOT}/port/runtime" "${ROOT}/port")

# --- The tool -------------------------------------------------------------------------
set(KHDAYS_STUDIO_FLAGS /W4 /std:c++20 /EHsc /utf-8 /permissive-)

add_library(khdays-studio-core STATIC
    core/models/anim_replace.cpp
    core/models/bca0.cpp
    core/characters/character_files.cpp
    core/characters/character_forms.cpp
    core/characters/character_pose.cpp
    core/characters/form_rules.cpp
    core/characters/game_items.cpp
    core/models/bones.cpp
    core/files/catalog.cpp
    core/files/catalog_tree.cpp
    core/mods/check.cpp
    core/mods/plugin_parameters.cpp
    core/models/clip_import.cpp
    core/files/containers.cpp
    core/files/file_write.cpp
    core/models/game_model.cpp
    core/missions/command_meanings.cpp
    core/missions/command_names.cpp
    core/files/lz.cpp
    core/missions/mission_db.cpp
    core/missions/missions.cpp
    core/missions/mission_edit.cpp
    core/missions/mission_names.cpp
    core/missions/mission_build.cpp
    core/missions/mod_missions.cpp
    core/missions/enemy_names.cpp
    core/texts/texts.cpp
    core/models/texture_export.cpp
    core/models/model_export.cpp
    core/models/model_files.cpp
    core/mods/mods.cpp
    core/mods/mods_log.cpp
    core/files/paths.cpp
    core/mods/process.cpp
    core/mods/prizes.cpp
    core/rooms/kh2_source.cpp
    core/rooms/room_change.cpp
    core/rooms/room_collision.cpp
    core/rooms/room_export.cpp
    core/mods/settings.cpp
    core/sheets/sheets.cpp
    core/rooms/worlds.cpp)
target_include_directories(khdays-studio-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" ${KHDAYS_STUDIO_PORT_HEADERS})
# The KH2 rooms' pairs (data/kh2_rooms.txt, names only) built into the studio.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/kh2_rooms.txt")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/kh2_rooms.txt" KH2_ROOMS_TEXT)
configure_file(core/rooms/kh2_rooms_data.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/kh2_rooms_data.h" @ONLY)
target_include_directories(khdays-studio-core PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(khdays-studio-core PUBLIC khdays-studio-port SDL3::SDL3)
target_compile_options(khdays-studio-core PRIVATE ${KHDAYS_STUDIO_FLAGS})

add_executable(khdays-studio
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/studio.rc   # the executable's icon (studio.ico)
    app/app.cpp
    app/views/browser.cpp
    app/views/character_view.cpp
    app/cli.cpp
    app/views/console.cpp
    app/gpu_images.cpp
    app/views/image_view.cpp
    app/main.cpp
    app/views/map_view.cpp
    app/views/mission_view.cpp
    app/views/mission_map.cpp
    app/views/movie_view.cpp
    app/views/text_view.cpp
    app/views/mod_panel.cpp
    app/views/mods_console.cpp
    app/views/plugin_parameters_view.cpp
    app/views/prizes_view.cpp
    app/views/model_view.cpp
    app/preview3d.cpp
    app/save_dialog.cpp
    app/views/sheet_view.cpp
    app/views/sound_view.cpp
    app/strings.cpp
    app/theme.cpp)
target_link_libraries(khdays-studio PRIVATE khdays-studio-core khdays-studio-imgui)
target_compile_options(khdays-studio PRIVATE ${KHDAYS_STUDIO_FLAGS})
if(WIN32)
    # a windowed program (no console window when opened from the desktop or a shortcut); main()
    # (SDL's, from SDL_main.h) stays the entry point, and it attaches to a terminal's console when
    # started from one
    target_link_options(khdays-studio PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

enable_testing()
add_executable(khdays-studio-tests
    tests/test_main.cpp
    tests/test_check.cpp
    tests/test_foundation.cpp
    tests/test_bones.cpp
    tests/test_anim.cpp
    tests/test_sheets.cpp
    tests/test_missions.cpp
    tests/test_worlds.cpp
    tests/test_kh2.cpp
    tests/test_mods_log.cpp
    tests/test_plugin_parameters.cpp
    tests/test_characters.cpp
    tests/test_character_forms.cpp
    tests/test_texts.cpp
    tests/test_prizes.cpp
    app/strings.cpp
    tests/test_strings.cpp)
target_include_directories(khdays-studio-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app")
target_link_libraries(khdays-studio-tests PRIVATE khdays-studio-core)
target_compile_options(khdays-studio-tests PRIVATE ${KHDAYS_STUDIO_FLAGS})
add_test(NAME khdays-studio-tests COMMAND khdays-studio-tests)
