#pragma once
// A message's script signature, as srw64_native.catalog.signature computes it: Unicode
// text and BR are free; each STOP/END-separated segment owns its G parameters. A
// translation must keep its record's (the importer and the text updates check it).
#include <algorithm>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace srw64::localization {
inline std::vector<std::vector<unsigned>> signature(const std::string& text) {
    if(!text.ends_with("<END>") || text.find_first_of(std::string("\0\r\n\f",4))!=std::string::npos)
        throw std::runtime_error("Invalid message control/terminator");
    static const std::regex token("<(BR|STOP|END|G:[0-9A-Fa-f]{1,4})>");
    std::vector<std::vector<unsigned>> result(1);size_t cursor=0;
    for(auto i=std::sregex_iterator(text.begin(),text.end(),token);i!=std::sregex_iterator();++i) {
        const auto start=size_t(i->position());
        if(text.substr(cursor,start-cursor).find('<')!=std::string::npos)throw std::runtime_error("Unsupported message token");
        const auto value=(*i)[1].str();
        if(value=="STOP" || value=="END"){result.back().push_back(value=="STOP"?0xfffd:0xffff);result.emplace_back();}
        else if(value.starts_with("G:"))result.back().push_back(unsigned(std::stoul(value.substr(2),nullptr,16)));
        cursor=start+size_t(i->length());
    }
    if(text.substr(cursor).find('<')!=std::string::npos)throw std::runtime_error("Unsupported message token");
    for(size_t i=0;i+2<result.size();++i)
        if(std::find(result[i].begin(),result[i].end(),0xffff)!=result[i].end())throw std::runtime_error("Early END in message");
    return result;
}
}
