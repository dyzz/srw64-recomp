#pragma once
#include <atomic>
#include <cstdint>

namespace srw64::input {
// The last input came from a controller, so on-screen hints name its buttons
// (the "_pad" UI labels). The shared UI sets it; the dialogue reader copies it
// into each frame snapshot so a hint never changes inside a drawn frame.
inline std::atomic_bool pad_hints{false};

// Controller bits above the N64 mask in srw64_pad_state(). The game never sees
// them: srw64_keyboard_input keeps 16 button bits and the four stick bits.
inline constexpr uint32_t pad_view = 1u << 20;  // View / Back: the settings window
// The triggers are the host's (docs/design/steam-deck-controls.md). In dialogue, L2 turns
// automatic reading on / off, R2 held fast-forwards and R2 + Menu skips the segment; on the
// idle tactical map they put the cursor on the previous / next enemy (enemy_cycle.cpp).
inline constexpr uint32_t pad_l2 = 1u << 21;
inline constexpr uint32_t pad_r2 = 1u << 22;
}
