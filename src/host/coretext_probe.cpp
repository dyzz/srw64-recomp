#include "coretext_probe.hpp"
#include "plume_metal.h"
#include "json/json.hpp"
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using json = nlohmann::json;
MTL::Device* device{};
MTL::RenderPipelineState* pipeline{};
MTL::Texture* texture{};
json config;
std::filesystem::path output;
uint32_t cached_width{}, cached_height{};
MTL::PixelFormat pipeline_format{};

template<class T> struct CFHandle {
    T value;
    explicit CFHandle(T value) : value(value) { if (!value) throw std::runtime_error("Core Text allocation failed"); }
    ~CFHandle() { CFRelease(value); }
    CFHandle(const CFHandle&) = delete;
};

std::string string(CFStringRef value) {
    std::vector<char> bytes(CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1);
    if (!CFStringGetCString(value, bytes.data(), bytes.size(), kCFStringEncodingUTF8))
        throw std::runtime_error("Core Text string conversion failed");
    return bytes.data();
}

CFStringRef cfstring(const std::string& value) {
    return CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(value.data()), value.size(), kCFStringEncodingUTF8, false);
}

void make_pipeline(MTL::PixelFormat format) {
    static const char* source = R"(
        #include <metal_stdlib>
        using namespace metal;
        struct V { float4 position [[position]]; };
        vertex V text_vertex(uint index [[vertex_id]]) {
            const float2 positions[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};
            return {float4(positions[index], 0, 1)};
        }
        fragment float4 text_fragment(V in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
            return tex.read(uint2(in.position.xy));
        }
    )";
    NS::Error* error = nullptr;
    auto* library = device->newLibrary(NS::String::string(source, NS::UTF8StringEncoding), nullptr, &error);
    if (!library) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "Metal shader compile failed");
    auto* vertex = library->newFunction(NS::String::string("text_vertex", NS::UTF8StringEncoding));
    auto* fragment = library->newFunction(NS::String::string("text_fragment", NS::UTF8StringEncoding));
    auto* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vertex);
    desc->setFragmentFunction(fragment);
    auto* attachment = desc->colorAttachments()->object(0);
    attachment->setPixelFormat(format);
    attachment->setBlendingEnabled(true);
    attachment->setSourceRGBBlendFactor(MTL::BlendFactorOne);
    attachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    attachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    attachment->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    auto* created = device->newRenderPipelineState(desc, &error);
    desc->release(); vertex->release(); fragment->release(); library->release();
    if (!created) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "Metal pipeline failed");
    if (pipeline) pipeline->release();
    pipeline = created;
    pipeline_format = format;
}

void rasterize(uint32_t width, uint32_t height) {
    if (width > 8192 || height > 8192 || width == 0 || height == 0)
        throw std::runtime_error("Core Text framebuffer size outside probe limits");
    const double scale = std::min(width / 960.0, height / 720.0);
    const double ox = (width - 960 * scale) / 2, oy = (height - 720 * scale) / 2;
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    CFHandle color_space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFHandle context(CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, color_space.value,
        CGBitmapInfo(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little));
    CGContextSetShouldAntialias(context.value, true);
    // Transparent textures use grayscale antialiasing, without LCD color fringes.
    CGContextSetShouldSmoothFonts(context.value, false);
    CGContextSetTextMatrix(context.value, CGAffineTransformIdentity);
    CFHandle requested_name(cfstring(config.at("font_name").get<std::string>()));
    const double size = config.at("font_size_px").get<double>() * scale;
    if (!std::isfinite(size) || size < 8 || size > 256) throw std::runtime_error("Invalid font size");
    CFHandle font(CTFontCreateWithName(requested_name.value, size, nullptr));
    CFHandle actual_name(CTFontCopyPostScriptName(font.value));
    if (string(actual_name.value) != config.at("font_name").get<std::string>())
        throw std::runtime_error("Requested font resolved to an unexpected substitute: " + string(actual_name.value));
    json report = {{"schema", "srw64.coretext-raster.v1"}, {"renderer", "Core Text + Core Graphics + Metal"},
        {"font_postscript_name", string(actual_name.value)}, {"font_size_pixels", size},
        {"width", width}, {"height", height}, {"scale_from_960x720", scale},
        {"pixel_format", "BGRA8 premultiplied sRGB values; non-sRGB UNORM target; 1:1 texel reads"},
        {"blocks", json::array()}};
    CFHandle font_url(static_cast<CFURLRef>(CTFontCopyAttribute(font.value, kCTFontURLAttribute)));
    CFHandle font_path(CFURLCopyFileSystemPath(font_url.value, kCFURLPOSIXPathStyle));
    report["font_file"] = string(font_path.value);
    for (const auto& block : config.at("blocks")) {
        const auto bounds = block.at("bounds").get<std::vector<double>>();
        const double x = ox + bounds.at(0) * scale, top = oy + bounds.at(1) * scale;
        const double w = bounds.at(2) * scale, h = bounds.at(3) * scale;
        if (x < 0 || top < 0 || w <= 0 || h <= 0 || x+w > width || top+h > height)
            throw std::runtime_error("Core Text block outside framebuffer");
        const double grey = block.at("grey").get<double>();
        CGFloat components[] = {grey, grey, grey, 1};
        CFHandle color(CGColorCreate(color_space.value, components));
        CFHandle text(cfstring(block.at("text").get<std::string>()));
        const void* keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName, kCTLanguageAttributeName};
        const void* values[] = {font.value, color.value, CFSTR("zh-Hans")};
        CFHandle attrs(CFDictionaryCreate(nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFHandle attributed(CFAttributedStringCreate(nullptr, text.value, attrs.value));
        CFHandle typesetter(CTTypesetterCreateWithAttributedString(attributed.value));
        CFIndex start = 0, length = CFStringGetLength(text.value);
        double baseline = height - top - CTFontGetAscent(font.value);
        json lines = json::array();
        CGContextSaveGState(context.value);
        CGContextClipToRect(context.value, CGRectMake(x, height-top-h, w, h));
        while (start < length) {
            const CFIndex count = CTTypesetterSuggestLineBreak(typesetter.value, start, w);
            if (count <= 0) throw std::runtime_error("Core Text line layout made no progress");
            CFHandle line(CTTypesetterCreateLine(typesetter.value, CFRangeMake(start, count)));
            CGFloat ascent{}, descent{}, leading{};
            const double advance = CTLineGetTypographicBounds(line.value, &ascent, &descent, &leading);
            const auto ink = CTLineGetBoundsWithOptions(line.value, kCTLineBoundsUseGlyphPathBounds);
            if (advance > w + .1 || baseline + CGRectGetMinY(ink) < height-top-h - .1 ||
                baseline + CGRectGetMaxY(ink) > height-top + .1 || CGRectGetMinX(ink) < -.1 || CGRectGetMaxX(ink) > w+.1)
                throw std::runtime_error("Text would overflow the probe dialogue box");
            CGContextSetTextPosition(context.value, x, baseline);
            CTLineDraw(line.value, context.value);
            CFHandle substring(CFStringCreateWithSubstring(nullptr, text.value, CFRangeMake(start, count)));
            json runs = json::array();
            auto glyph_runs = CTLineGetGlyphRuns(line.value);
            for (CFIndex i=0; i<CFArrayGetCount(glyph_runs); ++i) {
                auto run = static_cast<CTRunRef>(CFArrayGetValueAtIndex(glyph_runs, i));
                auto run_font = static_cast<CTFontRef>(CFDictionaryGetValue(CTRunGetAttributes(run), kCTFontAttributeName));
                CFHandle run_name(CTFontCopyPostScriptName(run_font));
                std::vector<CGGlyph> glyphs(CTRunGetGlyphCount(run));
                CTRunGetGlyphs(run, CFRangeMake(0,0), glyphs.data());
                if (std::find(glyphs.begin(), glyphs.end(), 0) != glyphs.end())
                    throw std::runtime_error("Missing glyph in Core Text probe");
                runs.push_back({{"font", string(run_name.value)}, {"glyph_count", glyphs.size()}});
            }
            lines.push_back({{"text", string(substring.value)}, {"utf16_start", start}, {"utf16_length", count},
                {"x", x}, {"baseline_y_from_top", height-baseline}, {"advance", advance},
                {"ink_bounds", {x+ink.origin.x, height-baseline-CGRectGetMaxY(ink), ink.size.width, ink.size.height}}, {"runs", runs}});
            start += count;
            baseline -= config.at("line_height_px").get<double>() * scale;
        }
        CGContextRestoreGState(context.value);
        report["blocks"].push_back({{"id", block.at("id")}, {"text", block.at("text")},
            {"fully_laid_out", start==length}, {"lines", lines}});
    }
    if (texture) texture->release();
    auto* desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatBGRA8Unorm, width, height, false);
    desc->setStorageMode(MTL::StorageModeShared);
    desc->setUsage(MTL::TextureUsageShaderRead);
    texture = device->newTexture(desc);
    if (!texture) throw std::runtime_error("Core Text texture allocation failed");
    texture->replaceRegion(MTL::Region(0,0,width,height), 0, pixels.data(), width*4);
    std::ofstream(output / "coretext-raster.json") << report.dump(2) << '\n';
    std::ofstream raw(output / "coretext-overlay.bgra", std::ios::binary);
    raw.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    cached_width = width; cached_height = height;
}
}

void srw64_coretext_init(plume::RenderDevice* value, const std::filesystem::path& directory) {
    device = static_cast<plume::MetalDevice*>(value)->mtl;
    output = directory;
    const char* path = std::getenv("SRW64_CORETEXT_CONFIG");
    if (!path) throw std::runtime_error("Core Text probe requires an explicit config");
    std::ifstream input(path);
    input >> config;
    if (config.at("schema") != "srw64.coretext-probe.v1") throw std::runtime_error("Unknown Core Text probe schema");
    if (config.contains("font_file")) {
        CFHandle path(cfstring(config.at("font_file").get<std::string>()));
        CFHandle url(CFURLCreateWithFileSystemPath(nullptr, path.value, kCFURLPOSIXPathStyle, false));
        CFErrorRef error{};
        if (!CTFontManagerRegisterFontsForURL(url.value, kCTFontManagerScopeProcess, &error)) {
            if (error) CFRelease(error);
            throw std::runtime_error("Could not register probe font");
        }
    }
}

void srw64_coretext_draw(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
    const auto* fb = static_cast<const plume::MetalFramebuffer*>(framebuffer);
    if (fb->colorAttachments.size() != 1) throw std::runtime_error("Core Text probe requires one color attachment");
    auto* target = fb->colorAttachments[0].getTexture();
    const auto format = target->pixelFormat();
    if (format != MTL::PixelFormatBGRA8Unorm && format != MTL::PixelFormatRGBA8Unorm)
        throw std::runtime_error("Unvalidated Core Text target color format");
    if (!pipeline || pipeline_format != format) make_pipeline(format);
    if (!texture || cached_width != framebuffer->getWidth() || cached_height != framebuffer->getHeight())
        rasterize(framebuffer->getWidth(), framebuffer->getHeight());
    auto* command = static_cast<plume::MetalCommandList*>(list);
    command->endActiveRenderEncoder();
    command->endActiveBlitEncoder();
    auto* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* color = pass->colorAttachments()->object(0);
    color->setTexture(target); color->setLoadAction(MTL::LoadActionLoad); color->setStoreAction(MTL::StoreActionStore);
    auto* encoder = command->mtl->renderCommandEncoder(pass);
    encoder->setRenderPipelineState(pipeline);
    encoder->setViewport(MTL::Viewport{0, 0, double(cached_width), double(cached_height), 0, 1});
    encoder->setFragmentTexture(texture, 0);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    encoder->endEncoding();
}

void srw64_coretext_shutdown() {
    if (texture) { texture->release(); texture=nullptr; }
    if (pipeline) { pipeline->release(); pipeline=nullptr; }
    device=nullptr; cached_width=cached_height=0;
}
