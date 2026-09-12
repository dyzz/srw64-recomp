#include "audio.hpp"
#include "audio_timing.hpp"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <vector>

uint64_t srw64_current_vi();

namespace {
bool enabled;
std::filesystem::path output;
std::mutex audio_mutex;
SDL_AudioDeviceID device;
uint32_t rate;
uint64_t queued_samples{}, max_queued_frames{}, captured_samples{};
uint64_t queue_calls{}, latency_recoveries{}, max_feedback_frames{};
std::ofstream capture;
}

void srw64_configure_audio(bool value, const std::filesystem::path& directory) {
    enabled = value;
    output = directory;
}

bool srw64_audio_enabled() { return enabled; }

void srw64_audio_frequency(uint32_t frequency) {
    if (!enabled) return;
    std::lock_guard lock(audio_mutex);
    if (device && rate == frequency) return;
    if (device) SDL_CloseAudioDevice(device);
    SDL_AudioSpec wanted{}, obtained{};
    wanted.freq = frequency;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 512;
    device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
    if (!device || obtained.freq != (int)frequency || obtained.format != AUDIO_S16SYS || obtained.channels != 2) {
        fprintf(stderr, "SRW64_AUDIO_DEVICE_FAILED %s\n", SDL_GetError());
        std::abort();
    }
    rate = frequency;
    SDL_PauseAudioDevice(device, 0);
    fprintf(stderr, "SRW64_AUDIO_DEVICE rate=%u channels=2 format=S16SYS\n", rate);
}

void srw64_queue_audio(int16_t* samples, size_t count) {
    if (!enabled) return;
    std::lock_guard lock(audio_mutex);
    if (!device || count % 2 || count > 0x1FFFC) {
        fprintf(stderr, "SRW64_AUDIO_INVALID_DMA samples=%zu rate=%u\n", count, rate);
        std::abort();
    }
    const size_t old_frames = SDL_GetQueuedAudioSize(device) / 4;
    if (old_frames > srw64_audio_queue_limit(rate)) {
        // Recover after a suspended/stalled device instead of retaining seconds
        // of stale audio. Record every recovery; ordinary playback should use none.
        SDL_ClearQueuedAudio(device);
        ++latency_recoveries;
        fprintf(stderr, "SRW64_AUDIO_LATENCY_RECOVERY vi=%llu dropped_frames=%zu\n",
                (unsigned long long)srw64_current_vi(), old_frames);
    }
    std::vector<int16_t> swapped(count);
    // RDRAM is stored with each 32-bit word byte-swapped. Native int16 reads
    // consequently reverse the two channels, as documented by the runtime's
    // reference SDL frontend. Restore L/R before sending samples to SDL.
    for (size_t i = 0; i < count; i += 2) {
        swapped[i] = samples[i + 1];
        swapped[i + 1] = samples[i];
    }
    if (old_frames > srw64_audio_queue_limit(rate)) {
        const size_t fade_frames = std::min<size_t>(64, count / 2);
        for (size_t i = 0; i < fade_frames; ++i) {
            swapped[i * 2] = int32_t(swapped[i * 2]) * int32_t(i) / int32_t(fade_frames);
            swapped[i * 2 + 1] = int32_t(swapped[i * 2 + 1]) * int32_t(i) / int32_t(fade_frames);
        }
    }
    if (SDL_QueueAudio(device, swapped.data(), swapped.size() * sizeof(int16_t))) {
        fprintf(stderr, "SRW64_AUDIO_QUEUE_FAILED %s\n", SDL_GetError());
        std::abort();
    }
    queued_samples += count;
    const uint64_t device_frames = SDL_GetQueuedAudioSize(device) / 4;
    max_queued_frames = std::max(max_queued_frames, device_frames);
    if (++queue_calls == 1 || queue_calls % 60 == 0) {
        const auto temporary = output / "audio-live.tmp";
        {
            std::ofstream live(temporary);
            live << "{\"schema\":\"srw64.native-audio-live.v1\",\"vi\":" << srw64_current_vi()
                 << ",\"frequency\":" << rate << ",\"device_pending_frames\":" << device_frames
                 << ",\"device_pending_ms\":" << double(device_frames) * 1000 / rate
                 << ",\"max_queued_frames\":" << max_queued_frames
                 << ",\"latency_recoveries\":" << latency_recoveries << "}\n";
        }
        std::filesystem::rename(temporary, output / "audio-live.json");
    }
    // Keep a bounded copy of the actual post-channel-swap device input.
    if (!capture.is_open() && captured_samples == 0) {
        capture.open(output / "audio-output.s16", std::ios::binary);
        std::ofstream metadata(output / "audio-output.json");
        metadata << "{\"schema\":\"srw64.native-audio-capture.v1\",\"frequency\":" << rate
                 << ",\"channels\":2,\"format\":\"signed-16-little-endian\",\"duration_limit_seconds\":30}\n";
    }
    const size_t keep = std::min<uint64_t>(count, uint64_t(rate) * 2 * 30 - std::min(captured_samples, uint64_t(rate) * 2 * 30));
    capture.write(reinterpret_cast<const char*>(swapped.data()), keep * sizeof(int16_t));
    captured_samples += keep;
}

size_t srw64_audio_remaining() {
    if (!enabled) return 0;
    std::lock_guard lock(audio_mutex);
    if (!device) return 0;
    const size_t remaining = srw64_audio_feedback_frames(SDL_GetQueuedAudioSize(device) / 4, rate);
    max_feedback_frames = std::max(max_feedback_frames, uint64_t(remaining));
    return remaining;
}

void srw64_close_audio() {
    if (!enabled) return;
    std::lock_guard lock(audio_mutex);
    std::ofstream report(output / "audio-device.json");
    report << "{\"schema\":\"srw64.native-audio-device.v1\",\"frequency\":" << rate
           << ",\"queued_samples\":" << queued_samples << ",\"max_queued_frames\":" << max_queued_frames
           << ",\"remaining_frames\":" << (device ? SDL_GetQueuedAudioSize(device) / 4 : 0)
           << ",\"feedback\":\"total-device-queue-with-one-VI-lead-and-one-VI-cap\""
           << ",\"max_feedback_frames\":" << max_feedback_frames
           << ",\"latency_recoveries\":" << latency_recoveries
           << ",\"captured_samples\":" << captured_samples << "}\n";
    capture.close();
    if (device) SDL_CloseAudioDevice(device);
    device = 0;
}
