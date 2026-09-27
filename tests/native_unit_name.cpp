// The 部隊名 stays the default: the naming choice answers itself, 3D5E does nothing.
#include "unit_name.hpp"
#include "game_hooks.hpp"
#include "native_name_entry.hpp"
#include "localization/catalog.hpp"
#include "json/json.hpp"
#include <cassert>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

SRW64GameHooks srw64_game_hooks;
uint64_t srw64_current_vi() {return 4321;}
// No catalogs are loaded here; add_default is checked on its own below.
std::map<std::string,srw64::localization::Snapshot> srw64::localization::registered() {return {};}
std::string srw64::localization::Catalog::ui(const std::string& key) const {return key;}
srw64::names::DefaultNames& srw64::names::default_names() {static DefaultNames table;return table;}
using namespace srw64;

int main() {
    {   // The default 部隊名 shows in each language; the Japanese label is the buffer's text.
        names::DefaultNames table;
        assert(!unit_name::add_default(table,{{"zh-Hans","三月风"}}) && table.empty());
        assert(!unit_name::add_default(table,{{"ja","unit_default_name"}}) && table.empty());
        assert(unit_name::add_default(table,{{"ja","マーチウィンド"},{"zh-Hans","三月风"},{"en","March Wind"},{"fr","unit_default_name"}}));
        assert(table.display(names::Field::Unit,"マーチウィンド","zh-Hans")=="三月风");
        assert(table.display(names::Field::Unit,"マーチウィンド","en")=="March Wind");
        assert(table.display(names::Field::Unit,"マーチウィンド","fr")=="マーチウィンド");   // no label: as stored
        assert(table.display(names::Field::Unit,"ロンド・ベル","en")=="ロンド・ベル");     // renamed in an older build
        assert(table.display(names::Field::Nick,"マーチウィンド","en")=="マーチウィンド");
    }
    std::vector<uint8_t> memory(0x800000,0x5A);auto* ram=memory.data();
    // A script VM context at 80190000, its PC at 80191002 (past the 3D44 opcode) and
    // engine+0x994 at 80192994.
    constexpr uint32_t vm=0x80190000,pc=0x80191002,result=0x80192994;
    auto setup=[&](uint16_t text,uint16_t options=2,uint16_t frame=0) {
        guest::write32(ram,vm+0x1C,pc);guest::write32(ram,vm+0xC,result);
        guest::write16(ram,vm+0x24,1);guest::write16(ram,vm+0x26,frame);
        guest::write16(ram,pc,0);guest::write16(ram,pc+2,options);guest::write16(ram,pc+4,text);
        guest::write16(ram,result,0);
    };
    for(uint16_t text:unit_name::naming_choices) {
        setup(text);
        assert(unit_name::answer_choice(ram,vm));
        assert(guest::read(ram,result,2)==0x3DD9 && guest::read(ram,vm+0x1C,4)==pc+6 && guest::read(ram,vm+0x24,2)==0);
    }
    // Any other choice, a later frame of a naming choice and bad pointers stay the original's.
    for(auto [text,options,frame]:{std::tuple{uint16_t(21815),uint16_t(2),uint16_t(0)},{uint16_t(24045),uint16_t(3),uint16_t(0)},
                                   {uint16_t(24045),uint16_t(2),uint16_t(1)}}) {
        setup(text,options,frame);const auto before=memory;
        assert(!unit_name::answer_choice(ram,vm) && memory==before);
    }
    setup(24045);guest::write32(ram,vm+0x1C,0x00001000);assert(!unit_name::answer_choice(ram,vm));
    setup(24045);guest::write32(ram,vm+0xC,0);assert(!unit_name::answer_choice(ram,vm));
    assert(!unit_name::answer_choice(ram,0x00190000) && !unit_name::answer_choice(ram,0x807FFFF0));

    // The hooks the host installs, and what they log.
    const auto out=std::filesystem::temp_directory_path()/("srw64-unit-name-test-"+std::to_string(getpid()));
    assert(std::filesystem::create_directory(out));
    unit_name::configure(out);
    assert(srw64_game_hooks.choice_step && srw64_game_hooks.unit_name_page);
    setup(21815);assert(!srw64_game_hooks.choice_step(ram,vm));
    setup(24417);assert(srw64_game_hooks.choice_step(ram,vm));
    const auto name_before=memory;
    assert(srw64_game_hooks.unit_name_page(ram) && memory==name_before);   // 8010F698 untouched
    std::ifstream log(out/"unit-name-events.jsonl");std::vector<nlohmann::json> rows;
    for(std::string line;std::getline(log,line);)rows.push_back(nlohmann::json::parse(line));
    assert(rows.size()==2);
    assert(rows[0]["kind"]=="choice-answered" && rows[0]["text"]==24417 && rows[0]["answer"]==1 && rows[0]["vi"]==4321);
    assert(rows[1]["kind"]=="page-skipped" && rows[1]["schema"]=="srw64.unit-name-event.v1");
    std::filesystem::remove_all(out);
}
