#include "bezel.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace srw64::bezel;

int main(int, char** argv) {
    // An overlay .cfg names its image relative to itself; a .png is itself; others nothing.
    const auto dir = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "tv.cfg") << "overlays = 1\n\noverlay0_overlay = \"tv frame.png\"\n\noverlay0_full_screen = true\n";
    assert(image_of(dir / "tv.cfg") == dir / "tv frame.png");
    assert(image_of(dir / "A.PNG") == dir / "A.PNG");
    std::ofstream(dir / "none.cfg") << "overlays = 0\n";
    assert(image_of(dir / "none.cfg").empty());
    assert(image_of(dir / "notes.txt").empty());

    // A 16 x 8 image, opaque but for a window at x 4..11, y 2..5.
    const int w = 16, h = 8;
    std::vector<uint8_t> rgba(size_t(w) * h * 4, 255);
    for (int y = 2; y < 6; ++y) for (int x = 4; x < 12; ++x) rgba[(size_t(y) * w + x) * 4 + 3] = 0;
    const auto hole = find_hole(rgba.data(), w, h);
    assert(hole.found && hole.left == 4 && hole.top == 2 && hole.right == 12 && hole.bottom == 6);
    // The window lands on the picture: picture 400 x 300 at (200, 50) is 50 per image pixel
    // across and 75 down, so the image starts 4 x 50 left and 2 x 75 above it.
    const auto r = place(w, h, hole, {200, 50, 400, 300}, 800, 400);
    assert(r.x == 0 && r.y == -100 && r.w == 800 && r.h == 600);
    // No window: the image covers the screen.
    std::vector<uint8_t> solid(size_t(w) * h * 4, 255);
    assert(!find_hole(solid.data(), w, h).found);
    const auto full = place(w, h, find_hole(solid.data(), w, h), {200, 50, 400, 300}, 800, 400);
    assert(full.x == 0 && full.y == 0 && full.w == 800 && full.h == 400);
    std::puts("native bezel: ok");
}
