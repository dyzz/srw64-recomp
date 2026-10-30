#define HLSL_CPU
#include "native_icon.hpp"
#ifndef SRW64_GL_ONLY
#include "hle/rt64_state.h"
#include "rhi/rt64_render_hooks.h"
#endif
#include "guest_memory.hpp"
#include "mod_library.hpp"
#include "native_gpu.hpp"
#include "native_quads.hpp"
#include "presentation/image_mode.hpp"
#include "unit_icon_art.hpp"
#include "unit_icon_facing.hpp"
#include "unit_shadow.hpp"
#include "upgrade_rules.hpp"
#include "wide_map.hpp"
#include "json/json.hpp"
#include "stb/stb_image.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace srw64::unit_icons {
namespace {
using Clock = std::chrono::steady_clock;

// A G_NOOP carrying this tag immediately precedes the marker rectangle.
constexpr uint32_t kTagW0 = 0x00534943;      // G_NOOP, "SIC"
constexpr uint32_t kTagW1 = 0x49430000;      // "IC" + 16-bit draw id
constexpr uint32_t kIdBase = 0x49430000;     // native draw ids handed to RT64
constexpr size_t kRing = 1024;
constexpr size_t kResident = 256;            // textures kept before idle ones are released
constexpr auto kIdle = std::chrono::seconds(20);

// One variant of a package picture. Its pixels are made from the shared master when it is
// drawn and dropped once uploaded, so only the masters stay (64 KiB each).
struct Asset {
    std::shared_ptr<const std::vector<uint8_t>> master;
    unsigned variant = 0;
    std::vector<std::vector<uint8_t>> levels;   // premultiplied RGBA8 mip chain, dropped after upload
#ifndef SRW64_GL_ONLY
    std::unique_ptr<gpu::Texture> texture;
    std::unique_ptr<plume::RenderDescriptorSet> set;
#endif
    std::shared_ptr<const std::vector<uint8_t>> gl_pixels;   // the GL renderer's level 0
    Clock::time_point used{};
};
// One package picture: its four variants (unit_icon_art::marked), by palette from 1010.
struct Icon {
    std::filesystem::path file;
    std::filesystem::file_time_type stamp{};
    std::array<int, 4> assets{-1, -1, -1, -1};
    char facing = 0;                            // 'L', 'R', 'F' from art/icons.json; 0: the original's
};
struct Draw {
    uint32_t id = 0;
    int asset = -1;
    float rect[4]{}, uv[4]{};
    float view_left = -1;                       // the widened map view's x = 0 (wide_map.hpp)
    float shadow = 0;                           // > 0: drawn black at this alpha, a silhouette (unit_shadow.hpp)
};

// Reserved once and never reallocated: draws on other threads index it while pictures join
// at the end. A replaced or new file adds four; kCapacity bounds a long session's changes.
constexpr size_t kCapacity = 4096;
std::vector<Asset> assets = [] { std::vector<Asset> v; v.reserve(kCapacity); return v; }();
std::map<unsigned, Icon> icons;                 // icon resource (688-1009) -> picture
std::mutex mod_mutex;                           // icons and the revision, game thread
std::mutex asset_mutex;                         // levels and textures cross threads
std::optional<unsigned> mod_revision;
mods::Version mod_game{-1, -1, -1};
bool hooks_installed = false;
std::array<Draw, kRing> ring;
std::mutex ring_mutex;
uint32_t next_id = 1;
std::atomic<uint64_t> rewritten{}, rendered{}, skipped{};
int shadow_asset = -1;                          // the HD shadow (shadow_master), asset_mutex

// The soft shadow the HD images draw under every map unit (unit_shadow.hpp): the original's
// ellipse (columns 1-14, rows 10-15 of its 16x16) at 8x, black at the original's 0xB4 alpha
// with an edge faded over the outer third. Made here: the HD pack carries no shadow.
std::shared_ptr<const std::vector<uint8_t>> shadow_master() {
    const int n = unit_icon_art::kSize;
    std::vector<uint8_t> rgba(size_t(n) * n * 4, 0);
    const float cx = 64.f, cy = 102.f, rx = 60.f, ry = 26.f, alpha = 0xB4;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float dx = (x + .5f - cx) / rx, dy = (y + .5f - cy) / ry;
            const float t = std::clamp((1.f - std::sqrt(dx * dx + dy * dy)) / 0.35f, 0.f, 1.f);
            rgba[(size_t(y) * n + x) * 4 + 3] = uint8_t(alpha * t * t * (3.f - 2.f * t) + .5f);
        }
    return std::make_shared<const std::vector<uint8_t>>(std::move(rgba));
}
#ifndef SRW64_GL_ONLY
RT64::NativeMeshClassify* previous_classify{};
RT64::NativeMeshRender* previous_render{};
std::unique_ptr<gpu::Program> program;          // the portraits' HdPortrait{VS,PS}: rect, uv, resolution, fill
#endif

// A package's art/icons.json: {"schema": "srw64.icons.v1", "facing": {"<unit>": "left" |
// "right" | "front"}}, the way each of its pictures looks when not the original icon's.
std::map<unsigned, char> facings(const std::filesystem::path& folder, const std::string& id, std::vector<std::string>& problems) {
    std::map<unsigned, char> out;
    std::error_code error;
    const auto path = folder / "art" / "icons.json";
    if (!std::filesystem::is_regular_file(path, error)) return out;
    std::ifstream in(path, std::ios::binary);
    const auto doc = nlohmann::json::parse(in, nullptr, false);
    const std::string name = id + " art/icons.json";
    if (doc.is_discarded() || !doc.is_object() || doc.value("schema", "") != "srw64.icons.v1" || !doc.contains("facing") || !doc["facing"].is_object()) {
        problems.push_back(name + "：要是 {\"schema\": \"srw64.icons.v1\", \"facing\": {…}}");
        return out;
    }
    for (const auto& [key, value] : doc["facing"].items()) {
        const bool number = !key.empty() && key.size() <= 3 && std::all_of(key.begin(), key.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
                            !(key.size() > 1 && key[0] == '0');
        const std::string way = value.is_string() ? value.get<std::string>() : "";
        if (!number || std::stoul(key) >= unit_icon_art::kUnits || (way != "left" && way != "right" && way != "front")) {
            problems.push_back(name + "：" + key + " 要是机体号，值是 left、right 或 front");
            continue;
        }
        out[unsigned(std::stoul(key))] = way == "left" ? 'L' : way == "right" ? 'R' : 'F';
    }
    return out;
}

// The packages' art/icons/<unit>.png again when they change. Each unit's icon resource is in
// the ROM's table; units that share an icon share the picture, and the upper package wins.
void refresh() {
    const unsigned now = mods::revision();
    const auto game = mods::game_version();
    if (mod_revision == now && game == mod_game) return;
    mod_revision = now; mod_game = game;
    std::map<unsigned, std::filesystem::path> files;
    std::map<unsigned, char> facing;            // icon -> the way its picture looks, when said
    std::vector<std::string> problems;
    std::error_code error;
    for (const auto& p : mods::installed(game)) {
        const auto dir = p.folder / "art" / "icons";
        if (!p.active || !std::filesystem::is_directory(dir, error)) continue;
        std::map<unsigned, unsigned> mine;      // icon -> the unit this package gave it for
        const auto ways = facings(p.folder, p.id(), problems);
        for (const auto& item : std::filesystem::directory_iterator(dir, error)) {
            const auto stem = item.path().stem().string(), name = p.id() + " art/icons/" + item.path().filename().string();
            auto ext = item.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (ext != ".png") continue;
            const bool number = !stem.empty() && stem.size() <= 3 && std::all_of(stem.begin(), stem.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
                                !(stem.size() > 1 && stem[0] == '0');
            const unsigned unit = number ? unsigned(std::stoul(stem)) : unit_icon_art::kUnits;
            const unsigned icon = unit < unit_icon_art::kUnits ? upgrades::rom_read(unit_icon_art::kIconTable + unit * 2, 2) : 0;
            if (icon < unit_icon_facing::kFirstIcon || icon > unit_icon_facing::kLastIcon) { problems.push_back(name + "：文件名要是有地图图标的机体号"); continue; }
            int w = 0, h = 0, n = 0;
            if (!stbi_info(item.path().string().c_str(), &w, &h, &n) || !unit_icon_art::size_ok(w, h)) {
                problems.push_back(name + "：要是 16、32、64 或 128 像素的正方形 PNG");
                continue;
            }
            if (const auto other = mine.find(icon); other != mine.end()) {
                problems.push_back(name + "：和 " + std::to_string(other->second) + " 号机体共用同一个图标，只用一张");
                continue;
            }
            mine[icon] = unit;
            files[icon] = item.path();
            if (const auto way = ways.find(unit); way != ways.end()) facing[icon] = way->second;
            else facing.erase(icon);
        }
    }
    std::map<unsigned, Icon> table;
    {
        std::lock_guard lock(asset_mutex);
        for (const auto& [icon, file] : files) {
            Icon entry{file, std::filesystem::last_write_time(file, error)};
            entry.facing = facing.count(icon) ? facing[icon] : 0;
            // A picture already made (same file and time) is used again.
            if (const auto old = icons.find(icon); old != icons.end() && old->second.file == file && old->second.stamp == entry.stamp) {
                table[icon] = old->second;
                table[icon].facing = entry.facing;
                continue;
            }
            if (assets.size() + 4 > kCapacity) { problems.push_back("too many MOD icons"); break; }
            int w = 0, h = 0, n = 0;
            stbi_uc* pixels = stbi_load(file.string().c_str(), &w, &h, &n, 4);
            if (!pixels || !unit_icon_art::size_ok(w, h)) {
                if (pixels) stbi_image_free(pixels);
                problems.push_back(file.filename().string() + "：读不出来");
                continue;
            }
            const auto master = std::make_shared<const std::vector<uint8_t>>(unit_icon_art::master(pixels, w));
            stbi_image_free(pixels);
            for (unsigned v = 0; v < 4; ++v) {
                Asset asset;
                asset.master = master;
                asset.variant = v;
                entry.assets[v] = int(assets.size());
                assets.push_back(std::move(asset));
            }
            table[icon] = entry;
        }
    }
    icons = std::move(table);
    for (const auto& problem : problems) fprintf(stderr, "SRW64_MOD_ICONS_PROBLEM %s\n", problem.c_str());
    fprintf(stderr, "SRW64_MOD_ICONS icons=%zu problems=%zu\n", icons.size(), problems.size());
}

// Game thread, asset_mutex held: the variant's pixels for a draw whose texture is not there.
void prepare(Asset& asset) {
#ifdef SRW64_GL_ONLY
    if (asset.gl_pixels) return;
    asset.gl_pixels = std::make_shared<const std::vector<uint8_t>>(std::move(unit_icon_art::levels(unit_icon_art::marked(*asset.master, asset.variant)).front()));
#else
    if (asset.set || !asset.levels.empty()) return;
    asset.levels = unit_icon_art::levels(unit_icon_art::marked(*asset.master, asset.variant));
#endif
}

#ifdef SRW64_GL_ONLY
bool gl_quads(uint32_t id, std::vector<native_quads::Quad>& out) {
    if ((id & 0xFFFF0000u) != kIdBase) return false;
    Draw draw;
    {
        std::lock_guard lock(ring_mutex);
        draw = ring[id % kRing];
    }
    if (draw.id != (id & 0xFFFF) || draw.asset < 0) return true;
    std::lock_guard lock(asset_mutex);
    Asset& asset = assets[size_t(draw.asset)];
    if (!asset.gl_pixels) return true;
    asset.used = Clock::now();
    native_quads::Quad q;
    q.pixels = asset.gl_pixels;
    q.width = q.height = uint32_t(unit_icon_art::kSize);
    std::copy(draw.rect, draw.rect + 4, q.rect);
    std::copy(draw.uv, draw.uv + 4, q.uv);
    q.left = draw.view_left;
    if (draw.shadow > 0) { q.fill[3] = 1; q.color[3] = draw.shadow; }   // a black silhouette
    q.label = draw.shadow > 0 ? "unit shadow" : "unit icon";
    out.push_back(std::move(q));
    ++rendered;
    return true;
}

#else
uint32_t classify(RT64::State* state, const RT64::DisplayList* dl) {
    const RT64::DisplayList& tag = dl[-1];
    if (tag.w0 == kTagW0 && (tag.w1 & 0xFFFF0000u) == kTagW1) return kIdBase | (tag.w1 & 0xFFFF);
    return previous_classify ? previous_classify(state, dl) : 0;
}

// Workload thread. Upload on first use; release textures idle long enough that no recorded
// work can still read them (gpu::Texture also waits before freeing).
plume::RenderDescriptorSet* texture_for(Asset& asset, plume::RenderCommandList* list) {
    std::lock_guard lock(asset_mutex);
    const auto now = Clock::now();
    if (!asset.set) {
        if (asset.levels.empty()) return nullptr;
        size_t resident = 0;
        for (auto& other : assets) resident += other.set != nullptr;
        if (resident >= kResident)
            for (auto& other : assets)
                if (other.set && now - other.used > kIdle) { other.set.reset(); other.texture.reset(); }
        asset.texture = std::make_unique<gpu::Texture>(uint32_t(unit_icon_art::kSize), uint32_t(unit_icon_art::kSize),
                                                       plume::RenderFormat::R8G8B8A8_UNORM, std::move(asset.levels));
        asset.levels.clear();
        asset.levels.shrink_to_fit();
        if (!asset.texture->upload(list)) { asset.texture.reset(); return nullptr; }
        asset.set = program->bind({asset.texture.get()});
    }
    asset.used = now;
    return asset.set.get();
}

bool render(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer, const RT64::NativeMeshDraw& call) {
    if ((call.id & 0xFFFF0000u) != kIdBase) return previous_render ? previous_render(list, framebuffer, call) : false;
    Draw draw;
    {
        std::lock_guard lock(ring_mutex);
        draw = ring[call.id % kRing];
    }
    if (draw.id != (call.id & 0xFFFF) || draw.asset < 0 || !program) { ++skipped; return true; }
    auto* set = texture_for(assets[size_t(draw.asset)], list);
    if (!set) { ++skipped; return true; }
    struct { float rect[4], uv[4], resolution[4], fill[4]; } u{};
    std::copy(draw.rect, draw.rect + 4, u.rect);
    std::copy(draw.uv, draw.uv + 4, u.uv);
    if (draw.shadow > 0) u.fill[3] = draw.shadow;   // black, at this alpha (HdPortraitPS)
    gpu::frame_resolution(call, u.resolution);
    // In the widened view (wide_map.hpp) the map's x = 0 sits at the view's left edge.
    if (draw.view_left >= 0) u.resolution[3] = draw.view_left;
    gpu::State state;
    state.blend = gpu::Blend::premultiplied;
    if (!program->begin(list, framebuffer, call, state, set, gpu::push_data(&u, sizeof(u)))) { ++skipped; return true; }
    list->drawInstanced(4, 1, 0, 0);
    ++rendered;
    return true;
}
#endif
}

void install() {
    if (hooks_installed) return;
    hooks_installed = true;
#ifdef SRW64_GL_ONLY
    native_quads::add(gl_quads);
#else
    previous_classify = RT64::GetNativeMeshClassify();
    previous_render = RT64::GetNativeMeshRender();
    RT64::SetNativeMeshHooks(classify, render);
#endif
}

void gpu_init() {
#ifndef SRW64_GL_ONLY
    program = std::make_unique<gpu::Program>("HdPortrait", 1, std::vector<gpu::Sampler>{{.linear = true, .mipmaps = true}});
#endif
}

void shutdown() {
    if (rewritten) fprintf(stderr, "SRW64_MOD_ICONS drawn rewritten=%llu rendered=%llu skipped=%llu\n",
                           (unsigned long long)rewritten.load(), (unsigned long long)rendered.load(), (unsigned long long)skipped.load());
    std::lock_guard lock(asset_mutex);
    for (auto& asset : assets) {
#ifndef SRW64_GL_ONLY
        asset.set.reset(); asset.texture.reset();
#endif
        asset.gl_pixels.reset();
    }
#ifndef SRW64_GL_ONLY
    program.reset();
#endif
}

// The texture rectangle in [begin, end) as a marker for a native draw of `asset`, turned
// from the way the picture looks (`drawn`) to the side's.
// The texture rectangle at p (E4, E1, F1) as a marker for a native draw of `asset`, turned from
// the way the picture looks (`drawn`) to the side's; `shadow` > 0 draws it as a black silhouette.
static void mark(uint8_t* rdram, uint32_t p, int asset, char drawn, bool look_left, float shadow) {
    const uint32_t w0 = guest::read(rdram, p, 4), w1 = guest::read(rdram, p + 4, 4);
    Draw record;
    unit_icon_art::Quad q;
    if (!unit_icon_art::quad(w0, w1, guest::read(rdram, p + 12, 4), guest::read(rdram, p + 20, 4), q)) return;
    unit_icon_art::face(q, drawn, look_left);
    record.asset = asset;
    record.shadow = shadow;
    std::copy(q.rect, q.rect + 4, record.rect);
    std::copy(q.uv, q.uv + 4, record.uv);
    record.view_left = wide_map::view_offset();
    {
        std::lock_guard lock(ring_mutex);
        record.id = next_id;
        next_id = next_id % 0xFFFF + 1;
        ring[record.id % kRing] = record;
    }
    // The rectangle stays where it is as the marker, without its texture steps; the command
    // before it (the tile's size, or the inserted shadow's combiner) carries the tag.
    guest::write32(rdram, p + 8, 0xE1000000); guest::write32(rdram, p + 12, 0);
    guest::write32(rdram, p + 16, 0xF1000000); guest::write32(rdram, p + 20, 0x04000400);
    guest::write32(rdram, p - 8, kTagW0); guest::write32(rdram, p - 4, kTagW1 | record.id);
    ++rewritten;
}

// The texture rectangles in [begin, end): the icon's is the last, a ground unit's silhouette
// shadow (unit_shadow.hpp) the one before it.
static std::vector<uint32_t> rectangles(const uint8_t* rdram, uint32_t begin, uint32_t end) {
    std::vector<uint32_t> found;
    for (uint32_t p = begin + 8; p + 24 <= end; p += 8)
        if (guest::read(rdram, p, 1) == 0xE4 && guest::read(rdram, p + 8, 1) == 0xE1 && guest::read(rdram, p + 16, 1) == 0xF1) found.push_back(p);
    return found;
}

void rewrite(uint8_t* rdram, uint32_t slot, uint32_t begin, uint32_t end) {
    if (!hooks_installed || end <= begin || end - begin > 0x400) return;
    int asset = -1;
    char drawn = 'F';
    bool look_left = true;
    {
        std::lock_guard lock(mod_mutex);
        refresh();
        if (icons.empty()) return;
        const uint32_t record = unit_icon_facing::kSubRecords + slot * unit_icon_facing::kSlotSize;
        const uint32_t icon = unit_icon_facing::resource(rdram, record + 0x14);
        const auto found = icons.find(icon);
        if (found == icons.end()) return;
        // 1010-1012 for the player, enemy and third party; any other scheme as drawn. An acted
        // unit keeps its side's palette in the record and is drawn with the grey one (1013).
        const uint32_t palette = unit_icon_facing::resource(rdram, record + 0x16);
        const unsigned variant = unit_icon_facing::acted(rdram, slot) ? unit_icon_art::kActed - unit_icon_art::kFirstPalette :
                                 palette >= unit_icon_art::kFirstPalette && palette <= unit_icon_art::kActed ? palette - unit_icon_art::kFirstPalette : 0;
        asset = found->second.assets[variant];
        // Turned by the way the picture looks, which may not be the original icon's.
        drawn = found->second.facing ? found->second.facing : unit_icon_facing::kFacing[icon - unit_icon_facing::kFirstIcon];
        look_left = unit_icon_facing::looks_left(rdram, slot, palette);
    }
    {
        std::lock_guard lock(asset_mutex);
        prepare(assets[size_t(asset)]);
    }
    const auto found = rectangles(rdram, begin, end);
    if (found.empty()) return;
    if (found.size() > 1) mark(rdram, found[found.size() - 2], asset, drawn, look_left, float(unit_shadow::kGroundAlpha) / 255.f);
    mark(rdram, found.back(), asset, drawn, look_left, 0);
}

void rewrite_shadow(uint8_t* rdram, uint32_t begin, uint32_t end) {
    if (!hooks_installed || end <= begin || end - begin > 0x400) return;
    if (presentation::image_mode.enabled() && presentation::image_mode.current() != 1) return;   // original images: the ROM's ellipse
    int asset = -1;
    {
        std::lock_guard lock(asset_mutex);
        if (shadow_asset < 0) {
            if (assets.size() >= kCapacity) return;
            Asset shadow;
            shadow.master = shadow_master();
            shadow_asset = int(assets.size());
            assets.push_back(std::move(shadow));
        }
        asset = shadow_asset;
        prepare(assets[size_t(asset)]);
    }
    const auto found = rectangles(rdram, begin, end);
    if (!found.empty()) mark(rdram, found.back(), asset, 'F', true, 0);
}
}
