#pragma once
// Windows PCs RT64 cannot draw on (docs/design/windows-gl-fallback.md): Marchwind64.exe, the
// RT64 host, looks at the GPUs before anything else and runs Marchwind64-gl.exe beside it, the
// GL host over ANGLE (Direct3D 11), when none can take RT64: no adapter with D3D12 at feature
// level 11_0, shader model 6.0 and ID3D12Device8 (Windows 10 2004), and no Vulkan 1.2 device.
// SRW64_GRAPHICS_API chooses instead: gl for the GL host, d3d12 or vulkan for RT64 whatever the
// check says. Without Marchwind64-gl.exe (a development build) the RT64 host carries on.
#include <string>

namespace srw64::renderer_fallback {
#ifdef _WIN32
// At the top of main, before anything is set up, for the starts that open the game's window
// (no arguments, --choose-rom, --play). Runs the GL host with the same command line and the
// same standard handles, waits for it and returns its exit code; -1 to go on in this process.
int run_gl_host_if_needed(int argc, char** argv);
// RT64 failed to start after all (graphics.cpp graphics_failure): starts the GL host in this
// process's place. True when it started; the caller then leaves at once, so the GL host takes
// the user directory's lock (UserLock waits for a game still exiting).
bool hand_over(const std::string& reason);
#else
inline int run_gl_host_if_needed(int, char**) { return -1; }
inline bool hand_over(const std::string&) { return false; }
#endif
}  // namespace srw64::renderer_fallback
