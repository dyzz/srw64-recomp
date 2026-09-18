#include "native_dialogue.hpp"
#include "diagnostics.hpp"
#include "localization/catalog.hpp"
#include "plume_metal.h"
#include "json/json.hpp"
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace srw64::dialogue {
namespace {
template<class T> struct CF {
    T v;
    explicit CF(T value):v(value) {if(!v)throw std::runtime_error("Core Text allocation failed");}
    ~CF(){CFRelease(v);}
    CF(const CF&)=delete;
};
CFStringRef string(const std::u16string& value) {
    return CFStringCreateWithCharacters(nullptr,reinterpret_cast<const UniChar*>(value.data()),value.size());
}
CTFontRef font(double size) {
    CF requested(string(utf16(localization::catalog().font)));
    auto result=CTFontCreateWithName(requested.v,size,nullptr);
    CF actual(CTFontCopyPostScriptName(result));
    if(!CFEqual(actual.v,requested.v)) {
        CFRelease(result);throw std::runtime_error("Configured native font is unavailable: "+localization::catalog().font);
    }
    return result;
}
CFAttributedStringRef attributed(CFStringRef text,CTFontRef face,CGColorRef color=nullptr) {
    const void* keys[]={kCTFontAttributeName,kCTLanguageAttributeName,kCTForegroundColorAttributeName};
    CF language(string(utf16(localization::catalog().locale)));
    const void* values[]={face,language.v,color};
    CF dictionary(CFDictionaryCreate(nullptr,keys,values,color?3:2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks));
    return CFAttributedStringCreate(nullptr,text,dictionary.v);
}
MTL::Device* device{};
MTL::RenderPipelineState* pipeline{};
MTL::Texture* texture{};
MTL::PixelFormat pipeline_format{};
std::filesystem::path output;
std::string cached_key;
using json=nlohmann::json;

void make_pipeline(MTL::PixelFormat format) {
    const char* source=R"(
        #include <metal_stdlib>
        using namespace metal;
        struct V { float4 p [[position]]; };
        vertex V vs(uint i [[vertex_id]]) {
            const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};
            return {float4(p[i],0,1)};
        }
        fragment float4 fs(V in [[stage_in]], texture2d<float> t [[texture(0)]]) {
            return t.read(uint2(in.p.xy));
        }
    )";
    NS::Error* error{};
    auto* library=device->newLibrary(NS::String::string(source,NS::UTF8StringEncoding),nullptr,&error);
    if(!library)throw std::runtime_error(error?error->localizedDescription()->utf8String():"Native text shader failed");
    auto* vs=library->newFunction(NS::String::string("vs",NS::UTF8StringEncoding));
    auto* fs=library->newFunction(NS::String::string("fs",NS::UTF8StringEncoding));
    auto* desc=MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vs);desc->setFragmentFunction(fs);
    auto* color=desc->colorAttachments()->object(0);
    color->setPixelFormat(format);color->setBlendingEnabled(true);
    color->setSourceRGBBlendFactor(MTL::BlendFactorOne);
    color->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    color->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    color->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    auto* next=device->newRenderPipelineState(desc,&error);
    desc->release();vs->release();fs->release();library->release();
    if(!next)throw std::runtime_error("Native text pipeline failed");
    if(pipeline)pipeline->release();pipeline=next;pipeline_format=format;
}
struct Painter {
    CGContextRef context;
    CGColorSpaceRef colors;
    double scale,ox,oy,height;
    json blocks=json::array();
    void text(const std::u16string& text,double x,double y,double size,double width,double h,
              const std::vector<Line>& lines,size_t revealed,double r,double g,double b,const char* role) {
        CF face(font(size*scale));CF content(string(text));
        CGFloat components[]={r,g,b,1};CF color(CGColorCreate(colors,components));
        CF attrs(attributed(content.v,face.v,color.v));CF setter(CTTypesetterCreateWithAttributedString(attrs.v));
        const double px=ox+x*scale,top=oy+y*scale;
        double baseline=height-top-CTFontGetAscent(face.v);
        const double line_height=size*1.22*scale;
        CGContextSaveGState(context);
        CGContextClipToRect(context,CGRectMake(px,height-top-h*scale,width*scale,h*scale));
        json drawn=json::array();
        for(const auto& line:lines) {
            CF value(CTTypesetterCreateLine(setter.v,CFRangeMake(line.start,line.end-line.start)));
            CGContextSetTextPosition(context,px,baseline);
            auto runs=CTLineGetGlyphRuns(value.v);
            for(CFIndex ri=0;ri<CFArrayGetCount(runs);++ri) {
                auto run=static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs,ri));
                const auto count=CTRunGetGlyphCount(run);
                std::vector<CFIndex> indices(count);CTRunGetStringIndices(run,CFRangeMake(0,0),indices.data());
                std::vector<CGGlyph> ids(count);CTRunGetGlyphs(run,CFRangeMake(0,0),ids.data());
                for(CFIndex i=0;i<count;++i) {
                    if(size_t(indices[i])>=revealed)continue;
                    if(!ids[i] && text[size_t(indices[i])]!=u'\n' && text[size_t(indices[i])]!=u'\r')
                        throw std::runtime_error("Missing native dialogue glyph");
                    CTRunDraw(run,context,CFRangeMake(i,1));
                }
            }
            const auto ink=CTLineGetBoundsWithOptions(value.v,kCTLineBoundsUseGlyphPathBounds);
            drawn.push_back({{"start",line.start},{"end",line.end},
                {"text",utf8(text.substr(line.start,line.end-line.start))},
                {"baseline",height-baseline},{"ink_bottom",height-baseline-CGRectGetMinY(ink)}});
            baseline-=line_height;
        }
        CGContextRestoreGState(context);
        blocks.push_back({{"role",role},{"font_pixels",size*scale},{"revealed_utf16",revealed},
            {"bounds",{px,top,width*scale,h*scale}},{"lines",drawn}});
    }
    void label(const std::u16string& value,double x,double y,double size,double width,double r,double g,double b,const char* role) {
        text(value,x,y,size,width,size*1.4,{{0,value.size(),0}},value.size(),r,g,b,role);
    }
    void panel(double x,double y,double w,double h) {
        CGContextSetRGBFillColor(context,.015,.035,.065,.96);
        CGContextFillRect(context,CGRectMake(ox+x*scale,height-oy-(y+h)*scale,w*scale,h*scale));
        CGContextSetRGBStrokeColor(context,.41,.75,1,1);CGContextSetLineWidth(context,scale);
        CGContextStrokeRect(context,CGRectMake(ox+x*scale,height-oy-(y+h)*scale,w*scale,h*scale));
    }
    void fill(double x,double y,double w,double h,double r,double g,double b,double alpha=1) {
        CGContextSetRGBFillColor(context,r,g,b,alpha);
        CGContextFillRect(context,CGRectMake(ox+x*scale,height-oy-(y+h)*scale,w*scale,h*scale));
    }
    void speaker_marker(const Box& box) {
        // Four short corner accents follow the original frame's bevels.
        const double x=box.x-5,y=box.y-22,w=187,h=59;
        CGContextSetRGBStrokeColor(context,.41,.83,1,1);CGContextSetLineWidth(context,1.3*scale);
        for(int right:{0,1})for(int bottom:{0,1}) {
            const double cx=x+right*w,cy=y+bottom*h,dx=right?-1:1,dy=bottom?-1:1;
            CGContextMoveToPoint(context,ox+(cx+dx*13)*scale,height-oy-cy*scale);
            CGContextAddLineToPoint(context,ox+(cx+dx*3)*scale,height-oy-cy*scale);
            CGContextAddLineToPoint(context,ox+cx*scale,height-oy-(cy+dy*3)*scale);
            CGContextAddLineToPoint(context,ox+cx*scale,height-oy-(cy+dy*11)*scale);
        }
        CGContextStrokePath(context);
        CGContextSetRGBFillColor(context,.41,.83,1,1);
        CGContextMoveToPoint(context,ox+(box.x-4)*scale,height-oy-(box.y-13)*scale);
        CGContextAddLineToPoint(context,ox+(box.x-1)*scale,height-oy-(box.y-10)*scale);
        CGContextAddLineToPoint(context,ox+(box.x-4)*scale,height-oy-(box.y-7)*scale);
        CGContextClosePath(context);CGContextFillPath(context);
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

void raster(const Frame& frame,uint32_t width,uint32_t height) {
    if(!width || !height || width>8192 || height>8192)throw std::runtime_error("Invalid native UI drawable");
    const double scale=std::min(width/320.0,height/240.0);
    const double ox=(width-320*scale)/2,oy=(height-240*scale)/2;
    std::vector<uint8_t> pixels(size_t(width)*height*4);
    CF colors(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CF context(CGBitmapContextCreate(pixels.data(),width,height,8,width*4,colors.v,
        CGBitmapInfo(kCGImageAlphaPremultipliedFirst)|kCGBitmapByteOrder32Little));
    CGContextSetShouldAntialias(context.v,true);CGContextSetShouldSmoothFonts(context.v,false);
    CGContextSetTextMatrix(context.v,CGAffineTransformIdentity);
    Painter paint{context.v,colors.v,scale,ox,oy,double(height)};
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
        paint.text(box.layout.text,box.x,box.y,frame.font_size,177,35,page.lines,box.revealed,grey,grey,grey,"body");
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
        struct HistoryLine {std::u16string text;Line range;bool speaker;bool warm_name{};bool notice{};};
        std::vector<HistoryLine> lines;
        for(const auto& entry:frame.history) {
            if(entry.text.empty())continue;
            // A host notice has no speaker; it keeps the accent colour of the UI frames.
            if(!entry.notice)lines.push_back({entry.speaker,{0,entry.speaker.size(),0},true,entry.warm_name});
            const auto layout=typeset(entry.text,10,270,10000);
            for(const auto& page:layout.pages)for(const auto& line:page.lines)
                lines.push_back({entry.text,line,false,false,entry.notice});
            lines.push_back({{}, {},false});
        }
        constexpr size_t shown=13;
        const auto end=lines.size()-std::min(frame.history_offset,lines.size());
        const auto begin=end>shown?end-shown:0;
        double y=45;
        for(size_t i=begin;i<end;++i) {
            const auto& line=lines[i];
            paint.text(line.text,24,y,10,270,13,{line.range},line.text.size(),
                line.notice?.61:line.speaker?(line.warm_name?1:.41):1,
                line.notice?.89:line.speaker?(line.warm_name?.67:.75):1,
                line.notice?.97:line.speaker && line.warm_name?.35:1,line.notice?"history_notice":"history_line");
            y+=12.5;
        }
    }
    auto* desc=MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatBGRA8Unorm,width,height,false);
    desc->setStorageMode(MTL::StorageModeShared);desc->setUsage(MTL::TextureUsageShaderRead);
    auto* next=device->newTexture(desc);if(!next)throw std::runtime_error("Native UI texture allocation failed");
    next->replaceRegion(MTL::Region(0,0,width,height),0,pixels.data(),width*4);
    // A fresh texture per update avoids overwriting a texture still in flight;
    // Metal command buffers retain the previous frame's texture until complete.
    if(texture)texture->release();texture=next;
    json report={{"schema","srw64.native-dialogue-raster.v1"},{"font",localization::catalog().font},{"locale",localization::catalog().locale},
        {"renderer","Core Text + Core Graphics + Metal"},{"native_vi",frame.vi},
        {"drawable",{width,height}},{"logical_font_size",frame.font_size},{"blocks",paint.blocks}};
    if(srw64_full_diagnostics())std::ofstream(output/"dialogue-raster.json")<<report.dump(2)<<'\n';
}
}
std::u16string utf16(const std::string& value) {
    CF text(CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(value.data()),value.size(),kCFStringEncodingUTF8,false));
    std::u16string result(CFStringGetLength(text.v),u'\0');
    CFStringGetCharacters(text.v,CFRangeMake(0,result.size()),reinterpret_cast<UniChar*>(result.data()));return result;
}
std::string utf8(const std::u16string& value) {
    CF text(string(value));std::vector<char> bytes(CFStringGetMaximumSizeForEncoding(value.size(),kCFStringEncodingUTF8)+1);
    if(!CFStringGetCString(text.v,bytes.data(),bytes.size(),kCFStringEncodingUTF8))throw std::runtime_error("Invalid native Unicode");
    return bytes.data();
}
Layout typeset(const std::u16string& value,unsigned size,double width,double height) {
    Layout result;result.text=value;
    CF text(string(value));CF face(font(size));CF attrs(attributed(text.v,face.v));
    CF setter(CTTypesetterCreateWithAttributedString(attrs.v));
    for(size_t i=0;i<value.size();) {
        auto range=CFStringGetRangeOfComposedCharactersAtIndex(text.v,i);
        i=range.location+range.length;result.clusters.push_back(i);
    }
    const size_t per_page=std::max<size_t>(1,std::floor(height/(size*1.22)));
    Page page;
    for(size_t start=0;start<value.size();) {
        const auto length=CTTypesetterSuggestLineBreak(setter.v,start,width);
        if(length<=0)throw std::runtime_error("Native text layout made no progress");
        CF line(CTTypesetterCreateLine(setter.v,CFRangeMake(start,length)));
        page.lines.push_back({start,start+size_t(length),CTLineGetTypographicBounds(line.v,nullptr,nullptr,nullptr)});
        start+=length;page.end=start;
        if(page.lines.size()==per_page || start==value.size()) {result.pages.push_back(page);page={};page.start=start;}
    }
    if(result.pages.empty())result.pages.push_back({});
    return result;
}
void metal_init(plume::RenderDevice* value,const std::filesystem::path& directory) {
    device=static_cast<plume::MetalDevice*>(value)->mtl;output=directory;
}
void metal_draw(plume::RenderCommandList* list,plume::RenderFramebuffer* framebuffer,uint64_t workload) {
    auto frame=presented_frame(workload);if(!frame)return;
    localization::Scope locale(frame->catalog);
    if(std::none_of(frame->boxes.begin(),frame->boxes.end(),[](const auto& b){return b.visible;}))return;
    const auto* fb=static_cast<const plume::MetalFramebuffer*>(framebuffer);
    if(fb->colorAttachments.size()!=1)throw std::runtime_error("Native UI needs one color target");
    auto* target=fb->colorAttachments[0].getTexture();const auto format=target->pixelFormat();
    if(format!=MTL::PixelFormatBGRA8Unorm && format!=MTL::PixelFormatRGBA8Unorm)throw std::runtime_error("Unvalidated native UI color target");
    if(!pipeline || pipeline_format!=format)make_pipeline(format);
    std::ostringstream key;key<<localization::catalog().locale<<localization::catalog().font<<localization::catalog().revision<<','<<framebuffer->getWidth()<<','<<framebuffer->getHeight()<<','<<frame->font_size
        <<','<<frame->speed<<frame->auto_read<<frame->history_open<<frame->history_offset<<frame->fast<<frame->skipping
        <<','<<frame->reading_event<<','<<frame->advance.visible<<frame->advance.waiting<<frame->advance.paused<<','<<frame->advance.permille;
    for(const auto& box:frame->boxes)key<<':'<<box.event<<','<<box.visible<<box.active<<','<<box.page<<','<<box.revealed<<','<<box.x<<','<<box.y;
    if(cached_key!=key.str()) {raster(*frame,framebuffer->getWidth(),framebuffer->getHeight());cached_key=key.str();}
    auto* command=static_cast<plume::MetalCommandList*>(list);
    command->endActiveRenderEncoder();command->endActiveBlitEncoder();
    auto* pass=MTL::RenderPassDescriptor::renderPassDescriptor();auto* color=pass->colorAttachments()->object(0);
    color->setTexture(target);color->setLoadAction(MTL::LoadActionLoad);color->setStoreAction(MTL::StoreActionStore);
    auto* encoder=command->mtl->renderCommandEncoder(pass);encoder->setRenderPipelineState(pipeline);
    encoder->setViewport(MTL::Viewport{0,0,double(framebuffer->getWidth()),double(framebuffer->getHeight()),0,1});
    encoder->setFragmentTexture(texture,0);encoder->drawPrimitives(MTL::PrimitiveTypeTriangle,NS::UInteger(0),NS::UInteger(3));encoder->endEncoding();
    if(!srw64_full_diagnostics())return;
    json state={{"schema","srw64.native-dialogue-present.v1"},{"workload",workload},{"native_vi",frame->vi},
        {"locale",localization::catalog().locale},{"catalog",localization::catalog().revision},
        {"history_open",frame->history_open},{"auto_read",frame->auto_read},{"speed",frame->speed},{"skipping",frame->skipping},
        {"reading_event",frame->reading_event},{"advance",{{"visible",frame->advance.visible},
            {"permille",frame->advance.permille},{"waiting",frame->advance.waiting},{"paused",frame->advance.paused}}},
        {"boxes",json::array()}};
    const auto* focus=frame->focused_box();state["focused_slot"]=focus?json(focus->slot):json(nullptr);
    for(const auto& b:frame->boxes)if(b.visible)state["boxes"].push_back({{"event",b.event},{"text_id",b.text_id},{"segment",b.segment},
        {"active",b.active},{"speaker",utf8(b.speaker)},{"text",utf8(b.layout.text)},
        {"page",b.page},{"pages",b.layout.pages.size()},{"revealed_utf16",b.revealed}});
    std::ofstream(output/"dialogue-present.tmp")<<state.dump(2)<<'\n';
    std::filesystem::rename(output/"dialogue-present.tmp",output/"dialogue-present.json");
}
void metal_shutdown() {
    if(texture){texture->release();texture=nullptr;}
    if(pipeline){pipeline->release();pipeline=nullptr;}
    device=nullptr;cached_key.clear();
}
}
