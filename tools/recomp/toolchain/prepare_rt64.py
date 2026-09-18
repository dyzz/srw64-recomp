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
    expected = original.replace(old, new) if old is not None else original
    previous = expected
    for before, after in [*NATIVE_MODEL_PATCHES.get(relative, []), *additional]:
        if expected.count(before) != 1:
            raise RuntimeError(f'additional graphics patch context differs in {relative}')
        expected = expected.replace(before, after)
    path = checkout / relative
    if path.read_text() not in (original, previous, expected):
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
                                           "src/rhi/rt64_render_hooks.cpp"} | set(NATIVE_MODEL_PATCHES)),
                                (plume, {"plume_metal.cpp"})):
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
    records = [patch(checkout, "CMakeLists.txt", old_cmake, new_cmake),
               patch(checkout, "src/tools/spirv_cross_msl/CMakeLists.txt",
                     "set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/build)",
                     "set(CMAKE_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})"),
               patch(plume, "plume_metal.cpp", old_library, new_library, [(old_resize, new_resize)])]
    # Match a native text snapshot to the workload actually being presented.
    # The draw callback must never read live guest RDRAM or the latest CPU frame.
    records += [patch(checkout, "src/rhi/rt64_render_hooks.h",
                      "    RenderHookInit *GetRenderHookInit();",
                      "    void SetRenderHookWorkloadId(unsigned long long id);\n"
                      "    unsigned long long GetRenderHookWorkloadId();\n"
                      "    RenderHookInit *GetRenderHookInit();"),
                patch(checkout, "src/rhi/rt64_render_hooks.cpp",
                      "    static RenderHookInit *init = nullptr;",
                      "    static thread_local unsigned long long hookWorkloadId = 0;\n"
                      "    void SetRenderHookWorkloadId(unsigned long long id) { hookWorkloadId = id; }\n"
                      "    unsigned long long GetRenderHookWorkloadId() { return hookWorkloadId; }\n"
                      "    static RenderHookInit *init = nullptr;"),
                patch(checkout, "src/hle/rt64_present_queue.cpp",
                      "                    drawHook(commandList, swapChainFramebuffer);",
                      "                    SetRenderHookWorkloadId(present.workloadId);\n"
                      "                    drawHook(commandList, swapChainFramebuffer);")]
    recorded = {r['path'] for r in records}
    for relative in NATIVE_MODEL_PATCHES:
        if str((checkout/relative).relative_to(ROOT)) not in recorded:
            records.append(patch(checkout, relative))
    report = {"schema": "srw64.recomp-graphics-source-patches.v1", "rt64_commit": revision,
              "plume_commit": plume_revision, "patches": records,
              "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "native_model_hooks_sha256": hashlib.sha256((ROOT/'tools/recomp/toolchain/native_model_hook_patches.py').read_bytes()).hexdigest(),
              "purpose": "Metal source compilation, resize descriptor synchronization, workload-matched UI hooks, and opt-in native model callbacks preserving scene transforms and draw order"}
    (ROOT / "build/recomp/graphics-source-patches.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
