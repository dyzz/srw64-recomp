#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include "update_version.hpp"

// The update check (docs/native/update-check.md): reads the website's /latest.json and
// says whether a newer release is out. It only reads: no identifier goes with the
// request, nothing is downloaded or installed, and the player opens the download page
// in their browser. A check on start-up is the player's choice, asked once on the
// title and kept in update.json (SRW64_UPDATE_STATE), at most one a day; the About page
// and the macOS application menu check on demand. Not on Android.
// The HD pack has versions of its own (dates, in its hd.json); /latest.json names the
// newest, and a player with an older pack installed is told so too.
namespace srw64::update {
constexpr const char* kLatestUrl = "https://srw64.dreamquest.club/latest.json";
constexpr const char* kSite = "https://srw64.dreamquest.club";
constexpr const char* kSource = "https://github.com/dyzz/srw64-recomp";
constexpr const char* kIssues = "https://github.com/dyzz/srw64-recomp/issues";

enum class State { Unchecked, Checking, Current, Available, Failed };
struct Release {
    std::string version, date;
    std::string notes, download;  // pages in the player's language
};
// The HD pack: the installed one (hd.json beside SRW64_ART_PACK) and the website's.
struct HdPack {
    bool installed = false;
    std::string version;   // installed; empty for a pack from before HD versions
    std::string latest;    // the website's, once a check has read it
    std::string download;  // its download page, in the player's language
    bool available = false;  // hd_newer(): latest is one to offer
};
struct Status {
    State state = State::Unchecked;
    Release latest;     // Current and Available; Available also from a past check
    HdPack hd;
    std::string error;  // Failed
    uint64_t serial = 0;  // changes with every new status, for redraws
};

// newer() and site_language() are in update_version.hpp.
// A /latest.json body for this language; nullopt when it is not one.
std::optional<Release> parse(const std::string& body, std::string_view language);

// Window thread. `version` is this build's (SRW64_VERSION). Reads update.json and the
// installed HD pack's hd.json.
void init(std::string_view version);
bool supported();
// The start-up check's switch: nullopt until the player has answered.
std::optional<bool> automatic();
void set_automatic(bool on);
// Whether to ask now: supported, never answered, and not a debug or scripted run.
bool should_ask();
// Starts a check in the background unless one is running. `manual`: the player asked,
// so it runs whatever the switch and the last check say.
void check(bool manual);
// The start-up check, if the switch is on and the last was a day or more ago.
void check_on_start();
// Pages in this website language (site_language).
Status status(std::string_view language);
// Opens a page in the system browser; false when the system could not.
bool open_url(const std::string& url);

// One HTTP GET on the calling thread (update_http.cpp, macos/update_http_macos.mm):
// the body of a 200, else throws with the reason. Gives up after `timeout_seconds`.
std::string http_get(const std::string& url, int timeout_seconds);
}
