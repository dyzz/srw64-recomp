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
    double size{},ascent{};
};
FontSet::FontSet(const std::vector<FontSource>& sources):state_(std::make_shared<State>(sources)) {}
TextLayout::TextLayout(std::shared_ptr<const Data> data):data_(std::move(data)) {}

TextLayout FontSet::layout(std::u16string text,unsigned pixels,double width,std::string locale) const {
    require(bool(state_),"Moved-from font set");
    require(pixels>0 && pixels<=256 && std::isfinite(width) && width>0,"Invalid portable layout dimensions");
    auto result=std::make_shared<TextLayout::Data>();result->fonts=state_;
    result->clusters=grapheme_ends(text);result->text=std::move(text);result->locale=std::move(locale);result->size=pixels;
    const auto line_locale=icu_locale(result->locale);
    const auto legal=boundaries(result->text,UBRK_LINE,line_locale.c_str());
    const auto language=hb_language_from_string(result->locale.c_str(),-1);
    std::lock_guard lock(state_->mutex);
    for(auto& f:state_->faces) {
        f->size(pixels);result->ascent=std::max(result->ascent,double(f->ft->ascender)*pixels/f->ft->units_per_EM);
    }
    std::vector<Grapheme> gs;std::map<std::u16string,size_t> fallback_cache;
    size_t begin=0;
    for(auto end:result->clusters) {
        const auto part=std::u16string_view(result->text).substr(begin,end-begin);
        const bool newline=hard_break(part.front());
        for(auto c:part)require(!(c<0x20 || c==0x7F) || hard_break(c),"Unsupported control character in rendered text");
        size_t font=0;
        if(!newline) {
            const std::u16string key(part);auto found=fallback_cache.find(key);
            if(found==fallback_cache.end())found=fallback_cache.emplace(key,state_->select(part,language)).first;
            font=found->second;
        }
        gs.push_back({begin,end,font,script_of(part)});begin=end;
    }
    // Resolve common/inherited scripts inside each paragraph only. This keeps
    // Latin + CJK and Arabic punctuation in correctly itemized shaping runs.
    for(size_t a=0;a<gs.size();) {
        size_t b=a;while(b<gs.size() && !hard_break(result->text[gs[b].start]))++b;
        UScriptCode previous=USCRIPT_COMMON;
        for(size_t i=a;i<b;++i) {if(strong_script(gs[i].script))previous=gs[i].script;else gs[i].script=previous;}
        UScriptCode next=USCRIPT_COMMON;
        for(size_t i=b;i>a;) {--i;if(strong_script(gs[i].script))next=gs[i].script;else gs[i].script=next;}
        const size_t pstart=gs[a].start,body_end=b<gs.size()?gs[b].start:result->text.size();
        const size_t pend=b<gs.size()?gs[b].end:body_end;
        if(pstart==body_end) {
            result->lines.push_back({pstart,pend,0,false,false});result->glyphs.emplace_back();a=b+1;continue;
        }
        auto paragraph=bidi();UErrorCode status=U_ZERO_ERROR;
        ubidi_setPara(paragraph.get(),reinterpret_cast<const UChar*>(result->text.data()+pstart),
            static_cast<int32_t>(body_end-pstart),UBIDI_DEFAULT_LTR,nullptr,&status);icu_check(status);
        const auto whole=state_->shape(result->text,gs,result->clusters,paragraph.get(),pstart,pstart,body_end,language);
        std::vector<double> prefix(b-a+1,0);
        for(const auto& glyph:whole.glyphs) {
            const auto it=std::lower_bound(result->clusters.begin()+static_cast<std::ptrdiff_t>(a),
                result->clusters.begin()+static_cast<std::ptrdiff_t>(b),glyph.end);
            require(it!=result->clusters.begin()+static_cast<std::ptrdiff_t>(b),"Glyph escapes paragraph");
            prefix[static_cast<size_t>(it-result->clusters.begin())-a+1]+=glyph.advance;
        }
        for(size_t i=1;i<prefix.size();++i)prefix[i]+=prefix[i-1];
        for(size_t start=pstart;start<body_end;) {
            const size_t gi=static_cast<size_t>(std::lower_bound(gs.begin()+static_cast<std::ptrdiff_t>(a),
                gs.begin()+static_cast<std::ptrdiff_t>(b),start,[](const auto& g,size_t p){return g.start<p;})-gs.begin());
            auto fit=std::upper_bound(prefix.begin()+static_cast<std::ptrdiff_t>(gi-a+1),prefix.end(),prefix[gi-a]+width);
            size_t ei=static_cast<size_t>(fit-prefix.begin())-1+a;
            ei=std::max(ei,gi+1);const size_t estimate=gs[ei-1].end;
            auto lb=std::upper_bound(legal.begin(),legal.end(),estimate);
            size_t end=estimate;bool emergency=true;
            if(lb!=legal.begin() && *(lb-1)>start) {end=*(lb-1);emergency=false;}
            if(end==body_end)emergency=false;
            auto shaped=state_->shape(result->text,gs,result->clusters,paragraph.get(),pstart,start,end,language);
            // Paragraph advances are only an estimate: reshape at the actual
            // line boundary, then shrink until it fits. Never split a grapheme.
            while(shaped.width>width+1e-6 && end>gs[gi].end) {
                auto prior=std::lower_bound(legal.begin(),legal.end(),end);
                if(prior!=legal.begin() && *(prior-1)>start) {end=*(prior-1);emergency=false;}
                else {auto e=std::lower_bound(result->clusters.begin(),result->clusters.end(),end);end=*(e-1);emergency=true;}
                shaped=state_->shape(result->text,gs,result->clusters,paragraph.get(),pstart,start,end,language);
            }
            const size_t consumed=end==body_end?pend:end;
            result->lines.push_back({start,consumed,shaped.width,emergency,shaped.width>width+1e-6});
            result->glyphs.push_back(std::move(shaped.glyphs));start=consumed;
        }
        a=b<gs.size()?b+1:b;
    }
    return TextLayout(std::move(result));
}
const std::u16string& TextLayout::text() const {require(bool(data_),"Empty layout handle");return data_->text;}
const std::vector<size_t>& TextLayout::clusters() const {require(bool(data_),"Empty layout handle");return data_->clusters;}
const std::vector<TextLine>& TextLayout::lines() const {require(bool(data_),"Empty layout handle");return data_->lines;}
const std::string& TextLayout::locale() const {require(bool(data_),"Empty layout handle");return data_->locale;}
double TextLayout::font_size() const {require(bool(data_),"Empty layout handle");return data_->size;}
double TextLayout::line_height() const {return font_size()*1.22;}
std::vector<TextPage> TextLayout::pages(double height) const {
    require(bool(data_) && std::isfinite(height) && height>0,"Invalid page height");
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
            const int64_t x0=std::max<int64_t>(0,-left),x1=std::min<int64_t>(bitmap.width,int64_t(target.width)-left);
            const int64_t y0=std::max<int64_t>(0,-top),y1=std::min<int64_t>(bitmap.rows,int64_t(target.height)-top);
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
