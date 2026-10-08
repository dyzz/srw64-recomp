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
FrameSpec line_frame(uint16_t scene) {
    const auto found = by_scene.find(scene);
    return found == by_scene.end() ? FrameSpec{scene, kLines, kDefaultPalette} : found->second;
}

IndexImage frame_image(const FrameSpec& spec, bool strokes) {
    IndexImage image;
    const auto scene = extract(spec.scene);
    const Atlas& source = atlas(spec.atlas);
    if (!scene || scene->size() < 8 || source.pixels.empty()) return image;
    const auto& d = *scene;
    const auto u16 = [&](size_t p) -> int { return p + 1 < d.size() ? d[p] << 8 | d[p + 1] : -1; };
    if (u16(0) != 6) return image;
    const int groups = u16(2), w0 = u16(4) * 8, h = u16(6) * 8;
    if (groups < 0 || w0 <= 0 || h <= 0) return image;
    // A widened menu: its cells moved over, so the grid may grow.
    const auto& widen = spec.widen;
    const int w = widen.delta > 0 ? std::max(w0, widen.shift_end + widen.delta) : w0;
    std::vector<uint8_t> grid(size_t(w) * h);
    const size_t table = 8 + size_t(w0 / 8) * (h / 8);
    for (int g = 0; g < groups; ++g) {
        const int tile = u16(table + g * 6), count = u16(table + g * 6 + 2), offset = u16(table + g * 6 + 4);
        if (tile < 0 || count < 0 || offset < 0) return {};
        if (!tile) continue;
        const int sx = (tile & 15) * 8 + ((tile & 0x300) >> 1), sy = ((tile & 0xF0) >> 1) + ((tile & 0xC00) >> 3);
        if (sy + 16 > source.height || sx + 16 > source.width) continue;   // 0xFFEF in 1246: no picture
        for (int k = 0; k < count; ++k) {
            const int flags = u16(offset + k * 6), y = u16(offset + k * 6 + 4);
            int x = u16(offset + k * 6 + 2), cells = 16;
            if (flags < 0 || x < 0 || y < 0) return {};
            // As menu_widen.cpp draws it: one column wider (sampled at 16 / (16 + delta)), the next ones over.
            if (widen.delta > 0 && y >= widen.top && y <= widen.bottom) {
                if (x == widen.stretch) cells = 16 + widen.delta;
                else if (x >= widen.shift && x < widen.shift_end) x += widen.delta;
            }
            for (int r = 0; r < 16; ++r)
                for (int c = 0; c < cells; ++c) {
                    const int u = c * 16 / cells;
                    const int src_r = flags & 0x8000 ? 15 - r : r, src_c = flags & 0x4000 ? 15 - u : u;
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
    if (strokes) draw(crop, cw, ch, kScale, image.palette, image.index, image.coverage);
    else {
        // The original pixels as they are, each a kScale square.
        image.index.assign(size_t(cw) * kScale * ch * kScale, 0);
        image.coverage.assign(image.index.size(), 0);
        for (int y = 0; y < ch * kScale; ++y)
            for (int x = 0; x < cw * kScale; ++x) {
                const uint8_t n = crop[size_t(y / kScale) * cw + x / kScale];
                const size_t i = size_t(y) * cw * kScale + x;
                image.index[i] = n; image.coverage[i] = n ? 255 : 0;
            }
    }
    image.source = std::move(crop);
    return image;
}
// --- Flat-colour scenes (tools/hd_ai/flat_scene_hd.py) --------------------------------

namespace {
struct Key { float r, g, b; };
struct FlatDef { FlatSpec spec; std::vector<std::vector<Key>> layers; };
const std::vector<FlatDef> kFlat = {
    {{619, 617, 618}, {{{8, 8, 8}, {0, 0, 0}}, {{255, 255, 255}}, {{189, 49, 49}}}},                      // BANPRESTO
    {{614, 615, 616}, {{{255, 255, 255}, {239, 239, 239}, {222, 222, 222}},
                       {{16, 16, 16}, {33, 33, 33}, {49, 49, 49}, {74, 74, 74}, {90, 90, 90}, {107, 107, 107}}}},  // GAME OVER
};
constexpr int kFlatScale = 8;
constexpr double kBlur = .45, kSharp = 2.0;

using Plane = std::vector<float>;

// The scene's one frame, straight RGBA8 (battle_graphics.render_scene with decode_indexed).
bool flat_frame(const FlatSpec& spec, int& width, int& height, std::vector<uint8_t>& rgba) {
    const auto scene = extract(spec.scene), image = extract(spec.atlas), palette = extract(spec.palette);
    if (!scene || !image || !palette || scene->size() < 6 || image->size() < 8 || palette->size() < 8) return false;
    const auto& d = *scene;
    const auto u16 = [&](size_t p) -> int { return p + 1 < d.size() ? d[p] << 8 | d[p + 1] : -1; };
    const int count = d[0];
    const size_t table = 2 + size_t(count) * 2 + 2;
    const int first = u16(table);
    if (first <= int(table)) return false;
    // One frame: its parts, 16 bytes each, until END_PART (0x8000).
    struct Part { int flags, s, t, w, h, x, y; };
    std::vector<Part> parts;
    for (size_t offset = size_t(first);; offset += 16) {
        if (offset + 16 > d.size()) return false;
        const int flags = u16(offset);
        if (flags & 0x8000) break;
        parts.push_back({flags, u16(offset + 2), u16(offset + 4), d[offset + 6], d[offset + 7],
                         int16_t(u16(offset + 8)), int16_t(u16(offset + 10))});
    }
    if (parts.empty()) return false;
    int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (const auto& p : parts) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x + p.w); y1 = std::max(y1, p.y + p.h); }
    const auto& im = *image;
    const int kind = im[0] << 8 | im[1], aw = im[2] << 8 | im[3], ah = im[4] << 8 | im[5];
    const bool ci4 = kind == 5 || kind == 14;
    const auto& pal = *palette;
    const size_t colours = (pal.size() - 8) / 2;
    const auto pixel = [&](int x, int y) -> std::array<uint8_t, 4> {
        if (x < 0 || y < 0 || x >= aw || y >= ah) return {0, 0, 0, 0};
        const size_t i = size_t(y) * aw + x;
        size_t n;
        if (ci4) { if (8 + i / 2 >= im.size()) return {}; n = i & 1 ? im[8 + i / 2] & 15 : im[8 + i / 2] >> 4; }
        else { if (8 + i >= im.size()) return {}; n = im[8 + i]; }
        if (n >= colours) return {};
        const uint16_t c = uint16_t(pal[8 + 2 * n] << 8 | pal[9 + 2 * n]);
        const auto ch = [&](int shift) { return uint8_t(std::lround(((c >> shift) & 31) * 255.0 / 31)); };
        return {ch(11), ch(6), ch(1), uint8_t(c & 1 ? 255 : 0)};
    };
    width = x1 - x0; height = y1 - y0;
    rgba.assign(size_t(width) * height * 4, 0);
    for (const auto& p : parts)
        for (int r = 0; r < p.h; ++r)
            for (int c = 0; c < p.w; ++c) {
                const auto v = pixel(p.flags & 0x10 ? p.s + p.w - 1 - c : p.s + c, p.t + r);
                if (!v[3]) continue;
                const int X = p.x - x0 + c, Y = p.y - y0 + r;
                if (X < 0 || Y < 0 || X >= width || Y >= height) continue;
                std::copy(v.begin(), v.end(), &rgba[(size_t(Y) * width + X) * 4]);
            }
    return true;
}

// Pillow's resize coefficients (BICUBIC, a = -0.5), for one axis.
struct Coeffs { std::vector<int> start, size; std::vector<double> weights; int taps = 0; };
Coeffs bicubic(int in, int out) {
    const double scale = double(in) / out, filterscale = std::max(scale, 1.0), support = 2 * filterscale;
    Coeffs c; c.taps = int(std::ceil(support)) * 2 + 1;
    c.start.resize(out); c.size.resize(out); c.weights.assign(size_t(out) * c.taps, 0);
    const auto cubic = [](double x) {
        constexpr double a = -.5;
        x = std::abs(x);
        if (x < 1) return ((a + 2) * x - (a + 3)) * x * x + 1;
        if (x < 2) return (((x - 5) * x + 8) * x - 4) * a;
        return 0.0;
    };
    for (int o = 0; o < out; ++o) {
        const double centre = (o + .5) * scale;
        const int x0 = std::max(int(centre - support + .5), 0), x1 = std::min(int(centre + support + .5), in);
        double total = 0;
        for (int x = x0; x < x1; ++x) total += c.weights[size_t(o) * c.taps + (x - x0)] = cubic((x - centre + .5) / filterscale);
        for (int x = x0; x < x1; ++x) if (total) c.weights[size_t(o) * c.taps + (x - x0)] /= total;
        c.start[o] = x0; c.size[o] = x1 - x0;
    }
    return c;
}
Plane resize(const Plane& in, int w, int h, int W, int H) {
    const Coeffs cx = bicubic(w, W), cy = bicubic(h, H);
    Plane mid(size_t(W) * h), out(size_t(W) * H);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < W; ++x) {
            double v = 0;
            for (int k = 0; k < cx.size[x]; ++k) v += in[size_t(y) * w + cx.start[x] + k] * cx.weights[size_t(x) * cx.taps + k];
            mid[size_t(y) * W + x] = float(v);
        }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double v = 0;
            for (int k = 0; k < cy.size[y]; ++k) v += mid[size_t(cy.start[y] + k) * W + x] * cy.weights[size_t(y) * cy.taps + k];
            out[size_t(y) * W + x] = float(v);
        }
    return out;
}
// Pillow's GaussianBlur: three extended box passes per axis on 8-bit values.
void box_blur(std::vector<uint8_t>& v, int w, int h, double radius) {
    const double sigma2 = radius * radius / 3, L = std::sqrt(12 * sigma2 + 1);
    const double l = std::floor((L - 1) / 2);
    const double box = l + (2 * l + 1) * (l * (l + 1) - 3 * sigma2) / (6 * (sigma2 - (l + 1) * (l + 1)));
    const int whole = int(box);
    const double part = box - whole, norm = 2 * box + 1;
    std::vector<uint8_t> line;
    const auto pass = [&](int count, int length, auto get, auto set) {
        line.resize(size_t(length));
        for (int i = 0; i < count; ++i) {
            for (int k = 0; k < length; ++k) line[size_t(k)] = get(i, k);
            for (int k = 0; k < length; ++k) {
                double sum = 0;
                for (int j = -whole; j <= whole; ++j) sum += line[size_t(std::clamp(k + j, 0, length - 1))];
                sum += part * (line[size_t(std::clamp(k - whole - 1, 0, length - 1))] + line[size_t(std::clamp(k + whole + 1, 0, length - 1))]);
                set(i, k, uint8_t(std::clamp(std::lround(sum / norm), 0L, 255L)));
            }
        }
    };
    for (int n = 0; n < 3; ++n)
        pass(h, w, [&](int y, int x) { return v[size_t(y) * w + x]; }, [&](int y, int x, uint8_t o) { v[size_t(y) * w + x] = o; });
    for (int n = 0; n < 3; ++n)
        pass(w, h, [&](int x, int y) { return v[size_t(y) * w + x]; }, [&](int x, int y, uint8_t o) { v[size_t(y) * w + x] = o; });
}
// flat_scene_hd._smooth: bicubic up, to 8 bits, Gaussian, back to [0, 1].
Plane smooth(const Plane& layer, int w, int h, int W, int H, double radius) {
    const Plane big = resize(layer, w, h, W, H);
    std::vector<uint8_t> bytes(big.size());
    for (size_t i = 0; i < big.size(); ++i) bytes[i] = uint8_t(std::clamp(std::lround(big[i] * 255), 0L, 255L));
    box_blur(bytes, W, H, radius);
    Plane out(bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i) out[i] = bytes[i] / 255.f;
    return out;
}
}

const std::vector<FlatSpec>& flat_scenes() {
    static const std::vector<FlatSpec> list = [] { std::vector<FlatSpec> out; for (const auto& f : kFlat) out.push_back(f.spec); return out; }();
    return list;
}

RgbaImage flat_image(const FlatSpec& spec) {
    RgbaImage result;
    const auto def = std::find_if(kFlat.begin(), kFlat.end(), [&](const FlatDef& f) { return f.spec.scene == spec.scene; });
    int w = 0, h = 0;
    std::vector<uint8_t> source;
    if (def == kFlat.end() || !ready() || !flat_frame(def->spec, w, h, source)) return result;
    std::vector<Key> keys;
    for (const auto& group : def->layers) for (const auto& k : group) keys.push_back({k.r / 255, k.g / 255, k.b / 255});
    // memberships: per key colour (and transparency last), how much of each pixel it covers.
    std::vector<Plane> m(keys.size() + 1, Plane(size_t(w) * h));
    for (size_t p = 0; p < size_t(w) * h; ++p) {
        const float r = source[p * 4] / 255.f, g = source[p * 4 + 1] / 255.f, b = source[p * 4 + 2] / 255.f, a = source[p * 4 + 3] / 255.f;
        m.back()[p] = 1 - a;
        if (a == 0) continue;
        float best = 1e30f; size_t bi = 0, bj = 0; float bt = 0;
        for (size_t i = 0; i < keys.size(); ++i)
            for (size_t j = i; j < keys.size(); ++j) {
                const float d[3] = {keys[j].r - keys[i].r, keys[j].g - keys[i].g, keys[j].b - keys[i].b};
                const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                const float t = dd == 0 ? 0 : std::clamp(((r - keys[i].r) * d[0] + (g - keys[i].g) * d[1] + (b - keys[i].b) * d[2]) / dd, 0.f, 1.f);
                const float e0 = r - (keys[i].r + t * d[0]), e1 = g - (keys[i].g + t * d[1]), e2 = b - (keys[i].b + t * d[2]);
                const float err = e0 * e0 + e1 * e1 + e2 * e2;
                if (err < best) { best = err; bi = i; bj = j; bt = t; }
            }
        m[bi][p] += a * (1 - bt);
        m[bj][p] += a * bt;
    }
    const int W = w * kFlatScale, H = h * kFlatScale;
    const double radius = kBlur * kFlatScale;
    std::vector<Plane> covers;
    std::vector<std::array<Plane, 3>> colours;
    size_t start = 0;
    for (const auto& group : def->layers) {
        Plane total(size_t(w) * h);
        for (size_t i = start; i < start + group.size(); ++i) for (size_t p = 0; p < total.size(); ++p) total[p] += m[i][p];
        covers.push_back(smooth(total, w, h, W, H, radius));
        std::array<Plane, 3> rgb;
        for (int c = 0; c < 3; ++c) {
            Plane mix(size_t(w) * h);
            for (size_t i = start; i < start + group.size(); ++i) {
                const float key = c == 0 ? keys[i].r : c == 1 ? keys[i].g : keys[i].b;
                for (size_t p = 0; p < mix.size(); ++p) mix[p] += m[i][p] * key;
            }
            rgb[c] = smooth(mix, w, h, W, H, radius);
            for (size_t p = 0; p < rgb[c].size(); ++p) rgb[c][p] /= std::max(covers.back()[p], 1e-4f);
        }
        colours.push_back(std::move(rgb));
        start += group.size();
    }
    covers.push_back(smooth(m.back(), w, h, W, H, radius));
    const float gain = float(kSharp * kFlatScale);
    const size_t n = size_t(W) * H;
    result.width = W; result.height = H; result.rgba.assign(n * 4, 0);
    std::vector<float> weight(covers.size());
    for (size_t p = 0; p < n; ++p) {
        float total = 0, opaque = 0;
        for (size_t i = 0; i < covers.size(); ++i) {
            float top = -1e30f;
            for (size_t j = 0; j < covers.size(); ++j) if (j != i) top = std::max(top, covers[j][p]);
            weight[i] = std::clamp((covers[i][p] - top) * gain + .5f, 0.f, 1.f);
            total += weight[i];
            if (i + 1 < covers.size()) opaque += weight[i];
        }
        result.rgba[p * 4 + 3] = uint8_t(std::lround(std::min(opaque / std::max(total, 1e-6f), 1.f) * 255));
        for (int c = 0; c < 3; ++c) {
            float mix = 0;
            for (size_t i = 0; i + 1 < covers.size(); ++i) mix += weight[i] * colours[i][c][p];
            result.rgba[p * 4 + c] = uint8_t(std::lround(std::clamp(mix / std::max(opaque, 1e-4f), 0.f, 1.f) * 255));
        }
    }
    return result;
}
}
