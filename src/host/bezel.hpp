#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

// RetroArch overlay images around the 4:3 picture (docs/native/bezels-and-filters.md).
// The picture never moves: the image is stretched so that its transparent window lands
// on the picture, whatever size the window was drawn for, and whatever of it falls
// outside the screen is cut off. Pure functions; the frontend draws it under the
// interface (bezel_sync).
namespace srw64::bezel {

// The image a choice names: a .png itself, or the first overlay's image in an overlay
// .cfg (overlay0_overlay = name.png, relative to the .cfg). Empty when there is none.
inline std::filesystem::path image_of(const std::filesystem::path& file) {
    auto extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {return char(std::tolower(c));});
    if (extension == ".png") return file;
    if (extension != ".cfg") return {};
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        auto key = line.substr(0, equals), value = line.substr(equals + 1);
        const auto trim = [](std::string& text) {
            const auto first = text.find_first_not_of(" \t\r\""), last = text.find_last_not_of(" \t\r\"");
            text = first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        };
        trim(key);
        trim(value);
        if (key == "overlay0_overlay" && !value.empty()) return file.parent_path() / std::filesystem::path(value);
    }
    return {};
}

// The transparent window: from the image's centre outwards while alpha stays under half,
// in image pixels, right and bottom exclusive. None when the centre itself is opaque.
struct Hole {int left = 0, top = 0, right = 0, bottom = 0; bool found = false;};
inline Hole find_hole(const uint8_t* rgba, int width, int height) {
    Hole hole;
    if (!rgba || width <= 0 || height <= 0) return hole;
    const auto clear = [&](int x, int y) {return rgba[(size_t(y) * width + x) * 4 + 3] < 128;};
    const int cx = width / 2, cy = height / 2;
    if (!clear(cx, cy)) return hole;
    int left = cx, right = cx, top = cy, bottom = cy;
    while (left > 0 && clear(left - 1, cy)) --left;
    while (right + 1 < width && clear(right + 1, cy)) ++right;
    while (top > 0 && clear(cx, top - 1)) --top;
    while (bottom + 1 < height && clear(cx, bottom + 1)) ++bottom;
    hole = {left, top, right + 1, bottom + 1, true};
    return hole;
}

// Where to draw the image, in window pixels, for the picture at (x, y, w, h): its window
// on the picture; without a window, the whole image over the whole screen.
struct Rect {float x = 0, y = 0, w = 0, h = 0;};
inline Rect place(int image_w, int image_h, const Hole& hole, const Rect& picture, float screen_w, float screen_h) {
    if (!hole.found || hole.right <= hole.left || hole.bottom <= hole.top) return {0, 0, screen_w, screen_h};
    const float sx = picture.w / float(hole.right - hole.left), sy = picture.h / float(hole.bottom - hole.top);
    return {picture.x - hole.left * sx, picture.y - hole.top * sy, image_w * sx, image_h * sy};
}

}  // namespace srw64::bezel
