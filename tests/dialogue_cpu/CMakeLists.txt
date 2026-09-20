cmake_minimum_required(VERSION 3.20)
project(srw64_dialogue_backend_tests LANGUAGES CXX)
include(CTest)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_executable(srw64-text-contract-test "${ROOT}/tests/native_text_contract.cpp"
    "${ROOT}/src/host/native_dialogue_text.cpp")
target_compile_features(srw64-text-contract-test PRIVATE cxx_std_20)
target_include_directories(srw64-text-contract-test PRIVATE "${ROOT}/src/native")
if(MSVC)
    target_compile_options(srw64-text-contract-test PRIVATE /W4 /utf-8)
else()
    target_compile_options(srw64-text-contract-test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME text-contract COMMAND srw64-text-contract-test)

set(SRW64_RT64_HEADERS "" CACHE PATH "Pinned RT64 checkout with JSON and the Plume submodule (headers only)")
if(APPLE AND SRW64_RT64_HEADERS)
    include("${ROOT}/cmake/DialogueCpu.cmake")
    srw64_add_dialogue_cpu("${SRW64_RT64_HEADERS}/src/contrib")
    add_executable(srw64-dialogue-reader-test "${ROOT}/tests/native_dialogue.cpp"
        "${ROOT}/src/native/localization/catalog.cpp")
    add_executable(srw64-dialogue-raster-test "${ROOT}/tests/native_dialogue_raster.cpp"
        "${ROOT}/src/native/localization/catalog.cpp")
    set(SRW64_LEGACY_DIALOGUE "" CACHE FILEPATH "CPU oracle generated from the locked pre-split renderer")
    if(SRW64_LEGACY_DIALOGUE)
        target_sources(srw64-dialogue-raster-test PRIVATE "${SRW64_LEGACY_DIALOGUE}")
        target_compile_definitions(srw64-dialogue-raster-test PRIVATE SRW64_COMPARE_LEGACY_RASTER)
    endif()
    foreach(target srw64-dialogue-reader-test srw64-dialogue-raster-test)
        target_link_libraries(${target} PRIVATE srw64_dialogue_cpu)
        # native_dialogue.cpp uses assert; Release must not silently erase tests.
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endforeach()
    add_test(NAME dialogue-reader COMMAND srw64-dialogue-reader-test)
    add_test(NAME dialogue-raster COMMAND srw64-dialogue-raster-test)
    # Compile the real compositor against the pinned Plume/Metal headers, but do
    # not link a fake renderer or claim a GPU/presentation test.
    add_library(srw64-dialogue-metal-compile OBJECT "${ROOT}/src/host/macos/dialogue_metal.cpp")
    target_link_libraries(srw64-dialogue-metal-compile PRIVATE srw64_dialogue_cpu)
    target_include_directories(srw64-dialogue-metal-compile PRIVATE
        "${SRW64_RT64_HEADERS}/src/contrib/plume"
        "${SRW64_RT64_HEADERS}/src/contrib/plume/contrib/metal-cpp")
    target_compile_options(srw64-dialogue-metal-compile PRIVATE -fblocks)
endif()
