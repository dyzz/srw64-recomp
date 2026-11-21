#pragma once
// What the program prints, kept for a bug report (docs/native/bug-report.md). Started
// first thing in main: stdout and stderr go through pipes, on to where they went before
// (the terminal, or logcat on Android, where they would otherwise be dropped) and into
// <run>/console.log once the run directory is known (attach does not create it). Lines
// from before then are held in memory (up to 1 MiB) and written first.
#include <filesystem>

namespace srw64::console_log {
void start();
void attach(const std::filesystem::path& run);
// A start that failed before attach (a refused setting, a ROM that cannot be read) leaves no
// console.log. Under RecompLauncher, whose crash report takes the newest .log of <user>/logs,
// what was held goes to <user>/logs/latest.log instead, or the report shows another program's
// log (issue #29). Nothing once attached or without the launcher.
void keep_unattached(const std::filesystem::path& user_dir);
// For a crash handler (async-signal-safe): pass on what is still in the pipes now, the
// open console.log (-1 before attach) and the original stderr, to write to directly
// once the process is dying (Windows: flush_now gives the pumps a moment, as the pipes
// cannot be read past them). start() already does this for abort() and crashes, before
// the handler that was there; a handler installed later calls flush_now itself.
void flush_now();
int crash_fd();
int crash_copy_fd();  // logs/latest.log under RecompLauncher, else -1
int original_stderr();
}
