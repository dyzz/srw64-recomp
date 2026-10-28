#include "hd_prepare.hpp"
#include "update_check.hpp"
#include "app/runtime.hpp"
#include "app/sha256.hpp"
#include "json/json.hpp"
#include "miniz.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <ostream>
#include <set>
#include <stdexcept>
#include <vector>

#ifndef SRW64_VERSION
#define SRW64_VERSION "?"
#endif

namespace srw64::hd {
namespace fs = std::filesystem;
using json = nlohmann::json;
namespace {
constexpr const char* pack_schema = "srw64.hd-bundle.v1";
constexpr unsigned max_entries = 50000;

std::string stamp() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}
json read_json(const fs::path& path, size_t limit) {
    return json::parse(app::read_text(path, limit), nullptr, false);
}
// A zip entry's path, forward slashes, no root, no "." or ".." parts; nullopt otherwise.
std::optional<std::string> safe_entry(std::string name) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos) return std::nullopt;
    size_t start = 0;
    while (start <= name.size()) {
        const auto end = name.find('/', start);
        const auto part = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() && end != std::string::npos && end + 1 != name.size() + 1 && start != name.size()) return std::nullopt;
        if (part == "." || part == "..") return std::nullopt;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return name;
}
FILE* open_read(const fs::path& path) {
#ifdef _WIN32
    return _wfopen(path.c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}
struct Entry {
    unsigned index;
    std::string name;  // inside the pack, without the top folder
    bool folder;
    uint64_t size;
};
}

std::optional<std::string> pack_version(const fs::path& folder) {
    std::error_code missing;
    const auto about = folder / "hd.json";
    if (!fs::is_regular_file(about, missing)) return std::nullopt;
    const auto pack = read_json(about, 1u << 20);
    if (!pack.is_object() || pack.value("schema", "") != pack_schema) return std::nullopt;
    return pack.contains("version") && pack["version"].is_string() ? pack["version"].get<std::string>() : std::string();
}

std::string install_pack(const fs::path& zip_path, const fs::path& user_dir, const Progress& progress) {
    FILE* file = open_read(zip_path);
    if (!file) throw std::runtime_error("cannot open " + zip_path.filename().string());
    struct Closer { FILE* f; ~Closer() { std::fclose(f); } } closer{file};
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, file, 0, 0)) throw std::runtime_error(zip_path.filename().string() + " is not a zip file");
    struct End { mz_zip_archive* z; ~End() { mz_zip_reader_end(z); } } end{&zip};
    const unsigned count = mz_zip_reader_get_num_files(&zip);
    if (count > max_entries) throw std::runtime_error("the archive has too many files");
    std::vector<Entry> entries;
    uint64_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) throw std::runtime_error("the archive cannot be read");
        std::string name = stat.m_filename;
        if (name.starts_with("__MACOSX/") || name.ends_with(".DS_Store")) continue;
        const auto safe = safe_entry(name);
        if (!safe) throw std::runtime_error("the archive has an unsafe path: " + name);
        total += stat.m_uncomp_size;
        entries.push_back({i, *safe, bool(stat.m_is_directory) || safe->ends_with('/'), stat.m_uncomp_size});
    }
    // hd.json at the top, or inside the one folder everything sits in (hd/ from build_release.py).
    const auto has = [&](const std::string& name) {
        return std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return !e.folder && e.name == name; });
    };
    std::string prefix;
    if (!has("hd.json")) {
        std::set<std::string> tops;
        for (const auto& e : entries) tops.insert(e.name.substr(0, e.name.find('/')));
        if (tops.size() == 1 && has(*tops.begin() + "/hd.json")) prefix = *tops.begin() + "/";
        else throw std::runtime_error("the archive is not an HD pack (no hd.json)");
    }
    const auto target = user_dir / "hd";
    const auto staging = user_dir / ("hd.installing-" + stamp());
    std::error_code error;
    fs::remove_all(staging, error);
    fs::create_directories(staging, error);
    if (error) throw std::runtime_error("cannot create " + staging.string() + ": " + error.message());
    struct Cleanup {
        fs::path folder;
        bool keep = false;
        ~Cleanup() { if (!keep) { std::error_code ignored; fs::remove_all(folder, ignored); } }
    } cleanup{staging};
    uint64_t done = 0;
    double reported = -1;
    for (const auto& e : entries) {
        if (!e.name.starts_with(prefix)) continue;
        const auto relative = e.name.substr(prefix.size());
        if (relative.empty()) continue;
        const auto out = staging / fs::path(relative);
        if (e.folder) { fs::create_directories(out, error); continue; }
        fs::create_directories(out.parent_path(), error);
        std::ofstream stream(out, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("cannot write " + relative);
        const auto write = [](void* user, mz_uint64, const void* buffer, size_t n) -> size_t {
            static_cast<std::ofstream*>(user)->write(static_cast<const char*>(buffer), std::streamsize(n));
            return n;
        };
        if (!mz_zip_reader_extract_to_callback(&zip, e.index, write, &stream, 0) || !stream) throw std::runtime_error("cannot unpack " + relative);
        stream.close();
        if (!stream) throw std::runtime_error("cannot write " + relative);
        done += e.size;
        const double fraction = total ? double(done) / double(total) : 1.0;
        if (progress && fraction - reported >= 0.01) { progress(fraction, relative); reported = fraction; }
    }
    // What the game checks before it starts in HD (launch.cpp).
    const auto version = pack_version(staging);
    if (!version || !fs::is_regular_file(staging / "art" / "rt64.json", error))
        throw std::runtime_error("the archive is not a complete HD pack for this release");
    // The old pack out of the way, the new one in, then the old one gone. A symlinked
    // hd/ (a test setup) is replaced as a link, what it points to untouched.
    const auto old = user_dir / ("hd.old-" + stamp());
    if (fs::exists(target, error) || fs::is_symlink(target, error)) {
        fs::rename(target, old, error);
        if (error) throw std::runtime_error("cannot move the installed pack aside: " + error.message());
    }
    fs::rename(staging, target, error);
    if (error) {
        std::error_code ignored;
        fs::rename(old, target, ignored);
        throw std::runtime_error("cannot install the pack: " + error.message());
    }
    cleanup.keep = true;
    fs::remove_all(old, error);
    if (progress) progress(1.0, "");
    return *version;
}

int prepare(const fs::path& user_dir, std::ostream& out) {
    double last = -1;
    // One line per half a percent: the stages below are further apart than that.
    const auto report = [&](double fraction, const std::string& text) {
        if (fraction < 1.0 && fraction - last < 0.005) return;
        last = fraction;
        char number[16];
        std::snprintf(number, sizeof number, "%.3f", fraction);
        out << "progress " << number << ' ' << text << '\n' << std::flush;
    };
    const auto mib = [](uint64_t bytes) { return std::to_string((bytes + (1u << 19)) >> 20); };
    try {
        std::error_code error;
        fs::create_directories(user_dir, error);
        if (error) throw std::runtime_error("cannot create " + user_dir.string() + ": " + error.message());
        // One game, import or setup in the user directory at a time.
        std::optional<app::UserLock> lock;
        try {
            lock.emplace(user_dir, std::chrono::seconds(3));
        } catch (const app::PlayLockHeld&) {
            throw std::runtime_error("Marchwind64 is running: close it, then run the setup again");
        }
        report(0.02, "Looking up the newest HD pack");
        std::string url = update::kLatestUrl;
        if (const char* other = std::getenv("SRW64_UPDATE_URL"); other && *other) url = other;  // a test's
        const auto latest = json::parse(update::http_get(url, update::user_agent(SRW64_VERSION), 15), nullptr, false);
        if (!latest.is_object() || latest.value("schema", "") != "srw64.latest.v1") throw std::runtime_error("not a release description: " + url);
        if (!latest.contains("hd") || !latest["hd"].is_object()) {
            report(1.0, "No HD pack is published for this release");
            return 0;
        }
        const auto& hd = latest["hd"];
        const auto version = hd.value("version", ""), name = hd.value("name", ""), pack_url = hd.value("url", ""), sha = hd.value("sha256", "");
        const uint64_t bytes = hd.value("bytes", uint64_t(0));
        if (version.empty() || name.empty() || pack_url.empty() || sha.size() != 64 || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
            throw std::runtime_error("the release description has no usable HD pack");
        // The pack the game would use (launch.cpp's order); the same version means nothing to do.
        for (const auto& folder : {user_dir / "hd", app::beside_game("hd"), app::bundled_resource("hd")}) {
            if (folder.empty() || !fs::exists(folder, error)) continue;
            if (pack_version(folder) == version) {
                report(1.0, "HD " + version + " is installed");
                return 0;
            }
            break;
        }
        const auto downloads = user_dir / "downloads";
        fs::create_directories(downloads, error);
        const auto zip = downloads / name, part = downloads / (name + ".part");
        // A finished download from an earlier attempt is kept when it checks out.
        bool have = fs::is_regular_file(zip, error) && fs::file_size(zip, error) == bytes;
        if (have) {
            report(0.05, "Checking the downloaded HD " + version);
            have = app::sha256_file(zip) == sha;
            if (!have) fs::remove(zip, error);
        }
        if (!have) {
            const auto label = "Downloading HD " + version;
            report(0.05, label + " (" + mib(bytes) + " MB)");
            update::http_download(pack_url, update::user_agent(SRW64_VERSION), part, [&](uint64_t received) {
                const double fraction = bytes ? std::min(1.0, double(received) / double(bytes)) : 0.0;
                report(0.05 + 0.75 * fraction, label + ": " + mib(received) + " of " + mib(bytes) + " MB");
            }, 60);
            report(0.81, "Checking the download");
            if (app::sha256_file(part) != sha) {
                fs::remove(part, error);
                throw std::runtime_error("the downloaded HD pack is damaged (its checksum does not match); run the setup again");
            }
            fs::rename(part, zip, error);
            if (error) throw std::runtime_error("cannot keep the download: " + error.message());
        }
        report(0.84, "Unpacking HD " + version);
        const auto installed = install_pack(zip, user_dir, [&](double fraction, const std::string&) {
            report(0.84 + 0.15 * fraction, "Unpacking HD " + version);
        });
        fs::remove(zip, error);
        fs::remove_all(downloads, error);
        report(1.0, "HD " + installed + " installed");
        return 0;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "SRW64_PREPARE_FAILURE %s\n", failure.what());
        std::fflush(stderr);
        return 3;
    }
}
}
