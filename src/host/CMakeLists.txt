cmake_minimum_required(VERSION 3.20)
project(srw64_native_probe LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-UNDEBUG -fno-strict-aliasing)
# The RSP vector unit needs SSE4.1 on x86-64 (librecomp rsp_vu.hpp); arm64 uses sse2neon.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    if(MSVC)
        add_compile_options(/clang:-march=x86-64-v2)
    else()
        add_compile_options(-march=x86-64-v2)
    endif()
endif()
if(WIN32)
    # clang-cl (docs/design/three-platform-port.md, X3): UTF-8 sources, no min/max macros, M_PI.
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
endif()
get_filename_component(SRW64_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT TARGET srw64_app)
    add_subdirectory("${SRW64_ROOT}/src/native/app" app)
endif()
set(SRW64_BUILD "${SRW64_ROOT}/build/recomp")
set(SRW64_RUNTIME "${SRW64_BUILD}/upstream/N64ModernRuntime")
set(SRW64_GENERATED "${SRW64_BUILD}/cpu-bound/generated")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRW64_ROOT}/config/recomp/rom-variants.json")
file(READ "${SRW64_ROOT}/config/recomp/rom-variants.json" SRW64_VARIANTS_JSON)
string(JSON SRW64_JP_HASH GET "${SRW64_VARIANTS_JSON}" variants jp xxh3_64)
string(JSON SRW64_JP_SHA256 GET "${SRW64_VARIANTS_JSON}" variants jp sha256)
string(JSON SRW64_JP_ID GET "${SRW64_VARIANTS_JSON}" variants jp game_id)
configure_file(rom_variants.hpp.in rom_variants.hpp @ONLY)
add_subdirectory("${SRW64_RUNTIME}" runtime EXCLUDE_FROM_ALL)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SRW64_ROOT}/tools/recomp/toolchain/prepare_runtime_lifecycle.py"
    "${SRW64_ROOT}/src/host/runtime-support/shutdown_trace.hpp"
    "${SRW64_ROOT}/src/host/runtime-support/guest_shutdown.hpp")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${SRW64_ROOT}/tools/recomp/toolchain/prepare_runtime_lifecycle.py"
    COMMAND_ERROR_IS_FATAL ANY)
get_target_property(SRW64_TIMER_SOURCES ultramodern SOURCES)
list(FILTER SRW64_TIMER_SOURCES EXCLUDE REGEX "/(timer|threads|mesgqueue|scheduling)\\.cpp$")
set_property(TARGET ultramodern PROPERTY SOURCES ${SRW64_TIMER_SOURCES}
    "${SRW64_BUILD}/runtime-lifecycle/timer.cpp"
    "${SRW64_BUILD}/runtime-lifecycle/threads.cpp"
    "${SRW64_BUILD}/runtime-lifecycle/mesgqueue.cpp"
    "${SRW64_BUILD}/runtime-lifecycle/scheduling.cpp")
get_target_property(SRW64_RUNTIME_SOURCES librecomp SOURCES)
list(FILTER SRW64_RUNTIME_SOURCES EXCLUDE REGEX "/recomp\\.cpp$")
set_property(TARGET librecomp PROPERTY SOURCES ${SRW64_RUNTIME_SOURCES} "${SRW64_BUILD}/runtime-lifecycle/recomp.cpp")
file(GLOB SRW64_CPU_SOURCES CONFIGURE_DEPENDS "${SRW64_GENERATED}/funcs_*.c")
add_library(srw64_cpu STATIC ${SRW64_CPU_SOURCES})
target_include_directories(srw64_cpu PUBLIC
    "${SRW64_BUILD}/upstream/N64Recomp/include" "${SRW64_GENERATED}")
target_compile_options(srw64_cpu PRIVATE -fno-strict-aliasing $<IF:$<BOOL:${MSVC}>,/clang:-fwrapv,-fwrapv> -Wno-unused-variable)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/host.cpp" AND NOT ANDROID)
    add_executable(srw64-host host.cpp game_hooks.cpp wide_map.cpp battle_hud.cpp "${SRW64_BUILD}/audio-probe/audio.cpp")
    target_include_directories(srw64-host PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${SRW64_BUILD}/upstream/N64Recomp/include"
        "${SRW64_GENERATED}"
        "${SRW64_RUNTIME}/librecomp/include/librecomp"
        "${SRW64_RUNTIME}/thirdparty/sse2neon")
    target_link_libraries(srw64-host PRIVATE srw64_cpu librecomp srw64_app)
endif()

option(SRW64_ENABLE_RT64 "Build the RT64 graphics host (Metal on macOS, Vulkan on Linux)" OFF)
option(SRW64_METAL_SOURCE_SHADERS "Compile embedded Metal source at runtime" ON)
if(SRW64_ENABLE_RT64)
    # Windows: docs/design/three-platform-port.md (X3), work in progress.
    # Android: docs/design/android-port.md; tools/release/android builds it as libmain.so.
    if(NOT APPLE AND NOT WIN32 AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The graphics host builds on macOS, Linux, Windows and Android")
    endif()
    if(APPLE)
        enable_language(OBJCXX)
    endif()
    set(SRW64_RT64 "${SRW64_BUILD}/upstream/RT64")
    srw64_add_launch("${SRW64_RT64}/src/contrib")
    include("${SRW64_ROOT}/cmake/DialogueCpu.cmake")
    srw64_add_dialogue_cpu("${SRW64_RT64}/src/contrib")
    set(RT64_STATIC TRUE CACHE BOOL "" FORCE)
    set(RT64_SDL_WINDOW_VULKAN TRUE CACHE BOOL "" FORCE)
    if(APPLE AND SRW64_METAL_SOURCE_SHADERS)
        add_compile_definitions(SRW64_METAL_SOURCE_SHADERS)
    endif()
    add_subdirectory("${SRW64_RT64}" rt64 EXCLUDE_FROM_ALL)
    if(APPLE)
        # RT64 disables Plume Retina support upstream; match SDL's HiDPI drawable
        # so dialogue and RmlUi render at display pixels instead of window points.
        target_compile_definitions(plume PRIVATE PLUME_APPLE_RETINA_ENABLED)
    endif()
    if(WIN32)
        # RT64 links the SDL2 it carries on Windows; the host uses the same one.
        set(SRW64_SDL2 "${SRW64_RT64}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
        add_library(SDL2::SDL2 SHARED IMPORTED)
        set_target_properties(SDL2::SDL2 PROPERTIES
            IMPORTED_IMPLIB "${SRW64_SDL2}/lib/x64/SDL2.lib"
            IMPORTED_LOCATION "${SRW64_SDL2}/lib/x64/SDL2.dll"
            INTERFACE_INCLUDE_DIRECTORIES "${SRW64_SDL2}/include")
    elseif(NOT TARGET SDL2::SDL2)
        find_package(SDL2 REQUIRED)
    endif()
    option(SRW64_BUILD_UI_PROBE "Build the optional RecompFrontend name-page probe" OFF)
    add_subdirectory("${SRW64_ROOT}/src/native/ui" ui-probe)
    target_include_directories(rt64 PRIVATE "${CMAKE_BINARY_DIR}/rt64/src")
    # Pinned hlsl++ scalar headers call labs without including its declaration.
    target_compile_options(rt64 PRIVATE $<IF:$<BOOL:${MSVC}>,/FIstdlib.h,-include stdlib.h>)
    if(ANDROID)
        # SDL's Java glue loads libmain.so and calls its SDL_main.
        add_library(srw64-gfx-host SHARED host.cpp graphics.cpp audio.cpp "${SRW64_BUILD}/audio-probe/audio.cpp")
        set_target_properties(srw64-gfx-host PROPERTIES OUTPUT_NAME main)
        target_link_libraries(srw64-gfx-host PRIVATE android log)
    else()
        add_executable(srw64-gfx-host host.cpp graphics.cpp audio.cpp "${SRW64_BUILD}/audio-probe/audio.cpp")
    endif()
    target_link_libraries(srw64-gfx-host PRIVATE srw64_launch srw64_ui_pages srw64_dialogue_cpu)
    include("${SRW64_ROOT}/cmake/PixelCompositor.cmake")
    include("${SRW64_ROOT}/cmake/NativeGpu.cmake")
    srw64_add_pixel_compositor("${SRW64_RT64}")
    target_link_libraries(srw64-gfx-host PRIVATE srw64_pixel_compositor)
    target_sources(srw64-gfx-host PRIVATE dialogue_plume.cpp)
    target_sources(srw64-gfx-host PRIVATE native_dialogue.cpp native_intro.cpp "${SRW64_ROOT}/src/native/localization/catalog.cpp")
    # Story text drawn as images, redrawn natively (docs/native/native-title-and-story-images.md).
    target_sources(srw64-gfx-host PRIVATE sprite_text.cpp)
    # The original UI's labels and numbers drawn natively (docs/native/native-ui-text.md).
    target_sources(srw64-gfx-host PRIVATE ui_text.cpp)
    # The title screen's ロード, オプション and song lists (docs/native/native-title-menus.md).
    target_sources(srw64-gfx-host PRIVATE title_page.cpp)
    # The battle viewer: one battle animation from the title (docs/design/battle-viewer.md).
    target_sources(srw64-gfx-host PRIVATE battle_viewer.cpp)
    # Holding R while choosing a move destination: the farthest squares (docs/native/move-jump.md).
    target_sources(srw64-gfx-host PRIVATE move_jump.cpp)
    # L2 / R2 on the idle tactical map: the cursor steps through the enemy units (docs/native/enemy-cycle.md).
    target_sources(srw64-gfx-host PRIVATE enemy_cycle.cpp)
    # The 部隊名 stays マーチウィンド: the naming choice answers itself, 3D5E does nothing
    # (docs/native/fixed-unit-name.md).
    target_sources(srw64-gfx-host PRIVATE unit_name.cpp)
    target_sources(srw64-gfx-host PRIVATE native_name_entry.cpp "${SRW64_ROOT}/src/native/ui/frontend.cpp" "${SRW64_ROOT}/src/native/ui/presentation_settings.cpp")
    # The release version (the root CMakeLists.txt project) for the settings window's About page.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRW64_ROOT}/CMakeLists.txt")
    file(STRINGS "${SRW64_ROOT}/CMakeLists.txt" SRW64_PROJECT_LINE REGEX "^project\\(.* VERSION ")
    string(REGEX REPLACE ".* VERSION ([0-9.]+).*" "\\1" SRW64_VERSION "${SRW64_PROJECT_LINE}")
    set_property(SOURCE "${SRW64_ROOT}/src/native/ui/frontend.cpp" APPEND PROPERTY COMPILE_DEFINITIONS "SRW64_VERSION=\"${SRW64_VERSION}\"")
    if(APPLE)
        target_sources(srw64-gfx-host PRIVATE macos/app_menu.mm)
        set_source_files_properties(macos/app_menu.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    endif()
    # Debug interface (docs/guide/debug-interface.md), inert unless SRW64_DEBUG=1.
    target_sources(srw64-gfx-host PRIVATE debug_server.cpp)
    # Banners and settings share the SDL/RmlUi surface.
    # Link Battler series page in front of the リンク screen.
    target_sources(srw64-gfx-host PRIVATE link_page.cpp battle_page.cpp intermission_page.cpp upgrade_page.cpp parts_page.cpp ability_page.cpp swap_page.cpp cheats.cpp save_page.cpp save_store.cpp autosave.cpp campaign_switch.cpp library.cpp)
    target_compile_definitions(srw64-gfx-host PRIVATE SRW64_NATIVE_DIALOGUE)
    if(APPLE)
        target_link_libraries(srw64-gfx-host PRIVATE "-framework Cocoa" "-framework QuartzCore")
    endif()
    set(SRW64_HOST_TARGETS srw64-gfx-host)
    if(NOT ANDROID)
    list(APPEND SRW64_HOST_TARGETS srw64-frame-host)
    add_executable(srw64-frame-host frame.cpp graphics.cpp audio.cpp)
    add_executable(srw64-content-test "${SRW64_ROOT}/tests/native_content.cpp" "${SRW64_ROOT}/src/native/localization/catalog.cpp")
    target_include_directories(srw64-content-test PRIVATE "${SRW64_ROOT}/src/native" "${SRW64_RT64}/src/contrib" "${CMAKE_CURRENT_LIST_DIR}")
    add_executable(srw64-dialogue-test "${SRW64_ROOT}/tests/native_dialogue.cpp" "${SRW64_ROOT}/src/native/localization/catalog.cpp")
    target_include_directories(srw64-dialogue-test PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_link_libraries(srw64-dialogue-test PRIVATE srw64_dialogue_cpu)
    target_compile_options(srw64-dialogue-test PRIVATE -UNDEBUG)
    endif()
    foreach(SRW64_HOST_TARGET ${SRW64_HOST_TARGETS})
    # HD art made from the player's ROM at run time: window frames (docs/native/native-ui-text.md).
    target_sources(${SRW64_HOST_TARGET} PRIVATE rom_art.cpp)
    target_sources(${SRW64_HOST_TARGET} PRIVATE "${SRW64_ROOT}/src/native/ui/window_test_control.cpp")
    # The HD layers draw through Plume (native_gpu.cpp, shaders in src/host/shaders).
    target_sources(${SRW64_HOST_TARGET} PRIVATE native_gpu.cpp native_map.cpp native_background.cpp native_marker.cpp
        native_sprite.cpp native_portrait.cpp wide_map.cpp battle_hud.cpp)
    # RetroArch shader presets through librashader, loaded at run time (post_filter.hpp);
    # tools/recomp/toolchain/fetch_librashader.py puts the library beside the host.
    target_sources(${SRW64_HOST_TARGET} PRIVATE post_filter.cpp)
    if(APPLE AND EXISTS "${SRW64_BUILD}/thirdparty/librashader/darwin/librashader.dylib")
        add_custom_command(TARGET ${SRW64_HOST_TARGET} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRW64_BUILD}/thirdparty/librashader/darwin/librashader.dylib" "$<TARGET_FILE_DIR:${SRW64_HOST_TARGET}>/librashader.dylib")
    endif()
        target_link_libraries(${SRW64_HOST_TARGET} PRIVATE "-framework Cocoa")
        target_compile_options(${SRW64_HOST_TARGET} PRIVATE -fblocks)
    endif()
    if(SRW64_HOST_TARGET STREQUAL "srw64-gfx-host")
        target_sources(${SRW64_HOST_TARGET} PRIVATE game_hooks.cpp)
        target_link_libraries(${SRW64_HOST_TARGET} PRIVATE srw64_cpu librecomp)
    else()
        # Frame probes replay display lists; they never execute guest CPU code.
        target_link_libraries(${SRW64_HOST_TARGET} PRIVATE librecomp)
    endif()
    target_compile_definitions(${SRW64_HOST_TARGET} PRIVATE SRW64_WITH_RT64)
    target_include_directories(${SRW64_HOST_TARGET} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${SRW64_ROOT}/src/native"
        "${SRW64_BUILD}/upstream/N64Recomp/include" "${SRW64_GENERATED}"
        "${SRW64_RUNTIME}/librecomp/include/librecomp" "${SRW64_RUNTIME}/thirdparty/sse2neon"
    srw64_add_native_gpu_shaders(${SRW64_HOST_TARGET} "${SRW64_RT64}")
    if(APPLE)
        "${SRW64_RT64}/src" "${SRW64_RT64}/src/contrib" "${SRW64_RT64}/src/contrib/hlslpp/include"
        "${SRW64_RT64}/src/contrib/dxc/inc" "${SRW64_RT64}/src/contrib/plume"
        "${SRW64_RT64}/src/contrib/plume/contrib/metal-cpp"
        "${SRW64_RT64}/src/rhi" "${SRW64_RT64}/src/render")
    target_link_libraries(${SRW64_HOST_TARGET} PRIVATE rt64 SDL2::SDL2)
    if(WIN32)
        # rt64 names SDL2 and SDL2main by library name; its link_directories stays in its own directory.
        target_link_directories(${SRW64_HOST_TARGET} PRIVATE "${SRW64_SDL2}/lib/x64")
    endif()
    endforeach()
endif()
