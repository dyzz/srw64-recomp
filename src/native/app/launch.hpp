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
// After run_standalone returned switch_campaign_exit: the campaign the title asked for
// (an empty path: the main game), taken out of the user directory. Nothing when there is
// no usable request.
std::optional<fs::path> take_campaign_switch(const fs::path& user_dir);
// The arguments that start the program again on the asked campaign, keeping the ROM,
// user directory, content, resolution and sound of this start.
std::vector<std::string> switch_arguments(const Options& options, const fs::path& campaign);
}
