# Android A0 probe (docs/design/android-port.md): libSDL3.so, libSDL2.so (sdl2-compat)
# and libmain.so (Plume clear probe), for tools/release/android/build_probe.py.
cmake_minimum_required(VERSION 3.20)
project(srw64_android_probe C CXX)
set(CMAKE_CXX_STANDARD 20)

set(SRW64_SDL3_SOURCE "" CACHE PATH "Pinned SDL3 source (config/recomp/macos-dependencies.json)")
set(SRW64_SDL2_COMPAT_SOURCE "" CACHE PATH "Pinned sdl2-compat source")
set(SRW64_PLUME_SOURCE "" CACHE PATH "Plume from the pinned RT64")
get_filename_component(SRW64_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SRW64_SDL3_SOURCE}" sdl3)

set(SDL2COMPAT_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2COMPAT_STATIC OFF CACHE BOOL "" FORCE)
set(SDL2COMPAT_X11 OFF CACHE BOOL "" FORCE)
add_subdirectory("${SRW64_SDL2_COMPAT_SOURCE}" sdl2-compat)

add_subdirectory("${SRW64_PLUME_SOURCE}" plume)

add_library(main SHARED "${SRW64_ROOT}/src/android/clear_probe.cpp")
target_link_libraries(main PRIVATE SDL2::SDL2 plume android log)
