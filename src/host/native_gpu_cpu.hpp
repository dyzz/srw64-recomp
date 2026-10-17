#pragma once
// native_gpu.hpp without Plume, for the GL-only host (SRW64_GL_ONLY, the PS Vita's path): the
// pictures the HD layers keep stay on the CPU, where native_quads.hpp hands them to the GL
// renderer. Only what native_map.cpp and native_sprite.cpp hold outside their RT64 hooks.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace plume {
enum class RenderFormat { R8G8B8A8_UNORM, R8_UINT };
struct RenderDescriptorSet {};
}

namespace srw64::gpu {
class Texture {
public:
    Texture(uint32_t width, uint32_t height, plume::RenderFormat, std::vector<std::vector<uint8_t>> levels)
        : width_(width), height_(height), levels(std::move(levels)) {}
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    const std::vector<uint8_t>* pixels() const { return levels.empty() ? nullptr : &levels[0]; }
private:
    uint32_t width_, height_;
    std::vector<std::vector<uint8_t>> levels;
};

// The mip chain of an RGBA8 picture, 2 x 2 averaged (native_gpu.cpp's).
inline std::vector<std::vector<uint8_t>> rgba_mips(std::vector<uint8_t> level0, uint32_t width, uint32_t height) {
    if (level0.size() != size_t(width) * height * 4) throw std::runtime_error("RGBA level has the wrong size");
    std::vector<std::vector<uint8_t>> levels;
    levels.push_back(std::move(level0));
    uint32_t w = width, h = height;
    while (w > 1 || h > 1) {
        const auto& src = levels.back();
        const uint32_t dw = std::max(1u, w / 2), dh = std::max(1u, h / 2);
        std::vector<uint8_t> next(size_t(dw) * dh * 4);
        for (uint32_t y = 0; y < dh; ++y)
            for (uint32_t x = 0; x < dw; ++x)
                for (uint32_t c = 0; c < 4; ++c) {
                    const auto at = [&](uint32_t xx, uint32_t yy) { return src[(size_t(std::min(yy, h - 1)) * w + std::min(xx, w - 1)) * 4 + c]; };
                    next[(size_t(y) * dw + x) * 4 + c] = uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        levels.push_back(std::move(next));
        w = dw; h = dh;
    }
    return levels;
}
}  // namespace srw64::gpu
