"""RT64 changes for the Android build (docs/design/android-port.md).

prepare_rt64.py applies these after its own patches, as exact and unique text
replacements. Each one is guarded by ANDROID, __ANDROID__ or an option that is off
on the desktop (RT64_SINGLE_SOURCE_BLEND), so the desktop hosts build as before.
"""
from __future__ import annotations

PATCHES: list[tuple[str, str, str]] = [
    # Shader tools run on the build machine: DXC for the host, file_to_c built for the
    # host and imported (RT64_FILE_TO_C), as Goemon64Recomp-Android does.
    ('CMakeLists.txt', '''if (WIN32)
    set (DXC "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/x64/dxc.exe")''', '''if (ANDROID)
    if (CMAKE_HOST_APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        set (DXC "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/arm64/dxc-macos")
    elseif (CMAKE_HOST_APPLE)
        set (DXC "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/src/contrib/dxc/lib/x64" "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/x64/dxc-macos")
    elseif (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set (DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/src/contrib/dxc/lib/x64" "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/x64/dxc-linux")
    else()
        set (DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/arm64/dxc-linux")
    endif()
elseif (WIN32)
    set (DXC "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/x64/dxc.exe")'''),
    ('CMakeLists.txt', '''add_subdirectory(src/tools/file_to_c)
add_subdirectory(src/contrib/re-spirv)
add_subdirectory(src/contrib/nativefiledialog-extended)''', '''if (ANDROID)
    # Built for the build machine; there is no file dialog on Android.
    if (NOT RT64_FILE_TO_C)
        message(FATAL_ERROR "Android builds need RT64_FILE_TO_C, a file_to_c built for the build machine")
    endif()
    add_executable(file_to_c IMPORTED GLOBAL)
    set_target_properties(file_to_c PROPERTIES IMPORTED_LOCATION "${RT64_FILE_TO_C}")
else()
    add_subdirectory(src/tools/file_to_c)
endif()
add_subdirectory(src/contrib/re-spirv)
if (NOT ANDROID)
    add_subdirectory(src/contrib/nativefiledialog-extended)
endif()'''),
    ('CMakeLists.txt', '''target_link_libraries(rt64 nfd)''', '''if (NOT ANDROID)
    target_link_libraries(rt64 nfd)
endif()'''),
    # zstd's dictionary builder needs qsort_r, which bionic lacks before API 36; RT64
    # only compresses and decompresses.
    ('CMakeLists.txt', '''set(ZSTD_BUILD_SHARED OFF)''', '''set(ZSTD_BUILD_SHARED OFF)
if (ANDROID)
    set(ZSTD_BUILD_DICTBUILDER OFF)
endif()'''),
    # SDL2 comes from the build's own sdl2-compat target on Android.
    ('CMakeLists.txt', '''else()
    find_package(SDL2 REQUIRED)
endif()

message(STATUS "${SDL2_INCLUDE_DIRS} ${SDL2_LIBRARIES}")''', '''elseif (ANDROID AND TARGET SDL2::SDL2)
    set(SDL2_INCLUDE_DIRS "$<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>")
    set(SDL2_LIBRARIES SDL2::SDL2)
else()
    find_package(SDL2 REQUIRED)
endif()

message(STATUS "${SDL2_INCLUDE_DIRS} ${SDL2_LIBRARIES}")'''),
    ('src/gui/rt64_file_dialog.cpp', '''#include <nfd.h>
''', '''#ifdef __ANDROID__
// Android has no native file dialog; RT64's developer menus get empty paths.
namespace RT64 {
    std::atomic<bool> FileDialog::isOpen = false;
    void FileDialog::initialize() {}
    void FileDialog::finish() {}
    std::filesystem::path FileDialog::getDirectoryPath() { return {}; }
    std::filesystem::path FileDialog::getOpenFilename(const std::vector<FileFilter> &) { return {}; }
    std::filesystem::path FileDialog::getSaveFilename(const std::vector<FileFilter> &) { return {}; }
};
#else
#include <nfd.h>
'''),
    ('src/gui/rt64_file_dialog.cpp', '''        isOpen = false;
        return path;
    }
};''', '''        isOpen = false;
        return path;
    }
};
#endif'''),
    # The host hands RT64 its window (an ANativeWindow on Android); the title-based
    # setup that creates a window stays unimplemented, but fails at run time.
    ('src/hle/rt64_application_window.cpp', '''#   if !defined(RT64_SDL_WINDOW_VULKAN)
#      include <X11/extensions/Xrandr.h>''', '''#   if !defined(RT64_SDL_WINDOW_VULKAN) && !defined(__ANDROID__)
#      include <X11/extensions/Xrandr.h>'''),
    ('src/hle/rt64_application_window.cpp', '''#   elif defined(__ANDROID__)
        static_assert(false && "Android unimplemented");
#   elif defined(__linux__) || defined(__APPLE__)''', '''#   elif defined(__ANDROID__)
        fprintf(stderr, "RT64 cannot create its own window on Android.\\n");
        return;
#   elif defined(__linux__) || defined(__APPLE__)'''),
    ('src/hle/rt64_application_window.cpp', '''#   elif defined(__ANDROID__)
        static_assert(false && "Android unimplemented");
#   elif defined(__linux__)''', '''#   elif defined(__ANDROID__)
        return;
#   elif defined(__linux__)'''),
    ('src/hle/rt64_application_window.cpp', '''#   elif defined(RT64_SDL_WINDOW_VULKAN)
        int displayIndex = SDL_GetWindowDisplayIndex(windowHandle);''', '''#   elif defined(__ANDROID__)
        SDL_DisplayMode displayMode = {};
        if (SDL_GetCurrentDisplayMode(0, &displayMode) != 0) {
            fprintf(stderr, "SDL_GetCurrentDisplayMode failed. Error: %s.\\n", SDL_GetError());
            return;
        }

        refreshRate = displayMode.refresh_rate;
#   elif defined(RT64_SDL_WINDOW_VULKAN)
        int displayIndex = SDL_GetWindowDisplayIndex(windowHandle);'''),
    ('src/hle/rt64_application_window.cpp', '''#   elif defined(RT64_SDL_WINDOW_VULKAN)
        SDL_GetWindowPosition(windowHandle, &newWindowLeft, &newWindowTop);''', '''#   elif defined(__ANDROID__)
        newWindowLeft = 0;
        newWindowTop = 0;
#   elif defined(RT64_SDL_WINDOW_VULKAN)
        SDL_GetWindowPosition(windowHandle, &newWindowLeft, &newWindowTop);'''),
    # Android surfaces offer R8G8B8A8 but not B8G8R8A8 (Solana Seeker, Mali-G615): with
    # B8G8R8A8 Plume finds no surface format and the Mali driver crashes in resize().
    ('src/hle/rt64_application.cpp', '''        swapChainDesc.format = RenderFormat::B8G8R8A8_UNORM;''', '''#   ifdef __ANDROID__
        swapChainDesc.format = RenderFormat::R8G8B8A8_UNORM;
#   else
        swapChainDesc.format = RenderFormat::B8G8R8A8_UNORM;
#   endif'''),
    # The Video Interface pipelines draw the game into the swap chain: they must match its
    # R8G8B8A8 there, or Mali draws through an incompatible render pass (validation layer:
    # VUID-vkCmdDraw-renderPass-02684) and the picture keeps stale blocks.
    ('src/render/rt64_shader_library.cpp', '''            pipelineDesc.renderTargetFormat[0] = RenderFormat::B8G8R8A8_UNORM; // TODO: Use whatever format the swap chain was created with.''', '''#       ifdef __ANDROID__
            pipelineDesc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
#       else
            pipelineDesc.renderTargetFormat[0] = RenderFormat::B8G8R8A8_UNORM; // TODO: Use whatever format the swap chain was created with.
#       endif'''),
    # Mali exposes two queues in its one family, and Plume spreads RT64's workers over both.
    # They then run at once, and RT64's waits between them do not cover that: the opening's
    # framebuffer effects sample stale blocks (the validation layer, which serializes
    # submissions, hides it). One queue, as the desktop GPUs effectively give, draws right.
    ('src/contrib/plume/plume_vulkan.cpp', '''                queueCreateInfo.queueCount = std::min(queueFamilyProperties[i].queueCount, MaxQueuesPerFamilyCount);''', '''                queueCreateInfo.queueCount = std::min(queueFamilyProperties[i].queueCount, MaxQueuesPerFamilyCount);
#           ifdef __ANDROID__
                queueCreateInfo.queueCount = 1;
#           endif'''),
    # The opening and the title make about 70 textures a frame from the previous picture,
    # each a TMEM texture and an RGBA decode target; creating them costs the Mali driver
    # about 0.2 ms apiece, 30 ms a frame. Evicted TMEM textures go back to a pool by size
    # once the cache's lock count shows the GPU is done with them (where RT64 deletes
    # them), and new ones come from it.
    ('src/render/rt64_texture_cache.h', '''        uint32_t lockCounter;
        bool developerMode;''', '''        uint32_t lockCounter;
        bool developerMode;
#   ifdef __ANDROID__
        std::mutex texturePoolMutex;
        std::unordered_map<uint64_t, std::vector<std::unique_ptr<RenderTexture>>> texturePool;
        std::unique_ptr<RenderTexture> takePooledTexture(uint64_t key);
        void recycleTexture(Texture *texture);
#   endif'''),
    ('src/render/rt64_texture_cache.cpp', '''                    newTexture->tmem = copyWorker->device->createTexture(RenderTextureDesc::Texture1D(std::max(uint32_t(upload.bytesTMEM.size()), 1U), 1, newTexture->format));''', '''#               ifdef __ANDROID__
                    newTexture->tmem = takePooledTexture(std::max(uint64_t(upload.bytesTMEM.size()), uint64_t(1)));
                    if (newTexture->tmem == nullptr)
#               endif
                    newTexture->tmem = copyWorker->device->createTexture(RenderTextureDesc::Texture1D(std::max(uint32_t(upload.bytesTMEM.size()), 1U), 1, newTexture->format));'''),
    ('src/render/rt64_texture_cache.cpp', '''                        dstTexture->texture = directWorker->device->createTexture(RenderTextureDesc::Texture2D(upload.width, upload.height, 1, dstTexture->format, RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS));''', '''#                   ifdef __ANDROID__
                        dstTexture->texture = takePooledTexture((uint64_t(1) << 63) | (uint64_t(upload.width) << 32) | upload.height);
                        if (dstTexture->texture == nullptr)
#                   endif
                        dstTexture->texture = directWorker->device->createTexture(RenderTextureDesc::Texture2D(upload.width, upload.height, 1, dstTexture->format, RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS));'''),
    ('src/render/rt64_texture_cache.cpp', '''            // Delete evicted textures from texture map.
            for (Texture *texture : textureMap.evictedTextures) {
                delete texture;
            }''', '''            // Delete evicted textures from texture map.
            for (Texture *texture : textureMap.evictedTextures) {
#           ifdef __ANDROID__
                recycleTexture(texture);
#           endif
                delete texture;
            }'''),
    ('src/render/rt64_texture_cache.cpp', '''    void TextureCache::incrementLock() {''', '''#ifdef __ANDROID__
    // Pool keys: a TMEM texture by its byte size; a decode target (bit 63) by width and height.
    std::unique_ptr<RenderTexture> TextureCache::takePooledTexture(uint64_t key) {
        std::unique_lock lock(texturePoolMutex);
        auto it = texturePool.find(key);
        if ((it == texturePool.end()) || it->second.empty()) {
            return nullptr;
        }

        std::unique_ptr<RenderTexture> texture = std::move(it->second.back());
        it->second.pop_back();
        return texture;
    }

    void TextureCache::recycleTexture(Texture *texture) {
        // Only the cache's own TMEM textures: replacements are loaded from files.
        if ((texture == nullptr) || (texture->tmem == nullptr)) {
            return;
        }

        const size_t PoolLimitPerKey = 256;
        std::unique_lock lock(texturePoolMutex);
        auto &tmemPool = texturePool[std::max(uint64_t(texture->bytesTMEM.size()), uint64_t(1))];
        if (tmemPool.size() < PoolLimitPerKey) {
            tmemPool.emplace_back(std::move(texture->tmem));
        }

        if (texture->decodeTMEM && (texture->texture != nullptr)) {
            auto &decodePool = texturePool[(uint64_t(1) << 63) | (uint64_t(texture->width) << 32) | texture->height];
            if (decodePool.size() < PoolLimitPerKey) {
                decodePool.emplace_back(std::move(texture->texture));
            }
        }
    }
#endif

    void TextureCache::incrementLock() {'''),
    # Dual-source blending (SRC1_ALPHA) is missing on Mali GPUs, which still accept the
    # pipeline and draw white. With RT64_SINGLE_SOURCE_BLEND (on for Android) the pixel
    # shader has one output: a blended draw writes its blend factor into alpha in place
    # of the coverage, and blends with SRC_ALPHA. Colours are the same; only the stored
    # coverage of blended pixels differs.
    ('CMakeLists.txt', '''set (DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_3")''', '''set (DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_3")
option(RT64_SINGLE_SOURCE_BLEND "Blend without dual-source blending (Android: most Mali GPUs lack it)" OFF)
if (ANDROID OR RT64_SINGLE_SOURCE_BLEND)
    list(APPEND DXC_PS_OPTS "-D" "SINGLE_SOURCE_BLEND")
    add_compile_definitions(RT64_SINGLE_SOURCE_BLEND)
endif()'''),
    ('src/shaders/RasterPS.hlsl', '''    // Add highlight color to the last step.''', '''#if defined(SINGLE_SOURCE_BLEND)
    // One output carries both: a blended draw stores its blend factor, not its coverage.
    if (alphaBlend) {
        resultColor.a = resultAlpha.a;
    }
#endif

    // Add highlight color to the last step.'''),
    ('src/shaders/RasterPS.hlsl', '''    , [[vk::location(0)]] [[vk::index(1)]] out float4 pixelAlpha : SV_TARGET1''', '''#if !defined(SINGLE_SOURCE_BLEND)
    , [[vk::location(0)]] [[vk::index(1)]] out float4 pixelAlpha : SV_TARGET1
#endif'''),
    ('src/shaders/RasterPS.hlsl', '''    pixelAlpha = resultAlpha;''', '''#if !defined(SINGLE_SOURCE_BLEND)
    pixelAlpha = resultAlpha;
#endif'''),
    ('src/render/rt64_raster_shader.cpp', '''            targetBlend.srcBlend = RenderBlend::SRC1_ALPHA;
            targetBlend.dstBlend = RenderBlend::INV_SRC1_ALPHA;''', '''#       ifdef RT64_SINGLE_SOURCE_BLEND
            targetBlend.srcBlend = RenderBlend::SRC_ALPHA;
            targetBlend.dstBlend = RenderBlend::INV_SRC_ALPHA;
#       else
            targetBlend.srcBlend = RenderBlend::SRC1_ALPHA;
            targetBlend.dstBlend = RenderBlend::INV_SRC1_ALPHA;
#       endif'''),
    # Qualcomm's shader compiler (Adreno 750, driver 0x802e800a, 2026-10-08) cannot build
    # the framebuffer compute shaders: vkCreateComputePipelines fails with VK_ERROR_UNKNOWN
    # for the five that use EndianSwapUINT16 (FbCommon.hlsli), and the first framebuffer
    # read binds the missing pipeline and crashes. Goemon64Recomp-Android met the same on
    # Adreno 6xx (ogdanimal/rt64 25fa568): the driver fails on the shift-mask-OR pattern
    # and on helper calls these -O0 shaders keep. As there, the swap masks once after the OR
    # (bit for bit the same), and spirv-opt from the NDK inlines every compute shader after
    # DXC, checked with spirv-val. The shaders see RT64_ANDROID only on Android.
    ('CMakeLists.txt', '''function(build_shader_spirv_impl TARGETOBJ FILENAME TARGET_NAME OUTNAME)
    add_custom_command(OUTPUT ${OUTNAME}.spv
        COMMAND ${DXC} ${DXC_SPV_OPTS} ${ARGN} ${FILENAME} /Fo ${OUTNAME}.spv
        DEPENDS ${FILENAME})
    add_custom_command(OUTPUT ${OUTNAME}.spirv.c
        COMMAND file_to_c ${OUTNAME}.spv ${TARGET_NAME}BlobSPIRV ${OUTNAME}.spirv.c ${OUTNAME}.spirv.h
        DEPENDS ${OUTNAME}.spv file_to_c
        BYPRODUCTS ${OUTNAME}.spirv.h)''', '''if (ANDROID)
    file(GLOB RT64_SHADER_TOOLS_DIRS "${ANDROID_NDK}/shader-tools/*")
    find_program(SPIRV_OPT NAMES spirv-opt HINTS ${RT64_SHADER_TOOLS_DIRS} REQUIRED)
    find_program(SPIRV_VAL NAMES spirv-val HINTS ${RT64_SHADER_TOOLS_DIRS} REQUIRED)
    # --skip-block-layout: the shaders use -fvk-use-dx-layout on purpose.
    set(RT64_SPIRV_INLINE_PASSES --skip-block-layout --eliminate-dead-branches --merge-return --inline-entry-points-exhaustive --eliminate-dead-functions)
endif()

function(build_shader_spirv_impl TARGETOBJ FILENAME TARGET_NAME OUTNAME)
    if (ANDROID)
        add_custom_command(OUTPUT ${OUTNAME}.spv
            COMMAND ${DXC} ${DXC_SPV_OPTS} ${ARGN} -D RT64_ANDROID ${FILENAME} /Fo ${OUTNAME}.spv
            DEPENDS ${FILENAME})
    else()
    add_custom_command(OUTPUT ${OUTNAME}.spv
        COMMAND ${DXC} ${DXC_SPV_OPTS} ${ARGN} ${FILENAME} /Fo ${OUTNAME}.spv
        DEPENDS ${FILENAME})
    endif()
    set(RT64_SPIRV_BLOB ${OUTNAME}.spv)
    # RT64_SPIRV_POST_OPT comes from build_compute_shader (CMake's dynamic scope).
    if (ANDROID AND RT64_SPIRV_POST_OPT)
        add_custom_command(OUTPUT ${OUTNAME}.opt.spv
            COMMAND ${SPIRV_OPT} ${RT64_SPIRV_INLINE_PASSES} ${OUTNAME}.spv -o ${OUTNAME}.opt.spv
            COMMAND ${SPIRV_VAL} --target-env vulkan1.0 --skip-block-layout ${OUTNAME}.opt.spv
            DEPENDS ${OUTNAME}.spv)
        set(RT64_SPIRV_BLOB ${OUTNAME}.opt.spv)
    endif()
    add_custom_command(OUTPUT ${OUTNAME}.spirv.c
        COMMAND file_to_c ${RT64_SPIRV_BLOB} ${TARGET_NAME}BlobSPIRV ${OUTNAME}.spirv.c ${OUTNAME}.spirv.h
        DEPENDS ${RT64_SPIRV_BLOB} file_to_c
        BYPRODUCTS ${OUTNAME}.spirv.h)'''),
    ('CMakeLists.txt', '''function(build_compute_shader TARGETOBJ SHADERNAME)
    build_shader(${TARGETOBJ} ${SHADERNAME} "${DXC_CS_OPTS}" ${ARGN})''', '''function(build_compute_shader TARGETOBJ SHADERNAME)
    if (ANDROID)
        set(RT64_SPIRV_POST_OPT TRUE)
    endif()
    build_shader(${TARGETOBJ} ${SHADERNAME} "${DXC_CS_OPTS}" ${ARGN})'''),
    ('src/shaders/FbCommon.hlsli', '''uint EndianSwapUINT16(uint i) {
    return ((i << 8) & 0xFF00) | ((i >> 8) & 0xFF);
}''', '''uint EndianSwapUINT16(uint i) {
#ifdef RT64_ANDROID
    i &= 0xFFFFu;
    return (i << 8 | i >> 8) & 0xFFFFu;
#else
    return ((i << 8) & 0xFF00) | ((i >> 8) & 0xFF);
#endif
}'''),
    # With build_game.py --validation the Android host builds Plume without NDEBUG, which names Vulkan
    # objects and so lists VK_EXT_debug_utils as a required instance extension. Older Mali
    # drivers (Vulkan 1.3.219: Galaxy A15 with Dimensity 6100+, A35 with Exynos 1380,
    # 2026-10-08) do not offer it, and the instance is never made: "Unable to find
    # compatible graphics device" at start. On Android it is optional, and objects go
    # unnamed without it.
    ('src/contrib/plume/plume_vulkan.cpp', '''#   ifdef VULKAN_OBJECT_NAMES_ENABLED
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#   endif''', '''#   if defined(VULKAN_OBJECT_NAMES_ENABLED) && !defined(__ANDROID__)
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#   endif'''),
    ('src/contrib/plume/plume_vulkan.cpp', '''    static const std::unordered_set<std::string> OptionalInstanceExtensions = {''', '''    static const std::unordered_set<std::string> OptionalInstanceExtensions = {
#   if defined(VULKAN_OBJECT_NAMES_ENABLED) && defined(__ANDROID__)
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#   endif'''),
    ('src/contrib/plume/plume_vulkan.cpp', '''    static void setObjectName(VkDevice device, VkObjectType objectType, uint64_t object, const std::string &name) {
#   ifdef VULKAN_OBJECT_NAMES_ENABLED''', '''#   ifdef __ANDROID__
    // Whether the instance has VK_EXT_debug_utils (optional on Android).
    static bool AndroidDebugUtilsEnabled = false;
#   endif

    static void setObjectName(VkDevice device, VkObjectType objectType, uint64_t object, const std::string &name) {
#   ifdef __ANDROID__
        if (!AndroidDebugUtilsEnabled) {
            return;
        }
#   endif
#   ifdef VULKAN_OBJECT_NAMES_ENABLED'''),
    ('src/contrib/plume/plume_vulkan.cpp', '''        volkLoadInstance(instance);''', '''        volkLoadInstance(instance);
#   ifdef __ANDROID__
        AndroidDebugUtilsEnabled = supportedOptionalExtensions.count(VK_EXT_DEBUG_UTILS_EXTENSION_NAME) > 0;
#   endif'''),
    # Which shaders a pipeline the driver fails to build was made of (Adreno 660 fails
    # some graphics pipelines with VK_ERROR_UNKNOWN, 2026-10-08): each module's SPIR-V
    # size and FNV-1a hash, matched against the build's .spv files, and the pipeline's
    # specialization constants.
    ('src/contrib/plume/plume_vulkan.cpp', '''    VulkanShader::VulkanShader(VulkanDevice *device, const void *data, uint64_t size, const char *entryPointName, RenderShaderFormat format) {''', '''#   ifdef __ANDROID__
    static std::mutex AndroidShaderInfoMutex;
    static std::unordered_map<VkShaderModule, std::pair<uint64_t, uint64_t>> AndroidShaderInfo;

    static void androidLogFailedStages(const char *kind, const VkPipelineShaderStageCreateInfo *stages, uint32_t count) {
        std::lock_guard<std::mutex> lock(AndroidShaderInfoMutex);
        for (uint32_t i = 0; i < count; i++) {
            const auto it = AndroidShaderInfo.find(stages[i].module);
            const VkSpecializationInfo *spec = stages[i].pSpecializationInfo;
            fprintf(stderr, "  %s stage 0x%X entry %s spirv bytes=%llu fnv1a=%016llx spec=", kind, unsigned(stages[i].stage),
                stages[i].pName != nullptr ? stages[i].pName : "?", (unsigned long long)(it != AndroidShaderInfo.end() ? it->second.first : 0),
                (unsigned long long)(it != AndroidShaderInfo.end() ? it->second.second : 0));
            for (uint32_t j = 0; (spec != nullptr) && (j < spec->mapEntryCount); j++) {
                uint32_t value = 0;
                memcpy(&value, static_cast<const uint8_t *>(spec->pData) + spec->pMapEntries[j].offset, std::min<size_t>(sizeof(value), spec->pMapEntries[j].size));
                fprintf(stderr, "%s%u=%u", j > 0 ? "," : "", spec->pMapEntries[j].constantID, value);
            }
            fprintf(stderr, "\\n");
        }
    }
#   endif

    VulkanShader::VulkanShader(VulkanDevice *device, const void *data, uint64_t size, const char *entryPointName, RenderShaderFormat format) {'''),
    ('src/contrib/plume/plume_vulkan.cpp', '''        VkResult res = vkCreateShaderModule(device->vk, &shaderInfo, nullptr, &vk);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "vkCreateShaderModule failed with error code 0x%X.\\n", res);
            return;
        }''', '''        VkResult res = vkCreateShaderModule(device->vk, &shaderInfo, nullptr, &vk);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "vkCreateShaderModule failed with error code 0x%X.\\n", res);
            return;
        }
#   ifdef __ANDROID__
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (uint64_t i = 0; i < size; i++) {
            hash = (hash ^ static_cast<const uint8_t *>(data)[i]) * 0x100000001b3ULL;
        }
        {
            std::lock_guard<std::mutex> lock(AndroidShaderInfoMutex);
            AndroidShaderInfo[vk] = { size, hash };
        }
#   endif'''),
    ('src/contrib/plume/plume_vulkan.cpp', '''    VulkanShader::~VulkanShader() {
        if (vk != VK_NULL_HANDLE) {''', '''    VulkanShader::~VulkanShader() {
#   ifdef __ANDROID__
        if (vk != VK_NULL_HANDLE) {
            std::lock_guard<std::mutex> lock(AndroidShaderInfoMutex);
            AndroidShaderInfo.erase(vk);
        }
#   endif
        if (vk != VK_NULL_HANDLE) {'''),
    ('src/contrib/plume/plume_vulkan.cpp', '''            fprintf(stderr, "vkCreateComputePipelines failed with error code 0x%X.\\n", res);
            return;''', '''            fprintf(stderr, "vkCreateComputePipelines failed with error code 0x%X.\\n", res);
#       ifdef __ANDROID__
            androidLogFailedStages("compute", &stageInfo, 1);
#       endif
            return;'''),
    ('src/contrib/plume/plume_vulkan.cpp', '''            fprintf(stderr, "vkCreateGraphicsPipelines failed with error code 0x%X.\\n", res);
            return;''', '''            fprintf(stderr, "vkCreateGraphicsPipelines failed with error code 0x%X.\\n", res);
#       ifdef __ANDROID__
            androidLogFailedStages("graphics", stages.data(), uint32_t(stages.size()));
#       endif
            return;'''),
    # The Adreno 650's driver crashes while recording the two compute dispatches that move
    # a framebuffer between RDRAM and the GPU (Galaxy Tab S7, 2026-10-08: in vkCmdDispatch
    # under NativeTarget::copyToNative, a second after the game starts drawing; a POCO F3
    # player too). Goemon64Recomp-Android met it on the Adreno 630 (ogdanimal/rt64 40b3011)
    # and skips both, as here: skipping one moves the crash to the other. Only the 650,
    # by its exact name (user: no other GPU gets it until one is seen to need it). The
    # barriers and readback copies around them stay; what the game reads back of a frame
    # or draws into one itself is then not exact.
    ('src/render/rt64_native_target.cpp', '''#include "rt64_render_worker.h"

namespace RT64 {''', '''#include "rt64_render_worker.h"

namespace RT64 {
#ifdef __ANDROID__
    // Set at setup from the device's name (rt64_application.cpp).
    bool AndroidFramebufferSyncDisabled = false;
#endif'''),
    ('src/render/rt64_native_target.cpp', '''        worker->commandList->setComputePushConstants(0, &nativeCB);
        worker->commandList->dispatch(dispatchX, dispatchY, 1);
        worker->commandList->barriers(RenderBarrierStage::ALL, afterBarriers''', '''        worker->commandList->setComputePushConstants(0, &nativeCB);
#   ifdef __ANDROID__
        if (!AndroidFramebufferSyncDisabled)
#   endif
        worker->commandList->dispatch(dispatchX, dispatchY, 1);
        worker->commandList->barriers(RenderBarrierStage::ALL, afterBarriers'''),
    ('src/render/rt64_native_target.cpp', '''        worker->commandList->setComputeDescriptorSet(srcTarget->fbWriteDescSet->get(), 1);
        worker->commandList->dispatch(dispatchX, dispatchY, 1);''', '''        worker->commandList->setComputeDescriptorSet(srcTarget->fbWriteDescSet->get(), 1);
#   ifdef __ANDROID__
        if (!AndroidFramebufferSyncDisabled)
#   endif
        worker->commandList->dispatch(dispatchX, dispatchY, 1);'''),
    ('src/hle/rt64_application.cpp', '''        fprintf(stdout, "Driver Version: 0x%" PRIx64 "\\n", deviceDescription.driverVersion);''', '''        fprintf(stdout, "Driver Version: 0x%" PRIx64 "\\n", deviceDescription.driverVersion);
#   ifdef __ANDROID__
        extern bool AndroidFramebufferSyncDisabled;
        AndroidFramebufferSyncDisabled = deviceDescription.name == "Adreno (TM) 650";
        if (AndroidFramebufferSyncDisabled) {
            fprintf(stdout, "SRW64_FRAMEBUFFER_SYNC off (Adreno 650 driver workaround: framebuffer effects are not exact)\\n");
        }
#   endif'''),
    # A boundless range (RT64's texture set: up to 8192 textures, as many as the texture
    # cache holds) is allocated with a variable count, and the pool is sized for that count.
    # Qualcomm's Adreno drivers count the layout's full upper bound against the pool instead:
    # vkAllocateDescriptorSets fails with OUT_OF_POOL_MEMORY and the next write to the empty
    # set crashes in vkUpdateDescriptorSets (Adreno 740, 2026-10-08; Mali follows the spec).
    # The pool takes the upper bound; a write to a set that failed to allocate is skipped.
    ('src/contrib/plume/plume_vulkan.cpp', '''            const RenderDescriptorRange &lastDescriptorRange = desc.descriptorRanges[desc.descriptorRangesCount - 1];
            typeCounts[toVk(lastDescriptorRange.type)] += boundlessRangeSize;''', '''            const RenderDescriptorRange &lastDescriptorRange = desc.descriptorRanges[desc.descriptorRangesCount - 1];
#       ifdef __ANDROID__
            typeCounts[toVk(lastDescriptorRange.type)] += std::max(boundlessRangeSize, lastDescriptorRange.count);
#       else
            typeCounts[toVk(lastDescriptorRange.type)] += boundlessRangeSize;
#       endif'''),
    ('src/contrib/plume/plume_vulkan.cpp', '''        assert(descriptorIndex < setLayout->descriptorBindingIndices.size());

        const uint32_t indexBase''', '''        assert(descriptorIndex < setLayout->descriptorBindingIndices.size());
#   ifdef __ANDROID__
        if (vk == VK_NULL_HANDLE) {
            return;
        }
#   endif

        const uint32_t indexBase'''),
    # Leaving the app (Home, the Files app) destroys the activity's window, and coming back
    # brings a new one: the swap chain's surface is lost for good and every new swap chain
    # on it fails (SURFACE_LOST, then NATIVE_WINDOW_IN_USE), so the picture stays frozen.
    # The host tells Plume where the current window is (graphics.cpp, from SDL); resize()
    # makes a surface on it when the old one is lost or the window changed, and waits
    # while there is none (in the background).
    ('src/contrib/plume/plume_vulkan.cpp', '''    // VulkanSwapChain

    VulkanSwapChain::VulkanSwapChain(''', '''    // VulkanSwapChain

#ifdef __ANDROID__
    // The activity's current window, or null while it has none; set by the host, which
    // declares it itself (prepare_rt64.py patches no Plume headers).
    ANativeWindow *(*AndroidCurrentWindow)() = nullptr;
#endif

    VulkanSwapChain::VulkanSwapChain('''),
    ('src/contrib/plume/plume_vulkan.cpp', '''    bool VulkanSwapChain::resize() {
        getWindowSize(width, height);''', '''    bool VulkanSwapChain::resize() {
#   ifdef __ANDROID__
        if (AndroidCurrentWindow != nullptr) {
            ANativeWindow *window = AndroidCurrentWindow();
            if (window == nullptr) {
                return false;
            }

            VkSurfaceCapabilitiesKHR capabilities = {};
            const bool lost = (surface == VK_NULL_HANDLE) ||
                (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(commandQueue->device->physicalDevice, surface, &capabilities) == VK_ERROR_SURFACE_LOST_KHR);
            if (lost || (window != desc.renderWindow)) {
                VulkanInterface *renderInterface = commandQueue->device->renderInterface;
                releaseImageViews();
                releaseSwapChain();
                if (surface != VK_NULL_HANDLE) {
                    vkDestroySurfaceKHR(renderInterface->instance, surface, nullptr);
                    surface = VK_NULL_HANDLE;
                }

                desc.renderWindow = window;
                VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
                surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
                surfaceCreateInfo.window = window;
                VkResult surfaceRes = vkCreateAndroidSurfaceKHR(renderInterface->instance, &surfaceCreateInfo, nullptr, &surface);
                if (surfaceRes != VK_SUCCESS) {
                    fprintf(stderr, "vkCreateAndroidSurfaceKHR failed with error code 0x%X.\\n", surfaceRes);
                    surface = VK_NULL_HANDLE;
                    return false;
                }
            }
        }
#   endif
        getWindowSize(width, height);'''),
]

# prepare_rt64.py applies a file's replacements in this order, after its own.
BY_FILE: dict[str, list[tuple[str, str]]] = {}
for _relative, _old, _new in PATCHES:
    BY_FILE.setdefault(_relative, []).append((_old, _new))
