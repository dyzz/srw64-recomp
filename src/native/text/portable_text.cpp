#include "portable_text.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/uloc.h>
#include <unicode/uscript.h>
#include <unicode/utf16.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace srw64::text {
namespace {
constexpr size_t max_units=65536, max_font_bytes=64*1024*1024;
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
void icu_check(UErrorCode status) {
    if(U_FAILURE(status))throw std::runtime_error(std::string("ICU: ")+u_errorName(status));
}
void validate_utf16(std::u16string_view value) {
    require(value.size()<=max_units,"Portable text exceeds the UTF-16 limit");
    for(size_t i=0;i<value.size();++i) {
        const auto c=value[i];
        if(c>=0xD800 && c<=0xDBFF) {
            require(i+1<value.size() && value[i+1]>=0xDC00 && value[i+1]<=0xDFFF,"Unpaired UTF-16 high surrogate");
            ++i;
        } else require(c<0xDC00 || c>0xDFFF,"Unpaired UTF-16 low surrogate");
    }
}
bool hard_break(char16_t c) {
    return c==u'\n' || c==u'\r' || c==0x85 || c==0x2028 || c==0x2029;
}
std::vector<size_t> boundaries(std::u16string_view value,UBreakIteratorType kind,const char* locale) {
    validate_utf16(value);
    if(value.empty())return {};
    UErrorCode status=U_ZERO_ERROR;
    using Breaks=std::unique_ptr<UBreakIterator,decltype(&ubrk_close)>;
    Breaks iterator(ubrk_open(kind,locale,reinterpret_cast<const UChar*>(value.data()),
                             static_cast<int32_t>(value.size()),&status),ubrk_close);
    icu_check(status);require(bool(iterator),"ICU could not allocate a break iterator");
    std::vector<size_t> result;
    for(auto p=ubrk_first(iterator.get());(p=ubrk_next(iterator.get()))!=UBRK_DONE;)
        result.push_back(static_cast<size_t>(p));
    return result;
}
std::string icu_locale(const std::string& tag) {
    require(!tag.empty() && tag.size()<128 && tag.find('\0')==std::string::npos,"Invalid BCP-47 locale");
    char name[ULOC_FULLNAME_CAPACITY];int32_t parsed=0;UErrorCode status=U_ZERO_ERROR;
    uloc_forLanguageTag(tag.c_str(),name,sizeof(name),&parsed,&status);icu_check(status);
    require(parsed==static_cast<int32_t>(tag.size()),"Invalid BCP-47 locale");
    status=U_ZERO_ERROR;
    uloc_setKeywordValue("lb","strict",name,sizeof(name),&status);icu_check(status);
    return name;
}
using Buffer=std::unique_ptr<hb_buffer_t,decltype(&hb_buffer_destroy)>;
Buffer buffer() {
    Buffer result(hb_buffer_create(),hb_buffer_destroy);
    require(hb_buffer_allocation_successful(result.get()),"HarfBuzz allocation failed");
    hb_buffer_set_cluster_level(result.get(),HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    return result;
}
using Bidi=std::unique_ptr<UBiDi,decltype(&ubidi_close)>;
Bidi bidi() { Bidi result(ubidi_open(),ubidi_close);require(bool(result),"ICU bidi allocation failed");return result; }
struct Glyph { uint32_t id{};size_t font{},start{},end{};double x{},y{},advance{}; };
struct Shaped { double width{};std::vector<Glyph> glyphs; };
struct Grapheme {size_t start{},end{},font{};UScriptCode script=USCRIPT_COMMON;};
bool strong_script(UScriptCode script) {
    return script!=USCRIPT_COMMON && script!=USCRIPT_INHERITED && script!=USCRIPT_UNKNOWN;
}
UScriptCode script_of(std::u16string_view text) {
    for(int32_t i=0;i<static_cast<int32_t>(text.size());) {
        UChar32 cp;U16_NEXT(text.data(),i,static_cast<int32_t>(text.size()),cp);
        UErrorCode status=U_ZERO_ERROR;const auto script=uscript_getScript(cp,&status);icu_check(status);
        if(strong_script(script))return script;
    }
    return USCRIPT_COMMON;
}
std::vector<uint8_t> read_font(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    require(bool(file),"Cannot open explicit portable font file");
    const auto size=file.tellg();
    require(size>0 && size<=static_cast<std::streamoff>(max_font_bytes),"Invalid portable font file size");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
    require(bool(file.read(reinterpret_cast<char*>(bytes.data()),size)),"Could not read complete font file");
    return bytes;
}
struct Face {
    std::vector<uint8_t> bytes;
    std::unique_ptr<std::remove_pointer_t<FT_Face>,decltype(&FT_Done_Face)> ft{nullptr,FT_Done_Face};
    std::unique_ptr<hb_font_t,decltype(&hb_font_destroy)> hb{nullptr,hb_font_destroy};
    Face(FT_Library lib,const FontSource& source):bytes(read_font(source.path)) {
        require(source.face_index>=0 && source.face_index<=65535,"Invalid font face index");
        FT_Face raw=nullptr;
        require(FT_New_Memory_Face(lib,bytes.data(),static_cast<FT_Long>(bytes.size()),source.face_index,&raw)==0,
                "FreeType could not open font face");
        ft.reset(raw);
        require(FT_IS_SCALABLE(raw) && raw->units_per_EM>0,"Portable text requires an outline font");
        require(FT_Select_Charmap(raw,FT_ENCODING_UNICODE)==0,"Font has no Unicode character map");
        require(FT_Set_Char_Size(raw,0,13*64,72,72)==0,"FreeType could not set font size");
        hb.reset(hb_ft_font_create_referenced(raw));
        require(hb.get()!=hb_font_get_empty(),"HarfBuzz could not create font");
        hb_ft_font_set_load_flags(hb.get(),FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP);
    }
    void size(double pixels) {
        require(std::isfinite(pixels) && pixels>0 && pixels<=4096,"Invalid raster font size");
        require(FT_Set_Char_Size(ft.get(),0,static_cast<FT_F26Dot6>(std::llround(pixels*64)),72,72)==0,
                "FreeType could not set font size");
        hb_ft_font_changed(hb.get());
    }
};
}

std::vector<size_t> grapheme_ends(std::u16string_view text) {return boundaries(text,UBRK_CHARACTER,"root");}

struct FontSet::State {
    std::unique_ptr<std::remove_pointer_t<FT_Library>,decltype(&FT_Done_FreeType)> lib{nullptr,FT_Done_FreeType};
    std::vector<std::unique_ptr<Face>> faces;
    mutable std::mutex mutex;
    explicit State(const std::vector<FontSource>& sources) {
        require(!sources.empty() && sources.size()<=8,"Provide one to eight explicit fonts");
        FT_Library raw=nullptr;require(FT_Init_FreeType(&raw)==0,"FreeType initialization failed");lib.reset(raw);
        size_t total=0;
        for(const auto& source:sources) {
            auto face=std::make_unique<Face>(lib.get(),source);total+=face->bytes.size();
            require(total<=2*max_font_bytes,"Portable font set exceeds memory budget");
            faces.push_back(std::move(face));
        }
    }
    size_t select(std::u16string_view text,hb_language_t language) {
        // Shape the entire grapheme rather than testing each nominal codepoint:
        // ccmp may compose an accent even if its standalone glyph is absent.
        for(size_t fi=0;fi<faces.size();++fi) {
            auto b=buffer();hb_buffer_add_utf16(b.get(),reinterpret_cast<const uint16_t*>(text.data()),
                static_cast<int>(text.size()),0,static_cast<int>(text.size()));
            hb_buffer_set_language(b.get(),language);hb_buffer_guess_segment_properties(b.get());
            hb_shape(faces[fi]->hb.get(),b.get(),nullptr,0);
            require(hb_buffer_allocation_successful(b.get()),"HarfBuzz shaping allocation failed");
            unsigned n=0;const auto* info=hb_buffer_get_glyph_infos(b.get(),&n);
            bool supported=true;for(unsigned i=0;i<n;++i)if(info[i].codepoint==0)supported=false;
            if(supported)return fi;
        }
        throw std::runtime_error("No configured font supports an entire grapheme");
    }
    Shaped shape(const std::u16string& text,const std::vector<Grapheme>& gs,
                 const std::vector<size_t>& ends,UBiDi* paragraph,size_t para_start,
                 size_t start,size_t end,hb_language_t language) {
        Shaped result;if(start==end)return result;
        auto line=bidi();UErrorCode status=U_ZERO_ERROR;
        ubidi_setLine(paragraph,static_cast<int32_t>(start-para_start),static_cast<int32_t>(end-para_start),line.get(),&status);
        icu_check(status);const auto runs=ubidi_countRuns(line.get(),&status);icu_check(status);
        for(int32_t ri=0;ri<runs;++ri) {
            int32_t offset=0,length=0;const auto direction=ubidi_getVisualRun(line.get(),ri,&offset,&length);
            const size_t run_start=start+static_cast<size_t>(offset),run_end=run_start+static_cast<size_t>(length);
            auto at=std::lower_bound(gs.begin(),gs.end(),run_start,[](const auto& g,size_t p){return g.start<p;});
            require(at!=gs.end() && at->start==run_start,"Bidi run splits a grapheme");
            struct Piece {size_t start,end,font;UScriptCode script;};std::vector<Piece> pieces;
            while(at!=gs.end() && at->start<run_end) {
                require(at->end<=run_end,"Bidi run splits a grapheme");
                if(!pieces.empty() && pieces.back().font==at->font && pieces.back().script==at->script)
                    pieces.back().end=at->end;
                else pieces.push_back({at->start,at->end,at->font,at->script});
                ++at;
            }
            if(direction==UBIDI_RTL)std::reverse(pieces.begin(),pieces.end());
            for(const auto& piece:pieces) {
                auto b=buffer();
                hb_buffer_add_utf16(b.get(),reinterpret_cast<const uint16_t*>(text.data()+start),
                    static_cast<int>(end-start),static_cast<unsigned>(piece.start-start),static_cast<int>(piece.end-piece.start));
                hb_buffer_set_direction(b.get(),direction==UBIDI_RTL?HB_DIRECTION_RTL:HB_DIRECTION_LTR);
                hb_buffer_set_language(b.get(),language);
                hb_buffer_set_script(b.get(),hb_script_from_string(uscript_getShortName(piece.script),-1));
                hb_buffer_flags_t flags=HB_BUFFER_FLAG_DEFAULT;
                if(piece.start==start)flags=static_cast<hb_buffer_flags_t>(flags|HB_BUFFER_FLAG_BOT);
                if(piece.end==end)flags=static_cast<hb_buffer_flags_t>(flags|HB_BUFFER_FLAG_EOT);
                hb_buffer_set_flags(b.get(),flags);
                hb_shape(faces[piece.font]->hb.get(),b.get(),nullptr,0);
                require(hb_buffer_allocation_successful(b.get()),"HarfBuzz shaping allocation failed");
                unsigned n=0;const auto* info=hb_buffer_get_glyph_infos(b.get(),&n);
                const auto* positions=hb_buffer_get_glyph_positions(b.get(),nullptr);
                std::vector<size_t> starts;starts.reserve(n+1);
                for(unsigned i=0;i<n;++i)starts.push_back(start+info[i].cluster);
                starts.push_back(piece.end);std::sort(starts.begin(),starts.end());
                starts.erase(std::unique(starts.begin(),starts.end()),starts.end());
                for(unsigned i=0;i<n;++i) {
                    require(info[i].codepoint!=0,"Shaping produced a missing glyph");
                    const size_t source=start+info[i].cluster;
                    require(source>=piece.start && source<piece.end,"Invalid HarfBuzz cluster");
                    const auto next=std::upper_bound(starts.begin(),starts.end(),source);
                    require(next!=starts.end(),"Unterminated shaping cluster");
                    const auto first=std::upper_bound(ends.begin(),ends.end(),source);
                    const size_t begin=first==ends.begin()?0:*(first-1);
                    const auto finish=std::lower_bound(ends.begin(),ends.end(),*next);
                    require(finish!=ends.end(),"Invalid shaping cluster end");
                    const double advance=positions[i].x_advance/64.0;
                    require(advance>=0,"Negative horizontal glyph advance is unsupported");
                    result.glyphs.push_back({info[i].codepoint,piece.font,begin,*finish,
                        result.width+positions[i].x_offset/64.0,-positions[i].y_offset/64.0,advance});
                    result.width+=advance;
                }
            }
        }
        return result;
    }
};
struct TextLayout::Data {
    std::shared_ptr<FontSet::State> fonts;
    std::u16string text;
    std::string locale;
    std::vector<size_t> clusters;
    std::vector<TextLine> lines;
    std::vector<std::vector<Glyph>> glyphs;
    std::vector<TextPage> pages;  // only for a paged layout
    double size{},ascent{},pitch{};
    bool paged=false;
};
FontSet::FontSet(const std::vector<FontSource>& sources):state_(std::make_shared<State>(sources)) {}
TextLayout::TextLayout(std::shared_ptr<const Data> data):data_(std::move(data)) {}

// Everything a line needs, shared by the greedy and the paged layout.
struct FontSet::Builder {
    FontSet::State& state;
    TextLayout::Data& out;
    double width;
    hb_language_t language;
    std::vector<size_t> legal;
    std::vector<Grapheme> gs;
    bool halve=false;
    struct Paragraph {size_t a,b,pstart,body_end,pend;Bidi bidi{nullptr,ubidi_close};std::vector<double> prefix;};
    std::vector<Paragraph> paragraphs;
    struct Built {TextLine line;std::vector<Glyph> glyphs;};
    std::map<std::pair<size_t,size_t>,Built> memo;
    Builder(FontSet::State& s,TextLayout::Data& d,double w):state(s),out(d),width(w) {}
    void paginate(const PageStyle& style,size_t per_page);
    void prepare() {
        const auto line_locale=icu_locale(out.locale);
        legal=boundaries(out.text,UBRK_LINE,line_locale.c_str());
        language=hb_language_from_string(out.locale.c_str(),-1);
        for(auto& f:state.faces) {
            f->size(out.size);out.ascent=std::max(out.ascent,double(f->ft->ascender)*out.size/f->ft->units_per_EM);
        }
        std::map<std::u16string,size_t> fallback_cache;
        size_t begin=0;
        for(auto end:out.clusters) {
            const auto part=std::u16string_view(out.text).substr(begin,end-begin);
            const bool newline=hard_break(part.front());
            for(auto c:part)require(!(c<0x20 || c==0x7F) || hard_break(c),"Unsupported control character in rendered text");
            size_t font=0;
            if(!newline) {
                const std::u16string key(part);auto found=fallback_cache.find(key);
                if(found==fallback_cache.end())found=fallback_cache.emplace(key,state.select(part,language)).first;
                font=found->second;
            }
            gs.push_back({begin,end,font,script_of(part)});begin=end;
        }
        // Resolve common/inherited scripts inside each paragraph only. This keeps
        // Latin + CJK and Arabic punctuation in correctly itemized shaping runs.
        for(size_t a=0;a<gs.size();) {
            size_t b=a;while(b<gs.size() && !hard_break(out.text[gs[b].start]))++b;
            UScriptCode previous=USCRIPT_COMMON;
            for(size_t i=a;i<b;++i) {if(strong_script(gs[i].script))previous=gs[i].script;else gs[i].script=previous;}
            UScriptCode next=USCRIPT_COMMON;
            for(size_t i=b;i>a;) {--i;if(strong_script(gs[i].script))next=gs[i].script;else gs[i].script=next;}
            Paragraph p{a,b,gs[a].start,b<gs.size()?gs[b].start:out.text.size(),0,{nullptr,ubidi_close},{}};
            p.pend=b<gs.size()?gs[b].end:p.body_end;
            if(p.pstart!=p.body_end) {
                p.bidi=bidi();UErrorCode status=U_ZERO_ERROR;
                ubidi_setPara(p.bidi.get(),reinterpret_cast<const UChar*>(out.text.data()+p.pstart),
                    static_cast<int32_t>(p.body_end-p.pstart),UBIDI_DEFAULT_LTR,nullptr,&status);icu_check(status);
                const auto whole=state.shape(out.text,gs,out.clusters,p.bidi.get(),p.pstart,p.pstart,p.body_end,language);
                p.prefix.assign(b-a+1,0);
                for(const auto& glyph:whole.glyphs) {
                    const auto it=std::lower_bound(out.clusters.begin()+static_cast<std::ptrdiff_t>(a),
                        out.clusters.begin()+static_cast<std::ptrdiff_t>(b),glyph.end);
                    require(it!=out.clusters.begin()+static_cast<std::ptrdiff_t>(b),"Glyph escapes paragraph");
                    p.prefix[static_cast<size_t>(it-out.clusters.begin())-a+1]+=glyph.advance;
                }
                for(size_t i=1;i<p.prefix.size();++i)p.prefix[i]+=p.prefix[i-1];
            }
            paragraphs.push_back(std::move(p));
            a=b<gs.size()?b+1:b;
        }
    }
    const Paragraph& paragraph(size_t start) const {
        for(const auto& p:paragraphs)if(start>=p.pstart && start<std::max(p.pend,p.pstart+1))return p;
        throw std::runtime_error("Line start outside every paragraph");
    }
    size_t grapheme(size_t start) const {
        return static_cast<size_t>(std::lower_bound(gs.begin(),gs.end(),start,[](const auto& g,size_t p){return g.start<p;})-gs.begin());
    }
    bool halvable(size_t end) const {
        if(!end)return false;
        const auto last=out.text[end-1];
        return halvable_marks.find(last)!=std::u16string_view::npos;
    }
    // The line from start: as much as fits, ending at a legal break (the
    // greedy fill), or ending exactly at limit when one is given.
    const Built& line(size_t start,size_t limit=0) {
        const auto key=std::make_pair(start,limit);
        if(auto found=memo.find(key);found!=memo.end())return found->second;
        const auto& p=paragraph(start);
        Built built;
        if(p.pstart==p.body_end) {built.line={p.pstart,p.pend,0,false,false};return memo.emplace(key,std::move(built)).first->second;}
        const size_t gi=grapheme(start);
        size_t end;bool emergency=false;Shaped shaped;
        if(limit) {
            end=std::min(limit,p.body_end);
            shaped=state.shape(out.text,gs,out.clusters,p.bidi.get(),p.pstart,start,end,language);
        } else {
            auto fit=std::upper_bound(p.prefix.begin()+static_cast<std::ptrdiff_t>(gi-p.a+1),p.prefix.end(),p.prefix[gi-p.a]+width);
            size_t ei=static_cast<size_t>(fit-p.prefix.begin())-1+p.a;
            ei=std::max(ei,gi+1);const size_t estimate=gs[ei-1].end;
            auto lb=std::upper_bound(legal.begin(),legal.end(),estimate);
            end=estimate;emergency=estimate!=p.body_end;
            // ICU's break after an explicit newline includes the newline.
            // A whole paragraph body that fits must not shrink to an earlier
            // legal break merely because that final break lies past body_end.
            if(end!=p.body_end && lb!=legal.begin() && *(lb-1)>start) {end=*(lb-1);emergency=false;}
            shaped=state.shape(out.text,gs,out.clusters,p.bidi.get(),p.pstart,start,end,language);
            // Paragraph advances are only an estimate: reshape at the actual
            // line boundary, then shrink until it fits. Never split a grapheme.
            while(shaped.width>width+1e-6 && end>gs[gi].end) {
                auto prior=std::lower_bound(legal.begin(),legal.end(),end);
                if(prior!=legal.begin() && *(prior-1)>start) {end=*(prior-1);emergency=false;}
                else {auto e=std::lower_bound(out.clusters.begin(),out.clusters.end(),end);end=*(e-1);emergency=true;}
                shaped=state.shape(out.text,gs,out.clusters,p.bidi.get(),p.pstart,start,end,language);
            }
            // A closing mark that only fits at half width may stay on the line
            // together with the character kinsoku would otherwise push down.
            if(halve && end<p.body_end) {
                const auto next=std::upper_bound(legal.begin(),legal.end(),end);
                if(next!=legal.end() && *next<=p.body_end && halvable(*next)) {
                    auto longer=state.shape(out.text,gs,out.clusters,p.bidi.get(),p.pstart,start,*next,language);
                    const double mark=longer.glyphs.empty()?0:longer.glyphs.back().advance;
                    if(longer.width-mark/2<=width+1e-6) {
                        end=*next;emergency=false;shaped=std::move(longer);shaped.width-=mark/2;built.line.halved=true;
                    }
                }
            }
        }
        const size_t consumed=end==p.body_end?p.pend:end;
        const bool halved=built.line.halved;
        built.line={start,consumed,shaped.width,emergency,shaped.width>width+1e-6,halved};
        built.glyphs=std::move(shaped.glyphs);
        return memo.emplace(key,std::move(built)).first->second;
    }
    void greedy() {
        for(size_t s=0;s<out.text.size();) {
            const auto& b=line(s);out.lines.push_back(b.line);out.glyphs.push_back(b.glyphs);s=b.line.end;
        }
    }
};
namespace {
// Page end classes for the ranking.
enum class End {sentence,comma,middle};
End end_class(std::u16string_view text,size_t end,const std::vector<size_t>& sentence_ends) {
    if(end>=text.size() || std::binary_search(sentence_ends.begin(),sentence_ends.end(),end))return End::sentence;
    size_t p=end;
    while(p>0 && (text[p-1]==u' ' || text[p-1]==u'\n' || text[p-1]==u'　'))--p;
    if(!p)return End::sentence;
    if(std::binary_search(sentence_ends.begin(),sentence_ends.end(),p))return End::sentence;
    const auto c=text[p-1];
    if(sentence_end_marks.find(c)!=std::u16string_view::npos)return End::sentence;
    if(comma_marks.find(c)!=std::u16string_view::npos)return End::comma;
    return End::middle;
}
// A last line of one or two characters, or of a single English word.
bool short_line(std::u16string_view text,const std::vector<size_t>& clusters,size_t start,size_t end) {
    while(end>start && (text[end-1]==u' ' || text[end-1]==u'\n'))--end;
    while(start<end && text[start]==u' ')++start;
    if(end<=start)return false;
    const auto body=text.substr(start,end-start);
    const bool latin=std::all_of(body.begin(),body.end(),[](char16_t c){return c<0x2E80;});
    if(latin)return body.find(u' ')==std::u16string_view::npos;
    // Punctuation and quotes do not count: “上！” is one character.
    size_t characters=0;
    for(auto it=std::upper_bound(clusters.begin(),clusters.end(),start);it!=clusters.end() && *it<=end;++it) {
        const size_t from=it==clusters.begin()?0:*(it-1);
        const auto cluster=text.substr(from,*it-from);
        const bool mark=cluster.size()==1 && (sentence_end_marks.find(cluster[0])!=std::u16string_view::npos ||
            comma_marks.find(cluster[0])!=std::u16string_view::npos || opening_marks.find(cluster[0])!=std::u16string_view::npos);
        characters+=!mark;
    }
    return characters<=2;
}
struct Rank {
    size_t pages{},middle{},comma{},orphans{};
    bool operator<(const Rank& o) const {return std::tie(pages,middle,comma,orphans)<std::tie(o.pages,o.middle,o.comma,o.orphans);}
};
}
void FontSet::Builder::paginate(const PageStyle& style,size_t per_page) {
    auto& builder=*this;
    const auto& text=out.text;
    const size_t n=text.size();
    std::vector<size_t> forced;
    for(auto f:style.forced)if(f>0 && f<n)forced.push_back(f);
    std::sort(forced.begin(),forced.end());forced.erase(std::unique(forced.begin(),forced.end()),forced.end());
    auto sentence_ends=style.sentence_ends;std::sort(sentence_ends.begin(),sentence_ends.end());
    // A page from s covers up to per_page greedy lines; it may end at the end
    // of any of them, at a legal break inside the last, but never past a forced
    // page start. best[e] ranks the cheapest way to start a page at e.
    std::map<size_t,std::pair<Rank,size_t>> best;best[0]={Rank{},n};
    std::vector<size_t> starts{0};
    std::map<size_t,std::vector<size_t>> ends_from;
    for(size_t si=0;si<starts.size();++si) {
        const size_t s=starts[si];
        if(s>=n)continue;
        const auto cap_it=std::upper_bound(forced.begin(),forced.end(),s);
        const size_t cap=cap_it==forced.end()?n:*cap_it;
        std::vector<size_t> candidates;
        size_t at=s;
        for(size_t li=0;li<per_page && at<n && at<cap;++li) {
            const auto& b=builder.line(at);
            const size_t line_end=std::min(b.line.end,cap);
            // Legal breaks inside this line are page ends too (only when ranking).
            if(style.rank_breaks)
                for(auto it=std::upper_bound(builder.legal.begin(),builder.legal.end(),at);it!=builder.legal.end() && *it<line_end;++it)
                    candidates.push_back(*it);
            candidates.push_back(line_end);
            at=line_end;
        }
        if(!style.rank_breaks)candidates={candidates.back()};
        std::sort(candidates.begin(),candidates.end());candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
        const Rank base=best.at(s).first;
        for(auto e:candidates) {
            if(e<=s)continue;
            Rank r=base;++r.pages;
            if(e<n) {
                const auto kind=end_class(text,e,sentence_ends);
                r.middle+=kind==End::middle;r.comma+=kind==End::comma;
            }
            // The page's last line: the greedy line holding e-1, cut at e.
            size_t ls=s;while(true){const auto& b=builder.line(ls);if(b.line.end>=e || b.line.end>=n)break;ls=b.line.end;}
            r.orphans+=short_line(text,out.clusters,ls,e);
            // Equal ranks: the page before e starts as late as possible, so
            // earlier pages are the fuller ones.
            auto found=best.find(e);
            if(found==best.end() || r<found->second.first || !(found->second.first<r)) {
                if(found==best.end())starts.push_back(e);
                best[e]={r,s};
            }
        }
        std::sort(starts.begin()+static_cast<std::ptrdiff_t>(si)+1,starts.end());
    }
    // Walk back from the end; then break each page's lines from its start.
    std::vector<size_t> bounds{n};
    for(size_t e=n;e>0;) {const size_t s=best.at(e).second;bounds.push_back(s);e=s;}
    std::reverse(bounds.begin(),bounds.end());
    for(size_t pi=0;pi+1<bounds.size();++pi) {
        const size_t s=bounds[pi],e=bounds[pi+1];
        TextPage page{out.lines.size(),0,s,e};
        for(size_t at=s;at<e;) {
            const auto* b=&builder.line(at);
            if(b->line.end>e)b=&builder.line(at,e);
            out.lines.push_back(b->line);out.glyphs.push_back(b->glyphs);at=b->line.end;++page.line_count;
        }
        out.pages.push_back(page);
    }
}

TextLayout FontSet::layout(std::u16string text,double pixels,double width,std::string locale) const {
    require(bool(state_),"Moved-from font set");
    require(std::isfinite(pixels) && pixels>0 && pixels<=256 && std::isfinite(width) && width>0,"Invalid portable layout dimensions");
    auto result=std::make_shared<TextLayout::Data>();result->fonts=state_;
    result->clusters=grapheme_ends(text);result->text=std::move(text);result->locale=std::move(locale);result->size=pixels;
    result->pitch=pixels*1.22;
    std::lock_guard lock(state_->mutex);
    Builder builder(*state_,*result,width);builder.prepare();builder.greedy();
    return TextLayout(std::move(result));
}
FontSet::PagingTrace FontSet::trace(std::u16string text,double pixels,double width,std::string locale,const PageStyle& style) const {
    const auto paged=layout(text,pixels,width,locale,style);
    auto data=std::make_shared<TextLayout::Data>();data->fonts=state_;
    data->clusters=grapheme_ends(text);data->text=std::move(text);data->locale=std::move(locale);data->size=pixels;
    PagingTrace result;
    std::lock_guard lock(state_->mutex);
    Builder builder(*state_,*data,width);builder.halve=style.halve_line_end;builder.prepare();
    result.clusters=data->clusters;result.legal=builder.legal;
    for(const auto& p:builder.paragraphs) {
        for(size_t i=p.a;i<p.b;++i)result.advances.push_back(p.prefix.empty()?0:p.prefix[i-p.a+1]-p.prefix[i-p.a]);
        if(p.b<builder.gs.size())result.advances.push_back(0);   // the newline
    }
    result.advances.resize(data->clusters.size());
    // Every start the ranking may try: 0, legal breaks, forced page starts and
    // the end of every line from those.
    const size_t n=data->text.size();
    std::set<size_t> starts{0},done;
    for(auto at:builder.legal)if(at<n)starts.insert(at);
    for(auto at:style.forced)if(at<n)starts.insert(at);
    while(!starts.empty()) {
        const size_t s=*starts.begin();starts.erase(starts.begin());
        if(!done.insert(s).second)continue;
        const auto& line=builder.line(s).line;
        result.lines.push_back(line);
        if(line.end<n && !done.count(line.end))starts.insert(line.end);
    }
    std::sort(result.lines.begin(),result.lines.end(),[](const TextLine& a,const TextLine& b){return a.start<b.start;});
    result.lines_per_page=static_cast<size_t>(std::max(1.0,std::floor(style.height/(pixels*style.min_spacing)+1e-6)));
    result.pitch=paged.line_height();
    return result;
}
TextLayout FontSet::layout(std::u16string text,double pixels,double width,std::string locale,const PageStyle& style) const {
    require(bool(state_),"Moved-from font set");
    require(std::isfinite(pixels) && pixels>0 && pixels<=256 && std::isfinite(width) && width>0,"Invalid portable layout dimensions");
    require(std::isfinite(style.height) && style.height>0 && style.min_spacing>0 && style.max_spacing>=style.min_spacing,
            "Invalid page style");
    auto result=std::make_shared<TextLayout::Data>();result->fonts=state_;
    result->clusters=grapheme_ends(text);result->text=std::move(text);result->locale=std::move(locale);result->size=pixels;
    // As many lines as fit at the minimum pitch (with a tolerance for 3.0008),
    // then the leftover height shared out up to the maximum pitch.
    const size_t per_page=static_cast<size_t>(std::max(1.0,std::floor(style.height/(pixels*style.min_spacing)+1e-6)));
    result->pitch=std::max(pixels*style.min_spacing,std::min(pixels*style.max_spacing,style.height/double(per_page)));
    result->paged=true;
    std::lock_guard lock(state_->mutex);
    Builder builder(*state_,*result,width);builder.halve=style.halve_line_end;builder.prepare();
    if(result->text.empty())result->pages.push_back({0,0,0,0});
    else builder.paginate(style,per_page);
    return TextLayout(std::move(result));
}
const std::u16string& TextLayout::text() const {require(bool(data_),"Empty layout handle");return data_->text;}
const std::vector<size_t>& TextLayout::clusters() const {require(bool(data_),"Empty layout handle");return data_->clusters;}
const std::vector<TextLine>& TextLayout::lines() const {require(bool(data_),"Empty layout handle");return data_->lines;}
const std::string& TextLayout::locale() const {require(bool(data_),"Empty layout handle");return data_->locale;}
double TextLayout::font_size() const {require(bool(data_),"Empty layout handle");return data_->size;}
double TextLayout::line_height() const {require(bool(data_),"Empty layout handle");return data_->pitch;}
bool TextLayout::paged() const {require(bool(data_),"Empty layout handle");return data_->paged;}
std::vector<TextPage> TextLayout::pages(double height) const {
    require(bool(data_) && std::isfinite(height) && height>0,"Invalid page height");
    if(data_->paged)return data_->pages;
    const auto& ls=data_->lines;if(ls.empty())return {{0,0,0,0}};
    const auto per=static_cast<size_t>(std::clamp(std::floor(height/line_height()),1.0,double(ls.size())));
    std::vector<TextPage> pages;
    for(size_t i=0;i<ls.size();i+=per) {
        const size_t count=std::min(per,ls.size()-i);pages.push_back({i,count,ls[i].start,ls[i+count-1].end});
    }
    return pages;
}
void TextLayout::draw(presentation::Bgra8Surface& target,const TextDraw& options) const {
    require(bool(data_),"Empty layout handle");target.validate();
    require(std::isfinite(options.scale) && options.scale>0 && options.scale<=32 && data_->size*options.scale<=4096,
            "Invalid text render scale");
    require(std::isfinite(options.x) && std::isfinite(options.y) && std::abs(options.x)<=1000000 && std::abs(options.y)<=1000000,
            "Invalid text render origin");
    require(options.first_line<=data_->lines.size(),"Invalid first text line");
    int64_t clip_left=0,clip_top=0,clip_right=target.width,clip_bottom=target.height;
    if(options.clip) {
        const auto& c=*options.clip;
        require(std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.width) && std::isfinite(c.height)
            && std::abs(c.x)<=1000000 && std::abs(c.y)<=1000000 && c.width>=0 && c.width<=1000000
            && c.height>=0 && c.height<=1000000,"Invalid text clip");
        clip_left=std::max<int64_t>(0,std::ceil(c.x));clip_top=std::max<int64_t>(0,std::ceil(c.y));
        clip_right=std::min<int64_t>(target.width,std::ceil(c.x+c.width));
        clip_bottom=std::min<int64_t>(target.height,std::ceil(c.y+c.height));
    }
    const size_t count=std::min(options.line_count,data_->lines.size()-options.first_line);
    size_t reveal=std::min(options.revealed_utf16,data_->text.size());
    const auto boundary=std::upper_bound(data_->clusters.begin(),data_->clusters.end(),reveal);
    reveal=boundary==data_->clusters.begin()?0:*(boundary-1);
    std::lock_guard lock(data_->fonts->mutex);
    for(auto& face:data_->fonts->faces)face->size(data_->size*options.scale);
    for(size_t li=0;li<count;++li) {
        const double baseline=options.y+(li*line_height()+data_->ascent)*options.scale;
        for(const auto& glyph:data_->glyphs[options.first_line+li]) {
            if(glyph.end>reveal)continue;
            auto face=data_->fonts->faces[glyph.font]->ft.get();
            const double x=options.x+glyph.x*options.scale,y=baseline+glyph.y*options.scale;
            // Retain fractional positioning while letting FreeType rasterize
            // outlines at the requested scale. The transform is reset per glyph.
            const auto ix=static_cast<int64_t>(std::floor(x)),iy=static_cast<int64_t>(std::floor(y));
            FT_Vector delta{static_cast<FT_Pos>(std::llround((x-ix)*64)),static_cast<FT_Pos>(-std::llround((y-iy)*64))};
            FT_Set_Transform(face,nullptr,&delta);
            const auto load_error=FT_Load_Glyph(face,glyph.id,FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP);
            FT_Set_Transform(face,nullptr,nullptr);
            require(load_error==0,"FreeType glyph load failed");
            require(FT_Render_Glyph(face->glyph,FT_RENDER_MODE_NORMAL)==0,"FreeType glyph render failed");
            const auto& bitmap=face->glyph->bitmap;
            if(!bitmap.width || !bitmap.rows)continue;
            require(bitmap.pixel_mode==FT_PIXEL_MODE_GRAY && bitmap.num_grays>1,"Unsupported glyph bitmap format");
            require(bitmap.width<=8192 && bitmap.rows<=8192 && bitmap.buffer!=nullptr,"Invalid glyph bitmap");
            const int64_t pitch=bitmap.pitch;
            require(std::abs(pitch)>=bitmap.width,"Invalid glyph bitmap pitch");
            const int64_t left=ix+face->glyph->bitmap_left,top=iy-face->glyph->bitmap_top;
            const int64_t x0=std::max<int64_t>(0,clip_left-left),x1=std::min<int64_t>(bitmap.width,clip_right-left);
            const int64_t y0=std::max<int64_t>(0,clip_top-top),y1=std::min<int64_t>(bitmap.rows,clip_bottom-top);
            for(auto row=y0;row<y1;++row) {
                const auto offset=pitch>=0?row*pitch:(int64_t(bitmap.rows)-1-row)*(-pitch);
                const auto* source=bitmap.buffer+offset;
                for(auto col=x0;col<x1;++col) {
                    const unsigned coverage=std::min(255U,unsigned(source[col])*255/(bitmap.num_grays-1));
                    const unsigned alpha=(coverage*options.color.a+127)/255,inverse=255-alpha;
                    const unsigned src[]={unsigned(options.color.b),unsigned(options.color.g),unsigned(options.color.r),255U};
                    auto* dst=target.pixels.data()+((top+row)*target.width+left+col)*4;
                    for(unsigned c=0;c<4;++c)dst[c]=static_cast<uint8_t>(std::min(255U,(src[c]*alpha+127)/255+(dst[c]*inverse+127)/255));
                }
            }
        }
    }
}
}
