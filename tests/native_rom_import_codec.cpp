#include "app/rom_import_codec.hpp"
#include <iostream>
#include <functional>
using namespace srw64::app::rom_import;
static unsigned checks;
void check(bool v){++checks;if(!v)throw std::runtime_error("codec check failed");}
void rejects(const std::function<void()>& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
void u32(std::vector<uint8_t>& b,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)b.at(p+i)=uint8_t(v>>(24-i*8));}
int main(){try {
    check(lz_decode({},0).bytes.empty());
    const std::array<uint8_t,4> lit{7,'a','b','c'};
    check(lz_decode(lit,3).bytes==std::vector<uint8_t>({'a','b','c'}));
    const std::array<uint8_t,4> overlap{1,'a',0xbe,0xc3};
    check(lz_decode(overlap,7).bytes==std::vector<uint8_t>(7,'a'));check(lz_decode(overlap,2).consumed==4);
    const std::array<uint8_t,3> zero{0,0,0};check(lz_decode(zero,3).bytes==std::vector<uint8_t>(3));
    for(size_t n=0;n<lit.size();++n)rejects([&]{lz_decode(Bytes(lit).first(n),3);});
    rejects([&]{lz_decode({},17*1024*1024);});
    std::vector<uint8_t> rom(256);u32(rom,0,16);u32(rom,16,1);u32(rom,20,16);u32(rom,24,20);
    const std::array<uint16_t,6> text{0,1,0x124,0xfffd,0xfffe,0xffff};
    for(size_t i=0;i<text.size();++i){rom[40+i*2]=uint8_t(text[i]>>8);rom[41+i*2]=uint8_t(text[i]);}
    std::map<uint16_t,std::string> glyphs{{0," "},{1,"日"},{0x124,"must not use"}};
    const auto records=text_records(rom,0,1,8,glyphs);
    check(records.size()==1 && records[0].key=="base:t00_00000");check(records[0].text==" 日<G:0124><STOP><BR><END>");check(records[0].offset==32 && records[0].size==20);
    for(uint32_t count:{65537u,0xffffffffu}){auto b=rom;u32(b,16,count);rejects([&]{text_records(b,0,1,8,glyphs);});}
    {auto b=rom;u32(b,20,0xfffffff0);rejects([&]{text_records(b,0,1,8,glyphs);});}
    {auto b=rom;b[40]=0x80;rejects([&]{text_records(b,0,1,8,glyphs);});}
    {auto b=rom;b[51]=0;rejects([&]{text_records(b,0,1,8,glyphs);});}
    {auto b=rom;u32(b,24,19);rejects([&]{text_records(b,0,1,8,glyphs);});}
    rejects([&]{text_records(rom,255,1,8,glyphs);});rejects([&]{text_records(rom,0,101,8,glyphs);});
    std::vector<uint8_t> image(8+96*96,1);image[0]=0;image[1]=6;image[2]=0;image[3]=96;image[4]=0;image[5]=96;image[6]=image[7]=0;
    std::vector<uint8_t> palette{0,3,0,128,0,0,0,0,0,0,0xf8,1};
    const auto face=portrait(image,palette);check(face.width==96 && face.rgba.size()==96*96*4);
    check(std::vector<uint8_t>(face.rgba.begin(),face.rgba.begin()+4)==std::vector<uint8_t>({255,0,0,255}));
    palette.pop_back();rejects([&]{portrait(image,palette);});palette.push_back(1);image.back()=2;rejects([&]{portrait(image,palette);});
    std::vector<uint8_t> table(40);u32(table,0,1);u32(table,4,12);u32(table,8,8);u32(table,12,3);std::copy(lit.begin(),lit.end(),table.begin()+16);
    check(resource(table,0,0).bytes==std::vector<uint8_t>({'a','b','c'}));rejects([&]{resource(table,0,1);});rejects([&]{resource(table,100,0);});
    u32(table,4,0xfffffffe);rejects([&]{resource(table,0,0);});
    std::cout<<checks<<" codec checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
