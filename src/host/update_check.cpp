#include "update_check.hpp"
#include "app/runtime.hpp"
#include "json/json.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>

namespace srw64::update {
namespace {
using json = nlohmann::json;
constexpr int64_t kDay = 24 * 60 * 60;
// The thread of a check may outlive the window at exit, so the state is never freed.
struct Shared {
    std::mutex mutex;
    std::string version;
    std::filesystem::path file;  // update.json; empty: nothing is kept
    std::optional<bool> automatic;
    int64_t checked_at = 0;
    json latest;  // the last /latest.json read, kept for the title's hint
    State state = State::Unchecked;
    std::string error;
    uint64_t serial = 1;
    bool running = false;
};
Shared& shared() {
    static auto* s = new Shared;
    return *s;
}
int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
// Caller holds the mutex.
void save(Shared& s) {
    if (s.file.empty()) return;
    json saved = {{"schema", "srw64.update-check.v1"}, {"checked_at", s.checked_at}};
    if (s.automatic) saved["automatic"] = *s.automatic;
    if (s.latest.is_object()) saved["latest"] = s.latest;
    try {
        app::atomic_write(s.file, saved.dump(2) + "\n");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "SRW64_UPDATE_SAVE_FAILED %s\n", error.what());
    }
}
void run(bool manual) {
    auto& s = shared();
    std::string url = kLatestUrl;
    // A test points the check elsewhere (a local server, a file:// URL).
    if (const char* other = std::getenv("SRW64_UPDATE_URL"); other && *other) url = other;
    json latest;
    std::string error;
    try {
        latest = json::parse(http_get(url, 15), nullptr, false);
        if (!latest.is_object() || latest.value("schema", "") != "srw64.latest.v1" || !latest.contains("version") ||
            !latest["version"].is_string())
            error = "not a release description";
    } catch (const std::exception& failure) {
        error = failure.what();
    }
    std::lock_guard lock(s.mutex);
    s.running = false;
    s.checked_at = now();
    if (error.empty()) {
        s.latest = latest;
        s.state = newer(latest["version"].get<std::string>(), s.version) ? State::Available : State::Current;
        s.error.clear();
    } else if (manual) {
        s.state = State::Failed;
        s.error = error;
    } else {
        // A failed check on start-up says nothing; the last result stays.
        s.state = s.latest.is_object() && newer(s.latest.value("version", ""), s.version) ? State::Available : State::Unchecked;
    }
    ++s.serial;
    save(s);
    std::fprintf(stderr, "SRW64_UPDATE_CHECK %s %s\n", error.empty() ? s.latest.value("version", "").c_str() : "failed",
                 error.c_str());
}
}

namespace {
std::optional<Release> describe(const json& latest, std::string_view language) {
    if (!latest.is_object() || latest.value("schema", "") != "srw64.latest.v1" || !latest.contains("version") ||
        !latest["version"].is_string())
        return std::nullopt;
    const auto page = [&](const char* key) {
        const auto found = latest.find(key);
        if (found == latest.end() || !found->is_object()) return std::string();
        const std::string lang(language);
        if (found->contains(lang) && (*found)[lang].is_string()) return (*found)[lang].get<std::string>();
        return found->contains("en") && (*found)["en"].is_string() ? (*found)["en"].get<std::string>() : std::string();
    };
    Release release{latest["version"].get<std::string>(), latest.value("date", ""), page("notes"), page("download")};
    if (release.download.empty()) release.download = std::string(kSite) + "/" + std::string(language) + "/install/";
    return release;
}
}
std::optional<Release> parse(const std::string& body, std::string_view language) {
    return describe(json::parse(body, nullptr, false), language);
}

bool supported() {
#ifdef __ANDROID__
    return false;
#else
    return true;
#endif
}

void init(std::string_view version) {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    s.version = version;
    if (const char* file = std::getenv("SRW64_UPDATE_STATE"); file && *file) s.file = file;
    if (s.file.empty() || !std::filesystem::exists(s.file)) return;
    std::ifstream in(s.file);
    const auto saved = json::parse(in, nullptr, false);
    if (!saved.is_object() || saved.value("schema", "") != "srw64.update-check.v1") return;
    if (saved.contains("automatic") && saved["automatic"].is_boolean()) s.automatic = saved["automatic"].get<bool>();
    if (saved.contains("checked_at") && saved["checked_at"].is_number_integer()) s.checked_at = saved["checked_at"].get<int64_t>();
    if (saved.contains("latest") && saved["latest"].is_object()) s.latest = saved["latest"];
    // A release found by an earlier check, until this build is that release or newer.
    if (s.latest.is_object() && newer(s.latest.value("version", ""), s.version)) s.state = State::Available;
}

std::optional<bool> automatic() {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    return s.automatic;
}

void set_automatic(bool on) {
    auto& s = shared();
    {
        std::lock_guard lock(s.mutex);
        s.automatic = on;
        ++s.serial;
        save(s);
    }
    if (on) check_on_start();
}

bool should_ask() {
    if (!supported() || std::getenv("SRW64_DEBUG") || std::getenv("SRW64_BACKGROUND")) return false;
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    return !s.file.empty() && !s.automatic;
}

void check(bool manual) {
    if (!supported()) return;
    auto& s = shared();
    {
        std::lock_guard lock(s.mutex);
        if (s.running) return;
        s.running = true;
        if (manual) {
            s.state = State::Checking;
            ++s.serial;
        }
    }
    std::thread(run, manual).detach();
}

void check_on_start() {
    auto& s = shared();
    {
        std::lock_guard lock(s.mutex);
        if (!s.automatic.value_or(false) || s.running) return;
        const int64_t since = now() - s.checked_at;
        if (since >= 0 && since < kDay) return;
    }
    check(false);
}

Status status(std::string_view language) {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    Status status{s.state, {}, s.error, s.serial};
    if (const auto release = describe(s.latest, language)) status.latest = *release;
    return status;
}

bool open_url(const std::string& url) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    return SDL_OpenURL(url.c_str()) == 0;
#else
    (void)url;
    return false;
#endif
}
}
