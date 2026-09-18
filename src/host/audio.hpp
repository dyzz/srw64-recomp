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
