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
if(SRW64_RT64_HEADERS)
    include("${ROOT}/cmake/DialogueCpu.cmake")
    srw64_add_dialogue_cpu("${SRW64_RT64_HEADERS}/src/contrib")
    add_executable(srw64-dialogue-reader-test "${ROOT}/tests/native_dialogue.cpp"
        "${ROOT}/src/native/localization/catalog.cpp")
    add_executable(srw64-dialogue-raster-test "${ROOT}/tests/native_dialogue_raster.cpp"
        "${ROOT}/src/native/localization/catalog.cpp")
    foreach(target srw64-dialogue-reader-test srw64-dialogue-raster-test)
        target_link_libraries(${target} PRIVATE srw64_dialogue_cpu)
        # native_dialogue.cpp uses assert; Release must not silently erase tests.
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG /utf-8)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
    add_test(NAME dialogue-reader COMMAND srw64-dialogue-reader-test)
    add_test(NAME dialogue-raster COMMAND srw64-dialogue-raster-test)
    # The paging cases shared with the Python reference; they need the packaged fonts.
    add_executable(srw64-paging-cases "${ROOT}/tests/dialogue_cpu/paging_cases.cpp"
        "${ROOT}/src/native/localization/catalog.cpp")
    target_link_libraries(srw64-paging-cases PRIVATE srw64_dialogue_cpu)
    set(SRW64_TEST_FONT_DIR "" CACHE PATH "Packaged fonts (tools/content/prepare_fonts.py) for the paging cases")
    if(SRW64_TEST_FONT_DIR)
        add_test(NAME dialogue-paging-cases COMMAND srw64-paging-cases --check
            "${ROOT}/tests/data/dialogue-paging-inputs.json" "${ROOT}/tests/data/dialogue-paging-cases.json")
        set_tests_properties(dialogue-paging-cases PROPERTIES ENVIRONMENT "SRW64_FONT_DIR=${SRW64_TEST_FONT_DIR}")
    endif()
endif()

set(SRW64_TEST_CJK_FONT "" CACHE FILEPATH "Optional explicit CJK test font")
if(SRW64_TEST_CJK_FONT AND SRW64_RT64_HEADERS)
    set_tests_properties(dialogue-reader dialogue-raster PROPERTIES
        ENVIRONMENT "SRW64_TEXT_FONT=${SRW64_TEST_CJK_FONT}")
endif()
