#include "text/portable_text.hpp"
#include "dialogue_layout_adapter.hpp"
#include <algorithm>
#include <chrono>
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
void run(const std::filesystem::path& cjk,const std::filesystem::path& arabic) {
    check(grapheme_ends(u"e\u0301\r\n\U0001F1F8\U0001F1EC\U0001F44B\U0001F3FD\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466")
        ==std::vector<size_t>({2,4,8,12,23}),"Extended Unicode grapheme boundaries");
    check(grapheme_ends(std::u16string({u'A',0,u'B'}))==std::vector<size_t>({1,2,3}),"Boundary analysis truncated embedded NUL");
    check(grapheme_ends(u"").empty(),"Empty grapheme analysis");
    rejects([]{grapheme_ends(std::u16string(1,0xD800));},"Unpaired high surrogate accepted");
    rejects([]{grapheme_ends(std::u16string(1,0xDC00));},"Unpaired low surrogate accepted");
    rejects([]{grapheme_ends(std::u16string(65537,u'a'));},"Text limit ignored");
    FontSet fonts({{cjk,0},{arabic,0}});
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
    const auto ligature=fonts.layout(u"\u0644\u0627",24,177,"ar");
    partial.revealed_utf16=1;
    check(transparent(render(ligature,partial)),"Arabic ligature disclosed unrevealed source text");
    partial.revealed_utf16=2;check(!transparent(render(ligature,partial)),"Arabic ligature missing");
    const auto rtl=fonts.layout(u"\u0627 \u0628",24,177,"ar");
    partial.revealed_utf16=1;const auto first_rtl=render(rtl,partial);
    size_t xmin=first_rtl.width;
    for(size_t y=0;y<first_rtl.height;++y)for(size_t x=0;x<first_rtl.width;++x)
        if(first_rtl.pixels[(y*first_rtl.width+x)*4+3])xmin=std::min(xmin,x);
    check(xmin>2 && xmin<first_rtl.width,"RTL logical prefix did not appear on the visual right");
    for(double width:{30.,77.,177.}) {
        const auto bidi=fonts.layout(u"ABC \u0627\u0644\u0639\u0631\u0628\u064A\u0629 123 中文 (\u0644\u0627)!",18,width,"ar");
        conservation(bidi,width);check(!transparent(render(bidi)),"Mixed-script fallback failed");
    }
    FontSet cjk_only({{cjk,0}});
    rejects([&]{cjk_only.layout(u"\u0644\u0627",13,177,"ar");},"Missing fallback silently became tofu");
    rejects([&]{fonts.layout(u"\U0010FFFF",13,177,"en");},"Unsupported character silently became tofu");
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
    // Exercise the existing Reader with the real portable layout, no fake
    // metrics, guest state, GPU or platform text APIs.
    using srw64::dialogue::Reader;
    const auto long_layout=fonts.layout(std::u16string(160,u'甲'),18,177,"zh-Hans");
    auto reader_value=srw64::dialogue::reader_layout(long_layout);
    const auto pages=reader_value.pages.size();Reader reader;
    reader.begin(1,100,0,u"甲",reader_value,0);
    for(size_t i=0;i<pages;++i) {
        check(reader.update(Reader::A,20+i*4)==(i+1==pages),"Portable pagination changed guest confirmation count");
        check(!reader.update(Reader::A,21+i*4),"Held confirm advanced a second page");reader.update(0,22+i*4);
    }
    check(reader.pending && reader.history.back().complete,"Portable Reader did not complete history");
    reader.begin(2,101,0,u"乙",srw64::dialogue::reader_layout(fonts.layout(u"e\u0301甲",13,177,"en")),200);
    reader.update(0,202);check(reader.visible==2,"Reader split portable grapheme");
    reader.update(Reader::L,203);check(reader.history_open,"History did not open");
    reader.update(0,300);check(!reader.pending,"History allowed guest progression");
    reader.switch_language(srw64::dialogue::reader_layout(fonts.layout(u"日本語",13,177,"ja")),"ja",301);
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
    rejects([]{FontSet({});},"Empty font set accepted");
    rejects([&]{FontSet({{cjk,-1}});},"Negative face index accepted");
    rejects([&]{FontSet({{cjk,65535}});},"Nonexistent font face accepted");
    {Scratch scratch;const auto invalid=scratch.path/"bad-font";std::ofstream(invalid)<<"not a font";
     rejects([&]{FontSet({{invalid,0}});},"Invalid font data accepted");}
}
}
int main(int argc,char** argv) {
    try {if(argc!=3)throw std::runtime_error("Provide CJK and Arabic outline font paths");
        run(std::filesystem::path(argv[1]),std::filesystem::path(argv[2]));
        std::cout<<checks<<" portable text checks passed (ICU + HarfBuzz + FreeType, real CPU glyph raster)\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}
}
