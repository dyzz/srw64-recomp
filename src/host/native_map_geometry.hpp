#pragma once
#include <algorithm>

namespace srw64::hdmap {
// Project a map-space sprite through the exact map quad, clipping to that quad.
// The clipped texture coordinates keep scrolling from stretching an edge sprite.
inline bool project_overlay(const float* map_rect, const float* map_uv,
                            int map_width, int map_height, int x, int y,
                            int width, int height, float* rect, float* uv) {
    const int extents[] = {map_width, map_height};
    const int origin[] = {x, y}, size[] = {width, height};
    for (int axis = 0; axis < 2; ++axis) {
        const float span = map_uv[axis + 2] - map_uv[axis];
        if (span <= 0 || extents[axis] <= 0 || size[axis] <= 0) return false;
        const float scale = (map_rect[axis + 2] - map_rect[axis]) / span / extents[axis];
        const float start = map_rect[axis] + (origin[axis] - map_uv[axis] * extents[axis]) * scale;
        const float end = start + size[axis] * scale;
        rect[axis] = std::max(start, map_rect[axis]);
        rect[axis + 2] = std::min(end, map_rect[axis + 2]);
        if (rect[axis + 2] <= rect[axis]) return false;
        uv[axis] = (rect[axis] - start) / (end - start);
        uv[axis + 2] = (rect[axis + 2] - start) / (end - start);
    }
    return true;
}
}
