#pragma once
// Small differences between the POSIX hosts and Windows (docs/design/three-platform-port.md, X3).
#include <cstdio>
#ifdef _WIN32
// stdio's per-stream lock: POSIX flockfile / funlockfile, the CRT's _lock_file / _unlock_file.
inline void flockfile(FILE* file) { _lock_file(file); }
inline void funlockfile(FILE* file) { _unlock_file(file); }
#endif
