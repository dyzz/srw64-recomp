#include "text/portable_text.hpp"
#include "dialogue_layout_adapter.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace srw64::text;
using srw64::presentation::Bgra8Surface;
namespace {
size_t checks=0;
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f,const char* message) {
    ++checks;try{f();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
bool transparent(const Bgra8Surface& image) {
    return std::all_of(image.pixels.begin(),image.pixels.end(),[](auto b){return b==0;});
}
Bgra8Surface render(const TextLayout& layout,TextDraw options={}) {
    Bgra8Surface result(480,200);layout.draw(result,options);return result;
}
void conservation(const TextLayout& layout,double width) {
    size_t offset=0;
    for(const auto& line:layout.lines()) {
        check(line.start==offset && line.end>line.start,"Line ranges lose or repeat source text");
        check(std::binary_search(layout.clusters().begin(),layout.clusters().end(),line.end),"Line splits a grapheme");
        check(line.width>=0 && std::isfinite(line.width),"Invalid measured width");
        if(!line.overflow)check(line.width<=width+1e-6,"Line overflow was not reported");
        offset=line.end;
    }
    check(offset==layout.text().size(),"Final line does not consume the entire source");
    size_t line=0,end=0;
    for(const auto& page:layout.pages(35)) {
        check(page.first_line==line && page.start==end,"Page gap or overlap");
        line+=page.line_count;end=page.end;
    }
    check(line==layout.lines().size() && end==layout.text().size(),"Page partition lost text");
}
struct Scratch {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("srw64-portable-text-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch(){if(!std::filesystem::create_directory(path))throw std::runtime_error("Scratch exists");}
    ~Scratch(){std::error_code error;std::filesystem::remove_all(path,error);}
};
void paged(const FontSet& fonts);
void run(const std::filesystem::path& cjk) {
    check(grapheme_ends(u"e\u0301\r\n\U0001F1F8\U0001F1EC\U0001F44B\U0001F3FD\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466")
        ==std::vector<size_t>({2,4,8,12,23}),"Extended Unicode grapheme boundaries");
    check(grapheme_ends(std::u16string({u'A',0,u'B'}))==std::vector<size_t>({1,2,3}),"Boundary analysis truncated embedded NUL");
    check(grapheme_ends(u"").empty(),"Empty grapheme analysis");
    rejects([]{grapheme_ends(std::u16string(1,0xD800));},"Unpaired high surrogate accepted");
    rejects([]{grapheme_ends(std::u16string(1,0xDC00));},"Unpaired low surrogate accepted");
    rejects([]{grapheme_ends(std::u16string(65537,u'a'));},"Text limit ignored");
    FontSet fonts({{cjk,0}});
    const auto empty=fonts.layout(u"",13,177,"zh-Hans");
    check(empty.lines().empty() && empty.pages(35).size()==1 && transparent(render(empty)),"Empty layout contract");
    const std::u16string mixed=u"中文和日本語：「これは長い会話です。」 English office e\u0301, 123.\r\n第二段：不要丢字。";
    for(const auto locale:{"zh-Hans","ja","en"})for(unsigned size:{10U,13U,18U})for(double width:{1.,43.,77.,177.,400.}) {
        const auto shaped=fonts.layout(mixed,size,width,locale);conservation(shaped,width);
        check(shaped.text()==mixed && shaped.locale()==locale,"Layout changed source text or locale");
        check(!transparent(render(shaped)),"Real glyph raster is empty");
    }
    const auto newline=fonts.layout(u"a\r\nb\n\nc\r",13,1000,"en");
    check(newline.lines().size()==4,"Explicit/empty line handling");conservation(newline,1000);
    const auto paragraph=fonts.layout(u"大小姐，茶泡好了。\n第二行。",13,177,"zh-Hans");
    check(paragraph.lines().size()==2 && paragraph.lines()[0].end==10,
          "A fitting paragraph wrapped before its explicit newline");
    const auto punctuation=fonts.layout(u"甲乙（丙丁）甲乙。丙丁「甲乙」丙丁、甲乙。",13,60,"ja");
    for(const auto& line:punctuation.lines())if(!line.emergency_break && line.end<punctuation.text().size())
        check(std::u16string_view(u"）、。」』！？").find(punctuation.text()[line.end])==std::u16string_view::npos,"CJK closing punctuation starts a normal line");
    const auto narrow=fonts.layout(u"甲",18,0.1,"zh-Hans");
    check(narrow.lines().size()==1 && narrow.lines()[0].overflow,"Oversized indivisible grapheme was not reported");
    conservation(fonts.layout(std::u16string(2000,u'甲'),18,177,"zh-Hans"),177);
    conservation(fonts.layout(std::u16string(4000,u'a'),13,1000000,"en"),1000000);
    const auto composed=fonts.layout(u"e\u0301",24,177,"en");
    TextDraw partial;partial.revealed_utf16=1;
    check(transparent(render(composed,partial)),"Reveal split a combining sequence");
    partial.revealed_utf16=2;check(!transparent(render(composed,partial)),"Completed grapheme was not rendered");
    rejects([&]{fonts.layout(u"\U0010FFFF",13,177,"en");},"Unsupported character silently became tofu");
    TextDraw clipped;clipped.clip=TextClip{8,4,11,9};
    const auto clip_image=render(composed,clipped);
    bool clip_ink=false;
    for(size_t y=0;y<clip_image.height;++y)for(size_t x=0;x<clip_image.width;++x) {
        const auto alpha=clip_image.pixels[(y*clip_image.width+x)*4+3];
        if(x<8 || x>=19 || y<4 || y>=13)check(alpha==0,"Text escaped its scene clip");
        else clip_ink|=alpha!=0;
    }
    check(clip_ink,"Text clip removed all in-bounds glyph pixels");
    rejects([&]{TextDraw d;d.clip=TextClip{0,0,-1,4};render(composed,d);},"Negative clip width accepted");
    TextDraw color;color.color={200,100,50,128};
    const auto colored=render(composed,color);bool ink=false;
    for(size_t i=0;i<colored.pixels.size();i+=4) {
        const auto* p=colored.pixels.data()+i;
        check(p[0]<=p[1] && p[1]<=p[2] && p[2]<=p[3] && p[3]<=128,"Wrong BGRA, alpha, or premultiplication");
        ink|=p[3]!=0;
    }
    check(ink,"Colored outline rendered no pixels");
    auto blended=colored;composed.draw(blended,color);
    for(size_t i=0;i<blended.pixels.size();i+=4)
        check(blended.pixels[i+3]>=colored.pixels[i+3] && blended.pixels[i+2]<=blended.pixels[i+3],"Source-over alpha failed");
    for(double scale:{0.5,1.,1.5,2.}) {
        TextDraw options;options.scale=scale;options.x=-3.25;options.y=-2.5;
        for(auto dimensions:{std::pair{1U,1U},{3U,5U},{321U,241U},{800U,600U},{1100U,760U}}) {
            Bgra8Surface target(dimensions.first,dimensions.second);composed.draw(target,options);target.validate();
            check(target.pixels.size()==size_t(dimensions.first)*dimensions.second*4,"Clipped/scaled raster extent changed");
        }
    }
    const auto retained=[&] {
        Scratch scratch;const auto path=scratch.path/std::filesystem::path(u8"字体副本.otf");
        std::filesystem::copy_file(cjk,path);
        FontSet temporary({{path,0}});auto layout=temporary.layout(u"旧帧 e\u0301",18,177,"zh-Hans");
        std::filesystem::remove(path);return layout;
    }();
    const auto reference=render(retained);
    check(!transparent(reference),"Layout lost font bytes after FontSet/file destruction");
    fonts.layout(u"Other locale",10,50,"en");
    check(reference.pixels==render(retained).pixels,"Later font/locale state changed old layout");
    const auto shared=fonts.layout(u"线程安全 e\u0301",13,177,"zh-Hans");const auto expected=render(shared).pixels;
    std::vector<std::future<bool>> tasks;
    for(unsigned i=0;i<6;++i)tasks.push_back(std::async(std::launch::async,[&fonts,&shared,&expected]{
        for(unsigned j=0;j<8;++j) {
            fonts.layout(u"別のサイズ",10+j,80,"ja");
            if(render(shared).pixels!=expected)return false;
        }return true;
    }));
    for(auto& task:tasks)check(task.get(),"Concurrent shaping changed retained raster");
    // Exercise the unchanged Reader with real portable layout and glyph ranges.
    using srw64::dialogue::Reader;
    const auto long_layout=fonts.layout(std::u16string(160,u'甲'),18,177,"zh-Hans");
    auto reader_value=srw64::dialogue::reader_layout(long_layout);
    const auto pages=reader_value.pages.size();Reader reader;
    reader.begin(1,100,u"甲",reader_value,0);
    for(size_t i=0;i<pages;++i) {
        check(reader.update(Reader::A,20+i*4)==(i+1==pages),"Portable pagination changed guest confirmation count");
        check(!reader.update(Reader::A,21+i*4),"Held confirm advanced a second page");reader.update(0,22+i*4);
    }
    check(reader.pending && reader.history.back().complete,"Portable Reader did not complete history");
    reader.begin(2,101,u"乙",srw64::dialogue::reader_layout(fonts.layout(u"e\u0301甲",13,177,"en")),200);
    reader.update(0,202);check(reader.visible==2,"Reader split portable grapheme");
    reader.update(Reader::L,203);check(reader.history_open,"History did not open");
    reader.update(0,300);check(!reader.pending,"History allowed guest progression");
    reader.switch_language(srw64::dialogue::reader_layout(fonts.layout(u"日本語",13,177,"ja")),{},"ja",301);
    check(reader.page==0 && reader.visible==0 && !reader.pending,"Language switch failed");
    reader.relayout(srw64::dialogue::reader_layout(fonts.layout(u"日本語",18,60,"ja")));
    check(reader.page<reader.layout.pages.size(),"Portable accessibility relayout failed");
    check(empty.pages(std::numeric_limits<double>::max()).size()==1,"Large finite page height overflowed");
    rejects([&]{fonts.layout(u"x",0,177);},"Zero font size accepted");
    rejects([&]{fonts.layout(u"x",257,177);},"Excessive font size accepted");
    rejects([&]{fonts.layout(u"x",13,0);},"Zero width accepted");
    rejects([&]{fonts.layout(u"x",13,std::numeric_limits<double>::infinity());},"Infinite width accepted");
    rejects([&]{fonts.layout(u"x",13,std::numeric_limits<double>::quiet_NaN());},"NaN width accepted");
    rejects([&]{fonts.layout(u"x",13,177,"not a locale");},"Malformed locale accepted");
    rejects([&]{fonts.layout(u"a\tb",13,177);},"Unsupported tab silently rendered");
    rejects([&]{fonts.layout(std::u16string({u'a',0,u'b'}),13,177);},"NUL silently rendered");
    rejects([&]{composed.pages(0);},"Zero page height accepted");
    rejects([&]{composed.pages(std::numeric_limits<double>::infinity());},"Infinite page height accepted");
    rejects([&]{TextDraw o;o.scale=std::numeric_limits<double>::quiet_NaN();render(composed,o);},"NaN scale accepted");
    rejects([&]{TextDraw o;o.x=std::numeric_limits<double>::infinity();render(composed,o);},"Infinite origin accepted");
    rejects([&]{TextDraw o;o.first_line=1000;render(composed,o);},"Out-of-range line accepted");
    rejects([]{TextLayout{}.lines();},"Invalid layout handle accepted");
    rejects([]{FontSet(std::vector<FontSource>{});},"Empty font set accepted");
    rejects([&]{FontSet({{cjk,-1}});},"Negative face index accepted");
    rejects([&]{FontSet({{cjk,65535}});},"Nonexistent font face accepted");
    {Scratch scratch;const auto invalid=scratch.path/"bad-font";std::ofstream(invalid)<<"not a font";
     rejects([&]{FontSet({{invalid,0}});},"Invalid font data accepted");}
    paged(fonts);
}
// FontSource::weight: a variable font's named instance drives both shaping and the raster.
size_t ink(const Bgra8Surface& image) {
    size_t total=0;for(size_t i=3;i<image.pixels.size();i+=4)total+=image.pixels[i];return total;
}
void weights(const std::filesystem::path& cjk,const std::filesystem::path& variable) {
    rejects([&]{FontSet({{cjk,0,700}});},"Weight accepted for a font without variations");
    const std::u16string sample=u"Bold Weight 粗体字重";
    const auto regular=FontSet({{variable,0}}).layout(sample,20,1000,"en");
    const auto normal=FontSet({{variable,0,400}}).layout(sample,20,1000,"en");
    const auto bold=FontSet({{variable,0,700}}).layout(sample,20,1000,"en");
    check(normal.lines()[0].width==regular.lines()[0].width,"Weight 400 is not the default instance");
    check(bold.lines()[0].width>regular.lines()[0].width,"Bold instance did not change the advances");
    check(ink(render(bold))>ink(render(regular))*6/5,"Bold instance did not change the raster");
}
// Dialogue page layout (docs/design/dialogue-typesetting.md).
size_t greedy_pages(const FontSet& fonts,const std::u16string& text,double size,double width,size_t per_page) {
    const auto lines=fonts.layout(text,size,width,"zh-Hans").lines().size();
    return (lines+per_page-1)/per_page;
}
void partition(const TextLayout& layout) {
    size_t line=0,end=0;
    for(const auto& page:layout.pages(1)) {
        check(page.first_line==line && page.start==end && page.end>page.start,"Paged layout gap or overlap");
        for(size_t i=0;i<page.line_count;++i) {
            const auto& l=layout.lines()[page.first_line+i];
            check(l.start==(i?layout.lines()[page.first_line+i-1].end:page.start),"Page lines are not contiguous");
        }
        check(layout.lines()[page.first_line+page.line_count-1].end==page.end,"Page does not end with its last line");
        line+=page.line_count;end=page.end;
    }
    check(line==layout.lines().size() && end==layout.text().size(),"Paged layout lost text");
}
void paged(const FontSet& fonts) {
    PageStyle style;style.height=43;style.min_spacing=1.08;style.max_spacing=1.22;style.rank_breaks=true;
    // Pitch: three lines at 1.08, the rest shared out (43 / 3), capped at 1.22.
    const auto three=fonts.layout(u"甲",13,177,"zh-Hans",style);
    check(three.paged() && std::abs(three.line_height()-43.0/3)<1e-9,"Adaptive pitch at 13");
    check(std::abs(fonts.layout(u"甲",10,177,"zh-Hans",style).line_height()-12.2)<1e-9,"Pitch capped at 1.22");
    PageStyle edge=style;edge.height=3*12.75*1.15;edge.min_spacing=1.15;
    check(std::abs(fonts.layout(u"a",12.75,177,"en",edge).line_height()-12.75*1.15)<1e-6,"Exact fit keeps three lines");
    const std::u16string story=u"但大半殖民卫星被毁，地上也遭受了巨大损失。人口锐减，驱动地球圈运转的国家也大多失去了力量。"
        u"于是联邦政府接管了一切，一个巨大的统一国家诞生了。可是，那并不是和平的开始，而是新的战争的序曲。";
    for(double size:{10.0,13.0,16.0,18.0}) {
        const auto ranked=fonts.layout(story,size,177,"zh-Hans",style);partition(ranked);
        const size_t per=static_cast<size_t>(std::floor(43/(size*1.08)+1e-6));
        check(ranked.pages(1).size()==greedy_pages(fonts,story,size,177,per),"Ranking changes the page count");
        for(const auto& page:ranked.pages(1))check(page.line_count<=per,"Page holds too many lines");
    }
    // Page ends: at a sentence end when that costs no extra page.
    const auto ranked=fonts.layout(story,13,177,"zh-Hans",style);
    for(size_t i=0;i+1<ranked.pages(1).size();++i) {
        const auto end=ranked.pages(1)[i].end;const auto c=story[end-1];
        check(sentence_end_marks.find(c)!=std::u16string_view::npos || comma_marks.find(c)!=std::u16string_view::npos,
              "A ranked page ends mid-sentence though a sentence end was available");
    }
    // Forced page starts stay page starts; the pages before them are unchanged.
    PageStyle forced=style;forced.forced={ranked.pages(1)[1].start+3};
    const auto kept=fonts.layout(story,13,177,"zh-Hans",forced);partition(kept);
    check(std::any_of(kept.pages(1).begin(),kept.pages(1).end(),[&](const TextPage& p){return p.start==forced.forced[0];}),
          "Forced page start was not a page start");
    // Extra sentence ends (the original's page breaks) count like a full stop.
    PageStyle stops=style;stops.sentence_ends={10};
    partition(fonts.layout(story,13,177,"zh-Hans",stops));
    // Half-width closing marks: 。 after a full line stays on it only when halving is on.
    const auto per_line=fonts.layout(std::u16string(40,u'甲'),13,177,"zh-Hans").lines().front().end;
    const std::u16string full=std::u16string(per_line,u'甲')+u"。乙";
    PageStyle halve=style;halve.halve_line_end=true;
    const auto halved=fonts.layout(full,13,177,"zh-Hans",halve);partition(halved);
    const auto plain=fonts.layout(full,13,177,"zh-Hans",style);
    check(halved.lines().front().end==per_line+1 && halved.lines().front().halved,"Line-end 。 was not halved");
    check(plain.lines().front().end<per_line+1 && !plain.lines().front().halved,"Halving applied without the option");
    check(halved.lines().front().width<=177+1e-6,"A halved line overflows");
    // Without ranking the paged layout is the greedy fill, page by page.
    PageStyle greedy=style;greedy.rank_breaks=false;
    const auto filled=fonts.layout(story,13,177,"zh-Hans",greedy);partition(filled);
    const auto plain_lines=fonts.layout(story,13,177,"zh-Hans").lines();
    check(filled.lines().size()==plain_lines.size(),"Greedy paged layout breaks lines differently");
    for(size_t i=0;i<plain_lines.size();++i)check(filled.lines()[i].end==plain_lines[i].end,"Greedy paged line differs");
    check(fonts.layout(u"",13,177,"zh-Hans",style).pages(1).size()==1,"Empty paged layout");
    // Speed: a long record lays out well within a frame budget.
    const auto begin=std::chrono::steady_clock::now();
    for(int i=0;i<10;++i)fonts.layout(story+story+story,13,177,"zh-Hans",halve);
    const auto each=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/10;
    std::cout<<"paged layout of "<<(story.size()*3)<<" UTF-16 units: "<<each<<" ms\n";
    check(each<50,"Paged layout is too slow");
}
}
int main(int argc,char** argv) {
    try {if(argc!=2 && argc!=3)throw std::runtime_error("Provide a CJK outline font path (and optionally a variable font)");
        run(std::filesystem::path(argv[1]));
        if(argc==3)weights(argv[1],argv[2]);
        std::cout<<checks<<" portable text checks passed (ICU + HarfBuzz + FreeType, real CPU glyph raster)\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}
}
