#include "mini_stage.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <vector>
static uint64_t current_vi=100;
uint64_t srw64_current_vi(){return current_vi;}
// Intro adapter stubs: the test drives the title state and records skip requests.
static int major_state=-1;static unsigned skip_requests=0;
namespace srw64::intro {
int title_major(){return major_state;}
void request_skip(){++skip_requests;}
}
int main(int argc,char** argv) {
    assert(argc==2);
    const std::filesystem::path dir=argv[1];
    std::filesystem::create_directories(dir);
    using namespace srw64::mini_stage;
    // Image validation.
    nlohmann::json image={{"schema","srw64.mini-stage-image.v1"},{"name","t"},{"map",20},{"slot",nullptr},
        {"events_hex","000C00000000000000003D3B00013D48FFFF000000070001000000040000" "3D4AFFFF0000"},
        {"aux_hex","0000000300040" "12C" "0002010A00000000000000000001000000000000" "03E70000"},
        {"events",nlohmann::json::array({{{"offset",0},{"size",18}},{{"offset",20},{"size",14}}})}};
    auto parsed=parse(image);
    assert(parsed.pointers.size()==2 && parsed.pointers[1]==event_block+20 && parsed.map && *parsed.map==20 && !parsed.slot);
    for(auto [key,value]:std::vector<std::pair<std::string,nlohmann::json>>{{"schema","x"},{"events_hex","ZZ"},{"aux_hex","00"},
            {"events",nlohmann::json::array({{{"offset",2},{"size",4}}})},{"events",nlohmann::json::array()}}) {
        auto bad=image;bad[key]=value;bool threw=false;
        try{parse(bad);}catch(const std::runtime_error&){threw=true;}
        assert(threw);
    }
    // Registration: buffers, pointer table and the deployment base argument.
    const auto path=dir/"image.json";std::ofstream(path)<<image.dump();
    setenv("SRW64_MINI_STAGE",path.c_str(),1);
    configure(dir);
    assert(state().image);
    std::vector<uint8_t> ram(0x800000);
    auto put=[&](uint32_t p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)ram[((p&0x1FFFFFFF)+i)^3]=v>>((n-i-1)*8);};
    recomp_context ctx{};
    const uint32_t table=0x8015F708;
    put(0x8010F5F0,1,1);ctx.r4=0;ctx.r6=int32_t(table);ctx.r7=int32_t(0x80199400);
    register_hook(ram.data(),&ctx);
    assert(state().active && state().bound && *state().bound==1 && state().applied==1);
    assert(read(ram.data(),event_block,2)==0x000C && read(ram.data(),event_block+10,2)==0x3D3B && read(ram.data(),event_block+20,2)==0x0007);
    assert(read(ram.data(),table,4)==event_block && read(ram.data(),table+4,4)==event_block+20 && read(ram.data(),table+8,4)==0xFFFFFFFF);
    assert(read(ram.data(),aux_block,2)==0 && read(ram.data(),aux_block+6,2)==300 && read(ram.data(),aux_block+28,2)==999);
    assert(uint32_t(ctx.r7)==aux_block);
    // Map index follows the image only while the bound scene is active.
    put(0x8010F5EE,5,1);map_hook(ram.data());assert(read(ram.data(),0x8010F5EE,1)==20);
    put(0x8010F5F0,4,1);register_hook(ram.data(),&ctx);assert(!state().active && state().applied==1);
    put(0x8010F5EE,7,1);map_hook(ram.data());assert(read(ram.data(),0x8010F5EE,1)==7);
    // Main-menu entry: the hotkey only arms on the main menu (intro state 3).
    state().applied=0;state().bound.reset();
    major_state=2;hotkey();current_vi=200;assert(input(0)==0 && !state().armed);
    major_state=3;hotkey();current_vi=210;assert(input(0)==0x1000 && state().armed);
    for(int i=0;i<3;++i)assert(input(0)==0x1000); // held across four polls
    assert(input(0)==0);
    major_state=13;current_vi=300;input(0);input(0);assert(skip_requests==2 && state().skips==1);
    major_state=5;input(0);major_state=13;input(0);assert(state().skips==2);
    put(0x8010F5F0,1,1);register_hook(ram.data(),&ctx);major_state=12;input(0);
    assert(!state().armed && state().applied==1);
    // Per-command captures: off unless SRW64_MINI_STAGE_CAPTURE=1, one per new
    // boundary inside the image's event block, and never for a PC outside it.
    const uint32_t owner=0x8015F800;
    auto set_pc=[&](uint32_t pc){put(owner+0x1C,pc,4);};
    assert(!state().capture_commands);
    set_pc(event_block+10);poll_hook(ram.data(),owner);assert(state().captures==0);
    setenv("SRW64_MINI_STAGE_CAPTURE","1",1);
    configure(dir);
    assert(state().capture_commands);
    put(0x8010F5F0,1,1);state().bound.reset();register_hook(ram.data(),&ctx);
    assert(state().active);
    srw64::state_probe::directory=dir;
    setenv("SRW64_STATE_PROBE","1",1);
    if(srw64::state_probe::enabled()) {
        set_pc(event_block+10);poll_hook(ram.data(),owner);
        set_pc(event_block+10);poll_hook(ram.data(),owner); // same boundary: no second capture
        assert(state().captures==1);
        set_pc(event_block+20);poll_hook(ram.data(),owner);assert(state().captures==2);
        set_pc(event_block+0x4000);poll_hook(ram.data(),owner);assert(state().captures==2); // outside the block
        set_pc(0x80700000);poll_hook(ram.data(),owner);assert(state().captures==2);
    }
    // Early exit: the run ends a grace period after the watched opcode is reached,
    // and only for that opcode — a probe must not stop on some other command.
    setenv("SRW64_MINI_STAGE_EXIT_AFTER","3D48",1);
    setenv("SRW64_MINI_STAGE_EXIT_GRACE","50",1);
    configure(dir);
    put(0x8010F5F0,1,1);state().bound.reset();register_hook(ram.data(),&ctx);
    current_vi=1000;
    set_pc(event_block+10);poll_hook(ram.data(),owner);   // 3D3B, not the watched one
    assert(!state().exit_seen && !finished());
    set_pc(event_block+14);poll_hook(ram.data(),owner);   // 3D48, the watched one
    assert(state().exit_seen);
    assert(!finished());                                   // still inside the grace period
    current_vi=1049;assert(!finished());
    current_vi=1050;assert(finished());                    // grace elapsed
    assert(!finished());                                   // fires once, not every VI
    unsetenv("SRW64_MINI_STAGE_EXIT_AFTER");unsetenv("SRW64_MINI_STAGE_EXIT_GRACE");

    std::ifstream log(dir/"mini-stage-events.jsonl");std::string line;unsigned armed=0,entered=0,skipped=0;
    while(std::getline(log,line)){armed+=line.find("\"armed\"")!=std::string::npos;entered+=line.find("\"entered\"")!=std::string::npos;skipped+=line.find("\"skipped\"")!=std::string::npos;}
    assert(armed==1 && entered==1 && skipped==1);
    return 0;
}
