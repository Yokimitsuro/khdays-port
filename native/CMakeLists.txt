# The native build: the decompilation's C compiled for x86 and linked with the
# HAL and runtime into one Windows executable. Driven by native/build.py, which
# first runs native/tools/prepare.py to generate KHDAYS_GEN.
cmake_minimum_required(VERSION 3.25)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)  # /Z7: no shared PDB to contend on
project(khdays_native C CXX ASM_MASM)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The native build is 32-bit only: the decompilation keeps "
                        "pointers in int and the DS address space is mapped 1:1. "
                        "Use native/build.py.")
endif()

set(KHDAYS_GEN "" CACHE PATH "Output directory of native/tools/prepare.py")
if(NOT EXISTS "${KHDAYS_GEN}/sources.cmake")
    message(FATAL_ERROR "KHDAYS_GEN does not hold prepare.py's output: ${KHDAYS_GEN}")
endif()
include("${KHDAYS_GEN}/sources.cmake")

set(DECOMP "${CMAKE_CURRENT_SOURCE_DIR}/../external/khdays-decomp")

# The window: SDL3, static, the same release the port uses (../CMakeLists.txt).
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

# The 2D display engines, pure (they read the state they are given), shared
# by the game and the savestate test.
add_library(khdays-gpu STATIC gpu/gpu2d.c gpu/gpu3d.c gpu/vram_map.c)
target_compile_options(khdays-gpu PRIVATE /W4 /std:c17)

# The game: every source the DS build links, compiled as the decomp wrote it.
# mwcc ran with -lang c99 -char signed -enum int; MSVC's char is signed and its
# enums are int already.
add_library(khdays-decomp OBJECT ${DECOMP_SOURCES})
# DS Protect's sources include their own headers (ov028's dsprot directory,
# wherever the decomp's layout puts it).
file(GLOB_RECURSE DSPROT_HEADER "${DECOMP}/src/*/dsprot_types.h")
list(GET DSPROT_HEADER 0 DSPROT_HEADER)
get_filename_component(DSPROT_DIR "${DSPROT_HEADER}" DIRECTORY)
target_include_directories(khdays-decomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/include"
    "${DSPROT_DIR}")
target_compile_definitions(khdays-decomp PRIVATE
    main=khdays_NitroMain       # the game's main; the process has its own
    # The game's C library (libs/msl) defines these C runtime names; renamed,
    # the game keeps its own and the rest of the process (SDL) the CRT's.
    strlen=khdays_msl_strlen
    strncmp=khdays_msl_strncmp
    strncpy=khdays_msl_strncpy
    strtol=khdays_msl_strtol
    abs=khdays_msl_abs
    _CRT_SECURE_NO_WARNINGS)
target_compile_options(khdays-decomp PRIVATE
    /W0
    # Struct members align to at most 4 bytes, as mwcc lays them out for the
    # ARM9: a u64 inside a struct sits at a 4-byte offset (OSContext's divider
    # context puts the thread state at +0x64, not +0x68).
    /Zp4
    "/FI${CMAKE_CURRENT_SOURCE_DIR}/compat/decomp_compat.h"
    $<$<COMPILE_LANGUAGE:C>:/std:c17>
    $<$<COMPILE_LANGUAGE:CXX>:/std:c++17>)

add_executable(khdays-native
    $<TARGET_OBJECTS:khdays-decomp>
    hal/sdk_bios.c
    hal/sdk_gx.c
    hal/sdk_irq.c
    hal/sdk_mi.c
    hal/sdk_misc.c
    hal/sdk_mtx.c
    hal/sdk_os.c
    hal/sdk_runtime.c
    hal/sdk_thread.c
    runtime/arm7.c
    runtime/boot.c
    runtime/card.c
    runtime/data_init.c
    runtime/clock.c
    runtime/diag.c
    runtime/display.c
    runtime/events.c
    runtime/input.c
    runtime/io_regs.c
    runtime/io_trap.c
    runtime/memory.c
    runtime/overlays.c
    runtime/rom.cpp
    runtime/runtime.c
    runtime/start.c
    runtime/vram.c
    host/window.c
    "${KHDAYS_GEN}/absolute.asm"
    ../src/vfs/filesystem.cpp)  # the port's VFS: files and mods
target_include_directories(khdays-native PRIVATE ../include)
target_compile_options(khdays-native PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/W4 /std:c17>
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /std:c++20 /EHsc>)

target_link_libraries(khdays-native PRIVATE khdays-gpu SDL3::SDL3 dbghelp winmm delayimp)
# The DS's address ranges are claimed first thing in main (memory.c). DLLs
# loaded with the process could take them first: user32 maps the 20 MB
# desktop heap at the lowest free address, right over main RAM. So SDL's
# system DLLs (and everything that pulls in user32) load on first use.
foreach(dll user32 gdi32 imm32 ole32 oleaut32 version setupapi shell32 winmm dbghelp)
    target_link_options(khdays-native PRIVATE /DELAYLOAD:${dll}.dll)
endforeach()

# Aliases for symbols the DS build gave two names (prepare.py).
file(STRINGS "${KHDAYS_GEN}/alternatenames.txt" KHDAYS_ALTERNATENAMES)
target_link_options(khdays-native PRIVATE
    ${KHDAYS_ALTERNATENAMES}
    /DEBUG
    /INCREMENTAL:NO             # no padding between .khdi entries (data_init.c walks them)
    /SAFESEH:NO                 # absolute.asm carries no exception handlers
    /LARGEADDRESSAWARE
    /DYNAMICBASE:NO)            # a fixed image, clear of the DS address range

# Renders a DeSmuME savestate's display state and compares it with what
# DeSmuME showed (native/tools/savestate_gpu.py extracts the input).
add_executable(khdays-gpu-test gpu/savestate_test.c)
target_link_libraries(khdays-gpu-test PRIVATE khdays-gpu)
target_compile_options(khdays-gpu-test PRIVATE /W4 /std:c17)
