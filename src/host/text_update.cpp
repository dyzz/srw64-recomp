#include "text_update.hpp"
#include "update_check.hpp"
#include "native_dialogue.hpp"
#include "notices.hpp"
#include "app/runtime.hpp"
#include "app/sha256.hpp"
#include "localization/dialogue_text.hpp"
#include "localization/signature.hpp"
#include "miniz.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <vector>

namespace srw64::text_update {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;
constexpr const char* kSchema = "srw64.text-pack.v1";
// A pack is the text that changed, tens of KB; every dialogue file in three languages,
// zipped, is about 5 MB. Unpacked, about 30 MB.
constexpr uint64_t kPackLimit = 16u << 20, kUnpackedLimit = 64u << 20;
struct Shared {
    std::mutex mutex;
    std::string version;
    fs::path root;  // SRW64_TEXT_UPDATES; empty: no text updates
    std::optional<Pack> pack;
    std::shared_ptr<const Layer> layer;
    bool bypassed = false, running = false;
    State state = State::None;
    std::string error;
    uint64_t serial = 1;
};
Shared& shared() {
    static auto* s = new Shared;
    return *s;
}
json read_json(const fs::path& path, size_t limit) {
    return json::parse(app::read_text(path, limit), nullptr, false);
}
// The pack in `dir` as its text.json says, every file's digest checked. Throws.
Pack describe(const fs::path& dir, const std::string& version, int revision) {
    const auto about = read_json(dir / "text.json", 1u << 20);
    if (!about.is_object() || about.value("schema", "") != kSchema) throw std::runtime_error("not a text pack");
    if (about.value("base", "") != version) throw std::runtime_error("a text pack for " + about.value("base", "another release"));
    const auto found = about.value("revision", 0);
    if (found <= 0 || (revision && found != revision)) throw std::runtime_error("the text pack's revision does not match");
    const auto& files = about.value("files", json::object());
    if (!files.is_object() || files.empty()) throw std::runtime_error("the text pack lists no files");
    for (const auto& [name, digest] : files.items())
        if (!digest.is_string() || app::sha256_file(app::content_path(dir, name)) != digest.get<std::string>())
            throw std::runtime_error("the text pack's " + name + " is damaged");
    // Nothing beside what it lists and text.json.
    size_t present = 0;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(dir, error); !error && it != fs::recursive_directory_iterator(); it.increment(error))
        if (it->is_regular_file(error)) ++present;
    if (error || present != files.size() + 1) throw std::runtime_error("the text pack holds files it does not list");
    return {found, about.value("date", ""), about.value("notes", json::object()), dir};
}
std::shared_ptr<Layer> read_layer(const Pack& pack) {
    auto layer = std::make_shared<Layer>();
    layer->revision = pack.revision;
    layer->dir = pack.dir;
    std::error_code error;
    for (const auto& file : fs::directory_iterator(pack.dir / "locales", error)) {
        if (file.path().extension() != ".json") continue;
        const auto catalog = read_json(file.path(), 32u << 20);
        if (!catalog.is_object()) throw std::runtime_error("the text pack's " + file.path().filename().string() + " is not JSON");
        const auto locale = file.path().stem().string();
        for (const auto& [part, table] : {std::pair{"entries", &layer->entries}, std::pair{"ui", &layer->ui}}) {
            const auto& rows = catalog.value(part, json::object());
            for (const auto& [key, value] : rows.items())
                if (value.is_string()) (*table)[locale][key] = value.get<std::string>();
        }
    }
    if (fs::is_regular_file(pack.dir / "library-names.json", error)) layer->library_names = read_json(pack.dir / "library-names.json", 4u << 20);
    return layer;
}
// The pack against the catalogs the game reads, as the bundled text was checked: every
// dialogue file loads with no problem, every entry keeps its record's script signature and
// every label is one this release has. The first problem, or empty.
std::string verify(const Layer& layer) {
    std::error_code error;
    for (const auto& folder : fs::directory_iterator(layer.dir / "dialogue", error)) {
        const auto locale = folder.path().filename().string();
        const auto catalog = localization::find(locale);
        if (!catalog) return "no " + locale + " text in this release";
        const auto result = localization::dialogue_text::load({folder.path()}, *catalog);
        if (!result.problems.empty()) {
            const auto& p = result.problems.front();
            return locale + "/" + p.path + ":" + std::to_string(p.line) + " " + p.message;
        }
    }
    for (const auto& [locale, entries] : layer.entries) {
        const auto catalog = localization::find(locale);
        if (!catalog) return "no " + locale + " text in this release";
        for (const auto& [key, target] : entries) {
            const auto* source = catalog->source_text({key});
            try {
                if (!source || localization::signature(target) != localization::signature(*source)) return locale + " " + key + " does not fit its record";
            } catch (const std::exception& problem) {
                return locale + " " + key + ": " + problem.what();
            }
        }
    }
    for (const auto& [locale, labels] : layer.ui) {
        const auto catalog = localization::find(locale);
        if (!catalog) return "no " + locale + " text in this release";
        for (const auto& [key, text] : labels)
            if (!catalog->ui_labels().contains(key) || text.empty()) return locale + " label " + key + " is not one this release has";
    }
    if (layer.library_names && (!layer.library_names->is_object() || layer.library_names->value("schema", "") != "srw64.library-names.v1"))
        return "the Library names are not readable";
    return {};
}
// Only the files a pack has (build_text_pack.py), no other paths.
void unpack(const fs::path& zip_path, const fs::path& into) {
    std::ifstream in(zip_path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) throw std::runtime_error("the text pack is not a zip file");
    struct End { mz_zip_archive* z; ~End() { mz_zip_reader_end(z); } } end{&zip};
    static const std::regex allowed(R"((text\.json|library-names\.json|locales/[A-Za-z-]{2,16}\.json|dialogue/[A-Za-z-]{2,16}/[A-Za-z0-9_-]+(/[A-Za-z0-9_-]+)*\.txt))");
    uint64_t total = 0;
    for (unsigned i = 0, count = mz_zip_reader_get_num_files(&zip); i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) throw std::runtime_error("the text pack cannot be read");
        if (stat.m_is_directory) continue;
        const std::string name = stat.m_filename;
        if (!std::regex_match(name, allowed)) throw std::runtime_error("the text pack holds " + name);
        if ((total += stat.m_uncomp_size) > kUnpackedLimit) throw std::runtime_error("the text pack is too large");
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data) throw std::runtime_error("cannot unpack " + name);
        const auto target = into / fs::path(name);
        fs::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary);
        out.write(static_cast<const char*>(data), std::streamsize(size));
        mz_free(data);
        if (!out) throw std::runtime_error("cannot write " + name);
    }
}
std::string folder_name(const std::string& version, int revision) { return version + "-r" + std::to_string(revision); }
// "<version>-r<N>": N for this version, else 0.
int revision_of(const std::string& name, const std::string& version) {
    const auto prefix = version + "-r";
    if (!name.starts_with(prefix) || name.size() == prefix.size()) return 0;
    const auto digits = name.substr(prefix.size());
    if (!std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }) || digits.size() > 6) return 0;
    return std::stoi(digits);
}
// Once, before anything reads the state: the newest pack for this release that is whole.
// Another release's packs, leftover downloads and unfinished installs are removed.
Shared& ready() {
    auto& s = shared();
    static std::once_flag once;
    std::call_once(once, [&] {
        std::lock_guard lock(s.mutex);
        s.version = SRW64_VERSION;
        const char* root = std::getenv("SRW64_TEXT_UPDATES");
        if (!root || !*root) return;
        s.root = root;
        std::error_code error;
        s.bypassed = fs::exists(s.root / "use-bundled", error);
        std::vector<std::pair<int, fs::path>> mine;
        for (const auto& entry : fs::directory_iterator(s.root, error)) {
            const auto name = entry.path().filename().string();
            std::error_code ignored;
            if (name == "use-bundled") continue;
            if (const int revision = entry.is_directory(ignored) ? revision_of(name, s.version) : 0) mine.emplace_back(revision, entry.path());
            else fs::remove_all(entry.path(), ignored);
        }
        std::sort(mine.rbegin(), mine.rend());
        for (const auto& [revision, dir] : mine) {
            try {
                auto pack = describe(dir, s.version, revision);
                s.layer = read_layer(pack);
                s.pack = std::move(pack);
                s.state = State::Installed;
                std::fprintf(stderr, "SRW64_TEXT_UPDATE r%d%s\n", revision, s.bypassed ? " (bundled text chosen)" : "");
                break;
            } catch (const std::exception& problem) {
                std::fprintf(stderr, "SRW64_TEXT_UPDATE_DAMAGED %s %s\n", dir.filename().string().c_str(), problem.what());
                std::error_code ignored;
                fs::remove_all(dir, ignored);
            }
        }
    });
    return s;
}
std::string filled(std::string text, const std::string& mark, const std::string& value) {
    if (const auto at = text.find(mark); at != std::string::npos) text.replace(at, mark.size(), value);
    return text;
}
}

localization::Snapshot Layer::apply(const localization::Snapshot& base) const {
    localization::Snapshot result = base;
    const auto tag = "text-r" + std::to_string(revision);
    if (const auto found = entries.find(base->locale); found != entries.end() && !found->second.empty())
        result = result->with_translations(found->second, tag);
    if (const auto found = ui.find(base->locale); found != ui.end() && !found->second.empty())
        result = result->with_labels(found->second, tag);
    return result;
}

Status status() {
    auto& s = ready();
    std::lock_guard lock(s.mutex);
    return {s.state, s.pack, s.bypassed, s.error, s.serial};
}

std::shared_ptr<const Layer> layer() {
    auto& s = ready();
    std::lock_guard lock(s.mutex);
    return s.bypassed ? nullptr : s.layer;
}

void set_bypassed(bool bypassed) {
    auto& s = ready();
    {
        std::lock_guard lock(s.mutex);
        if (s.root.empty() || s.bypassed == bypassed) return;
        s.bypassed = bypassed;
        ++s.serial;
        std::error_code error;
        if (bypassed) {
            fs::create_directories(s.root, error);
            std::ofstream(s.root / "use-bundled") << "The About page's switch: the text this release shipped with, not the text update.\n";
        } else {
            fs::remove(s.root / "use-bundled", error);
        }
    }
    dialogue::request_reload(false);
}

void take(const json& latest, const std::string& user_agent) {
    auto& s = ready();
    std::string version;
    fs::path root;
    int installed = 0;
    {
        std::lock_guard lock(s.mutex);
        if (s.root.empty() || s.running) return;
        version = s.version;
        root = s.root;
        installed = s.pack ? s.pack->revision : 0;
    }
    const auto text = latest.is_object() ? latest.value("text", json::object()) : json::object();
    const auto found = text.is_object() ? text.find(version) : text.end();
    if (found == text.end() || !found->is_object()) return;
    const auto& offer = *found;
    const int revision = offer.value("revision", 0);
    const std::string url = offer.value("url", ""), digest = offer.value("sha256", "");
    const uint64_t bytes = offer.value("bytes", uint64_t(0));
    if (revision <= installed || url.empty() || !bytes || bytes > kPackLimit || digest.size() != 64) return;
    {
        std::lock_guard lock(s.mutex);
        s.running = true;
        s.state = State::Downloading;
        ++s.serial;
    }
    const auto part = root / ("download-" + folder_name(version, revision) + ".part");
    const auto staging = root / (".staging-" + folder_name(version, revision));
    std::optional<Pack> previous;
    std::error_code ignored;
    try {
        fs::create_directories(root);
        update::http_download(url, user_agent, part, [](uint64_t) {}, 30);
        if (fs::file_size(part) != bytes || app::sha256_file(part) != digest) throw std::runtime_error("the download is damaged");
        fs::remove_all(staging, ignored);
        fs::create_directories(staging);
        unpack(part, staging);
        fs::remove(part, ignored);
        auto pack = describe(staging, version, revision);
        auto next = read_layer(pack);
        if (const auto problem = verify(*next); !problem.empty()) throw std::runtime_error(problem);
        const auto dir = root / folder_name(version, revision);
        fs::remove_all(dir, ignored);
        fs::rename(staging, dir);
        pack.dir = next->dir = dir;
        bool bypassed;
        {
            std::lock_guard lock(s.mutex);
            previous = s.pack;
            s.pack = pack;
            s.layer = next;
            s.state = State::Installed;
            s.error.clear();
            s.running = false;
            bypassed = s.bypassed;
            ++s.serial;
        }
        // The one before stays, should this one turn out wrong on the disk; older ones go.
        for (const auto& entry : fs::directory_iterator(root, ignored)) {
            const int other = revision_of(entry.path().filename().string(), version);
            if (other && other != revision && (!previous || other != previous->revision)) fs::remove_all(entry.path(), ignored);
        }
        std::fprintf(stderr, "SRW64_TEXT_UPDATE_INSTALLED r%d\n", revision);
        if (!bypassed) {
            dialogue::request_reload(false);
            notices::post("text-update", filled(localization::catalog().ui("update_text_notice"), "{revision}", std::to_string(revision)));
        }
    } catch (const std::exception& problem) {
        fs::remove(part, ignored);
        fs::remove_all(staging, ignored);
        std::lock_guard lock(s.mutex);
        s.running = false;
        s.state = State::Failed;
        s.error = problem.what();
        ++s.serial;
        std::fprintf(stderr, "SRW64_TEXT_UPDATE_FAILED r%d %s\n", revision, problem.what());
    }
}

std::string note(const Pack& pack, std::string_view language) {
    for (const auto& key : {std::string(language), std::string(language == "tw" ? "zh" : "en"), std::string("en")})
        if (pack.notes.is_object() && pack.notes.contains(key) && pack.notes[key].is_string()) return pack.notes[key].get<std::string>();
    return {};
}
}
