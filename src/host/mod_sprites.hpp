#pragma once
// A package's own battle pictures (docs/design/new-unit-ids.md §11): data/new_units.json's
// "sprites" are PNGs the host turns into the three resources the game draws a scene from
// (docs/data/battle-graphics.md): an atlas of 32x32 parts (CI8, kind 7, each frame on its own
// 32x32 grid as the ROM's poses are, one texel more for the filter),
// a palette (RGBA5551, 128 entries, entry 0 transparent) and a scene (steps, a frame table,
// parts and their vertices). They get resource numbers past the ROM's 6,436 and are served
// to the loader (80089E9C) as LZSS streams of literals only (mod_tables.hpp).
#include "stb/stb_image.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace srw64::mod_sprites {
struct Frame { unsigned width=0, height=0; std::vector<uint8_t> rgba; };
struct Step { unsigned frame=0, ticks=1; };
struct Built { std::vector<uint8_t> scene, atlas, palette; unsigned frames=0, parts=0, colors=0; };

inline constexpr unsigned tile=32, palette_entries=128, largest=1024;

inline void put16(std::vector<uint8_t>& out,unsigned v){out.push_back(uint8_t(v>>8));out.push_back(uint8_t(v));}
inline void put32(std::vector<uint8_t>& out,uint32_t v){put16(out,v>>16);put16(out,v&0xFFFF);}
inline void set16(std::vector<uint8_t>& out,size_t at,unsigned v){out[at]=uint8_t(v>>8);out[at+1]=uint8_t(v);}
inline void set32(std::vector<uint8_t>& out,size_t at,uint32_t v){set16(out,at,v>>16);set16(out,at+2,v&0xFFFF);}

// RGBA8 to the palette's RGBA5551; a pixel under half alpha is transparent (index 0).
inline uint16_t rgba5551(const uint8_t* p) {
    return uint16_t((p[0]>>3)<<11 | (p[1]>>3)<<6 | (p[2]>>3)<<1 | 1);
}

// The frames (same size or not) with the scene origin at pixel (origin_x, origin_y) of each:
// a part's x, y are its place less the origin, as the ROM's poses keep theirs (feet near 0).
// mirror: vertex mode 0 (each part also mirrored, for a sprite that faces a side, as a unit's
// pose or a weapon's actor); else mode 1 (one quad, as the cut-ins). Steps play frames for
// ticks each and go back to `loop` at the end. Empty string when it worked, else why not.
inline std::string build(const std::vector<Frame>& frames,int origin_x,int origin_y,bool mirror,
                         const std::vector<Step>& steps,unsigned loop,Built& out) {
    if(frames.empty())return "没有画面";
    if(steps.empty() || steps.size()>255)return "步骤要 1–255 个";
    if(loop>=steps.size())return "loop 超出步骤数";
    for(const auto& s:steps)if(s.frame>=frames.size() || !s.ticks || s.ticks>255)return "步骤的画面号或拍数不对（拍数 1–255）";
    // The palette: every opaque colour, in the order met.
    std::vector<uint16_t> colors{0};
    std::map<uint16_t,uint8_t> index;
    for(const auto& f:frames) {
        if(!f.width || !f.height || f.width>largest || f.height>largest || f.rgba.size()!=size_t(f.width)*f.height*4)return "图片大小不对（最大 1024×1024）";
        for(size_t i=0;i<size_t(f.width)*f.height;++i) {
            const uint8_t* p=&f.rgba[i*4];
            if(p[3]<128)continue;
            const uint16_t c=rgba5551(p);
            if(index.count(c))continue;
            if(colors.size()>=palette_entries)return "颜色太多：按 RGBA5551 算最多 127 种不透明颜色（先减色）";
            index[c]=uint8_t(colors.size());colors.push_back(c);
        }
    }
    const auto texel=[&](const Frame& f,int x,int y)->uint8_t{
        if(x<0 || y<0 || x>=int(f.width) || y>=int(f.height))return 0;
        const uint8_t* p=&f.rgba[(size_t(y)*f.width+size_t(x))*4];
        return p[3]<128?0:index.at(rgba5551(p));
    };
    // The atlas: each frame on its own 32x32 grid, the frames one under another. The game
    // draws a part with the texels right of and under it as well (the filter), so a part's
    // neighbours in the atlas must be its neighbours in the picture, as in the ROM's poses
    // (32k+1 wide and tall: their picture laid out as it is). Packing the parts closer drew
    // a one-texel line of another part along their edges. Under each frame a row of nothing,
    // and a column of nothing right of the widest.
    std::vector<unsigned> top(frames.size());
    unsigned width=1,height=0;
    for(size_t n=0;n<frames.size();++n) {
        const unsigned columns=(frames[n].width+tile-1)/tile,rows=(frames[n].height+tile-1)/tile;
        top[n]=height;
        height+=rows*tile+1;
        width=std::max(width,columns*tile+1);
    }
    out.atlas.clear();
    put16(out.atlas,7);put16(out.atlas,width);put16(out.atlas,height);put16(out.atlas,0);
    out.atlas.resize(8+size_t(width)*height,0);
    // Parts: every 32x32 tile of a frame with an opaque texel, at its place in the atlas.
    struct Part { unsigned s, t; int x, y; };
    std::vector<std::vector<Part>> parts(frames.size());
    for(size_t n=0;n<frames.size();++n) {
        const auto& f=frames[n];
        for(unsigned ty=0;ty<f.height;ty+=tile)for(unsigned tx=0;tx<f.width;tx+=tile) {
            bool any=false;
            for(unsigned y=0;y<tile;++y)for(unsigned x=0;x<tile;++x) {
                const uint8_t c=texel(f,int(tx+x),int(ty+y));
                out.atlas[8+size_t(top[n]+ty+y)*width+tx+x]=c;
                any|=c!=0;
            }
            if(any)parts[n].push_back({tx,top[n]+ty,int(tx)-origin_x,int(ty)-origin_y});
        }
        if(parts[n].empty())return "第 "+std::to_string(n+1)+" 张图是全透明的";
        if(parts[n].size()>64)return "第 "+std::to_string(n+1)+" 张图太大（最多 64 块 32×32）";
    }
    // The palette: kind 3, its byte count, four zero bytes, the colours.
    out.palette.clear();
    put16(out.palette,3);put16(out.palette,palette_entries*2);put32(out.palette,0);
    for(unsigned i=0;i<palette_entries;++i)put16(out.palette,i<colors.size()?colors[i]:0);
    // The scene: step count, vertex mode; (frame, ticks) steps; FF, loop; a u16 offset per
    // frame; each frame's 16-byte parts and an end part (0x8000, then the end of that frame's
    // vertices); 16-aligned, the vertices: 16 bytes each (x, y up, z, 0, s, t in 10.5, 0),
    // four for the quad (top left, bottom left, bottom right, top right) and, in mode 0, four
    // more mirrored about x = 0 for the other side.
    auto& sc=out.scene;
    sc.clear();
    sc.push_back(uint8_t(steps.size()));sc.push_back(mirror?0:1);
    for(const auto& s:steps){sc.push_back(uint8_t(s.frame));sc.push_back(uint8_t(s.ticks));}
    sc.push_back(0xFF);sc.push_back(uint8_t(loop));
    const size_t table=sc.size();
    for(size_t n=0;n<frames.size();++n)put16(sc,0);
    std::vector<std::pair<size_t,size_t>> slots;   // (part record, frame end record) by part, in order
    std::vector<size_t> ends(frames.size());
    for(size_t n=0;n<frames.size();++n) {
        set16(sc,table+n*2,unsigned(sc.size()));
        for(const auto& p:parts[n]) {
            slots.push_back({sc.size(),0});
            put16(sc,0);put16(sc,p.s);put16(sc,p.t);
            sc.push_back(tile);sc.push_back(tile);put16(sc,unsigned(p.x)&0xFFFF);put16(sc,unsigned(p.y)&0xFFFF);put32(sc,0);
        }
        ends[n]=sc.size();
        put16(sc,0x8000);for(unsigned i=0;i<10;++i)sc.push_back(0);put32(sc,0);
    }
    while(sc.size()%16)sc.push_back(0);
    const unsigned per=mirror?8:4;
    size_t k=0;
    for(size_t n=0;n<frames.size();++n) {
        for(const auto& p:parts[n]) {
            set32(sc,slots[k++].first+12,uint32_t(sc.size()));
            const int x0=p.x,x1=p.x+int(tile),y0=-p.y,y1=-(p.y+int(tile));
            const unsigned s1=tile<<5,t1=tile<<5;
            const auto vertex=[&](int x,int y,unsigned s,unsigned t){
                put16(sc,unsigned(x)&0xFFFF);put16(sc,unsigned(y)&0xFFFF);put16(sc,0);put16(sc,0);
                put16(sc,s);put16(sc,t);put32(sc,0);
            };
            vertex(x0,y0,0,0);vertex(x0,y1,0,t1);vertex(x1,y1,s1,t1);vertex(x1,y0,s1,0);
            if(per==8){vertex(-x1,y0,s1,0);vertex(-x1,y1,s1,t1);vertex(-x0,y1,0,t1);vertex(-x0,y0,0,0);}
        }
        set32(sc,ends[n]+12,uint32_t(sc.size()));
    }
    out.frames=unsigned(frames.size());out.parts=unsigned(slots.size());out.colors=unsigned(colors.size()-1);
    return {};
}

// A resource as the loader reads it from the descriptor's offset: the decoded size, then an
// LZSS stream (src/srw64_rom/resources.py lz_decode) of literals only, a flag byte 0xFF
// before every eight bytes.
inline std::vector<uint8_t> stream(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    put32(out,uint32_t(data.size()));
    for(size_t i=0;i<data.size();i+=8) {
        out.push_back(0xFF);
        for(size_t k=i;k<std::min(data.size(),i+8);++k)out.push_back(data[k]);
    }
    return out;
}

// A PNG as RGBA8; empty with `why` set when it cannot be read.
inline Frame load(const std::filesystem::path& file,std::string& why) {
    Frame f;
    int w=0,h=0,n=0;
    stbi_uc* pixels=stbi_load(file.string().c_str(),&w,&h,&n,4);
    if(!pixels){why=file.filename().string()+" 不是能读的 PNG";return f;}
    f.width=unsigned(w);f.height=unsigned(h);f.rgba.assign(pixels,pixels+size_t(w)*h*4);
    stbi_image_free(pixels);
    return f;
}
}
