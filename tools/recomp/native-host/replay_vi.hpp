#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

// Old task captures contain the OSViMode table but not the live VI registers.
// Reconstruct the control word only when the captured game code still matches
// its reviewed SetMode -> SetSpecialFeatures sequence at 8008BFB4..8008BFD8.
inline uint32_t srw64_replay_vi_status(const uint8_t* big_endian_ram, size_t size, uint32_t mode_status) {
    constexpr size_t at = 0x8BFB4;
    constexpr std::array<uint32_t, 10> instructions = {
        0x0C02D82C, 0x00822021,  // osViSetMode(mode)
        0x0C02D840, 0x24040002,  // OS_VI_GAMMA_OFF
        0x0C02D840, 0x24040004,  // OS_VI_GAMMA_DITHER_ON
        0x0C02D840, 0x24040080,  // OS_VI_DITHER_FILTER_OFF
        0x0C02D840, 0x24040020,  // OS_VI_DIVOT_OFF
    };
    if (size < at + instructions.size()*4) throw std::runtime_error("Replay VI signature outside capture");
    for (size_t i=0; i<instructions.size(); ++i) {
        const auto* p=big_endian_ram+at+i*4;
        const uint32_t word=(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
        if (word != instructions[i]) throw std::runtime_error("Unrecognized replay VI setup; live register capture required");
    }
    // The mode's AA bits are retained, matching DITHER_FILTER_OFF after SetMode.
    constexpr uint32_t gamma_dither = 0x4, gamma = 0x8, divot = 0x10, dither_filter = 0x10000;
    return (mode_status | gamma_dither) & ~(gamma | divot | dither_filter);
}
