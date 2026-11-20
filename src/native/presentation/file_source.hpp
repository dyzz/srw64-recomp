#pragma once
// Reading a file the game's code names by path. A path into a MOD package file reads as if the
// file were a folder ("<mods>/x.srw64mod/art/a.png"): the host's package library installs that
// reader (mod_library.cpp), so a package is read in place, never unpacked
// (docs/design/mod-packages.md §2). Without it, or for any other path, the disk.
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <vector>

namespace srw64::presentation {
struct FileSource {
    std::optional<std::vector<unsigned char>> (*read)(const std::filesystem::path&) = nullptr;
    bool (*exists)(const std::filesystem::path&) = nullptr;   // a file there, in a package or on the disk
    // Every file under a folder, at any depth, by its full path, sorted.
    std::vector<std::filesystem::path> (*list)(const std::filesystem::path&) = nullptr;
};
inline std::atomic<const FileSource*>& file_source() {
    static std::atomic<const FileSource*> source{nullptr};
    return source;
}
inline std::optional<std::vector<unsigned char>> read_file(const std::filesystem::path& path) {
    if (const auto* source = file_source().load()) return source->read(path);
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {});
}
inline bool file_exists(const std::filesystem::path& path) {
    if (const auto* source = file_source().load()) return source->exists(path);
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}
inline std::vector<std::filesystem::path> list_files(const std::filesystem::path& folder) {
    if (const auto* source = file_source().load()) return source->list(folder);
    std::vector<std::filesystem::path> out;
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) return out;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        if (it->is_regular_file(error)) out.push_back(it->path());
    std::sort(out.begin(), out.end());
    return out;
}
}  // namespace srw64::presentation
