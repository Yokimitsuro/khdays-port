cmake_minimum_required(VERSION 3.24)

project(
    khdays_port
    VERSION 0.0.2
    DESCRIPTION "Experimental native PC runtime for Kingdom Hearts 358/2 Days"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Build a pinned, static SDL3 dependency so Windows does not need a separately
# copied SDL3.dll next to khdays-port.exe.
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

add_executable(
    khdays-port
    src/main.cpp
    platform/pc/sdl_runtime.cpp
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
        SDL3::SDL3
)

if(MSVC)
    target_compile_options(khdays-port PRIVATE /W4 /permissive-)
else()
    target_compile_options(khdays-port PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if(BUILD_TESTING)
    add_test(
        NAME khdays-port-version
        COMMAND khdays-port --version
    )
endif()
