#pragma once
// The Switch's HD folder in one file (tools/release/switch/pack_hd.py, docs/design/
// switch-port.md): art.pack holds every file of the folder, found by the hash of its path.
// On the console it is mounted as the device "hd:" (hd_pack.cpp), so whatever read
// hd/art/x.png from the SD card reads hd:/art/x.png from the pack with the same calls.
// The reader itself is plain POSIX and is tested on a computer (tests/native_hd_pack.cpp).
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace srw64::hd_pack {
struct Entry { uint64_t hash, offset; uint32_t size, name; };
static_assert(sizeof(Entry) == 24);
constexpr uint32_t kNoName = 0xFFFFFFFF;   // a version 1 entry carried over: found by hash, in no folder

uint64_t fnv1a(std::string_view text);
// "art//x/../y.png" and "/art/y.png" as "art/y.png"; "" is the folder itself.
std::string normal(std::string_view path);

class Pack {
public:
    struct Child { std::string name; bool directory; uint32_t size; };
    ~Pack();
    // False for a file that is not a pack of version 1 or 2 (nothing is kept open then).
    bool open(const std::string& file);
    bool opened() const { return file_ >= 0; }
    uint32_t version() const { return version_; }
    size_t files() const { return entries_.size(); }
    // A file by its normal path, or nullptr.
    const Entry* find(std::string_view path) const;
    // A folder's entries, or nullptr when the path is not a folder (version 2 only).
    const std::vector<Child>* list(std::string_view path) const;
    // `size` bytes at `at` of the entry; false when they are not all there.
    bool read(const Entry& entry, uint64_t at, void* out, size_t size) const;

private:
    int file_ = -1;
    uint32_t version_ = 0;
    std::vector<Entry> entries_;   // sorted by hash
    std::map<std::string, std::vector<Child>, std::less<>> folders_;
    mutable std::mutex mutex_;     // a read is a seek and a read
};

#ifdef __SWITCH__
// Finds the pack (the NRO's romfs, else hd/ beside the NRO) and, for a version 2 pack,
// mounts it as "hd:". Returns the art folder to use ("hd:/art"), or "" when there is no
// version 2 pack: the caller then looks for loose files as before.
std::string mount();
#endif
// A whole file by the path the game uses for it: "hd:/..." of the mounted pack, or a path
// under the folder of a version 1 pack. False when the pack does not hold it.
bool read_file(const std::string& path, std::string& out);
}
