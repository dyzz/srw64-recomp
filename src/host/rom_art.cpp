#include "rom_art.hpp"
#include "app/rom_import_codec.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>

namespace srw64::rom_art {
namespace {
using namespace srw64::app::rom_import;

constexpr size_t kResources = 0xA20BD0;                     // resource table (srw64_rom.resources)
constexpr uint32_t kResidentRam = 0x80076610, kResidentRom = 0x1000;
constexpr uint32_t kLayouts = 0x800C8BB8, kLayoutCount = 150, kLayoutSize = 24;
constexpr uint16_t kLines = 1295, kArrows = 1302;           // window line atlas, red page arrows
constexpr uint16_t kFrameFirst = 1165, kFrameLast = 1330, kDefaultPalette = 1297;
constexpr int kScale = 4;
// Frames the tactical overlay builds itself (801CD718, 801CABAC) and one it shows from a table.
const std::pair<uint16_t, uint16_t> kExtra[] = {{1014, 1015}, {1165, 1297}, {1172, 1297}, {1173, 1297}};
const std::set<uint16_t> kHudStrip = {1193, 1194};          // resource-1296 scenes: not frames

std::span<const uint8_t> rom_bytes;
std::vector<FrameSpec> specs;
std::map<uint16_t, FrameSpec> by_scene;

// A CI4 atlas as one index per pixel, decoded once.
struct Atlas { int width = 0, height = 0; std::vector<uint8_t> pixels; };
std::mutex atlas_mutex;
std::map<uint16_t, Atlas> atlases;

std::optional<std::vector<uint8_t>> extract(uint16_t id) {
    try { return resource(rom_bytes, kResources, id).bytes; }
    catch (const std::exception&) { return std::nullopt; }
}

const Atlas& atlas(uint16_t id) {
    std::lock_guard lock(atlas_mutex);
    auto found = atlases.find(id);
    if (found != atlases.end()) return found->second;
    Atlas out;
    if (const auto data = extract(id); data && data->size() >= 8) {
        const int w = (*data)[2] << 8 | (*data)[3], h = (*data)[4] << 8 | (*data)[5];
        if (w > 0 && h > 0 && size_t(8) + size_t(w) * h / 2 <= data->size()) {
            out.width = w; out.height = h; out.pixels.resize(size_t(w) * h);
            for (size_t i = 0; i < size_t(w) * h / 2; ++i) {
                const uint8_t v = (*data)[8 + i];
                out.pixels[2 * i] = v >> 4; out.pixels[2 * i + 1] = v & 15;
            }
        }
    }
    return atlases.emplace(id, std::move(out)).first->second;
}

void build_table() {
    std::map<uint16_t, FrameSpec> out;
    std::set<uint16_t> other;
    for (uint32_t i = 0; i < kLayoutCount; ++i) {
        const size_t entry = kLayouts + i * kLayoutSize - kResidentRam + kResidentRom;
        const uint16_t scene = be16(rom_bytes, entry + 12), atlas_id = be16(rom_bytes, entry + 14), palette = be16(rom_bytes, entry + 16);
        if (atlas_id == kLines || atlas_id == kArrows) out.try_emplace(scene, FrameSpec{scene, atlas_id, palette});
        else other.insert(scene);
    }
    for (const auto& [scene, palette] : kExtra) out.try_emplace(scene, FrameSpec{scene, kLines, palette});
    // Menus pick some grid scenes at run time (1198, 1199): the rest of the frame range.
    for (uint16_t scene = kFrameFirst; scene <= kFrameLast; ++scene) {
        if (out.count(scene) || other.count(scene) || kHudStrip.count(scene)) continue;
        const auto data = extract(scene);
        if (data && data->size() >= 8 && ((*data)[0] << 8 | (*data)[1]) == 6) out.emplace(scene, FrameSpec{scene, kLines, kDefaultPalette});
    }
    for (const auto& [scene, spec] : out) { specs.push_back(spec); by_scene[scene] = spec; }
}

// Strokes through pixel centres: a capsule of radius `radius` source pixels from a to b.
// Each texel keeps the highest coverage and the index of the nearer end's pixel.
struct Layer {
    std::vector<float> cover;
    std::vector<uint8_t> index;
};
void stroke(Layer& layer, int width, int height, int scale, float ax, float ay, float bx, float by,
            uint8_t index_a, uint8_t index_b, float radius) {
    const float x0 = std::min(ax, bx) - radius - 1, x1 = std::max(ax, bx) + radius + 1;
    const float y0 = std::min(ay, by) - radius - 1, y1 = std::max(ay, by) + radius + 1;
    const int tx0 = std::max(0, int(x0 * scale)), tx1 = std::min(width * scale, int(x1 * scale) + 1);
    const int ty0 = std::max(0, int(y0 * scale)), ty1 = std::min(height * scale, int(y1 * scale) + 1);
    const float dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
    for (int ty = ty0; ty < ty1; ++ty)
        for (int tx = tx0; tx < tx1; ++tx) {
            const float px = (tx + .5f) / scale, py = (ty + .5f) / scale;
            float t = length > 0 ? ((px - ax) * dx + (py - ay) * dy) / length : 0;
            t = std::clamp(t, 0.f, 1.f);
            const float qx = ax + t * dx - px, qy = ay + t * dy - py;
            const float distance = std::sqrt(qx * qx + qy * qy);
            const float c = std::clamp((radius - distance) * scale + .5f, 0.f, 1.f);
            const size_t i = size_t(ty) * width * scale + tx;
            if (c > layer.cover[i]) { layer.cover[i] = c; layer.index[i] = t < .5f ? index_a : index_b; }
        }
}

// The frame redrawn: line and shadow pixels (by the palette's brightness) as strokes.
void draw(const std::vector<uint8_t>& grid, int width, int height, int scale, const std::array<uint32_t, 256>& palette,
          std::vector<uint8_t>& index, std::vector<uint8_t>& coverage) {
    const size_t texels = size_t(width) * scale * height * scale;
    Layer line{std::vector<float>(texels), std::vector<uint8_t>(texels)}, shadow = line;
    const auto dark = [&](uint8_t n) {
        const uint32_t c = palette[n];
        return .3f * (c & 0xFF) + .59f * ((c >> 8) & 0xFF) + .11f * ((c >> 16) & 0xFF) < 48;
    };
    const auto at = [&](int x, int y) -> uint8_t { return x >= 0 && y >= 0 && x < width && y < height ? grid[size_t(y) * width + x] : 0; };
    constexpr float radius = .5f;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const uint8_t n = at(x, y);
            if (!n) continue;
            const bool is_dark = dark(n);
            Layer& layer = is_dark ? shadow : line;
            const float cx = x + .5f, cy = y + .5f;
            stroke(layer, width, height, scale, cx, cy, cx, cy, n, n, radius);
            for (const auto [ox, oy] : {std::pair{1, 0}, {0, 1}, {1, 1}, {-1, 1}}) {
                const uint8_t m = at(x + ox, y + oy);
                if (!m || dark(m) != is_dark) continue;
                // A diagonal only where no straight neighbour already turns the corner.
                if (ox && oy && (at(x + ox, y) && dark(at(x + ox, y)) == is_dark || at(x, y + oy) && dark(at(x, y + oy)) == is_dark)) continue;
                stroke(layer, width, height, scale, cx, cy, cx + ox, cy + oy, n, m, radius);
            }
        }
    index.assign(texels, 0);
    coverage.assign(texels, 0);
    for (size_t i = 0; i < texels; ++i) {
        const float c = std::max(line.cover[i], shadow.cover[i]);
        if (c <= 0) continue;
        // The blue line lies over its shadow.
        index[i] = line.cover[i] >= .5f || line.cover[i] >= shadow.cover[i] ? line.index[i] : shadow.index[i];
        coverage[i] = uint8_t(std::lround(c * 255));
    }
}
}

void initialize(std::span<const uint8_t> rom) {
    if (!rom_bytes.empty()) return;
    rom_bytes = rom;
    try { build_table(); }
    catch (const std::exception&) { specs.clear(); by_scene.clear(); }
}
bool ready() { return !rom_bytes.empty(); }
const std::vector<FrameSpec>& frames() { return specs; }
std::optional<FrameSpec> frame(uint16_t scene) {
    const auto found = by_scene.find(scene);
    return found == by_scene.end() ? std::nullopt : std::optional(found->second);
}

IndexImage frame_image(const FrameSpec& spec) {
    IndexImage image;
    const auto scene = extract(spec.scene);
    const Atlas& source = atlas(spec.atlas);
    if (!scene || scene->size() < 8 || source.pixels.empty()) return image;
    const auto& d = *scene;
    const auto u16 = [&](size_t p) -> int { return p + 1 < d.size() ? d[p] << 8 | d[p + 1] : -1; };
    if (u16(0) != 6) return image;
    const int groups = u16(2), w = u16(4) * 8, h = u16(6) * 8;
    if (groups < 0 || w <= 0 || h <= 0) return image;
    std::vector<uint8_t> grid(size_t(w) * h);
    const size_t table = 8 + size_t(w / 8) * (h / 8);
    for (int g = 0; g < groups; ++g) {
        const int tile = u16(table + g * 6), count = u16(table + g * 6 + 2), offset = u16(table + g * 6 + 4);
        if (tile < 0 || count < 0 || offset < 0) return {};
        if (!tile) continue;
        const int sx = (tile & 15) * 8 + ((tile & 0x300) >> 1), sy = ((tile & 0xF0) >> 1) + ((tile & 0xC00) >> 3);
        if (sy + 16 > source.height || sx + 16 > source.width) continue;   // 0xFFEF in 1246: no picture
        for (int k = 0; k < count; ++k) {
            const int flags = u16(offset + k * 6), x = u16(offset + k * 6 + 2), y = u16(offset + k * 6 + 4);
            if (flags < 0 || x < 0 || y < 0) return {};
            for (int r = 0; r < 16; ++r)
                for (int c = 0; c < 16; ++c) {
                    const int src_r = flags & 0x8000 ? 15 - r : r, src_c = flags & 0x4000 ? 15 - c : c;
                    const uint8_t n = source.pixels[size_t(sy + src_r) * source.width + sx + src_c];
                    if (n && y + r < h && x + c < w) grid[size_t(y + r) * w + x + c] = n;
                }
        }
    }
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (grid[size_t(y) * w + x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 < 0) return image;
    x0 = std::max(0, x0 - 1); y0 = std::max(0, y0 - 1); x1 = std::min(w, x1 + 2); y1 = std::min(h, y1 + 2);
    const int cw = x1 - x0, ch = y1 - y0;
    std::vector<uint8_t> crop(size_t(cw) * ch);
    for (int y = 0; y < ch; ++y) std::copy_n(&grid[size_t(y + y0) * w + x0], cw, &crop[size_t(y) * cw]);
    image.width = cw; image.height = ch; image.scale = kScale;
    image.origin[0] = x0; image.origin[1] = y0;
    if (const auto palette = extract(spec.palette); palette && palette->size() >= 8) {
        const size_t count = (palette->size() - 8) / 2;
        for (size_t n = 0; n < 256 && n < count; ++n) {
            const uint16_t c = uint16_t((*palette)[8 + 2 * n] << 8 | (*palette)[9 + 2 * n]);
            const auto channel = [&](int shift) { return uint32_t(((c >> shift) & 31) * 255 / 31); };
            image.palette[n] = channel(11) | channel(6) << 8 | channel(1) << 16 | (c & 1 ? 0xFFu : 0u) << 24;
        }
    }
    image.palette[0] &= 0x00FFFFFFu;
    draw(crop, cw, ch, kScale, image.palette, image.index, image.coverage);
    image.source = std::move(crop);
    return image;
}
}
