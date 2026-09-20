#include "probe_surface.hpp"
#include "plume_metal.h"
#include <SDL_syswm.h>
#include <SDL_metal.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#include <stdexcept>
namespace plume { std::unique_ptr<RenderInterface> CreateMetalInterface(); }

namespace srw64::ui {
ProbeSurface::ProbeSurface() {
    window=SDL_CreateWindow("SRW64 shared name page — standalone prototype",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        1100,760,SDL_WINDOW_METAL|SDL_WINDOW_RESIZABLE);
    if(!window)throw std::runtime_error(SDL_GetError());
    SDL_SysWMinfo info{};SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window,&info))throw std::runtime_error(SDL_GetError());
    view=SDL_Metal_CreateView(window);
    if(!view)throw std::runtime_error(SDL_GetError());
    auto* layer=static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(view));layer->setFramebufferOnly(false);
    handle={info.info.cocoa.window,layer};interface=plume::CreateMetalInterface();
}
ProbeSurface::~ProbeSurface(){interface.reset();if(view)SDL_Metal_DestroyView(view);if(window)SDL_DestroyWindow(window);}
std::function<void()> ProbeSurface::capture(plume::RenderDevice* device,plume::RenderCommandList* list,
    plume::RenderTexture* texture,unsigned width,unsigned height,const std::filesystem::path& path) {
    const unsigned stride=((width*4+255)/256)*256;
    std::shared_ptr<plume::RenderBuffer> buffer=device->createBuffer(plume::RenderBufferDesc::ReadbackBuffer(uint64_t(stride)*height));
    auto* commands=static_cast<plume::MetalCommandList*>(list);
    commands->endActiveRenderEncoder();commands->endActiveBlitEncoder();
    auto* encoder=commands->mtl->blitCommandEncoder();
    encoder->copyFromTexture(static_cast<plume::ExtendedRenderTexture*>(texture)->getTexture(),0,0,MTL::Origin(0,0,0),MTL::Size(width,height,1),
        static_cast<plume::MetalBuffer*>(buffer.get())->mtl,0,stride,uint64_t(stride)*height);
    encoder->endEncoding();
    return [buffer,width,height,stride,path]{
        const auto* pixels=static_cast<const unsigned char*>(buffer->map());std::vector<unsigned char> rgba(size_t(width)*height*4);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            auto* out=rgba.data()+(size_t(y)*width+x)*4;const auto* in=pixels+size_t(y)*stride+x*4;
            out[0]=in[2];out[1]=in[1];out[2]=in[0];out[3]=in[3];
        }
        buffer->unmap();
        if(!stbi_write_png(path.string().c_str(),width,height,4,rgba.data(),width*4))throw std::runtime_error("PNG capture failed");
    };
}
}
