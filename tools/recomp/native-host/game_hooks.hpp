#pragma once
#include "recomp.h"

// Installed before starting guest threads. Empty callbacks preserve the original
// routines in the CPU-only and fixed-frame diagnostic hosts.
struct SRW64GameHooks {
    void (*presentation_step)(uint8_t*){};
    bool (*dialogue_step)(uint8_t*, recomp_context*){};
    void (*text_loaded)(uint8_t*, unsigned){};
    void (*text_drawn)(uint8_t*, uint32_t, uint32_t){};
    void (*reset)(uint8_t*){};
    bool (*script_before)(uint8_t*, uint32_t){};
    void (*script_after)(uint8_t*, uint32_t){};
    void (*choice)(uint8_t*){};
    void (*intro_step)(uint8_t*, recomp_context*){};
    void (*name_begin)(uint8_t*, unsigned){};
    bool (*name_step)(uint8_t*, recomp_context*, unsigned){};
};
extern SRW64GameHooks srw64_game_hooks;
extern "C" void srw64_original_dialogue_step(uint8_t*, recomp_context*);
