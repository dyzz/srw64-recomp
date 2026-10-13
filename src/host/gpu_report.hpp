#pragma once
// What a player's PC offers RT64, written as SRW64_GPU lines before RT64 chooses, so a
// console.log from a game that never drew says why. Windows: each display adapter's D3D12
// support against what plume's D3D12 device asks (feature level 11_0, shader model 6.0,
// ID3D12Device8) and its driver version, then the Vulkan loader and its surface
// extensions (src/host/windows/gpu_report_windows.cpp). Other systems: nothing.
#include <string>
#include <vector>

namespace srw64::gpu_report {
#ifdef _WIN32
// The hardware adapters' names, for the startup failure message (startup_failure.hpp).
std::vector<std::string> log();
#else
inline std::vector<std::string> log() { return {}; }
#endif
}
