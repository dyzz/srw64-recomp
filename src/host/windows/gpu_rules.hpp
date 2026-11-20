#pragma once
// GPUs the start-up check treats by name rather than by what their driver says
// (renderer_fallback_windows.cpp; listed in the SRW64_GPU report, gpu_report_windows.cpp).
// docs/design/windows-gl-fallback.md, "显卡名单".
//
// - Vulkan: RT64, but not on Direct3D 12, whose driver fails there. AMD's Vega integrated
//   graphics: the 5600G's Vega 7 (driver 31.0.21925.1001) hung under D3D12 and the driver
//   reset, and Vulkan played (issue #12); other N64 recompilations saw the same on these.
// - GL: never RT64; the GL renderer over ANGLE. The generations below RT64's minimum (Intel
//   HD 510, GeForce GT 630, Radeon HD 7750, as Zelda64Recomp's README gives them): their
//   drivers have no shader model 6.0 or Vulkan 1.2 and the check sends them to GL anyway;
//   the list keeps them there should a driver claim otherwise.
//
// A listing only ever moves a GPU down (D3D12 to Vulkan, RT64 to GL). The player still
// chooses with SRW64_GRAPHICS_API (gl, d3d12, vulkan). Device ids are PCI ids; the ranges are
// per generation and stop short of the next (Intel Gen 9 from 0x1902, NVIDIA Kepler, AMD GCN).
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace srw64::gpu_rules {
enum class Action : uint8_t { none, vulkan, gl };

struct Rule {
    uint16_t vendor, first, last;   // device ids first..last, inclusive
    Action action;
    const char* name;
};

inline constexpr Rule kRules[] = {
    // AMD Vega integrated graphics (GCN 5): D3D12 resets the driver (issue #12).
    {0x1002, 0x15D8, 0x15D8, Action::vulkan, "amd-vega-igpu"},   // Picasso, Dali, Pollock
    {0x1002, 0x15DD, 0x15DD, Action::vulkan, "amd-vega-igpu"},   // Raven Ridge
    {0x1002, 0x15E7, 0x15E7, Action::vulkan, "amd-vega-igpu"},   // Barcelo
    {0x1002, 0x1636, 0x1636, Action::vulkan, "amd-vega-igpu"},   // Renoir
    {0x1002, 0x1638, 0x1638, Action::vulkan, "amd-vega-igpu"},   // Cezanne (5600G, 5700G)
    {0x1002, 0x164C, 0x164C, Action::vulkan, "amd-vega-igpu"},   // Lucienne

    // Intel before Gen 9 (Skylake, HD 510): Ironlake, Sandy Bridge, Ivy Bridge, Bay Trail,
    // Haswell, Broadwell, Cherry Trail and Braswell.
    {0x8086, 0x0042, 0x0046, Action::gl, "intel-gen5"},          // Ironlake
    {0x8086, 0x0102, 0x0126, Action::gl, "intel-gen6"},          // Sandy Bridge
    {0x8086, 0x0152, 0x016A, Action::gl, "intel-gen7"},          // Ivy Bridge
    {0x8086, 0x0F31, 0x0F33, Action::gl, "intel-gen7"},          // Bay Trail
    {0x8086, 0x0402, 0x041E, Action::gl, "intel-gen7.5"},        // Haswell
    {0x8086, 0x0A02, 0x0A2E, Action::gl, "intel-gen7.5"},        // Haswell ULT
    {0x8086, 0x0C02, 0x0C2E, Action::gl, "intel-gen7.5"},        // Haswell SDV
    {0x8086, 0x0D02, 0x0D2E, Action::gl, "intel-gen7.5"},        // Haswell CRW (Iris Pro)
    {0x8086, 0x1602, 0x163E, Action::gl, "intel-gen8"},          // Broadwell
    {0x8086, 0x22B0, 0x22B3, Action::gl, "intel-gen8"},          // Cherry Trail, Braswell

    // NVIDIA Fermi (GeForce 400 and 500, GT 610 to 630 on GF1xx) and older Tesla parts.
    {0x10DE, 0x0400, 0x06DF, Action::gl, "nvidia-tesla-fermi"},  // G8x to GT200, GF100
    {0x10DE, 0x0A20, 0x0A7F, Action::gl, "nvidia-tesla"},        // GT21x
    {0x10DE, 0x0CA0, 0x0CBF, Action::gl, "nvidia-tesla"},        // GT215
    {0x10DE, 0x0DC0, 0x0DFF, Action::gl, "nvidia-fermi"},        // GF106, GF108
    {0x10DE, 0x0E22, 0x0E3B, Action::gl, "nvidia-fermi"},        // GF104
    {0x10DE, 0x1040, 0x109B, Action::gl, "nvidia-fermi"},        // GF119, GF110
    {0x10DE, 0x1140, 0x1141, Action::gl, "nvidia-fermi"},        // GF117
    {0x10DE, 0x1200, 0x1251, Action::gl, "nvidia-fermi"},        // GF114, GF116

    // AMD TeraScale (Radeon HD 2000 to 6000, and the APUs before GCN).
    {0x1002, 0x9400, 0x95FF, Action::gl, "amd-terascale"},       // R600, RV6xx, RV7xx
    {0x1002, 0x9640, 0x964F, Action::gl, "amd-terascale"},       // Sumo (Llano)
    {0x1002, 0x9800, 0x980A, Action::gl, "amd-terascale"},       // Palm (Brazos)
    {0x1002, 0x9900, 0x99A4, Action::gl, "amd-terascale"},       // Trinity, Richland
    {0x1002, 0x6700, 0x677F, Action::gl, "amd-terascale"},       // Cayman, Barts, Turks, Caicos
    {0x1002, 0x6880, 0x68FF, Action::gl, "amd-terascale"},       // Evergreen
};

// The rule for a GPU, or nullptr. SRW64_GPU_RULE_TEST=VVVV:DDDD:gl or :vulkan (hexadecimal ids)
// lists one GPU more, to try the rules on a PC whose GPU is not listed.
inline const Rule* find(uint32_t vendor, uint32_t device) {
    static const Rule test = [] {
        Rule r{0, 0, 0, Action::none, "test"};
        unsigned v = 0, d = 0;
        char action[8] = {};
        if (const char* value = std::getenv("SRW64_GPU_RULE_TEST"); value && std::sscanf(value, "%x:%x:%7s", &v, &d, action) == 3) {
            r.vendor = uint16_t(v); r.first = r.last = uint16_t(d);
            r.action = action[0] == 'g' ? Action::gl : Action::vulkan;
        }
        return r;
    }();
    if (test.action != Action::none && test.vendor == vendor && device >= test.first && device <= test.last) return &test;
    for (const auto& rule : kRules)
        if (rule.vendor == vendor && device >= rule.first && device <= rule.last) return &rule;
    return nullptr;
}
}  // namespace srw64::gpu_rules
