#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>

void srw64_configure_audio(bool enabled, const std::filesystem::path& directory);
bool srw64_audio_enabled();
void srw64_audio_frequency(uint32_t frequency);
void srw64_queue_audio(int16_t* samples, size_t count);
size_t srw64_audio_remaining();
void srw64_close_audio();
// Running audio figures for the debug status: frames already queued when the game queues
// more and when it asks how much is left, what it is told, and the gaps between its queues.
struct Srw64AudioStats {
    uint64_t queues = 0, queued_before_frames = 0, asks = 0, asked_queue_frames = 0, told_frames = 0, told_capped = 0;
    uint64_t gaps[5] = {};   // under 12 ms, 12-15.5, 15.5-18, 18-21.5, 21.5 and over
    uint32_t rate = 0;
};
Srw64AudioStats srw64_audio_stats();
