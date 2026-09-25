// The run-time HD frame art (src/host/rom_art.cpp) against tools/hd_ai/frame_hd.py: every
// scene the script wrote to assets/hd-ai/frames/v1 must come out of the ROM with the same
// table entry, crop and reference palette. The strokes are drawn anew, so the pictures
// differ; they must cover every original line pixel, stay inside the crop and use only
// the indices the original scene uses. With a third argument, previews go there as PPM.
// Usage: test ROM FRAMES_DIR [PREVIEW_DIR]. Exits 0 with a note when ROM or frames are missing.
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "json/json.hpp"
#include "rom_art.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

namespace fs = std::filesystem;
using srw64::rom_art::FrameSpec;

int main(int argc, char** argv) {
    const fs::path rom_path = argc > 1 ? argv[1] : "rom.z64", frames = argc > 2 ? argv[2] : "assets/hd-ai/frames/v1";
    if (!fs::exists(rom_path) || !fs::exists(frames / "frames.json")) {
        std::printf("rom art: skipped (no ROM or no reference frames)\n");
        return 0;
    }
    std::ifstream rom_stream(rom_path, std::ios::binary);
    const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rom_stream)), {});
    srw64::rom_art::initialize(rom);
    const auto reference = nlohmann::json::parse(std::ifstream(frames / "frames.json"));
    std::set<uint16_t> expected, made;
    int failures = 0;
    for (const auto& row : reference.at("scenes")) expected.insert(row.at("scene").get<uint16_t>());
    for (const auto& spec : srw64::rom_art::frames()) {
        const auto image = srw64::rom_art::frame_image(spec);
        if (!image.width) continue;
        made.insert(spec.scene);
        const fs::path folder = frames / std::to_string(spec.scene);
        if (!expected.count(spec.scene)) { std::printf("scene %u: not in the reference\n", spec.scene); ++failures; continue; }
        const auto meta = nlohmann::json::parse(std::ifstream(folder / "meta.json"));
        if (meta.at("width") != image.width || meta.at("height") != image.height || meta.at("scale") != image.scale ||
            meta.at("origin")[0] != image.origin[0] || meta.at("origin")[1] != image.origin[1] ||
            meta.at("atlas") != spec.atlas || meta.at("palette") != spec.palette) {
            std::printf("scene %u: crop or table differs\n", spec.scene); ++failures; continue;
        }
        for (int n = 0; n < 256; ++n) {
            const auto& c = meta.at("reference_palette")[n];
            const uint32_t want = c[0].get<uint32_t>() | c[1].get<uint32_t>() << 8 | c[2].get<uint32_t>() << 16 | c[3].get<uint32_t>() << 24;
            if (want != image.palette[n]) { std::printf("scene %u: palette entry %d differs\n", spec.scene, n); ++failures; break; }
        }
        int w = 0, h = 0, channels = 0;
        uint8_t* pixels = stbi_load((folder / "index.png").c_str(), &w, &h, &channels, 1);
        if (!pixels || w != image.width * image.scale || h != image.height * image.scale ||
            image.index.size() != size_t(w) * h || image.coverage.size() != image.index.size()) {
            std::printf("scene %u: size differs\n", spec.scene); ++failures;
        } else {
            // Every original pixel's centre is covered; only the original's indices appear.
            std::set<uint8_t> used(image.source.begin(), image.source.end());
            bool bad = image.source.size() != size_t(image.width) * image.height;
            for (int y = 0; y < image.height && !bad; ++y)
                for (int x = 0; x < image.width && !bad; ++x) {
                    const size_t centre = size_t(y * image.scale + image.scale / 2) * w + x * image.scale + image.scale / 2;
                    if (image.source[size_t(y) * image.width + x] && image.coverage[centre] < 128) bad = true;
                }
            for (size_t i = 0; i < image.index.size() && !bad; ++i)
                if (image.coverage[i] && !used.count(image.index[i])) bad = true;
            if (bad) { std::printf("scene %u: strokes miss a line or use a foreign index\n", spec.scene); ++failures; }
        }
        stbi_image_free(pixels);
        if (argc > 3) {
            // RGB preview over a dark panel colour.
            std::ofstream out(fs::path(argv[3]) / (std::to_string(spec.scene) + ".ppm"), std::ios::binary);
            const int pw = image.width * image.scale, ph = image.height * image.scale;
            out << "P6\n" << pw << ' ' << ph << "\n255\n";
            for (size_t i = 0; i < image.index.size(); ++i) {
                const uint32_t c = image.palette[image.index[i]];
                const float a = image.coverage[i] / 255.f * float(c >> 24) / 255.f;
                const uint8_t bg[3] = {24, 30, 52};
                for (int k = 0; k < 3; ++k) out.put(char(uint8_t(((c >> (8 * k)) & 0xFF) * a + bg[k] * (1 - a))));
            }
        }
    }
    for (const auto scene : expected)
        if (!made.count(scene)) { std::printf("scene %u: missing at run time\n", scene); ++failures; }
    std::printf("rom art: %zu frame scenes drawn and checked, %d failure(s)\n", made.size(), failures);
    return failures ? 1 : 0;
}
