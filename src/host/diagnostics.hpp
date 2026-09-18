#pragma once
#include <cstdlib>
#include <string_view>

// Probe captures are deliberately expensive. The player launcher uses light
// diagnostics; explicit full probes retain the existing evidence pipeline.
inline bool srw64_full_diagnostics() {
    const char* mode=std::getenv("SRW64_DIAGNOSTICS");
    return !mode || std::string_view(mode)=="full";
}
