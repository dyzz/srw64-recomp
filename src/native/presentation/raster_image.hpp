#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace srw64::presentation {
// Owned CPU pixels, no device/texture/command-buffer handles. Rows are top-down,
// tightly packed BGRA8 with premultiplied alpha and the existing sRGB-encoded
// component values. Upload as BGRA8_UNORM and blend ONE / ONE_MINUS_SRC_ALPHA;
// do not silently add an sRGB texture conversion or unpremultiply the pixels.
struct Bgra8Surface {
    uint32_t width{}, height{};
    std::vector<uint8_t> pixels;
    static size_t required_bytes(uint32_t width, uint32_t height) {
        // Preserve the old drawable bound; check before multiplication/allocation.
        if (!width || !height || width > 8192 || height > 8192)
            throw std::runtime_error("Invalid native UI drawable");
        return size_t(width) * size_t(height) * 4;
    }
    Bgra8Surface(uint32_t w, uint32_t h) : width(w), height(h), pixels(required_bytes(w, h)) {}
    size_t row_bytes() const { return size_t(width) * 4; }
    void validate() const {
        if (pixels.size() != required_bytes(width, height))
            throw std::runtime_error("Native UI pixel buffer size mismatch");
    }
};
}
