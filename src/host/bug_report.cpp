#include "bug_report.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include <SDL.h>
#include "miniz.h"
#include "steam_deck.hpp"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__ANDROID__)
#include <sys/system_properties.h>
#include <sys/utsname.h>
#elif defined(__linux__)
#include <sys/utsname.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace srw64::bug_report {
namespace {
namespace fs = std::filesystem;

// Settings files of the user directory, by their name in the report.
constexpr std::pair<const char*, const char*> kSettings[] = {
    {"settings/presentation.json", "presentation.json"}, {"settings/input.json", "input.json"},
    {"settings/rules.json", "rules.json"}, {"settings/update.json", "update.json"},
    {"settings/saves.json", "saves/settings.json"}, {"settings/hd.json", "hd/hd.json"},
};
// A run directory's files that go in: its logs and the state it writes. Never debug.json
// (the debug interface's token), the ROM copy in runtime-data/, audio-output.s16 or captures.
bool run_file(const fs::path& file) {
    const auto name = file.filename().string(), ext = file.extension().string();
    if (name == "debug.json" || name.ends_with(".tmp")) return false;
    return ext == ".json" || ext == ".jsonl" || ext == ".log" || ext == ".txt";
}
constexpr size_t kFileLimit = 4u << 20;   // the end of a longer file
constexpr unsigned kSessions = 3;
// A crash's minidump (Windows, crash_dump.hpp): as it is, the newest two of a run, none
// larger than this (they are a few MB).
constexpr size_t kDumpLimit = 32u << 20;
constexpr size_t kDumps = 2;
bool dump_file(const fs::path& file) { return file.extension() == ".dmp"; }

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto size = size_t(std::max<std::streamoff>(in.tellg(), 0));
    const size_t from = size > kFileLimit ? size - kFileLimit : 0;
    in.seekg(std::streamoff(from));
    std::string text(size - from, '\0');
    in.read(text.data(), std::streamsize(text.size()));
    if (from) text = "[... first " + std::to_string(from) + " bytes left out ...]\n" + text.substr(text.find('\n') + 1);
    return text;
}

std::string read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size()) text.replace(at, from.size(), to);
}

std::string home() {
#ifdef _WIN32
    const char* value = std::getenv("USERPROFILE");
#else
    const char* value = std::getenv("HOME");
#endif
    return value && *value ? std::string(value) : std::string();
}

std::string trim(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '"')) text.pop_back();
    while (!text.empty() && text.front() == '"') text.erase(0, 1);
    return text;
}

#if defined(__APPLE__)
std::string sysctl_text(const char* name) {
    size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return {};
    std::string text(size, '\0');
    if (sysctlbyname(name, text.data(), &size, nullptr, 0) != 0) return {};
    return std::string(text.c_str());
}
#elif defined(__ANDROID__)
std::string property(const char* name) {
    char value[PROP_VALUE_MAX] = {};
    __system_property_get(name, value);
    return value;
}
#elif defined(__linux__)
std::string os_release(const char* key) {
    std::ifstream in("/etc/os-release");
    for (std::string line; std::getline(in, line);)
        if (line.starts_with(std::string(key) + "=")) return trim(line.substr(std::strlen(key) + 1));
    return {};
}
std::string first_line(const char* path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    return trim(line);
}
#endif

const char* architecture() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86-64";
#else
    return "other";
#endif
}

std::string stamp(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), format, &local);
    return text;
}

// The sessions the user directory keeps, this run's first, then the newest.
std::vector<fs::path> sessions(const fs::path& user, const fs::path& run) {
    std::vector<std::pair<fs::file_time_type, fs::path>> found;
    std::error_code error;
    const auto current = run.parent_path();
    for (const auto& entry : fs::directory_iterator(user / "sessions", error))
        if (entry.is_directory(error) && entry.path() != current)
            found.emplace_back(entry.last_write_time(error), entry.path());
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<fs::path> list;
    if (fs::is_directory(current, error) && current.parent_path().filename() == "sessions") list.push_back(current);
    for (const auto& [time, path] : found)
        if (list.size() < kSessions) list.push_back(path);
    return list;
}
}

std::string redact(std::string text) {
    const auto mine = home();
    if (mine.size() < 2) return text;
    replace_all(text, mine, "~");
#ifdef _WIN32
    std::string escaped, slashed = mine;
    for (const char c : mine) escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
    std::replace(slashed.begin(), slashed.end(), '\\', '/');
    replace_all(text, escaped, "~");
    replace_all(text, slashed, "~");
#endif
    return text;
}

nlohmann::json system() {
    std::string platform = SDL_GetPlatform();
    if (platform == "Mac OS X") platform = "macOS";
    nlohmann::json s = {{"platform", platform}, {"architecture", architecture()},
                        {"cpu_count", SDL_GetCPUCount()}, {"memory_mb", SDL_GetSystemRAM()}, {"steam_deck", on_steam_deck()}};
#if defined(__APPLE__)
    s["version"] = sysctl_text("kern.osproductversion");
    s["build"] = sysctl_text("kern.osversion");
    s["model"] = sysctl_text("hw.model");
    s["cpu"] = sysctl_text("machdep.cpu.brand_string");
#elif defined(__ANDROID__)
    s["version"] = property("ro.build.version.release");
    s["sdk"] = property("ro.build.version.sdk");
    s["model"] = property("ro.product.manufacturer") + " " + property("ro.product.model");
    s["cpu"] = property("ro.soc.model");
    utsname u{};
    if (uname(&u) == 0) s["kernel"] = u.release;
#elif defined(__linux__)
    s["version"] = os_release("PRETTY_NAME");
    s["model"] = first_line("/sys/devices/virtual/dmi/id/sys_vendor") + " " + first_line("/sys/devices/virtual/dmi/id/product_name");
    utsname u{};
    if (uname(&u) == 0) s["kernel"] = u.release;
    if (const char* desktop = std::getenv("XDG_CURRENT_DESKTOP")) s["desktop"] = desktop;
    if (const char* session = std::getenv("XDG_SESSION_TYPE")) s["session"] = session;
#elif defined(_WIN32)
    // RtlGetVersion: GetVersionEx answers 6.2 to programs without a manifest naming Windows 10.
    using Get = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (const auto get = reinterpret_cast<Get>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) {
        OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        if (get(&info) == 0)
            s["version"] = std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." + std::to_string(info.dwBuildNumber);
    }
    if (std::getenv("WINEPREFIX") || GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) s["wine"] = true;
#endif
    return s;
}

std::string summary(const nlohmann::json& facts) {
    const auto sys = system();
    const auto text = [](const nlohmann::json& j, const char* key) {
        const auto it = j.find(key);
        if (it == j.end() || it->is_null()) return std::string();
        return it->is_string() ? it->get<std::string>() : it->dump();
    };
    const auto join = [](std::initializer_list<std::string> parts) {
        std::string line;
        for (const auto& part : parts)
            if (!part.empty() && part != "\"\"") line += (line.empty() ? "" : " · ") + part;
        return line;
    };
    const auto hd = facts.value("hd_pack", nlohmann::json::object());
    std::string os = text(sys, "platform");
    if (const auto v = text(sys, "version"); !v.empty()) os += " " + v;
    if (const auto b = text(sys, "build"); !b.empty()) os += " (" + b + ")";
    if (sys.value("steam_deck", false)) os += " · Steam Deck";
    if (sys.value("wine", false)) os += " · Wine";
    const auto memory = sys.value("memory_mb", 0);
    const auto graphics = facts.value("graphics", nlohmann::json::object());
    const auto window = facts.value("window", nlohmann::json::object());
    std::string size;
    if (window.is_object() && window.contains("width"))
        size = std::to_string(window.value("width", 0)) + "x" + std::to_string(window.value("height", 0)) +
               " (" + std::to_string(window.value("pixel_width", 0)) + "x" + std::to_string(window.value("pixel_height", 0)) + " px)" +
               (window.value("fullscreen", false) ? " fullscreen" : "");
    std::string lines;
    lines += "Marchwind64 " + text(facts, "version") + " · images " + text(facts, "images") + " · HD pack " +
             (!hd.value("installed", false) ? std::string("none") : hd.value("version", std::string()).empty() ? std::string("unversioned") : hd.value("version", std::string())) + "\n";
    lines += "System: " + join({os, text(sys, "model"), text(sys, "cpu"), text(sys, "architecture"),
                                memory ? std::to_string((memory + 512) / 1024) + " GB" : std::string(), text(sys, "kernel")}) + "\n";
    lines += "Graphics: " + join({text(graphics, "api"), text(graphics, "device"), text(graphics, "vendor")}) + "\n";
    lines += "Window: " + join({size, "UI " + text(facts, "ui_size"), text(facts, "locale"), facts.value("wide", false) ? "wide" : "4:3",
                                text(facts, "filter").empty() ? std::string() : "filter " + text(facts, "filter"),
                                text(facts, "bezel").empty() ? std::string() : "bezel " + text(facts, "bezel")}) + "\n";
    lines += "Input: " + join({text(facts, "controller").empty() ? std::string("no controller") : text(facts, "controller"),
                               facts.value("touch", false) ? "touch" : "", facts.value("handheld", false) ? "handheld" : ""}) + "\n";
    const auto count = [&](const char* key) { const auto it = facts.find(key); return it != facts.end() && it->is_array() ? it->size() : size_t(0); };
    lines += "Rules: " + std::to_string(count("rules")) + " fixes on, " + std::to_string(count("cheats")) + " cheats on";
    return redact(lines);
}

Result write(const fs::path& user, const fs::path& run, nlohmann::json facts) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 1 << 20)) return {{}, "zip"};
    bool ok = true;
    const auto add = [&](const std::string& name, const std::string& text) {
        ok = ok && mz_zip_writer_add_mem(&zip, name.c_str(), text.data(), text.size(), MZ_BEST_COMPRESSION);
    };
    std::error_code error;
    nlohmann::json files = nlohmann::json::array();
    for (const auto& [name, relative] : kSettings) {
        const auto path = user / relative;
        if (!fs::is_regular_file(path, error)) continue;
        add(name, redact(read_text(path)));
        files.push_back(name);
    }
    for (const auto& session : sessions(user, run)) {
        const auto base = "sessions/" + session.filename().string() + "/";
        if (fs::is_regular_file(session / "launch.json", error)) {
            add(base + "launch.json", redact(read_text(session / "launch.json")));
            files.push_back(base + "launch.json");
        }
        std::vector<fs::path> logs, dumps;
        for (const auto& entry : fs::directory_iterator(session / "run", error)) {
            // The file's own size: on Windows the listing's is that of when it was last
            // closed, 0 for this run's logs, which are still open.
            if (!entry.is_regular_file(error)) continue;
            const auto size = fs::file_size(entry.path(), error);
            if (error || size == 0) continue;
            if (run_file(entry.path())) logs.push_back(entry.path());
            else if (dump_file(entry.path()) && size <= kDumpLimit) dumps.push_back(entry.path());
        }
        std::sort(logs.begin(), logs.end());
        for (const auto& path : logs) {
            add(base + "run/" + path.filename().string(), redact(read_text(path)));
            files.push_back(base + "run/" + path.filename().string());
        }
        // Binary, so not through redact: crash-<date>-<time>.dmp, the newest last.
        std::sort(dumps.begin(), dumps.end());
        if (dumps.size() > kDumps) dumps.erase(dumps.begin(), dumps.end() - kDumps);
        for (const auto& path : dumps) {
            add(base + "run/" + path.filename().string(), read_bytes(path));
            files.push_back(base + "run/" + path.filename().string());
        }
    }
    const nlohmann::json report = {{"schema", "srw64.bug-report.v1"}, {"created", stamp("%Y-%m-%dT%H:%M:%S%z")},
                                   {"system", system()}, {"game", std::move(facts)}, {"files", files}};
    add("report.json", redact(report.dump(2)) + "\n");
    void* buffer = nullptr;
    size_t size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    mz_zip_writer_end(&zip);
    if (!ok) { if (buffer) mz_free(buffer); return {{}, "zip"}; }
    const auto folder = user / "reports";
    fs::create_directories(folder, error);
    const auto path = folder / ("Marchwind64-report-" + stamp("%Y%m%d-%H%M%S") + ".zip");
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(buffer), std::streamsize(size));
    out.close();
    mz_free(buffer);
    if (!out) return {{}, path.string()};
    return {path, {}};
}
}
