# Building Linux plugins (.so) on another system with clang and lld:
#   cmake -S sdk -B build/sdk-linux -G Ninja -DCMAKE_TOOLCHAIN_FILE=sdk/cmake/toolchain-linux-x64-clang.cmake
#         [-DKH_LINUX_SYSROOT=<a Linux system's files: usr/include, usr/lib>]
# With a sysroot every example builds as on Linux itself. Without one only plugins that use no C library build (the
# SDK's tests/abi_check.c): the examples are left out. On Linux itself no toolchain file is needed (gcc or clang).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_LINKER_TYPE LLD)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(KH_LINUX_SYSROOT "" CACHE PATH "a Linux system's files to build against (empty: plugins without a C library)")
if(KH_LINUX_SYSROOT)
    set(CMAKE_SYSROOT "${KH_LINUX_SYSROOT}")
else()
    # nothing of a Linux system to link against: the compiler's own checks only compile
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_C_FLAGS_INIT "-ffreestanding")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld -nostdlib")
    set(KH_SDK_FREESTANDING ON)
endif()
