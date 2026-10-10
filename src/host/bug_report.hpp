#pragma once
// A bug report for the player to attach to an issue (docs/native/bug-report.md): one zip
// in <user>/reports with report.json (the version, the system, the graphics, the window,
// the game's settings in effect: `facts`, gathered by the caller), the settings files of
// the user directory, and the logs of the runs the user directory keeps (sessions/*:
// launch.json, console.log and the run's event and state files, and on Windows a crash's
// minidump). Never the ROM or its copy, saves, recorded audio or the debug interface's
// token; the home folder in every text file is written as ~ (a minidump goes as it is).
#include <filesystem>
#include <string>

#include "json/json.hpp"

namespace srw64::bug_report {
struct Result {
    std::filesystem::path zip;   // empty when it failed
    std::string error;
};
// `user`: the user directory; `run`: this run's directory, whose session goes first.
Result write(const std::filesystem::path& user, const std::filesystem::path& run, nlohmann::json facts);
// A few lines to paste into an issue: the version and HD pack, the system, the graphics,
// the window and interface, the controller (system() and the caller's facts).
std::string summary(const nlohmann::json& facts);
// The system this runs on: name, version, model, processor, memory, Steam Deck.
nlohmann::json system();
// Home folder → ~ (also in its JSON-escaped spelling on Windows).
std::string redact(std::string text);
}
