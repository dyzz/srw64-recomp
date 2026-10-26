cmake_minimum_required(VERSION 3.20)
project(srw64_native_probe LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NINTENDO_SWITCH)
    # libnx has no libdl; FindICU would name it for ICU::uc.
    set(CMAKE_DL_LIBS "")
endif()
# assert() stays on in the desktop hosts. Android ships with NDEBUG (2026-10-08): a failed
# assert closes the game, and Plume without NDEBUG names Vulkan objects (VK_EXT_debug_utils)
# and looks for the validation layer. SRW64_ASSERTS=ON keeps them (build_game.py --validation).
if(ANDROID)
    option(SRW64_ASSERTS "Build with assert() and Plume's debug paths" OFF)
else()
    option(SRW64_ASSERTS "Build with assert() and Plume's debug paths" ON)
endif()
if(SRW64_ASSERTS)
    add_compile_options(-UNDEBUG)
endif()
# Every target a third-party directory defines, its subdirectories' too, built with NDEBUG.
function(srw64_release_targets directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            target_compile_options(${target} PRIVATE -DNDEBUG)
        endif()
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        srw64_release_targets("${subdirectory}")
    endforeach()
endfunction()
add_compile_options(-fno-strict-aliasing)
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
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(NINTENDO_SWITCH)
    # The Switch (docs/design/switch-port.md, S0): the runtime's counterparts that
    # tools/recomp/toolchain/prepare_switch_runtime.py writes (no mods, live recompiler or
    # recompiled heap; RDRAM from calloc).
    set(SRW64_SWITCH_RUNTIME "${SRW64_BUILD}/switch-runtime")
    # newlib hides POSIX and BSD declarations (posix_memalign for sse2neon) under strict ISO.
    add_compile_definitions(_GNU_SOURCE)
    set(SRW64_LIBRECOMP_SOURCE "${SRW64_RUNTIME}/librecomp")
    add_subdirectory("${SRW64_RUNTIME}/ultramodern" ultramodern EXCLUDE_FROM_ALL)
    target_include_directories(ultramodern BEFORE PUBLIC "${SRW64_SWITCH_RUNTIME}/include")
    add_subdirectory("${SRW64_SWITCH_RUNTIME}/librecomp" librecomp EXCLUDE_FROM_ALL)
else()
    add_subdirectory("${SRW64_RUNTIME}" runtime EXCLUDE_FROM_ALL)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SRW64_ROOT}/tools/recomp/toolchain/prepare_runtime_lifecycle.py"
    "${SRW64_ROOT}/src/host/runtime-support/shutdown_trace.hpp"
    "${SRW64_ROOT}/src/host/runtime-support/guest_shutdown.hpp")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${SRW64_ROOT}/tools/recomp/toolchain/prepare_runtime_lifecycle.py"
    COMMAND_ERROR_IS_FATAL ANY)
get_target_property(SRW64_TIMER_SOURCES ultramodern SOURCES)
list(FILTER SRW64_TIMER_SOURCES EXCLUDE REGEX "/(timer|threads|mesgqueue|scheduling|events)\\.cpp$")
if(NINTENDO_SWITCH)   # events.cpp: the lifecycle's with the Switch's changes over it (prepare_switch_runtime.py)
    list(FILTER SRW64_TIMER_SOURCES EXCLUDE REGEX "/error_handling\\.cpp$")
    list(APPEND SRW64_TIMER_SOURCES "${SRW64_SWITCH_RUNTIME}/error_handling.cpp")
    set(SRW64_EVENTS "${SRW64_SWITCH_RUNTIME}/events.cpp")
else()
    set(SRW64_EVENTS "${SRW64_BUILD}/runtime-lifecycle/events.cpp")
endif()
if(VITA)   # the lifecycle file with the stall report's counts (prepare_vita_runtime.py)
    set(SRW64_MESGQUEUE "${SRW64_VITA_RUNTIME}/mesgqueue.cpp")
    target_include_directories(ultramodern PRIVATE "${SRW64_BUILD}/runtime-lifecycle")
else()
    set(SRW64_MESGQUEUE "${SRW64_BUILD}/runtime-lifecycle/mesgqueue.cpp")
endif()
set_property(TARGET ultramodern PROPERTY SOURCES ${SRW64_TIMER_SOURCES}
    "${SRW64_BUILD}/runtime-lifecycle/timer.cpp"
    "${SRW64_BUILD}/runtime-lifecycle/threads.cpp"
    "${SRW64_MESGQUEUE}"
    "${SRW64_BUILD}/runtime-lifecycle/scheduling.cpp"
    "${SRW64_EVENTS}")
if(NOT NINTENDO_SWITCH)   # the Switch's librecomp names its own recomp.cpp
    get_target_property(SRW64_RUNTIME_SOURCES librecomp SOURCES)
    list(FILTER SRW64_RUNTIME_SOURCES EXCLUDE REGEX "/recomp\\.cpp$")
    set_property(TARGET librecomp PROPERTY SOURCES ${SRW64_RUNTIME_SOURCES} "${SRW64_BUILD}/runtime-lifecycle/recomp.cpp")
endif()
file(GLOB SRW64_CPU_SOURCES CONFIGURE_DEPENDS "${SRW64_GENERATED}/funcs_*.c")
add_library(srw64_cpu STATIC ${SRW64_CPU_SOURCES})
target_include_directories(srw64_cpu PUBLIC
    "${SRW64_BUILD}/upstream/N64Recomp/include" "${SRW64_GENERATED}")
target_compile_options(srw64_cpu PRIVATE -fno-strict-aliasing $<IF:$<BOOL:${MSVC}>,/clang:-fwrapv,-fwrapv> -Wno-unused-variable)
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
    # SRW64_ASSERTS is for our own code. RT64 and what it carries build with NDEBUG:
    # without it Plume turns on the D3D12 debug layer, breaking on every error, and the
    # Vulkan validation layer, wherever they are installed (Windows' Graphics Tools, a
    # Vulkan SDK; 0.4.6 broke so at the title on a player's Radeon 8060S), and the rest
    # keep debug paths no release wants. Appended after the directory's -UNDEBUG, so NDEBUG
    # wins for these targets alone, as for RmlUi (src/native/ui/CMakeLists.txt).
    srw64_release_targets("${SRW64_RT64}")
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
    target_sources(srw64-gfx-host PRIVATE native_dialogue.cpp native_intro.cpp dialogue_data.cpp "${SRW64_ROOT}/src/native/localization/catalog.cpp")
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
    if(WIN32)
        # Double-clicking Marchwind64.exe: the ROM dialog and message box (host.cpp main()).
        target_sources(srw64-gfx-host PRIVATE windows/desktop_windows.cpp)
        # The menu bar (src/native/ui/app_menu.hpp), the executable's icon, and the manifest
        # that makes UTF-8 its code page (CMake embeds a listed .manifest).
        target_sources(srw64-gfx-host PRIVATE windows/app_menu_windows.cpp windows/marchwind64.rc windows/marchwind64.manifest)
        target_link_libraries(srw64-gfx-host PRIVATE comdlg32 user32 ole32 shell32)
        # A crash's minidump into the run directory (crash_dump.hpp, docs/native/bug-report.md).
        target_sources(srw64-gfx-host PRIVATE windows/crash_dump_windows.cpp)
        target_link_libraries(srw64-gfx-host PRIVATE dbghelp synchronization)
        # Each adapter's D3D12 support and the Vulkan loader's, into console.log (gpu_report.hpp).
        target_sources(srw64-gfx-host PRIVATE windows/gpu_report_windows.cpp)
    endif()
    # The update check: the website's /latest.json through the system's HTTP (docs/native/update-check.md).
    target_sources(srw64-gfx-host PRIVATE update_check.cpp update_http.cpp)
    # What the program prints, into the run's console.log, and the bug report zip of the
    # Feedback page (docs/native/bug-report.md).
    target_sources(srw64-gfx-host PRIVATE console_log.cpp bug_report.cpp stall_watchdog.cpp)
    if(APPLE)
        target_sources(srw64-gfx-host PRIVATE macos/update_http_macos.mm)
        set_source_files_properties(macos/update_http_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    elseif(WIN32)
        target_link_libraries(srw64-gfx-host PRIVATE winhttp)
    elseif(NOT ANDROID)
        target_link_libraries(srw64-gfx-host PRIVATE ${CMAKE_DL_LIBS})
    endif()
    # Debug interface (docs/guide/debug-interface.md), inert unless SRW64_DEBUG=1.
    target_sources(srw64-gfx-host PRIVATE debug_server.cpp debug_transport.cpp)
    # Display list statistics for the PS Vita study (docs/design/psv-port.md), with SRW64_GBI_STATS.
    target_sources(srw64-gfx-host PRIVATE gbi_stats.cpp)
    if(WIN32)
        target_link_libraries(srw64-gfx-host PRIVATE ws2_32)
    endif()
    # Banners and settings share the SDL/RmlUi surface.
    # Link Battler series page in front of the リンク screen.
    target_sources(srw64-gfx-host PRIVATE link_page.cpp battle_page.cpp intermission_page.cpp upgrade_page.cpp parts_page.cpp ability_page.cpp swap_page.cpp cheats.cpp save_page.cpp save_store.cpp autosave.cpp quick_load.cpp campaign_switch.cpp library.cpp stage_info.cpp legacy.cpp mod_library.cpp music.cpp)
    target_compile_definitions(srw64-gfx-host PRIVATE SRW64_NATIVE_DIALOGUE)
    if(APPLE)
        target_link_libraries(srw64-gfx-host PRIVATE "-framework Cocoa" "-framework QuartzCore")
    endif()
    set(SRW64_HOST_TARGETS srw64-gfx-host)
    if(NOT ANDROID)
    # The PS Vita renderer replayed offline on the desktop's OpenGL 2.1 against RT64's
    # pictures (docs/design/psv-port.md, V1; dumps from gbi_stats.hpp). Built on request.
    find_package(OpenGL)
    if(OpenGL_FOUND)
        add_executable(srw64-gl-replay EXCLUDE_FROM_ALL "${SRW64_ROOT}/src/gl/replay.cpp"
            "${SRW64_ROOT}/src/gl/display_list.cpp" "${SRW64_ROOT}/src/gl/gl_renderer.cpp" "${SRW64_ROOT}/src/gl/program_cache.cpp")
        target_include_directories(srw64-gl-replay PRIVATE "${SRW64_RT64}/src/contrib")
        target_link_libraries(srw64-gl-replay PRIVATE SDL2::SDL2 OpenGL::GL)
    endif()
    list(APPEND SRW64_HOST_TARGETS srw64-frame-host)
    add_executable(srw64-frame-host frame.cpp graphics.cpp audio.cpp stall_watchdog.cpp)
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
        native_sprite.cpp native_portrait.cpp wide_map.cpp battle_hud.cpp menu_widen.cpp native_quads.cpp hd_texture.cpp)
    # RetroArch shader presets through librashader, loaded at run time (post_filter.hpp);
    # tools/recomp/toolchain/fetch_librashader.py puts the library beside the host.
    target_sources(${SRW64_HOST_TARGET} PRIVATE post_filter.cpp)
    if(APPLE AND EXISTS "${SRW64_BUILD}/thirdparty/librashader/darwin/librashader.dylib")
        add_custom_command(TARGET ${SRW64_HOST_TARGET} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SRW64_BUILD}/thirdparty/librashader/darwin/librashader.dylib" "$<TARGET_FILE_DIR:${SRW64_HOST_TARGET}>/librashader.dylib")
    endif()
    # The built-in filters beside the host, as the packages ship them (tools/content/fetch_filters.py).
    if(EXISTS "${SRW64_ROOT}/build/filters")
        add_custom_command(TARGET ${SRW64_HOST_TARGET} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRW64_ROOT}/build/filters" "$<TARGET_FILE_DIR:${SRW64_HOST_TARGET}>/filters")
    endif()
    srw64_add_native_gpu_shaders(${SRW64_HOST_TARGET} "${SRW64_RT64}")
    if(APPLE)
        target_link_libraries(${SRW64_HOST_TARGET} PRIVATE "-framework Cocoa")
        target_compile_options(${SRW64_HOST_TARGET} PRIVATE -fblocks)
    endif()
    if(SRW64_HOST_TARGET STREQUAL "srw64-gfx-host")
        target_sources(${SRW64_HOST_TARGET} PRIVATE game_hooks.cpp)
        # The PS Vita's GL renderer, chosen at run time with SRW64_RENDERER=gl (graphics.cpp),
        # on the Mac only, for developing the consoles' renderer: the Windows SDK's GL/gl.h needs
        # <windows.h> first and has no GL/glext.h, and the Linux package may not link libOpenGL.
        if(OpenGL_FOUND AND APPLE)
            target_sources(${SRW64_HOST_TARGET} PRIVATE "${SRW64_ROOT}/src/gl/display_list.cpp" "${SRW64_ROOT}/src/gl/gl_renderer.cpp" "${SRW64_ROOT}/src/gl/program_cache.cpp" dialogue_gl.cpp native_gl.cpp hd_replace.cpp
                "${SRW64_ROOT}/src/native/ui/ui_renderer_gl.cpp")
            target_compile_definitions(${SRW64_HOST_TARGET} PRIVATE SRW64_GL_RENDERER)
            target_link_libraries(${SRW64_HOST_TARGET} PRIVATE OpenGL::GL)
        endif()
        target_link_libraries(${SRW64_HOST_TARGET} PRIVATE srw64_cpu librecomp)
    else()
        # Frame probes replay display lists; they never execute guest CPU code.
        target_link_libraries(${SRW64_HOST_TARGET} PRIVATE librecomp)
    endif()
    target_include_directories(${SRW64_HOST_TARGET} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${SRW64_ROOT}/src/native"
        "${SRW64_BUILD}/upstream/N64Recomp/include" "${SRW64_GENERATED}"
        "${SRW64_RUNTIME}/librecomp/include/librecomp" "${SRW64_RUNTIME}/thirdparty/sse2neon"
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
# The PS Vita's host without RT64 or Plume (docs/design/psv-port.md, V1 → V2): the GL
# renderer (src/gl), RmlUi on GL, the native dialogue and the host's tagged draws, built on
# the desktop's OpenGL 2.1 here and on vitaGL there. The HD layers, the RetroArch filters and
# the Plume adapters are not compiled (gl_only_stubs.cpp answers for them).
option(SRW64_GL_ONLY "Build srw64-gl-host: the GL renderer only, no RT64" OFF)
if(SRW64_GL_ONLY)
    if(SRW64_ENABLE_RT64)
        message(FATAL_ERROR "SRW64_GL_ONLY and SRW64_ENABLE_RT64 build different hosts: use separate build folders")
    endif()
    if(APPLE)
        enable_language(OBJCXX)
    endif()
    # Header-only libraries that live in RT64's checkout (json, stb); nothing of RT64 is built.
    set(SRW64_RT64 "${SRW64_BUILD}/upstream/RT64")
    srw64_add_launch("${SRW64_RT64}/src/contrib")
    include("${SRW64_ROOT}/cmake/DialogueCpu.cmake")
    srw64_add_dialogue_cpu("${SRW64_RT64}/src/contrib")
    if(NOT TARGET SDL2::SDL2)
        find_package(SDL2 REQUIRED)
    endif()
    if(NINTENDO_SWITCH)
        # OpenGL ES 2 on devkitPro's Mesa (nouveau) through EGL; src/gl/gl_api.hpp takes the
        # GLES headers there.
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SRW64_SWITCH_GL REQUIRED IMPORTED_TARGET glesv2 egl)
        add_library(OpenGL::GL ALIAS PkgConfig::SRW64_SWITCH_GL)
    else()
        find_package(OpenGL REQUIRED)
    endif()
    add_subdirectory("${SRW64_ROOT}/src/native/ui" ui-probe)
    add_executable(srw64-gl-host host.cpp graphics.cpp audio.cpp "${SRW64_BUILD}/audio-probe/audio.cpp"
        native_dialogue.cpp native_intro.cpp dialogue_data.cpp "${SRW64_ROOT}/src/native/localization/catalog.cpp"
        sprite_text.cpp ui_text.cpp title_page.cpp battle_viewer.cpp move_jump.cpp enemy_cycle.cpp unit_name.cpp
        native_name_entry.cpp "${SRW64_ROOT}/src/native/ui/frontend.cpp" "${SRW64_ROOT}/src/native/ui/presentation_settings.cpp"
        update_check.cpp update_http.cpp console_log.cpp bug_report.cpp stall_watchdog.cpp
        debug_server.cpp debug_transport.cpp gbi_stats.cpp
        link_page.cpp battle_page.cpp intermission_page.cpp upgrade_page.cpp parts_page.cpp ability_page.cpp swap_page.cpp
        cheats.cpp save_page.cpp save_store.cpp autosave.cpp campaign_switch.cpp library.cpp stage_info.cpp
        rom_art.cpp legacy.cpp quick_load.cpp mod_library.cpp music.cpp "${SRW64_ROOT}/src/native/ui/window_test_control.cpp"
        native_map.cpp native_sprite.cpp wide_map.cpp battle_hud.cpp menu_widen.cpp native_quads.cpp game_hooks.cpp
        native_portrait.cpp native_background.cpp hd_texture.cpp hd_replace.cpp native_marker.cpp
        "${SRW64_ROOT}/src/gl/display_list.cpp" "${SRW64_ROOT}/src/gl/gl_renderer.cpp" "${SRW64_ROOT}/src/gl/program_cache.cpp" dialogue_gl.cpp native_gl.cpp hd_replace.cpp
        "${SRW64_ROOT}/src/native/ui/ui_renderer_gl.cpp" gl_only_stubs.cpp)
    target_compile_definitions(srw64-gl-host PRIVATE SRW64_NATIVE_DIALOGUE SRW64_GL_RENDERER SRW64_GL_ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRW64_ROOT}/CMakeLists.txt")
    file(STRINGS "${SRW64_ROOT}/CMakeLists.txt" SRW64_PROJECT_LINE REGEX "^project\\(.* VERSION ")
    string(REGEX REPLACE ".* VERSION ([0-9.]+).*" "\\1" SRW64_VERSION "${SRW64_PROJECT_LINE}")
    set_property(SOURCE "${SRW64_ROOT}/src/native/ui/frontend.cpp" APPEND PROPERTY COMPILE_DEFINITIONS "SRW64_VERSION=\"${SRW64_VERSION}\"")
    if(APPLE)
        target_sources(srw64-gl-host PRIVATE macos/app_menu.mm macos/update_http_macos.mm)
        set_source_files_properties(macos/app_menu.mm macos/update_http_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(srw64-gl-host PRIVATE "-framework Cocoa" "-framework QuartzCore")
    elseif(NINTENDO_SWITCH)
        # console_data.cpp: the game data made from the player's ROM on the first launch.
        target_sources(srw64-gl-host PRIVATE switch/switch_platform.cpp "${SRW64_ROOT}/src/native/app/console_data.cpp")
        set_property(SOURCE "${SRW64_ROOT}/src/native/app/console_data.cpp" APPEND PROPERTY COMPILE_DEFINITIONS "SRW64_VERSION=\"${SRW64_VERSION}\"")
        target_link_options(srw64-gl-host PRIVATE -Wl,--wrap=abort,--wrap=rename,--wrap=__syscall_thread_create,--wrap=fchmod,--wrap=chmod)
        target_link_libraries(srw64-gl-host PRIVATE zstd)   # the NRO's HD pictures (hd_texture.cpp)
    elseif(NOT WIN32)
        target_link_libraries(srw64-gl-host PRIVATE ${CMAKE_DL_LIBS})
    endif()
    if(NINTENDO_SWITCH)   # the runtime's Switch headers ahead of upstream's (the semaphore)
        target_include_directories(srw64-gl-host BEFORE PRIVATE "${SRW64_SWITCH_RUNTIME}/include")
    endif()
    target_include_directories(srw64-gl-host PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${SRW64_ROOT}/src/native"
        "${SRW64_BUILD}/upstream/N64Recomp/include" "${SRW64_GENERATED}"
        "${SRW64_RUNTIME}/librecomp/include/librecomp" "${SRW64_RUNTIME}/thirdparty/sse2neon"
        "${SRW64_RUNTIME}/thirdparty/concurrentqueue" "${SRW64_RT64}/src/contrib" "${SRW64_RT64}/include")
    target_link_libraries(srw64-gl-host PRIVATE srw64_launch srw64_ui_pages srw64_dialogue_cpu srw64_cpu librecomp
        SDL2::SDL2 OpenGL::GL)
    if(NINTENDO_SWITCH)
        # Marchwind64.nro: the NACP (name, author, version) and the default icon for now.
        nx_generate_nacp(Marchwind64.nacp NAME "Marchwind64" AUTHOR "Marchwind64" VERSION "${SRW64_VERSION}")
        # hbmenu's icon: the M64 badge on the Steam icon's navy, 256 x 256 (tools/release/switch/icon.jpg).
        # The HD pack prepared for the Switch's GPU (tools/release/switch/prepare-hd) in the
        # NRO's romfs, when it has been made: one file to copy, the art read where it lies.
        set(SRW64_SWITCH_ROMFS "${SRW64_BUILD}/switch-romfs")
        option(SRW64_SWITCH_EMBED_HD "Put the prepared HD pack in the NRO (off: package.py --hd sends it beside the NRO)" ON)
        if(SRW64_SWITCH_EMBED_HD AND EXISTS "${SRW64_SWITCH_ROMFS}/hd")
            nx_create_nro(srw64-gl-host OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Marchwind64.nro" NACP Marchwind64.nacp
                          ICON "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/release/switch/icon.jpg" ROMFS "${SRW64_SWITCH_ROMFS}")
        else()
            nx_create_nro(srw64-gl-host OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Marchwind64.nro" NACP Marchwind64.nacp
                          ICON "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/release/switch/icon.jpg")
        endif()
    endif()
endif()
