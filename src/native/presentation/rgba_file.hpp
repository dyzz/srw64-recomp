#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "stb/stb_image.h"

namespace srw64::presentation {
// Straight RGBA8 from an image file. The released HD pack stores opaque colour
// as JPEG with the alpha beside it: "name.jpg" + "name.alpha.png", a grey+alpha
// PNG whose grey is the silhouette colour, so the same file is the silhouette
// (tools/release/compress_hd.py). Any other file loads as it is.
struct RgbaFile {
    int width = 0, height = 0;
    bool split = false;  // colour and alpha came from two files
    std::vector<uint8_t> pixels;
};

inline std::filesystem::path alpha_beside(const std::filesystem::path& file) {
    if (file.extension() != ".jpg") return {};
    auto alpha = file;
    alpha.replace_extension(".alpha.png");
    return std::filesystem::exists(alpha) ? alpha : std::filesystem::path();
}

// An image file's bytes, or a range of one: "<file>#<offset>:<size>", the ROM import's
// art.pack (launch.cpp hands its images out so).
inline std::vector<char> read_image_bytes(const std::string& path) {
    const auto mark = path.rfind('#');
    std::ifstream input(mark == std::string::npos ? path : path.substr(0, mark), std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read image " + path);
    if (mark == std::string::npos) {   // whole, not a byte at a time through the stream
        input.seekg(0, std::ios::end);
        std::vector<char> bytes(size_t(input.tellg()));
        input.seekg(0);
        input.read(bytes.data(), std::streamsize(bytes.size()));
        return bytes;
    }
    const auto colon = path.find(':', mark);
    if (colon == std::string::npos) throw std::runtime_error("Invalid image range " + path);
    const auto offset = std::stoull(path.substr(mark + 1, colon - mark - 1)), size = std::stoull(path.substr(colon + 1));
    std::vector<char> bytes(size);
    input.seekg(std::streamoff(offset));
    if (!input.read(bytes.data(), std::streamsize(size))) throw std::runtime_error("Cannot read image " + path);
    return bytes;
}

inline RgbaFile load_rgba(const std::filesystem::path& file) {
    RgbaFile image;
    int n = 0;
    if (const auto name = file.string(); name.find('#') != std::string::npos) {
        const auto bytes = read_image_bytes(name);
        uint8_t* colour = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(bytes.size()),
                                                &image.width, &image.height, &n, 4);
        if (!colour) throw std::runtime_error("Cannot read image " + name);
        image.pixels.assign(colour, colour + size_t(image.width) * image.height * 4);
        stbi_image_free(colour);
        return image;
    }
    uint8_t* colour = stbi_load(file.string().c_str(), &image.width, &image.height, &n, 4);
    if (!colour) throw std::runtime_error("Cannot read image " + file.string());
    image.pixels.assign(colour, colour + size_t(image.width) * image.height * 4);
    stbi_image_free(colour);
    const auto alpha_file = alpha_beside(file);
    if (alpha_file.empty()) return image;
    int w = 0, h = 0;
    uint8_t* alpha = stbi_load(alpha_file.string().c_str(), &w, &h, &n, 2);
    if (!alpha) throw std::runtime_error("Cannot read image " + alpha_file.string());
    if (w != image.width || h != image.height) {
        stbi_image_free(alpha);
        throw std::runtime_error("Alpha size differs from its colour: " + alpha_file.string());
    }
    for (size_t i = 0; i < size_t(w) * h; ++i) image.pixels[i * 4 + 3] = alpha[i * 2 + 1];
    stbi_image_free(alpha);
    image.split = true;
    return image;
}
}
