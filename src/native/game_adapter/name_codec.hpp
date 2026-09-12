#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace srw64::names {
// Only original, decoded font glyphs enter guest buffers. Unicode codepoints
// and dynamic-name/script opcodes must never be mistaken for font indices.
class Codec {
    std::map<char16_t,uint16_t> encode_map;
    std::map<uint16_t,char16_t> decode_map;
    std::array<uint8_t,8> header{};
public:
    static constexpr uint16_t first_id=0xE000, last_glyph=0x081E;
    static constexpr uint32_t virtual_base=0x02400000, table_base=0x01A34980;
    static constexpr std::array<unsigned,3> limits={7,7,5}, offsets={0,8,16};
    void add(uint16_t glyph,const std::u16string& value) {
        if(glyph>last_glyph || (glyph>=0x124 && glyph<=0x12C) || value.size()!=1 ||
           value[0]<0x20 || (value[0]>=0xD800 && value[0]<=0xDFFF))return;
        decode_map.emplace(glyph,value[0]);encode_map.emplace(value[0],glyph);
        if(value[0]>=0xFF01 && value[0]<=0xFF5E)encode_map.emplace(value[0]-0xFEE0,glyph);
    }
    void initialize_rom(const uint8_t* rom,size_t size) {
        if(size!=0x2000000)throw std::runtime_error("Native name codec requires the pinned JP ROM");
        auto word=[&](uint32_t p){return (uint32_t(rom[p])<<24)|(rom[p+1]<<16)|(rom[p+2]<<8)|rom[p+3];};
        if(word(table_base)>=first_id)throw std::runtime_error("Native name TEXT IDs overlap ROM records");
        const auto at=table_base+word(table_base+4+0x147F*8);
        if(at>size-12)throw std::runtime_error("Name character template outside ROM");
        std::memcpy(header.data(),rom+at,header.size());
    }
    std::u16string decode(const std::vector<uint16_t>& codes) const {
        std::u16string result;
        for(auto code:codes) {
            auto found=decode_map.find(code);
            if(found==decode_map.end())return {}; // Preserve original UI for unknown defaults.
            result+=found->second;
        }
        return result;
    }
    // Returns a localized UI error key. Output is atomic even on late errors.
    std::string encode(const std::u16string& text,unsigned field,std::vector<uint16_t>& result) const {
        if(field>=limits.size())return "name_invalid";
        if(text.empty())return "name_empty";
        std::vector<uint16_t> codes;
        for(auto c:text) {
            auto found=encode_map.find(c);
            if(found==encode_map.end())return "name_unsupported";
            codes.push_back(found->second);
        }
        if(codes.size()>limits[field])return "name_long";
        if(codes.front()==0 || codes.back()==0)return "name_spaces";
        result=std::move(codes);return {};
    }
    bool descriptor(uint16_t table,uint16_t id,uint32_t& offset,uint32_t& size) const {
        if(table || id<first_id || id>first_id+last_glyph)return false;
        if(!decode_map.count(id-first_id))throw std::runtime_error("Unknown virtual name glyph");
        offset=virtual_base+(id-first_id)*0x400-table_base;size=12;return true;
    }
    bool read(uint32_t source,uint8_t* ram,uint32_t destination,uint32_t size) const {
        if(source<virtual_base || source>=virtual_base+(last_glyph+1)*0x400)return false;
        const auto glyph=(source-virtual_base)/0x400;
        destination&=0x1FFFFFFF;
        if((source-virtual_base)%0x400 || size>0x400 || destination>0x800000 ||
           size>0x800000-destination || !decode_map.count(glyph))
            throw std::runtime_error("Invalid virtual name character read");
        for(unsigned i=0;i<size;++i)ram[(destination+i)^3]=
            i<8?header[i]:i==8?glyph>>8:i==9?glyph:i<12?0xFF:0;
        return true;
    }
};
}
