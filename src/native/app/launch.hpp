#pragma once
#include "runtime.hpp"
namespace srw64::app {
struct RuleDescriptor { std::string id; bool enabled_by_default; };
struct GameIdentity {
    std::string baseline, rom_sha256, save_file;
    unsigned rules_version{1};
    std::vector<RuleDescriptor> rules;
};
using HostMain = std::function<int(int, char**)>;
// Injection seam for ROM-free tests. The game CLI never accepts import metadata.
using ContentImporter = std::function<fs::path(const fs::path&, const fs::path&, const std::string&)>;
int run_standalone(const Options&, const GameIdentity&, const HostMain&, const ContentImporter& = {});
}
