"""RT64 changes for the Android build (docs/design/android-port.md).

prepare_rt64.py applies these after its own patches, as exact and unique text
replacements. Each one is guarded by ANDROID, __ANDROID__ or an option that is off
on the desktop (RT64_SINGLE_SOURCE_BLEND), so the desktop hosts build as before.
"""
from __future__ import annotations

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
    # Android surfaces offer R8G8B8A8 but not B8G8R8A8 (Solana Seeker, Mali-G615): with
    # B8G8R8A8 Plume finds no surface format and the Mali driver crashes in resize().
    ('src/hle/rt64_application.cpp', '''        swapChainDesc.format = RenderFormat::B8G8R8A8_UNORM;''', '''#   ifdef __ANDROID__
        swapChainDesc.format = RenderFormat::R8G8B8A8_UNORM;
#   else
        swapChainDesc.format = RenderFormat::B8G8R8A8_UNORM;
#   endif'''),
    # Dual-source blending (SRC1_ALPHA) is missing on Mali GPUs, which still accept the
    # pipeline and draw white. With RT64_SINGLE_SOURCE_BLEND (on for Android) the pixel
    # shader has one output: a blended draw writes its blend factor into alpha in place
    # of the coverage, and blends with SRC_ALPHA. Colours are the same; only the stored
    # coverage of blended pixels differs.
    ('CMakeLists.txt', '''set (DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_3")''', '''set (DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_3")
option(RT64_SINGLE_SOURCE_BLEND "Blend without dual-source blending (Android: most Mali GPUs lack it)" OFF)
if (ANDROID OR RT64_SINGLE_SOURCE_BLEND)
    list(APPEND DXC_PS_OPTS "-D" "SINGLE_SOURCE_BLEND")
    add_compile_definitions(RT64_SINGLE_SOURCE_BLEND)
endif()'''),
    ('src/shaders/RasterPS.hlsl', '''    // Add highlight color to the last step.''', '''#if defined(SINGLE_SOURCE_BLEND)
    // One output carries both: a blended draw stores its blend factor, not its coverage.
    if (alphaBlend) {
        resultColor.a = resultAlpha.a;
    }
#endif

    // Add highlight color to the last step.'''),
    ('src/shaders/RasterPS.hlsl', '''    , [[vk::location(0)]] [[vk::index(1)]] out float4 pixelAlpha : SV_TARGET1''', '''#if !defined(SINGLE_SOURCE_BLEND)
    , [[vk::location(0)]] [[vk::index(1)]] out float4 pixelAlpha : SV_TARGET1
#endif'''),
    ('src/shaders/RasterPS.hlsl', '''    pixelAlpha = resultAlpha;''', '''#if !defined(SINGLE_SOURCE_BLEND)
    pixelAlpha = resultAlpha;
#endif'''),
    ('src/render/rt64_raster_shader.cpp', '''            targetBlend.srcBlend = RenderBlend::SRC1_ALPHA;
            targetBlend.dstBlend = RenderBlend::INV_SRC1_ALPHA;''', '''#       ifdef RT64_SINGLE_SOURCE_BLEND
            targetBlend.srcBlend = RenderBlend::SRC_ALPHA;
            targetBlend.dstBlend = RenderBlend::INV_SRC_ALPHA;
#       else
            targetBlend.srcBlend = RenderBlend::SRC1_ALPHA;
            targetBlend.dstBlend = RenderBlend::INV_SRC1_ALPHA;
#       endif'''),
]

# prepare_rt64.py applies a file's replacements in this order, after its own.
BY_FILE: dict[str, list[tuple[str, str]]] = {}
for _relative, _old, _new in PATCHES:
    BY_FILE.setdefault(_relative, []).append((_old, _new))
