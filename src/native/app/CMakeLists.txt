add_library(srw64_app STATIC runtime.cpp desktop.cpp)
target_compile_features(srw64_app PUBLIC cxx_std_20)
target_include_directories(srw64_app PUBLIC "${CMAKE_CURRENT_LIST_DIR}/..")
if(MSVC)
    target_compile_options(srw64_app PRIVATE /W4 /utf-8)
else()
    target_compile_options(srw64_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Keep Cocoa in a separate adapter; the controller/session layer stays portable.
if(APPLE)
    enable_language(OBJCXX)
    add_library(srw64_desktop_macos STATIC "${CMAKE_CURRENT_LIST_DIR}/../../host/macos/desktop_macos.mm")
    set_target_properties(srw64_desktop_macos PROPERTIES
        OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES OBJCXX_EXTENSIONS NO)
    target_compile_options(srw64_desktop_macos PRIVATE -fobjc-arc)
    target_link_libraries(srw64_desktop_macos PUBLIC srw64_app PRIVATE "-framework Cocoa")
endif()

option(SRW64_EMBED_IMPORT_SPEC "Embed ROM-free importer metadata (Python is BUILD-TIME only)" ON)
# Reuse the pinned RT64 JSON header without linking its renderer.
function(srw64_add_launch json_include)
    if(TARGET srw64_launch)
        return()
    endif()
    if(NOT EXISTS "${json_include}/json/json.hpp")
        message(FATAL_ERROR "A directory containing json/json.hpp is required for the standalone bootstrap")
    endif()
    add_library(srw64_launch STATIC "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/launch.cpp"
                                   "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/rom_import.cpp")
    target_include_directories(srw64_launch PRIVATE "${json_include}")
    target_link_libraries(srw64_launch PUBLIC srw64_app)
    if(TARGET srw64_desktop_macos)
        target_link_libraries(srw64_launch PRIVATE srw64_desktop_macos)
    endif()
    if(SRW64_EMBED_IMPORT_SPEC)
        find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)
        get_filename_component(import_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../.." ABSOLUTE)
        set(import_build "${CMAKE_CURRENT_BINARY_DIR}/import-metadata")
        file(GLOB_RECURSE import_inputs CONFIGURE_DEPENDS
             "${import_root}/config/*.json" "${import_root}/content/locales/*.json"
             "${import_root}/reference/*.csv" "${import_root}/src/srw64_rom/*.py"
             "${import_root}/src/srw64_native/*.py")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${import_inputs}
                     "${import_root}/tools/release/build_import_spec.py")
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${import_root}/tools/release/build_import_spec.py"
                        --root "${import_root}" --output "${import_build}/import-spec.json"
                        --header "${import_build}/srw64_import_spec.hpp"
                        COMMAND_ERROR_IS_FATAL ANY)
        target_include_directories(srw64_launch PRIVATE "${import_build}")
        target_compile_definitions(srw64_launch PRIVATE SRW64_EMBEDDED_IMPORT_SPEC)
    endif()
    if(MSVC)
        target_compile_options(srw64_launch PRIVATE /W4 /utf-8)
    else()
        target_compile_options(srw64_launch PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
