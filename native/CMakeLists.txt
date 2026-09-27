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

# The game: every source the DS build links, compiled as the decomp wrote it.
# mwcc ran with -lang c99 -char signed -enum int; MSVC's char is signed and its
# enums are int already.
add_library(khdays-decomp OBJECT ${DECOMP_SOURCES})
target_include_directories(khdays-decomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compat/include"
    "${DECOMP}/src/overlays/ov028/dsprot")
target_compile_definitions(khdays-decomp PRIVATE
    main=khdays_NitroMain       # the game's main; the process has its own
    _CRT_SECURE_NO_WARNINGS)
target_compile_options(khdays-decomp PRIVATE
    /W0
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
    runtime/memory.c
    runtime/runtime.c
    runtime/start.c
    "${KHDAYS_GEN}/absolute.asm")
target_compile_options(khdays-native PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/W4 /std:c17>)

# Aliases for symbols the DS build gave two names (prepare.py).
file(STRINGS "${KHDAYS_GEN}/alternatenames.txt" KHDAYS_ALTERNATENAMES)
target_link_options(khdays-native PRIVATE
    ${KHDAYS_ALTERNATENAMES}
    /DEBUG
    /SAFESEH:NO                 # absolute.asm carries no exception handlers
    /LARGEADDRESSAWARE
    /DYNAMICBASE:NO)            # a fixed image, clear of the DS address range
