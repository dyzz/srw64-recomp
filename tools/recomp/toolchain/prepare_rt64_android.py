#!/usr/bin/env python3
"""Android adaptations for a copy of the prepared RT64 (docs/design/android-port.md).

prepare_rt64.py prepares build/recomp/upstream/RT64 for the desktop hosts. The Android
build works on its own copy of that tree (other sessions build from the shared one),
and this script applies the Android changes there: each is an exact, unique text
replacement, and every one is guarded by ANDROID or __ANDROID__, so the same patches
leave the desktop builds unchanged when they move into prepare_rt64.py.

  tools/recomp/toolchain/prepare_rt64_android.py build/android/upstream/RT64
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

PATCHES: list[tuple[str, str, str]] = [
    # Shader tools run on the build machine: DXC for the host, file_to_c built for the
    # host and imported (RT64_FILE_TO_C), as Goemon64Recomp-Android does.
    ('CMakeLists.txt', '''if (WIN32)
    set (DXC "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/x64/dxc.exe")''', '''if (ANDROID)
    if (CMAKE_HOST_APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        set (DXC "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/arm64/dxc-macos")
    elseif (CMAKE_HOST_APPLE)
        set (DXC "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/src/contrib/dxc/lib/x64" "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/x64/dxc-macos")
    elseif (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set (DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/src/contrib/dxc/lib/x64" "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/x64/dxc-linux")
    else()
        set (DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/arm64/dxc-linux")
    endif()
elseif (WIN32)
    set (DXC "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/x64/dxc.exe")'''),
    ('CMakeLists.txt', '''add_subdirectory(src/tools/file_to_c)
add_subdirectory(src/contrib/re-spirv)
add_subdirectory(src/contrib/nativefiledialog-extended)''', '''if (ANDROID)
    # Built for the build machine; there is no file dialog on Android.
    if (NOT RT64_FILE_TO_C)
        message(FATAL_ERROR "Android builds need RT64_FILE_TO_C, a file_to_c built for the build machine")
    endif()
    add_executable(file_to_c IMPORTED)
    set_target_properties(file_to_c PROPERTIES IMPORTED_LOCATION "${RT64_FILE_TO_C}")
else()
    add_subdirectory(src/tools/file_to_c)
endif()
add_subdirectory(src/contrib/re-spirv)
if (NOT ANDROID)
    add_subdirectory(src/contrib/nativefiledialog-extended)
endif()'''),
    ('CMakeLists.txt', '''target_link_libraries(rt64 nfd)''', '''if (NOT ANDROID)
    target_link_libraries(rt64 nfd)
endif()'''),
    # zstd's dictionary builder needs qsort_r, which bionic lacks before API 36; RT64
    # only compresses and decompresses.
    ('CMakeLists.txt', '''set(ZSTD_BUILD_SHARED OFF)''', '''set(ZSTD_BUILD_SHARED OFF)
if (ANDROID)
    set(ZSTD_BUILD_DICTBUILDER OFF)
endif()'''),
    # SDL2 comes from the build's own sdl2-compat target on Android.
    ('CMakeLists.txt', '''else()
    find_package(SDL2 REQUIRED)
endif()

message(STATUS "${SDL2_INCLUDE_DIRS} ${SDL2_LIBRARIES}")''', '''elseif (ANDROID AND TARGET SDL2::SDL2)
    set(SDL2_INCLUDE_DIRS "$<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>")
    set(SDL2_LIBRARIES SDL2::SDL2)
else()
    find_package(SDL2 REQUIRED)
endif()

message(STATUS "${SDL2_INCLUDE_DIRS} ${SDL2_LIBRARIES}")'''),
    ('src/gui/rt64_file_dialog.cpp', '''#include <nfd.h>
''', '''#ifdef __ANDROID__
// Android has no native file dialog; RT64's developer menus get empty paths.
namespace RT64 {
    std::atomic<bool> FileDialog::isOpen = false;
    void FileDialog::initialize() {}
    void FileDialog::finish() {}
    std::filesystem::path FileDialog::getDirectoryPath() { return {}; }
    std::filesystem::path FileDialog::getOpenFilename(const std::vector<FileFilter> &) { return {}; }
    std::filesystem::path FileDialog::getSaveFilename(const std::vector<FileFilter> &) { return {}; }
};
#else
#include <nfd.h>
'''),
    ('src/gui/rt64_file_dialog.cpp', '''        isOpen = false;
        return path;
    }
};''', '''        isOpen = false;
        return path;
    }
};
#endif'''),
    # The host hands RT64 its window (an ANativeWindow on Android); the title-based
    # setup that creates a window stays unimplemented, but fails at run time.
    ('src/hle/rt64_application_window.cpp', '''#   if !defined(RT64_SDL_WINDOW_VULKAN)
#      include <X11/extensions/Xrandr.h>''', '''#   if !defined(RT64_SDL_WINDOW_VULKAN) && !defined(__ANDROID__)
#      include <X11/extensions/Xrandr.h>'''),
    ('src/hle/rt64_application_window.cpp', '''#   elif defined(__ANDROID__)
        static_assert(false && "Android unimplemented");
#   elif defined(__linux__) || defined(__APPLE__)''', '''#   elif defined(__ANDROID__)
        fprintf(stderr, "RT64 cannot create its own window on Android.\\n");
        return;
#   elif defined(__linux__) || defined(__APPLE__)'''),
    ('src/hle/rt64_application_window.cpp', '''#   elif defined(__ANDROID__)
        static_assert(false && "Android unimplemented");
#   elif defined(__linux__)''', '''#   elif defined(__ANDROID__)
        return;
#   elif defined(__linux__)'''),
    ('src/hle/rt64_application_window.cpp', '''#   elif defined(RT64_SDL_WINDOW_VULKAN)
        int displayIndex = SDL_GetWindowDisplayIndex(windowHandle);''', '''#   elif defined(__ANDROID__)
        SDL_DisplayMode displayMode = {};
        if (SDL_GetCurrentDisplayMode(0, &displayMode) != 0) {
            fprintf(stderr, "SDL_GetCurrentDisplayMode failed. Error: %s.\\n", SDL_GetError());
            return;
        }

        refreshRate = displayMode.refresh_rate;
#   elif defined(RT64_SDL_WINDOW_VULKAN)
        int displayIndex = SDL_GetWindowDisplayIndex(windowHandle);'''),
    ('src/hle/rt64_application_window.cpp', '''#   elif defined(RT64_SDL_WINDOW_VULKAN)
        SDL_GetWindowPosition(windowHandle, &newWindowLeft, &newWindowTop);''', '''#   elif defined(__ANDROID__)
        newWindowLeft = 0;
        newWindowTop = 0;
#   elif defined(RT64_SDL_WINDOW_VULKAN)
        SDL_GetWindowPosition(windowHandle, &newWindowLeft, &newWindowTop);'''),
]


def apply(checkout: Path) -> list[str]:
    applied = []
    texts: dict[str, str] = {}
    for relative, old, new in PATCHES:
        text = texts.setdefault(relative, (checkout / relative).read_text())
        if text.count(new) == 1 and text.count(old) <= (1 if old in new else 0):
            continue  # already applied
        if text.count(old) != 1:
            raise RuntimeError(f'Android patch context differs in {relative}: {old.splitlines()[0]!r}')
        texts[relative] = text.replace(old, new)
        applied.append(relative)
    for relative, text in texts.items():
        if (checkout / relative).read_text() != text:
            (checkout / relative).write_text(text)
    return applied


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('checkout', type=Path)
    args = parser.parse_args()
    applied = apply(args.checkout)
    print(f'SRW64_RT64_ANDROID applied={len(applied)} patches={len(PATCHES)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
