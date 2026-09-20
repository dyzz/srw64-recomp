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
// Validates local, relocatable content and invokes the compiled host in-process.
// No build tools, shell commands, interpreter, repository paths or downloads.
int run_standalone(const Options&, const GameIdentity&, const HostMain&);
}
