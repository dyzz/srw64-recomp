#!/usr/bin/env python3
"""Prepare the pinned, optional RecompFrontend renderer without changing upstream."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "build/recomp/upstream/RecompFrontend"
OUTPUT = ROOT / "build/recomp/frontend-adapter"


def git(*args: str, cwd: Path = SOURCE) -> str:
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


def exported(directory: Path) -> bool:
    """A copy without its .git, carrying its revision in .srw64-revision (the Windows CI job)."""
    return not (directory / ".git").exists() and (directory / ".srw64-revision").is_file()


def revision(directory: Path) -> str:
    return (directory / ".srw64-revision").read_text().strip() if exported(directory) else git("rev-parse", "HEAD", cwd=directory)


def write_changed(path: Path, data: bytes) -> None:
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def prepare(fetch: bool = False) -> dict:
    lock = json.loads((ROOT / "config/recomp/frontend.json").read_text())
    if lock.get("schema") != "srw64.frontend-source.v1":
        raise ValueError("Invalid frontend lock")
    if not SOURCE.exists():
        if not fetch:
            raise RuntimeError("Run tools/recomp/toolchain/prepare_frontend.py --fetch first")
        SOURCE.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--no-checkout", lock["url"], str(SOURCE)], check=True)
        git("checkout", "--detach", lock["commit"])
    if revision(SOURCE) != lock["commit"]:
        raise RuntimeError("Frontend checkout differs from lock; existing checkout left untouched")
    if fetch:
        git("submodule", "update", "--init", "recompui/lib/RmlUi")
    rml = SOURCE / "recompui/lib/RmlUi"
    if revision(rml) != lock["rmlui_commit"]:
        raise RuntimeError("RmlUi checkout differs from lock")
    for directory in (SOURCE, rml):
        if not exported(directory) and git("status", "--porcelain", cwd=directory):
            raise RuntimeError(f"Dirty upstream checkout: {directory}")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    renderer = SOURCE / "recompui/src/renderer"
    header = (renderer / "ui_renderer.h").read_text()
    # The implementation only needs RmlUi and Plume. Avoid pulling the launcher's
    # mod/config/input headers through its umbrella header.
    before = '#include "recompui.h"'
    if header.count(before) != 1:
        raise RuntimeError("Pinned renderer header adapter no longer matches")
    # On Windows rt64_plume.h brings in objbase.h, which defines interface as struct, and
    # the renderer names a parameter interface.
    header = header.replace(before, '#include <string>\n#include <vector>\n#include "common/rt64_plume.h"\n'
                            '#ifdef interface\n#undef interface\n#endif\n#include <RmlUi/Core.h>')
    source = (renderer / "ui_renderer.cpp").read_bytes()
    # Android swap chains are R8G8B8A8 (tools/recomp/toolchain/rt64_android_patches.py); the
    # renderer's pipelines must match the render pass it draws into.
    swap_format = b"    static constexpr plume::RenderFormat SwapChainFormat = plume::RenderFormat::B8G8R8A8_UNORM;"
    if source.count(swap_format) != 1:
        raise RuntimeError("Pinned renderer swap chain format no longer matches")
    source = source.replace(swap_format, b"#ifdef __ANDROID__\n"
                            b"    static constexpr plume::RenderFormat SwapChainFormat = plume::RenderFormat::R8G8B8A8_UNORM;\n"
                            b"#else\n" + swap_format + b"\n#endif")
    # Upstream draws the UI through an 8x MSAA copy of the screen it resolves every frame.
    # Here it draws straight into the target, as it in fact always had: frontend.cpp sets
    # the target after start(), so the MSAA pipeline drew into the 1x screen, which D3D12
    # calls an error (#614 RENDER_TARGET_SAMPLE_DESC_MISMATCH_PIPELINE_STATE; with the debug
    # layer present, 0.4.6 broke on it at the title, Radeon 8060S) and which garbled every
    # page on a 6th-gen Intel GPU on Vulkan. Drawn through the copy instead, the composite
    # blends with SRC_ALPHA a second time and darkens every translucent panel.
    msaa = b"        const plume::RenderSampleCounts desired_sample_count = plume::RenderSampleCount::COUNT_8;"
    if source.count(msaa) != 1:
        raise RuntimeError("Pinned renderer sample count no longer matches")
    source = source.replace(msaa, b"        const plume::RenderSampleCounts desired_sample_count = plume::RenderSampleCount::COUNT_1;")
    # Two passes on one command list: the pages under a RetroArch filter, then those over it
    # (docs/native/bezels-and-filters.md). The second pass must keep the first's vertices and
    # uploads, which start() would otherwise write over before the GPU reads them.
    replacements = [
        (b"    void start(plume::RenderCommandList* list, int image_width, int image_height) {\n",
         b"    void start(plume::RenderCommandList* list, int image_width, int image_height, bool continue_frame = false) {\n"),
        (b"        stale_buffers_.clear();\n\n        // Reset buffers.\n        reset_dynamic_buffer(upload_buffer_);\n"
         b"        reset_dynamic_buffer(vertex_buffer_);\n        reset_dynamic_buffer(index_buffer_);\n",
         b"        // SRW64: a second pass on the same command list maps the buffers again where the\n"
         b"        // first left them; only a new command list starts them over.\n"
         b"        if (continue_frame) {\n"
         b"            for (DynamicBuffer *buffer : {&upload_buffer_, &vertex_buffer_, &index_buffer_})\n"
         b"                buffer->mapped_data_ = reinterpret_cast<uint8_t*>(buffer->buffer_->map());\n"
         b"        } else {\n"
         b"        stale_buffers_.clear();\n\n        // Reset buffers.\n        reset_dynamic_buffer(upload_buffer_);\n"
         b"        reset_dynamic_buffer(vertex_buffer_);\n        reset_dynamic_buffer(index_buffer_);\n        }\n"),
        (b"void recompui::RmlRenderInterface_RT64::start(plume::RenderCommandList* list, int image_width, int image_height) {\n"
         b"    assert(static_cast<bool>(impl));\n\n    impl->start(list, image_width, image_height);\n",
         b"void recompui::RmlRenderInterface_RT64::start(plume::RenderCommandList* list, int image_width, int image_height, bool continue_frame) {\n"
         b"    assert(static_cast<bool>(impl));\n\n    impl->start(list, image_width, image_height, continue_frame);\n"),
    ]
    for before, after in replacements:
        if source.count(before) != 1:
            raise RuntimeError("Pinned renderer start() no longer matches")
        source = source.replace(before, after)
    start = "        void start(plume::RenderCommandList* list, int image_width, int image_height);"
    if header.count(start) != 1:
        raise RuntimeError("Pinned renderer header start() no longer matches")
    header = header.replace(start, "        void start(plume::RenderCommandList* list, int image_width, int image_height, bool continue_frame = false);")
    write_changed(OUTPUT / "ui_renderer.h", header.encode())
    write_changed(OUTPUT / "ui_renderer.cpp", source)
    # RmlUi: shrink-to-fit widths kept for one layout. A flex item without a definite size is
    # measured by laying its whole subtree out apart, and nested flex rows ask again at every
    # level: a stat box of the pre-battle page 16 times a layout, the text in it 64 times, most of
    # the page's 1.8 ms layout on an i7-8700K (2026-10-22). Within one LayoutEngine::FormatElement
    # the answer for an element and containing block does not change. The two files are compiled
    # from here in place of the pinned ones (src/native/ui/CMakeLists.txt).
    layout = rml / "Source/Core/Layout"
    rml_edits = {
        "LayoutDetails.cpp": [
            ("#include <float.h>", "#include <float.h>\n#include <unordered_map>"),
            ("float LayoutDetails::GetShrinkToFitWidth(Element* element, Vector2f containing_block)\n{",
             "static float ShrinkToFitWidthUncached(Element* element, Vector2f containing_block)\n{"),
            ("\treturn shrink_to_fit_width;\n}\n",
             "\treturn shrink_to_fit_width;\n}\n\n"
             "// SRW64: kept until the next LayoutEngine::FormatElement (prepare_frontend.py).\n"
             "unsigned srw64_layout_generation = 1;\n"
             "namespace {\n"
             "struct ShrinkToFitKey {\n"
             "\tElement* element;\n"
             "\tfloat x, y;\n"
             "\tbool operator==(const ShrinkToFitKey& o) const { return element == o.element && x == o.x && y == o.y; }\n"
             "};\n"
             "struct ShrinkToFitHash {\n"
             "\tsize_t operator()(const ShrinkToFitKey& k) const\n"
             "\t{\n"
             "\t\treturn std::hash<Element*>()(k.element) ^ (std::hash<float>()(k.x) * 31) ^ (std::hash<float>()(k.y) * 131);\n"
             "\t}\n"
             "};\n"
             "std::unordered_map<ShrinkToFitKey, float, ShrinkToFitHash> shrink_to_fit_widths;\n"
             "unsigned shrink_to_fit_generation = 0;\n"
             "} // namespace\n\n"
             "float LayoutDetails::GetShrinkToFitWidth(Element* element, Vector2f containing_block)\n"
             "{\n"
             "\tif (shrink_to_fit_generation != srw64_layout_generation)\n"
             "\t{\n"
             "\t\tshrink_to_fit_widths.clear();\n"
             "\t\tshrink_to_fit_generation = srw64_layout_generation;\n"
             "\t}\n"
             "\tconst ShrinkToFitKey key{element, containing_block.x, containing_block.y};\n"
             "\tif (auto found = shrink_to_fit_widths.find(key); found != shrink_to_fit_widths.end())\n"
             "\t\treturn found->second;\n"
             "\tconst float width = ShrinkToFitWidthUncached(element, containing_block);\n"
             "\tshrink_to_fit_widths.emplace(key, width);\n"
             "\treturn width;\n"
             "}\n"),
        ],
        "LayoutEngine.cpp": [
            ("namespace Rml {\n", "namespace Rml {\n\nextern unsigned srw64_layout_generation;\n"),
            ("void LayoutEngine::FormatElement(Element* element, Vector2f containing_block)\n{\n"
             "\tRMLUI_ASSERT(element && containing_block.x >= 0 && containing_block.y >= 0);\n",
             "void LayoutEngine::FormatElement(Element* element, Vector2f containing_block)\n{\n"
             "\tRMLUI_ASSERT(element && containing_block.x >= 0 && containing_block.y >= 0);\n"
             "\t// SRW64: a new layout; the last one's shrink-to-fit widths go (LayoutDetails.cpp).\n"
             "\t++srw64_layout_generation;\n"),
        ],
    }
    (OUTPUT / "rmlui").mkdir(exist_ok=True)
    for name, edits in rml_edits.items():
        text = (layout / name).read_text()
        for before, after in edits:
            if text.count(before) != 1:
                raise RuntimeError(f"Pinned RmlUi {name} no longer matches")
            text = text.replace(before, after)
        # The copy lives in the adapter: its quoted includes point back into the pinned tree.
        text = re.sub(r'#include "([^"]+)"', lambda m: f'#include "{(layout / m[1]).resolve().as_posix()}"', text)
        write_changed(OUTPUT / "rmlui" / name, text.encode())
    report = {"schema": "srw64.frontend-adapter.v1", "commit": lock["commit"],
              "rmlui_commit": lock["rmlui_commit"],
              "renderer_sha256": hashlib.sha256(source).hexdigest(),
              "adapted_header_sha256": hashlib.sha256(header.encode()).hexdigest(),
              "scope": lock["scope"]}
    write_changed(OUTPUT / "source.json", (json.dumps(report, indent=2) + "\n").encode())
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    print(json.dumps(prepare(parser.parse_args().fetch), indent=2))
