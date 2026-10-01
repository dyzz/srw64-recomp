include_guard(GLOBAL)
# Shaders of the host-drawn HD layers (src/host/shaders), embedded for the running
# backend like the pixel compositor's: SPIR-V everywhere but Apple, MSL on Apple
# (from the same SPIR-V through RT64's converter), DXIL on Windows. Each program is
# <Name>VS.hlsl + <Name>PS.hlsl with VSMain / PSMain; blobs are <Name><Stage>Blob<Format>.
function(srw64_add_native_gpu_shaders target rt64_root)
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(source "${root}/src/host/shaders")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/native-gpu-shaders")
    file(MAKE_DIRECTORY "${output}")
    target_include_directories(${target} PRIVATE "${output}")
    # DXC runs on the build machine, which differs from the target for Android.
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(arch arm64)
    else()
        set(arch x64)
    endif()
    if(CMAKE_HOST_WIN32)
        set(dxc "${rt64_root}/src/contrib/dxc/bin/x64/dxc.exe")
    elseif(CMAKE_HOST_APPLE)
        set(dxc "${CMAKE_COMMAND}" -E env "DYLD_LIBRARY_PATH=${rt64_root}/src/contrib/dxc/lib/${arch}" "${rt64_root}/src/contrib/dxc/bin/${arch}/dxc-macos")
    else()
        set(dxc "${CMAKE_COMMAND}" -E env "LD_LIBRARY_PATH=${rt64_root}/src/contrib/dxc/lib/${arch}" "${rt64_root}/src/contrib/dxc/bin/${arch}/dxc-linux")
    endif()
    file(GLOB shaders CONFIGURE_DEPENDS "${source}/*VS.hlsl" "${source}/*PS.hlsl")
    file(GLOB includes CONFIGURE_DEPENDS "${source}/*.hlsli")
    foreach(shader ${shaders})
        get_filename_component(name "${shader}" NAME_WE)
        # D3D clip space in the source; -fvk-invert-y for Vulkan, and RT64's MSL
        # converter flips it back for Metal (flip_vert_y).
        if(name MATCHES "VS$")
            set(entry VSMain)
            set(profile vs_6_0)
            set(extra -fvk-invert-y)
        else()
            set(entry PSMain)
            set(profile ps_6_0)
            set(extra)
        endif()
        set(out "${output}/${name}.hlsl")
        add_custom_command(OUTPUT "${out}.spv"
            COMMAND ${dxc} -spirv -fspv-target-env=vulkan1.0 -fvk-use-dx-layout ${extra} -I "${source}"
                    -E ${entry} -T ${profile} "${shader}" -Fo "${out}.spv"
            DEPENDS "${shader}" ${includes} VERBATIM)
        # SPIR-V everywhere (Vulkan, and MoltenVK tests on a Mac), plus MSL or DXIL.
        add_custom_command(OUTPUT "${out}.spirv.c" "${out}.spirv.h"
            COMMAND file_to_c "${out}.spv" ${name}BlobSPIRV "${out}.spirv.c" "${out}.spirv.h"
            DEPENDS "${out}.spv" file_to_c VERBATIM)
        target_sources(${target} PRIVATE "${out}.spirv.c")
        if(APPLE)
            add_custom_command(OUTPUT "${out}.metal" COMMAND spirv_cross_msl "${out}.spv" "${out}.metal" DEPENDS "${out}.spv" spirv_cross_msl VERBATIM)
            if(SRW64_METAL_SOURCE_SHADERS)
                set(metal_input "${out}.metal")
            else()
                add_custom_command(OUTPUT "${out}.air" COMMAND xcrun -sdk macosx metal -c "${out}.metal" -o "${out}.air" DEPENDS "${out}.metal" VERBATIM)
                add_custom_command(OUTPUT "${out}.metallib" COMMAND xcrun -sdk macosx metallib "${out}.air" -o "${out}.metallib" DEPENDS "${out}.air" VERBATIM)
                set(metal_input "${out}.metallib")
            endif()
            add_custom_command(OUTPUT "${out}.metal.c" "${out}.metal.h"
                COMMAND file_to_c "${metal_input}" ${name}BlobMSL "${out}.metal.c" "${out}.metal.h"
                DEPENDS "${metal_input}" file_to_c VERBATIM)
            target_sources(${target} PRIVATE "${out}.metal.c")
        elseif(WIN32)
            add_custom_command(OUTPUT "${out}.dxil" COMMAND ${dxc} -I "${source}" -E ${entry} -T ${profile} "${shader}" -Fo "${out}.dxil"
                DEPENDS "${shader}" ${includes} VERBATIM)
            add_custom_command(OUTPUT "${out}.dxil.c" "${out}.dxil.h"
                COMMAND file_to_c "${out}.dxil" ${name}BlobDXIL "${out}.dxil.c" "${out}.dxil.h"
                DEPENDS "${out}.dxil" file_to_c VERBATIM)
            target_sources(${target} PRIVATE "${out}.dxil.c")
        endif()
    endforeach()
endfunction()
