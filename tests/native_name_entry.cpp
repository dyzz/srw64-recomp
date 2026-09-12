// Real bridge, fake guest validator: verify transactional memory/input ownership.
#include "native_name_entry.hpp"
#include "game_adapter/name_codec.hpp"
#include "game_hooks.hpp"
#include "native_dialogue.hpp"
#include "json/json.hpp"
#include <cassert>
#include <codecvt>
#include <fstream>
#include <locale>
#include <unistd.h>

SRW64GameHooks srw64_game_hooks;
uint64_t srw64_current_vi() {return 1200;}
namespace srw64::dialogue {
std::u16string utf16(const std::string& s) {return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>{}.from_bytes(s);}
std::string utf8(const std::u16string& s) {return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>{}.to_bytes(s);}
}
int fade=-1;bool reject=false;unsigned validations{},transitions{};
extern "C" void resident_func_80099B30(uint8_t*,recomp_context* ctx) {ctx->r2=fade;}
extern "C" void resident_func_80099814(uint8_t*,recomp_context* ctx) {
    assert(ctx->r4==5 && ctx->r5==1 && ctx->r6==2);++transitions;ctx->r16=0xBAD;
}
extern "C" void load_001090A0_func_801C474C(uint8_t* rdram,recomp_context* ctx) {
    ++validations;MEM_H(0,int32_t(0x8010F5F8))=0xABCD;ctx->r2=reject;ctx->r16=0xBAD;
}
int main() {
    using namespace srw64::names;
    Codec c;c.add(0,u" ");c.add(1,u"ア");c.add(2,u"Ａ");c.add(0x500,u"光");
    c.add(0x124,u"危");c.add(0x900,u"険");c.add(3,u"two");
    std::vector<uint16_t> codes={99};
    assert(c.encode(u"A光",0,codes).empty() && codes==std::vector<uint16_t>({2,0x500}));
    assert(c.encode(u"",0,codes)=="name_empty");
    assert(c.encode(u"アアアアアアアア",0,codes)=="name_long");
    assert(c.encode(u"アアアアアア",2,codes)=="name_long");
    assert(c.encode(u" 光",0,codes)=="name_spaces");
    for(auto s:{u"危",u"険",u"🙂",u"\n",u"two"})assert(c.encode(s,0,codes)=="name_unsupported");
    assert(codes==std::vector<uint16_t>({2,0x500}));
    const auto out=std::filesystem::temp_directory_path()/("srw64-name-test-"+std::to_string(getpid()));
    assert(std::filesystem::create_directory(out));
    const auto data=out/"dialogue.json";
    std::ofstream(data)<<nlohmann::json({{"schema","srw64.native-dialogue-data.v2"},
        {"glyphs",{{"0"," "},{"1","ア"},{"2","Ａ"},{"1280","光"}}}}).dump();
    setenv("SRW64_DIALOGUE_DATA",data.c_str(),1);configure(out);
    std::vector<uint8_t> rom(0x2000000),memory(0x800000,0x5A);auto* rdram=memory.data();
    initialize_rom(rom.data(),rom.size());overlay_loaded(0x1090A0,0x801C2600,0x49B0);
    for(unsigned i=0;i<21;++i)MEM_H(0,int32_t(0x801C71E0+2*i))=0;
    for(auto offset:Codec::offsets)MEM_H(0,int32_t(0x801C71E0+2*offset))=1;
    recomp_context ctx{};ctx.r16=0x1234;
    srw64_game_hooks.name_begin(rdram,0);
    fade=1;assert(srw64_game_hooks.name_step(rdram,&ctx,0));assert(!request().active && request().visible && owns_input());
    fade=-1;srw64_game_hooks.name_step(rdram,&ctx,0);auto r=request();
    assert(r.active && r.values[0]==u"ア" && owns_input());
    assert(input(0xFFFF)==0); // Entire game controller belongs to the editor.
    auto before=memory;auto values=r.values;values[0]=u"光Ａ";
    submit(r.serial+1,values);srw64_game_hooks.name_step(rdram,&ctx,0);assert(!validations);
    reject=true;submit(r.serial,values);srw64_game_hooks.name_step(rdram,&ctx,0);
    assert(memory==before && request().error=="name_invalid" && request().active);
    reject=false;submit(r.serial,values);srw64_game_hooks.name_step(rdram,&ctx,0);
    assert(validations==2 && transitions==1 && !request().active && ctx.r16==0x1234);
    assert(MEM_W(0,int32_t(0x801C6FB0))==2);
    assert(MEM_HU(0,int32_t(0x801C70B8))==0xE500);
    assert(request().visible && owns_input()); // Retain page through next guest init.
    assert(input(0xFFFF)==0);assert(input(0)==0);assert(input(0x8000)==0);
    uint32_t offset,size;
    assert(descriptor(0,0xE500,offset,size) && size==12);
    assert(read(Codec::table_base+offset,rdram,0x80110000,0x400));
    assert(MEM_HU(8,int32_t(0x80110000))==0x500 && MEM_HU(10,int32_t(0x80110000))==0xFFFF);
    assert(MEM_W(12,int32_t(0x80110000))==0);
    bool threw=false;try{read(Codec::table_base+offset,rdram,0x807FFFF8,12);}catch(const std::runtime_error&){threw=true;}assert(threw);
    assert(!descriptor(1,0xE500,offset,size) && !descriptor(0,42,offset,size));
    srw64_game_hooks.name_begin(rdram,1);srw64_game_hooks.name_step(rdram,&ctx,1);
    r=request();assert(r.active);before=memory;submit(r.serial,r.values,true);
    srw64_game_hooks.name_step(rdram,&ctx,1);
    assert(validations==2 && transitions==2 && MEM_W(0,int32_t(0x801C6FB0))==0);
    assert(std::memcmp(memory.data()+0x10F5F8,before.data()+0x10F5F8,0xA0)==0);
    srw64_game_hooks.name_begin(rdram,Selection);assert(!request().visible && !owns_input());
    assert(input(0x8000)==0);assert(input(0)==0);assert(input(0x8000)==0x8000);
    for(auto address:{0x10F5F8,0x10F618,0x10F638,0x10F608,0x10F628,0x10F644}) {
        MEM_H(0,int32_t(0x80000000+address))=1;MEM_H(2,int32_t(0x80000000+address))=0xFFFF;
    }
    srw64_game_hooks.name_begin(rdram,Review);srw64_game_hooks.name_step(rdram,&ctx,Review);
    r=request();assert(r.person==Review && r.active && r.names[1][2]==u"ア");before=memory;
    submit(r.serial,values);review(r.serial+1,true);srw64_game_hooks.name_step(rdram,&ctx,Review);assert(memory==before);
    review(r.serial,false);srw64_game_hooks.name_step(rdram,&ctx,Review);
    assert(MEM_W(0,int32_t(0x801C6FB0))==1 && transitions==3 && validations==2 && request().visible);
    srw64_game_hooks.name_begin(rdram,Review);srw64_game_hooks.name_step(rdram,&ctx,Review);
    review(request().serial,true);srw64_game_hooks.name_step(rdram,&ctx,Review);
    assert(MEM_BU(0,int32_t(0x801C70F4))==2 && transitions==4 && ctx.r16==0x1234);
    queue_cover(42,true);queue_cover(43,false);assert(frame_cover(42) && !frame_cover(43));
    cover_presented(42,true);assert(cover_in_flight());cover_presented(43,false);cover_presented(42,true);assert(!cover_in_flight());
    overlay_loaded(0x123456,0x80200000,0x1000);assert(request().visible);
    overlay_loaded(0x10DA50,0x801C4500,0x7C50);assert(!srw64_game_hooks.name_step(rdram,&ctx,0));
    assert(!request().active && !request().visible && !owns_input());std::filesystem::remove_all(out);
}
