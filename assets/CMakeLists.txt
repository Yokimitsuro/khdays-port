cmake_minimum_required(VERSION 3.24)

# The library the port (port/) and the studio (studio/) share, portable C++
# for every platform: the game's data formats, the mod layer that resolves
# user overrides before the originals, the game filesystem, and their tests.
project(
    khdays_port
    VERSION 0.1.0
    DESCRIPTION "Native PC port of Kingdom Hearts 358/2 Days: data formats, mods and filesystem"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KHDAYS_ENABLE_SANITIZERS
    "Build khdays targets with AddressSanitizer + UndefinedBehaviorSanitizer (Clang/GCC only)"
    OFF)

if(KHDAYS_ENABLE_SANITIZERS AND MSVC)
    message(WARNING
        "KHDAYS_ENABLE_SANITIZERS is only wired for Clang/GCC; it will be ignored under MSVC.")
endif()

# ASan+UBSan on one of the project's own targets; fetched third-party code is
# left uninstrumented.
function(khdays_enable_sanitizers target)
    if(NOT KHDAYS_ENABLE_SANITIZERS OR MSVC)
        return()
    endif()

    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
    target_link_options(${target} PRIVATE
        -fsanitize=address,undefined)
endfunction()

# The project's warning level on one of its own targets.
function(khdays_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

include(FetchContent)

# stb_image (PNG, JPEG, TGA, BMP), cgltf and ufbx (mods' models), pinned in
# THIRD_PARTY_NOTICES.md.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    # stb has no release tags: a pinned commit (a shallow clone cannot fetch one).
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG 85cd62382dfea638278962690cf515023f33ed00
)
FetchContent_MakeAvailable(stb cgltf)
include("${CMAKE_CURRENT_SOURCE_DIR}/fetch_ufbx.cmake")
add_library(khdays-ufbx STATIC "${ufbx_SOURCE_DIR}/ufbx.c")

# Our own targets are UTF-8, source and execution: without this MSVC reads
# sources in the host's codepage and mangles non-ASCII literals. Set after
# FetchContent so third-party targets keep their own flags.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# The game's data formats decoded to neutral forms, the open formats mods bring,
# and the game filesystem (NitroFS paths across mods, decompressed and raw data).
add_library(
    khdays-assets
    STATIC
        src/animation.cpp
        src/audio.cpp
        src/cell.cpp
        src/dds.cpp
        src/fbx.cpp
        src/filesystem.cpp
        src/font.cpp
        src/gltf.cpp
        src/gltf_export.cpp
        src/graphics2d.cpp
        src/image.cpp
        src/kh2_bar.cpp
        src/kh2_map.cpp
        src/kh2_sequence.cpp
        src/kh2_room.cpp
        src/mesh.cpp
        src/message.cpp
)
target_include_directories(khdays-assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(khdays-assets PRIVATE "${stb_SOURCE_DIR}" "${cgltf_SOURCE_DIR}" "${ufbx_SOURCE_DIR}")
target_link_libraries(khdays-assets PRIVATE khdays-ufbx)
khdays_warnings(khdays-assets)
khdays_enable_sanitizers(khdays-assets)

include(CTest)

if(BUILD_TESTING)
    # One executable per tests/test_<name>.cpp, run by ctest as
    # khdays-<name>. khdays-asset-tests builds them all: CI builds that target
    # instead of naming each test, so adding a test here is the only place it
    # has to be added.
    set(KHDAYS_TESTS audio cell font graphics2d kh2_map mesh message model_import vfs)
    set(KHDAYS_TEST_TARGETS)
    foreach(name IN LISTS KHDAYS_TESTS)
        string(REPLACE "_" "-" dashed "${name}")
        add_executable(khdays-${dashed}-test tests/test_${name}.cpp)
        target_link_libraries(khdays-${dashed}-test PRIVATE khdays-assets)
        khdays_warnings(khdays-${dashed}-test)
        khdays_enable_sanitizers(khdays-${dashed}-test)
        add_test(NAME khdays-${dashed} COMMAND khdays-${dashed}-test)
        list(APPEND KHDAYS_TEST_TARGETS khdays-${dashed}-test)
    endforeach()
    add_custom_target(khdays-asset-tests DEPENDS ${KHDAYS_TEST_TARGETS})
endif()
