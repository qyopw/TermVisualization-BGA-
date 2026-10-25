cmake_minimum_required(VERSION 3.16)

project(TermVisualizationBGA VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Embed the project data so the resulting executable can be copied on its own.
file(READ "${PROJECT_SOURCE_DIR}/data/model.stl" MODEL_STL_HEX HEX)
file(READ "${PROJECT_SOURCE_DIR}/data/sensors.csv" SENSOR_CSV_HEX HEX)
string(LENGTH "${MODEL_STL_HEX}" MODEL_STL_HEX_SIZE)
string(LENGTH "${SENSOR_CSV_HEX}" SENSOR_CSV_HEX_SIZE)
math(EXPR MODEL_STL_SIZE "${MODEL_STL_HEX_SIZE} / 2")
math(EXPR SENSOR_CSV_SIZE "${SENSOR_CSV_HEX_SIZE} / 2")
set(EMBEDDED_ASSETS "#pragma once\n#include <array>\nnamespace embedded_assets {\n")
string(APPEND EMBEDDED_ASSETS "inline constexpr std::array<unsigned char, ${MODEL_STL_SIZE}> model = {")
string(REGEX REPLACE "(..)" "0x\\1," MODEL_STL_INITIALIZER "${MODEL_STL_HEX}")
string(APPEND EMBEDDED_ASSETS "${MODEL_STL_INITIALIZER}")
string(APPEND EMBEDDED_ASSETS "};\ninline constexpr std::array<char, ${SENSOR_CSV_SIZE}> sensors = {")
string(REGEX REPLACE "(..)" "static_cast<char>(0x\\1)," SENSOR_CSV_INITIALIZER "${SENSOR_CSV_HEX}")
string(APPEND EMBEDDED_ASSETS "${SENSOR_CSV_INITIALIZER}")
string(APPEND EMBEDDED_ASSETS "};\n}\n")
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
file(WRITE "${PROJECT_BINARY_DIR}/generated/embedded_assets.h" "${EMBEDDED_ASSETS}")

find_package(SDL2 QUIET CONFIG)

if(TARGET SDL2::SDL2)
    set(BGA_SDL_TARGET SDL2::SDL2)
elseif(TARGET SDL2::SDL2-shared)
    set(BGA_SDL_TARGET SDL2::SDL2-shared)
elseif(TARGET SDL2::SDL2-static)
    set(BGA_SDL_TARGET SDL2::SDL2-static)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()

    if(TARGET PkgConfig::SDL2)
        set(BGA_SDL_TARGET PkgConfig::SDL2)
    else()
        message(FATAL_ERROR
            "SDL2 development files were not found. Install SDL2 and rerun CMake.\n"
            "macOS (Homebrew): brew install sdl2\n"
            "Ubuntu/Debian: sudo apt install libsdl2-dev pkg-config\n"
            "Windows: install SDL2 development files and set CMAKE_PREFIX_PATH."
        )
    endif()
endif()

add_executable(bga_visualizer
    src/main.cpp
    src/sensor_state.cpp
    src/can_protocol.cpp
    src/can_receiver.cpp
)
target_include_directories(bga_visualizer PRIVATE "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(bga_visualizer PRIVATE ${BGA_SDL_TARGET} Threads::Threads)

# SocketCAN is a Linux kernel API. Keep it out of non-Linux builds so the
# visualizer remains usable there in --no-can mode.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(bga_visualizer PRIVATE BGA_HAS_SOCKETCAN=1)
else()
    target_compile_definitions(bga_visualizer PRIVATE BGA_HAS_SOCKETCAN=0)
endif()

if(MSVC)
    target_compile_options(bga_visualizer PRIVATE /W4 /permissive-)
else()
    target_compile_options(bga_visualizer PRIVATE -Wall -Wextra -Wpedantic)
endif()

