#pragma once
// Line breaking for running text in RmlUi pages. RmlUi breaks only at ASCII spaces, so a
// Chinese or Japanese sentence is one unbreakable word and runs out of its box (and its
// `word-break` hangs the layout, docs/native/settings-window.md §7). A page that shows
// paragraphs breaks them itself: this splits a paragraph into lines no wider than WIDTH
// as MEASURE (a string's width in the element's font) says. CJK characters may break
// anywhere, Latin text between words; closing punctuation never starts a line and
// opening punctuation never ends one. tests/native_text_wrap.cpp.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace srw64::text_wrap {

inline char32_t decode(std::string_view s,size_t& i) {
    const auto c=uint8_t(s[i]);
    const unsigned n=c<0x80?1:c<0xE0?2:c<0xF0?3:4;
    char32_t value=n==1?c:n==2?c&0x1F:n==3?c&0x0F:c&0x07;
    for(unsigned k=1;k<n && i+k<s.size();++k)value=(value<<6)|(uint8_t(s[i+k])&0x3F);
    i+=n;
    return value;
}
// Characters a line may break before and after: CJK, kana, full-width forms and the like.
// Curly quotes are not: they belong to the word they touch (don’t, “Showdown!”).
inline bool wide(char32_t c) {
    if(c>=0x2018 && c<=0x201F)return false;
    return (c>=0x2E80 && c<=0x9FFF) || (c>=0xAC00 && c<=0xD7AF) || (c>=0xF900 && c<=0xFAFF) || (c>=0xFE30 && c<=0xFE4F) ||
           (c>=0xFF00 && c<=0xFFEF) || (c>=0x2010 && c<=0x206F) || (c>=0x2190 && c<=0x21FF) || (c>=0x2460 && c<=0x27BF);
}
inline bool closing(char32_t c) {
    for(const char32_t p:U"，。、；：！？）」』》〉】〕…‥・ー％,.;:!?)]}%\u201D\u2019")if(c==p)return true;
    return false;
}
inline bool opening(char32_t c) {
    for(const char32_t p:U"（「『《〈【〔([{\u201C\u2018")if(c==p)return true;
    return false;
}

template<class Measure>
std::vector<std::string> lines(std::string_view text,float width,Measure measure) {
    // Tokens: one wide character, or a run of narrow ones up to and including its spaces.
    struct Token {std::string text;char32_t first{},last{};bool space{};};
    std::vector<Token> tokens;
    for(size_t i=0;i<text.size();) {
        const size_t start=i;
        const char32_t c=decode(text,i);
        if(c=='\n'){tokens.push_back({"\n",c,c});continue;}
        if(wide(c)){tokens.push_back({std::string(text.substr(start,i-start)),c,c});continue;}
        char32_t last=c;size_t end=i;
        while(end<text.size()) {
            size_t next=end;const char32_t d=decode(text,next);
            if(d=='\n' || wide(d) || (last==' ' && d!=' '))break;
            last=d;end=next;
        }
        i=end;
        tokens.push_back({std::string(text.substr(start,end-start)),c,last,last==' '});
    }
    std::vector<std::string> out;
    std::string line;
    const auto trimmed=[](std::string s){while(!s.empty() && s.back()==' ')s.pop_back();return s;};
    for(size_t t=0;t<tokens.size();++t) {
        const auto& token=tokens[t];
        if(token.text=="\n"){out.push_back(trimmed(line));line.clear();continue;}
        if(line.empty() && token.text.find_first_not_of(' ')==std::string::npos)continue;   // no leading spaces
        const bool fits=measure(trimmed(line+token.text))<=width;
        // Closing punctuation stays on the line it follows, a little over the edge if need be.
        if(fits || line.empty() || closing(token.first)){line+=token.text;continue;}
        // An opening bracket at the end goes down with the word it opens.
        std::string carry;
        size_t i=line.size();
        while(i>0) {
            size_t start=i-1;
            while(start>0 && (uint8_t(line[start])&0xC0)==0x80)--start;
            size_t at=start;
            if(!opening(decode(line,at)))break;
            i=start;
        }
        if(i>0 && i<line.size()){carry=line.substr(i);line.resize(i);}
        out.push_back(trimmed(line));
        line=carry+token.text;
    }
    if(!line.empty())out.push_back(trimmed(line));
    return out;
}
}
