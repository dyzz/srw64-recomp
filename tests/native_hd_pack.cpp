// The HD pack's reader (src/host/hd_pack.cpp) on a pack tools/release/switch/pack_hd.py
// wrote: tests/test_switch_hd_pack.py builds this, makes the pack and runs it.
//   native_hd_pack <art.pack> <list>
// <list> has a line for each file: its path, a tab, its size, a tab, the FNV-1a 64 of its
// bytes; "-" for the path of an entry carried over from a version 1 pack (found by hash only).
#include "../src/host/hd_pack.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace srw64::hd_pack;

static int failures = 0;
static void check(bool good, const std::string& what) {
    if (!good) { ++failures; std::fprintf(stderr, "FAILED %s\n", what.c_str()); }
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    check(normal("/art//x/../y.png") == "art/y.png", "normal()");
    check(normal("") == "" && normal("/") == "" && normal("./a/./b/") == "a/b", "normal() of the folder itself");
    Pack pack;
    check(!pack.open(argv[2]), "a file that is not a pack opened");
    check(pack.open(argv[1]), "the pack did not open");
    check(pack.version() == 2, "version");
    std::ifstream list(argv[2]);
    std::set<std::string> folders{""};
    size_t files = 0;
    for (std::string line; std::getline(list, line);) {
        std::istringstream fields(line);
        std::string path, size_text, hash_text;
        std::getline(fields, path, '\t'); std::getline(fields, size_text, '\t'); std::getline(fields, hash_text, '\t');
        const uint32_t size = uint32_t(std::stoul(size_text));
        const uint64_t hash = std::stoull(hash_text, nullptr, 16);
        const Entry* entry = pack.find(path);
        check(entry != nullptr, "missing " + path);
        if (!entry) continue;
        ++files;
        check(entry->size == size, "size of " + path);
        std::string bytes(size, '\0');
        check(pack.read(*entry, 0, bytes.data(), bytes.size()), "read of " + path);
        check(fnv1a(bytes) == hash, "bytes of " + path);
        if (size > 3) {
            char tail[2];
            check(pack.read(*entry, size - 2, tail, 2) && tail[0] == bytes[size - 2] && tail[1] == bytes[size - 1], "read at an offset in " + path);
            check(!pack.read(*entry, size - 1, tail, 2), "a read past the end of " + path);
        }
        // Listed in its folder, and each folder in the one above.
        const size_t slash = path.rfind('/');
        const std::string folder = slash == std::string::npos ? "" : path.substr(0, slash), name = path.substr(slash == std::string::npos ? 0 : slash + 1);
        const auto* children = pack.list(folder);
        check(children != nullptr, "folder " + folder);
        bool listed = false;
        if (children) for (const auto& child : *children) listed = listed || (child.name == name && !child.directory && child.size == size);
        check(listed, path + " is not listed in its folder");
        for (std::string inner = folder; !inner.empty();) {
            folders.insert(inner);
            const size_t up = inner.rfind('/');
            const std::string above = up == std::string::npos ? "" : inner.substr(0, up), leaf = inner.substr(up == std::string::npos ? 0 : up + 1);
            bool found = false;
            if (const auto* in = pack.list(above)) for (const auto& child : *in) found = found || (child.name == leaf && child.directory);
            check(found, "folder " + inner + " is not listed in " + above);
            inner = above;
        }
    }
    check(files > 0, "an empty list");
    check(pack.find("no/such/file.png") == nullptr, "a file that is not there was found");
    check(pack.list("no/such") == nullptr, "a folder that is not there was listed");
    check(pack.list("art/a.png") == nullptr || files == 0, "a file listed as a folder");
    for (const auto& folder : folders) {
        const auto* children = pack.list(folder);
        if (!children) continue;
        std::set<std::string> names;
        for (const auto& child : *children) check(names.insert(child.name).second, "twice in " + folder + ": " + child.name);
    }
    std::printf("%zu files checked, %d failures\n", files, failures);
    return failures ? 1 : 0;
}
