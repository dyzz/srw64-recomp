// Number MODs (src/host/mod_data.hpp, docs/design/mod-packages.md §5): the terrain ranks
// of data/units.json and data/pilots.json against the real ROM records, the file rules,
// the packages' order, and the copy patches they join.
#include "mod_data.hpp"
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
    check(mod_data::parse(json::parse(R"({"731":{"power":3600,"hit":-5,"ammo":-1,"critical":20,"terrain":{"space":"A"}}})"),mod_data::Kind::weapons,rom,numbers).empty(),"weapon numbers");
    check(numbers==mod_data::Bytes{{weapon_at+1,36},{weapon_at+4,0xFB},{weapon_at+5,0xFF},{weapon_at+0xD,20},{weapon_at+0xC,4}},"weapon bytes");
    const auto weapon=[&](const char* text){mod_data::Bytes b;return mod_data::parse(json::parse(text),mod_data::Kind::weapons,rom,b);};
    check(weapon(R"({"731":{"power":3650}})").find("100")!=std::string::npos,"power in hundreds");
    check(weapon(R"({"731":{"power":25600}})").find("超出范围")!=std::string::npos,"power at most 25500");
    check(weapon(R"({"731":{"range_min":5,"range_max":3}})").find("range_min")!=std::string::npos,"min range past max range");
    check(weapon(R"({"731":{"range_min":30}})").find("range_min")!=std::string::npos,"min range past the ROM's max range");
    check(weapon(R"({"731":{"hit":"5"}})").find("整数")!=std::string::npos && weapon(R"({"731":{"hit":1.5}})").find("整数")!=std::string::npos,"integers only");
    check(weapon(R"({"1329":{"power":100}})").find("超出范围")!=std::string::npos,"weapon numbers in range");

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

    if(failures)std::fprintf(stderr,"%d failures\n",failures);
    else std::puts("mod data: ok");
    return failures?1:0;
}
