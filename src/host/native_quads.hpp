// The host's tagged draws as pictures on quads, for a renderer without RT64 (the PS Vita's GL
// path, docs/design/psv-port.md V1): a module that tags draws (native_map.cpp's window frames,
// native_sprite.cpp's story and title text) says what each tag id paints, and the renderer
// draws it where the tag was. RT64 builds still draw these through their own hooks.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace srw64::native_quads {
struct Quad {
    // The picture: premultiplied RGBA8, width x height. A renderer may keep its texture for as
    // long as it holds `pixels`; the same pointer is the same picture.
    std::shared_ptr<const std::vector<uint8_t>> pixels;
    uint32_t width = 0, height = 0;
    // A GPU-compressed picture instead (hd_texture.hpp): its GL format and mip levels, the
    // levels' blocks one after the other in `pixels`. 0: RGBA8, one level.
    uint32_t format = 0, levels = 1;
    // A rectangle in picture pixels (left, top, right, bottom), or with `model` in the tagged
    // triangles' model space (left, top, right, bottom, y up) at depth z.
    bool model = false;
    float rect[4]{}, uv[4]{0, 0, 1, 1};
    float z = 0;
    float color[4]{1, 1, 1, 1};   // tint, and the alpha everything is multiplied by
    float fill[4]{};              // a colour (rgb) that replaces the picture's by fill[3] (a silhouette)
    // A tactical map's palette shift (HdMapPS): one palette index per texel (width x height),
    // and per index the live palette's difference from the reference, 128 + d / 2 in RGB.
    std::shared_ptr<const std::vector<uint8_t>> index;
    std::shared_ptr<const std::vector<uint8_t>> shift;   // 256 x RGBA8
    bool wrap = false;            // repeats across
    bool whole = false;           // drawn past the original's 320: not held to the scissor
    float left = -1;              // >= 0: where the rectangle's x = 0 is in the picture (a widened
                                  // map's view, wide_map.hpp), instead of the command's place
    std::string label;            // what it is, for traces
};
// True when the id is the provider's (out holds what it paints, maybe nothing yet).
using Provider = bool (*)(uint32_t id, std::vector<Quad>& out);
void add(Provider provider);
// Every provider in turn, until one knows the id.
bool find(uint32_t id, std::vector<Quad>& out);
}
