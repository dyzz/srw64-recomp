#pragma once
// Windows: a crash writes a minidump into the run directory, <run>/crash-<date>-<time>.dmp,
// and one SRW64_CRASH line (exception code, address, module) to stderr and console.log,
// for a bug report (docs/native/bug-report.md). Other systems keep their signal handlers
// (console_log.cpp, host.cpp crash_backtrace); this is Windows only
// (src/host/windows/crash_dump_windows.cpp).
#include <filesystem>

namespace srw64::crash_dump {
// After console_log::start (whose SIGABRT handler runs after this one's): the unhandled
// exception filter, the CRT's invalid-parameter, pure-call and terminate handlers, SIGABRT.
void install();
// The run directory, once known; until then a crash writes only the line. With
// SRW64_CRASH_TEST set (access, stack, abort, invalid, terminate) it crashes right here,
// to try the handlers.
void attach(const std::filesystem::path& run);
}
