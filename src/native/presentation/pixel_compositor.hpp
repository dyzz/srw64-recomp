#pragma once
#include "raster_image.hpp"
#include "plume_render_interface.h"
#include <memory>
#include <span>

namespace srw64::presentation {
struct PixelShaders {
    std::span<const unsigned char> vertex, pixel;
    plume::RenderShaderFormat format{};
};
PixelShaders embedded_pixel_shaders(plume::RenderShaderFormat);

// Single recording thread / one ordered graphics queue. No window, text engine,
// backend casts, queue submission or implicit waits. The caller owns the device.
class PixelCompositor {
    struct State;
    struct Pixels;
    std::shared_ptr<State> state_;
public:
    using Image = std::shared_ptr<const Pixels>;
    using Retention = std::shared_ptr<const void>;
    PixelCompositor(plume::RenderDevice&, const PixelShaders&);
    ~PixelCompositor();
    PixelCompositor(const PixelCompositor&) = delete;
    PixelCompositor& operator=(const PixelCompositor&) = delete;

    // Records a fresh, immutable texture upload on the supplied command list.
    // Keep Image alive until this upload completes, even if no draw follows.
    // Reuse only on the same ordered queue after this list has been submitted;
    // discard the Image if the upload list is abandoned. Never edit in-flight data.
    Image upload(plume::RenderCommandList&, const Bgra8Surface&);

    // The target must be a single, non-MSAA BGRA/RGBA8 UNORM color attachment
    // already transitioned to COLOR_WRITE, with exactly the uploaded dimensions.
    // The format is explicit because Plume's public framebuffer has no query.
    // Keep the returned token until GPU completion (or discard without submission).
    // It retains the texture, upload buffer, descriptors, shaders and pipelines,
    // even after the compositor/cache is replaced. Device teardown must wait.
    Retention draw(plume::RenderCommandList&, plume::RenderFramebuffer&,
                   plume::RenderFormat target_format, const Image&);
};
}
