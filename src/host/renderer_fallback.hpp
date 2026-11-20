#pragma once
// Windows PCs RT64 cannot draw on (docs/design/windows-gl-fallback.md): Marchwind64.exe looks
// at the GPUs before anything else and draws with its GL renderer over ANGLE (Direct3D 11,
// libEGL.dll and libGLESv2.dll beside it) when none can take RT64: no adapter with D3D12 at
// feature level 11_0, shader model 6.0 and ID3D12Device8 (Windows 10 2004), other than
// Microsoft's WARP, and no Vulkan 1.2 GPU. SRW64_GRAPHICS_API chooses instead: gl for the GL
// renderer, d3d12 or vulkan for RT64 whatever the check says. Without ANGLE's DLLs (a
// development build) RT64 stays.
#include <string>

namespace srw64::renderer_fallback {
#ifdef _WIN32
// At the top of main, before anything is set up, for the starts that open the game's window
// (no arguments, --choose-rom, --play): whether the GL renderer draws (graphics.cpp gl_mode).
void decide(int argc, char** argv);
bool gl_chosen();
// RT64 failed to start after all (graphics.cpp graphics_failure): this program again, with the
// GL renderer chosen. True when it started; the caller then leaves at once, so the new process
// takes the user directory's lock (UserLock waits for a game still exiting).
bool hand_over(const std::string& reason);
#else
inline void decide(int, char**) {}
inline bool gl_chosen() { return false; }
inline bool hand_over(const std::string&) { return false; }
#endif
}  // namespace srw64::renderer_fallback
