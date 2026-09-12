#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

// Native SRW64 pacing: include the whole device queue in synthesis feedback.
// Reserve one VI of output lead, then expose at most one VI to the game. The
// runtime adds its own smaller lead. This bounds the original signed-halfword
// length formula without discarding feedback about queued future DMA buffers.
inline size_t srw64_audio_feedback_frames(size_t queued_frames, uint32_t rate) {
    const size_t frames_per_vi = rate / 60;
    return std::min(queued_frames > frames_per_vi ? queued_frames - frames_per_vi : 0,
                    frames_per_vi);
}

inline size_t srw64_audio_queue_limit(uint32_t rate) { return rate / 10; }
