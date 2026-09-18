#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

// Tint only the name-row glyph draws in the verified opening-dialogue overlay.
// HD glyph alpha supplies coverage; its active/inactive RGB is deliberately not
// used for names. Restore both RDP states immediately after each rectangle.
// The caller supplies the padded root display list; RDRAM is never written.
inline unsigned srw64_blue_dialogue_names(const uint8_t* ram, uint32_t start,
                                          uint32_t size, std::vector<uint8_t>& display) {
    auto word = [](const uint8_t* data, uint32_t p) {
        uint32_t value; std::memcpy(&value, data+p, 4); return value;
    };
    if (start > 0x800000 || size > 0x800000-start || size%8 ||
        display.size() != size_t(start)+size) return 0;
    const uint32_t signature[] = {0x3C058011,0x94A5F5C0,0x3C068011,0x94C6F5C2,
                                  0x27BDFFC8,0xAFB40028,0x0080A021,0xAFB60030};
    for (unsigned i=0;i<8;++i)
        if (word(ram,0x1C2600+4*i)!=signature[i]) return 0;
    std::vector<uint8_t> styled(start);
    auto emit = [&styled](uint32_t a, uint32_t b) {
        const auto at=styled.size(); styled.resize(at+8);
        std::memcpy(styled.data()+at,&a,4);
        std::memcpy(styled.data()+at+4,&b,4);
    };
    uint32_t combine_a{}, combine_b{}, primitive_a{}, primitive_b{};
    bool have_combine=false, have_primitive=false, font=false;
    unsigned count=0;
    for (uint32_t p=start;p<start+size;p+=8) {
        const auto a=word(display.data(),p), b=word(display.data(),p+4), op=a>>24;
        if (op==0xDE) have_combine=have_primitive=false;
        if (op==0xFC) {combine_a=a;combine_b=b;have_combine=true;}
        if (op==0xFA) {primitive_a=a;primitive_b=b;have_primitive=true;}
        if (op==0xFD) {
            const auto address=b&0x1FFFFFFF;
            font=a==0xFD4800FB && address>=8 && address<0x800000 &&
                word(ram,address-8)==0x000501F8 && word(ram,address-4)==0x01F80000;
        }
        const auto x=(b>>12)&0xFFF, y=b&0xFFF, right=(a>>12)&0xFFF, bottom=a&0xFFF;
        const bool name=(y==26*4 && x>=122*4 && right<=298*4) ||
                        (y==172*4 && x>=26*4 && right<=202*4);
        if (op==0xE4 && font && name && bottom-y==56 && (right-x==56 || right-x==32) &&
            have_combine && have_primitive && combine_a==0xFCFFFFFF && combine_b==0xFFFCF279 &&
            p+24<=start+size && word(display.data(),p+8)==0xE1000000 &&
            word(display.data(),p+16)==0xF1000000) {
            emit(0xE7000000,0); // Pipe sync before changing the combiner.
            emit(0xFA000000,0x69BFFFFF);
            // Both cycles: RGB = PRIMITIVE; alpha = TEXEL0 alpha.
            emit(0xFCFFFFFF,0xFFFDF2F9);
            emit(a,b);
            emit(word(display.data(),p+8),word(display.data(),p+12));
            emit(word(display.data(),p+16),word(display.data(),p+20));
            emit(0xE7000000,0);
            emit(combine_a,combine_b);
            emit(primitive_a,primitive_b);
            p+=16; ++count;
        }
        else emit(a,b);
    }
    if (count) display.swap(styled);
    return count;
}
