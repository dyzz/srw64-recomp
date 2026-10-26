#pragma once
#include "runtime.hpp"
#include "json/json.hpp"
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
// The PS Vita (docs/design/psv-port.md): its data is prepared on a computer
// (tools/release/vita/prepare_data.py) and copied into the player's folder, game/ holding
// the ROM, the art pack and the dialogue data as the host reads them. Nothing is checked,
// imported or rewritten at start; the run's files go in run/, the same folder each time.
inline constexpr unsigned prepared_format=2;  // 2: the string tables in dialogue.bin
inline constexpr const char* prepared_schema="srw64.prepared-data.v1";
int run_prepared(const Options&, const GameIdentity&, const HostMain&);
// The HD pack's page pictures (art/srw64-page-portraits.json, srw64-units-hd.json) attached
// to the dialogue data's portraits and battle art as their "hd" paths.
void attach_hd_art(nlohmann::json& data, const fs::path& folder);
}
