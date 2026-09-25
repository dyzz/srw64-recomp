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
}
