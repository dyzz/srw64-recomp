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
#include <functional>
#include <mutex>
#include <regex>
#include <set>
#include <stdexcept>
#include <vector>

namespace srw64::text_update {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;
constexpr const char* kSchema = "srw64.text-update.v1";
// The list names every text file: about 1200, 30 MB, 10 MB compressed, for three translations.
// Room for many more languages; a file the program cannot use is not counted.
constexpr size_t kListLimit = 16u << 20, kFileLimit = 16u << 20;
constexpr uint64_t kUnpackedLimit = 256u << 20;
constexpr size_t kHashDigits = 12;  // build_text_update.py HASH_DIGITS
// Where a download is put together, kept when it stops half way.
constexpr const char* kStaging = ".staging";
// Beside text.json in an update: {"version", "files": [names]}, the files it keeps: those
// that differ from the text this program shipped with (the rest are read from the bundle).
constexpr const char* kKept = "kept.json";
struct Shared {
    std::mutex mutex;
    std::string version;
    fs::path root;  // SRW64_TEXT_UPDATES; empty: no text updates
    std::optional<Pack> pack;
    std::shared_ptr<const Layer> layer;
    bool bypassed = false, running = false;
    int seen = 0;  // the newest list read that had nothing for this release
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
std::string hash_of(std::string_view data) {
    app::Sha256 digest;
    digest.update({reinterpret_cast<const uint8_t*>(data.data()), data.size()});
    return digest.finish().substr(0, kHashDigits);
}
std::vector<int> version_parts(const std::string& version) {
    std::vector<int> parts;
    for (size_t at = 0; at <= version.size();) {
        const auto dot = std::min(version.find('.', at), version.size());
        const auto part = version.substr(at, dot - at);
        if (part.empty() || part.size() > 6 || !std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
        parts.push_back(std::stoi(part));
        at = dot + 1;
    }
    return parts;
}
// The paths this program reads from a list (build_text_update.py); a list may name others,
// for later programs, and those are left alone.
bool allowed(const std::string& name) {
    static const std::regex paths(R"((library-names\.json|locales/[A-Za-z-]{2,16}\.json|dialogue/[A-Za-z-]{2,16}/[A-Za-z0-9_-]+(/[A-Za-z0-9_-]+)*\.txt))");
    return std::regex_match(name, paths);
}
struct Wanted { std::string hash; uint64_t bytes; };
// "dialogue/<locale>/…" and "locales/<locale>.json": the locale, else empty.
std::string locale_of(const std::string& name) {
    for (const std::string_view folder : {"dialogue/", "locales/"})
        if (name.starts_with(folder)) {
            const auto rest = name.substr(folder.size());
            return rest.substr(0, std::min(rest.find('/'), rest.find(".json")));
        }
    return {};
}
// The list's files this program reads (allowed(), and a language it has when `usable`), or
// none when this program is outside the list's releases: newer than `release` (it ships newer
// text than the list has) or older than `oldest` (the website has not kept what it needs).
// Throws when the list is not one.
std::map<std::string, Wanted> wanted(const json& list, const std::string& version,
                                     const std::function<bool(const std::string&)>& usable = {}) {
    if (!list.is_object() || list.value("schema", "") != kSchema || list.value("revision", 0) <= 0) throw std::runtime_error("not a text update list");
    const auto mine = version_parts(version), release = version_parts(list.value("release", "")), oldest = version_parts(list.value("oldest", ""));
    const auto& files = list.value("files", json::object());
    if (mine.empty() || release.empty() || oldest.empty() || !files.is_object()) throw std::runtime_error("not a text update list");
    std::map<std::string, Wanted> found;
    if (mine > release || mine < oldest) return found;
    for (const auto& [name, row] : files.items()) {
        if (!allowed(name)) continue;
        if (const auto locale = locale_of(name); usable && !locale.empty() && !usable(locale)) continue;
        if (!row.is_object()) throw std::runtime_error("the text update lists " + name + " wrongly");
        const auto hash = row.value("hash", "");
        const auto bytes = row.value("bytes", uint64_t(0));
        if (hash.size() != kHashDigits || bytes > kFileLimit) throw std::runtime_error("the text update lists " + name + " wrongly");
        found[name] = {hash, bytes};
    }
    return found;
}
// The update in `dir` as its text.json and kept.json say: made for this program, every file
// it keeps there with the list's hash, and nothing else (another file is removed). Throws.
Pack describe(const fs::path& dir, const std::string& version, int revision) {
    const auto list = read_json(dir / "text.json", kListLimit);
    const auto files = wanted(list, version);
    if (list.value("revision", 0) != revision) throw std::runtime_error("the text update's revision does not match");
    if (files.empty()) throw std::runtime_error("this program is outside the text update's releases");
    const auto kept = read_json(dir / kKept, kListLimit);
    // Kept against another program's bundled text: what this one needs may be missing.
    if (!kept.is_object() || kept.value("version", "") != version || !kept.value("files", json()).is_array())
        throw std::runtime_error("the text update was made for another version");
    std::set<std::string> names;
    for (const auto& name : kept["files"]) {
        if (!name.is_string() || !files.contains(name.get<std::string>())) throw std::runtime_error("the text update keeps a file its list lacks");
        const auto& file = files.at(name.get<std::string>());
        if (app::sha256_file(dir / fs::path(name.get<std::string>())).substr(0, kHashDigits) != file.hash)
            throw std::runtime_error("the text update's " + name.get<std::string>() + " is damaged");
        names.insert(name.get<std::string>());
    }
    std::error_code error;
    std::vector<fs::path> extra;
    for (auto it = fs::recursive_directory_iterator(dir, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        const auto name = fs::relative(it->path(), dir, error).generic_string();
        if (name != "text.json" && name != kKept && !names.contains(name)) extra.push_back(it->path());
    }
    if (error) throw std::runtime_error("the text update cannot be read");
    for (const auto& path : extra) fs::remove(path, error);
    return {revision, list.value("date", ""), list.value("notes", json::object()), dir};
}
std::shared_ptr<Layer> read_layer(const Pack& pack) {
    auto layer = std::make_shared<Layer>();
    layer->revision = pack.revision;
    layer->dir = pack.dir;
    std::error_code error;
    for (const auto& file : fs::directory_iterator(pack.dir / "locales", error)) {
        if (file.path().extension() != ".json") continue;
        const auto catalog = read_json(file.path(), 32u << 20);
        if (!catalog.is_object()) throw std::runtime_error("the text update's " + file.path().filename().string() + " is not JSON");
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
// The update against the catalogs the game reads, as the bundled text was checked: every
// dialogue file loads with no problem and every entry keeps its record's script signature.
// A language this release lacks is not read, and labels are taken only where they fit
// (Layer::apply). The first problem, or empty.
std::string verify(const Layer& layer) {
    std::error_code error;
    for (const auto& folder : fs::directory_iterator(layer.dir / "dialogue", error)) {
        const auto locale = folder.path().filename().string();
        const auto catalog = localization::find(locale);
        if (!catalog) continue;
        const auto result = localization::dialogue_text::load({folder.path()}, *catalog);
        if (!result.problems.empty()) {
            const auto& p = result.problems.front();
            return locale + "/" + p.path + ":" + std::to_string(p.line) + " " + p.message;
        }
    }
    for (const auto& [locale, entries] : layer.entries) {
        const auto catalog = localization::find(locale);
        if (!catalog) continue;
        for (const auto& [key, target] : entries) {
            const auto* source = catalog->source_text({key});
            try {
                if (!source || localization::signature(target) != localization::signature(*source)) return locale + " " + key + " does not fit its record";
            } catch (const std::exception& problem) {
                return locale + " " + key + ": " + problem.what();
            }
        }
    }
    if (layer.library_names && (!layer.library_names->is_object() || layer.library_names->value("schema", "") != "srw64.library-names.v1"))
        return "the Library names are not readable";
    return {};
}
// A label's {marks} (update_text_notice's {revision}, button prompts' {A}), in order.
std::vector<std::string> marks(const std::string& text) {
    std::vector<std::string> found;
    for (size_t at = text.find('{'); at != std::string::npos; at = text.find('{', at + 1))
        if (const auto end = text.find('}', at); end != std::string::npos) found.push_back(text.substr(at, end - at + 1));
    std::sort(found.begin(), found.end());
    return found;
}
std::string folder_name(int revision) { return "r" + std::to_string(revision); }
// "r<N>": N, else 0.
int revision_of(const std::string& name) {
    if (name.size() < 2 || name.size() > 7 || name[0] != 'r') return 0;
    if (!std::all_of(name.begin() + 1, name.end(), [](char c) { return c >= '0' && c <= '9'; })) return 0;
    return std::stoi(name.substr(1));
}
// Once, before anything reads the state: the newest update that is whole and not older than
// this program. Anything else goes, but an unfinished download (kStaging) stays for the next.
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
        std::vector<std::pair<int, fs::path>> found;
        for (const auto& entry : fs::directory_iterator(s.root, error)) {
            const auto name = entry.path().filename().string();
            std::error_code ignored;
            if (name == "use-bundled" || name == kStaging) continue;
            if (const int revision = entry.is_directory(ignored) ? revision_of(name) : 0) found.emplace_back(revision, entry.path());
            else fs::remove_all(entry.path(), ignored);
        }
        std::sort(found.rbegin(), found.rend());
        for (const auto& [revision, dir] : found) {
            try {
                auto pack = describe(dir, s.version, revision);
                s.layer = read_layer(pack);
                s.pack = std::move(pack);
                s.state = State::Installed;
                std::fprintf(stderr, "SRW64_TEXT_UPDATE r%d%s\n", revision, s.bypassed ? " (bundled text chosen)" : "");
                break;
            } catch (const std::exception& problem) {
                // Its files wait in kStaging: the next check takes what still fits.
                std::fprintf(stderr, "SRW64_TEXT_UPDATE_REMOVED %s %s\n", dir.filename().string().c_str(), problem.what());
                std::error_code ignored;
                if (!fs::exists(s.root / kStaging, ignored)) fs::rename(dir, s.root / kStaging, ignored);
                fs::remove_all(dir, ignored);
            }
        }
    });
    return s;
}
// The file a list path names in the text this program shipped with, if it has one: the
// dialogue files, where the dialogue layer reads them (SRW64_DIALOGUE_TEXT), else the
// bundle's dialogue/ (the consoles keep it, for preparing their data). The locale catalogs
// and Library names are built in another form, so the update always keeps the list's.
fs::path bundled(const std::string& name) {
    constexpr std::string_view folder = "dialogue/";
    if (!name.starts_with(folder)) return {};
    static const auto root = [] {
        const char* text = std::getenv("SRW64_DIALOGUE_TEXT");
        return text && *text ? fs::path(text) : app::bundled_resource("dialogue");
    }();
    return root.empty() ? fs::path() : root / fs::path(name.substr(folder.size()));
}
// `path` holds the list's file: its size, then its hash.
bool same(const fs::path& path, const Wanted& file) {
    std::error_code error;
    return !path.empty() && fs::is_regular_file(path, error) && fs::file_size(path, error) == file.bytes &&
           app::sha256_file(path).substr(0, kHashDigits) == file.hash;
}
// The list's folder on the website: where its files are, as files/<hash>.z.
std::string folder_url(const std::string& url) { return url.substr(0, url.rfind('/') + 1); }
// A downloaded file, inflated and checked.
std::string unpacked(const std::string& packed, const std::string& name, const Wanted& file) {
    if (packed.size() > kFileLimit) throw std::runtime_error("the text update's " + name + " is too large");
    std::string data(file.bytes, '\0');
    if (data.empty()) return hash_of(data) == file.hash ? data : throw std::runtime_error("the download is damaged");
    mz_ulong size = mz_ulong(data.size());
    if (mz_uncompress(reinterpret_cast<unsigned char*>(data.data()), &size, reinterpret_cast<const unsigned char*>(packed.data()), mz_ulong(packed.size())) != MZ_OK ||
        size != data.size() || hash_of(data) != file.hash)
        throw std::runtime_error("the download is damaged");
    return data;
}
// One file of the list: kept when `staging` has it already (a download that stopped half
// way), copied when the installed update has the same, else downloaded. True when downloaded.
bool fetch(const std::string& base, const std::string& name, const Wanted& file, const std::optional<Pack>& installed,
           const fs::path& staging, const std::string& user_agent) {
    // `name` is one allowed() passed: no "..", no root.
    const auto target = staging / fs::path(name);
    if (same(target, file)) return false;
    std::string data;
    const bool copied = installed && same(installed->dir / fs::path(name), file);
    if (copied) {
        data = app::read_text(installed->dir / fs::path(name), kFileLimit);
    } else {
        data = unpacked(update::http_get(base + "files/" + file.hash + ".z", user_agent, 30), name, file);
    }
    fs::create_directories(target.parent_path());
    std::ofstream out(target, std::ios::binary);
    out.write(data.data(), std::streamsize(data.size()));
    if (!out) throw std::runtime_error("cannot write " + name);
    return !copied;
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
    // The newest labels, for every release: only those this release has, with the same marks.
    if (const auto found = ui.find(base->locale); found != ui.end()) {
        localization::Catalog::Table labels;
        for (const auto& [key, text] : found->second)
            if (const auto mine = base->ui_labels().find(key); mine != base->ui_labels().end() && marks(mine->second) == marks(text))
                labels[key] = text;
        if (!labels.empty()) result = result->with_labels(labels, tag);
    }
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
    std::optional<Pack> installed;
    int seen = 0;
    {
        std::lock_guard lock(s.mutex);
        if (s.root.empty() || s.running) return;
        version = s.version;
        root = s.root;
        installed = s.pack;
        seen = s.seen;
    }
    const auto offer = latest.is_object() ? latest.value("text", json::object()) : json::object();
    if (!offer.is_object()) return;
    const int revision = offer.value("revision", 0);
    const std::string url = offer.value("url", ""), digest = offer.value("sha256", "");
    const uint64_t bytes = offer.value("bytes", uint64_t(0));
    if (revision <= std::max(installed ? installed->revision : 0, seen) || url.empty() || !bytes || bytes > kListLimit || digest.size() != 64) return;
    {
        std::lock_guard lock(s.mutex);
        s.running = true;
        s.state = State::Downloading;
        ++s.serial;
    }
    const auto staging = root / kStaging;
    std::error_code ignored;
    try {
        const auto text = update::http_get(url, user_agent, 30);
        app::Sha256 whole;
        whole.update({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
        if (text.size() != bytes || whole.finish() != digest) throw std::runtime_error("the download is damaged");
        const auto list = json::parse(text, nullptr, false);
        const auto files = wanted(list, version, [](const std::string& locale) { return localization::find(locale) != nullptr; });
        if (list.value("revision", 0) != revision) throw std::runtime_error("the text update's revision does not match");
        uint64_t total = 0;
        for (const auto& [name, file] : files)
            if ((total += file.bytes) > kUnpackedLimit) throw std::runtime_error("the text update is too large");
        if (files.empty()) {
            // This program is outside the list's releases: the installed update goes too.
            bool had;
            {
                std::lock_guard lock(s.mutex);
                had = s.pack.has_value();
                s.pack.reset();
                s.layer.reset();
                s.seen = revision;
                s.state = State::None;
                s.error.clear();
                s.running = false;
                ++s.serial;
            }
            for (const auto& entry : fs::directory_iterator(root, ignored))
                if (revision_of(entry.path().filename().string()) || entry.path() == staging) fs::remove_all(entry.path(), ignored);
            std::fprintf(stderr, "SRW64_TEXT_UPDATE_NONE r%d\n", revision);
            if (had) dialogue::request_reload(false);
            return;
        }
        fs::create_directories(staging);
        std::ofstream(staging / "text.json", std::ios::binary) << text;
        const auto base = folder_url(url);
        // The files the bundled text has the same are read from there; the website keeps
        // only the others (build_text_update.py).
        size_t downloaded = 0;
        json kept = json::array();
        for (const auto& [name, file] : files) {
            if (same(bundled(name), file)) continue;
            if (fetch(base, name, file, installed, staging, user_agent)) ++downloaded;
            kept.push_back(name);
        }
        std::ofstream(staging / kKept, std::ios::binary) << json{{"version", version}, {"files", kept}}.dump(1) << '\n';
        auto pack = describe(staging, version, revision);
        auto next = read_layer(pack);
        if (const auto problem = verify(*next); !problem.empty()) throw std::runtime_error(problem);
        const auto dir = root / folder_name(revision);
        fs::remove_all(dir, ignored);
        fs::rename(staging, dir);
        pack.dir = next->dir = dir;
        bool bypassed;
        {
            std::lock_guard lock(s.mutex);
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
            const int other = revision_of(entry.path().filename().string());
            if (other && other != revision && (!installed || other != installed->revision)) fs::remove_all(entry.path(), ignored);
        }
        std::fprintf(stderr, "SRW64_TEXT_UPDATE_INSTALLED r%d (%zu files, %zu kept, %zu downloaded)\n", revision, files.size(), kept.size(), downloaded);
        if (!bypassed) {
            dialogue::request_reload(false);
            notices::post("text-update", filled(localization::catalog().ui("update_text_notice"), "{revision}", std::to_string(revision)));
        }
    } catch (const std::exception& problem) {
        // What came whole stays in kStaging for the next check.
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
