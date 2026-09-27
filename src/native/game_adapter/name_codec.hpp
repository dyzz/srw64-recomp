#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace srw64::names {
// Original font glyphs as text. Dynamic-name/script opcodes are never glyphs.
class Codec {
    std::map<uint16_t,char16_t> decode_map;
public:
    static constexpr uint16_t last_glyph=0x081E;
    // The original name fields: given name, family name, nickname; their offsets in
    // the editor buffer 801C71E0.
    static constexpr std::array<unsigned,3> limits={7,7,5}, offsets={0,8,16};
    void add(uint16_t glyph,const std::u16string& value) {
        if(glyph>last_glyph || (glyph>=0x124 && glyph<=0x12C) || value.size()!=1 ||
           value[0]<0x20 || (value[0]>=0xD800 && value[0]<=0xDFFF))return;
        decode_map.emplace(glyph,value[0]);
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
};
}
