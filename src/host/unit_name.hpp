#pragma once
#include "guest_memory.hpp"
#include "game_adapter/default_names.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <string>
// The 部隊名 stays マーチウィンド (docs/native/fixed-unit-name.md). The naming choice
// 「それでかまわない／気に入らない」 takes its first answer without being shown, so the
// branch that runs 3D5E (the 部隊名 page) is never reached; if 3D5E runs anyway, it
// does nothing and the name in 8010F698 is left as it is.
namespace srw64::unit_name {
// Text ids of the three 3D44 choices before 3D5E: scene 33 (event 001AB86C),
// scene 62 (001AC6F0) and scenes 46/58 (001BC304).
inline constexpr uint16_t naming_choices[]={24045,24417,32003};
// 8009FA94 handles 3D44 for the script VM context `vm`: +0x1C is the PC past the
// opcode (window slot, option count, text id), +0x26 is 0 on the command's first
// frame, +0x24 is the busy flag the VM waits on, +0xC points at engine+0x994, where
// the answer goes as 0x3DD9 + index. On the first frame of a naming choice this
// answers it the way the original does after A on the first line, before any window
// opens. True: handled, and the original handler must not run.
inline bool answer_choice(uint8_t* ram,uint32_t vm) {
    using namespace guest;
    if(!valid(vm,0x30) || read(ram,vm+0x26,2))return false;
    const uint32_t pc=read(ram,vm+0x1C,4),result=read(ram,vm+0xC,4);
    if(!valid(pc,6) || !valid(result,2) || read(ram,pc+2,2)!=2)return false;
    const auto text=uint16_t(read(ram,pc+4,2));
    if(std::find(std::begin(naming_choices),std::end(naming_choices),text)==std::end(naming_choices))return false;
    write16(ram,result,0x3DD9);write32(ram,vm+0x1C,pc+6);write16(ram,vm+0x24,0);
    return true;
}
// The default 部隊名 in every language, from each catalog's unit_default_name label
// (the Japanese one is the buffer's text; tests/test_unit_name.py checks it against
// the ROM glyphs at 801C6F54). False, and the table unchanged, without that label.
inline bool add_default(names::DefaultNames& table,const std::map<std::string,std::string>& by_locale) {
    const auto label=[](const std::string& text){return !text.empty() && text!="unit_default_name";};
    const auto ja=by_locale.find("ja");
    if(ja==by_locale.end() || !label(ja->second))return false;
    names::DefaultName row{names::Field::Unit,0,ja->second,{}};
    for(const auto& [locale,text]:by_locale)if(locale!="ja" && label(text))row.text[locale]=text;
    table.add(std::move(row));
    return true;
}
// Registers the default 部隊名 with names::default_names() (after the dialogue catalogs
// are loaded) and installs the game hooks; events go to `output`/unit-name-events.jsonl.
void configure(const std::filesystem::path& output);
}
