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

// Bounded diagnostic capture of the device input. Without a VI window the copy
// keeps the first 30 seconds, which is the wrong span when the sound being
// investigated happens minutes into a scripted run; SRW64_AUDIO_CAPTURE_FROM/_TO
// narrow it to the commands of interest. Played audio is never affected.
struct Srw64AudioCaptureWindow {
    uint64_t from{}, to{};
    bool bounded() const { return to > from; }
    // What this VI should do with the capture file: skip it, write it, or close it.
    enum class Action { Skip, Write, Close };
    Action act(uint64_t vi, bool open) const {
        if (!bounded()) return Action::Write;
        if (vi >= to) return open ? Action::Close : Action::Skip;
        return vi >= from ? Action::Write : Action::Skip;
    }
};

