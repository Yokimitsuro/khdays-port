cmake_minimum_required(VERSION 3.24)

project(
    khdays_port
    VERSION 0.0.1
    DESCRIPTION "Experimental native PC runtime for Kingdom Hearts 358/2 Days"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(khdays-port
    src/main.cpp
)

target_include_directories(khdays-port
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(khdays-port
    PRIVATE
        KHDAYS_PORT_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(khdays-port PRIVATE /W4 /permissive-)
else()
    target_compile_options(khdays-port PRIVATE -Wall -Wextra -Wpedantic)
endif()
