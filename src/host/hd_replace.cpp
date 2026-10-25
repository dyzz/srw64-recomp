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

namespace srw64::hd_replace {
namespace {
struct Entry {
    std::filesystem::path file;
    bool icon = false;          // a map unit icon: drawn only with the HD icon preference
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

bool load(Entry& e) {
    glGenTextures(1, &e.id);
    glBindTexture(GL_TEXTURE_2D, e.id);
    // The Switch's prepared copy (straight alpha, its mips; tools/release/switch/prepare-hd),
    // else the PNG as RT64 reads it.
    hd_texture::Compressed c;
    if (hd_texture::load(e.file, c)) {
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
    try {
        const auto image = presentation::load_rgba(e.file);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        e.width = uint16_t(image.width); e.height = uint16_t(image.height); e.bytes = image.pixels.size();
        return true;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "SRW64_HD_REPLACE_FAILED %s\n", error.what());
    }
    glDeleteTextures(1, &e.id);
    e.id = 0;
    return false;
}
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
        if (!load(e)) { e.failed = true; return 0; }
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

void end_frame() {
    ++frame;
    if (held <= kBudget) return;
    for (auto& [key, e] : entries)
        if (e.id && frame - e.used > kIdleFrames) {
            glDeleteTextures(1, &e.id);
            e.id = 0;
            held -= e.bytes;
        }
}
}  // namespace srw64::hd_replace
