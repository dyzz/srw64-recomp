"""Build a CPU-only oracle from the exact pre-split renderer (test use only).

The baseline is fetched by CI from its pinned public source commit, never ROM
content. Only GPU upload and file output are replaced with an owned result;
Core Text helpers, layout, painting coordinates, colours and diagnostic blocks
remain the original code. Refuse any other input rather than silently rebaseline.
"""
import argparse
import hashlib
from pathlib import Path

BASE_BLOB = "c3ad3775a78548586c828ec34c9da8d9b766e653"


def generate(path: Path) -> str:
    raw = path.read_bytes()
    digest = hashlib.sha1(f"blob {len(raw)}\0".encode() + raw).hexdigest()
    if digest != BASE_BLOB:
        raise ValueError("Legacy renderer does not match the pre-split source lock")
    source = raw.decode("utf-8")
    helpers = source[source.index("template<class T> struct CF"):source.index("MTL::Device* device")]
    painter = source[source.index("struct Painter {"):source.index("void raster(const Frame&")]
    raster = source[source.index("void raster(const Frame&"):source.index("std::u16string utf16(")]
    raster = raster.removesuffix("}\n")
    raster = raster.replace("void raster(const Frame& frame,uint32_t width,uint32_t height) {",
                           "RasterizedFrame rasterize_frame(const Frame& frame,uint32_t width,uint32_t height) {\n"
                           "    localization::Scope scope(frame.catalog);\n"
                           "    RasterizedFrame result{presentation::Bgra8Surface(width,height),{}};")
    raster = raster.replace("    std::vector<uint8_t> pixels(size_t(width)*height*4);",
                            "    auto& pixels=result.image.pixels;")
    begin = raster.index("    auto* desc=MTL::TextureDescriptor")
    end = raster.index("    json report=")
    raster = raster[:begin] + raster[end:]
    raster = raster.replace('    if(srw64_full_diagnostics())std::ofstream(output/"dialogue-raster.json")<<report.dump(2)<<\'\\n\';',
                            '    result.report=std::move(report);return result;')
    conversions_layout = source[source.index("std::u16string utf16("):source.index("void metal_init(")]
    return ('#include "dialogue_raster.hpp"\n#include <CoreText/CoreText.h>\n#include <CoreGraphics/CoreGraphics.h>\n'
            '#include <algorithm>\n#include <cmath>\n#include <stdexcept>\n#include <utility>\n'
            'namespace srw64::dialogue::reference {\n'
            'std::u16string utf16(const std::string&);\nstd::string utf8(const std::u16string&);\n'
            'Layout typeset(const std::u16string&,unsigned,double,double);\n'
            'namespace {\nusing json=nlohmann::json;\n' + helpers + painter + '}\n' +
            raster + conversions_layout + '}\n')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(generate(args.source), encoding="utf-8")
