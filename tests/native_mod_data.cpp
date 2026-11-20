// Number MODs (src/host/mod_data.hpp, docs/design/mod-packages.md §5): the terrain ranks
// of data/units.json and data/pilots.json against the real ROM records, the file rules,
// the packages' order, and the copy patches they join.
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#include "mod_data.hpp"
#include "mod_forms.hpp"
#include "battle_speaker.hpp"
#include <cstdio>
#include <cstdlib>
#include <iterator>
uint64_t srw64_current_vi(){return 0;}
using namespace srw64;
using json=nlohmann::json;
namespace fs=std::filesystem;

static int failures=0;
static void check(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what.c_str());++failures;}}
static void write(const fs::path& path,const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary)<<text;
}
static std::string why(const json& document,bool pilots,std::span<const uint8_t> rom) {
    mod_data::Bytes bytes;
    return mod_data::parse(document,pilots?mod_data::Kind::pilots:mod_data::Kind::units,rom,bytes);
}

int main(int argc,char** argv) {
    if(argc!=3){std::fprintf(stderr,"usage: test <scratch folder> <rom>\n");return 2;}
    const fs::path dir=argv[1];
    fs::remove_all(dir);fs::create_directories(dir);
    std::ifstream in(argv[2],std::ios::binary);
    const std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(image.size()!=0x2000000){std::fprintf(stderr,"not the ROM\n");return 2;}
    const std::span<const uint8_t> rom(image);

    // ゴーショーグン (unit 184) is 空A 陸A 海B 宇A; 真吾 (pilot 133, record 110 alone) 空A 陸A 海B 宇B.
    const uint32_t unit=mod_data::unit_records+184*mod_data::unit_record_size+mod_data::unit_terrain;
    check(std::vector<uint8_t>(rom.begin()+unit,rom.begin()+unit+4)==std::vector<uint8_t>{4,4,3,4},"GoShogun's ranks in the ROM");
    check(mod_data::pilot_record(rom,133)==110 && mod_data::sharing(rom,133).empty(),"真吾 has a record of his own");
    const uint32_t pilot=mod_data::pilot_records+110*mod_data::pilot_record_size+mod_data::pilot_terrain;
    check(std::vector<uint8_t>(rom.begin()+pilot,rom.begin()+pilot+4)==std::vector<uint8_t>{4,4,3,3},"真吾's ranks in the ROM");

    mod_data::Bytes bytes;
    check(mod_data::parse(json::parse(R"({"184":{"terrain":{"sea":"A"}}})"),mod_data::Kind::units,rom,bytes).empty(),"a unit's sea rank");
    check(mod_data::parse(json::parse(R"({"133":{"terrain":{"space":"A","air":"-"}}})"),mod_data::Kind::pilots,rom,bytes).empty(),"a pilot's ranks");
    check(bytes==mod_data::Bytes{{unit+2,4},{pilot,0},{pilot+3,4}},"the bytes they become");

    // Numbers: big-endian words, weapons' power in hundreds, signed bytes, ammo -1 as none.
    const uint32_t unit_at=mod_data::unit_records+184*mod_data::unit_record_size, pilot_at=mod_data::pilot_records+110*mod_data::pilot_record_size;
    const uint32_t weapon_at=mod_data::weapon_records+731*mod_data::weapon_record_size;
    mod_data::Bytes numbers;
    check(mod_data::parse(json::parse(R"({"184":{"hp":5200,"move":7,"armor":1300,"repair_cost":4000,"upgrade_cap":10}})"),mod_data::Kind::units,rom,numbers).empty(),"unit numbers");
    check(numbers==mod_data::Bytes{{unit_at,0x14},{unit_at+1,0x50},{unit_at+6,7},{unit_at+0xA,0x05},{unit_at+0xB,0x14},{unit_at+0x14,0x0F},{unit_at+0x15,0xA0},{unit_at+0x20,10}},"unit bytes");
    numbers.clear();
    check(mod_data::parse(json::parse(R"({"133":{"melee":160,"sp":90,"double_move":40}})"),mod_data::Kind::pilots,rom,numbers).empty(),"pilot numbers");
    check(numbers==mod_data::Bytes{{pilot_at+1,160},{pilot_at+0xC,90},{pilot_at+0xE,40}},"pilot bytes, on 真吾's record");
    numbers.clear();
    // The のりかえ class, +0xB: Brad (27) is on record 17.
    check(mod_data::parse(json::parse(R"({"27":{"swap_class":8}})"),mod_data::Kind::pilots,rom,numbers).empty(),"pilot swap class");
    check(numbers==mod_data::Bytes{{mod_data::pilot_records+17*mod_data::pilot_record_size+0xB,8}},"pilot swap class byte");
    numbers.clear();
    check(!mod_data::parse(json::parse(R"({"27":{"swap_class":9}})"),mod_data::Kind::pilots,rom,numbers).empty(),"pilot swap class range");
    numbers.clear();
    check(mod_data::parse(json::parse(R"({"731":{"power":3600,"hit":-5,"ammo":-1,"critical":20,"terrain":{"space":"A"}}})"),mod_data::Kind::weapons,rom,numbers).empty(),"weapon numbers");
    check(numbers==mod_data::Bytes{{weapon_at+1,36},{weapon_at+4,0xFB},{weapon_at+5,0xFF},{weapon_at+0xD,20},{weapon_at+0xC,4}},"weapon bytes");
    const auto weapon=[&](const char* text){mod_data::Bytes b;return mod_data::parse(json::parse(text),mod_data::Kind::weapons,rom,b);};
    check(weapon(R"({"731":{"power":3650}})").find("100")!=std::string::npos,"power in hundreds");
    check(weapon(R"({"731":{"power":25600}})").find("超出范围")!=std::string::npos,"power at most 25500");
    check(weapon(R"({"731":{"range_min":5,"range_max":3}})").find("range_min")!=std::string::npos,"min range past max range");
    check(weapon(R"({"731":{"range_min":30}})").find("range_min")!=std::string::npos,"min range past the ROM's max range");
    check(weapon(R"({"731":{"range_max":30}})").empty(),"an ordinary weapon's range goes to 30");
    check(weapon(R"({"16":{"range_max":15}})").empty(),"a map weapon's range goes to 15");
    check(weapon(R"({"16":{"range_max":16}})").find("地图武器")!=std::string::npos,"a map weapon's range stops at 15");
    check(weapon(R"({"731":{"hit":"5"}})").find("整数")!=std::string::npos && weapon(R"({"731":{"hit":1.5}})").find("整数")!=std::string::npos,"integers only");
    check(weapon(R"({"1329":{"power":100}})").find("超出范围")!=std::string::npos,"weapon numbers in range");
    // The kind byte's bits, the requirement code, the upgrade type and the lock (mod_data.hpp's
    // comment): 731 スペースバズーカ is 0x40 ranged, not P, no beam, not parryable, no requirement.
    {
        mod_data::Bytes b;
        check(mod_data::parse(json::parse(R"({"731":{"attack":"melee","post_move":true,"beam":true,"parryable":true,"terrain_at":"target","requires":"newtype_4","upgrade_type":4,"locked":true}})"),mod_data::Kind::weapons,rom,b).empty(),"weapon kind fields");
        check(rom[weapon_at]==0x40 && b[weapon_at]==0x8E && b[weapon_at+8]==13 && b[weapon_at+0xE]==4 && b[weapon_at+0xF]==(rom[weapon_at+0xF]|0x04),"kind bits set, requirement 13, type 4, locked");
        mod_data::Bytes c;
        check(mod_data::parse(json::parse(R"({"209":{"post_move":false,"requires":null,"terrain_at":"attacker","locked":false}})"),mod_data::Kind::weapons,rom,c).empty(),"fields cleared");
        const uint32_t fin=mod_data::weapon_records+209*mod_data::weapon_record_size;
        check(c[fin]==(rom[fin]&~0x04)&&(c[fin]&0x40) && c[fin+8]==0 && c[fin+0xF]==(rom[fin+0xF]&~0x04),"P off, 0x40 on, no requirement, unlocked");
        check(!weapon(R"({"731":{"post_move":1}})").empty() && !weapon(R"({"731":{"attack":"beam"}})").empty() && !weapon(R"({"731":{"terrain_at":"self"}})").empty(),"booleans and names checked");
        check(weapon(R"({"731":{"requires":"ninja"}})").find("newtype")!=std::string::npos && !weapon(R"({"731":{"upgrade_type":5}})").empty() && !weapon(R"({"731":{"map":true}})").empty(),"unknown requirements, types and fields refused");
    }

    // Abilities keep the bits a file cannot name (forms); size keeps +4's high bits; skills
    // write their threshold row; spirits their six pairs.
    {
        mod_data::Bytes b;
        check(mod_data::parse(json::parse(R"({"184":{"abilities":["i_field","hp_regen_20"],"shield":true,"sword":false,"size":"LL","parts_slots":4}})"),mod_data::Kind::units,rom,b).empty(),"unit equipment");
        const uint32_t rom_bits=uint32_t(rom[unit_at+0x1C])<<24|rom[unit_at+0x1D]<<16|rom[unit_at+0x1E]<<8|rom[unit_at+0x1F];
        const uint32_t bits=uint32_t(b[unit_at+0x1C])<<24|b[unit_at+0x1D]<<16|b[unit_at+0x1E]<<8|b[unit_at+0x1F];
        check(bits==((rom_bits&0x1C2)|0x20|0x8),"abilities replace the nameable bits only");
        check(b[unit_at+0x18]==((rom[unit_at+0x18]|2)&~1) && b[unit_at+4]==((rom[unit_at+4]&0xC0)|0x10) && b[unit_at+0x19]==4,"shield, size, slots");
        b.clear();
        // Move types keep +5's high bits; the のりかえ class, funds and experience are plain fields.
        check(mod_data::parse(json::parse(R"({"184":{"move_types":["air","underground"],"swap_class":3,"funds":12000,"exp":200}})"),mod_data::Kind::units,rom,b).empty(),"unit move types, class, rewards");
        check(b[unit_at+5]==((rom[unit_at+5]&0xE0)|0x12) && b[unit_at+0x12]==3 && b[unit_at+0x16]==0x2E && b[unit_at+0x17]==0xE0 && b[unit_at+0x1A]==200,"+5, +12, +16, +1A");
        b.clear();
        check(!mod_data::parse(json::parse(R"({"184":{"move_types":[]}})"),mod_data::Kind::units,rom,b).empty(),"no move type");
        check(!mod_data::parse(json::parse(R"({"184":{"move_types":["fly"]}})"),mod_data::Kind::units,rom,b).empty(),"unknown move type");
        check(mod_data::parse(json::parse(R"({"184":{"swap_class":8}})"),mod_data::Kind::units,rom,b).empty(),"swap class 8");
        check(!mod_data::parse(json::parse(R"({"184":{"swap_class":9}})"),mod_data::Kind::units,rom,b).empty(),"swap class range");
        b.clear();
        check(mod_data::parse(json::parse(R"({"133":{"skills":{"newtype":[1,4,8,12,18,24,30,38,45],"parry":null},"spirits":[["focus",1],["valor",10],["soul",40]]}})"),mod_data::Kind::pilots,rom,b).empty(),"skills and spirits");
        const uint32_t rows=mod_data::threshold_records+110*mod_data::threshold_record_size;
        check(b[pilot_at+0xF]==((rom[pilot_at+0xF]&~0x7D)|0x08) && b[rows]==1 && b[rows+8]==45 && b[rows+9]==0,"newtype on row 0, parry off");
        const int sr=mod_data::spirit_record(rom,133);
        const uint32_t sp=mod_data::spirit_records+uint32_t(sr)*mod_data::spirit_record_size;
        check(sr>=0 && b[sp]==1 && b[sp+1]==4 && b[sp+3]==11 && b[sp+5]==17 && b[sp+6]==0xFF && b[sp+7]==0 && b[sp+10]==0xFF,"spirits as (level, command) pairs, unused ones out of reach");
        const auto pilot=[&](const char* text){mod_data::Bytes x;return mod_data::parse(json::parse(text),mod_data::Kind::pilots,rom,x);};
        check(!pilot(R"({"133":{"skills":{"newtype":[1],"esp":[2]}}})").empty(),"one first-row skill");
        check(!pilot(R"({"133":{"skills":{"newtype":[5,1]}}})").empty() && !pilot(R"({"133":{"skills":{"newtype":[]}}})").empty(),"levels ascending, not empty");
        check(!pilot(R"({"133":{"spirits":[["focus",5],["valor",1]]}})").empty() && !pilot(R"({"133":{"spirits":[["fly",1]]}})").empty(),"spirits ascending, known");
        mod_data::Bytes x;
        check(!mod_data::parse(json::parse(R"({"184":{"abilities":["transform"]}})"),mod_data::Kind::units,rom,x).empty(),"forms are not a file's to give");
    }

    // The file rules; a bad file adds nothing.
    check(!why(json::array(),false,rom).empty(),"the top is an object");
    check(!why(json::parse(R"({"0184":{}})"),false,rom).empty() && !why(json::parse(R"({"x":{}})"),false,rom).empty(),"numbers are plain decimals");
    check(!why(json::parse(R"({"363":{}})"),false,rom).empty() && !why(json::parse(R"({"360":{}})"),true,rom).empty(),"numbers in range");
    check(!why(json::parse(R"({"184":{"speed":5}})"),false,rom).empty(),"unknown fields refused");
    check(!why(json::parse(R"({"184":{"terrain":{"water":"A"}}})"),false,rom).empty(),"unknown channels refused");
    check(!why(json::parse(R"({"184":{"terrain":{"sea":"S"}}})"),false,rom).empty() && !why(json::parse(R"({"184":{"terrain":{"sea":4}}})"),false,rom).empty(),"ranks are letters");
    check(!why(json::parse(R"({"0":{"terrain":{"sea":"A"}}})"),true,rom).empty(),"a pilot without a record");
    mod_data::Bytes untouched;
    mod_data::parse(json::parse(R"({"184":{"terrain":{"sea":"A"}},"185":{"terrain":{"sea":"S"}}})"),mod_data::Kind::units,rom,untouched);
    check(untouched.empty(),"all or nothing");

    // Packages: the upper one wins, a disabled one and a bad file count for nothing.
    const fs::path mods_dir=dir/"mods";
    const auto package=[&](const std::string& id,const std::string& units) {
        write(mods_dir/id/"mod.json",json({{"schema","srw64.mod.v1"},{"id",id},{"version","1.0.0"},{"name",id}}).dump());
        write(mods_dir/id/"data"/"units.json",units);
    };
    package("test.lower",R"({"184":{"terrain":{"sea":"C","land":"B"}}})");
    package("test.upper",R"({"184":{"terrain":{"sea":"A"}}})");
    package("test.off",R"({"184":{"terrain":{"air":"D"}}})");
    package("test.broken","{");
    write(mods_dir/"test.upper"/"data"/"parts.json","{}");
    write(mods_dir/"mods.json",json({{"schema",mods::state_schema},{"order",{"test.lower","test.upper","test.off","test.broken"}},{"disabled",{"test.off"}}}).dump());
    setenv("SRW64_MODS_DIR",mods_dir.string().c_str(),1);
    mods::set_game_version({0,5,0});
    const auto found=mod_data::collect(mods::installed(mods::game_version()),rom);
    check(found.bytes==mod_data::Bytes{{unit+1,3},{unit+2,4}},"the upper package wins");
    check(found.problems.size()==2,"a broken file and an unsupported one are reported");

    // The copy patches: the rules file's stay underneath, a rank the ROM already has is no patch.
    upgrades::patches()={{0x100,7}};
    mod_data::configure(rom,dir);
    mod_data::refresh();
    check(upgrades::patches()==std::vector<std::pair<uint32_t,uint8_t>>{{0x100,7},{unit+1,3},{unit+2,4}},"patches merged");
    check(fs::is_regular_file(dir/"mod-data.json"),"a report");
    check(upgrades::game_byte(rom,unit+1)==3 && upgrades::game_byte(rom,0x100)==7 && upgrades::game_byte(rom,unit+3)==rom[unit+3],"the bytes as other threads read them");
    mods::set_enabled("test.lower",false);
    mod_data::refresh();
    check(upgrades::patches()==std::vector<std::pair<uint32_t,uint8_t>>{{0x100,7},{unit+2,4}},"again when the packages change");

    // The table window (table_window.hpp): the ROM's rows read where they are, the weapon
    // lists' offsets 0x1000 further on with the region after them.
    namespace tw=table_window;
    const auto window=[&](const mod_units::Bytes& b,uint32_t at){return mod_units::read_byte(b,rom,at);};
    const auto w16=[&](const mod_units::Bytes& b,uint32_t at){return unsigned(window(b,at))<<8|window(b,at+1);};
    const auto w32=[&](const mod_units::Bytes& b,uint32_t at){return uint32_t(w16(b,at))<<16|w16(b,at+2);};
    check(tw::row(tw::units,184)==mod_data::unit_records+184*0x24 && tw::row(tw::units,400)==tw::base(tw::units)+400*0x24,"rows: the ROM's, the window's");
    check(window({},tw::base(tw::units)+184*0x24+0xE)==4 && window({},tw::base(tw::battle_units)+360*14)==rom[0x118610+360*14],"the window reads the ROM's rows");
    // Row -1 (a speaker with no face asks for portrait -1): the ROM's bytes before the table.
    bool before=true;
    for(uint32_t i=1;i<=4;++i)before&=window({},tw::base(tw::portraits)-i)==rom[0x84220-i] && window({},tw::base(tw::thresholds)-i)==rom[0x7B1B0-i];
    check(before,"row -1 reads the ROM before the table");
    // Past the ROM's rows: a number outside the new range reads on into the ROM as the original
    // did; a new number no package gives reads row 0.
    check(window({},tw::base(tw::portraits)+380*4)==rom[0x84220+380*4] && window({},tw::base(tw::portraits)+450*4+1)==rom[0x84220+1],
          "past the table: the ROM's bytes, or row 0 for a new number");
    {
        const uint32_t lists=tw::base(tw::weapon_lists),rom_offset=uint32_t(rom[0x7E210+224*4])<<24|rom[0x7E210+224*4+1]<<16|rom[0x7E210+224*4+2]<<8|rom[0x7E210+224*4+3];
        check(w32({},lists+224*4)==rom_offset+tw::lists_copy && w16({},lists+w32({},lists+224*4))==(unsigned(rom[0x7E210+rom_offset])<<8|rom[0x7E210+rom_offset+1]),"a unit's weapon list through the window");
        // A number no package gives (a save loaded without its package, until turned back):
        // an empty list, row 0's records, never zeros walked as records.
        check(w16({},lists+w32({},lists+500*4))==0xFFFF,"an empty list for a number no package gives");
        check(w16({},tw::row(tw::units,500))==w16({},tw::row(tw::units,0)) && w32({},tw::row(tw::anim_offsets,2000))==w32({},tw::row(tw::anim_offsets,0)),"row 0's records for it");
        upgrades::NameSources none;uint32_t from=0,size=0;
        check(upgrades::name_source(rom,none,tw::unit_name(500),from,size) && size>8 && size<=tw::name_slot,"unit 0's name record for it");
    }
    check(tw::name_record(tw::NameKind::unit,224)==527+224 && tw::name_record(tw::NameKind::unit,400)==0xF000 && tw::name_record(tw::NameKind::weapon_menu,1401)==0xF801,"name records");
    check(tw::name_of(0xF401) && tw::name_of(0xF401)->number==1401 && !tw::name_of(0xEFFF),"a private record's number");

    // Numbers (mod_units.hpp assign): by the key's hash, the same in any order, in range.
    {
        const json a={{"schema",mod_units::schema},{"units",{{"final",json::object()}}},{"weapons",{{"cannon",json::object()},{"sword",json::object()}}}};
        const json b={{"schema",mod_units::schema},{"units",{{"other",json::object()}}}};
        const auto one=mod_units::assign({{"pkg.a",a},{"pkg.b",b}}),two=mod_units::assign({{"pkg.b",b},{"pkg.a",a}});
        check(one.units==two.units && one.weapons==two.weapons,"numbers do not follow the packages' order");
        const unsigned final_unit=one.units.at("pkg.a@final");
        check(tw::new_unit(final_unit) && tw::new_unit(one.units.at("pkg.b@other")) && final_unit!=one.units.at("pkg.b@other"),"units in their range, apart");
        check(tw::new_weapon(one.weapons.at("pkg.a@cannon")) && one.weapon_keys.at(one.weapons.at("pkg.a@sword"))=="pkg.a@sword","weapons in theirs");
        check(mod_units::assign({{"pkg.a",a}}).units.at("pkg.a@final")==final_unit,"another package leaves a number be unless they clash");
        check(mod_units::resolve(json("@final"),"pkg.a",one,mod_units::Of::unit)==final_unit && mod_units::resolve(json("pkg.a@final"),"pkg.b",one,mod_units::Of::unit)==final_unit,"@name and package@name");
        check(mod_units::resolve(json(224),"pkg.a",one,mod_units::Of::unit)==224u && mod_units::resolve(json("224"),"pkg.a",one,mod_units::Of::unit)==224u,"the ROM's numbers");
        check(!mod_units::resolve(json(363),"pkg.a",one,mod_units::Of::unit) && !mod_units::resolve(json("@none"),"pkg.a",one,mod_units::Of::unit),"nothing else");
        // Every name of 600 gets a number: the clashes move on.
        json many={{"schema",mod_units::schema},{"units",json::object()}};
        for(unsigned i=0;i<600;++i)many["units"]["u"+std::to_string(i)]=json::object();
        check(mod_units::assign({{"p",many}}).units.size()==tw::last_new_unit-tw::first_new_unit+1,"the range fills up, then no more");
    }

    // New units (mod_units.hpp parse): rows in the window. A copy drops 変形, 分離 and 合体,
    // which need form tables the new number is in none of, and keeps the other abilities.
    // ダンクーガ 224 is 分離 (0x40) alone; ビルバイン 248 is 0x4012 (オーラバリア, 分身, 変形).
    const auto abilities=[&](const mod_units::Bytes& b,unsigned u){return w32(b,tw::row(tw::units,u)+0x1C);};
    check(abilities({},224)==0x40 && abilities({},248)==0x4012,"224's and 248's abilities in the ROM");
    for(const auto& [source,expected]:{std::pair{224u,0u},std::pair{248u,0x4010u}}) {
        mod_units::Bytes added;
        mod_units::Taken taken;
        const json document={{"schema",mod_units::schema},{"units",{{"final",{{"copy",source},{"weapons",{870}}}}}}};
        const auto numbers=mod_units::assign({{"pkg",document}});
        check(mod_units::parse(document,"pkg",numbers,rom,{},added,taken).empty(),"a new unit copying "+std::to_string(source));
        check(abilities(added,numbers.units.at("pkg@final"))==expected,"a new unit copying "+std::to_string(source)+" has no form abilities");
    }
    {
        // A Final Dancouga: a scene, a weapon with its own animation, a unit carrying it.
        const json document=json::parse(R"({"schema":"srw64.new-units.v2",
            "scenes":{"pose":[2560,1716,2118]},
            "weapons":{"cannon":{"copy":875,"inherits_from":875,"animation":{"camera":1,"hit":59,"action":84,"defender":125,"sounds":[219],
                "actors":[{"scene":"@pose","h4":1,"h5":255,"behavior":359},{"scene":1016,"h4":16452,"h5":255,"behavior":240}]}},
                "sword":{"copy":880}},
            "units":{"final":{"copy":224,"pose":[2324,1724,2016],"icon":978,"weapons":[870,"@sword","@cannon"],"inherits_from":224,"crew":[40,41,42,43]}}})");
        const auto numbers=mod_units::assign({{"pkg",document}});
        const unsigned unit=numbers.units.at("pkg@final"),cannon=numbers.weapons.at("pkg@cannon"),sword=numbers.weapons.at("pkg@sword");
        mod_units::Bytes added;
        mod_units::Taken taken;
        check(mod_units::parse(document,"pkg",numbers,rom,{},added,taken).empty(),"a Final Dancouga");
        check(w16(added,tw::row(tw::units,unit))==w16({},tw::row(tw::units,224)),"its record is 224's");
        check(w16(added,tw::row(tw::poses,unit))==2324 && w16(added,tw::row(tw::map_icons,unit))==978,"its pose and icon");
        const uint32_t lists=tw::base(tw::weapon_lists),row=lists+w32(added,lists+unit*4);
        check(w16(added,row)==870 && w16(added,row+2)==unit && w16(added,row+12)==sword && w16(added,row+24)==cannon && w16(added,row+36)==0xFFFF,"its weapon list");
        check(taken.scenes.at("pkg@pose")==tw::rom_scenes && w16(added,tw::row(tw::scenes,tw::rom_scenes))==2560,"the scene past the ROM's");
        const uint32_t record=tw::animation_bank+w32(added,tw::row(tw::anim_offsets,cannon));
        check(record==tw::base(tw::anim_records) && w16(added,record)==1 && w16(added,record+10)==0xFFFF && w16(added,record+12)==tw::rom_scenes,"the cannon's animation names the scene");
        check(w32(added,tw::row(tw::anim_offsets,sword))==w32({},tw::row(tw::anim_offsets,880)),"the sword's is 880's");
        check(w16(added,tw::row(tw::weapons,cannon)+0)==w16({},tw::row(tw::weapons,875)),"the cannon's record is 875's");
        check(taken.names.at(tw::unit_name(unit)).second && taken.names.at(tw::weapon_menu(cannon)).second,"name records copied from 224's and 875's");
        check(taken.predecessors.at(unit)==224 && taken.copies.at(unit)==224 && taken.weapon_copies.at(cannon)==875,"its predecessor, the copies");
        // 870 is 224's own weapon, the cannon inherits 875's levels; the sword inherits nothing.
        const auto& carried=taken.carries.at(unit);
        check(std::find(carried.begin(),carried.end(),std::pair<unsigned,unsigned>{870,870})!=carried.end() &&
              std::find(carried.begin(),carried.end(),std::pair<unsigned,unsigned>{cannon,875})!=carried.end() &&
              std::none_of(carried.begin(),carried.end(),[&](const auto& c){return c.first==sword;}),"the weapons' levels it carries");
        check((taken.crews.at(unit)==std::vector<unsigned>{40,41,42,43}),"its crew");
        {
            // Any number of new units inherit (no rows of the ROM's 30-row table are borrowed).
            json many={{"schema",mod_units::schema},{"units",json::object()}};
            for(int i=0;i<8;++i)many["units"]["u"+std::to_string(i)]={{"copy",224},{"weapons",{870}},{"inherits_from",224}};
            mod_units::Bytes b2;mod_units::Taken t3;
            check(mod_units::parse(many,"pkg",mod_units::assign({{"pkg",many}}),rom,{},b2,t3).empty() && t3.predecessors.size()==8,"eight new units inherit");
            // A unit combined from five parts over Dancouga (mod_combine.hpp).
            json five={{"schema",mod_units::schema},{"units",{{"final",{{"copy",224},{"weapons",{870}},{"work",21},
                {"combine",{{"parts",{219,230,233,225,228}},{"over",{223,224}}}}}}}}};
            mod_units::Bytes b5;mod_units::Taken t5;
            const auto n5=mod_units::assign({{"pkg",five}});
            check(mod_units::parse(five,"pkg",n5,rom,{},b5,t5).empty() && t5.combines.size()==1 &&
                  (t5.combines.at(n5.units.at("pkg@final")).parts==std::vector<unsigned>{219,230,233,225,228}) &&
                  (t5.combines.at(n5.units.at("pkg@final")).over==std::vector<unsigned>{223,224}),"a unit combined from five parts");
            check(t5.works.at(n5.units.at("pkg@final"))==21,"a new unit's Library work");
            // A form family (mod_forms.hpp): one shared weapon list naming the forms per row,
            // 変形 back on for both, the records as the original's.
            {
                const json zeta=json::parse(R"({"schema":"srw64.new-units.v2","units":{
                    "z":{"copy":48,"weapons":[193,195,196,198],"forms":["@wr"]},
                    "wr":{"copy":49,"weapons":[199,196,198]}}})");
                const auto nz=mod_units::assign({{"pkg",zeta}});
                mod_units::Bytes bz;mod_units::Taken tz;
                {const auto why=mod_units::parse(zeta,"pkg",nz,rom,{},bz,tz);if(!why.empty())std::fprintf(stderr,"%s\n",why.c_str());check(why.empty(),"a form family");}
                const unsigned z=nz.units.at("pkg@z"),wr=nz.units.at("pkg@wr");
                check(tz.forms.size()==1 && (tz.forms.at(z)==std::vector<unsigned>{z,wr}),"the family, its base first");
                const auto w32=[&](uint32_t a){return uint32_t(bz.at(a))<<24|uint32_t(bz.at(a+1))<<16|uint32_t(bz.at(a+2))<<8|bz.at(a+3);};
                check(w32(tw::row(tw::weapon_lists,z))==w32(tw::row(tw::weapon_lists,wr)),"both forms read one list");
                const uint32_t list=tw::base(tw::weapon_lists)+w32(tw::row(tw::weapon_lists,z));
                const auto u16=[&](uint32_t a){return unsigned(bz.at(a)<<8|bz.at(a+1));};
                std::vector<std::vector<unsigned>> rows;
                for(uint32_t r=list;u16(r)!=0xFFFF;r+=12){std::vector<unsigned> row{u16(r)};for(unsigned f=1;f<6;++f)if(u16(r+f*2)!=0xFFFF)row.push_back(u16(r+f*2));rows.push_back(row);}
                check(rows==std::vector<std::vector<unsigned>>{{193,z},{195,z},{196,z,wr},{198,z,wr},{199,wr}},"each row names the forms that have the weapon");
                check((bz.at(tw::row(tw::units,z)+0x1F)&0x02) && (bz.at(tw::row(tw::units,wr)+0x1F)&0x02),"変形 on for both");
                check((bz.at(tw::row(tw::units,z)+4)&0x80) && !(bz.at(tw::row(tw::units,wr)+4)&0x80),"only the base form goes out");
                const json alone=json::parse(R"({"schema":"srw64.new-units.v2","units":{"wr":{"copy":49,"weapons":[199]}}})");
                const auto na=mod_units::assign({{"pkg",alone}});
                mod_units::Bytes ba;mod_units::Taken ta;
                check(mod_units::parse(alone,"pkg",na,rom,{},ba,ta).empty() && (ba.at(tw::row(tw::units,na.units.at("pkg@wr"))+4)&0x80),
                      "a copy of a form that waits goes out on its own");
                check((mod_forms::record({z,wr},wr)==std::vector<uint8_t>{1,0,uint8_t(z>>8),uint8_t(z),0xFF,0xFF,uint8_t(z>>8),uint8_t(z)}),"wr's record: one other form, z, base z");
                json loop=zeta;loop["units"]["wr"]["forms"]={"@z"};
                mod_units::Bytes bl;mod_units::Taken tl;
                check(!mod_units::parse(loop,"pkg",mod_units::assign({{"pkg",loop}}),rom,{},bl,tl).empty(),"a unit in two families is refused");
                json outside=zeta;outside["units"]["z"]["forms"]={49};
                mod_units::Bytes bo;mod_units::Taken to;
                check(!mod_units::parse(outside,"pkg",mod_units::assign({{"pkg",outside}}),rom,{},bo,to).empty(),"an original unit as a form is refused");
            }
            // A combination attack (mod_combos.hpp): the record's combination bit, the units,
            // their component weapons and the multiplier.
            {
                const json duo=json::parse(R"({"schema":"srw64.new-units.v2",
                    "weapons":{"duo":{"copy":878,"combination":{"units":["@z",228],"parts":[198,899],"multiplier":1.6,"pilots":[["@hero",166]]}}},
                    "units":{"z":{"copy":48,"weapons":[193,"@duo"]}},"pilots":{"hero":{"copy":133}}})");
                const auto nd=mod_units::assign({{"pkg",duo}});
                mod_units::Bytes bd;mod_units::Taken td;
                {const auto why=mod_units::parse(duo,"pkg",nd,rom,{},bd,td);if(!why.empty())std::fprintf(stderr,"%s\n",why.c_str());check(why.empty(),"a combination attack");}
                const unsigned w=nd.weapons.at("pkg@duo"),z=nd.units.at("pkg@z");
                check(bd.at(tw::row(tw::weapons,w)+0xF)&0x02,"its record's combination bit");
                check(bd.at(tw::row(tw::weapons,w)+0xE)==0,"no upgrade type, as the original's");
                const auto& c=td.combinations.at(w);
                check((c.units==std::vector<unsigned>{z,228}) && (c.parts==std::vector<unsigned>{198,899}) && std::abs(c.multiplier-1.6f)<1e-6f &&
                      (c.pilots==std::vector<std::vector<unsigned>>{{nd.pilots.at("pkg@hero"),166}}),"its units, parts, multiplier and pilots");
                json short_parts=duo;short_parts["weapons"]["duo"]["combination"]["parts"]={198};
                mod_units::Bytes bs;mod_units::Taken ts;
                check(!mod_units::parse(short_parts,"pkg",mod_units::assign({{"pkg",short_parts}}),rom,{},bs,ts).empty(),"a part for each unit");
            }
            json far=five;far["units"]["final"]["work"]=25;
            mod_units::Bytes b7;mod_units::Taken t7;
            check(!mod_units::parse(far,"pkg",mod_units::assign({{"pkg",far}}),rom,{},b7,t7).empty(),"a work past the original's 25");
            // A new character copies an original's records at its own rows; pilots.json writes
            // its stats there by name (docs/design/unit-mod-kit.md §5.4).
            {
                json people={{"schema",mod_units::schema},{"pilots",{{"hero",{{"copy",133},{"voice",4}}}}}};
                const auto np=mod_units::assign({{"pkg",people}});
                mod_units::Bytes bp;mod_units::Taken tp;
                {const auto why=mod_units::parse(people,"pkg",np,rom,{},bp,tp);if(!why.empty())std::fprintf(stderr,"%s\n",why.c_str());check(why.empty(),"a new character");}
                const unsigned hero=np.pilots.at("pkg@hero");
                check(tw::new_pilot(hero) && tp.pilot_copies.at(hero)==133 && tp.voices.at(hero)==4,"its copy and voice");
                const int record=mod_data::pilot_record(rom,133);
                bool same=true;
                for(uint32_t i=0;i<16;++i)same&=bp.at(tw::row(tw::pilot_records,hero)+i)==rom[tw::row(tw::pilot_records,unsigned(record))+i];
                check(same,"its ability record is the original's");
                check(tp.names.count(tw::name_record(tw::NameKind::pilot,hero)) && tp.names.count(tw::name_record(tw::NameKind::pilot_full,hero)),"its name records");
                mod_data::Bytes stats=bp;
                check(mod_data::parse(json::parse(R"({"@hero":{"melee":200,"spirits":[["focus",1]]}})"),mod_data::Kind::pilots,rom,stats,"pkg",np).empty(),"its stats by name");
                check(stats.at(tw::row(tw::pilot_records,hero)+1)==200 && stats.at(tw::row(tw::spirit_records,hero)+1)==4,"melee and the first spirit at its rows");
                // Its own battle quotes (mod_quotes.hpp): a voice part of its own, generic
                // segments at the package's records, the rest the borrowed part's, and a
                // conditional list in the window.
                {
                    namespace mq=mod_quotes;
                    const json said=json::parse(R"({"schema":"srw64.battle-quotes.v1","pilots":{"@hero":{
                        "attack":[{"ja":"「行くぞ!」","zh-Hans":"「上了！」","voice":"hero-attack.ogg"},{"ja":"「二つ目」"}],
                        "evade":[{"ja":"「当たるか!」"}],
                        "special":[{"weapon":870,"lines":[{"ja":"「必殺!」"}]},{"opponent":4,"situation":"evade","lines":[{"ja":"「ドモンか」"}]}]}}})");
                    mod_units::Bytes bq=bp;mq::Taken tq;
                    {const auto why=mq::parse(said,"pkg",np,rom,bq,tp.voices,tq);if(!why.empty())std::fprintf(stderr,"%s\n",why.c_str());check(why.empty(),"battle quotes");}
                    const unsigned part=tw::voice_of(hero);
                    check(part>=257 && tq.voices.at(hero)==part && tq.lines.size()==5 && tq.lines[0].speaker==hero && tq.lines[0].voice=="hero-attack.ogg","its own voice part and five lines");
                    const auto u16=[&](uint32_t at){return unsigned(bq.at(at)<<8|bq.at(at+1));};
                    const uint32_t seg=tw::row(tw::quote_segments,part);
                    check(u16(seg)==unsigned(mq::text_first-mq::generic_first) && u16(seg+2)==2,"attack: two lines from the first record");
                    check(u16(seg+5*4)==unsigned(mq::text_first+2-mq::generic_first) && u16(seg+5*4+2)==1,"evade: the third");
                    const int borrowed=mq::rom_voice(rom,4);
                    check(borrowed>=0 && u16(seg+4)==unsigned(rom[0x1161C0+borrowed*36+4]<<8|rom[0x1161C0+borrowed*36+5]),"shot down: the borrowed part's");
                    const uint32_t list=tw::base(tw::quote_lists)+(part-tw::first_new_voice)*tw::quote_list,index=tw::row(tw::quote_index,part);
                    check((u16(index)<<16|u16(index+2))==list-tw::quote_lists_rom,"the list's offset from 0x20F250");
                    const unsigned field=unsigned(rom[tw::row(tw::battle_weapons,870)]<<8|rom[tw::row(tw::battle_weapons,870)+1]);
                    check(u16(list)==2600+field && u16(list+2)==unsigned(mq::text_first+3-mq::special_first),"a weapon line: 2600 + its quote code");
                    check(u16(list+4)==25000+400+unsigned(mq::rom_voice(rom,4)) && u16(list+8)==0xFFFF,"an opponent line when evading, then the end");
                    const auto refused=[&](const char* text){mod_units::Bytes b=bp;mq::Taken t;return !mq::parse(json::parse(text),"pkg",np,rom,b,tp.voices,t).empty() && t.lines.empty() && b==bp;};
                    check(refused(R"({"schema":"srw64.battle-quotes.v1","pilots":{"4":{"attack":[{"ja":"「あ」"}]}}})"),"an original character is refused");
                    check(refused(R"({"schema":"srw64.battle-quotes.v1","pilots":{"@hero":{"attack":[{"zh-Hans":"上"}]}}})"),"a line without ja");
                    check(refused(R"({"schema":"srw64.battle-quotes.v1","pilots":{"@hero":{"attack":[{"ja":"一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三"}]}}})"),"more than three rows");
                    check(refused(R"({"schema":"srw64.battle-quotes.v1","pilots":{"@hero":{"taunt":[{"ja":"「あ」"}]}}})"),"an unknown situation");
                    check(refused(R"({"schema":"srw64.battle-quotes.v1","pilots":{"@hero":{"attack":[{"ja":"「あ」","voice":"../x.ogg"}]}}})"),"a voice outside the folder");
                }
                // Love rows (the partner's too when mutual) and who it counts as in combination attacks.
                json loving=people;loving["pilots"]["hero"]["love"]=json::parse(R"([{"with":166,"mutual":true}])");loving["pilots"]["hero"]["combo_as"]=4;
                mod_units::Bytes bl;mod_units::Taken tl;
                check(mod_units::parse(loving,"pkg",mod_units::assign({{"pkg",loving}}),rom,{},bl,tl).empty() && tl.loves.size()==2 &&
                      tl.loves[0]==std::pair<unsigned,unsigned>{hero,166} && tl.loves[1]==std::pair<unsigned,unsigned>{166,hero} && tl.combo_as.at(hero)==4,"love both ways, combo_as");
                json selfish=people;selfish["pilots"]["hero"]["love"]=json::parse(R"([{"with":"@hero"}])");
                mod_units::Bytes bs;mod_units::Taken ts;
                check(!mod_units::parse(selfish,"pkg",mod_units::assign({{"pkg",selfish}}),rom,{},bs,ts).empty(),"no love for oneself");
                json bad=people;bad["pilots"]["hero"]["copy"]=360;
                mod_units::Bytes b8;mod_units::Taken t8;
                check(!mod_units::parse(bad,"pkg",mod_units::assign({{"pkg",bad}}),rom,{},b8,t8).empty(),"a copy past the original's");
            }
            json one=five;one["units"]["final"]["combine"]["parts"]={219};
            mod_units::Bytes b6;mod_units::Taken t6;
            check(!mod_units::parse(one,"pkg",mod_units::assign({{"pkg",one}}),rom,{},b6,t6).empty(),"one part is refused");
            json crewless=many;crewless["units"]["u0"]["crew"]=json::array();
            mod_units::Bytes b3;mod_units::Taken t4;
            check(!mod_units::parse(crewless,"pkg",mod_units::assign({{"pkg",crewless}}),rom,{},b3,t4).empty(),"an empty crew is refused");
        }
        // A number file names them.
        mod_data::Bytes b=added;
        check(mod_data::parse(json::parse(R"({"@final":{"hp":8000}})"),mod_data::Kind::units,rom,b,"pkg",numbers).empty(),"data/units.json names a new unit");
        check(w16(b,tw::row(tw::units,unit))==8000,"its HP in the window");
        check(mod_data::parse(json::parse(R"({"@cannon":{"power":3500,"range_max":7}})"),mod_data::Kind::weapons,rom,b,"pkg",numbers).empty() && window(b,tw::row(tw::weapons,cannon)+3)==7,"data/weapons.json names a new weapon");
        check(!mod_data::parse(json::parse(R"({"@nothing":{"hp":1}})"),mod_data::Kind::units,rom,b,"pkg",numbers).empty(),"an unknown name is refused");
        const json bad=json::parse(R"({"schema":"srw64.new-units.v2","units":{"x":{"copy":224,"weapons":["@missing"]}}})");
        mod_units::Bytes none;mod_units::Taken t2;
        check(!mod_units::parse(bad,"pkg",mod_units::assign({{"pkg",bad}}),rom,{},none,t2).empty() && none.empty(),"a missing weapon refuses the file");
    }

    // Sprites (mod_sprites.hpp): PNG frames into an atlas, a palette and a scene the game draws.
    {
        namespace ms=mod_sprites;
        ms::Frame f;f.width=40;f.height=40;f.rgba.assign(40*40*4,0);
        for(unsigned y=0;y<40;++y)for(unsigned x=0;x<40;++x) {
            uint8_t* p=&f.rgba[(y*40+x)*4];
            p[0]=x<20?255:0;p[1]=y<20?255:0;p[2]=40;p[3]=255;   // four colours
        }
        ms::Built b;
        check(ms::build({f},20,40,true,{{0,1}},0,b).empty(),"a sprite builds");
        // Left for tests/test_mod_sprites.py, which reads them with the ROM tools' decoders.
        std::ofstream(dir/"sprite.scene",std::ios::binary).write(reinterpret_cast<const char*>(b.scene.data()),std::streamsize(b.scene.size()));
        std::ofstream(dir/"sprite.atlas",std::ios::binary).write(reinterpret_cast<const char*>(b.atlas.data()),std::streamsize(b.atlas.size()));
        std::ofstream(dir/"sprite.palette",std::ios::binary).write(reinterpret_cast<const char*>(b.palette.data()),std::streamsize(b.palette.size()));
        const auto u16=[](const std::vector<uint8_t>& d,size_t a){return unsigned(d[a])<<8|d[a+1];};
        const auto s16=[&](const std::vector<uint8_t>& d,size_t a){return int(int16_t(u16(d,a)));};
        check(u16(b.atlas,0)==7 && u16(b.atlas,2)==4*32+1 && u16(b.atlas,4)==33 && b.atlas.size()==8+129*33,"its atlas: kind 7, 32k+1");
        check(u16(b.palette,0)==3 && u16(b.palette,2)==256 && b.palette.size()==264 && u16(b.palette,8)==0 && b.colors==4,"its palette: 128 entries, 0 transparent");
        const auto& sc=b.scene;
        check(sc[0]==1 && sc[1]==0 && sc[2]==0 && sc[3]==1 && sc[4]==0xFF && sc[5]==0 && u16(sc,6)==8,"its steps and frame table");
        check(b.parts==4 && u16(sc,8)==0 && u16(sc,10)==0 && u16(sc,12)==0 && sc[14]==32 && sc[15]==32 && s16(sc,16)==-20 && s16(sc,18)==-40,"its first part");
        check(u16(sc,8+4*16)==0x8000,"the frame's end part");
        const uint32_t v=uint32_t(u16(sc,20))<<16|u16(sc,22);
        check(v%16==0 && s16(sc,v)==-20 && s16(sc,v+2)==40 && s16(sc,v+16+2)==8 && u16(sc,v+16+10)==0x400,"its vertices: x, y up, t in 10.5");
        check(s16(sc,v+64)==-12 && s16(sc,v+64+2)==40 && u16(sc,v+64+8)==0x400 && s16(sc,v+64+48)==20,"the mirrored set");
        check(uint32_t(u16(sc,8+4*16+12))<<16 == 0 && u16(sc,8+4*16+14)==sc.size(),"the end part ends the vertices");
        // The stream: literals only, eight to a flag byte.
        const auto st=ms::stream(b.scene);
        std::vector<uint8_t> back;
        for(size_t i=4;i<st.size() && back.size()<b.scene.size();) {const uint8_t flags=st[i++];for(int k=0;k<8 && i<st.size() && back.size()<b.scene.size();++k){if(!(flags>>k&1))break;back.push_back(st[i++]);}}
        check((uint32_t(st[0])<<24|st[1]<<16|st[2]<<8|st[3])==b.scene.size() && back==b.scene,"the stream decodes to the scene");
        ms::Built one;
        check(ms::build({f},20,40,false,{{0,1}},0,one).empty() && one.scene[1]==1 && one.scene.size()<b.scene.size(),"mode 1: one quad a part");
        ms::Frame many;many.width=16;many.height=16;many.rgba.assign(16*16*4,255);
        for(unsigned i=0;i<256;++i){many.rgba[i*4]=uint8_t(i);many.rgba[i*4+1]=uint8_t(i*7);}
        check(!ms::build({many},0,0,true,{{0,1}},0,one).empty(),"more than 127 colours refused");
        // In a package: PNGs in its folder, the pose and a scene by @name, resource numbers past the ROM's.
        const fs::path pack=dir/"sprite-pack";
        fs::create_directories(pack/"art");
        stbi_write_png((pack/"art"/"stand.png").string().c_str(),40,40,4,f.rgba.data(),40*4);
        stbi_write_png((pack/"art"/"stand-hd.png").string().c_str(),40,40,4,f.rgba.data(),40*4);
        const json document=json::parse(R"({"schema":"srw64.new-units.v2",
            "sprites":{"stand":{"image":"art/stand.png","origin":[20,40],"hd":"art/stand-hd.png"}},
            "scenes":{"pose":"@stand"},
            "units":{"final":{"copy":224,"pose":"@stand","weapons":[870]}}})");
        const auto numbers=mod_units::assign({{"pkg",document}});
        mod_units::Bytes added;mod_units::Taken taken;
        check(mod_units::parse(document,"pkg",numbers,rom,{},added,taken,pack).empty(),"a package with a sprite");
        const auto& sprite=taken.sprites.at("pkg@stand");
        check(sprite.scene==tw::rom_resources && sprite.atlas==tw::rom_resources+1 && sprite.palette==tw::rom_resources+2 && taken.resources.size()==3,"three resources past the ROM's");
        const unsigned unit=numbers.units.at("pkg@final");
        check(w16(added,tw::row(tw::poses,unit))==tw::rom_resources && w16(added,tw::row(tw::poses,unit)+4)==tw::rom_resources+2,"the pose names them");
        check(w16(added,tw::row(tw::scenes,tw::rom_scenes))==tw::rom_resources,"so does the scene");
        check(mod_units::art(sprite).value("width",0)==40 && mod_units::art(sprite).contains("hd"),"its picture for the pages");
        const json escape=json::parse(R"({"schema":"srw64.new-units.v2","sprites":{"x":{"image":"../x.png","origin":[0,0]}}})");
        mod_units::Taken t3;mod_units::Bytes b3;
        check(!mod_units::parse(escape,"pkg",numbers,rom,{},b3,t3,pack).empty(),"a sprite's file stays inside the package");
        mod_tables::publish_resources(taken.resources);
        uint32_t size=0,offset=0;
        check(mod_tables::resource(uint16_t(tw::rom_resources),size,offset) && size==taken.resources.at(tw::rom_resources).size() && offset+tw::resource_table==tw::resource_window,"the loader's descriptor of it");
        check(!mod_tables::resource(100,size,offset) && !mod_tables::resource(uint16_t(tw::rom_resources+3),size,offset),"the ROM's and unknown ones are the original's");
        // A new character's own portrait: a 96x96 picture becomes a CI8 image and a 64-colour
        // palette, its portrait row names them, the digests are of their data.
        ms::Frame face;face.width=96;face.height=96;face.rgba.assign(96*96*4,0);
        for(unsigned i=0;i<96*96;++i)if((i%96)>=8 && (i%96)<88){face.rgba[i*4]=uint8_t((i/96)<48?255:0);face.rgba[i*4+2]=200;face.rgba[i*4+3]=255;}
        stbi_write_png((pack/"art"/"kai.png").string().c_str(),96,96,4,face.rgba.data(),96*4);
        stbi_write_png((pack/"art"/"kai-hd.png").string().c_str(),96,96,4,face.rgba.data(),96*4);
        const json people=json::parse(R"({"schema":"srw64.new-units.v2","pilots":{"kai":{"copy":133,"portrait":{"image":"art/kai.png","hd":"art/kai-hd.png"}}}})");
        const auto pn=mod_units::assign({{"pkg",people}});
        mod_units::Bytes pb;mod_units::Taken pt;
        check(mod_units::parse(people,"pkg",pn,rom,{},pb,pt,pack).empty(),"a character with its own portrait");
        const unsigned kai=pn.pilots.at("pkg@kai");
        const auto& own=pt.portraits.at(kai);
        check(own.image==tw::rom_resources && own.palette==tw::rom_resources+1 && w16(pb,tw::row(tw::portraits,kai))==own.image && w16(pb,tw::row(tw::portraits,kai)+2)==own.palette,"two resources, named by its row");
        const auto& picture=pt.resources.at(own.image);const auto& colours=pt.resources.at(own.palette);
        check(picture.size()==8+96*96 && picture[1]==15 && picture[3]==96 && colours.size()==8+128 && colours[1]==3 && colours[3]==0x80,"a kind-15 96x96 image and a kind-3 palette");
        uint64_t d=0xCBF29CE484222325ull;for(size_t i=8;i<picture.size();++i)d=(d^picture[i])*0x100000001B3ull;
        check(d==own.pixel_digest && picture[8]==0 && picture[8+8]!=0 && !own.hd.empty(),"its digest, transparent edge, the HD picture");
        json big=people;big["pilots"]["kai"]["portrait"]["image"]="art/stand.png";
        mod_units::Bytes bb;mod_units::Taken bt;
        check(!mod_units::parse(big,"pkg",mod_units::assign({{"pkg",big}}),rom,{},bb,bt,pack).empty(),"a portrait must be 96x96");
    }

    {
        // Who says a battle quote (battle_speaker.hpp): the slot's participant, shown for the
        // record's speaker when it is a new character with that speaker's voice part.
        namespace bs=srw64::battle_speaker;
        std::vector<uint8_t> ram(0x800000);
        const auto w16=[&](uint32_t a,unsigned v){srw64::guest::write16(ram.data(),a,uint16_t(v));};
        const auto w32=[&](uint32_t a,uint32_t v){srw64::guest::write32(ram.data(),a,v);};
        w16(bs::attacker_index,1);w16(bs::defender_index,0);
        w32(bs::attacker_pilot,0x80300000);w16(0x80300002,433);   // the rival attacks
        w32(bs::defender_pilot,0x80300100);w16(0x80300102,166);   // Alan defends
        w16(tw::pilot_voice_copy+2*298,120);w16(tw::pilot_voice_copy+2*433,120);w16(tw::pilot_voice_copy+2*166,30);
        w16(tw::pilot_voice_copy+2*434,30);w16(tw::pilot_voice_copy+2*435,0xFFFF);
        w16(bs::records+bs::record_size+2,120);w16(bs::records+2,30);   // each record's voice part
        check(bs::character_of_slot(ram.data(),bs::records+bs::record_size+bs::attack_slot)==433,"the attacker's slot");
        check(bs::character_of_slot(ram.data(),bs::records+bs::hit_slot)==166,"the defender's slot");
        w16(bs::attacker_index,0);w16(bs::defender_index,1);   // Alan's counter: the indices swap, the pointers stay
        check(bs::character_of_slot(ram.data(),bs::records+bs::record_size+bs::hit_slot)==433,"the rival hit by the counter: still the rival");
        w16(tw::pilot_voice_copy+2*433,30);w16(bs::records+bs::record_size+2,30);   // both of one voice part: the indices decide
        check(bs::character_of_slot(ram.data(),bs::records+bs::record_size+bs::hit_slot)==166,"one voice part: by the index words");
        w16(tw::pilot_voice_copy+2*433,120);w16(bs::records+bs::record_size+2,120);w16(bs::attacker_index,1);w16(bs::defender_index,0);
        check(!bs::character_of_slot(ram.data(),bs::records+0x30) && !bs::character_of_slot(ram.data(),0x80300000),"no participant's slot");
        const auto swap=bs::swap(ram.data(),298,433);
        check(swap && swap->first==298 && swap->second==433,"the rival's quote, a ムゲ兵 record: shown as the rival");
        check(!bs::swap(ram.data(),166,166) && !bs::swap(ram.data(),298,166),"Alan's own lines stay his");
        check(!bs::swap(ram.data(),166,433),"another voice part's line (a two-person exchange) keeps its speaker");
        check(!bs::swap(ram.data(),4,273),"the original's copies (Domon 273) stay as they are");
        check(!bs::swap(ram.data(),435,435) && !bs::swap(ram.data(),166,435),"no voice part: nothing to borrow");
        bs::active()={298,433};
        check(bs::shown(298)==433 && bs::shown(166)==166,"during the call");
        bs::active()={};
        check(bs::shown(298)==298,"and not after it");
    }

    if(failures)std::fprintf(stderr,"%d failures\n",failures);
    else std::puts("mod data: ok");
    return failures?1:0;
}
