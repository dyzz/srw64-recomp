#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>
namespace srw64::battle_page {
void configure(const std::filesystem::path&);
nlohmann::json state();
void answer(uint64_t serial,const std::string& action);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t);
}
