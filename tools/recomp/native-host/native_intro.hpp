#pragma once
#include <cstdint>
#include <filesystem>
namespace srw64::intro {
void configure(const std::filesystem::path&);
uint16_t input(uint16_t);
void overlay_loaded(uint32_t rom, uint32_t ram, uint32_t size);
}
