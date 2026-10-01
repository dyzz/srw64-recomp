# The game for arm64 Android (docs/design/android-port.md): SDL3 and sdl2-compat from
# the pinned sources, then src/host with RT64 as libmain.so. The text libraries come
# from tools/release/android/build_dependencies.py (SRW64_ANDROID_PREFIX), file_to_c
# built for the build machine (RT64_FILE_TO_C). tools/release/android/build_game.py
# configures this and packages the APK.
cmake_minimum_required(VERSION 3.20)
project(srw64_android C CXX)
set(CMAKE_CXX_STANDARD 20)

set(SRW64_SDL3_SOURCE "" CACHE PATH "Pinned SDL3 source")
set(SRW64_SDL2_COMPAT_SOURCE "" CACHE PATH "Pinned sdl2-compat source")
set(SRW64_ANDROID_PREFIX "" CACHE PATH "Static FreeType, HarfBuzz and ICU for Android")
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

list(APPEND CMAKE_FIND_ROOT_PATH "${SRW64_ANDROID_PREFIX}")
list(APPEND CMAKE_PREFIX_PATH "${SRW64_ANDROID_PREFIX}")
set(ICU_ROOT "${SRW64_ANDROID_PREFIX}")
set(SRW64_ENABLE_RT64 ON CACHE BOOL "" FORCE)
add_subdirectory("${SRW64_ROOT}/src/host" host)
