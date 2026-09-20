// Portable Chinese/Japanese/English dialogue scene. No OS or GPU APIs.
#include "dialogue_raster.hpp"
#include "dialogue_layout_adapter.hpp"
#include "text/game_fonts.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace srw64::dialogue {
namespace {
using json=nlohmann::json;
Layout layout_text(const std::u16string& value,double size,double width,double height) {
    return reader_layout(text::game_fonts(localization::catalog().locale)->layout(
        value,size,width,localization::catalog().locale),height);
}
struct Painter {
    presentation::Bgra8Surface& image;
    double scale,ox,oy;
    json blocks=json::array();
    void blend(int x,int y,double r,double g,double b,double alpha) {
        if(x<0 || y<0 || x>=int(image.width) || y>=int(image.height))return;
        const unsigned a=unsigned(std::lround(std::clamp(alpha,0.0,1.0)*255)),inv=255-a;
        auto* p=image.pixels.data()+(size_t(y)*image.width+x)*4;
        const double color[]={b,g,r,1};
        for(unsigned c=0;c<4;++c)p[c]=uint8_t(std::min(255U,
            unsigned(std::lround(color[c]*a))+(p[c]*inv+127)/255));
    }
    void fill(double x,double y,double w,double h,double r,double g,double b,double alpha=1) {
        const double left=ox+x*scale,top=oy+y*scale,right=left+w*scale,bottom=top+h*scale;
        for(int py=std::max(0,int(std::floor(top)));py<std::min(int(image.height),int(std::ceil(bottom)));++py)
            for(int px=std::max(0,int(std::floor(left)));px<std::min(int(image.width),int(std::ceil(right)));++px) {
                const double cover=std::max(0.0,std::min(right,px+1.0)-std::max(left,double(px)))*
                    std::max(0.0,std::min(bottom,py+1.0)-std::max(top,double(py)));
                blend(px,py,r,g,b,alpha*cover);
            }
    }
    void stroke(double ax,double ay,double bx,double by,double width) {
        ax=ox+ax*scale;ay=oy+ay*scale;bx=ox+bx*scale;by=oy+by*scale;
        const double radius=width*scale/2,dx=bx-ax,dy=by-ay,length=dx*dx+dy*dy;
        for(int y=std::max(0,int(std::floor(std::min(ay,by)-radius-1)));y<std::min(int(image.height),int(std::ceil(std::max(ay,by)+radius+1)));++y)
            for(int x=std::max(0,int(std::floor(std::min(ax,bx)-radius-1)));x<std::min(int(image.width),int(std::ceil(std::max(ax,bx)+radius+1)));++x) {
                const double t=length?std::clamp(((x+.5-ax)*dx+(y+.5-ay)*dy)/length,0.0,1.0):0;
                const double distance=std::hypot(x+.5-ax-t*dx,y+.5-ay-t*dy);
                blend(x,y,.41,.83,1,std::clamp(radius+.5-distance,0.0,1.0));
            }
    }
    void panel(double x,double y,double w,double h) {
        fill(x,y,w,h,.015,.035,.065,.96);
        fill(x-.5,y-.5,w+1,1,.41,.75,1);fill(x-.5,y+h-.5,w+1,1,.41,.75,1);
        fill(x-.5,y+.5,1,h-1,.41,.75,1);fill(x+w-.5,y+.5,1,h-1,.41,.75,1);
    }
    void text(const Layout& value,double x,double y,double width,double h,
              const std::vector<Line>& lines,size_t revealed,double r,double g,double b,const char* role) {
        if(!value.shaped)throw std::runtime_error("Dialogue frame has no pinned portable layout");
        const auto& shaped=*value.shaped;
        const double px=ox+x*scale,top=oy+y*scale;
        json drawn=json::array();
        size_t row=0;
        for(const auto& line:lines) {
            const auto found=std::find_if(shaped.lines().begin(),shaped.lines().end(),
                [&](const auto& l){return l.start==line.start && l.end==line.end;});
            if(found==shaped.lines().end()) {
                if(line.start==line.end)continue;
                throw std::runtime_error("Reader line does not match its pinned glyph layout");
            }
            text::TextDraw draw;draw.x=px;draw.y=top+row*shaped.line_height()*scale;draw.scale=scale;
            draw.first_line=size_t(found-shaped.lines().begin());draw.line_count=1;draw.revealed_utf16=revealed;
            draw.clip=text::TextClip{px,top,width*scale,h*scale};
            draw.color={uint8_t(std::lround(r*255)),uint8_t(std::lround(g*255)),uint8_t(std::lround(b*255)),255};
            shaped.draw(image,draw);
            drawn.push_back({{"start",line.start},{"end",line.end},
                {"text",utf8(value.text.substr(line.start,line.end-line.start))},
                {"top",draw.y}});
            ++row;
        }
        blocks.push_back({{"role",role},{"font_pixels",shaped.font_size()*scale},{"revealed_utf16",revealed},
            {"bounds",{px,top,width*scale,h*scale}},{"lines",drawn}});
    }
    void label(const std::u16string& value,double x,double y,double size,double width,double r,double g,double b,const char* role) {
        const auto shaped=layout_text(value,size,1000000,size*1.4);
        text(shaped,x,y,width,size*1.4,shaped.pages.front().lines,value.size(),r,g,b,role);
    }
    void speaker_marker(const Box& box) {
        const double x=box.x-5,y=box.y-22,w=187,h=59;
        for(int right:{0,1})for(int bottom:{0,1}) {
            const double cx=x+right*w,cy=y+bottom*h,dx=right?-1:1,dy=bottom?-1:1;
            stroke(cx+dx*13,cy,cx+dx*3,cy,1.3);
            stroke(cx+dx*3,cy,cx,cy+dy*3,1.3);
            stroke(cx,cy+dy*3,cx,cy+dy*11,1.3);
        }
        for(int i=0;i<6;++i)fill(box.x-4,box.y-13+i,i<3?i+1:6-i,1,.41,.83,1);
        blocks.push_back({{"role","speaker_focus"},{"slot",box.slot},{"event",box.event},
            {"bounds",{ox+x*scale,oy+y*scale,w*scale,h*scale}}});
    }
    void progress(const Box& box,const AdvanceProgress& value) {
        const double x=box.x+116,y=box.y-20,w=61,h=2.5;
        fill(x-1,y-1,w+2,h+2,.025,.055,.085,.96);
        fill(x,y,w,h,.14,.23,.28);
        // The fill follows the actual reading timer: cyan while revealing,
        // amber while waiting for automatic advance, grey during history.
        const double r=value.paused?.48:value.waiting?1:.41;
        const double g=value.paused?.58:value.waiting?.64:.83;
        const double b=value.paused?.65:value.waiting?.23:1;
        fill(x,y,w*value.permille/1000.0,h,r,g,b);
        if(value.paused) {
            fill(x+w-5,y-.5,1,3.5,.82,.88,.93);
            fill(x+w-2.5,y-.5,1,3.5,.82,.88,.93);
        }
        blocks.push_back({{"role","advance_progress"},{"slot",box.slot},{"event",box.event},
            {"permille",value.permille},{"waiting",value.waiting},{"paused",value.paused},
            {"bounds",{ox+x*scale,oy+y*scale,w*scale,h*scale}}});
    }
};

}

RasterizedFrame rasterize_frame(const Frame& frame,uint32_t width,uint32_t height) {
    localization::Scope locale(frame.catalog);
    RasterizedFrame result{presentation::Bgra8Surface(width,height),{}};
    if(!frame.font_size || frame.font_size>256)throw std::runtime_error("Invalid native UI font size");
    const double scale=std::min(width/320.0,height/240.0);
    const double ox=(width-320*scale)/2,oy=(height-240*scale)/2;
    Painter paint{result.image,scale,ox,oy};
    const auto* focus=frame.focused_box();
    for(const auto& box:frame.boxes) {
        if(!box.visible || box.layout.pages.empty())continue;
        const auto& page=box.layout.pages.at(box.page);
        // The fixed header has room for 13 logical pixels; body size is
        // independent, so larger accessibility text cannot overlap the name.
        const bool focused=&box==focus;
        if(focused)paint.speaker_marker(box);
        paint.label(box.speaker,box.x,box.y-16,std::min(frame.font_size,13U),177,
            focused?105./255:.43,focused?191./255:.57,focused?1:.65,"speaker");
        const double grey=box.active?1:123./255;
        paint.text(box.layout,box.x,box.y,177,35,page.lines,box.revealed,grey,grey,grey,"body");
        if(focused) {
            if(box.layout.pages.size()>1)paint.label(utf16(std::to_string(box.page+1)+"/"+
                std::to_string(box.layout.pages.size())),box.x+92,box.y-23,5,22,.65,.84,.96,"page_number");
            if(frame.advance.visible && box.event==frame.reading_event)paint.progress(box,frame.advance);
        }
    }
    // A guest confirmation can leave both panels inactive before the next
    // speaker/STOP fragment arrives. Keep the shared controls visible for the
    // visible dialogue, independently of which panel currently owns reading.
    const auto visible=[](const Box& box){return box.visible && !box.layout.pages.empty();};
    if(std::any_of(frame.boxes.begin(),frame.boxes.end(),visible)) {
        const auto& catalog=localization::catalog();
        const auto status=frame.skipping?catalog.ui("skip"):frame.fast?catalog.ui("fast"):frame.auto_read?
            catalog.ui("auto")+" "+std::to_string(frame.speed)+"/"+std::to_string(Reader::max_speed):catalog.ui("manual");
        // The map marker and portraits occupy the middle of this scene.
        // Keep the reading controls on the otherwise unused bottom edge.
        paint.panel(3,229,314,10);
        paint.label(utf16(status),6,230,6,31,.75,.87,1,"status");
        constexpr double speed_pitch=28.0/Reader::max_speed;
        for(unsigned i=0;i<Reader::max_speed;++i) {
            const bool lit=frame.auto_read && i<frame.speed;
            paint.fill(38+i*speed_pitch,232,speed_pitch-1,4.5,lit?1:.17,lit?.64:.26,lit?.23:.32);
        }
        paint.blocks.push_back({{"role","auto_speed"},{"level",frame.auto_read?frame.speed:0},{"maximum",Reader::max_speed}});
        paint.label(utf16(catalog.ui("font_size")+" "+std::to_string(frame.font_size)),68,230,6,24,.75,.87,1,"font_size");
        paint.label(utf16(catalog.ui("controls")),92,231,5.1,222,.75,.8,.86,"controls");
    }
    if(frame.history_open) {
        paint.panel(16,18,288,202);
        paint.label(utf16(localization::catalog().ui("history_title")),23,23,12,210,.41,.75,1,"history_title");
        paint.label(utf16(localization::catalog().ui("history_controls")),151,27,6.2,147,.75,.8,.86,"history_controls");
        struct HistoryLine {Layout layout;Line range;bool speaker;bool warm_name{};bool notice{};};
        std::vector<HistoryLine> lines;
        for(const auto& entry:frame.history) {
            if(entry.text.empty())continue;
            // A host notice has no speaker; it keeps the accent colour of the UI frames.
            if(!entry.notice)lines.push_back({typeset(entry.speaker,10,10000,10000),{0,entry.speaker.size(),0},true,entry.warm_name});
            const auto layout=typeset(entry.text,10,270,10000);
            for(const auto& page:layout.pages)for(const auto& line:page.lines)
                lines.push_back({layout,line,false,false,entry.notice});
            lines.push_back({{}, {},false});
        }
        constexpr size_t shown=13;
        const auto end=lines.size()-std::min(frame.history_offset,lines.size());
        const auto begin=end>shown?end-shown:0;
        double y=45;
        for(size_t i=begin;i<end;++i) {
            const auto& line=lines[i];
            if(line.layout.shaped)paint.text(line.layout,24,y,270,13,{line.range},line.layout.text.size(),
                line.notice?.61:line.speaker?(line.warm_name?1:.41):1,
                line.notice?.89:line.speaker?(line.warm_name?.67:.75):1,
                line.notice?.97:line.speaker && line.warm_name?.35:1,line.notice?"history_notice":"history_line");
            y+=12.5;
        }
    }
    json report={{"schema","srw64.native-dialogue-raster.v1"},{"font",text::game_font_path().filename().string()},{"locale",localization::catalog().locale},
        {"renderer","FreeType + HarfBuzz + ICU"},{"native_vi",frame.vi},
        {"drawable",{width,height}},{"logical_font_size",frame.font_size},{"blocks",paint.blocks}};
    result.report=std::move(report);
    result.image.validate();
    return result;
}
Layout typeset(const std::u16string& value,unsigned size,double width,double height) {
    return layout_text(value,size,width,height);
}
}
