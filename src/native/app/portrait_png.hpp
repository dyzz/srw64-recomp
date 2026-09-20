#pragma once
#include "rom_import_codec.hpp"
// Deliberately small RGBA8 writer for 96/97-pixel portraits. Stored DEFLATE
// blocks avoid adding a runtime compressor dependency. PNG (W3C), RFC 1950/1951.
namespace srw64::app::rom_import {
inline std::vector<uint8_t> portrait_png(const Portrait& image) {
    if(!image.width || image.width>128 || !image.height || image.height>128 ||
       image.rgba.size()!=size_t(image.width)*image.height*4)throw std::runtime_error("Invalid PNG portrait extent");
    std::vector<uint8_t> raw;
    for(unsigned y=0;y<image.height;++y){
        raw.push_back(0); // filter None
        const auto row=Bytes(image.rgba).subspan(size_t(y)*image.width*4,size_t(image.width)*4);
        raw.insert(raw.end(),row.begin(),row.end());
    }
    auto word=[](std::vector<uint8_t>& out,uint32_t n){for(unsigned shift:{24u,16u,8u,0u})out.push_back(uint8_t(n>>shift));};
    std::vector<uint8_t> z{0x78,0x01};
    for(size_t p=0;p<raw.size();){
        const size_t n=std::min<size_t>(65535,raw.size()-p);
        z.push_back(p+n==raw.size()?1:0);
        z.push_back(uint8_t(n));z.push_back(uint8_t(n>>8));
        z.push_back(uint8_t(~n));z.push_back(uint8_t((~n)>>8));
        z.insert(z.end(),raw.begin()+p,raw.begin()+p+n);p+=n;
    }
    uint32_t a=1,b=0;for(auto v:raw){a=(a+v)%65521;b=(b+a)%65521;}word(z,(b<<16)|a);
    std::vector<uint8_t> out{137,80,78,71,13,10,26,10};
    auto chunk=[&](const char* type,Bytes data){
        word(out,uint32_t(data.size()));
        uint32_t crc=0xffffffff;
        auto emit=[&](uint8_t v){out.push_back(v);crc^=v;for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0);};
        for(unsigned i=0;i<4;++i)emit(uint8_t(type[i]));for(auto v:data)emit(v);word(out,crc^0xffffffff);
    };
    std::vector<uint8_t> ihdr;word(ihdr,image.width);word(ihdr,image.height);
    ihdr.insert(ihdr.end(),{8,6,0,0,0});chunk("IHDR",ihdr);chunk("IDAT",z);chunk("IEND",{});
    return out;
}
}
