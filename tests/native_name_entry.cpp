// Real bridge, fake guest functions: selection commits the default names through the
// original validator, the review starts the story or returns, input stays owned.
#include "native_name_entry.hpp"
#include "game_adapter/name_codec.hpp"
#include "game_hooks.hpp"
#include "native_dialogue.hpp"
#include "presentation_settings.hpp"
#include "localization/catalog.hpp"
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
std::string ui_text(const uint8_t*,uint16_t id) {return "text"+std::to_string(id);}
}
namespace srw64::localization {const Catalog& catalog() {static Catalog c;return c;}}
namespace srw64::settings {bool native=true;bool native_name_entry_ui() {return native;}}
int fade=-1;bool reject=false,original_yes=false;unsigned transitions{},defaults_loaded{};
std::vector<uint32_t> sounds;
struct Validation {uint32_t person;std::vector<uint16_t> editor;};
std::vector<Validation> validations;
extern "C" void resident_func_8007E8A8(uint8_t*,recomp_context* ctx) {sounds.push_back(uint32_t(ctx->r4));ctx->r16=0xBAD;}
extern "C" void load_001090A0_func_801C3744(uint8_t*,recomp_context* ctx) {++defaults_loaded;ctx->r16=0xBAD;}
extern "C" void resident_func_80099B30(uint8_t*,recomp_context* ctx) {ctx->r2=fade;}
extern "C" void resident_func_80099814(uint8_t*,recomp_context* ctx) {
    assert(ctx->r4==5 && ctx->r5==1 && ctx->r6==2);++transitions;ctx->r16=0xBAD;
}
extern "C" void load_001090A0_func_801C474C(uint8_t* rdram,recomp_context* ctx) {
    Validation v{uint32_t(ctx->r4),{}};
    for(unsigned i=0;i<21;++i)v.editor.push_back(MEM_HU(0,int32_t(0x801C71E0+2*i)));
    validations.push_back(v);ctx->r2=reject;ctx->r16=0xBAD;
}
// The original selection page: its はい moves to the protagonist's name page.
extern "C" void srw64_original_name_selection_step(uint8_t* rdram,recomp_context*) {
    if(original_yes)MEM_W(0,int32_t(0x801C6FB0))=1;
}
int main() {
    using namespace srw64::names;
    Codec c;c.add(1,u"ア");c.add(0x124,u"危");c.add(0x900,u"険");c.add(3,u"two");
    assert(c.decode({1,1})==u"アア" && c.decode({1,0x124}).empty() && c.decode({0x900}).empty() && c.decode({3}).empty());
    const auto out=std::filesystem::temp_directory_path()/("srw64-name-test-"+std::to_string(getpid()));
    assert(std::filesystem::create_directory(out));
    const auto data=out/"dialogue.json";
    std::ofstream(data)<<nlohmann::json({{"schema","srw64.native-dialogue-data.v2"},
        {"glyphs",{{"1","ア"},{"2","Ａ"},{"3","イ"},{"1280","光"}}}}).dump();
    setenv("SRW64_DIALOGUE_DATA",data.c_str(),1);configure(out);
    std::vector<uint8_t> rom(0x2000000),memory(0x800000,0x5A);auto* rdram=memory.data();
    initialize_rom(rom.data(),rom.size());overlay_loaded(0x1090A0,0x801C2600,0x49B0);
    recomp_context ctx{};ctx.r16=0x1234;
    auto state=[&]{return MEM_W(0,int32_t(0x801C6FB0));};
    // Default names are name-grid codes: text id 0x147F + code holds one font glyph.
    // Glyphs the codec does not know keep the original page.
    auto rom_word=[&](uint32_t at,uint32_t v){for(unsigned i=0;i<4;++i)rom[at+i]=uint8_t(v>>(24-8*i));};
    constexpr uint32_t text_table=0x01A34980;
    auto grid=[&](uint16_t code,uint16_t glyph){
        const uint32_t entry=0x100+code*12;rom_word(text_table+4+(0x147F+code)*8,entry);
        rom[text_table+entry+8]=uint8_t(glyph>>8);rom[text_table+entry+9]=uint8_t(glyph);
    };
    grid(5,1);grid(6,2);grid(7,0x700);grid(8,3);
    for(uint32_t a=0x801C6C00;a<0x801C6CE0;a+=2)MEM_H(0,int32_t(a))=7;
    srw64_game_hooks.name_begin(rdram,Selection);assert(!request().visible && !owns_input());
    assert(input(0x8000)==0x8000);
    // Four routes: given name アイアイアイア (7 codes) for route 2's protagonist, ア with
    // blank padding otherwise; family name ア, route 3's partner アＡ.
    for(unsigned route=0;route<4;++route)for(uint32_t table:{0x801C6C00u,0x801C6C70u})for(uint32_t partner:{0u,0x38u}) {
        const uint32_t at=table+partner+route*14;
        const bool long_name=route==2 && !partner && table==0x801C6C00u;
        for(unsigned i=0;i<7;++i)MEM_H(0,int32_t(at+2*i))=long_name?(i%2?8:5):i==0?5:i==1&&route==3&&partner?6:0xCA;
    }
    MEM_H(0,int32_t(0x801C70FA))=2;MEM_H(0,int32_t(0x801C70B0))=2;MEM_W(0,int32_t(0x801C6FB0))=0;
    srw64_game_hooks.name_begin(rdram,Selection);auto r=request();
    assert(r.visible && r.person==Selection && r.route==2 && owns_input());
    assert(r.choices[0].names[0][0]==u"ア" && r.choices[2].names[0][0]==u"アイアイアイア" && r.choices[3].names[1][1]==u"アＡ");
    // Starting units: アースゲイン (34) for route 0's protagonist, シグルーン (328) for route 3's partner.
    assert(r.choices[0].unit_names[0]=="text"+std::to_string(0x20F+34) && r.choices[3].unit_names[1]=="text"+std::to_string(0x20F+328));
    assert(input(0xFFFF)==0);   // the whole controller belongs to the page
    fade=1;assert(srw64_game_hooks.name_step(rdram,&ctx,Selection) && !request().active);
    fade=-1;srw64_game_hooks.name_step(rdram,&ctx,Selection);assert(request().active);
    select(r.serial+1,1);select(r.serial,3);select(r.serial,3);select(r.serial,0);select(r.serial,9);
    srw64_game_hooks.name_step(rdram,&ctx,Selection);
    assert(sounds==std::vector<uint32_t>({0xB9}) && request().route==0);   // one cursor sound per step
    auto before=memory;choose(r.serial+1,1);srw64_game_hooks.name_step(rdram,&ctx,Selection);assert(memory==before);
    // Route 2 again: no template reset; both names committed, the nickname cut to 3.
    choose(r.serial,2);srw64_game_hooks.name_step(rdram,&ctx,Selection);
    assert(sounds.back()==0xB7 && defaults_loaded==0 && state()==3 && transitions==1 && ctx.r16==0x1234);
    assert(validations.size()==2 && validations[0].person==0 && validations[1].person==1);
    const std::vector<uint16_t> hero={1,3,1,3,1,3,1,0, 1,0,0,0,0,0,0,0, 1,3,1,0,0};
    const std::vector<uint16_t> partner={1,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0, 1,0,0,0,0};
    assert(validations[0].editor==hero && validations[1].editor==partner);
    assert(request().visible && !request().active && owns_input());   // held until the review opens
    // Another route resets the templates and loads its defaults, as the original はい.
    srw64_game_hooks.name_begin(rdram,Selection);srw64_game_hooks.name_step(rdram,&ctx,Selection);
    choose(request().serial,3);srw64_game_hooks.name_step(rdram,&ctx,Selection);
    assert(defaults_loaded==1 && MEM_HU(0,int32_t(0x801C6FB8))==0x1549 && MEM_HU(58,int32_t(0x801C70B8))==0x1549);
    assert(MEM_HU(0,int32_t(0x801C70FA))==3 && MEM_HU(0,int32_t(0x801C70B0))==3 && state()==3 && transitions==2);
    assert(validations.size()==4 && validations[3].editor[8]==1 && validations[3].editor[9]==2);
    // A refused default leaves for the original name pages.
    reject=true;srw64_game_hooks.name_begin(rdram,Selection);srw64_game_hooks.name_step(rdram,&ctx,Selection);
    choose(request().serial,0);srw64_game_hooks.name_step(rdram,&ctx,Selection);
    assert(validations.size()==5 && state()==1 && transitions==3);reject=false;
    // The name editors never open a native page; the original runs if one is reached.
    srw64_game_hooks.name_begin(rdram,Player);assert(!request().visible);
    assert(!srw64_game_hooks.name_step(rdram,&ctx,Player));
    // Review: the committed names; back returns to the selection, confirm starts.
    for(auto address:{0x10F5F8,0x10F618,0x10F638,0x10F608,0x10F628,0x10F644}) {
        MEM_H(0,int32_t(0x80000000+address))=1;MEM_H(2,int32_t(0x80000000+address))=0xFFFF;
    }
    MEM_W(0,int32_t(0x801C6FB0))=3;
    srw64_game_hooks.name_begin(rdram,Review);srw64_game_hooks.name_step(rdram,&ctx,Review);
    r=request();assert(r.person==Review && r.active && r.names[1][2]==u"ア");before=memory;
    review(r.serial+1,true);srw64_game_hooks.name_step(rdram,&ctx,Review);assert(memory==before);
    review(r.serial,false);srw64_game_hooks.name_step(rdram,&ctx,Review);
    assert(state()==0 && transitions==4 && request().visible && MEM_BU(0,int32_t(0x801C70F4))!=2);
    MEM_W(0,int32_t(0x801C6FB0))=3;
    srw64_game_hooks.name_begin(rdram,Review);srw64_game_hooks.name_step(rdram,&ctx,Review);
    review(request().serial,true);srw64_game_hooks.name_step(rdram,&ctx,Review);
    assert(MEM_BU(0,int32_t(0x801C70F4))==2 && state()==3 && transitions==5 && ctx.r16==0x1234);
    // The original selection page (settings): its はい commits the defaults and leaves
    // for the story; any other step is the original's alone.
    srw64::settings::native=false;MEM_B(0,int32_t(0x801C70F4))=0;MEM_W(0,int32_t(0x801C6FB0))=0;
    MEM_H(0,int32_t(0x801C70FA))=1;
    srw64_game_hooks.name_begin(rdram,Selection);assert(!request().visible);
    assert(srw64_game_hooks.name_step(rdram,&ctx,Selection) && validations.size()==5 && MEM_BU(0,int32_t(0x801C70F4))==0);
    original_yes=true;srw64_game_hooks.name_step(rdram,&ctx,Selection);
    assert(validations.size()==7 && validations[5].editor==partner && MEM_BU(0,int32_t(0x801C70F4))==2 && transitions==5);
    srw64::settings::native=true;original_yes=false;
    queue_cover(42,true);queue_cover(43,false);assert(frame_cover(42) && !frame_cover(43));
    cover_presented(42,true);assert(cover_in_flight());cover_presented(43,false);cover_presented(42,true);assert(!cover_in_flight());
    overlay_loaded(0x123456,0x80200000,0x1000);
    overlay_loaded(0x10DA50,0x801C4500,0x7C50);assert(!srw64_game_hooks.name_step(rdram,&ctx,0));
    assert(!request().active && !request().visible && !owns_input());std::filesystem::remove_all(out);
    std::puts("native name entry: selection commits defaults, review, original はい, input ownership passed");
}
