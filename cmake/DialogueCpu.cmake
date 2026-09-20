# Shared portable scene renderer used by the game and local CPU tests.
include("${CMAKE_CURRENT_LIST_DIR}/PortableText.cmake")
function(srw64_add_dialogue_cpu json_include)
    if(TARGET srw64_dialogue_cpu)
        return()
    endif()
    srw64_add_portable_text()
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_library(srw64_dialogue_cpu STATIC
        "${root}/src/host/native_dialogue_text.cpp"
        "${root}/src/host/dialogue_scene.cpp"
        "${root}/src/native/text/game_fonts.cpp")
    target_compile_features(srw64_dialogue_cpu PUBLIC cxx_std_20)
    target_include_directories(srw64_dialogue_cpu PUBLIC
        "${root}/src/host" "${root}/src/native" "${json_include}")
    target_link_libraries(srw64_dialogue_cpu PUBLIC srw64_portable_text)
    if(MSVC)
        target_compile_options(srw64_dialogue_cpu PRIVATE /utf-8)
    endif()
endfunction()
