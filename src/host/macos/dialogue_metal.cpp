// macOS compositor. Text layout/rasterization is behind dialogue_raster.hpp.
#include "dialogue_raster.hpp"
#include "diagnostics.hpp"
#include "plume_metal.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace srw64::dialogue {
namespace {
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
void upload_raster(const Frame& frame,uint32_t width,uint32_t height) {
    auto raster=rasterize_frame(frame,width,height);
    raster.image.validate();
    auto* desc=MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatBGRA8Unorm,width,height,false);
    desc->setStorageMode(MTL::StorageModeShared);desc->setUsage(MTL::TextureUsageShaderRead);
    auto* next=device->newTexture(desc);if(!next)throw std::runtime_error("Native UI texture allocation failed");
    next->replaceRegion(MTL::Region(0,0,width,height),0,raster.image.pixels.data(),raster.image.row_bytes());
    // Keep the existing fresh-texture ownership rule: in-flight Metal command
    // buffers retain their texture. Never overwrite the previous frame in place.
    if(texture)texture->release();texture=next;
    // Preserve the diagnostic schema/renderer name consumed by existing probes.
    raster.report["renderer"]="Core Text + Core Graphics + Metal";
    if(srw64_full_diagnostics())std::ofstream(output/"dialogue-raster.json")<<raster.report.dump(2)<<'\n';
}
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
    if(cached_key!=key.str()) {upload_raster(*frame,framebuffer->getWidth(),framebuffer->getHeight());cached_key=key.str();}
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
