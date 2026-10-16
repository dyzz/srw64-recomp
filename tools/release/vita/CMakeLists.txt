cmake_minimum_required(VERSION 3.20)
# The PS Vita V0 probe (docs/design/psv-port.md): the recompiled game (srw64_cpu), the RSP
# audio microcode (srw64_audio) and the runtime (ultramodern, librecomp) as static libraries
# for a 32-bit ARM target, from the sources src/host builds plus the Vita counterparts
# tools/recomp/toolchain/prepare_vita_runtime.py writes. With SRW64_VITA_PROBE_HOST the
# headless probe host (probe_host.cpp) links them too, for qemu-arm or a native reference.
project(srw64_vita_probe LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(SRW64_VITA_PROBE_HOST "Build the headless probe host (Linux or macOS, not the Vita toolchain)" OFF)
get_filename_component(SRW64_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(SRW64_BUILD "${SRW64_ROOT}/build/recomp")
set(SRW64_RUNTIME "${SRW64_BUILD}/upstream/N64ModernRuntime")
set(SRW64_GENERATED "${SRW64_BUILD}/cpu-bound/generated")
set(SRW64_VITA_RUNTIME "${SRW64_BUILD}/vita-runtime")
set(SRW64_LIBRECOMP_SOURCE "${SRW64_RUNTIME}/librecomp")
add_compile_options(-fno-strict-aliasing)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SRW64_VITA_RUNTIME}/manifest.json" "${SRW64_BUILD}/runtime-lifecycle/manifest.json")

# ultramodern: upstream's target with the lifecycle sources, as src/host/CMakeLists.txt builds it.
add_subdirectory("${SRW64_RUNTIME}/ultramodern" ultramodern)
get_target_property(SRW64_ULTRAMODERN_SOURCES ultramodern SOURCES)
list(FILTER SRW64_ULTRAMODERN_SOURCES EXCLUDE REGEX "/(timer|threads|mesgqueue|scheduling|error_handling)\\.cpp$")
if(SRW64_VITA_PROBE_HOST)
    # The VI thread paced by the game for reproducible snapshots (build_probe.py writes it).
    list(FILTER SRW64_ULTRAMODERN_SOURCES EXCLUDE REGEX "/events\\.cpp$")
    list(APPEND SRW64_ULTRAMODERN_SOURCES "${CMAKE_BINARY_DIR}/probe_events.cpp")
endif()
set_property(TARGET ultramodern PROPERTY SOURCES ${SRW64_ULTRAMODERN_SOURCES}
    "${SRW64_VITA_RUNTIME}/error_handling.cpp"
    "${SRW64_BUILD}/runtime-lifecycle/timer.cpp"
    "${SRW64_BUILD}/runtime-lifecycle/threads.cpp"
    "${SRW64_BUILD}/runtime-lifecycle/mesgqueue.cpp"
    "${SRW64_BUILD}/runtime-lifecycle/scheduling.cpp")
# The Vita counterparts of the headers (renderer_context.hpp, the semaphore) ahead of upstream's.
target_include_directories(ultramodern BEFORE PUBLIC "${SRW64_VITA_RUNTIME}/include")
# librecomp: the Vita counterpart of its CMakeLists.txt (no mods, live recompiler or heap).
add_subdirectory("${SRW64_VITA_RUNTIME}/librecomp" librecomp)

file(GLOB SRW64_CPU_SOURCES CONFIGURE_DEPENDS "${SRW64_GENERATED}/funcs_*.c")
add_library(srw64_cpu STATIC ${SRW64_CPU_SOURCES})
target_include_directories(srw64_cpu PUBLIC
    "${SRW64_VITA_RUNTIME}/include" "${SRW64_BUILD}/upstream/N64Recomp/include" "${SRW64_GENERATED}")
target_compile_options(srw64_cpu PRIVATE -fno-strict-aliasing -fwrapv -Wno-unused-variable)

add_library(srw64_audio STATIC "${SRW64_BUILD}/audio-probe/audio.cpp")
target_link_libraries(srw64_audio PRIVATE librecomp ultramodern)

if(SRW64_VITA_PROBE_HOST)
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/probe_hooks.cpp" OR NOT EXISTS "${CMAKE_BINARY_DIR}/probe_events.cpp")
        message(FATAL_ERROR "build_probe.py writes probe_hooks.cpp and probe_events.cpp into the build directory first")
    endif()
    add_executable(srw64-vita-probe probe_host.cpp "${CMAKE_BINARY_DIR}/probe_hooks.cpp")
    target_include_directories(srw64-vita-probe PRIVATE
        "${SRW64_GENERATED}" "${SRW64_RUNTIME}/librecomp/include/librecomp" "${SRW64_RUNTIME}/thirdparty/sse2neon")
    find_package(Threads REQUIRED)
    target_link_libraries(srw64-vita-probe PRIVATE srw64_cpu srw64_audio librecomp ultramodern Threads::Threads)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # The runtime's Linux thread naming; static so qemu-arm needs no sysroot at run time.
        target_link_options(srw64-vita-probe PRIVATE -static)
    endif()
endif()
