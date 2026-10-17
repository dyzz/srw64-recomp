# Shared SDL/RmlUi frontend used by the game and the optional standalone probe.
execute_process(COMMAND "${Python3_EXECUTABLE}" "${SRW64_ROOT}/tools/recomp/toolchain/prepare_frontend.py"
    COMMAND_ERROR_IS_FATAL ANY OUTPUT_QUIET)
set(FRONTEND "${SRW64_BUILD}/upstream/RecompFrontend/recompui")
set(ADAPTER "${SRW64_BUILD}/frontend-adapter")
set(BUILD_SHARED_LIBS OFF)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${FRONTEND}/lib/RmlUi" rmlui EXCLUDE_FROM_ALL)
# The host's -UNDEBUG would turn on RMLUI_DEBUG (Platform.h), and on Windows a failed
# RmlUi assert opens a modal box, then breaks into a debugger that is not there.
# Appended after the directory's -UNDEBUG, so NDEBUG wins for RmlUi alone.
target_compile_options(rmlui_core PRIVATE -DNDEBUG)
# Two layout files from the adapter instead: shrink-to-fit widths kept for one layout
# (prepare_frontend.py).
get_target_property(rmlui_sources rmlui_core SOURCES)
list(TRANSFORM rmlui_sources REPLACE "^.*/Layout/(LayoutDetails|LayoutEngine)\\.cpp$" "${ADAPTER}/rmlui/\\1.cpp")
set(rmlui_adapted ${rmlui_sources})
list(FILTER rmlui_adapted INCLUDE REGEX "frontend-adapter/rmlui/Layout")
list(LENGTH rmlui_adapted rmlui_adapted_count)
if(NOT rmlui_adapted_count EQUAL 2)
    message(FATAL_ERROR "RmlUi's layout sources moved; the adapted copies do not replace them")
endif()
set_target_properties(rmlui_core PROPERTIES SOURCES "${rmlui_sources}")
# The Plume adapter and its shaders; the GL-only host (SRW64_GL_ONLY, the PS Vita's path)
# draws RmlUi with ui_renderer_gl.cpp instead.
if(NOT SRW64_GL_ONLY)
    add_library(srw64_ui_renderer STATIC "${ADAPTER}/ui_renderer.cpp")
    target_compile_definitions(srw64_ui_renderer PRIVATE HLSL_CPU)
    target_include_directories(srw64_ui_renderer PUBLIC "${ADAPTER}")
    target_include_directories(srw64_ui_renderer PRIVATE
        "${SRW64_RT64}/src" "${SRW64_RT64}/src/render" "${SRW64_RT64}/src/rhi"
        "${SRW64_RT64}/src/contrib" "${SRW64_RT64}/src/contrib/hlslpp/include"
        "${SRW64_RT64}/src/contrib/dxc/inc"
        "${SRW64_RUNTIME}/thirdparty/concurrentqueue" "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    target_link_libraries(srw64_ui_renderer PUBLIC rt64 RmlUi::Core)
    set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
    set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
    get_directory_property(DXC DIRECTORY "${SRW64_RT64}" DEFINITION DXC)
    if(NOT DXC)
        message(FATAL_ERROR "RT64's pinned shader compiler is unavailable")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    foreach(stage VS PS)
        if(stage STREQUAL "VS")
            set(options -E VSMain -T vs_6_3 -fvk-invert-y)
        else()
            set(options -E PSMain -T ps_6_3)
        endif()
        set(shader "${FRONTEND}/shaders/Interface${stage}.hlsl")
        set(out "${CMAKE_CURRENT_BINARY_DIR}/shaders/Interface${stage}.hlsl")
        # msl and spirv helpers share the intermediate .spv command on Apple.
        if(APPLE)
            build_shader_msl_impl(srw64_ui_renderer "${shader}" Interface${stage} "${out}" ${options})
            add_custom_command(OUTPUT "${out}.spirv.c"
                COMMAND file_to_c "${out}.spv" Interface${stage}BlobSPIRV "${out}.spirv.c" "${out}.spirv.h"
                DEPENDS "${out}.spv" file_to_c BYPRODUCTS "${out}.spirv.h")
            target_sources(srw64_ui_renderer PRIVATE "${out}.spirv.c")
        else()
            build_shader_spirv_impl(srw64_ui_renderer "${shader}" Interface${stage} "${out}" ${options})
            if(WIN32)
                build_shader_dxil_impl(srw64_ui_renderer "${shader}" Interface${stage} "${out}" ${options})
            endif()
        endif()
    endforeach()
endif()
add_library(srw64_ui_pages STATIC name_page.cpp text_input.cpp
    "${FRONTEND}/lib/RmlUi/Backends/RmlUi_Platform_SDL.cpp")
target_include_directories(srw64_ui_pages PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${SRW64_ROOT}/src/host"
    "${SRW64_ROOT}/src/native" "${FRONTEND}/lib/RmlUi/Backends" "${SRW64_RT64}/src/contrib")
if(SRW64_GL_ONLY)
    target_link_libraries(srw64_ui_pages PUBLIC RmlUi::Core SDL2::SDL2)
else()
    target_link_libraries(srw64_ui_pages PUBLIC srw64_ui_renderer SDL2::SDL2)
endif()
if(SRW64_BUILD_UI_PROBE)
    add_executable(srw64-ui-probe probe.cpp probe_surface_macos.cpp)
    target_include_directories(srw64-ui-probe PRIVATE "${SRW64_RT64}/src"
        "${SRW64_RT64}/src/contrib/plume/contrib/metal-cpp")
    target_link_libraries(srw64-ui-probe PRIVATE srw64_ui_pages)
endif()
