#include "dialogue_raster.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace srw64::dialogue;
namespace {
unsigned checks{};
void check(bool condition,const char* message) { ++checks; if(!condition)throw std::runtime_error(message); }
template<class F> void rejects(F f,const char* message) {
    ++checks;try{f();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
struct Scratch {
    std::filesystem::path old=std::filesystem::current_path(),path;
    Scratch() {
        path=std::filesystem::temp_directory_path()/
            ("srw64-raster-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if(!std::filesystem::create_directory(path))throw std::runtime_error("scratch directory exists");
        std::filesystem::current_path(path);
    }
    ~Scratch(){std::error_code ignored;std::filesystem::current_path(old,ignored);std::filesystem::remove_all(path,ignored);}
};
std::shared_ptr<srw64::localization::Catalog> catalog(const std::string& locale,const std::string& manual) {
    using json=nlohmann::json;
    auto result=std::make_shared<srw64::localization::Catalog>();
    result->load({{"schema","srw64.native-dialogue-data.v2"},
        {"config",{{"locale",locale},{"font","Helvetica"}}},{"entries",json::object()},
        {"source_entries",json::object()},{"ui",{{"manual",manual},{"auto","Auto"},{"skip","Skip"},
            {"fast","Fast"},{"font_size","Size"},{"controls","Next / History"},
            {"history_title","History"},{"history_controls","Back"}}}});
    return result;
}
void verify_raster(const Frame& frame,uint32_t width,uint32_t height) {
    const auto result=rasterize_frame(frame,width,height);
    result.image.validate();
    check(result.report.at("renderer")=="FreeType + HarfBuzz + ICU","Wrong game text backend");
    for(const auto& block:result.report.at("blocks"))
        if(block.contains("bounds"))check(block.at("bounds").size()==4,"Invalid scene block");
}
// Patches applied to a kept canvas give exactly the full raster, and nothing is drawn
// outside the reported extent. Returns the patched area.
size_t verify_incremental(IncrementalRaster& raster,srw64::presentation::Bgra8Surface& canvas,const Frame& frame,double picture=320) {
    const auto update=raster.update(frame,canvas.width,canvas.height,picture,"test");
    size_t area=0;
    for(const auto& patch:update.patches) {
        check(patch.pixels.width==patch.rect.width() && patch.pixels.height==patch.rect.height(),"patch size");
        for(uint32_t row=0;row<patch.pixels.height;++row)
            std::copy_n(patch.pixels.pixels.begin()+row*patch.pixels.row_bytes(),patch.pixels.row_bytes(),
                canvas.pixels.begin()+(size_t(patch.rect.top+row)*canvas.width+patch.rect.left)*4);
        area+=size_t(patch.rect.width())*patch.rect.height();
    }
    const auto full=rasterize_frame(frame,canvas.width,canvas.height,picture);
    check(canvas.pixels==full.image.pixels,"incremental raster differs from a full raster");
    for(uint32_t y=0;y<canvas.height;++y)for(uint32_t x=0;x<canvas.width;++x) {
        const bool inside=int(x)>=update.drawn.left && int(x)<update.drawn.right && int(y)>=update.drawn.top && int(y)<update.drawn.bottom;
        if(!inside)check(full.image.pixels[(size_t(y)*canvas.width+x)*4+3]==0,"pixel drawn outside the reported extent");
    }
    return area;
}
void incremental() {
    Frame frame;frame.catalog=catalog("zh-Hans","Manual");frame.font_size=13;
    frame.controls_text="Next / History";frame.history_controls_text="Back";
    auto& top=frame.boxes[0];top.visible=top.active=true;top.event=1;top.x=80;top.y=50;top.speaker=u"阿姆罗";
    {srw64::localization::Scope scope(frame.catalog);
     top.layout=typeset(utf16("日本語、中文 é が。A longer line for pagination, and a second line or more."),13);}
    auto& bottom=frame.boxes[1];bottom=top;bottom.event=2;bottom.y=150;bottom.active=false;bottom.speaker=u"夏亚";
    bottom.revealed=bottom.layout.pages[0].end;
    frame.reading_event=1;
    srw64::presentation::Bgra8Surface canvas(1100,760);
    IncrementalRaster raster;
    const size_t whole=size_t(canvas.width)*canvas.height;
    check(verify_incremental(raster,canvas,frame)==whole,"first update did not cover the canvas");
    // Typewriter reveal: each step repaints a line's band, not the window.
    for(size_t shown=0;shown<=top.layout.pages[0].end;++shown) {
        top.revealed=shown;
        check(verify_incremental(raster,canvas,frame)<whole/8,"a revealed character repainted too much");
    }
    check(verify_incremental(raster,canvas,frame)==0,"an unchanged frame repainted");
    // Automatic reading: the progress bar alone changes.
    frame.auto_read=true;frame.speed=3;frame.advance.visible=true;
    verify_incremental(raster,canvas,frame);
    for(unsigned permille=0;permille<=1000;permille+=37) {
        frame.advance.permille=permille;
        check(verify_incremental(raster,canvas,frame)<whole/100,"the progress bar repainted too much");
    }
    frame.advance.waiting=true;verify_incremental(raster,canvas,frame);
    frame.advance.paused=true;verify_incremental(raster,canvas,frame);
    // Hand-off between speakers, pages, history, sizes, a hidden box and back.
    top.active=false;bottom.active=true;frame.reading_event=2;verify_incremental(raster,canvas,frame);
    for(unsigned page=0;page<top.layout.pages.size();++page){top.page=page;top.revealed=top.layout.pages[page].end;verify_incremental(raster,canvas,frame);}
    // A battle quote laid out again in a wider box: its last character leaves the second
    // line for the first, partway through the reveal and after it.
    {
        srw64::localization::Scope scope(frame.catalog);
        const auto quote=utf16("“竟敢小看老夫百鬼一族……回头可别后悔”");
        const auto narrow=typeset(quote,13,150),wide=typeset(quote,13,260);
        check(narrow.pages[0].lines.size()==2 && wide.pages[0].lines.size()==1,"the quote does not change its wrap");
        for(const size_t shown:{size_t(9),quote.size()})for(const auto* layout:{&narrow,&wide,&narrow}) {
            top.layout=*layout;top.width=layout==&wide?260:150;top.page=0;top.revealed=shown;
            verify_incremental(raster,canvas,frame);
        }
        // The next quote in the same box, from another speaker, shorter, then revealed.
        top.speaker=u"玲";top.event=7;frame.reading_event=7;
        top.layout=typeset(utf16("“燃烧得通红！”"),13,260);
        for(size_t shown=0;shown<=top.layout.pages[0].end;++shown){top.revealed=shown;verify_incremental(raster,canvas,frame);}
        top.speaker=u"阿姆罗";top.event=1;top.width=177;frame.reading_event=2;
        top.layout=typeset(utf16("日本語、中文 é が。A longer line for pagination, and a second line or more."),13);
        verify_incremental(raster,canvas,frame);
    }
    frame.history_open=true;frame.history.emplace_back();frame.history.back().text=u"Refund received";verify_incremental(raster,canvas,frame);
    frame.history_open=false;verify_incremental(raster,canvas,frame);
    frame.bar_scale=1.4;verify_incremental(raster,canvas,frame);
    // The controls bar fading out, gone, and back (settings dialogue_hints).
    for(const double fade:{.5,.1,0.,1.}){frame.bar_fade=fade;verify_incremental(raster,canvas,frame);}
    bottom.visible=false;verify_incremental(raster,canvas,frame);
    bottom.visible=true;frame.font_size=18;verify_incremental(raster,canvas,frame);
    // A transition's black lines take the text away under them, partly, then wholly.
    frame.cover.assign(240,{0.f,0.f});
    for(size_t y=0;y<240;y+=3)frame.cover[y]={0.f,320.f};
    for(size_t y=150;y<200;++y)frame.cover[y]={100.f,250.f};
    verify_incremental(raster,canvas,frame);
    {
        const double scale=std::min(canvas.width/320.0,canvas.height/240.0),oy=(canvas.height-240*scale)/2;
        const int row=int(std::lround(oy+3*scale))+1;  // line 3, inside its band
        check(std::all_of(canvas.pixels.begin()+size_t(row)*canvas.width*4,canvas.pixels.begin()+size_t(row+1)*canvas.width*4,
            [](auto b){return b==0;}),"a covered line kept pixels");
    }
    frame.cover.assign(240,{0.f,320.f});
    verify_incremental(raster,canvas,frame);
    check(std::all_of(canvas.pixels.begin(),canvas.pixels.end(),[](auto b){return b==0;}),"a full cover left pixels");
    frame.cover.clear();
    check(verify_incremental(raster,canvas,frame)>0,"lifting the cover did not repaint");
    // Spans under a pixel wide, left behind after a wipe, cover nothing, not even the
    // margin left of the picture, where a widened battle quote box reaches.
    top.shift_x=-90;verify_incremental(raster,canvas,frame);
    const auto uncovered=canvas.pixels;
    frame.cover.assign(240,{0.f,.75f});
    verify_incremental(raster,canvas,frame);
    check(canvas.pixels==uncovered,"a sub-pixel span covered something");
    frame.cover.clear();
    // A widened picture (384): the idle task's [0, 1) and [319, 320) lines sit at the
    // picture's edges, a pixel and a fifth wide (wide_map::wipe_end), and a quote box past
    // the 4:3 picture keeps its text.
    verify_incremental(raster,canvas,frame,384);
    const auto wide_uncovered=canvas.pixels;
    frame.cover.assign(240,{0.f,1.f});
    for(size_t y=1;y<240;y+=2)frame.cover[y]={319.f,320.f};
    verify_incremental(raster,canvas,frame,384);
    {
        const double scale=std::min(canvas.width/384.0,canvas.height/240.0),edge=(canvas.width-384*scale)/2;
        for(uint32_t y=0;y<canvas.height;++y)for(uint32_t x=uint32_t(edge+2*scale);x<uint32_t(canvas.width-edge-2*scale);++x)
            for(unsigned c=0;c<4;++c){const size_t i=(size_t(y)*canvas.width+x)*4+c;check(canvas.pixels[i]==wide_uncovered[i],"idle wipe lines covered more than the picture's edge columns");}
    }
    frame.cover.clear();top.shift_x=0;verify_incremental(raster,canvas,frame);
    check(std::any_of(canvas.pixels.begin(),canvas.pixels.end(),[](auto b){return b!=0;}),"the picture did not come back");
    frame.boxes={};
    check(verify_incremental(raster,canvas,frame)>0,"hiding everything left the old picture");
    check(std::all_of(canvas.pixels.begin(),canvas.pixels.end(),[](auto b){return b==0;}),"empty frame left pixels");
}
void run() {
    Scratch scratch;
    Frame frame;frame.catalog=catalog("en","Manual");frame.font_size=13;
    frame.controls_text="Next / History";frame.history_controls_text="Back";  // expanded by the host
    verify_raster(frame,320,240);
    const auto empty=rasterize_frame(frame,320,240);
    check(std::all_of(empty.image.pixels.begin(),empty.image.pixels.end(),[](auto b){return b==0;}),"hidden frame is not transparent");
    check(empty.report.at("blocks").empty(),"hidden frame drew controls");
    auto& box=frame.boxes[0];box.visible=box.active=true;box.event=1;box.x=80;box.y=50;box.speaker=u"TEST";
    {
        srw64::localization::Scope scope(frame.catalog);
        box.layout=typeset(u"Hello e\u0301, world.",13);
    }
    box.revealed=box.layout.text.size();frame.reading_event=1;
    verify_raster(frame,320,240);
    const auto full=rasterize_frame(frame,320,240);full.image.validate();
    check(full.report.at("locale")=="en","raster ignored the pinned catalog");
    check(full.report.at("drawable")==nlohmann::json({320,240}),"raster dimensions");
    // Existing bottom control panel: this pixel is away from text and borders.
    const auto i=(size_t(235)*320+5)*4;
    check(full.image.pixels[i+3]>200,"image row orientation/alpha changed");
    check(full.image.pixels[i]>full.image.pixels[i+1] && full.image.pixels[i+1]>full.image.pixels[i+2],"image is not BGRA");
    for(size_t p=0;p<full.image.pixels.size();p+=4)
        check(full.image.pixels[p]<=full.image.pixels[p+3] && full.image.pixels[p+1]<=full.image.pixels[p+3] &&
              full.image.pixels[p+2]<=full.image.pixels[p+3],"non-premultiplied raster pixel");
    check(std::all_of(full.image.pixels.begin(),full.image.pixels.begin()+320*4*5,[](auto b){return b==0;}),"top margin was not transparent");
    const auto other=catalog("ja","Different labels");srw64::localization::activate(other);
    const auto pinned=rasterize_frame(frame,320,240);
    check(pinned.image.pixels==full.image.pixels,"active locale changed an already queued frame");
    check(srw64::localization::snapshot()==other,"raster leaked its scoped locale");
    box.revealed=0;
    verify_raster(frame,320,240);
    const auto hidden_text=rasterize_frame(frame,320,240);
    check(hidden_text.image.pixels!=full.image.pixels,"typewriter reveal did not affect pixels");
    box.revealed=box.layout.text.size();
    verify_raster(frame,640,480);
    const auto scaled=rasterize_frame(frame,640,480);
    check(scaled.image.row_bytes()==2560 && scaled.report.at("drawable")==nlohmann::json({640,480}),"scaled raster extent");
    frame.history_open=true;frame.history.emplace_back();frame.history.back().text=u"Refund received";
    frame.history.back().notice=true;
    verify_raster(frame,320,240);
    const auto history=rasterize_frame(frame,320,240);
    check(std::any_of(history.report.at("blocks").begin(),history.report.at("blocks").end(),
        [](const auto& b){return b.at("role")=="history_notice";}),"history notice lost");
    check(history.image.pixels!=full.image.pixels,"history did not render");
    // Exercise the actual game scene across both panels, inactive handoff, auto-read
    // progress, paginated mixed CJK/Unicode, history notices and letterboxing.
    for(unsigned font:{10U,13U,18U}) {
        frame.font_size=font;
        {srw64::localization::Scope scope(frame.catalog);
         box.layout=typeset(utf16("日本語、中文 é が。A longer line for pagination."),font);}
        frame.boxes[1]=box;frame.boxes[1].y=150;frame.boxes[1].active=false;
        frame.auto_read=true;frame.speed=2;frame.advance={};
        frame.advance.visible=true;frame.advance.permille=567;frame.advance.waiting=true;
        for(bool history_open:{false,true})for(bool active:{false,true}) {
            frame.history_open=history_open;box.active=active;
            for(unsigned page=0;page<box.layout.pages.size();++page) {
                box.page=page;box.revealed=box.layout.pages[page].end;
                verify_raster(frame,800,600);
                verify_raster(frame,1100,760);
            }
        }
    }
    for(const auto& locale:{"zh-Hans","ja","en"}) {
        frame.catalog=catalog(locale,"Manual");
        srw64::localization::Scope scope(frame.catalog);
        box.layout=typeset(utf16("中文、日本語、English é。"),13);
        check(bool(box.layout.shaped),"Game Reader lost shaped glyph ownership");
        box.page=0;box.revealed=box.layout.pages[0].end;
        frame.font_size=13;frame.boxes[1]=box;frame.boxes[1].y=150;
        verify_raster(frame,800,600);
    }
    incremental();
    check(std::filesystem::is_empty(scratch.path),"CPU raster backend wrote diagnostic files");
    rejects([&]{rasterize_frame(frame,0,240);},"zero drawable accepted");
    rejects([&]{rasterize_frame(frame,8193,240);},"oversized drawable accepted");
    rejects([&]{typeset(u"x",0);},"zero font accepted");
    rejects([&]{typeset(u"x",13,0,35);},"zero line width accepted");
    rejects([&]{typeset(u"x",13,177,std::numeric_limits<double>::infinity());},"infinite height accepted");
    rejects([&]{typeset(u"x",13,std::numeric_limits<double>::quiet_NaN(),35);},"NaN width accepted");
    check(typeset(u"x",13,177,std::numeric_limits<double>::max()).pages.size()==1,"large finite height overflow");
}
}
int main(){try{run();std::cout<<checks<<" CPU raster checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
