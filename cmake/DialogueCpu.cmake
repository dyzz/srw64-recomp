# Shared by the playable host and ROM/GPU-free text tests. Never link the
# compositor into this target: adding Metal here defeats the backend boundary.
function(srw64_add_dialogue_cpu json_include)
    if(TARGET srw64_dialogue_cpu)
        return()
    endif()
    if(NOT APPLE)
        message(FATAL_ERROR "The current CPU raster backend uses Core Text; portable replacement is still pending")
    endif()
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_library(srw64_dialogue_cpu STATIC
        "${root}/src/host/native_dialogue_text.cpp"
        "${root}/src/host/macos/dialogue_coretext.cpp")
    target_compile_features(srw64_dialogue_cpu PUBLIC cxx_std_20)
    target_include_directories(srw64_dialogue_cpu PUBLIC
        "${root}/src/host" "${root}/src/native" "${json_include}")
    target_link_libraries(srw64_dialogue_cpu PRIVATE
        "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation")
endfunction()
