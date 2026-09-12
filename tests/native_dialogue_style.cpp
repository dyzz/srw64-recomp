#include "../tools/recomp/native-host/dialogue_layout.hpp"
#include "../tools/recomp/native-host/dialogue_style.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    assert(argc==3);
    std::ifstream input(argv[1],std::ios::binary), taskfile(argv[2],std::ios::binary);
    std::vector<uint8_t> ram((std::istreambuf_iterator<char>(input)),{});
    assert(ram.size()==0x800000);
    for(size_t p=0;p<ram.size();p+=4) {std::swap(ram[p],ram[p+3]);std::swap(ram[p+1],ram[p+2]);}
    uint32_t task[16];taskfile.read(reinterpret_cast<char*>(task),sizeof(task));
    assert(taskfile.gcount()==sizeof(task));
    const auto original=ram;
    const auto start=task[12]&0x1FFFFFFF, size=task[13];
    std::vector<uint8_t> padded;
    assert(srw64_pad_dialogue(ram.data(),start,size,padded)==40);
    auto styled=padded;
    assert(srw64_blue_dialogue_names(ram.data(),start,size,styled)==6);
    assert(styled.size()==padded.size()+6*48);
    assert(ram==original);
    auto word=[](const auto& data, size_t p){uint32_t v;std::memcpy(&v,data.data()+p,4);return v;};
    // Undo only the documented wrappers; the entire original command stream,
    // including all body text, UVs, portraits and map draws, must be identical.
    std::vector<uint8_t> restored(start);
    unsigned seen=0;
    for(size_t p=start;p<styled.size();) {
        if(p+72<=styled.size() && word(styled,p)==0xE7000000 &&
           word(styled,p+8)==0xFA000000 && word(styled,p+12)==0x69BFFFFF) {
            assert(word(styled,p+16)==0xFCFFFFFF && word(styled,p+20)==0xFFFDF2F9);
            const auto a=word(styled,p+24), b=word(styled,p+28);
            assert(a>>24==0xE4 && ((b&4095)==26*4 || (b&4095)==172*4));
            assert(word(styled,p+48)==0xE7000000);
            assert(word(styled,p+56)==0xFCFFFFFF && word(styled,p+60)==0xFFFCF279);
            assert(word(styled,p+64)==0xFA000000 && word(styled,p+68)==0x000000FF);
            restored.insert(restored.end(),styled.begin()+p+24,styled.begin()+p+48);
            p+=72;++seen;
        } else {
            restored.insert(restored.end(),styled.begin()+p,styled.begin()+p+8);p+=8;
        }
    }
    assert(seen==6 && restored==padded);
    auto bad=ram;bad[0x1C2600]^=1;styled=padded;
    assert(srw64_blue_dialogue_names(bad.data(),start,size,styled)==0 && styled==padded);
    styled.pop_back();const auto short_copy=styled;
    assert(srw64_blue_dialogue_names(ram.data(),start,size,styled)==0 && styled==short_copy);
    assert(srw64_blue_dialogue_names(ram.data(),0x7FFFF8,16,styled)==0);
    std::cout<<"Six real name glyphs tinted; all source commands and memory preserved; state restored; invalid overlay and bounds rejected.\n";
}
