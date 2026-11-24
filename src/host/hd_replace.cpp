#include "hd_replace.hpp"
#include "app/runtime.hpp"
#include "hd_texture.hpp"
#include "presentation/image_mode.hpp"
#include "presentation/rgba_file.hpp"
#ifdef SRW64_NATIVE_DIALOGUE
#include "presentation_settings.hpp"
#endif
#include "json/json.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>

#include "../gl/gl_api.hpp"

#ifdef __SWITCH__
#include "../gl/display_list.hpp"
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>
extern "C" void srw64_switch_background_thread();   // host/switch/switch_platform.cpp
#endif

namespace srw64::hd_replace {
namespace {
struct Entry {
    std::filesystem::path file;
    bool icon = false;          // a map unit icon: drawn only with the HD icon preference
#ifdef __SWITCH__
    bool battle = false, border = false;
#endif
    GLuint id = 0;
    uint16_t width = 0, height = 0;
    size_t bytes = 0;
    uint64_t used = 0;
    uint32_t mode = ~0u;        // the sampler state last set
    bool failed = false;
};
std::unordered_map<uint64_t, Entry> entries;   // fixed after configure
uint64_t frame = 0;
size_t held = 0;
#ifdef __SWITCH__
constexpr size_t kBudget = size_t(96) << 20;   // BC7: some 300 of the battle's 512 x 512 grounds
#else
constexpr size_t kBudget = size_t(384) << 20;
#endif
constexpr uint64_t kIdleFrames = 600;

bool upload(Entry& e, const hd_texture::Compressed& c) {
    const uint8_t* at = c.data->data();
    uint32_t w = c.width, h = c.height;
    for (uint32_t level = 0; level < c.levels; ++level, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
        const size_t size = hd_texture::level_bytes(c.format, w, h);
        if (c.format == 0x8058) {   // GL_RGBA8: uncompressed pixel art
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, GLint(level), GL_RGBA, GLsizei(w), GLsizei(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, at);
        } else {
            glCompressedTexImage2D(GL_TEXTURE_2D, GLint(level), c.format, GLsizei(w), GLsizei(h), 0, GLsizei(size), at);
        }
        at += size;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(c.levels - 1));
    e.width = uint16_t(c.width); e.height = uint16_t(c.height); e.bytes = c.data->size();
    return true;
}

bool upload(Entry& e, const presentation::RgbaFile& image) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    e.width = uint16_t(image.width); e.height = uint16_t(image.height); e.bytes = image.pixels.size();
    return true;
}

#ifdef __SWITCH__
// A picture read and unpacked, not yet uploaded: the Switch's prepared copy (straight alpha,
// its mips; tools/release/switch/prepare-hd), else the PNG as RT64 reads it. Any thread.
struct Decoded {
    hd_texture::Compressed c;
    presentation::RgbaFile rgba;
    bool prepared = false, good = false;
};
std::unique_ptr<Decoded> decode(const std::filesystem::path& file) {
    auto d = std::make_unique<Decoded>();
    try {
        if (hd_texture::load(file, d->c)) {
            d->prepared = d->good = true;
            return d;
        }
        d->rgba = presentation::load_rgba(file);
        d->good = true;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "SRW64_HD_REPLACE_FAILED %s\n", error.what());
    }
    return d;
}
size_t decoded_bytes(const Decoded& d) {
    return d.prepared ? d.c.data->size() : d.rgba.pixels.size();
}

// Only the warm worker shares this cache with the render thread. A demand read
// claims its key first, or waits for the one read already in flight for that key.
std::mutex ready_mutex;
std::condition_variable decoded;
std::unordered_map<uint64_t, std::unique_ptr<Decoded>> ready;
std::unordered_set<uint64_t> demanded;
uint64_t in_flight = 0;
std::atomic<bool> stopping{false};
std::future<void> warmer, writer;
constexpr size_t kWarmBudget = 16u << 20;
constexpr size_t kHistoryLimit = 128;
std::unordered_map<uint64_t, std::unique_ptr<Decoded>> batch;
std::filesystem::path warm_file;
std::vector<uint64_t> history;
bool dirty = false;
unsigned warm_hits = 0;

std::unique_ptr<Decoded> demand(uint64_t key, const Entry& entry) {
    if (key && (entry.battle || entry.border) && !warm_file.empty()) {
        std::unique_lock lock(ready_mutex);
        demanded.insert(key);
        decoded.wait(lock, [key] { return in_flight != key; });
        if (auto cached = ready.extract(key)) {
            ++warm_hits;
            return std::move(cached.mapped());
        }
    }
    return decode(entry.file);
}

void save_history() {
    if (!dirty || (writer.valid() && writer.wait_for(std::chrono::seconds(0)) != std::future_status::ready)) return;
    std::string text;
    for (auto key : history) {
        char line[24];
        std::snprintf(line, sizeof line, "%016llx\n", (unsigned long long)key);
        text += line;
    }
    writer = std::async(std::launch::async, [file = warm_file, text = std::move(text)] {
        try { app::atomic_write(file, text); }
        catch (const std::exception& error) { std::fprintf(stderr, "SRW64_HD_HISTORY %s\n", error.what()); }
    });
    dirty = false;
}

bool load(Entry& e, std::unique_ptr<Decoded> d) {
    if (!d) d = decode(e.file);
    if (!d->good) return false;
    glGenTextures(1, &e.id);
    glBindTexture(GL_TEXTURE_2D, e.id);
    return d->prepared ? upload(e, d->c) : upload(e, d->rgba);
}
#else
bool load(Entry& e) {
    glGenTextures(1, &e.id);
    glBindTexture(GL_TEXTURE_2D, e.id);
    // The Switch's prepared copy (straight alpha, its mips; tools/release/switch/prepare-hd),
    // else the PNG as RT64 reads it.
    hd_texture::Compressed c;
    if (hd_texture::load(e.file, c)) {
        return upload(e, c);
    }
    try {
        const auto image = presentation::load_rgba(e.file);
        return upload(e, image);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "SRW64_HD_REPLACE_FAILED %s\n", error.what());
    }
    glDeleteTextures(1, &e.id);
    e.id = 0;
    return false;
}
#endif
}  // namespace

void configure(const std::filesystem::path& art) {
    std::error_code missing;
    if (!std::filesystem::is_regular_file(art / "rt64.json", missing)) return;
    // Read whole: a stream parse went a character at a time, a second on the Switch.
    const auto database = nlohmann::json::parse(app::read_text(art / "rt64.json", 64u << 20));
    if (database.at("configuration").value("hashVersion", 0) != 5) {
        std::fprintf(stderr, "SRW64_HD_REPLACE unsupported hash version\n");
        return;
    }
    size_t icons = 0;
    for (const auto& row : database.at("textures")) {
        Entry e;
        e.file = art / row.at("path").get<std::string>();
        e.icon = row.value("kind", "") == "icon";
#ifdef __SWITCH__
        e.battle = row.value("kind", "") == "battle";
        e.border = row.value("kind", "") == "frame";
#endif
        icons += e.icon;
        entries[std::stoull(row.at("hashes").at("rt64").get<std::string>(), nullptr, 16)] = std::move(e);
    }
    std::fprintf(stderr, "SRW64_HD_REPLACE %zu picture(s), %zu map icon(s)\n", entries.size(), icons);
}

bool has(uint64_t key) { return entries.count(key) != 0; }

uint32_t texture(uint64_t key, uint8_t wrap_s, uint8_t wrap_t, bool linear, uint16_t& width, uint16_t& height) {
    const auto found = entries.find(key);
    if (found == entries.end()) return 0;
    Entry& e = found->second;
    if (presentation::image_mode.enabled() && presentation::image_mode.current() != 1) return 0;
#ifdef SRW64_NATIVE_DIALOGUE
    if (e.icon && !settings::hd_map_unit_icons()) return 0;
#endif
    if (!e.id) {
        if (e.failed) return 0;
#ifdef __SWITCH__
        auto early = batch.extract(key);
        auto picture = early ? std::move(early.mapped()) : (warm_file.empty() ? nullptr : demand(key, e));
        if (!load(e, std::move(picture))) { e.failed = true; return 0; }
        if (e.battle && !warm_file.empty()) {
            const auto previous = std::find(history.begin(), history.end(), key);
            if (previous != history.end()) history.erase(previous);
            history.insert(history.begin(), key);
            if (history.size() > kHistoryLimit) history.pop_back();
            dirty = true;
        }
#else
        if (!load(e)) { e.failed = true; return 0; }
#endif
        held += e.bytes;
        static size_t loads = 0;
        if (++loads <= 16 || loads % 100 == 0)
            std::fprintf(stderr, "SRW64_HD_REPLACE load %zu %016llx %s %ux%u held=%zu KiB\n", loads, (unsigned long long)key,
                         e.file.filename().string().c_str(), e.width, e.height, held >> 10);
        e.mode = ~0u;
    }
    if (const uint32_t mode = wrap_s | wrap_t << 2 | uint32_t(linear) << 4; mode != e.mode) {
        e.mode = mode;
        glBindTexture(GL_TEXTURE_2D, e.id);
        auto wrap = [](uint8_t m) { return GLint(m == 1 ? GL_REPEAT : m == 2 ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE); };
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap(wrap_s));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap(wrap_t));
    }
    e.used = frame;
    width = e.width; height = e.height;
    return e.id;
}

#ifdef __SWITCH__
void prepare(const n64gl::Frame& list) {
    if (list.textures.size() < 2 || (presentation::image_mode.enabled() && presentation::image_mode.current() != 1)) return;
    struct Item { uint64_t key; const Entry* entry; std::unique_ptr<Decoded> picture; };
    std::array<Item, 24> items;
    size_t count = 0;
    for (const auto& tile : list.textures) {
        if (!tile.replacement) continue;
        const auto found = entries.find(tile.replacement);
        if (found == entries.end()) continue;
        const auto& e = found->second;
        if ((!e.battle && !e.border) || e.id || e.failed) continue;
        if (std::any_of(items.begin(), items.begin() + count, [&](const Item& item) { return item.key == tile.replacement; })) continue;
        items[count++] = {tile.replacement, &e, {}};
        if (count == items.size()) break;
    }
    if (count < 2) return;
    const auto start = std::chrono::steady_clock::now();
    const size_t total = count;
    {
        std::lock_guard lock(ready_mutex);
        // Cached pictures need neither a helper thread nor another decode.
        for (size_t i = 0; i < count;) {
            if (auto cached = ready.extract(items[i].key)) {
                batch.emplace(items[i].key, std::move(cached.mapped()));
                ++warm_hits;
                --count;
                if (i < count) items[i] = std::move(items[count]);
            } else {
                ++i;
            }
        }
    }
    std::atomic<size_t> next{0};
    const auto work = [&] {
        for (size_t i; (i = next.fetch_add(1)) < count;) {
            auto& item = items[i];
            item.picture = demand(item.key, *item.entry);
        }
    };
    std::array<std::thread, 2> helpers;
    try {
        for (size_t i = 0; i < helpers.size() && i + 1 < count; ++i) helpers[i] = std::thread(work);
    } catch (const std::system_error&) {}  // remaining work runs on the render thread
    work();
    for (auto& thread : helpers) if (thread.joinable()) thread.join();
    for (size_t i = 0; i < count; ++i) batch.emplace(items[i].key, std::move(items[i].picture));
    std::fprintf(stderr, "SRW64_HD_BATCH %zu pictures, %zu ready, %.1f ms, cumulative warm hits %u\n", total, total - count,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), warm_hits);
}

void warm(const std::filesystem::path& file) {
    warm_file = file;
    std::vector<uint64_t> keys;
    for (const auto& [key, e] : entries) if (e.border && key) keys.push_back(key);
    std::ifstream listed(file);
    for (std::string line; history.size() < kHistoryLimit && std::getline(listed, line);) {
        uint64_t key = 0;
        if (line.size() != 16) continue;
        const auto result = std::from_chars(line.data(), line.data() + line.size(), key, 16);
        if (result.ec != std::errc{} || result.ptr != line.data() + line.size() || !key) continue;
        const auto found = entries.find(key);
        if (found == entries.end() || !found->second.battle || std::find(history.begin(), history.end(), key) != history.end()) continue;
        history.push_back(key);
    }
    keys.insert(keys.end(), history.begin(), history.end());
    stopping = false;
    warmer = std::async(std::launch::async, [keys = std::move(keys)] {
#ifdef __SWITCH__
        srw64_switch_background_thread();
#endif
        size_t bytes = 0, read = 0;
        for (auto key : keys) {
            {
                std::lock_guard lock(ready_mutex);
                if (stopping) break;
                if (demanded.count(key)) continue;
                in_flight = key;
            }
            auto d = decode(entries.at(key).file);
            const size_t size = decoded_bytes(*d);
            {
                std::lock_guard lock(ready_mutex);
                in_flight = 0;
                if (d->good && size <= kWarmBudget - bytes) {
                    ready.emplace(key, std::move(d));
                    bytes += size;
                    ++read;
                }
            }
            decoded.notify_all();
            if (bytes == kWarmBudget || (d && d->good)) break;  // next picture exceeded the remaining budget
        }
        std::fprintf(stderr, "SRW64_HD_WARM %zu/%zu pictures %zu KiB\n", read, keys.size(), bytes >> 10);
    });
}

void stop() {
    stopping = true;
    if (warmer.valid()) warmer.get();
    if (writer.valid()) writer.get();
    save_history();
    if (writer.valid()) writer.get();
    ready.clear();
    demanded.clear();
    history.clear();
    warm_file.clear();
    warm_hits = 0;
}

#endif
void end_frame() {
    ++frame;
#ifdef __SWITCH__
    batch.clear();  // a decoded tile can be skipped by the renderer
    if (dirty && frame % 120 == 0) save_history();
#endif
    if (held <= kBudget) return;
    for (auto& [key, e] : entries)
        if (e.id && frame - e.used > kIdleFrames) {
            glDeleteTextures(1, &e.id);
            e.id = 0;
            held -= e.bytes;
        }
}
}  // namespace srw64::hd_replace
