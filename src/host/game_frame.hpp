#pragma once
// The game picture in the window (docs/design/deck-16x10.md). The original is 320 x 240,
// 4:3. By default the picture follows the screen's shape from 4:3 to 16:9: 384 x 240 on a
// Steam Deck's 16:10, 400 x 240 on a 5:3 800 x 480 handheld, 426.7 x 240 at 16:9, with the
// original 320 centred in it; a wider screen keeps 16:9 and a narrower one 4:3, the rest
// black. RT64 draws the wider picture (graphics.cpp) and everything the host draws over
// the original's 320 x 240 places it with scale(). Saved as aspect with the locale
// ("auto" or "4:3", presentation_settings.cpp); read on any thread.
#include <algorithm>
#include <atomic>

namespace srw64::frame {
inline constexpr float kWidth = 320, kHeight = 240, kMaxWidth = kHeight * 16 / 9;
// Follow the screen (true) or keep the original 4:3.
inline std::atomic_bool wide{true};
// The picture's width in original pixels for a window of w x h pixels.
inline float width(float w, float h) {
    if (!wide.load(std::memory_order_relaxed) || !(w > 0 && h > 0)) return kWidth;
    return std::clamp(kHeight * w / h, kWidth, kMaxWidth);
}
// The picture RT64 draws now, in original pixels (graphics.cpp sets it with the window's
// shape); the game thread sizes the tactical map's view from it (wide_map.hpp).
inline std::atomic<float> picture_width{kWidth};
// Window pixels per original pixel. The original 320 x 240 sits centred whatever the
// picture's width, at ((w - 320 * scale) / 2, (h - 240 * scale) / 2).
inline float scale(float w, float h) { return std::min(w / width(w, h), h / kHeight); }
}  // namespace srw64::frame
