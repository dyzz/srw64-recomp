#pragma once
#include "recomp.h"
#include "script_trace.hpp"
#include "json/json.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

// Opt-in observations taken synchronously on the game thread. These snapshots
// are diagnostic evidence, not a writable save-state or a safety assertion.
namespace srw64::state_probe {
inline bool enabled(){static const bool value=[] {const char* p=std::getenv("SRW64_STATE_PROBE");return p && std::string_view(p)=="1";}();return value;}
inline std::filesystem::path directory;
inline std::mutex mutex;
inline uint64_t sequence{};
inline nlohmann::json regions(const uint8_t* ram) {
    struct Region {const char* name;uint32_t address,size;};
    constexpr Region list[]={
        {"game_rng",0x800D49D0,0x834},
        {"battle_seed_clock",0x8015DC50,4},
        {"animation_seed_clock",0x80172D0C,4},
        {"persistent_records",0x8010F4D0,0xA0},
        {"campaign_flags",0x8015E818,0x34},
        {"scenario_variables",0x8015E990,0x24},
        {"player_progress_and_names",0x8010F5E8,0xC8},
        {"map_roster",0x8015E100,0x708},
        {"unit_instances",0x8016A210,0x89D0},
        {"pilot_instances",0x80172F40,0x5910},
        {"part_instances",0x80178F80,0x12750}};
    nlohmann::json result=nlohmann::json::object();
    constexpr char digits[]="0123456789abcdef";
    for(const auto& r:list) {
        std::string bytes;bytes.reserve(r.size*2);
        for(uint32_t i=0;i<r.size;++i){const auto b=script_trace::read(ram,r.address+i,1);bytes+=digits[b>>4];bytes+=digits[b&15];}
        result[r.name]={{"address",r.address},{"size",r.size},{"bytes",bytes}};
    }
    return result;
}
inline void publish(const std::filesystem::path& path,const std::string& bytes) {
    const auto temporary=path.string()+".tmp";
    {std::ofstream out(temporary,std::ios::binary);out.write(bytes.data(),bytes.size());
     out.flush();if(!out)throw std::runtime_error("Cannot write state observation");}
    std::filesystem::rename(temporary,path);
}
// Deliberately narrow QA setup: align the RNG and its two reseed counters once
// at a specified dialogue boundary. Never restore arbitrary guest addresses.
// This is not a production rule, save loader, or a fix for later divergence.
inline void apply_fixture(uint8_t* ram,const char* boundary,uint32_t argument) {
    static bool applied=false;
    const char* source=std::getenv("SRW64_STATE_FIXTURE");
    if(applied || !enabled() || !source || std::string(boundary)!="dialogue-fragment")return;
    std::ifstream input(source);const auto data=nlohmann::json::parse(input);
    if(data.at("schema")!="srw64.rng-comparison-fixture.v1" || data.at("boundary")!="dialogue-fragment")
        throw std::runtime_error("Unsupported comparison fixture");
    if(data.at("argument")!=argument)return;
    const auto expected=regions(ram);
    std::vector<std::pair<uint32_t,std::vector<uint8_t>>> writes;
    if(data.at("regions").size()!=3)throw std::runtime_error("Unexpected fixture regions");
    for(const char* name:{"game_rng","battle_seed_clock","animation_seed_clock"}) {
        const auto& row=data.at("regions").at(name);const auto& layout=expected.at(name);
        const auto hex=row.at("bytes").get<std::string>();
        const auto size=layout.at("size").get<unsigned>();
        if(row.at("address")!=layout.at("address") || row.at("size")!=size || hex.size()!=size*2 ||
           hex.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("Invalid comparison fixture layout");
        std::vector<uint8_t> bytes;bytes.reserve(size);
        for(unsigned i=0;i<size;++i)bytes.push_back(std::stoul(hex.substr(i*2,2),nullptr,16));
        writes.emplace_back(row.at("address").get<uint32_t>(),std::move(bytes));
    }
    for(const auto& [address,bytes]:writes)for(unsigned i=0;i<bytes.size();++i)
        ram[((address&0x1FFFFFFF)+i)^3]=bytes[i];
    applied=true;publish(directory/"applied-comparison-fixture.json",data.dump(2)+"\n");
}
inline void capture(uint8_t* ram,const char* boundary,uint32_t argument=0,
                    const nlohmann::json& attachment=nlohmann::json::object()) {
    if(!enabled() || directory.empty())return;
    std::lock_guard lock(mutex);
    apply_fixture(ram,boundary,argument);
    nlohmann::json data={{"schema","srw64.game-state-observation.v1"},{"boundary",boundary},
        {"sequence",++sequence},{"vi",srw64_current_vi()},{"argument",argument},
        {"regions",regions(ram)},{"coverage_complete",false},{"attachment",attachment}};
    publish(directory/("state-"+std::to_string(sequence)+"-"+boundary+".json"),data.dump()+"\n");
}
inline void rng(const char* event,uint32_t value,const uint8_t* ram) {
    if(!enabled() || directory.empty())return;
    const nlohmann::json data={{"schema","srw64.rng-event.v1"},{"event",event},{"value",value},
        {"index",script_trace::read(ram,0x800D49D0,4)},{"vi",srw64_current_vi()}};
    std::ofstream(directory/"rng-events.jsonl",std::ios::app)<<data.dump()<<'\n';
}
inline void save_payload(uint8_t* ram,bool tactical,unsigned slot) {
    if(!enabled() || directory.empty())return;
    const uint32_t start=tactical?0x800FBEF0:0x801C2600;
    const uint32_t size=tactical?0x3AE0:0x1F00;
    std::string bytes(size,'\0');
    for(unsigned i=0;i<size;++i)bytes[i]=script_trace::read(ram,start+i,1);
    const auto name=std::string(tactical?"tactical":"intermission")+"-"+std::to_string(sequence+1)+".payload";
    publish(directory/name,bytes);
    capture(ram,tactical?"tactical-save":"intermission-save",slot,{{"payload",name},{"size",size}});
}
}
