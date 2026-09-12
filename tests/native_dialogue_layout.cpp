#include "../tools/recomp/native-host/dialogue_layout.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>

int main(int argc,char** argv) {
    assert(argc==3 || argc==5);
    std::ifstream input(argv[1],std::ios::binary), taskfile(argv[2],std::ios::binary);
    std::vector<uint8_t> ram((std::istreambuf_iterator<char>(input)),{});
    assert(ram.size()==0x800000);
    for (size_t p=0;p<ram.size();p+=4) { std::swap(ram[p],ram[p+3]);std::swap(ram[p+1],ram[p+2]); }
    uint32_t task[16];taskfile.read(reinterpret_cast<char*>(task),sizeof(task));assert(taskfile.gcount()==sizeof(task));
    const auto original=ram;
    std::vector<uint8_t> display,again;
    const uint32_t start=task[12]&0x1FFFFFFF,size=task[13];
    const auto count=srw64_pad_dialogue(ram.data(),start,size,display);
    assert(count>=30 && count<100);assert(ram==original);
    assert(srw64_pad_dialogue(ram.data(),start,size,again)==count && again==display);
    unsigned changed=0;
    for (uint32_t p=start;p<start+size;p+=8) {
        uint32_t a,b,c,d;
        std::memcpy(&a,ram.data()+p,4);std::memcpy(&b,ram.data()+p+4,4);
        std::memcpy(&c,display.data()+p,4);std::memcpy(&d,display.data()+p+4,4);
        if(a==c && b==d)continue;
        ++changed;assert(a>>24==0xE4 && c>>24==0xE4);
        assert(((c>>12)&4095)==((a>>12)&4095)+16 && (c&4095)==(a&4095)+16);
        assert(((d>>12)&4095)==((b>>12)&4095)+16 && (d&4095)==(b&4095)+16);
    }
    assert(changed==count);
    ram[0x1C2600]^=1;assert(srw64_pad_dialogue(ram.data(),start,size,again)==0);
    assert(srw64_pad_dialogue(original.data(),0x7FFFF8,16,again)==0);
    std::cout<<"Verified "<<count<<" real dialogue glyphs: same size/UVs, translated +4/+4, original memory unchanged.\n";
    if(argc==5) {
        std::ifstream editor_ram(argv[3],std::ios::binary),editor_task(argv[4],std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(editor_ram)),{}),copy;
        assert(data.size()==0x800000);
        for(size_t p=0;p<data.size();p+=4){std::swap(data[p],data[p+3]);std::swap(data[p+1],data[p+2]);}
        editor_task.read(reinterpret_cast<char*>(task),sizeof(task));assert(editor_task.gcount()==sizeof(task));
        const uint32_t at=task[12]&0x1FFFFFFF,length=task[13];
        assert(srw64_pad_dialogue(data.data(),at,length,copy)==9);
        unsigned spaced=0;
        for(uint32_t p=at;p<at+length;p+=8) {
            uint32_t a,b,c,d;
            std::memcpy(&a,data.data()+p,4);std::memcpy(&b,data.data()+p+4,4);
            std::memcpy(&c,copy.data()+p,4);std::memcpy(&d,copy.data()+p+4,4);
            if(a==c && b==d)continue;
            ++spaced;assert(a>>24==0xE4 && c>>24==0xE4 && (b&4095)==256);
            const int delta=int((d>>12)&4095)-int((b>>12)&4095);
            assert(delta==24 || delta==48);assert(((c>>12)&4095)==((a>>12)&4095)+delta);
            assert((a&4095)==(c&4095));
        }
        assert(spaced==6);std::cout<<"Verified all three Chinese default-name fields with unchanged glyph size and no overlap.\n";
    }
}
