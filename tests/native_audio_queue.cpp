#include "audio_timing.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>

static int game_frames(size_t reported_frames) {
    // Original 0x8007E2D0..0x8007E310, with runtime's 735-byte lead at 44.1 kHz.
    const uint32_t bytes = reported_frames * 4;
    const uint32_t reported = bytes > 735 ? bytes - 735 : 0;
    const int16_t length = ((736 - (reported >> 2) + 240) & 0xFFF0) + 16;
    return uint32_t(int32_t(length)) < 720 ? 720 : length;
}

int main() {
    // Raw total-device feedback reproduces the crash's signed length overflow.
    assert(game_frames(1440) < 0);
    // Even extreme device backlog cannot enter that invalid game length range.
    for (size_t pending = 0; pending < 2000000; pending += 37) {
        const int count = game_frames(srw64_audio_feedback_frames(pending, 44100));
        assert(count >= 720 && count <= 992);
    }

    // Feed the original game's next-buffer formula through uneven consumption,
    // including 512-frame device granularity and a delayed callback every 101
    // VIs. Track latency, not only the validity of generated buffer lengths.
    size_t device_pending = 0;
    size_t delayed_frames = 0;
    int next = 992;
    for (int vi = 0; vi < 36000; ++vi) {
        size_t consumed = ((vi + 1) * 735 / 512 - vi * 735 / 512) * 512;
        if (vi % 101 == 0) { delayed_frames = consumed; consumed = 0; }
        else { consumed += delayed_frames; delayed_frames = 0; }
        device_pending -= std::min(device_pending, consumed);
        device_pending += next;
        next = game_frames(srw64_audio_feedback_frames(device_pending, 44100));
        assert(next >= 720 && next <= 992);
        assert(device_pending < srw64_audio_queue_limit(44100));
    }

    // Bounded capture window. Without one every VI writes, so the caller's own
    // 30-second cap is what limits the file.
    using Action = Srw64AudioCaptureWindow::Action;
    const Srw64AudioCaptureWindow unbounded{};
    assert(!unbounded.bounded());
    for (uint64_t vi : {uint64_t(0), uint64_t(5000), uint64_t(1u << 30)})
        assert(unbounded.act(vi, false) == Action::Write && unbounded.act(vi, true) == Action::Write);

    // With a window: skip before it, write inside it, close once on the way out
    // and skip afterwards, so a probe keeps exactly the span it asked for.
    const Srw64AudioCaptureWindow window{4400, 6600};
    assert(window.bounded());
    assert(window.act(0, false) == Action::Skip);
    assert(window.act(4399, false) == Action::Skip);
    assert(window.act(4400, false) == Action::Write);
    assert(window.act(6599, true) == Action::Write);
    assert(window.act(6600, true) == Action::Close);   // closes the file once
    assert(window.act(6600, false) == Action::Skip);   // already closed: nothing more
    assert(window.act(9999, false) == Action::Skip);

    // A degenerate window (to <= from) is treated as no window rather than as a
    // window that never opens, so a mistyped bound cannot silently lose a capture.
    const Srw64AudioCaptureWindow degenerate{6600, 4400};
    assert(!degenerate.bounded());
    assert(degenerate.act(0, false) == Action::Write);
}
