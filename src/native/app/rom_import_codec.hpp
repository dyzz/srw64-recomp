#pragma once
// Pure bounded codecs, matching srw64_rom/text.py and resources.py.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace srw64::app::rom_import {
using Bytes = std::span<const uint8_t>;
inline Bytes slice(Bytes b, size_t p, size_t n) {
    if (p > b.size() || n > b.size()-p) throw std::runtime_error("ROM span exceeds input");
    return b.subspan(p,n);
}
inline uint32_t be32(Bytes b,size_t p) { auto s=slice(b,p,4);return (uint32_t(s[0])<<24)|(uint32_t(s[1])<<16)|(uint32_t(s[2])<<8)|s[3]; }
inline uint16_t be16(Bytes b,size_t p) { auto s=slice(b,p,2);return uint16_t((uint16_t(s[0])<<8)|s[1]); }
struct Decoded { std::vector<uint8_t> bytes; size_t consumed{}; };
inline Decoded lz_decode(Bytes source,size_t size) {
    if(size>16*1024*1024)throw std::runtime_error("Decoded resource exceeds importer budget");
    std::array<uint8_t,1024> ring{};
    size_t cursor=0x3be,input=0; unsigned flags=0;
    Decoded result;result.bytes.reserve(size);
    auto next=[&]{if(input==source.size())throw std::runtime_error("Truncated LZ stream");return source[input++];};
    auto emit=[&](uint8_t v){result.bytes.push_back(v);ring[cursor]=v;cursor=(cursor+1)&1023;};
    while(result.bytes.size()<size) {
        flags>>=1;if(!(flags&0x100))flags=unsigned(next())|0xff00;
        if(flags&1)emit(next());
        else {
            const unsigned low=next(),high=next(),start=low|((high&0xc0)<<2),length=(high&0x3f)+3;
            for(unsigned i=0;i<length && result.bytes.size()<size;++i)emit(ring[(start+i)&1023]);
        }
        flags&=0xffff;
    }
    result.consumed=input;return result;
}
inline Decoded resource(Bytes rom,size_t base,uint32_t id) {
    const auto count=be32(rom,base);
    const auto tail=slice(rom,base,rom.size()-base);
    if(count>(tail.size()-4)/8 || id>=count)throw std::runtime_error("Invalid resource descriptor table/id");
    const size_t p=4+size_t(id)*8;
    const auto encoded=slice(tail,be32(tail,p),be32(tail,p+4));
    if(encoded.size()<4)throw std::runtime_error("Resource span has no size header");
    return lz_decode(encoded.subspan(4),be32(encoded,0));
}
struct TextRecord { std::string key,text; size_t offset{},size{}; };
inline std::vector<TextRecord> text_records(Bytes rom,size_t pointers,unsigned tables,unsigned header,
                                           const std::map<uint16_t,std::string>& glyphs) {
    if(!tables || tables>100 || !header || header>64)throw std::runtime_error("Unsupported text layout");
    slice(rom,pointers,size_t(tables)*4);
    std::vector<TextRecord> result;size_t total_text=0;
    for(unsigned table=0;table<tables;++table) {
        const size_t base=be32(rom,pointers+size_t(table)*4);
        const auto count=be32(rom,base);
        const auto tail=slice(rom,base,rom.size()-base);
        if(count>65536 || count>(tail.size()-4)/8 || count>200000-result.size())throw std::runtime_error("Invalid or oversized text table");
        for(uint32_t id=0;id<count;++id) {
            const size_t descriptor=4+size_t(id)*8;
            const auto relative=be32(tail,descriptor),size=be32(tail,descriptor+4);
            const auto data=slice(tail,relative,size);
            if(size<header+2 || (size-header)%2 || be16(data,size-2)!=0xffff)throw std::runtime_error("Invalid text header/body/terminator");
            char key[32];std::snprintf(key,sizeof(key),"base:t%02u_%05u",table,unsigned(id));
            TextRecord record{key,{},base+relative,size};
            for(size_t p=header;p<size;p+=2) {
                const auto u=be16(data,p);
                if(u==0xffff)record.text+="<END>";
                else if(u==0xfffe)record.text+="<BR>";
                else if(u==0xfffd)record.text+="<STOP>";
                else if(u>=0x8000)throw std::runtime_error("Unapproved text control word");
                else {
                    const auto found=glyphs.find(u);
                    if(!(u>=0x124 && u<=0x12c) && found!=glyphs.end())record.text+=found->second;
                    else {char token[16];std::snprintf(token,sizeof(token),"<G:%04X>",unsigned(u));record.text+=token;}
                }
                if(record.text.size()>1024*1024)throw std::runtime_error("Text record exceeds importer budget");
            }
            total_text+=record.text.size();
            if(total_text>24*1024*1024)throw std::runtime_error("Text catalog exceeds importer budget");
            result.push_back(std::move(record));
        }
    }
    return result;
}
struct Portrait { unsigned width{},height{}; std::vector<uint8_t> rgba; };
inline Portrait portrait(Bytes image,Bytes palette) {
    const auto kind=be16(image,0),width=be16(image,2),height=be16(image,4);
    if((kind!=6 && kind!=15) || width!=height || (width!=96 && width!=97) || be16(image,6)!=0 || image.size()!=8+size_t(width)*height)
        throw std::runtime_error("Unexpected portrait image shape");
    constexpr std::array<uint8_t,8> magic{0,3,0,128,0,0,0,0};
    const auto ph=slice(palette,0,8);
    if(!std::equal(magic.begin(),magic.end(),ph.begin()) || (palette.size()-8)%2 || palette.size()>8+512)
        throw std::runtime_error("Unexpected portrait palette shape");
    Portrait result{width,height,{}};result.rgba.reserve(size_t(width)*height*4);
    for(auto index:image.subspan(8)) {
        const auto v=be16(palette,8+size_t(index)*2);
        for(unsigned shift:{11u,6u,1u})result.rgba.push_back(uint8_t((((v>>shift)&31)*255+15)/31));
        result.rgba.push_back((v&1)?255:0);
    }
    return result;
}
}
