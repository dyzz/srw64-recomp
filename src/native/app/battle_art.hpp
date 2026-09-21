#pragma once
#include "rom_import_codec.hpp"

// Original CI4/CI8 and battle pose composition. Matches battle_graphics.py;
// resources are decoded locally from the verified ROM, never bundled as art.
namespace srw64::app::rom_import {
inline Portrait battle_atlas(Bytes image,Bytes palette) {
    const unsigned kind=be16(image,0),w=be16(image,2),h=be16(image,4),colors=be16(palette,2)/2;
    const bool ci4=kind==5 || kind==14;
    if((!ci4 && kind!=6 && kind!=7 && kind!=8 && kind!=15) || !w || !h || w>2048 || h>2048 ||
       be16(image,6) || image.size()!=8+(ci4?(size_t(w)*h+1)/2:size_t(w)*h) || be16(palette,0)!=3 ||
       !colors || be16(palette,2)%2 || palette.size()!=8+size_t(colors)*2)
        throw std::runtime_error("Invalid battle atlas");
    Portrait result{uint16_t(w),uint16_t(h),std::vector<uint8_t>(size_t(w)*h*4)};
    for(size_t i=0;i<size_t(w)*h;++i) {
        const unsigned packed=image[8+(ci4?i/2:i)],index=ci4?((i%2)?packed&15:packed>>4):packed;
        if(index>=colors)throw std::runtime_error("Battle palette index exceeds bounds");
        const auto v=be16(palette,8+index*2);
        unsigned c=0;for(unsigned shift:{11u,6u,1u})result.rgba[i*4+c++]=uint8_t((((v>>shift)&31)*255+15)/31);
        result.rgba[i*4+3]=(v&1)?255:0;
    }
    return result;
}
inline Portrait battle_pose(Bytes scene,const Portrait& atlas) {
    const auto head=slice(scene,0,2);const unsigned steps=head[0],mode=head[1];
    const size_t table=4+steps*2;const auto sequence=slice(scene,2,steps*2+2);
    if(mode>2 || sequence[steps*2]!=255 || sequence[steps*2+1]>steps)throw std::runtime_error("Invalid battle pose sequence");
    const unsigned first=be16(scene,table);
    if(first<=table || (first-table)%2)throw std::runtime_error("Invalid pose frame offsets");
    const size_t count=(first-table)/2;
    unsigned chosen=0;for(unsigned i=0;i<steps;++i)if(sequence[i*2]!=255){chosen=sequence[i*2];break;}
    if(chosen>=count)throw std::runtime_error("Invalid pose frame");
    struct Part{unsigned flags,s,t,w,h;int x,y;};std::vector<std::vector<Part>> frames(count);
    int x0=32767,y0=32767,x1=-32768,y1=-32768;
    for(size_t f=0;f<count;++f) {
        for(size_t p=be16(scene,table+2*f);;p+=16) {
            const auto raw=slice(scene,p,16);const auto flags=be16(raw,0);if(flags&0x8000)break;
            if((flags&~0x10) || !raw[6] || !raw[7])throw std::runtime_error("Invalid pose part");
            if(mode<2)slice(scene,be32(raw,12),(mode?4:8)*16);
            Part a{flags,be16(raw,2),be16(raw,4),raw[6],raw[7],int16_t(be16(raw,8)),int16_t(be16(raw,10))};
            x0=std::min(x0,a.x);y0=std::min(y0,a.y);x1=std::max(x1,a.x+int(a.w));y1=std::max(y1,a.y+int(a.h));
            frames[f].push_back(a);
        }
    }
    if(x1<x0 || y1<y0){x0=y0=0;x1=y1=1;}
    const int w=x1-x0,h=y1-y0;
    if(w<1 || h<1 || w>2048 || h>2048)throw std::runtime_error("Pose extent exceeds budget");
    Portrait result{uint16_t(w),uint16_t(h),std::vector<uint8_t>(size_t(w)*h*4)};
    for(const auto& a:frames[chosen])for(unsigned y=0;y<a.h;++y)for(unsigned x=0;x<a.w;++x) {
        const unsigned sx=a.s+((a.flags&0x10)?a.w-1-x:x),sy=a.t+y;
        if(sx>=atlas.width || sy>=atlas.height)continue;
        const size_t src=(size_t(sy)*atlas.width+sx)*4,dst=(size_t(a.y-y0+y)*w+a.x-x0+x)*4;
        // RGBA16 alpha is binary; transparent parts preserve earlier pixels.
        if(atlas.rgba[src+3])std::copy_n(atlas.rgba.begin()+src,4,result.rgba.begin()+dst);
    }
    return result;
}
}
