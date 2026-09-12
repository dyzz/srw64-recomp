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
}
