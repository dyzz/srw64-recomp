cmake_minimum_required(VERSION 3.20)
project(srw64_native_probe LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-UNDEBUG -fno-strict-aliasing)
get_filename_component(SRW64_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(SRW64_BUILD "${SRW64_ROOT}/build/recomp")
set(SRW64_RUNTIME "${SRW64_BUILD}/upstream/N64ModernRuntime")
set(SRW64_GENERATED "${SRW64_BUILD}/cpu-bound/generated")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRW64_ROOT}/config/recomp/rom-variants.json")
file(READ "${SRW64_ROOT}/config/recomp/rom-variants.json" SRW64_VARIANTS_JSON)
string(JSON SRW64_JP_HASH GET "${SRW64_VARIANTS_JSON}" variants jp xxh3_64)
string(JSON SRW64_JP_ID GET "${SRW64_VARIANTS_JSON}" variants jp game_id)
string(JSON SRW64_MODEL5600_HASH GET "${SRW64_VARIANTS_JSON}" variants model5600 xxh3_64)
string(JSON SRW64_MODEL5600_ID GET "${SRW64_VARIANTS_JSON}" variants model5600 game_id)
configure_file(rom_variants.hpp.in rom_variants.hpp @ONLY)
add_subdirectory("${SRW64_RUNTIME}" runtime EXCLUDE_FROM_ALL)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SRW64_ROOT}/tools/recomp/prepare_runtime_lifecycle.py"
    "${SRW64_ROOT}/tools/recomp/runtime-support/shutdown_trace.hpp"
    "${SRW64_ROOT}/tools/recomp/runtime-support/guest_shutdown.hpp")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${SRW64_ROOT}/tools/recomp/prepare_runtime_lifecycle.py"
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
target_compile_options(srw64_cpu PRIVATE -fno-strict-aliasing -fwrapv -Wno-unused-variable)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/host.cpp")
    add_executable(srw64-host host.cpp game_hooks.cpp "${SRW64_BUILD}/audio-probe/audio.cpp")
    target_include_directories(srw64-host PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${SRW64_BUILD}/upstream/N64Recomp/include"
        "${SRW64_GENERATED}"
        "${SRW64_RUNTIME}/librecomp/include/librecomp"
        "${SRW64_RUNTIME}/thirdparty/sse2neon")
    target_link_libraries(srw64-host PRIVATE srw64_cpu librecomp)
endif()

option(SRW64_ENABLE_RT64 "Build the experimental Metal graphics host" OFF)
option(SRW64_METAL_SOURCE_SHADERS "Compile embedded Metal source at runtime" ON)
if(SRW64_ENABLE_RT64)
    if(NOT APPLE)
        message(FATAL_ERROR "The current SRW64 window adapter targets macOS")
    endif()
    enable_language(OBJCXX)
    set(SRW64_RT64 "${SRW64_BUILD}/upstream/RT64")
    set(RT64_STATIC TRUE CACHE BOOL "" FORCE)
    set(RT64_SDL_WINDOW_VULKAN TRUE CACHE BOOL "" FORCE)
    if(SRW64_METAL_SOURCE_SHADERS)
        add_compile_definitions(SRW64_METAL_SOURCE_SHADERS)
    endif()
    add_subdirectory("${SRW64_RT64}" rt64 EXCLUDE_FROM_ALL)
    find_package(SDL2 REQUIRED)
    target_include_directories(rt64 PRIVATE "${CMAKE_BINARY_DIR}/rt64/src")
    # Pinned hlsl++ scalar headers call labs without including its declaration.
    target_compile_options(rt64 PRIVATE -include stdlib.h)
    add_executable(srw64-gfx-host host.cpp graphics.cpp audio.cpp "${SRW64_BUILD}/audio-probe/audio.cpp")
    target_sources(srw64-gfx-host PRIVATE native_dialogue.cpp native_dialogue_text.cpp native_intro.cpp "${SRW64_ROOT}/src/native/localization/catalog.cpp")
    target_sources(srw64-gfx-host PRIVATE native_name_entry.cpp native_name_entry_macos.mm presentation_settings_macos.mm rule_menu_macos.mm settings_window_macos.mm)
    set_source_files_properties(native_name_entry_macos.mm window_test_control_macos.mm presentation_settings_macos.mm rule_menu_macos.mm settings_window_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(srw64-gfx-host PRIVATE SRW64_NATIVE_DIALOGUE)
    target_link_libraries(srw64-gfx-host PRIVATE "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation" "-framework Cocoa")
    add_executable(srw64-frame-host frame.cpp graphics.cpp audio.cpp)
    add_executable(srw64-coretext-frame-host frame.cpp graphics.cpp audio.cpp coretext_probe.cpp)
    add_executable(srw64-content-test "${SRW64_ROOT}/tests/native_content.cpp" "${SRW64_ROOT}/src/native/localization/catalog.cpp")
    target_include_directories(srw64-content-test PRIVATE "${SRW64_ROOT}/src/native" "${SRW64_RT64}/src/contrib" "${CMAKE_CURRENT_LIST_DIR}")
    add_executable(srw64-dialogue-test "${SRW64_ROOT}/tests/native_dialogue.cpp" native_dialogue_text.cpp "${SRW64_ROOT}/src/native/localization/catalog.cpp")
    target_include_directories(srw64-dialogue-test PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_link_libraries(srw64-dialogue-test PRIVATE "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation")
    target_compile_definitions(srw64-coretext-frame-host PRIVATE SRW64_CORETEXT_PROBE)
    target_link_libraries(srw64-coretext-frame-host PRIVATE "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation")
    foreach(SRW64_HOST_TARGET srw64-gfx-host srw64-frame-host srw64-coretext-frame-host srw64-dialogue-test)
    if(NOT SRW64_HOST_TARGET STREQUAL "srw64-dialogue-test")
        target_sources(${SRW64_HOST_TARGET} PRIVATE native_marker.cpp window_test_control_macos.mm)
        target_link_libraries(${SRW64_HOST_TARGET} PRIVATE "-framework Cocoa")
    endif()
    if(SRW64_HOST_TARGET STREQUAL "srw64-gfx-host")
        target_sources(${SRW64_HOST_TARGET} PRIVATE game_hooks.cpp)
        target_link_libraries(${SRW64_HOST_TARGET} PRIVATE srw64_cpu librecomp)
    elseif(NOT SRW64_HOST_TARGET STREQUAL "srw64-dialogue-test")
        # Frame probes replay display lists; they never execute guest CPU code.
        target_link_libraries(${SRW64_HOST_TARGET} PRIVATE librecomp)
    endif()
    target_compile_definitions(${SRW64_HOST_TARGET} PRIVATE SRW64_WITH_RT64)
    target_include_directories(${SRW64_HOST_TARGET} PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}" "${SRW64_ROOT}/src/native"
        "${SRW64_BUILD}/upstream/N64Recomp/include" "${SRW64_GENERATED}"
        "${SRW64_RUNTIME}/librecomp/include/librecomp" "${SRW64_RUNTIME}/thirdparty/sse2neon"
        "${SRW64_RT64}/src" "${SRW64_RT64}/src/contrib" "${SRW64_RT64}/src/contrib/hlslpp/include"
        "${SRW64_RT64}/src/contrib/dxc/inc" "${SRW64_RT64}/src/contrib/plume"
        "${SRW64_RT64}/src/contrib/plume/contrib/metal-cpp"
        "${SRW64_RT64}/src/rhi" "${SRW64_RT64}/src/render")
    target_compile_options(${SRW64_HOST_TARGET} PRIVATE -fblocks)
    target_link_libraries(${SRW64_HOST_TARGET} PRIVATE rt64 SDL2::SDL2)
    endforeach()
endif()
