#pragma once
#include "runtime.hpp"
namespace srw64::app {
// Empty only in a deliberately metadata-free test build.
std::string_view embedded_import_spec();
// Caller must hold the user-directory Session lock. No interpreter/downloads.
// spec is build-time metadata, never a user-supplied override in the game CLI.
fs::path prepare_rom_content(const fs::path& rom,const fs::path& cache_root,
                             std::string_view spec,const std::string& expected_rom_hash);
}
