#!/usr/bin/env python3
"""Apply narrow, checked RT64 adaptations for the local graphics experiment.

The Command Line Tools install has no offline Metal compiler. RT64's Plume
backend already uses the Metal runtime source compiler for internal shaders.
This opt-in path also embeds generated MSL for its prebuilt shader collection.
All alterations are confined to ignored source clones, with before/after hashes.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # tools/, home of the recomp package
from recomp.toolchain.analyze_layout import ROOT
from recomp.toolchain.native_model_hook_patches import PATCHES as NATIVE_MODEL_PATCHES


def patch(checkout: Path, relative: str, old: str | None = None, new: str | None = None, additional=()) -> dict:
    original = subprocess.check_output(["git", "show", f"HEAD:{relative}"], cwd=checkout).decode()
    if old is not None and original.count(old) != 1:
        raise RuntimeError(f"patch context differs in {relative}")
    steps = [*([(old, new)] if old is not None else []), *NATIVE_MODEL_PATCHES.get(relative, []), *additional]
    expected = original
    # A checkout patched by an earlier revision of these lists holds some of the
    # patches, in order: accept any such subset, nothing else.
    states = {original}
    for before, after in steps:
        if expected.count(before) != 1:
            raise RuntimeError(f'additional graphics patch context differs in {relative}')
        expected = expected.replace(before, after)
        states |= {state.replace(before, after) for state in states if state.count(before) == 1}
    path = checkout / relative
    if path.read_text() not in states:
        raise RuntimeError(f"unexpected local changes in {path}")
    if path.read_text() != expected:
        path.write_text(expected)
    return {"path": str(path.relative_to(ROOT)), "before_sha256": hashlib.sha256(original.encode()).hexdigest(),
            "after_sha256": hashlib.sha256(expected.encode()).hexdigest()}


def main() -> int:
    checkout = ROOT / "build/recomp/upstream/RT64"
    lock = json.loads((ROOT / "config/recomp/toolchain.json").read_text())
    for name, source in lock["sources"].items():
        if source.get("component") != "graphics":
            continue
        directory = ROOT / "build/recomp/upstream" / name
        if not directory.exists():
            subprocess.run(["git", "clone", "--filter=blob:none", "--no-checkout", source["url"], str(directory)], check=True)
            subprocess.run(["git", "checkout", "--detach", source["commit"]], cwd=directory, check=True)
        actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=directory, text=True).strip()
        if actual != source["commit"]:
            raise RuntimeError(f"{name} differs from graphics source lock")
        if source["recursive"]:
            subprocess.run(["git", "-c", "http.lowSpeedLimit=1000", "-c", "http.lowSpeedTime=30",
                            "submodule", "update", "--init", "--recursive", "--depth", "1", "--jobs", "4"], cwd=directory, check=True)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=checkout, text=True).strip()
    if revision != lock["sources"]["RT64"]["commit"]:
        raise RuntimeError("RT64 revision differs")
    plume = checkout / "src/contrib/plume"
    plume_revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=plume, text=True).strip()
    pinned_plume = subprocess.check_output(["git", "rev-parse", "HEAD:src/contrib/plume"], cwd=checkout, text=True).strip()
    if plume_revision != pinned_plume:
        raise RuntimeError("Plume revision differs from RT64 submodule")
    for repository, allowed in ((checkout, {"CMakeLists.txt", "src/contrib/plume",
                                           "src/tools/spirv_cross_msl/CMakeLists.txt",
                                           "src/hle/rt64_present_queue.cpp", "src/rhi/rt64_render_hooks.h",
                                           "src/rhi/rt64_render_hooks.cpp", "src/gui/rt64_file_dialog.cpp",
                                           "src/hle/rt64_framebuffer_manager.cpp", "src/hle/rt64_workload_queue.cpp"}
                                           | set(NATIVE_MODEL_PATCHES)),
                                (plume, {"plume_metal.cpp", "plume_vulkan.cpp", "plume_apple.h", "plume_apple.mm"})):
        changed = set(subprocess.check_output(["git", "diff", "--name-only", "HEAD"], cwd=repository, text=True).splitlines())
        if changed - allowed:
            raise RuntimeError(f"unrelated local changes in graphics dependency: {changed - allowed}")
    old_cmake = '''    add_custom_command(OUTPUT ${OUTNAME}.ir
            COMMAND xcrun -sdk macosx metal -o ${OUTNAME}.ir -c ${OUTNAME}.metal $<$<CONFIG:Debug>:-frecord-sources>
            DEPENDS ${OUTNAME}.metal)
    add_custom_command(OUTPUT ${OUTNAME}.metallib
            COMMAND xcrun -sdk macosx metallib ${OUTNAME}.ir -o ${OUTNAME}.metallib
            DEPENDS ${OUTNAME}.ir)
    add_custom_command(OUTPUT ${OUTNAME}.metal.c
            COMMAND file_to_c ${OUTNAME}.metallib ${TARGET_NAME}BlobMSL ${OUTNAME}.metal.c ${OUTNAME}.metal.h
            DEPENDS ${OUTNAME}.metallib file_to_c
            BYPRODUCTS ${OUTNAME}.metal.h)'''
    new_cmake = '''    if(SRW64_METAL_SOURCE_SHADERS)
        add_custom_command(OUTPUT ${OUTNAME}.metal.c
                COMMAND file_to_c ${OUTNAME}.metal ${TARGET_NAME}BlobMSL ${OUTNAME}.metal.c ${OUTNAME}.metal.h
                DEPENDS ${OUTNAME}.metal file_to_c
                BYPRODUCTS ${OUTNAME}.metal.h)
    else()
''' + old_cmake + '''
    endif()'''
    old_library = '''        const dispatch_data_t dispatchData = dispatch_data_create(data, size, dispatch_get_main_queue(), ^{});
        library = device->mtl->newLibrary(dispatchData, &error);'''
    new_library = '''#if defined(SRW64_METAL_SOURCE_SHADERS)
        if (size < 4 || memcmp(data, "MTLB", 4) != 0) {
            const std::string source(static_cast<const char *>(data), size);
            library = device->mtl->newLibrary(NS::String::string(source.c_str(), NS::UTF8StringEncoding), nullptr, &error);
        }
        else
#endif
        {
            const dispatch_data_t dispatchData = dispatch_data_create(data, size, dispatch_get_main_queue(), ^{});
            library = device->mtl->newLibrary(dispatchData, &error);
            dispatch_release(dispatchData);
        }'''
    old_resize = '''            layer->setDrawableSize(drawableSize);

            for (uint32_t i = 0; i < MAX_DRAWABLES; i++) {
                MetalDrawable &drawable = drawables[i];
                drawable.desc.width = width;
                drawable.desc.height = height;
            }
        }

        return true;
    }

    bool MetalSwapChain::needsResize() const'''
    new_resize = '''            layer->setDrawableSize(drawableSize);
        }

        // SDL may resize the CAMetalLayer before this render-thread callback.
        // Refresh descriptors even when its drawableSize is already correct;
        // new framebuffers and native overlays must use the same pixel extent.
        for (uint32_t i = 0; i < MAX_DRAWABLES; i++) {
            MetalDrawable &drawable = drawables[i];
            drawable.desc.width = width;
            drawable.desc.height = height;
        }

        return true;
    }

    bool MetalSwapChain::needsResize() const'''
    old_copy = """            vkCmdCopyBufferToImage(vk, srcBuffer->vk, dstTexture->vk, toImageLayout(dstTexture->textureLayout), 1, &imageCopy);
        }
        else {"""
    new_copy = """            vkCmdCopyBufferToImage(vk, srcBuffer->vk, dstTexture->vk, toImageLayout(dstTexture->textureLayout), 1, &imageCopy);
        }
        else if ((dstLocation.type == RenderTextureCopyType::PLACED_FOOTPRINT) && (srcLocation.type == RenderTextureCopyType::SUBRESOURCE)) {
            assert(dstBuffer != nullptr);
            assert(srcTexture != nullptr);

            const uint32_t blockWidth = RenderFormatBlockWidth(srcTexture->desc.format);
            VkBufferImageCopy imageCopy = {};
            imageCopy.bufferOffset = dstLocation.placedFootprint.offset;
            imageCopy.bufferRowLength = ((dstLocation.placedFootprint.rowWidth + blockWidth - 1) / blockWidth) * blockWidth;
            imageCopy.bufferImageHeight = ((dstLocation.placedFootprint.height + blockWidth - 1) / blockWidth) * blockWidth;
            imageCopy.imageSubresource.aspectMask = toAspectFlags(srcTexture->desc.format, srcTexture->desc.flags);
            imageCopy.imageSubresource.baseArrayLayer = srcLocation.subresource.arrayIndex;
            imageCopy.imageSubresource.layerCount = 1;
            imageCopy.imageSubresource.mipLevel = srcLocation.subresource.mipLevel;
            imageCopy.imageExtent.width = dstLocation.placedFootprint.width;
            imageCopy.imageExtent.height = dstLocation.placedFootprint.height;
            imageCopy.imageExtent.depth = dstLocation.placedFootprint.depth;
            vkCmdCopyImageToBuffer(vk, srcTexture->vk, toImageLayout(srcTexture->textureLayout), dstBuffer->vk, 1, &imageCopy);
        }
        else {"""
    # CocoaWindow queues blocks on the main queue from the present thread (every
    # needsResize()), and they capture `this`. Application::end() frees the swapchain
    # and its CocoaWindow on the graphics thread while the main thread is joining it,
    # so a block queued after the main thread's last event pump runs in SDL_Quit's
    # run loop (Cocoa_VideoQuit) against freed memory: SIGSEGV at exit on macOS.
    # The blocks now share a lifetime record and return once the window is gone.
    apple_include = ("#include <atomic>\n#include <mutex>\n",
                     "#include <atomic>\n#include <memory>\n#include <mutex>\n")
    apple_class = ("    class CocoaWindow {\n"
                   "        void* windowHandle;\n"
                   "        CocoaWindowAttributes cachedAttributes;\n"
                   "        std::atomic<int> cachedRefreshRate;\n"
                   "        mutable std::mutex attributesMutex;\n",
                   "    // Blocks queued on the main queue can outlive the CocoaWindow that queued them.\n"
                   "    struct CocoaWindowLifetime {\n"
                   "        std::mutex mutex;\n"
                   "        bool alive = true;\n"
                   "    };\n\n"
                   "    class CocoaWindow {\n"
                   "        void* windowHandle;\n"
                   "        CocoaWindowAttributes cachedAttributes;\n"
                   "        std::atomic<int> cachedRefreshRate;\n"
                   "        mutable std::mutex attributesMutex;\n"
                   "        std::shared_ptr<CocoaWindowLifetime> lifetime = std::make_shared<CocoaWindowLifetime>();\n")
    def lifetime_check(indent: str) -> str:
        return (f"{indent}std::lock_guard<std::mutex> lifetimeLock(windowLifetime->mutex);\n"
                f"{indent}if (!windowLifetime->alive) {{\n{indent}    return;\n{indent}}}\n\n")
    apple_destructor = ("    CocoaWindow::~CocoaWindow() {}",
                        "    CocoaWindow::~CocoaWindow() {\n"
                        "        std::lock_guard<std::mutex> lock(lifetime->mutex);\n"
                        "        lifetime->alive = false;\n"
                        "    }")
    apple_attributes = ("        auto updateBlock = ^{\n"
                        "            NSWindow *nsWindow = (__bridge NSWindow *)windowHandle;\n"
                        "            NSRect contentFrame",
                        "        std::shared_ptr<CocoaWindowLifetime> windowLifetime = lifetime;\n"
                        "        auto updateBlock = ^{\n" + lifetime_check(" " * 12) +
                        "            NSWindow *nsWindow = (__bridge NSWindow *)windowHandle;\n"
                        "            NSRect contentFrame")
    apple_refresh = ("        auto updateBlock = ^{\n"
                     "            NSWindow *nsWindow = (__bridge NSWindow *)windowHandle;\n"
                     "            NSScreen *screen",
                     "        std::shared_ptr<CocoaWindowLifetime> windowLifetime = lifetime;\n"
                     "        auto updateBlock = ^{\n" + lifetime_check(" " * 12) +
                     "            NSWindow *nsWindow = (__bridge NSWindow *)windowHandle;\n"
                     "            NSScreen *screen")
    apple_fullscreen = ("            dispatch_async(dispatch_get_main_queue(), ^{\n"
                        "                NSWindow *nsWindow",
                        "            std::shared_ptr<CocoaWindowLifetime> windowLifetime = lifetime;\n"
                        "            dispatch_async(dispatch_get_main_queue(), ^{\n" + lifetime_check(" " * 16) +
                        "                NSWindow *nsWindow")
    records = [patch(checkout, "CMakeLists.txt", old_cmake, new_cmake),
               patch(checkout, "src/tools/spirv_cross_msl/CMakeLists.txt",
                     "set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/build)",
                     "set(CMAKE_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})"),
               patch(plume, "plume_metal.cpp", old_library, new_library, [(old_resize, new_resize)]),
               patch(plume, "plume_apple.h", *apple_include, [apple_class]),
               patch(plume, "plume_apple.mm", *apple_destructor, [apple_attributes, apple_refresh, apple_fullscreen]),
               # Screenshots on Vulkan: read the swapchain image back into a buffer.
               patch(plume, "plume_vulkan.cpp",
                     "        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;",
                     "        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;",
                     [(old_copy, new_copy)])]
    # Match a native text snapshot to the workload actually being presented.
    # The draw callback must never read live guest RDRAM or the latest CPU frame.
    # After the present queue's own fence wait, report that the command list the
    # draw hook recorded has finished on the GPU. Metal hosts use completion
    # handlers instead; Vulkan and D3D12 have no per-list callback in Plume.
    presented_declaration = ("    void SetRenderHooks(RenderHookInit *init, RenderHookDraw *draw, RenderHookDeinit *deinit);",
                             "    void SetRenderHooks(RenderHookInit *init, RenderHookDraw *draw, RenderHookDeinit *deinit);\n"
                             "    using RenderHookPresented = void(unsigned long long workloadId);\n"
                             "    void SetRenderHookPresented(RenderHookPresented *presented);\n"
                             "    RenderHookPresented *GetRenderHookPresented();")
    # The swapchain texture behind the draw hook's framebuffer, for screenshots: Plume
    # framebuffers expose no texture to copy from.
    swapchain_declaration = ("    RenderHookPresented *GetRenderHookPresented();",
                             "    RenderHookPresented *GetRenderHookPresented();\n"
                             "    void SetRenderHookSwapChainTexture(RenderTexture *texture);\n"
                             "    RenderTexture *GetRenderHookSwapChainTexture();")
    swapchain_definition = ("    RenderHookPresented *GetRenderHookPresented() { return presentedHook; }",
                            "    RenderHookPresented *GetRenderHookPresented() { return presentedHook; }\n"
                            "    static thread_local RenderTexture *hookSwapChainTexture = nullptr;\n"
                            "    void SetRenderHookSwapChainTexture(RenderTexture *texture) { hookSwapChainTexture = texture; }\n"
                            "    RenderTexture *GetRenderHookSwapChainTexture() { return hookSwapChainTexture; }")
    swapchain_call = ("                    SetRenderHookWorkloadId(present.workloadId);\n",
                      "                    SetRenderHookWorkloadId(present.workloadId);\n"
                      "                    SetRenderHookSwapChainTexture(swapChainTexture);\n")
    presented_definition = ("    static RenderHookDeinit *deinit = nullptr;",
                            "    static RenderHookDeinit *deinit = nullptr;\n"
                            "    static RenderHookPresented *presentedHook = nullptr;\n"
                            "    void SetRenderHookPresented(RenderHookPresented *presented) { presentedHook = presented; }\n"
                            "    RenderHookPresented *GetRenderHookPresented() { return presentedHook; }")
    presented_call = ("                    ext.presentGraphicsWorker->commandQueue->executeCommandLists(&commandList, 1, &waitSemaphore, 1, &signalSemaphore, 1, ext.presentGraphicsWorker->commandFence.get());\n"
                      "                    ext.presentGraphicsWorker->wait();\n",
                      "                    ext.presentGraphicsWorker->commandQueue->executeCommandLists(&commandList, 1, &waitSemaphore, 1, &signalSemaphore, 1, ext.presentGraphicsWorker->commandFence.get());\n"
                      "                    ext.presentGraphicsWorker->wait();\n"
                      "                    if (RenderHookPresented *presentedHook = GetRenderHookPresented()) {\n"
                      "                        presentedHook(present.workloadId);\n"
                      "                    }\n")
    records += [patch(checkout, "src/rhi/rt64_render_hooks.h",
                      "    RenderHookInit *GetRenderHookInit();",
                      "    void SetRenderHookWorkloadId(unsigned long long id);\n"
                      "    unsigned long long GetRenderHookWorkloadId();\n"
                      "    RenderHookInit *GetRenderHookInit();", [presented_declaration, swapchain_declaration]),
                patch(checkout, "src/rhi/rt64_render_hooks.cpp",
                      "    static RenderHookInit *init = nullptr;",
                      "    static thread_local unsigned long long hookWorkloadId = 0;\n"
                      "    void SetRenderHookWorkloadId(unsigned long long id) { hookWorkloadId = id; }\n"
                      "    unsigned long long GetRenderHookWorkloadId() { return hookWorkloadId; }\n"
                      "    static RenderHookInit *init = nullptr;", [presented_definition, swapchain_definition]),
                patch(checkout, "src/hle/rt64_present_queue.cpp",
                      "                    drawHook(commandList, swapChainFramebuffer);",
                      "                    SetRenderHookWorkloadId(present.workloadId);\n"
                      "                    drawHook(commandList, swapChainFramebuffer);", [presented_call, swapchain_call])]
    # RT64 quits the file-dialog library even when it failed to start (no D-Bus session
    # bus, as in containers): the xdg-portal build then aborts in dbus_connection_unref.
    records.append(patch(checkout, "src/gui/rt64_file_dialog.cpp",
                         "    void FileDialog::initialize() {\n        NFD_Init();\n    }\n\n    void FileDialog::finish() {\n        NFD_Quit();\n    }",
                         "    static bool nfdReady = false;\n\n"
                         "    void FileDialog::initialize() {\n        nfdReady = (NFD_Init() == NFD_OKAY);\n    }\n\n"
                         "    void FileDialog::finish() {\n        if (nfdReady) {\n            NFD_Quit();\n        }\n\n        nfdReady = false;\n    }"))
    # Framebuffer copies drawn back by texture rectangles at a fractional scale (any picture
    # wider than 4:3; docs/design/deck-16x10.md). The game's afterimage (80083744) draws the
    # last frame back every frame in 64 x 16 rectangles, each loaded as 68 x 17 texels. RT64
    # samples a copy at its own scale (its rounded width over its 68 texels, 4.794 at 16:10)
    # but the rectangle's 64 texels cover 307 or 308 pixels (both edges rounded): up to a
    # pixel of drift per tile, fed back each frame, smears whole columns (the BANPRESTO logo's
    # centre). 1) A copy spans the pixels a rectangle over the same tile covers: both edges
    # rounded, not the left edge and the width separately.
    records.append(patch(checkout, "src/hle/rt64_framebuffer_manager.cpp",
                         "        const uint32_t tileWidth = std::clamp<long>(lround((fbTile.right - fbTile.left) * resolutionScale.x), 1L, RenderTarget::MaxDimension);\n"
                         "        const uint32_t tileHeight = std::clamp<long>(lround((fbTile.bottom - fbTile.top) * resolutionScale.y), 1L, RenderTarget::MaxDimension);\n",
                         "        const long scaledLeft = lround(fbTile.left * resolutionScale.x);\n"
                         "        const long scaledTop = lround(fbTile.top * resolutionScale.y);\n"
                         "        const uint32_t tileWidth = std::clamp<long>(lround(fbTile.right * resolutionScale.x) - scaledLeft, 1L, RenderTarget::MaxDimension);\n"
                         "        const uint32_t tileHeight = std::clamp<long>(lround(fbTile.bottom * resolutionScale.y) - scaledTop, 1L, RenderTarget::MaxDimension);\n",
                         [("        tileCopy.left = std::clamp<long>(lround(fbTile.left * resolutionScale.x), 0, RenderTarget::MaxDimension);\n"
                           "        tileCopy.top = std::clamp<long>(lround(fbTile.top * resolutionScale.y), 0, RenderTarget::MaxDimension);\n",
                           "        tileCopy.left = std::clamp<long>(scaledLeft, 0, RenderTarget::MaxDimension);\n"
                           "        tileCopy.top = std::clamp<long>(scaledTop, 0, RenderTarget::MaxDimension);\n")]))
    # 2) A 1:1 texture rectangle stretched across the picture samples its copies at its own
    # scale, the pixels it covers over its texels, so a copy drawn back where it was taken
    # lands pixel for pixel, as at 4:3. Collected per framebuffer pair (each has its scale),
    # applied to the GPU tiles before they upload.
    rect_scale_collect = '''                    framebufferRenderer->addFramebuffer(drawParams);

                    for (uint32_t j = 0; j < fbPair.projectionCount; j++) {
                        const Projection &proj = fbPair.projections[j];
                        if (proj.type != Projection::Type::Rectangle) {
                            continue;
                        }

                        for (uint32_t d = 0; d < proj.gameCallCount; d++) {
                            const DrawCall &call = proj.gameCalls[d].callDesc;
                            if (call.rect.isNull() || !call.identityRectScale() || (call.rectAspect == G_EX_ASPECT_ADJUST) ||
                                (call.rectLeftOrigin != G_EX_ORIGIN_NONE) || (call.rectRightOrigin != G_EX_ORIGIN_NONE))
                            {
                                continue;
                            }

                            // The same rounding as convertViewportRect.
                            const int32_t left = call.rect.left(true), right = call.rect.right(true);
                            const int32_t top = call.rect.top(true), bottom = call.rect.bottom(true);
                            if ((right <= left) || (bottom <= top)) {
                                continue;
                            }

                            const float origin = float(nativeColorWidth) / 2;
                            const float scaleX = fixedResScale[0], scaleY = fixedResScale[1];
                            const float pixelsX = std::round((origin + (left - origin)) * scaleX), pixelsRight = std::round((origin + (right - origin)) * scaleX);
                            const float pixelsY = std::round(top * scaleY), pixelsBottom = std::round(bottom * scaleY);
                            for (uint32_t t = 0; t < call.tileCount; t++) {
                                if (workload.drawData.callTiles[call.tileIndex + t].tileCopyUsed) {
                                    copyRectScales.push_back({ call.tileIndex + t, (pixelsRight - pixelsX) / float(right - left), (pixelsBottom - pixelsY) / float(bottom - top) });
                                }
                            }
                        }
                    }'''
    rect_scale_apply = '''                    workload.drawData.gpuTiles.data(), &fbManager, ext.textureCache, workload.submissionFrame);

                for (const CopyRectScale &rectScale : copyRectScales) {
                    interop::GPUTile &gpuTile = workload.drawData.gpuTiles[rectScale.tile];
                    const auto it = fbManager.tileCopies.find(workload.drawData.callTiles[rectScale.tile].tmemHashOrID);

                    // Only a copy at the picture's scale, not one reinterpreted to another texel size.
                    if (!gpuTile.flags.fromCopy || (it == fbManager.tileCopies.end()) ||
                        (std::abs(float(gpuTile.tcScale.x) / rectScale.x - 1.0f) > 0.05f) || (std::abs(float(gpuTile.tcScale.y) / rectScale.y - 1.0f) > 0.05f))
                    {
                        continue;
                    }

                    gpuTile.tcScale.x = rectScale.x;
                    gpuTile.tcScale.y = rectScale.y;
                    gpuTile.ulScale.x = it->second.ulScaleS ? rectScale.x : 1.0f;
                    gpuTile.ulScale.y = it->second.ulScaleT ? rectScale.y : 1.0f;
                }'''
    records.append(patch(checkout, "src/hle/rt64_workload_queue.cpp",
                         "            scratchFbChangePool.reset();\n",
                         "            struct CopyRectScale {\n"
                         "                uint32_t tile;\n"
                         "                float x, y;\n"
                         "            };\n\n"
                         "            thread_local std::vector<CopyRectScale> copyRectScales;\n"
                         "            copyRectScales.clear();\n"
                         "            scratchFbChangePool.reset();\n",
                         [("                    framebufferRenderer->addFramebuffer(drawParams);", rect_scale_collect),
                          ("                    workload.drawData.gpuTiles.data(), &fbManager, ext.textureCache, workload.submissionFrame);",
                           rect_scale_apply),
                          ('#include "rt64_workload_queue.h"\n',
                           '#include "rt64_workload_queue.h"\n\n#include <cmath>\n\n#include "../include/rt64_extended_gbi.h"\n')]))
    recorded = {r['path'] for r in records}
    for relative in NATIVE_MODEL_PATCHES:
        if str((checkout/relative).relative_to(ROOT)) not in recorded:
            records.append(patch(checkout, relative))
    report = {"schema": "srw64.recomp-graphics-source-patches.v1", "rt64_commit": revision,
              "plume_commit": plume_revision, "patches": records,
              "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "native_model_hooks_sha256": hashlib.sha256((ROOT/'tools/recomp/toolchain/native_model_hook_patches.py').read_bytes()).hexdigest(),
              "purpose": "Metal source compilation, resize descriptor synchronization, main-queue window blocks that outlive their swapchain, workload-matched UI hooks, and opt-in native model callbacks preserving scene transforms and draw order"}
    (ROOT / "build/recomp/graphics-source-patches.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
