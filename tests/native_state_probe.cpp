#include "state_probe.hpp"
#include <cassert>
#include <unistd.h>

uint64_t srw64_current_vi(){return 123;}
int main() {
    using namespace srw64::state_probe;
    directory=std::filesystem::temp_directory_path()/("srw64-state-probe-"+std::to_string(getpid()));
    assert(std::filesystem::create_directory(directory));setenv("SRW64_STATE_PROBE","1",1);
    std::vector<uint8_t> ram(0x800000);
    auto set=[&](uint32_t address,uint8_t value){ram[(address&0x1FFFFFFF)^3]=value;};
    capture(ram.data(),"dialogue-fragment",100);
    std::ifstream first(directory/"state-1-dialogue-fragment.json");
    const auto observation=nlohmann::json::parse(first);
    assert(observation.at("coverage_complete")==false);
    nlohmann::json fixture={{"schema","srw64.rng-comparison-fixture.v1"},
        {"boundary","dialogue-fragment"},{"argument",100},{"regions",nlohmann::json::object()}};
    for(const char* name:{"game_rng","battle_seed_clock","animation_seed_clock"})
        fixture["regions"][name]=observation.at("regions").at(name);
    const auto path=directory/"fixture.json";setenv("SRW64_STATE_FIXTURE",path.c_str(),1);
    auto invalid=fixture;invalid["regions"]["animation_seed_clock"]["address"]=0x80000000u;
    publish(path,invalid.dump());set(0x800D49D3,7);
    bool rejected=false;
    try {capture(ram.data(),"dialogue-fragment",100);}catch(const std::runtime_error&){rejected=true;}
    assert(rejected && srw64::script_trace::read(ram.data(),0x800D49D0,4)==7);
    publish(path,fixture.dump());capture(ram.data(),"dialogue-fragment",100);
    assert(srw64::script_trace::read(ram.data(),0x800D49D0,4)==0);
    assert(std::filesystem::exists(directory/"applied-comparison-fixture.json"));
    set(0x800D49D3,8);capture(ram.data(),"dialogue-fragment",100);
    assert(srw64::script_trace::read(ram.data(),0x800D49D0,4)==8); // Applied once, never hides later divergence.
    save_payload(ram.data(),true,0);
    std::ifstream saved(directory/"state-4-tactical-save.json");
    const auto state=nlohmann::json::parse(saved);
    assert(state.at("attachment").at("payload")=="tactical-4.payload");
    assert(std::filesystem::file_size(directory/"tactical-4.payload")==0x3AE0);
    for(const auto& entry:std::filesystem::directory_iterator(directory))assert(entry.path().extension()!=".tmp");
    std::filesystem::remove_all(directory);
}
