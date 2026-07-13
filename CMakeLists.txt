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

add_library(
    khdays-assets
    STATIC
        src/assets/mdl0.cpp
        src/assets/mesh.cpp
        src/assets/tex0.cpp
)

target_include_directories(
    khdays-assets
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(MSVC)
    target_compile_options(khdays-assets PRIVATE /W4 /permissive-)
else()
    target_compile_options(khdays-assets PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
        khdays-assets
        SDL3::SDL3
)

if(MSVC)
    target_compile_options(khdays-port PRIVATE /W4 /permissive-)
else()
    target_compile_options(khdays-port PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
