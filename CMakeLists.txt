cmake_minimum_required(VERSION 3.24)

project(
    khdays_port
    VERSION 0.0.4
    DESCRIPTION "Experimental native PC runtime for Kingdom Hearts 358/2 Days"
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

# Apply ASan+UBSan to a single target. Sanitizers are scoped to the project's
# own targets so the fetched SDL3 build is left uninstrumented, which keeps the
# build fast and avoids third-party diagnostics we cannot act on.
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

# Optional PNG texture overrides via stb_image. When off, the build needs no
# network for stb and PNG overrides are simply unavailable (BMP still works).
option(KHDAYS_ENABLE_PNG "Support PNG texture overrides (fetches stb)" ON)
if(KHDAYS_ENABLE_PNG)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        # Header-only; track master. Pin to a commit for fully reproducible builds.
        GIT_TAG master
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(stb)
endif()

add_library(
    khdays-assets
    STATIC
        src/assets/animation.cpp
        src/assets/bmp.cpp
        src/assets/mdl0.cpp
        src/assets/mesh.cpp
        src/assets/sdat.cpp
        src/assets/tex0.cpp
)

target_include_directories(
    khdays-assets
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(KHDAYS_ENABLE_PNG)
    target_sources(khdays-assets PRIVATE src/assets/png.cpp)
    target_include_directories(khdays-assets PRIVATE "${stb_SOURCE_DIR}")
    target_compile_definitions(khdays-assets PUBLIC KHDAYS_HAS_PNG)
endif()

if(MSVC)
    target_compile_options(khdays-assets PRIVATE /W4 /permissive-)
else()
    target_compile_options(khdays-assets PRIVATE -Wall -Wextra -Wpedantic)
endif()

khdays_enable_sanitizers(khdays-assets)

# The resource layer resolves neutral assets (from DS formats or user overrides)
# so the engine never depends on Nintendo DS formats directly.
add_library(
    khdays-resource
    STATIC
        src/resource/loader.cpp
)

target_link_libraries(khdays-resource PUBLIC khdays-assets)

if(MSVC)
    target_compile_options(khdays-resource PRIVATE /W4 /permissive-)
else()
    target_compile_options(khdays-resource PRIVATE -Wall -Wextra -Wpedantic)
endif()

khdays_enable_sanitizers(khdays-resource)

add_executable(
    khdays-port
    src/main.cpp
    platform/pc/sdl_runtime.cpp
    platform/pc/gpu_renderer.cpp
)

target_include_directories(
    khdays-port
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(
    khdays-port
    PRIVATE
        KHDAYS_PORT_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(
    khdays-port
    PRIVATE
        khdays-resource
        SDL3::SDL3
)

if(MSVC)
    target_compile_options(khdays-port PRIVATE /W4 /permissive-)
else()
    target_compile_options(khdays-port PRIVATE -Wall -Wextra -Wpedantic)
endif()

khdays_enable_sanitizers(khdays-port)

include(CTest)

if(BUILD_TESTING)
    add_executable(
        khdays-tex0-test
        tests/cpp/test_tex0.cpp
    )

    target_link_libraries(
        khdays-tex0-test
        PRIVATE
            khdays-assets
    )

    add_executable(
        khdays-mdl0-test
        tests/cpp/test_mdl0.cpp
    )

    target_link_libraries(
        khdays-mdl0-test
        PRIVATE
            khdays-assets
    )

    add_executable(
        khdays-mesh-test
        tests/cpp/test_mesh.cpp
    )

    target_link_libraries(
        khdays-mesh-test
        PRIVATE
            khdays-assets
    )

    if(MSVC)
        target_compile_options(khdays-tex0-test PRIVATE /W4 /permissive-)
        target_compile_options(khdays-mdl0-test PRIVATE /W4 /permissive-)
        target_compile_options(khdays-mesh-test PRIVATE /W4 /permissive-)
    else()
        target_compile_options(khdays-tex0-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(khdays-mdl0-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(khdays-mesh-test PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    khdays_enable_sanitizers(khdays-tex0-test)
    khdays_enable_sanitizers(khdays-mdl0-test)
    khdays_enable_sanitizers(khdays-mesh-test)

    add_test(
        NAME khdays-port-version
        COMMAND khdays-port --version
    )

    add_test(
        NAME khdays-tex0-parser
        COMMAND khdays-tex0-test
    )

    add_test(
        NAME khdays-mdl0-parser
        COMMAND khdays-mdl0-test
    )

    add_test(
        NAME khdays-mesh-decoder
        COMMAND khdays-mesh-test
    )
endif()
