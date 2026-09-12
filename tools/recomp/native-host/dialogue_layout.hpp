#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

// Native RDRAM is word-swapped. Copy the root display list for RT64 to consume;
// game memory, glyph advances, UVs and the original frame capture stay intact.
inline unsigned srw64_pad_dialogue(const uint8_t* ram, uint32_t start, uint32_t size,
                                  std::vector<uint8_t>& display) {
    auto word = [ram](uint32_t p) { uint32_t v; std::memcpy(&v, ram + p, 4); return v; };
    if (start > 0x800000 || size > 0x800000-start || size%8) return 0;
    // Exact opening-dialogue overlay signature; character creation and other
    // overlays reuse this address and must retain their own layout.
    const uint32_t signature[] = {0x3C058011,0x94A5F5C0,0x3C068011,0x94C6F5C2,
                                  0x27BDFFC8,0xAFB40028,0x0080A021,0xAFB60030};
    bool dialogue=true, editor=true;
    const uint32_t editor_signature[]={0x00002821,0x3C04801C,0x24847150,0x3C03801C,
                                       0x24637220,0xA4600000,0xAC800000,0x24840004};
    for (unsigned i=0;i<8;++i) {
        dialogue &= word(0x1C2600+4*i)==signature[i];
        editor &= word(0x1C2600+4*i)==editor_signature[i];
    }
    if(!dialogue && !editor)return 0;
    display.resize(start+size);
    std::memcpy(display.data()+start,ram+start,size);
    bool font=false;
    unsigned changed=0;
    for (uint32_t p=start;p<start+size;p+=8) {
        uint32_t first=word(p),second=word(p+4),op=first>>24;
        if (op==0xFD) {
            const uint32_t a=second&0x1FFFFFFF;
            font=first==0xFD4800FB && a>=8 && a<0x800000 &&
                 word(a-8)==0x000501F8 && word(a-4)==0x01F80000;
        }
        if (op!=0xE4 || !font) continue;
        const auto x=(second>>12)&0xFFF, y=second&0xFFF;
        const auto right=(first>>12)&0xFFF, bottom=first&0xFFF;
        if(editor) {
            // Default Chinese names use three wide glyphs in fields originally
            // spaced for narrow kana. Each three-character name fits its field.
            if(y!=64*4 || bottom-y!=56 || right-x!=56 || x<128*4 || x>=312*4)continue;
            const auto column=(x/4-128)/64,slot=(x/4-(128+column*64))/8;
            if(slot>2 || x!=(128+column*64+slot*8)*4)continue;
            const auto delta=slot*6*4;
            first=(first&0xFF000FFF)|((right+delta)<<12);
            second=(second&0xFF000FFF)|((x+delta)<<12);
            std::memcpy(display.data()+p,&first,4);
            std::memcpy(display.data()+p+4,&second,4);
            ++changed;continue;
        }
        // The two opening speech boxes, with room for the extra four pixels.
        const bool upper=x>=113*4 && right<=298*4 && y>=17*4 && bottom<=75*4;
        const bool lower=x>=17*4 && right<=203*4 && y>=163*4 && bottom<=221*4;
        if (!(upper||lower) || bottom-y!=56 || (right-x!=32 && right-x!=56)) continue;
        first=(first&0xFF000000)|((right+16)<<12)|(bottom+16);
        second=(second&0xFF000000)|((x+16)<<12)|(y+16);
        std::memcpy(display.data()+p,&first,4);
        std::memcpy(display.data()+p+4,&second,4);
        ++changed;
    }
    return changed;
}
