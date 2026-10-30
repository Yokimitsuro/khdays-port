# The KH Mod SDK: the headers a mod's native plugin is built against (include/kh/) and its examples. Built alone:
#   cmake -S sdk -B build/sdk && cmake --build build/sdk --config Release
# For Linux from another system: cmake/toolchain-linux-x64-clang.cmake.
# docs/README.md says how to make and install a plugin.
cmake_minimum_required(VERSION 3.20)
project(KhModSdk LANGUAGES C CXX)

include(cmake/KhModSdk.cmake)

# the boundary's own check: a plugin with no C library (tests/load_plugin.py loads it)
kh_add_plugin(abi_check tests/abi_check.c)

if(NOT KH_SDK_FREESTANDING)
    add_subdirectory(examples/HelloWorld)
    add_subdirectory(examples/AdvancedNativeMod)
    add_subdirectory(examples/CustomCharacter)
    add_subdirectory(examples/CustomLimit)
    add_subdirectory(examples/CustomEnemy)
    add_subdirectory(examples/CharacterForms)
endif()
