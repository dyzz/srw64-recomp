// Portable Chinese/Japanese/English dialogue scene. No OS or GPU APIs.
#include "dialogue_raster.hpp"
#include "dialogue_layout_adapter.hpp"
#include "text/game_fonts.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace srw64::dialogue {
namespace {
using json=nlohmann::json;
Layout layout_text(const std::u16string& value,double size,double width,double height) {
    return reader_layout(text::game_fonts(localization::catalog().locale)->layout(
        value,size,width,localization::catalog().locale),height);
}
// The name row: a 10-point name at the top left inside the box, the text area
// right under it (docs/design/dialogue-typesetting.md §4). The box interior
// runs from 20 above the guest's text origin to 34 below it.
constexpr double name_size=10, name_top=-19.5, body_top=-9.5;
// Text slot byte +3 indexes the palette table filled at 8008C5E4: 0 white (resource 2),
// 2 dark (resource 4). 8008FD40 sets 2 on the box a story line leaves; battle never does.
constexpr unsigned dark_palette=2;
void blend(presentation::Bgra8Surface& image,int x,int y,double r,double g,double b,double alpha) {
    if(x<0 || y<0 || x>=int(image.width) || y>=int(image.height))return;
    const unsigned a=unsigned(std::lround(std::clamp(alpha,0.0,1.0)*255)),inv=255-a;
    auto* p=image.pixels.data()+(size_t(y)*image.width+x)*4;
    const double color[]={b,g,r,1};
    for(unsigned c=0;c<4;++c)p[c]=uint8_t(std::min(255U,
        unsigned(std::lround(color[c]*a))+(p[c]*inv+127)/255));
}
std::string exact(double value) {char text[40];return {text,size_t(std::snprintf(text,sizeof text," %a",value))};}
// One drawing step: what it paints (the key), where (bounds, which hold all its pixels),
// and how, into a surface that covers REGION of the window.
struct Op {
    std::string key;
    PixelRect bounds;
    std::function<void(presentation::Bgra8Surface&,const PixelRect& region)> paint;
};
struct Painter {
    uint32_t width,height;
    double scale,ox,oy;
    std::vector<Op> ops;
    json blocks=json::array();
    void add(std::string key,PixelRect bounds,std::function<void(presentation::Bgra8Surface&,const PixelRect&)> paint) {
        bounds=bounds&PixelRect{0,0,int(width),int(height)};
        if(bounds.empty())return;
        for(int v:{bounds.left,bounds.top,bounds.right,bounds.bottom})key+=' '+std::to_string(v);
        ops.push_back({std::move(key),bounds,std::move(paint)});
    }
    void fill(double x,double y,double w,double h,double r,double g,double b,double alpha=1) {
        const double left=ox+x*scale,top=oy+y*scale,right=left+w*scale,bottom=top+h*scale;
        std::string key="fill";
        for(double v:{left,top,right,bottom,r,g,b,alpha})key+=exact(v);
        const PixelRect bounds{int(std::floor(left)),int(std::floor(top)),int(std::ceil(right)),int(std::ceil(bottom))};
        add(std::move(key),bounds,[=](presentation::Bgra8Surface& image,const PixelRect& region) {
            const auto area=bounds&region;
            for(int py=area.top;py<area.bottom;++py)
                for(int px=area.left;px<area.right;++px) {
                    const double cover=std::max(0.0,std::min(right,px+1.0)-std::max(left,double(px)))*
                        std::max(0.0,std::min(bottom,py+1.0)-std::max(top,double(py)));
                    blend(image,px-region.left,py-region.top,r,g,b,alpha*cover);
                }
        });
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
            // The line's glyphs stay inside its clip, and within a line height above and
            // below its own row.
            const auto& clip=*draw.clip;
            const double band=shaped.line_height()*scale;
            const PixelRect bounds=PixelRect{int(std::ceil(clip.x)),int(std::ceil(clip.y)),
                    int(std::ceil(clip.x+clip.width)),int(std::ceil(clip.y+clip.height))}&
                PixelRect{int(std::floor(clip.x)),int(std::floor(draw.y-band)),int(std::ceil(clip.x+clip.width)),int(std::ceil(draw.y+2*band))};
            std::string key="text "+utf8(value.text.substr(line.start,line.end-line.start));
            for(double v:{found->width,shaped.font_size(),draw.x,draw.y,scale,clip.x,clip.y,clip.width,clip.height,r,g,b})key+=exact(v);
            key+=' '+std::to_string(std::clamp(revealed,line.start,line.end));
            add(std::move(key),bounds,[layout=shaped,draw](presentation::Bgra8Surface& image,const PixelRect& region) {
                auto shifted=draw;
                shifted.x-=region.left;shifted.y-=region.top;
                shifted.clip->x-=region.left;shifted.clip->y-=region.top;
                layout.draw(image,shifted);
            });
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
    // One line at SIZE, or smaller if that is what fits WIDTH.
    void label_fit(const std::u16string& value,double x,double y,double size,double width,double r,double g,double b,const char* role) {
        const auto shaped=layout_text(value,size,1000000,size*1.4);
        const double wide=shaped.shaped && !shaped.shaped->lines().empty()?shaped.shaped->lines().front().width:0;
        label(value,x,y,wide>width?size*width/wide:size,width,r,g,b,role);
    }
    void progress(const Box& box,const AdvanceProgress& value) {
        const double x=box.x+116,y=box.y-15.5,w=61,h=2.5;  // right of the name
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

struct Scene {
    std::vector<Op> ops;
    json report;
};
Scene build(const Frame& frame,uint32_t width,uint32_t height,double picture_width) {
    localization::Scope locale(frame.catalog);
    presentation::Bgra8Surface::required_bytes(width,height);
    if(!frame.font_size || frame.font_size>256)throw std::runtime_error("Invalid native UI font size");
    if(!(picture_width>=320 && picture_width<=640))throw std::runtime_error("Invalid picture width");
    const double scale=std::min(width/picture_width,height/240.0);
    const double ox=(width-320*scale)/2,oy=(height-240*scale)/2;
    Painter paint{width,height,scale,ox,oy};
    const auto* focus=frame.focused_box();
    for(const auto& box:frame.boxes) {
        if(!box.visible || box.layout.pages.empty())continue;
        const auto& page=box.layout.pages.at(box.page);
        // The name keeps its size; the text area below it takes any body size.
        // The current speaker shows only by a slightly brighter name: the same blue, the
        // other box's at four fifths (the user had the corner brackets and the
        // triangle marker removed).
        const bool focused=&box==focus;
        const double name=focused?1:.8;
        const double x=box.x+box.shift_x,y=box.y+box.shift_y;
        paint.label(box.speaker,x,y+name_top,name_size,box.width,
            name*105/255,name*191/255,name,"speaker");
        // Dark text exactly when the original draws it so: the slot's palette byte names
        // the dark text palette (resource 4) when a story speaker hands over to the other
        // side. Reading state plays no part; a battle quote stays white to the end.
        const double grey=box.palette==dark_palette?123./255:1;
        paint.text(box.layout,x,y+body_top,box.width,body_height,page.lines,box.revealed,grey,grey,grey,"body");
        // No page counter: a long translation simply continues on the next A,
        // like the original's own pages.
        if(focused && frame.advance.visible && box.event==frame.reading_event)paint.progress(box,frame.advance);
    }
    // A guest confirmation can leave both panels inactive before the next
    // speaker/STOP fragment arrives. Keep the shared controls visible for the
    // visible dialogue, independently of which panel currently owns reading.
    const auto visible=[](const Box& box){return box.visible && !box.layout.pages.empty();};
    if(!frame.display_only && std::any_of(frame.boxes.begin(),frame.boxes.end(),visible)) {
        const auto& catalog=localization::catalog();
        const auto status=frame.skipping?catalog.ui("skip"):frame.fast?catalog.ui("fast"):frame.auto_read?
            catalog.ui("auto")+" "+std::to_string(frame.speed)+"/"+std::to_string(Reader::max_speed):catalog.ui("manual");
        // The map marker and portraits occupy the middle of this scene.
        // Keep the reading controls on the otherwise unused bottom edge. The bar grows
        // upward with the interface size, up to the lower dialogue box's edge (225), and
        // the controls take what width is left, shrinking to fit it.
        const double f=std::clamp(frame.bar_scale,1.0,1.4),top=239-10*f;
        const auto x=[&](double at){return 6+(at-6)*f;};
        const auto y=[&](double at){return top+(at-229)*f;};
        paint.panel(3,top,314,10*f);
        paint.label(utf16(status),6,y(230),6*f,31*f,.75,.87,1,"status");
        constexpr double speed_pitch=28.0/Reader::max_speed;
        for(unsigned i=0;i<Reader::max_speed;++i) {
            const bool lit=frame.auto_read && i<frame.speed;
            paint.fill(x(38+i*speed_pitch),y(232),(speed_pitch-1)*f,4.5*f,lit?1:.17,lit?.64:.26,lit?.23:.32);
        }
        paint.blocks.push_back({{"role","auto_speed"},{"level",frame.auto_read?frame.speed:0},{"maximum",Reader::max_speed}});
        paint.label(utf16(catalog.ui("font_size")+" "+std::to_string(frame.font_size)),x(68),y(230),6*f,24*f,.75,.87,1,"font_size");
        if(!frame.controls_text.empty())paint.label_fit(utf16(frame.controls_text),x(92),y(231),5.1*f,314-x(92),.75,.8,.86,"controls");
    }
    if(frame.history_open) {
        paint.panel(16,18,288,202);
        paint.label(utf16(localization::catalog().ui("history_title")),23,23,12,210,.41,.75,1,"history_title");
        if(!frame.history_controls_text.empty())paint.label_fit(utf16(frame.history_controls_text),151,27,6.2,147,.75,.8,.86,"history_controls");
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
        constexpr size_t shown=Reader::history_shown;
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
    return {std::move(paint.ops),std::move(report)};
}
}

PixelRect PixelRect::operator&(const PixelRect& other) const {
    return {std::max(left,other.left),std::max(top,other.top),std::min(right,other.right),std::min(bottom,other.bottom)};
}
PixelRect PixelRect::operator|(const PixelRect& other) const {
    if(empty())return other;
    if(other.empty())return *this;
    return {std::min(left,other.left),std::min(top,other.top),std::max(right,other.right),std::max(bottom,other.bottom)};
}
RasterizedFrame rasterize_frame(const Frame& frame,uint32_t width,uint32_t height,double picture_width) {
    auto scene=build(frame,width,height,picture_width);
    RasterizedFrame result{presentation::Bgra8Surface(width,height),std::move(scene.report)};
    const PixelRect all{0,0,int(width),int(height)};
    for(const auto& op:scene.ops)op.paint(result.image,all);
    result.image.validate();
    return result;
}
IncrementalRaster::Update IncrementalRaster::update(const Frame& frame,uint32_t width,uint32_t height,double picture_width,
                                                    const std::string& context) {
    auto scene=build(frame,width,height,picture_width);
    Update result;
    std::vector<std::pair<std::string,PixelRect>> drawn;
    drawn.reserve(scene.ops.size());
    for(const auto& op:scene.ops){drawn.emplace_back(op.key,op.bounds);result.drawn=result.drawn|op.bounds;}
    const auto by_key=[](const auto& a,const auto& b){return a.first<b.first;};
    std::sort(drawn.begin(),drawn.end(),by_key);
    // What changed: the steps drawn last time and not now, and the other way round
    // (a key holds its bounds). Everywhere else the last picture is this one.
    const PixelRect all{0,0,int(width),int(height)};
    std::vector<PixelRect> dirty;
    if(!valid_ || context!=context_ || width!=width_ || height!=height_)dirty.push_back(all);
    else for(auto a=drawn_.begin(),b=drawn.begin();a!=drawn_.end() || b!=drawn.end();) {
        if(b==drawn.end() || (a!=drawn_.end() && a->first<b->first))dirty.push_back((a++)->second);
        else if(a==drawn_.end() || b->first<a->first)dirty.push_back((b++)->second);
        else {++a;++b;}
    }
    // Rectangles that overlap or touch are painted once, as one.
    for(bool merged=true;merged;) {
        merged=false;
        for(size_t i=0;i<dirty.size() && !merged;++i)for(size_t j=i+1;j<dirty.size();++j) {
            const auto &p=dirty[i],&q=dirty[j];
            if(p.left<=q.right && q.left<=p.right && p.top<=q.bottom && q.top<=p.bottom) {
                dirty[i]=p|q;dirty.erase(dirty.begin()+j);merged=true;break;
            }
        }
    }
    if(dirty.size()>8){PixelRect all_dirty;for(const auto& rect:dirty)all_dirty=all_dirty|rect;dirty={all_dirty};}
    for(const auto& rect:dirty) {
        const auto area=rect&all;
        if(area.empty())continue;
        Patch patch{area,presentation::Bgra8Surface(area.width(),area.height())};
        for(const auto& op:scene.ops)if(!(op.bounds&area).empty())op.paint(patch.pixels,area);
        result.patches.push_back(std::move(patch));
    }
    valid_=true;context_=context;width_=width;height_=height;drawn_=std::move(drawn);
    return result;
}
Layout typeset(const std::u16string& value,double size,double width,double height) {
    return layout_text(value,size,width,height);
}
double body_size(unsigned setting) {
    return localization::catalog().locale=="en"?setting*0.85:setting;
}
text::PageStyle body_style(const std::string& locale,std::vector<size_t> stops,std::vector<size_t> forced) {
    text::PageStyle style;
    style.height=body_height;
    style.min_spacing=locale=="en"?1.15:1.08;style.max_spacing=1.22;
    style.rank_breaks=true;style.halve_line_end=locale=="zh-Hans";
    std::sort(forced.begin(),forced.end());std::sort(stops.begin(),stops.end());
    // The original's page breaks end a sentence only in Japanese; a translation
    // marks its own sentence ends, and a bare page break there is mid-sentence.
    style.forced=std::move(forced);
    if(locale=="ja")style.sentence_ends=std::move(stops);
    return style;
}
Layout typeset_body(const std::u16string& value,double size,std::vector<size_t> stops,std::vector<size_t> forced,double width) {
    const auto& locale=localization::catalog().locale;
    return reader_layout(text::game_fonts(locale)->layout(value,size,width,locale,
        body_style(locale,std::move(stops),std::move(forced))),body_height);
}
}
