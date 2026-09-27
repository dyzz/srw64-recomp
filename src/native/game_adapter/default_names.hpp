#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace srw64::names {
// The player no longer renames anyone (docs/native/default-names.md): the guest's name
// buffers keep the game's defaults in original glyphs, and the host shows a default in
// the reading language. A name entered in an older build or in the original game shows
// as stored, in every language.
enum class Field : uint8_t { Name, Surname, Nick, Unit };
// The protagonists and partners in their ROM record order: アーク, セレイン, ブラッド,
// マナミ, then partners エルリッヒ, リッシュ, カーツ, アイシャ. The selection page counts
// routes from ブラッド.
inline constexpr unsigned people=8;
inline constexpr unsigned person(unsigned route,bool partner){return ((route+2)&3)+(partner?4:0);}
struct DefaultName {
    Field field{};
    unsigned person{};                        // 0-7 as above; 0 for the unit
    std::string ja;                           // the buffer's text, decoded from its glyphs
    std::map<std::string,std::string> text;   // by locale; a missing locale shows `ja`
};
// A full name joins given and family name with the locale's separator.
inline std::string separator(const std::string& locale) {
    if(locale=="zh-Hans")return "·";
    if(locale=="en")return " ";
    return "・";
}
class DefaultNames {
    std::vector<DefaultName> rows;
    // Blanks around a name are padding, not part of it.
    static std::string trimmed(std::string text) {
        static const std::string wide="　";
        for(bool again=true;again;) {
            again=true;
            if(text.ends_with(' '))text.pop_back();
            else if(text.ends_with(wide))text.resize(text.size()-wide.size());
            else if(text.starts_with(' '))text.erase(0,1);
            else if(text.starts_with(wide))text.erase(0,wide.size());
            else again=false;
        }
        return text;
    }
public:
    void add(DefaultName row) {row.ja=trimmed(row.ja);rows.push_back(std::move(row));}
    bool empty() const {return rows.empty();}
    const DefaultName* find(Field field,unsigned person) const {
        for(const auto& row:rows)if(row.field==field && row.person==person)return &row;
        return nullptr;
    }
    // The default a buffer still holds, if it holds one.
    const DefaultName* match(Field field,const std::string& stored) const {
        const auto text=trimmed(stored);
        if(text.empty())return nullptr;
        for(const auto& row:rows)if(row.field==field && row.ja==text)return &row;
        return nullptr;
    }
    std::string default_text(const DefaultName& row,const std::string& locale) const {
        const auto found=row.text.find(locale);
        return found==row.text.end() || found->second.empty() ? row.ja : found->second;
    }
    // Japanese keeps the buffer exactly as drawn, so callers comparing against the
    // guest's own text still see equal strings.
    std::string display(Field field,const std::string& stored,const std::string& locale) const {
        if(locale=="ja")return stored;
        const auto* row=match(field,stored);
        return row?default_text(*row,locale):stored;
    }
    // A full name buffer is given name, the original's middle dot (glyph 0xE7), family name.
    std::string display_full(const std::string& stored,const std::string& locale) const {
        static const std::string dot="・";
        const auto at=stored.find(dot);
        if(locale=="ja" || at==std::string::npos || stored.find(dot,at+dot.size())!=std::string::npos)return stored;
        const auto given=stored.substr(0,at),family=stored.substr(at+dot.size());
        const auto g=display(Field::Name,given,locale),f=display(Field::Surname,family,locale);
        return g==given && f==family ? stored : g+separator(locale)+f;
    }
};
// The eight people from their records: short name 487 + person, also the default
// nickname (801C3744 cuts the given name to 5 codes, アークライト to 3), and full name
// 495 + person, split at the locale's separator. `text(locale, record)` is the catalog
// text, "" when absent. Returns the locales whose full names do not split in two; those
// show the Japanese given and family names.
inline std::vector<std::string> add_people(DefaultNames& table,const std::vector<std::string>& locales,
                                           const std::function<std::string(const std::string&,unsigned)>& text) {
    auto record=[&](const std::string& locale,unsigned id){auto s=text(locale,id);return s.substr(0,s.find("<END>"));};
    auto split=[](const std::string& full,const std::string& sep,std::string& given,std::string& family) {
        const auto at=full.find(sep);
        if(at==std::string::npos || at==0 || full.find(sep,at+sep.size())!=std::string::npos || at+sep.size()==full.size())return false;
        given=full.substr(0,at);family=full.substr(at+sep.size());return true;
    };
    std::vector<std::string> problems;
    auto problem=[&](const std::string& locale){for(const auto& p:problems)if(p==locale)return;problems.push_back(locale);};
    for(unsigned p=0;p<people;++p) {
        DefaultName name{Field::Name,p},family{Field::Surname,p},nick{Field::Nick,p};
        nick.ja=record("ja",487+p);
        if(!split(record("ja",495+p),separator("ja"),name.ja,family.ja) || nick.ja.empty()){problem("ja");continue;}
        for(const auto& locale:locales) {
            if(locale=="ja")continue;
            if(auto short_name=record(locale,487+p);!short_name.empty())nick.text[locale]=short_name;
            std::string g,f;
            if(split(record(locale,495+p),separator(locale),g,f)){name.text[locale]=g;family.text[locale]=f;}
            else problem(locale);
        }
        table.add(std::move(name));table.add(std::move(family));table.add(std::move(nick));
    }
    return problems;
}
}
