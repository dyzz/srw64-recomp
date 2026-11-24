// A campaign's own tactical map (src/host/map_mod.hpp): the compiled map's rules, the three
// original resources it stands in for, and the window their streams are read from.
#include "map_mod.hpp"
#include <cstdio>
using namespace srw64;
using json=nlohmann::json;
namespace fs=std::filesystem;

static int failures=0;
static void check(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what.c_str());++failures;}}
static std::string hex(const std::vector<uint8_t>& bytes) {
    std::string out;char pair[3];
    for(const auto b:bytes){std::snprintf(pair,sizeof pair,"%02X",b);out+=pair;}
    return out;
}
static std::string fails(const json& document) {
    try {map_mod::parse(document);} catch(const std::exception& e){return e.what();}
    return {};
}

int main(int argc,char** argv) {
    if(argc!=2){std::fprintf(stderr,"usage: test <scratch folder>\n");return 2;}
    const fs::path dir=argv[1];
    fs::remove_all(dir);fs::create_directories(dir/"one");
    // A 2 x 2 cell layout: header (7, one group, 4, 4), four cells, the group, its four placements.
    std::vector<uint8_t> layout{0,7,0,1,0,4,0,4};
    for(int i=0;i<4;++i)for(const uint8_t b:{0,99,0,2})layout.push_back(b);
    for(const uint8_t b:{0,2,0,4,0,30})layout.push_back(b);
    for(int i=0;i<4;++i)for(const int b:{0,0,0,i%2*16,0,i/2*16})layout.push_back(uint8_t(b));
    std::vector<uint8_t> atlas{0,8,2,0,2,0,0,0};atlas.resize(8+512*512,7);
    std::vector<uint8_t> palette{0,3,2,0,0,0,0,0};palette.resize(8+512,1);
    const json document={{"schema",map_mod::image_schema},{"replaces",{{"layout",6284},{"atlas",6228},{"palette",6243}}},
        {"layout_hex",hex(layout)},{"atlas_hex",hex(atlas)},{"palette_hex",hex(palette)},{"hd",true}};
    check(fails(document).empty(),"a small map: "+fails(document));
    {
        auto bad=document;bad["schema"]="x";check(!fails(bad).empty(),"another schema");
        bad=document;bad["replaces"]["atlas"]=7000;check(!fails(bad).empty(),"a number past the ROM's resources");
        bad=document;bad["replaces"]["atlas"]=6284;check(!fails(bad).empty(),"one number twice");
        bad=document;bad["atlas_hex"]=hex({0,8,2,0,2,0,0,0});check(!fails(bad).empty(),"an atlas of another size");
        bad=document;bad["palette_hex"]="0G";check(!fails(bad).empty(),"not hexadecimal");
        auto cut=layout;cut.resize(20);bad=document;bad["layout_hex"]=hex(cut);check(!fails(bad).empty(),"a layout cut short");
    }
    std::ofstream(dir/"one"/"map.json")<<document.dump();
    std::vector<uint8_t> ram(0x800000);
    uint32_t size{},offset{};
    check(!map_mod::descriptor(6284,size,offset),"no stage map: the original's own");
    check(map_mod::read(map_mod::window,ram.data(),0x80300000,4) && guest::read(ram.data(),0x80300000,4)==0,"the window is empty then");
    const auto map=map_mod::activate(dir/"one");
    check(map && map->hd && map->numbers==std::array<uint16_t,3>{6284,6228,6243},"the stage's map");
    check(map_mod::descriptor(6284,size,offset) && size==layout.size() && offset==map_mod::window-table_window::resource_table,"the layout's descriptor");
    check(map_mod::descriptor(6243,size,offset) && size==520 && offset==map_mod::window+2*map_mod::slot-table_window::resource_table,"the palette's");
    check(!map_mod::descriptor(6285,size,offset),"another map's layout is the ROM's");
    // The stream: the decoded size, then 0xFF and eight bytes at a time.
    check(map_mod::read(map_mod::window,ram.data(),0x80300000,16),"the layout's stream is served");
    check(guest::read(ram.data(),0x80300000,4)==layout.size() && guest::read(ram.data(),0x80300004,1)==0xFF &&
          guest::read(ram.data(),0x80300005,2)==7 && guest::read(ram.data(),0x8030000D,1)==0xFF,"decoded size, flag byte, the header");
    check(map_mod::read(map_mod::window+map_mod::slot+4,ram.data(),0x80300000,4) && guest::read(ram.data(),0x80300000,4)==0xFF000802,"the atlas's, from inside it");
    check(!map_mod::read(map_mod::window-4,ram.data(),0x80300000,4),"below the window is not ours");
    check(map_mod::activate(dir/"one")==map,"loaded once");
    bool threw=false;
    try {map_mod::activate(dir/"missing");} catch(const std::exception&){threw=true;}
    check(threw && !map_mod::current() && !map_mod::descriptor(6284,size,offset),"a map that cannot be read: none in place");
    map_mod::activate(dir/"one");
    check(!map_mod::activate({}) && !map_mod::descriptor(6284,size,offset),"the next stage has none");
    if(failures)std::fprintf(stderr,"%d failure(s)\n",failures);
    else std::printf("map MOD tests passed\n");
    return failures?1:0;
}
