#pragma once
// Language packages (docs/design/mod-language-packs.md): dialogue/<locale>/ of the enabled
// packages. A folder with language.json adds a reading language over a built-in fallback;
// a built-in locale's folder lays its text over ours. Read once at host start-up: the
// locale list is fixed for the run, so a change takes effect on the next start.
//
//   dialogue/ko/language.json  { "schema": "srw64.language.v1", "locale": "ko", "name": "한국어",
//                                "fallback": "en", "fonts": { "text": ["fonts/A.ttf"], "title": "fonts/B.ttf" } }
//   dialogue/ko/terms.json     { "schema": "srw64.language-terms.v1", "ui": { key: text },
//                                "entries": { "base:t00_00000": "…<END>" or { "text": "…<END>", … } } }
//   dialogue/ko/**/*.txt       the dialogue text files (localization/dialogue_text.hpp)
//   art/ko/scene-images/<file>, art/ko/plates/<resource>.png (5590.png, 5599.6.png)
#include "mod_library.hpp"
#include "localization/catalog.hpp"
#include "localization/signature.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <regex>
#include <set>

namespace srw64::mod_languages {
namespace fs=std::filesystem;
using json=nlohmann::json;

inline constexpr const char* language_schema="srw64.language.v1";
inline constexpr const char* terms_schema="srw64.language-terms.v1";
inline constexpr const char* built_in[]={"ja","zh-Hans","zh-Hant","en","ko"};
// The ones a package's language may stand on: Korean stands on English itself (docs/design/korean.md).
inline constexpr const char* fallbacks[]={"ja","zh-Hans","zh-Hant","en"};
inline constexpr size_t most_languages=16, longest_name=24;
inline constexpr uintmax_t largest_font=uintmax_t(32)<<20, largest_terms=uintmax_t(16)<<20;
// Records before this one are the term tables' (names, labels, messages); the rest is dialogue.
inline constexpr unsigned first_dialogue_record=5644;

inline bool is_built_in(const std::string& locale) {
    return std::find(std::begin(built_in),std::end(built_in),locale)!=std::end(built_in);
}
// A BCP-47 tag as the importer takes it (rom_import.cpp), not one of ours.
inline bool valid_locale(const std::string& locale) {
    static const std::regex tag("[a-z]{2,3}(-[A-Za-z0-9]{2,8})*");
    return locale.size()<=35 && std::regex_match(locale,tag);
}
inline size_t code_points(const std::string& text) {
    return size_t(std::count_if(text.begin(),text.end(),[](char c){return (uint8_t(c)&0xC0)!=0x80;}));
}

// A new reading language, as the topmost package declaring it wrote it.
struct Language {
    std::string locale,name,fallback="en",package;
    fs::path folder;                         // dialogue/<locale>/ of that package
    std::vector<fs::path> text_fonts;        // in front of the fallback's faces
    fs::path title_font;                     // the chapter cards' face, else none
};
// One package's folder for one locale (a new one or a built-in one), bottom first.
struct Layer {
    std::string package;
    fs::path folder,art;                     // dialogue/<locale>/, art/<locale>/ (may not exist)
    std::map<std::string,std::string> ui,entries;
};
struct Found {
    std::vector<Language> languages;                       // in package order, bottom first
    std::map<std::string,std::vector<Layer>> layers;      // locale -> packages' layers, bottom first
    std::vector<std::string> problems;                     // "<id> dialogue/<locale>/…: why"
};

inline json read_json(const fs::path& path,uintmax_t limit,std::string& why) {
    std::error_code error;
    const auto size=fs::file_size(path,error);
    if(error){why="读不到";return nullptr;}
    if(size>limit){why="太大";return nullptr;}
    std::ifstream in(path,std::ios::binary);
    auto value=json::parse(in,nullptr,false);
    if(value.is_discarded()){why="JSON 无效";return nullptr;}
    return value;
}
// A font path in language.json: fonts/<name>.ttf|.otf inside the folder, at most 32 MB.
inline std::string font_problem(const fs::path& folder,const json& value,fs::path& out) {
    if(!value.is_string())return "要是字符串";
    const auto text=value.get<std::string>();
    const auto safe=mods::safe_entry(text);
    if(!safe || !safe->starts_with("fonts/") || safe->find('/',6)!=std::string::npos)return text+"：只能是 fonts/ 里的文件";
    auto ext=fs::path(*safe).extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(ext!=".ttf" && ext!=".otf")return text+"：只能是 .ttf 或 .otf";
    std::error_code error;
    const auto path=folder/fs::path(*safe);
    if(!fs::is_regular_file(path,error))return text+"：没有这个文件";
    if(fs::file_size(path,error)>largest_font)return text+"：超过 32 MB";
    out=path;
    return {};
}
// language.json, strictly. Empty when it is good.
inline std::string parse_language(const json& data,const fs::path& folder,Language& out) {
    if(!data.is_object())return "要是对象";
    static const std::set<std::string> known{"schema","locale","name","fallback","fonts"};
    for(const auto& [key,value]:data.items())if(!known.contains(key))return "未知字段 "+key;
    if(data.value("schema","")!=language_schema)return std::string("schema 要是 ")+language_schema;
    if(!data.contains("locale") || !data["locale"].is_string())return "缺少 locale";
    out.locale=data["locale"].get<std::string>();
    if(out.locale!=folder.filename().string())return "locale 要和文件夹名一样";
    if(!data.contains("name") || !data["name"].is_string())return "缺少 name";
    out.name=data["name"].get<std::string>();
    if(out.name.empty() || code_points(out.name)>longest_name)return "name 要有 1–24 个字";
    if(data.contains("fallback")) {
        if(!data["fallback"].is_string() || std::find(std::begin(fallbacks),std::end(fallbacks),data["fallback"].get<std::string>())==std::end(fallbacks))
            return "fallback 要是 ja、zh-Hans、zh-Hant、en 之一";
        out.fallback=data["fallback"].get<std::string>();
    }
    if(data.contains("fonts")) {
        const auto& fonts=data["fonts"];
        if(!fonts.is_object())return "fonts 要是对象";
        for(const auto& [key,value]:fonts.items()) {
            if(key=="text") {
                if(!value.is_array() || value.size()>4)return "fonts.text 要是数组（最多 4 个）";
                for(const auto& font:value) {
                    fs::path path;
                    if(auto why=font_problem(folder,font,path);!why.empty())return "fonts.text："+why;
                    out.text_fonts.push_back(path);
                }
            } else if(key=="title") {
                if(auto why=font_problem(folder,value,out.title_font);!why.empty())return "fonts.title："+why;
            } else return "fonts 的未知字段 "+key;
        }
    }
    return {};
}
// terms.json. Rows that do not fit are left out with a problem each; a broken file is one
// problem and nothing of it is used. `source` checks entries against the original records.
inline void parse_terms(const json& data,const localization::Catalog& source,Layer& layer,std::vector<std::string>& problems,const std::string& where) {
    if(!data.is_object() || data.value("schema","")!=terms_schema){problems.push_back(where+"schema 要是 "+terms_schema);return;}
    for(const auto& [key,value]:data.items())
        if(key!="schema" && key!="ui" && key!="entries"){problems.push_back(where+"未知字段 "+key);return;}
    const auto& ui=data.value("ui",json::object());
    const auto& entries=data.value("entries",json::object());
    if(!ui.is_object() || !entries.is_object()){problems.push_back(where+"ui 和 entries 要是对象");return;}
    for(const auto& [key,value]:ui.items()) {
        if(!value.is_string() || value.get<std::string>().empty()){problems.push_back(where+"ui."+key+"：要是非空字符串");continue;}
        layer.ui[key]=value.get<std::string>();
    }
    static const std::regex record("base:t00_([0-9]{5})");
    for(const auto& [key,value]:entries.items()) {
        std::smatch m;
        if(!std::regex_match(key,m,record) || std::stoul(m[1].str())>=first_dialogue_record) {
            problems.push_back(where+"entries."+key+"：只能是 base:t00_00000–base:t00_05643，台词写进 .txt");continue;
        }
        const auto& text=value.is_object()?value.value("text",json()):value;
        if(!text.is_string()){problems.push_back(where+"entries."+key+"：要是字符串或带 text 的对象");continue;}
        const auto target=text.get<std::string>();
        const auto* original=source.source_text({key});
        try {
            if(!original){problems.push_back(where+"entries."+key+"：没有这条记录");continue;}
            if(localization::signature(target)!=localization::signature(*original)){problems.push_back(where+"entries."+key+"：控制码和原文不一样");continue;}
        } catch(const std::exception& error) {
            problems.push_back(where+"entries."+key+"："+error.what());continue;
        }
        layer.entries[key]=target;
    }
}
// The {marks} of a label ({A}, {n}): a package's label must keep its original's.
inline std::vector<std::string> marks(const std::string& text) {
    std::vector<std::string> found;
    for(size_t at=text.find('{');at!=std::string::npos;at=text.find('{',at+1))
        if(const auto end=text.find('}',at);end!=std::string::npos)found.push_back(text.substr(at,end-at+1));
    std::sort(found.begin(),found.end());
    return found;
}

// The active packages, bottom first as mods::installed() returns them. `source` holds the
// original records (any registered catalog).
inline Found collect(const std::vector<mods::Package>& packages,const localization::Catalog& source) {
    Found found;
    std::map<std::string,size_t> declared;   // locale -> index in found.languages
    std::error_code error;
    for(const auto& p:packages) {
        if(!p.active)continue;
        const auto root=p.folder/"dialogue";
        if(!fs::is_directory(root,error))continue;
        std::vector<fs::path> folders;
        for(const auto& entry:fs::directory_iterator(root,error))if(entry.is_directory(error))folders.push_back(entry.path());
        std::sort(folders.begin(),folders.end());
        for(const auto& folder:folders) {
            const auto locale=folder.filename().string();
            const auto where=p.id()+" dialogue/"+locale+"/";
            const auto declaration=folder/"language.json";
            const bool new_language=fs::is_regular_file(declaration,error);
            // A declaration of one of ours is left unread (the Korean demo package of 0.5.3
            // declared ko before it was ours): its files lie over ours as any package's do.
            if(is_built_in(locale)) {
                if(new_language && locale!="ko"){found.problems.push_back(where+"language.json：内置语言不用声明");continue;}
            } else if(!new_language) {
                found.problems.push_back(where+"：不是内置语言，要有 language.json");continue;
            } else {
                std::string why;
                const auto data=read_json(declaration,1u<<20,why);
                Language language;
                if(why.empty())why=parse_language(data,folder,language);
                if(!why.empty()){found.problems.push_back(where+"language.json："+why);continue;}
                if(!valid_locale(locale)){found.problems.push_back(where+"language.json：语言代码要像 ko、fr、pt-BR");continue;}
                language.package=p.id();language.folder=folder;
                if(const auto at=declared.find(locale);at!=declared.end())found.languages[at->second]=std::move(language);
                else if(declared.size()>=most_languages){found.problems.push_back(where+"：语言包最多 16 种");continue;}
                else {declared[locale]=found.languages.size();found.languages.push_back(std::move(language));}
            }
            Layer layer;
            layer.package=p.id();layer.folder=folder;layer.art=p.folder/"art"/locale;
            if(const auto terms=folder/"terms.json";fs::is_regular_file(terms,error)) {
                std::string why;
                const auto data=read_json(terms,largest_terms,why);
                if(!why.empty())found.problems.push_back(where+"terms.json："+why);
                else parse_terms(data,source,layer,found.problems,where+"terms.json：");
            }
            found.layers[locale].push_back(std::move(layer));
        }
    }
    return found;
}

// What the host start-up found (native_dialogue.cpp configure), for the readers below.
struct State {
    std::mutex mutex;
    Found found;
};
inline State& state(){static State value;return value;}
inline void set(Found found) {
    for(const auto& p:found.problems)std::fprintf(stderr,"SRW64_MOD_LANGUAGE_PROBLEM %s\n",p.c_str());
    std::string listed;
    for(const auto& l:found.languages)listed+=(listed.empty()?"":",")+l.locale+"<"+l.fallback;
    std::fprintf(stderr,"SRW64_MOD_LANGUAGES %s layers=%zu problems=%zu\n",listed.empty()?"-":listed.c_str(),found.layers.size(),found.problems.size());
    auto& s=state();
    std::lock_guard lock(s.mutex);
    s.found=std::move(found);
}
inline std::vector<Language> languages() {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    return s.found.languages;
}
inline std::vector<Layer> layers(const std::string& locale) {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    const auto found=s.found.layers.find(locale);
    return found==s.found.layers.end()?std::vector<Layer>():found->second;
}
// Every locale a package gives a folder for, new or built-in.
inline std::vector<std::string> locales() {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    std::vector<std::string> out;
    for(const auto& [locale,layers]:s.found.layers)out.push_back(locale);
    return out;
}
inline bool layers_empty() {
    auto& s=state();
    std::lock_guard lock(s.mutex);
    return s.found.layers.empty();
}
// A file a package gives for this locale under art/<locale>/<relative>, topmost first; empty
// when none does.
inline fs::path art(const std::string& locale,const std::string& relative) {
    const auto safe=mods::safe_entry(relative);
    if(!safe)return {};
    std::error_code error;
    for(const auto& layer:[&]{auto l=layers(locale);std::reverse(l.begin(),l.end());return l;}())
        if(const auto path=layer.art/fs::path(*safe);fs::is_regular_file(path,error))return path;
    return {};
}
}
