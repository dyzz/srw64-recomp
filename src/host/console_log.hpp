#pragma once
// What the program prints, kept for a bug report (docs/native/bug-report.md). Started
// first thing in main: stdout and stderr go through pipes, on to where they went before
// (the terminal, or logcat on Android, where they would otherwise be dropped) and into
// <run>/console.log once the run directory is known. Lines from before then are held in
// memory (up to 1 MiB) and written first.
#include <filesystem>

namespace srw64::console_log {
void start();
void attach(const std::filesystem::path& run);
// The open console.log for a crash handler to write to directly (the pipe may never be
// drained once the process is dying); -1 before attach.
int crash_fd();
}
