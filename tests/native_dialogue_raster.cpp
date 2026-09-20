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
void run() {
    Scratch scratch;
    Frame frame;frame.catalog=catalog("en","Manual");frame.font_size=13;
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
