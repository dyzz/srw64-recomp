// Writes or checks tests/data/dialogue-paging-cases.json, the cases the C++
// dialogue layout and the Python reference share (docs/design/dialogue-typesetting.md §6).
//
//   srw64-paging-cases INPUTS CASES          write CASES from INPUTS
//   srw64-paging-cases --check INPUTS CASES  lay INPUTS out again and compare
//
// SRW64_FONT_DIR must hold the packaged fonts (tools/content/prepare_fonts.py).
// Offsets are UTF-16 code units, as in the game.
#include "native_dialogue.hpp"
#include "text/game_fonts.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace srw64::dialogue {
std::shared_ptr<const Frame> presented_frame(uint64_t) {return {};}
}
using namespace srw64;
using json=nlohmann::json;

namespace {
json lines_of(const std::vector<text::TextLine>& lines) {
    json result=json::array();
    for(const auto& line:lines)result.push_back({{"start",line.start},{"end",line.end},{"width",line.width},
        {"halved",line.halved},{"emergency",line.emergency_break}});
    return result;
}
json laid_out(const json& input) {
    const std::string locale=input.at("locale");
    auto catalog=std::make_shared<localization::Catalog>();catalog->locale=locale;
    localization::Scope scope(catalog);
    std::u16string pages;
    const auto& original=input.at("pages");
    for(size_t i=0;i<original.size();++i)pages+=(i?u"\f":u"")+dialogue::utf16(original[i].get<std::string>());
    const auto record=dialogue::joined_record(pages,locale);
    const unsigned setting=input.at("setting");
    const double size=dialogue::body_size(setting);
    const auto fonts=text::game_fonts(locale);
    std::vector<size_t> forced;
    if(input.contains("forced_after_page")) {
        const auto plain=fonts->layout(record.text,size,dialogue::body_width,locale,dialogue::body_style(locale,record.stops));
        forced.push_back(plain.pages(1).at(input.at("forced_after_page").get<size_t>()).start+input.at("forced_shift").get<size_t>());
    }
    const auto style=dialogue::body_style(locale,record.stops,forced);
    const auto layout=fonts->layout(record.text,size,dialogue::body_width,locale,style);
    const auto trace=fonts->trace(record.text,size,dialogue::body_width,locale,style);
    json pages_out=json::array(),halved=json::array();
    for(const auto& page:layout.pages(1))pages_out.push_back({page.start,page.end});
    for(const auto& line:layout.lines())if(line.halved)halved.push_back(line.end);
    return {{"name",input.at("name")},{"record",input.value("record",0)},{"locale",locale},{"setting",setting},
        {"size",size},{"width",dialogue::body_width},{"height",style.height},
        {"min_spacing",style.min_spacing},{"max_spacing",style.max_spacing},{"halve_line_end",style.halve_line_end},
        {"text",dialogue::utf8(record.text)},{"stops",record.stops},{"forced",forced},
        {"clusters",trace.clusters},{"advances",trace.advances},{"legal",trace.legal},
        {"line_table",lines_of(trace.lines)},
        {"expected",{{"pitch",layout.line_height()},{"lines_per_page",trace.lines_per_page},
            {"pages",pages_out},{"lines",lines_of(layout.lines())},{"halved",halved}}}};
}
}

int main(int argc,char** argv) {
    const bool check=argc==4 && std::string(argv[1])=="--check";
    if(argc!=3 && !check) {std::cerr<<"usage: srw64-paging-cases [--check] INPUTS CASES\n";return 2;}
    const char* dir=std::getenv("SRW64_FONT_DIR");
    if(!dir || !*dir) {std::cerr<<"SRW64_FONT_DIR must name the packaged fonts\n";return 2;}
    const auto inputs=json::parse(std::ifstream(argv[check?2:1]));
    json cases=json::array();
    for(const auto& input:inputs.at("cases"))cases.push_back(laid_out(input));
    if(!check) {
        const json doc={{"schema","srw64.dialogue-paging-cases.v1"},
            {"about","Written by tests/dialogue_cpu/paging_cases.cpp from tests/data/dialogue-paging-inputs.json. "
                "Offsets are UTF-16 code units. clusters are grapheme ends, advances each grapheme's width in its "
                "paragraph, legal the ICU strict line breaks, line_table the greedy line from every start the ranking "
                "may try: 0, legal breaks, forced starts and line ends (end includes a trailing newline; halved: the "
                "closing mark at the end takes half width). "
                "The ranking picks page ends from these; expected is what the game lays out."},
            {"fonts",{{"zh-Hans",{"HarmonyOS_Sans_SC.ttf","SRW64Symbols.ttf"}},
                {"en",{"HarmonyOS_Sans_Condensed.ttf","HarmonyOS_Sans_SC.ttf","SRW64Symbols.ttf"}}}},
            {"marks",{{"sentence_end",dialogue::utf8(std::u16string(text::sentence_end_marks))},
                {"comma",dialogue::utf8(std::u16string(text::comma_marks))},
                {"halvable",dialogue::utf8(std::u16string(text::halvable_marks))},
                {"opening",dialogue::utf8(std::u16string(text::opening_marks))}}},
            {"cases",cases}};
        std::ofstream(argv[2])<<doc.dump(1)<<'\n';
        std::cout<<cases.size()<<" paging cases written\n";
        return 0;
    }
    const auto stored=json::parse(std::ifstream(argv[3])).at("cases");
    size_t failures=0;
    for(size_t i=0;i<cases.size();++i) {
        const auto& now=cases[i];
        const auto found=std::find_if(stored.begin(),stored.end(),[&](const json& c){return c.at("name")==now.at("name");});
        if(found==stored.end()) {std::cerr<<now.at("name")<<": not in the stored cases\n";++failures;continue;}
        for(const char* field:{"text","stops","forced","legal","clusters"})
            if((*found).at(field)!=now.at(field)) {std::cerr<<now.at("name")<<": "<<field<<" differs\n";++failures;}
        const auto& a=(*found).at("expected");const auto& b=now.at("expected");
        for(const char* field:{"lines_per_page","pages","halved"})
            if(a.at(field)!=b.at(field)) {std::cerr<<now.at("name")<<": "<<field<<" differs\n";++failures;}
        if(std::abs(a.at("pitch").get<double>()-b.at("pitch").get<double>())>1e-9) {std::cerr<<now.at("name")<<": pitch differs\n";++failures;}
    }
    if(failures) {std::cerr<<failures<<" paging differences\n";return 1;}
    std::cout<<cases.size()<<" paging cases match\n";
    return 0;
}
