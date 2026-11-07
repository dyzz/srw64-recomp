#pragma once
#include <cstdlib>
#include <cstring>

// What RecompLauncher tells every game it starts (its developer docs, "What every game is told"):
// RECOMPLAUNCHER=1, and RECOMPLAUNCHER_MODE=bigpicture when Play was pressed in its full-screen
// view. It installs and updates the program itself, so the game does not offer new versions.
namespace srw64::launcher_env {
inline bool active() {
    static const bool value = [] { const char* v = std::getenv("RECOMPLAUNCHER"); return v && !std::strcmp(v, "1"); }();
    return value;
}
inline bool big_picture() {
    static const bool value = [] { const char* v = std::getenv("RECOMPLAUNCHER_MODE"); return active() && v && !std::strcmp(v, "bigpicture"); }();
    return value;
}
}  // namespace srw64::launcher_env
