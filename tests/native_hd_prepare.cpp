// The HD pack's setup step (src/host/hd_prepare.hpp): a downloaded pack unpacked into a
// user directory's hd/ through a staging folder, with the top folder or without, replacing
// the pack there (a folder or a link); an archive that is not a complete pack, or has an
// unsafe path, changes nothing. The network part (prepare) is tried by hand with
// SRW64_UPDATE_URL (docs/guide/release.md).
#include "hd_prepare.hpp"
#include "update_check.hpp"
#include "json/json.hpp"
#include "miniz.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;
static int failures = 0;
static void check(bool ok, const std::string& what) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", what.c_str()); ++failures; } }

// The HTTP layer is not linked here: prepare() is not exercised.
namespace srw64::update {
std::string http_get(const std::string&, const std::string&, int) { throw std::runtime_error("no network in the test"); }
void http_download(const std::string&, const std::string&, const fs::path&, const std::function<void(uint64_t)>&, int) { throw std::runtime_error("no network in the test"); }
std::string user_agent(const std::string& v) { return "test/" + v; }
}

static void write_zip(const fs::path& path, const std::map<std::string, std::string>& files) {
    mz_zip_archive zip{};
    check(mz_zip_writer_init_file(&zip, path.string().c_str(), 0), "zip writer");
    for (const auto& [name, text] : files)
        check(mz_zip_writer_add_mem(&zip, name.c_str(), text.data(), text.size(), MZ_DEFAULT_COMPRESSION), "zip add " + name);
    check(mz_zip_writer_finalize_archive(&zip), "zip finalize");
    mz_zip_writer_end(&zip);
}
static std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
static std::map<std::string, std::string> pack(const std::string& version, const std::string& top = "hd/") {
    return {{top + "hd.json", json({{"schema", "srw64.hd-bundle.v1"}, {"version", version}}).dump()},
            {top + "art/rt64.json", "{}"},
            {top + "art/portraits/one.jpg", "picture " + version},
            {top + "NOTICE.txt", "notice"}};
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s SCRATCH_DIR\n", argv[0]); return 2; }
    const fs::path root = fs::absolute(argv[1]);
    fs::remove_all(root);
    fs::create_directories(root);
    const auto user = root / "user data";
    fs::create_directories(user);
    using srw64::hd::install_pack;
    using srw64::hd::pack_version;

    // A first install from build_release.py's layout (hd/ at the top).
    write_zip(root / "a.zip", pack("1.2"));
    unsigned reports = 0;
    double last = 0;
    const auto installed = install_pack(root / "a.zip", user, [&](double fraction, const std::string&) { ++reports; check(fraction >= last, "progress runs forward"); last = fraction; });
    check(installed == "1.2", "installed version");
    check(reports >= 2 && last == 1.0, "progress reported to 1");
    check(read(user / "hd/art/portraits/one.jpg") == "picture 1.2", "pack contents");
    check(pack_version(user / "hd") == std::optional<std::string>("1.2"), "pack_version");
    check(!pack_version(root).has_value(), "no pack: nullopt");
    bool stray = false;
    for (const auto& entry : fs::directory_iterator(user)) if (entry.path().filename() != "hd") stray = true;
    check(!stray, "nothing but hd/ left in the user directory");

    // The next version replaces it; the contents at the top also work.
    write_zip(root / "b.zip", pack("1.3", ""));
    check(install_pack(root / "b.zip", user, {}) == "1.3", "replacing install");
    check(read(user / "hd/art/portraits/one.jpg") == "picture 1.3", "replaced contents");
    check(!fs::exists(user / "hd.old-0"), "the old pack is gone");

    // An incomplete pack (no art/rt64.json), a pack of another schema, a zip that is not a
    // pack, and an unsafe path: the 1.3 pack stays.
    auto incomplete = pack("1.4"); incomplete.erase("hd/art/rt64.json");
    write_zip(root / "c.zip", incomplete);
    auto other = pack("1.4"); other["hd/hd.json"] = json({{"schema", "srw64.hd-bundle.v0"}}).dump();
    write_zip(root / "d.zip", other);
    write_zip(root / "e.zip", {{"readme.txt", "hello"}});
    auto escaping = pack("1.4"); escaping["hd/../outside.txt"] = "x";
    write_zip(root / "f.zip", escaping);
    std::ofstream(root / "g.zip") << "not a zip";
    for (const char* name : {"c.zip", "d.zip", "e.zip", "f.zip", "g.zip"}) {
        bool refused = false;
        try { install_pack(root / name, user, {}); } catch (const std::exception&) { refused = true; }
        check(refused, std::string(name) + " refused");
        check(read(user / "hd/art/portraits/one.jpg") == "picture 1.3", std::string(name) + " left the pack alone");
        check(!fs::exists(root / "outside.txt") && !fs::exists(user / "outside.txt"), std::string(name) + " wrote nothing outside");
    }
    stray = false;
    for (const auto& entry : fs::directory_iterator(user)) if (entry.path().filename() != "hd") stray = true;
    check(!stray, "no staging folder left behind");

    // A linked hd/ (attach.py's debugging data directory) is replaced as a link; the
    // folder it pointed to is untouched.
    const auto elsewhere = root / "elsewhere";
    fs::rename(user / "hd", elsewhere);
    fs::create_directory_symlink(elsewhere, user / "hd");
    write_zip(root / "h.zip", pack("1.5"));
    check(install_pack(root / "h.zip", user, {}) == "1.5", "install over a link");
    check(!fs::is_symlink(user / "hd") && read(user / "hd/art/portraits/one.jpg") == "picture 1.5", "a real folder now");
    check(read(elsewhere / "art/portraits/one.jpg") == "picture 1.3", "the linked folder untouched");

    fs::remove_all(root);
    std::printf("%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
