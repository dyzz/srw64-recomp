#pragma once
#include "mod_library.hpp"
#include "game_adapter/default_names.hpp"
#include <cstdio>
#include <fstream>
#include <mutex>

// Name MODs (docs/design/mod-names-art-data.md §1): dialogue/names.json of the enabled
// packages names the eight original characters and the unit. Shown over the defaults
// (names::DefaultNames) wherever a default is shown; the name buffers and saves keep
// the game's own names, so turning a package off brings ours back.
//
//   { "schema": "srw64.mod-names.v1",
//     "heroes": { "super-male": { "hero": { "zh-Hans": { "name": "龙", "surname": "天野", "nickname": "龙" } } } },
//     "unit": { "zh-Hans": "疾风", "en": "Gale" } }
namespace srw64::mod_names {
namespace fs=std::filesystem;
using json=nlohmann::json;

inline constexpr const char* schema="srw64.mod-names.v1";
inline constexpr const char* file="names.json";
// The routes in the defaults' person order: アーク, セレイン, ブラッド, マナミ; partners +4.
inline constexpr std::array<std::string_view,4> routes={"real-male","real-female","super-male","super-female"};
// A name longer than this is a mistake, not a name: the whole file is left out.
inline constexpr size_t longest=16, longest_unit=24;
// The original's buffers, in cells (wide glyphs): longer names get a warning only.
inline constexpr unsigned cells_name=7, cells_nick=5, cells_unit=10;

inline std::optional<std::u32string> code_points(const std::string& text) {
    std::u32string out;
    for(size_t i=0;i<text.size();) {
        const auto c=uint8_t(text[i]);
        const unsigned n=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;
        if(!n || i+n>text.size())return std::nullopt;
        char32_t cp=n==1?c:c&(0x7F>>n);
        for(unsigned k=1;k<n;++k) {
            const auto t=uint8_t(text[i+k]);
            if((t&0xC0)!=0x80)return std::nullopt;
            cp=cp<<6|(t&0x3F);
        }
        out+=cp;i+=n;
    }
    return out;
}
// Display width in half cells: Latin letters and digits one, everything else two.
inline unsigned width(const std::string& text) {
    unsigned w=0;
    if(const auto points=code_points(text))for(const auto cp:*points)w+=cp<0x2E80 || (cp>=0xFF61 && cp<=0xFF9F)?1:2;
    return w;
}

struct Parsed {
    names::ModNames names;
    std::vector<std::string> warnings;
};
// The widest default of a field over every person and locale (`widest` by field), in
// half cells: a package name past it, or past the original's cells, is worth a warning.
using Widest=std::map<names::Field,unsigned>;
inline Widest widest_defaults(const names::DefaultNames& table) {
    Widest out;
    for(const auto field:{names::Field::Name,names::Field::Surname,names::Field::Nick,names::Field::Unit})
        for(unsigned p=0;p<names::people;++p)
            if(const auto* row=table.find(field,p)) {
                out[field]=std::max(out[field],width(row->ja));
                for(const auto& [locale,text]:row->text)out[field]=std::max(out[field],width(text));
            }
    return out;
}

// One file, all or nothing; an error names the entry and the field. `locales` are the
// reading languages a package may name.
inline std::string parse(const json& document,const std::vector<std::string>& locales,const Widest& widest,Parsed& out) {
    if(!document.is_object())return "顶层要是对象";
    Parsed parsed;
    const auto name=[&](const json& value,const std::string& where,size_t limit,names::Field field,unsigned cells,std::string& text)->std::string {
        if(!value.is_string())return where+"：要是字符串";
        text=value.get<std::string>();
        const auto points=code_points(text);
        if(!points)return where+"：不是有效的 UTF-8";
        if(points->empty() || text.find_first_not_of(" \t")==std::string::npos)return where+"：不能为空";
        if(text.front()==' ' || text.back()==' ')return where+"：首尾不能有空格";
        for(const auto cp:*points)
            if(cp<0x20 || cp==0x7F || cp=='<' || cp=='>' || cp=='{' || cp=='}' || cp==0x3000)return where+"：不能有控制字符、全角空格或 < > { }";
        if(points->size()>limit)return where+"：太长（最多 "+std::to_string(limit)+" 个字符）";
        const unsigned w=width(text);
        const auto found=widest.find(field);
        if(w>cells*2)parsed.warnings.push_back(where+"：超过原版 "+std::to_string(cells)+" 格，界面可能放不下");
        else if(found!=widest.end() && found->second && w>found->second)parsed.warnings.push_back(where+"：比现有最长的译名还宽，界面可能放不下");
        return {};
    };
    const auto known=[&](const std::string& locale){return std::find(locales.begin(),locales.end(),locale)!=locales.end();};
    std::string locale_list;
    for(const auto& l:locales)locale_list+=(locale_list.empty()?"":"、")+l;
    for(const auto& [key,value]:document.items()) {
        if(key=="schema") {
            if(value!=schema)return std::string("schema：要是 ")+schema;
        } else if(key=="heroes") {
            if(!value.is_object())return "heroes：要是对象";
            for(const auto& [route,people]:value.items()) {
                const auto at=std::find(routes.begin(),routes.end(),route);
                if(at==routes.end())return "heroes."+route+"：未知路线（real-male、real-female、super-male、super-female）";
                if(!people.is_object())return "heroes."+route+"：要是对象";
                for(const auto& [role,by_locale]:people.items()) {
                    if(role!="hero" && role!="partner")return "heroes."+route+"."+role+"：要是 hero 或 partner";
                    if(!by_locale.is_object())return "heroes."+route+"."+role+"：要是对象";
                    const unsigned person=unsigned(at-routes.begin())+(role=="partner"?4:0);
                    for(const auto& [locale,fields]:by_locale.items()) {
                        const auto where="heroes."+route+"."+role+"."+locale;
                        if(!known(locale))return where+"：未知语言（"+locale_list+"）";
                        if(!fields.is_object())return where+"：要是对象";
                        if(!fields.contains("name"))return where+"：缺少 name";
                        std::string given,family,nick;
                        for(const auto& [field,text]:fields.items()) {
                            std::string why;
                            if(field=="name")why=name(text,where+".name",longest,names::Field::Name,cells_name,given);
                            else if(field=="surname")why=name(text,where+".surname",longest,names::Field::Surname,cells_name,family);
                            else if(field=="nickname")why=name(text,where+".nickname",longest,names::Field::Nick,cells_nick,nick);
                            else return where+"：未知字段 "+field+"（name、surname、nickname）";
                            if(!why.empty())return why;
                        }
                        // Without a nickname the given name is the nickname, as the original's
                        // default nicknames are (801C3744).
                        parsed.names[{names::Field::Name,person,locale}]=given;
                        if(!family.empty())parsed.names[{names::Field::Surname,person,locale}]=family;
                        parsed.names[{names::Field::Nick,person,locale}]=nick.empty()?given:nick;
                    }
                }
            }
        } else if(key=="unit") {
            if(!value.is_object())return "unit：要是对象";
            for(const auto& [locale,text]:value.items()) {
                if(!known(locale))return "unit."+locale+"：未知语言（"+locale_list+"）";
                std::string shown;
                if(auto why=name(text,"unit."+locale,longest_unit,names::Field::Unit,cells_unit,shown);!why.empty())return why;
                parsed.names[{names::Field::Unit,0,locale}]=shown;
            }
        } else return "未知字段 "+key+"（schema、heroes、unit）";
    }
    for(auto& [k,v]:parsed.names)out.names[k]=std::move(v);
    for(auto& w:parsed.warnings)out.warnings.push_back(std::move(w));
    return {};
}

struct Collected {
    names::ModNames names;
    std::vector<std::string> warnings,problems;   // "<id> dialogue/names.json：…"; a problem leaves the file out
};
// The active packages, bottom first as mods::installed() returns them: the upper wins.
inline Collected collect(const std::vector<mods::Package>& packages,const std::vector<std::string>& locales,const Widest& widest) {
    Collected c;
    std::error_code error;
    for(const auto& p:packages) {
        const auto path=p.folder/"dialogue"/file;
        if(!p.active || !fs::is_regular_file(path,error))continue;
        const std::string where=p.id()+" dialogue/"+file+"：";
        std::ifstream in(path,std::ios::binary);
        const auto document=json::parse(in,nullptr,false);
        if(document.is_discarded()){c.problems.push_back(where+"JSON 无效");continue;}
        Parsed parsed;
        if(auto why=parse(document,locales,widest,parsed);!why.empty()){c.problems.push_back(where+why);continue;}
        for(auto& [k,v]:parsed.names)c.names[k]=std::move(v);
        for(auto& w:parsed.warnings)c.warnings.push_back(where+w);
    }
    return c;
}

struct State {
    std::mutex mutex;
    std::vector<std::string> locales;
    fs::path output;
    std::optional<unsigned> revision;
    mods::Version game{-1,-1,-1};
};
inline State& state(){static State value;return value;}

// Host start-up, after the dialogue catalogs are loaded: the reading languages a
// package may name, and where mod-names.json goes.
inline void configure(std::vector<std::string> locales,const fs::path& output) {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    s.locales=std::move(locales);s.output=output;s.revision.reset();
}
// From names::default_names() on any thread: rebuilt when the packages change or once
// the front end has told the game its version.
inline void refresh(names::DefaultNames& table) {
    auto& s=state();
    const unsigned now=mods::revision();
    const auto game=mods::game_version();
    std::lock_guard lock(s.mutex);
    if(s.locales.empty() || (s.revision==now && game==s.game))return;
    s.revision=now;s.game=game;
    const auto found=collect(mods::installed(game),s.locales,widest_defaults(table));
    table.set_mod_names(found.names);
    for(const auto& w:found.warnings)std::fprintf(stderr,"SRW64_MOD_NAMES_WARNING %s\n",w.c_str());
    for(const auto& p:found.problems)std::fprintf(stderr,"SRW64_MOD_NAMES_PROBLEM %s\n",p.c_str());
    std::fprintf(stderr,"SRW64_MOD_NAMES names=%zu warnings=%zu problems=%zu\n",found.names.size(),found.warnings.size(),found.problems.size());
    if(!s.output.empty()) {
        static constexpr const char* fields[]={"name","surname","nickname","unit"};
        json rows=json::array();
        for(const auto& [key,text]:found.names)
            rows.push_back({{"field",fields[unsigned(std::get<0>(key))]},{"person",std::get<1>(key)},{"locale",std::get<2>(key)},{"text",text}});
        std::ofstream(s.output/"mod-names.json")<<json({{"schema","srw64.mod-names-applied.v1"},{"names",rows},
            {"warnings",found.warnings},{"problems",found.problems}}).dump(2)<<'\n';
    }
}
}
