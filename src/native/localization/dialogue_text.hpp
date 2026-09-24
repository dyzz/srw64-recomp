#pragma once
// Plain-text dialogue files (docs/guide/dialogue-text.md), read in the game.
// Same rules as src/srw64_native/dialogue_text.py: an entry becomes catalog
// form (<BR> lines, <STOP> pages, <G:XXXX> placeholders, <END>) after its
// pages, options and placeholders are checked against the original record.
#include "localization/catalog.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace srw64::localization::dialogue_text {
// Records before this one are names, labels and messages owned by the term tables.
inline constexpr unsigned first_dialogue_record=5644;
inline constexpr std::pair<unsigned,std::string_view> names[]={{0x124,"HeroNick"},{0x125,"HeroFull"},{0x126,"HeroName"},
    {0x127,"HeroSurname"},{0x128,"PartnerNick"},{0x129,"PartnerFull"},{0x12A,"PartnerName"},{0x12B,"PartnerSurname"},{0x12C,"HeroMech"}};

struct Problem {std::string path;unsigned line{};std::string key,message;};
struct Page {std::vector<std::string> source,target;bool source_options{};std::vector<bool> target_options;};
struct Entry {std::string key,note,path;unsigned line{};std::vector<Page> pages{Page{}};};
// intro: @intro:<resource> text pages (opening and ending pages) in catalog form;
// intro_source: their original ('>') lines in the same form, for Japanese.
struct Result {std::map<std::string,std::string> targets,intro,intro_source;std::vector<Problem> problems;unsigned files{};};

inline std::string hex4(unsigned code){char text[8];std::snprintf(text,sizeof(text),"%04X",code);return text;}
inline std::string name_of(unsigned code) {
    for(const auto& [c,n]:names)if(c==code)return std::string(n);
    return "G:"+hex4(code);
}
inline std::optional<unsigned> code_of(std::string_view name) {
    for(const auto& [c,n]:names)if(n==name)return c;
    if(name.size()==6 && name.starts_with("G:") && std::all_of(name.begin()+2,name.end(),[](char c){return std::isxdigit(static_cast<unsigned char>(c));}))
        return unsigned(std::stoul(std::string(name.substr(2)),nullptr,16));
    return std::nullopt;
}
// {Letters} or {G:XXXX}; any other brace text is literal.
inline bool placeholder_name(std::string_view name) {
    if(name.size()==6 && name.starts_with("G:"))
        return std::all_of(name.begin()+2,name.end(),[](char c){return std::isxdigit(static_cast<unsigned char>(c))!=0;});
    return !name.empty() && std::all_of(name.begin(),name.end(),[](char c){return (c>='A' && c<='Z') || (c>='a' && c<='z');});
}
// Catalog form -> pages of lines, a run of one name glyph as one placeholder.
inline std::vector<std::vector<std::string>> pages_of(std::string_view text) {
    if(!text.ends_with("<END>"))throw std::runtime_error("message must end with <END>");
    text.remove_suffix(5);
    std::vector<std::vector<std::string>> pages(1);
    std::string line;int last=-1;
    for(size_t i=0;i<text.size();) {
        if(text[i]!='<'){line+=text[i++];last=-1;continue;}
        const auto end=text.find('>',i);
        if(end==std::string_view::npos)throw std::runtime_error("unterminated control in the original");
        const auto token=text.substr(i+1,end-i-1);i=end+1;
        if(token=="BR"){pages.back().push_back(std::move(line));line.clear();last=-1;}
        else if(token=="STOP"){pages.back().push_back(std::move(line));line.clear();pages.emplace_back();last=-1;}
        else if(token=="END")throw std::runtime_error("the original has an early <END>");
        else if(token.starts_with("G:")) {
            const unsigned code=unsigned(std::stoul(std::string(token.substr(2)),nullptr,16));
            const bool name=std::any_of(std::begin(names),std::end(names),[&](const auto& n){return n.first==code;});
            if(!name || int(code)!=last)line+="{"+name_of(code)+"}";
            last=name?int(code):-1;
        } else throw std::runtime_error("unsupported control in the original");
    }
    pages.back().push_back(std::move(line));
    return pages;
}
// Placeholder codes on one line, in order; an unknown {Word} is an error.
inline std::vector<unsigned> placeholders(std::string_view line,const std::string& where) {
    std::vector<unsigned> codes;
    for(size_t i=0;(i=line.find('{',i))!=std::string_view::npos;) {
        const auto end=line.find('}',i);
        if(end==std::string_view::npos)break;
        const auto name=line.substr(i+1,end-i-1);
        if(placeholder_name(name)) {
            const auto code=code_of(name);
            if(!code)throw std::runtime_error(where+": unknown placeholder {"+std::string(name)+"}");
            codes.push_back(*code);i=end+1;
        } else ++i;
    }
    return codes;
}
inline std::string catalog_line(std::string_view line) {
    if(line.find('<')!=std::string_view::npos)throw std::runtime_error("translation contains a half-width '<'; use '\xEF\xBC\x9C'");
    std::string out;
    for(size_t i=0;i<line.size();) {
        if(line[i]=='{') {
            const auto end=line.find('}',i);
            if(end!=std::string_view::npos && placeholder_name(line.substr(i+1,end-i-1))) {
                const auto code=code_of(line.substr(i+1,end-i-1));
                if(!code)throw std::runtime_error("unknown placeholder {"+std::string(line.substr(i+1,end-i-1))+"}");
                out+="<G:"+hex4(*code)+">";i=end+1;continue;
            }
        }
        out+=line[i++];
    }
    return out;
}
inline std::string trimmed(std::string_view line) {
    while(!line.empty() && (line.back()==' ' || line.back()=='\t' || line.back()=='\r'))line.remove_suffix(1);
    return std::string(line);
}
// Syntax only; compile() checks an entry against its original text.
inline std::vector<Entry> parse(std::string_view text,const std::string& path,std::vector<Problem>& problems) {
    std::vector<Entry> entries;
    bool open=false;  // the last entry is still being read
    if(text.starts_with("\xEF\xBB\xBF"))text.remove_prefix(3);
    unsigned number=0;
    for(size_t start=0;start<=text.size();) {
        auto stop=text.find('\n',start);if(stop==std::string_view::npos)stop=text.size();
        const auto line=trimmed(text.substr(start,stop-start));start=stop+1;++number;
        if(line.empty() || line[0]=='#')continue;
        if(line[0]=='@') {
            std::string_view rest(line);rest.remove_prefix(1);
            const bool intro=rest.starts_with("intro:");if(intro)rest.remove_prefix(6);
            size_t digits=0;while(digits<rest.size() && digits<6 && rest[digits]>='0' && rest[digits]<='9')++digits;
            if(!digits || digits>5 || (digits<rest.size() && rest[digits]!=' ' && rest[digits]!='\t')) {
                problems.push_back({path,number,"","malformed entry header"});open=false;continue;
            }
            const unsigned record=unsigned(std::stoul(std::string(rest.substr(0,digits))));
            auto note=rest.substr(digits);while(!note.empty() && (note[0]==' ' || note[0]=='\t'))note.remove_prefix(1);
            char key[32];std::snprintf(key,sizeof(key),"base:t00_%05u",record);
            entries.push_back({intro?"intro:"+std::to_string(record):std::string(key),std::string(note),path,number});
            open=true;continue;
        }
        if(!open){problems.push_back({path,number,"","text outside an entry"});continue;}
        auto& entry=entries.back();
        auto& page=entry.pages.back();
        if(line=="---")entry.pages.emplace_back();
        else if(line[0]=='>') {
            std::string content=line.substr(1);if(content.starts_with(" "))content.erase(0,1);
            if(content.starts_with("* ") || content=="*"){page.source_options=true;content.erase(0,std::min<size_t>(2,content.size()));}
            page.source.push_back(std::move(content));
        } else if(line[0]=='\\'){page.target.push_back(line.substr(1));page.target_options.push_back(false);}
        else if(line[0]=='*') {
            std::string content=line.substr(1);if(content.starts_with(" "))content.erase(0,1);
            page.target.push_back(std::move(content));page.target_options.push_back(true);
        } else {page.target.push_back(line);page.target_options.push_back(false);}
    }
    return entries;
}
// The catalog target for one entry; nullopt for an untranslated template.
inline std::optional<std::string> compile(const Entry& entry,const std::string* source) {
    if(std::none_of(entry.pages.begin(),entry.pages.end(),[](const Page& p){return !p.target.empty();}))return std::nullopt;
    const auto join=[](const std::vector<std::string>& lines) {
        std::string out;
        for(size_t i=0;i<lines.size();++i){if(i)out+="<BR>";out+=catalog_line(lines[i]);}
        return out;
    };
    std::string result;
    if(entry.key.starts_with("intro:")) {
        for(size_t n=0;n<entry.pages.size();++n){if(n)result+="<STOP>";result+=join(entry.pages[n].target);}
        return result+"<END>";
    }
    const unsigned record=unsigned(std::stoul(entry.key.substr(entry.key.find('_')+1)));
    if(record<first_dialogue_record)throw std::runtime_error("record "+std::to_string(record)+" is a name or label; edit the term tables instead");
    if(!source)throw std::runtime_error("no such text record");
    const auto original=pages_of(*source);
    if(entry.pages.size()!=original.size())
        throw std::runtime_error(std::to_string(entry.pages.size())+" pages, the original has "+std::to_string(original.size()));
    for(size_t n=0;n<original.size();++n) {
        const auto& page=entry.pages[n];const auto& lines=original[n];
        const auto where="page "+std::to_string(n+1);
        if(!page.source.empty()) {
            std::vector<std::string> expected;for(const auto& l:lines)expected.push_back(trimmed(l));
            if(page.source!=expected)throw std::runtime_error(where+": the '>' lines are not this record's original text");
        }
        if(page.target.empty())throw std::runtime_error(where+" has no translation");
        const auto& options=page.target_options;
        const bool any=std::find(options.begin(),options.end(),true)!=options.end();
        const bool all=std::find(options.begin(),options.end(),false)==options.end();
        if(any && !all)throw std::runtime_error(where+" mixes options (*) and plain lines");
        if((page.source_options || any) && (!all || page.target.size()!=lines.size()))
            throw std::runtime_error(where+": "+std::to_string(page.target.size())+" options, the original has "+std::to_string(lines.size()));
        std::map<unsigned,int> balance;
        for(const auto& l:lines)for(const auto c:placeholders(l,where+" original"))++balance[c];
        for(const auto& l:page.target)for(const auto c:placeholders(l,where))--balance[c];
        std::string missing,extra;
        for(const auto& [code,count]:balance) {
            auto& list=count>0?missing:extra;
            for(int k=0;k<std::abs(count);++k)list+=(list.empty()?"{":", {")+name_of(code)+"}";
        }
        if(!missing.empty() || !extra.empty())
            throw std::runtime_error(where+": placeholders differ from the original"+(missing.empty()?"":"; missing "+missing)+(extra.empty()?"":"; extra "+extra));
        if(n)result+="<STOP>";
        result+=join(page.target);
    }
    return result+"<END>";
}
// All *.txt under the roots; a later root overrides an earlier one entry by entry.
inline Result load(const std::vector<std::filesystem::path>& roots,const Catalog& catalog) {
    namespace fs=std::filesystem;
    Result result;
    for(const auto& root:roots) {
        std::error_code error;
        if(root.empty() || !fs::is_directory(root,error))continue;
        std::vector<fs::path> files;
        for(auto it=fs::recursive_directory_iterator(root,fs::directory_options::skip_permission_denied,error);!error && it!=fs::recursive_directory_iterator();it.increment(error))
            if(it->is_regular_file(error) && it->path().extension()==".txt")files.push_back(it->path());
        std::sort(files.begin(),files.end());
        std::map<std::string,std::pair<std::string,unsigned>> seen;
        for(const auto& file:files) {
            const auto name=fs::relative(file,root,error).generic_string();
            std::ifstream input(file,std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
            if(!input && !input.eof()){result.problems.push_back({name,0,"","unreadable"});continue;}
            ++result.files;
            for(auto& entry:parse(text,name,result.problems)) {
                if(const auto found=seen.find(entry.key);found!=seen.end()) {
                    result.problems.push_back({name,entry.line,entry.key,"also at "+found->second.first+":"+std::to_string(found->second.second)+"; this one is ignored"});
                    continue;
                }
                seen.emplace(entry.key,std::make_pair(name,entry.line));
                try {
                    const auto target=compile(entry,entry.key.starts_with("intro:")?nullptr:catalog.source_text({entry.key}));
                    if(target)(entry.key.starts_with("intro:")?result.intro:result.targets)[entry.key]=*target;
                    if(entry.key.starts_with("intro:") && std::any_of(entry.pages.begin(),entry.pages.end(),[](const Page& p){return !p.source.empty();})) {
                        std::string source;
                        for(size_t n=0;n<entry.pages.size();++n) {
                            if(n)source+="<STOP>";
                            for(size_t i=0;i<entry.pages[n].source.size();++i){if(i)source+="<BR>";source+=entry.pages[n].source[i];}
                        }
                        result.intro_source[entry.key]=source+"<END>";
                    }
                } catch(const std::exception& problem) {
                    result.problems.push_back({name,entry.line,entry.key,problem.what()});
                }
            }
        }
    }
    return result;
}
}
