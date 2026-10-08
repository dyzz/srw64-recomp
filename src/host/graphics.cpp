#define HLSL_CPU
#include "hle/rt64_application.h"
#include "graphics.hpp"
#include "mini_stage.hpp"
#include "debug_protocol.hpp"
#include "audio.hpp"
#include "window_test_control.hpp"
#include "input_mode.hpp"
#include "input_bindings.hpp"
#include "steam_deck.hpp"
#include "game_frame.hpp"
#include "frame_rate.hpp"
#include "wide_map.hpp"
#include "native_marker.hpp"
#include "native_map.hpp"
#include "native_gpu.hpp"
#include "native_portrait.hpp"
#include "native_background.hpp"
#include "native_sprite.hpp"
#include "presentation/image_mode.hpp"
#ifdef SRW64_NATIVE_DIALOGUE
#include "presentation_settings.hpp"
#include "settings_window.hpp"
#include "debug_server.hpp"
#include "debug_ui.hpp"
#include "ui/frontend.hpp"
#endif
#include "hle/rt64_workload_queue.h"
#include "hle/rt64_present_queue.h"
#include "../include/rt64_extended_gbi.h"
#ifdef SRW64_NATIVE_DIALOGUE
#include "native_name_entry.hpp"
#include "link_page.hpp"
#include "battle_page.hpp"
#include "intermission_page.hpp"
#include "upgrade_page.hpp"
#include "parts_page.hpp"
#include "ability_page.hpp"
#include "swap_page.hpp"
#include "save_page.hpp"
#include "title_page.hpp"
#include "native_dialogue.hpp"
#include "post_filter.hpp"
#include "ultramodern/ultramodern.hpp"
#endif
#include "librecomp/game.hpp"
#include "rt64_render_hooks.h"
#ifdef __APPLE__
#include "plume_metal.h"
#endif
#ifdef __ANDROID__
#include <android/native_window.h>
// Plume's Vulkan swap chain asks for the activity's current window (rt64_android_patches.py).
namespace plume { extern ANativeWindow *(*AndroidCurrentWindow)(); }
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#include <SDL.h>
#include <SDL_syswm.h>
#ifdef __APPLE__
#include <SDL_metal.h>
#else
#include <SDL_vulkan.h>
#endif
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>

namespace {
#ifdef __ANDROID__
// The game's SDL window and its current ANativeWindow, for Plume (rt64_android_patches.py).
SDL_Window* android_window = nullptr;
ANativeWindow* android_current_window() {
    SDL_SysWMinfo wm{};
    SDL_VERSION(&wm.version);
    return SDL_GetWindowWMInfo(android_window, &wm) && wm.subsystem == SDL_SYSWM_ANDROID ? wm.info.android.window : nullptr;
}
#endif
SDL_Window* window;
#ifdef __APPLE__
SDL_MetalView view;
// Metal unless SRW64_GRAPHICS_API=vulkan runs RT64 on MoltenVK (a test of the Vulkan path).
bool metal_backend = true;
#endif
// Completion callbacks for the list the draw hook recorded, run by the presented
// hook after RT64 waits for that submission (all backends but Metal). Both run on
// the present thread.
std::vector<std::function<void(bool)>> after_present;
void run_after_present(bool completed) {
    auto callbacks = std::move(after_present);
    after_present.clear();
    for (auto& callback : callbacks) callback(completed);
}
std::array<uint8_t, 0x1000> dmem{}, imem{};
std::array<uint8_t, 0x40> header{};
uint32_t mi_interrupt{}, dpc_start{}, dpc_end{}, dpc_current{}, dpc_status{};
uint32_t dpc_clock{}, dpc_buffer_busy{}, dpc_pipe_busy{}, dpc_tmem{};
plume::RenderDevice* capture_device;
int graphics_api = -1;  // RT64's chosen API (RT64::UserConfiguration::GraphicsAPI), for srw64_graphics_info
std::filesystem::path capture_directory;
uint64_t presented_frames{};
std::string capture_clock = "native_vi_at_draw";
// SDL is polled on the window thread; the game reads one coherent snapshot.
std::atomic<uint32_t> keyboard_state{}, pad_state{};
SDL_GameController* pad{};

// input_bindings.hpp spells SDL's numbers out so it needs no SDL.
static_assert(SDL_SCANCODE_Z == srw64::input::scancode::Z && SDL_SCANCODE_RETURN == srw64::input::scancode::Return &&
              SDL_SCANCODE_SPACE == srw64::input::scancode::Space && SDL_SCANCODE_UP == srw64::input::scancode::Up &&
              SDL_SCANCODE_A == srw64::input::scancode::A && SDL_SCANCODE_ESCAPE == srw64::input::scancode::Escape &&
              SDL_SCANCODE_T == srw64::input::scancode::T && SDL_SCANCODE_1 == srw64::input::scancode::N1 &&
              SDL_SCANCODE_4 == srw64::input::scancode::N4 && SDL_SCANCODE_BACKSPACE == srw64::input::scancode::Backspace);
static_assert(SDL_CONTROLLER_BUTTON_BACK == srw64::input::pad_button::Back && SDL_CONTROLLER_BUTTON_START == srw64::input::pad_button::Start &&
              SDL_CONTROLLER_BUTTON_LEFTSHOULDER == srw64::input::pad_button::LeftShoulder &&
              SDL_CONTROLLER_BUTTON_DPAD_RIGHT == srw64::input::pad_button::DRight && SDL_CONTROLLER_BUTTON_MAX == srw64::input::pad_button::Count);
static_assert(SDL_CONTROLLER_AXIS_RIGHTY == srw64::input::pad_axis::RightY && SDL_CONTROLLER_AXIS_TRIGGERRIGHT == srw64::input::pad_axis::TriggerRight &&
              SDL_CONTROLLER_AXIS_MAX == srw64::input::pad_axis::Count);

// The physical key of the same name as a key the debug interface holds or presses.
SDL_Scancode virtual_scancode(srw64::debug::Key key) {
    static const auto table = [] {
        std::array<SDL_Scancode, srw64::debug::KeyCount> t{};
        for (unsigned k = 0; k < srw64::debug::KeyCount; ++k)
            t[k] = SDL_GetScancodeFromName(std::string(srw64::debug::key_names[k]).c_str());
        return t;
    }();
    return table[key];
}

// At 16:10 the original's 4:3 stays centred, but what the game draws across the whole
// frame reaches the sides: its clear colour (grey in battle), full-screen flashes, the
// after-images the battle copies from earlier frames. Until a scene is widened on purpose
// (docs/design/deck-16x10.md) the sides are black, like the 4:3 picture's pillarbox.
// Each frame says how much of the picture it fills (wide_map::Shape): all of it, the band a
// widened tactical map's view covers, or the original's 4:3 (art not yet drawn wider).
void mask_sides(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
    const float width = float(framebuffer->getWidth()), height = float(framebuffer->getHeight());
    const float scale = srw64::frame::scale(width, height), picture = srw64::frame::width(width, height);
    const auto shape = srw64::wide_map::frame_shape(RT64::GetRenderHookWorkloadId());
    if (shape.fill == srw64::wide_map::Shape::Fill::whole) return;
    float band_left = (picture - srw64::frame::kWidth) / 2, band_right = (picture + srw64::frame::kWidth) / 2;
    if (shape.fill == srw64::wide_map::Shape::Fill::view) { band_left = shape.left; band_right = shape.right; }
    const float origin = (width - picture * scale) / 2;
    const int32_t left = std::max(0, int32_t(std::lround(origin + band_left * scale)));
    const int32_t right = std::min(int32_t(width), int32_t(std::lround(origin + band_right * scale)));
    if (left <= 0 && right >= int32_t(width)) return;
    plume::RenderRect sides[2];
    uint32_t count = 0;
    if (left > 0) sides[count++] = {0, 0, left, int32_t(height)};
    if (right < int32_t(width)) sides[count++] = {right, 0, int32_t(width), int32_t(height)};
    list->setFramebuffer(framebuffer);
    list->clearColor(0, plume::RenderColor(0.f, 0.f, 0.f, 1.f), sides, count);
}

void capture_frame(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
    using namespace plume;
    mask_sides(list, framebuffer);
#ifdef SRW64_NATIVE_DIALOGUE
    srw64::dialogue::gpu_draw(list, framebuffer, RT64::GetRenderHookWorkloadId());
    // Clear workload-keyed guest naming frames before the shared UI renders.
    const auto name_workload=RT64::GetRenderHookWorkloadId();
    const bool name_cover=srw64::names::frame_cover(name_workload);
    if(name_cover) {
#ifdef __APPLE__
        if(metal_backend) {
        auto* name_commands=static_cast<plume::MetalCommandList*>(list);
        name_commands->endActiveRenderEncoder();name_commands->endActiveBlitEncoder();
        auto* pass=MTL::RenderPassDescriptor::renderPassDescriptor();
        auto* color=pass->colorAttachments()->object(0);
        color->setTexture(static_cast<const plume::MetalFramebuffer*>(framebuffer)->colorAttachments[0].getTexture());
        color->setLoadAction(MTL::LoadActionClear);color->setStoreAction(MTL::StoreActionStore);
        color->setClearColor(MTL::ClearColor(.028,.045,.07,1));
        name_commands->mtl->renderCommandEncoder(pass)->endEncoding();
        } else
#endif
        {
            list->setFramebuffer(framebuffer);
            list->clearColor(0, RenderColor(.028f,.045f,.07f,1.f));
        }
    }
    // With a RetroArch preset (post_filter.hpp): the pages that belong to the game picture,
    // then the preset over the picture, its dialogue and those pages, then the rest of the
    // interface (settings, notices, the bezel) sharp on top.
    bool ui_drawn;
    if(srw64::post_filter::active()) {
        const bool pages=srw64::ui::draw(list,framebuffer,name_cover,0);
        ui_drawn=pages;
        const float width = float(framebuffer->getWidth()), height = float(framebuffer->getHeight());
        const float scale = srw64::frame::scale(width, height), picture = srw64::frame::width(width, height);
        const float w = picture * scale, h = srw64::frame::kHeight * scale;
        srw64::post_filter::apply(list, framebuffer, int(std::lround((width - w) / 2)), int(std::lround((height - h) / 2)),
                                  int(std::lround(w)), int(std::lround(h)), picture, pages);
        ui_drawn=srw64::ui::draw(list,framebuffer,name_cover,1) || ui_drawn;
    } else {
        ui_drawn=srw64::ui::draw(list,framebuffer,name_cover);
    }
    srw64_after_gpu(list,[name_workload,name_cover,ui_drawn](bool completed) {
        if(ui_drawn)srw64::ui::presented();
        if(completed)srw64::names::cover_presented(name_workload,name_cover);
    });
#endif
    const uint64_t frame = ++presented_frames;
    const uint64_t vi = srw64_current_vi();
    const std::string clock_name = capture_clock;
    const int image_mode = srw64::presentation::image_mode.current();
    const uint64_t workload = RT64::GetRenderHookWorkloadId();
    // Opt-in temporal QA: sample every completed frame, so a single bad present is visible;
    // a frame that changes sharply is also saved whole.
    static const uint64_t trace_begin = std::getenv("SRW64_FRAME_TRACE_FROM") ? std::stoull(std::getenv("SRW64_FRAME_TRACE_FROM")) : UINT64_MAX;
    static const uint64_t trace_end = std::getenv("SRW64_FRAME_TRACE_TO") ? std::stoull(std::getenv("SRW64_FRAME_TRACE_TO")) : 0;
    const bool trace = vi >= trace_begin && vi <= trace_end;
    // A debug-interface screenshot request takes this present; a recording takes every one.
    const auto debug_shot = srw64::debug::screenshots().take();
    const bool recording = srw64::debug::recording().active();
    if (!trace && !debug_shot && !recording) return;
    nlohmann::json dialogue_trace=nullptr;
#ifdef SRW64_NATIVE_DIALOGUE
    if(trace)if(auto snapshot=srw64::dialogue::presented_frame(workload)) {
        unsigned visible=0,active=0;
        for(const auto& box:snapshot->boxes)if(box.visible && !box.layout.pages.empty()) {
            ++visible;if(box.active)++active;
        }
        dialogue_trace={{"visible_boxes",visible},{"active_boxes",active},
            {"locale",snapshot->catalog?snapshot->catalog->locale:srw64::localization::catalog().locale},
            {"automatic",snapshot->auto_read},{"speed",snapshot->speed},{"speed_maximum",srw64::dialogue::Reader::max_speed},{"native_vi",snapshot->vi},
            {"reading_event",snapshot->reading_event},{"history_open",snapshot->history_open},
            {"advance",{{"visible",snapshot->advance.visible},{"permille",snapshot->advance.permille},
                        {"waiting",snapshot->advance.waiting},{"paused",snapshot->advance.paused}}}};
        const auto* focus=snapshot->focused_box();
        dialogue_trace["focus"]=focus?nlohmann::json({{"slot",focus->slot},{"event",focus->event},
            {"active",focus->active},{"page",focus->page},{"pages",focus->layout.pages.size()},
            {"x",focus->x},{"y",focus->y}}):nlohmann::json(nullptr);
    }
#endif
    const uint32_t width = framebuffer->getWidth(), height = framebuffer->getHeight();
    const uint32_t row_pixels = (width + 63) & ~63U;
    auto buffer = std::shared_ptr<RenderBuffer>(capture_device->createBuffer(RenderBufferDesc::ReadbackBuffer((uint64_t)row_pixels * 4 * height)));
#ifdef __ANDROID__
    bool bgra = false;  // the swap chain is R8G8B8A8 there (rt64_android_patches.py)
#else
    bool bgra = true;
#endif
#ifdef __APPLE__
    if (metal_backend) {
    const auto* metal_framebuffer = static_cast<const plume::MetalFramebuffer*>(framebuffer);
    if (metal_framebuffer->colorAttachments.size() != 1) std::abort();
    const auto& attachment = metal_framebuffer->colorAttachments[0];
    bgra = attachment.format == RenderFormat::B8G8R8A8_UNORM;
    if (!bgra && attachment.format != RenderFormat::R8G8B8A8_UNORM) {
        fprintf(stderr, "SRW64_CAPTURE_UNSUPPORTED_FORMAT %u\n", (unsigned)attachment.format);
        return;
    }
    // Metal: a texture-to-buffer blit on the same command buffer, after ending the
    // render encoder and before presentation.
    auto* command_list = static_cast<plume::MetalCommandList*>(list);
    command_list->checkActiveBlitEncoder();
    auto* blit = command_list->activeBlitEncoder;
    blit->copyFromTexture(attachment.getTexture(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(width, height, 1),
                         static_cast<plume::MetalBuffer*>(buffer.get())->mtl, 0, row_pixels * 4, (uint64_t)row_pixels * 4 * height);
    command_list->endActiveBlitEncoder();
    } else
#endif
    {
        // Vulkan and D3D12: copy RT64's swapchain image (B8G8R8A8, rt64_application.cpp)
        // into the buffer (prepare_rt64.py), then return it to COLOR_WRITE for RT64's
        // present barrier.
        auto* texture = RT64::GetRenderHookSwapChainTexture();
        if (!texture) {
            if (debug_shot) srw64::debug::screenshots().finish(debug_shot->id, {{"error", "no swapchain texture to read back"}});
            return;
        }
        list->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture, RenderTextureLayout::COPY_SOURCE));
        list->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(buffer.get(), bgra ? RenderFormat::B8G8R8A8_UNORM : RenderFormat::R8G8B8A8_UNORM, width, height, 1, row_pixels),
                                RenderTextureCopyLocation::Subresource(texture));
        list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(texture, RenderTextureLayout::COLOR_WRITE));
    }
    const bool interactive = std::getenv("SRW64_INTERACTIVE") && std::string(std::getenv("SRW64_INTERACTIVE")) == "1";
    const auto path = capture_directory / (interactive ? "present-latest.png" : "present-" + std::to_string(frame) + ".png");
    srw64_after_gpu(list, [buffer, width, height, row_pixels, bgra, path, frame, vi, clock_name, image_mode, workload, trace, dialogue_trace, debug_shot, recording](bool completed) {
        if (!completed) {
            fprintf(stderr, "SRW64_CAPTURE_GPU_FAILED\n");
            if (debug_shot) srw64::debug::screenshots().finish(debug_shot->id, {{"error", "GPU capture failed"}});
            return;
        }
        auto* raw = static_cast<const uint8_t*>(buffer->map());
        std::vector<uint8_t> rgba((uint64_t)width * height * 4);
        for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
            const auto* pixel = raw + ((uint64_t)y * row_pixels + x) * 4;
            auto* out = rgba.data() + ((uint64_t)y * width + x) * 4;
            out[0] = pixel[bgra ? 2 : 0]; out[1] = pixel[1]; out[2] = pixel[bgra ? 0 : 2]; out[3] = pixel[3];
        }
        buffer->unmap();
        bool anomaly=false;
        if(trace) {
            static std::vector<uint8_t> previous;
            static std::ofstream samples(capture_directory/"frame-trace.rgb",std::ios::binary);
            static std::ofstream events(capture_directory/"frame-trace.jsonl");
            std::vector<uint8_t> sample(160*120*3);
            uint64_t difference=0,luminance=0;
            for(unsigned y=0;y<120;++y)for(unsigned x=0;x<160;++x)for(unsigned c=0;c<3;++c) {
                const size_t i=(y*160+x)*3+c;
                sample[i]=rgba[((y*height/120)*width+x*width/160)*4+c];
                luminance+=sample[i];
                if(!previous.empty())difference+=std::abs(int(sample[i])-int(previous[i]));
            }
            const double change=double(difference)/sample.size();
            anomaly=!previous.empty() && change>2;
            samples.write(reinterpret_cast<const char*>(sample.data()),sample.size());samples.flush();
            events<<nlohmann::json({{"present",frame},{"vi",vi},{"workload",workload},{"image_mode",image_mode},
                {"mean_rgb",double(luminance)/sample.size()},{"mean_delta",change},{"width",160},{"height",120},
                {"dialogue",dialogue_trace}}).dump()<<'\n';events.flush();
            previous=std::move(sample);
        }
        if (recording) srw64::debug::recording().add(rgba.data(), width, height);
        if (debug_shot) {
            const bool written = stbi_write_png(debug_shot->path.string().c_str(), width, height, 4, rgba.data(), width * 4);
            srw64::debug::screenshots().finish(debug_shot->id, written
                ? nlohmann::json({{"path", debug_shot->path.string()}, {"present", frame}, {"vi", vi},
                                  {"width", width}, {"height", height}, {"image_mode", image_mode}})
                : nlohmann::json({{"error", "cannot write " + debug_shot->path.string()}}));
        }
        if(!anomaly)return;
        if (!stbi_write_png(path.string().c_str(), width, height, 4, rgba.data(), width * 4)) std::abort();
        auto metadata_path = path;
        metadata_path.replace_extension("json");
        std::ofstream metadata(metadata_path);
        metadata << "{\"schema\":\"srw64.native-gpu-frame.v1\",\"present\":" << frame
                 << ",\"" << clock_name << "\":" << vi << ",\"width\":" << width << ",\"height\":" << height
                 << ",\"image_mode\":" << image_mode << ",\"GPU_completion\":\"completed\"}\n";
        fprintf(stderr, "SRW64_CAPTURE %s vi=%llu %ux%u\n", path.filename().string().c_str(), (unsigned long long)vi, width, height);
    });
}

class SRW64Renderer final : public ultramodern::renderer::RendererContext {
public:
    SRW64Renderer(uint8_t* rdram, ultramodern::renderer::WindowHandle handle) {
        srw64::marker::configure(capture_directory);
        srw64::hdmap::configure(capture_directory);
        RT64::SetRenderHooks([](plume::RenderInterface* rhi, plume::RenderDevice* device) {
            capture_device = device;
#ifdef __APPLE__
            metal_backend = rhi->getCapabilities().shaderFormat == plume::RenderShaderFormat::METAL;
#endif
            srw64::gpu::init(rhi, device);
            {
                const auto format = rhi->getCapabilities().shaderFormat;
                using srw64::post_filter::Backend;
                srw64::post_filter::init(format == plume::RenderShaderFormat::METAL ? Backend::metal :
                                         format == plume::RenderShaderFormat::SPIRV ? Backend::vulkan :
                                         format == plume::RenderShaderFormat::DXIL ? Backend::d3d12 : Backend::none, capture_directory);
            }
            srw64::marker::gpu_init();
            srw64::hdmap::gpu_init();
            srw64::portraits::gpu_init();
            srw64::backgrounds::gpu_init();
            srw64::sprites::gpu_init();
#ifdef SRW64_NATIVE_DIALOGUE
            srw64::dialogue::gpu_init(rhi, device, capture_directory);
            srw64::ui::render_init(rhi,device);
#endif
        }, capture_frame, [] {
            // Nothing is in flight after RT64's last present wait; settle the UI anyway.
            run_after_present(false);
            srw64::post_filter::shutdown();
            srw64::marker::shutdown();
            srw64::hdmap::shutdown();
            srw64::portraits::shutdown();
            srw64::backgrounds::shutdown();
            srw64::sprites::shutdown();
#ifdef SRW64_NATIVE_DIALOGUE
            srw64::ui::render_shutdown();
            srw64::dialogue::gpu_shutdown();
#endif
            srw64::gpu::shutdown();
        });
        RT64::SetRenderHookPresented([](unsigned long long) { run_after_present(true); });
        RT64::Application::Core core{};
#if defined(__APPLE__)
        core.window.window = handle.window;
        core.window.view = handle.view;
#elif defined(_WIN32)
        core.window = handle.window;
#elif defined(__ANDROID__)
        // ultramodern hands over the SDL window; Plume wants its ANativeWindow.
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(handle, &info) || info.subsystem != SDL_SYSWM_ANDROID) std::abort();
        core.window = info.info.android.window;
        // Back from the background the activity has a new window (rt64_android_patches.py):
        // Plume asks SDL for it when it remakes the swap chain; null while there is none.
        android_window = handle;
        plume::AndroidCurrentWindow = android_current_window;
#else
        core.window = handle;
#endif
        core.checkInterrupts = [] {};
        core.HEADER = header.data();
        core.RDRAM = rdram;
        core.DMEM = dmem.data();
        core.IMEM = imem.data();
        core.MI_INTR_REG = &mi_interrupt;
        core.DPC_START_REG = &dpc_start;
        core.DPC_END_REG = &dpc_end;
        core.DPC_CURRENT_REG = &dpc_current;
        core.DPC_STATUS_REG = &dpc_status;
        core.DPC_CLOCK_REG = &dpc_clock;
        core.DPC_BUFBUSY_REG = &dpc_buffer_busy;
        core.DPC_PIPEBUSY_REG = &dpc_pipe_busy;
        core.DPC_TMEM_REG = &dpc_tmem;
        auto* vi = ultramodern::renderer::get_vi_regs();
#define SRW64_VI(name) core.name = &vi->name
        SRW64_VI(VI_STATUS_REG); SRW64_VI(VI_ORIGIN_REG); SRW64_VI(VI_WIDTH_REG);
        SRW64_VI(VI_INTR_REG); SRW64_VI(VI_V_CURRENT_LINE_REG); SRW64_VI(VI_TIMING_REG);
        SRW64_VI(VI_V_SYNC_REG); SRW64_VI(VI_H_SYNC_REG); SRW64_VI(VI_LEAP_REG);
        SRW64_VI(VI_H_START_REG); SRW64_VI(VI_V_START_REG); SRW64_VI(VI_V_BURST_REG);
        SRW64_VI(VI_X_SCALE_REG); SRW64_VI(VI_Y_SCALE_REG);
#undef SRW64_VI
        RT64::ApplicationConfiguration configuration;
        configuration.useConfigurationFile = false;
        app = std::make_unique<RT64::Application>(core, configuration);
        using GraphicsAPI = RT64::UserConfiguration::GraphicsAPI;
#ifdef __APPLE__
        app->userConfig.graphicsAPI = GraphicsAPI::Metal;
#else
        // RT64 picks Vulkan on Linux, and D3D12 with a Vulkan fallback on Windows.
        app->userConfig.graphicsAPI = GraphicsAPI::Automatic;
#endif
        // For tests only: SRW64_GRAPHICS_API=vulkan (MoltenVK on a Mac), d3d12 or metal.
        if (const char* api = std::getenv("SRW64_GRAPHICS_API")) {
            const std::string name = api;
            if (name == "vulkan") app->userConfig.graphicsAPI = GraphicsAPI::Vulkan;
            else if (name == "d3d12") app->userConfig.graphicsAPI = GraphicsAPI::D3D12;
            else if (name == "metal") app->userConfig.graphicsAPI = GraphicsAPI::Metal;
        }
        app->userConfig.resolution = std::getenv("SRW64_NATIVE_RESOLUTION") && std::string(std::getenv("SRW64_NATIVE_RESOLUTION")) == "1"
            ? RT64::UserConfiguration::Resolution::WindowIntegerScale : RT64::UserConfiguration::Resolution::Original;
        if (const char* scale = std::getenv("SRW64_RESOLUTION_SCALE")) {
            char* end = nullptr;
            const long value = std::strtol(scale, &end, 10);
            if (end == scale || *end || value < 1 || value > 8) {
                fprintf(stderr, "SRW64_INVALID_RESOLUTION_SCALE %s\n", scale);
                std::abort();
            }
            app->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            app->userConfig.resolutionMultiplier = value;
        }
        fprintf(stderr, "SRW64_RENDER_RESOLUTION mode=%d multiplier=%.0f\n",
                int(app->userConfig.resolution), app->userConfig.resolutionMultiplier);
        configure_aspect(srw64::frame::kWidth);   // the window's shape is known from the first frame
        app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
        // 4x MSAA; SRW64_MSAA=0/2/4/8 for comparisons. RT64 falls back when the device
        // lacks the sample count; native draws follow the scene target's sample count.
        app->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::MSAA4X;
#ifdef __ANDROID__
        // A phone GPU (docs/design/android-port.md): no MSAA, 8-bit colour targets (RT64
        // picks 16-bit when device memory exceeds 512 MB, which shared memory always does)
        // and no idle compute work to keep the GPU clock up.
        app->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None;
        app->userConfig.internalColorFormat = RT64::UserConfiguration::InternalColorFormat::Standard;
        app->userConfig.idleWorkActive = false;
#endif
        if (const char* msaa = std::getenv("SRW64_MSAA")) {
            const std::string value = msaa;
            app->userConfig.antialiasing = value == "0" ? RT64::UserConfiguration::Antialiasing::None
                : value == "2" ? RT64::UserConfiguration::Antialiasing::MSAA2X
                : value == "8" ? RT64::UserConfiguration::Antialiasing::MSAA8X
                : RT64::UserConfiguration::Antialiasing::MSAA4X;
        }
        // For comparisons with the Android settings: SRW64_COLOR_FORMAT=standard (8-bit) or high.
        if (const char* color = std::getenv("SRW64_COLOR_FORMAT")) {
            const std::string value = color;
            if (value == "standard") app->userConfig.internalColorFormat = RT64::UserConfiguration::InternalColorFormat::Standard;
            else if (value == "high") app->userConfig.internalColorFormat = RT64::UserConfiguration::InternalColorFormat::High;
        }
        fprintf(stderr, "SRW64_MSAA samples=%u\n", app->userConfig.msaaSampleCount());
        app->userConfig.developerMode = false;
        const auto result = app->setup(0);
        switch (app->chosenGraphicsAPI) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12: chosen_api = ultramodern::renderer::GraphicsApi::D3D12; break;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan: chosen_api = ultramodern::renderer::GraphicsApi::Vulkan; break;
        default: chosen_api = ultramodern::renderer::GraphicsApi::Metal; break;
        }
        fprintf(stderr, "SRW64_GRAPHICS_API %d\n", int(app->chosenGraphicsAPI));
        graphics_api = int(app->chosenGraphicsAPI);
        setup_result = result == RT64::Application::SetupResult::Success
            ? ultramodern::renderer::SetupResult::Success : ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
        if (setup_result != ultramodern::renderer::SetupResult::Success) {
            fprintf(stderr, "SRW64_RT64_SETUP_FAILED result=%d\n", (int)result);
            app.reset();
        }
        else {
            if (const char* directory = std::getenv("SRW64_TEXTURE_DUMP")) {
                std::filesystem::create_directories(directory);
                app->state->dumpingTexturesDirectory = directory;
            }
            const char* art = std::getenv("SRW64_ART_PACK");
            const char* hd_available = std::getenv("SRW64_HD_AVAILABLE");
            if (hd_available && std::string(hd_available)=="0") {
                srw64::presentation::image_mode.original_only();
                const nlohmann::json status={{"schema","srw64.image-mode.v1"},{"mode","original"},
                    {"hd_available",false},{"model_5600","original"},{"vi",srw64_current_vi()}};
                std::ofstream(capture_directory/"image-mode.json")<<status.dump(2)<<'\n';
                fprintf(stderr,"SRW64_IMAGE_MODE original hd_available=0\n");
            }
            if (const char* directory = art ? art : std::getenv("SRW64_FONT_PACK")) {
                if (!app->textureCache->loadReplacementDirectory(RT64::ReplacementDirectory(directory))) {
                    fprintf(stderr, "SRW64_FONT_PACK_FAILED %s\n", directory);
                    std::abort();
                }
                fprintf(stderr, "SRW64_TEXTURE_PACK_LOADED %s\n", directory);
                if(art) {
                    const char* mode=std::getenv("SRW64_IMAGE_MODE");
                    srw64::presentation::image_mode.configure(mode && std::string(mode)=="hd");
                    apply_images();
                    // Whole-image portraits ship beside the RT64 pack; they chain their
                    // native draw hooks after marker/hdmap.
                    srw64::portraits::configure(directory, capture_directory);
                    srw64::backgrounds::configure(directory, capture_directory);
                }
                const auto map_spec = std::filesystem::path(directory) / "srw64-worldmap-hd.json";
                if (std::filesystem::exists(map_spec)) {
                    std::ifstream input(map_spec);
                    input >> worldmap_spec;
                    // The Earth surfaces 5602-5606 of the story world map, 64 px tiles redrawn at 512.
                    const auto region = [](const nlohmann::json& id) { return id.is_number_unsigned() && id >= 5602 && id <= 5606; };
                    const bool regions = worldmap_spec.contains("resources") && !worldmap_spec.at("resources").empty() &&
                        std::all_of(worldmap_spec.at("resources").begin(), worldmap_spec.at("resources").end(), region);
                    if (worldmap_spec.at("schema") != "srw64.worldmap-hd.v1" || worldmap_spec.at("source_tile_size") != 64 || !regions)
                        throw std::runtime_error("Unsupported HD world-map specification");
                    worldmap_tile_size = worldmap_spec.at("replacement_tile_size").get<unsigned>();
                    if (worldmap_tile_size != 512)
                        throw std::runtime_error("Unsupported HD world-map tile density");
                    for (const auto& hash : worldmap_spec.at("hashes")) {
                        const auto value = hash.get<std::string>();
                        if (value.size() != 16 || value.find_first_not_of("0123456789abcdef") != std::string::npos)
                            throw std::runtime_error("Invalid HD world-map hash");
                        worldmap_hashes.push_back(std::stoull(value,nullptr,16));
                    }
                }
            }
            // Scene sprites: HD title frames from the art pack, and the story text drawn
            // natively with or without one. Chained after the portraits.
            srw64::sprites::configure(art ? std::filesystem::path(art) : std::filesystem::path(), capture_directory);
        }
    }
    bool valid() override { return app != nullptr; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
#ifdef __ANDROID__
    // Where a frame's time goes on the phone (docs/design/android-port.md): logged every
    // 60 display lists while the port is tuned.
    struct Timing {
        using clock = std::chrono::steady_clock;
        clock::time_point last{};
        double gap = 0, images = 0, lists = 0, screen = 0, gap_max = 0, lists_max = 0;
        unsigned count = 0, screens = 0;
        static double ms(clock::time_point a, clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
    } timing;
#endif
    void send_dl(const OSTask* task) override {
        srw64::frame_rate::list_sent();
#ifdef __ANDROID__
        const auto t0 = Timing::clock::now();
        if (timing.last != Timing::clock::time_point{}) {
            const double gap = Timing::ms(timing.last, t0);
            timing.gap += gap;
            timing.gap_max = std::max(timing.gap_max, gap);
        }
        timing.last = t0;
#endif
        apply_images();
#ifdef __ANDROID__
        const auto t1 = Timing::clock::now();
#endif
        apply_aspect();
        app->state->rsp->reset();
        app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
        // What this frame fills of the picture, from its draws (wide_map::Shape).
        const auto shape=srw64::wide_map::take_shape();
        // A picture wider than 4:3 keeps the original's rectangles centred until a scene
        // places them wider (wide_map.cpp); a rectangle as wide as the screen stretches
        // across the picture (RT64's own rule): fades, dimming, flashes and letterbox bars,
        // never pictures (the game draws those in tiles). 3D widens: the world map, the
        // title's and the prologue's lines, the battle's ground, units and effects. The
        // same as gEXEnable at the head of the display list; RT64 clears it with every
        // workload. Always on, 4:3 too: the game thread wrote this list for the picture
        // as it was then, and a window just made 4:3 must still read its extended commands
        // as such (read as F3DEX2, a 16-byte one splits in two and its offset word can be a
        // G_BRANCH_Z into nothing). F3DEX2 has no opcode 0x64 of its own.
        app->state->enableExtendedGBI(RT64_EXTENDED_OPCODE);
        const uint32_t start=task->t.data_ptr & 0x3FFFFFF;
        bool native_text=false;
#ifdef SRW64_NATIVE_DIALOGUE
        auto native_frame=srw64::dialogue::take_frame(start,task->t.data_size,display_copy,app->core.RDRAM);
        native_text=bool(native_frame);
        srw64::dialogue::queue_frame(app->state->workloadId+1,native_frame);
        const bool name_cover=srw64::names::request().visible;
        srw64::names::queue_cover(app->state->workloadId+1,name_cover);
#endif
        srw64::wide_map::queue_shape(app->state->workloadId+1,shape);
        // The native dialogue submits an edited copy of the display list.
#ifdef __ANDROID__
        const auto t2 = Timing::clock::now();
#endif
        app->processDisplayLists(native_text ? display_copy.data() : app->core.RDRAM, start, 0, true);
#ifdef __ANDROID__
        const auto t3 = Timing::clock::now();
        timing.images += Timing::ms(t0, t1);
        timing.lists += Timing::ms(t2, t3);
        timing.lists_max = std::max(timing.lists_max, Timing::ms(t2, t3));
        if (++timing.count == 60) {
            fprintf(stderr, "SRW64_TIMING lists=60 gap=%.1f/%.1f images=%.1f process=%.1f/%.1f screens=%u screen=%.1f ms (mean/max)\n",
                    timing.gap / 59, timing.gap_max, timing.images / 60, timing.lists / 60, timing.lists_max,
                    timing.screens, timing.screens ? timing.screen / timing.screens : 0.0);
            timing = Timing{.last = timing.last};
        }
#endif
        srw64::wide_map::queue_shape(app->state->workloadId,shape);
#ifdef SRW64_NATIVE_DIALOGUE
        srw64::dialogue::queue_frame(app->state->workloadId,native_frame);
        srw64::names::queue_cover(app->state->workloadId,name_cover);
#endif
        audit_worldmap();
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {
#ifdef __ANDROID__
        const auto t0 = Timing::clock::now();
        app->updateScreen();
        timing.screen += Timing::ms(t0, Timing::clock::now());
        ++timing.screens;
#else
        app->updateScreen();
#endif
    }
    void shutdown() override { if (app) app->end(); }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
private:
    // A picture wider than the original renders that much more of the original's width
    // (RT64 Manual at the picture's ratio, game_frame.hpp); extended origins reach its
    // edges (Expand: all the way). Otherwise the plain 4:3 picture.
    void configure_aspect(float width) {
        using AspectRatio = RT64::UserConfiguration::AspectRatio;
        const bool wide = width > srw64::frame::kWidth;
        app->userConfig.aspectRatio = wide ? AspectRatio::Manual : AspectRatio::Original;
        app->userConfig.aspectTarget = width / srw64::frame::kHeight;
        app->userConfig.extAspectRatio = wide ? AspectRatio::Expand : AspectRatio::Original;
        aspect_width = width;
        srw64::frame::picture_width = width;
    }
    // The picture follows the window, and the settings window turns it on or off while
    // the game runs; RT64 reads the configuration again for each workload.
    void apply_aspect() {
        uint32_t w, h;
        {
            auto& shared = *app->sharedQueueResources;
            std::scoped_lock lock(shared.configurationMutex);
            w = shared.swapChainWidth; h = shared.swapChainHeight;
        }
        const float width = srw64::frame::width(float(w), float(h));
        if (width == aspect_width) return;
        configure_aspect(width);
        app->updateUserConfig(false);
        fprintf(stderr, "SRW64_ASPECT width=%.2f window=%ux%u\n", width, w, h);
    }
    float aspect_width = 0;
    void apply_images() {
        auto& mode=srw64::presentation::image_mode;
        if(!mode.enabled() || mode.current()==int(mode.requested()))return;
        const bool hd=mode.requested();
        // Matches RT64's configuration-change drain. A mutex alone would let
        // a workload mix original UV scales with replacement descriptors.
        app->workloadQueue->waitForWorkloadId(app->state->workloadId);
        app->presentQueue->waitForPresentId(app->state->presentId);
        app->workloadQueue->waitForIdle();app->presentQueue->waitForIdle();
        {
            std::unique_lock lock(app->textureCache->textureMapMutex);
            app->textureCache->textureMap.replacementMapEnabled=hd;
        }
        worldmap_verified=0;
        mode.acknowledge(hd);
        nlohmann::json status={{"schema","srw64.image-mode.v1"},{"mode",hd?"hd":"original"},
            {"hd_available",true},
            {"model_5600",srw64::marker::replacement_enabled()?"waterdrop":"original"},
            {"vi",srw64_current_vi()},{"workload",app->state->workloadId}};
        const auto pending=capture_directory/"image-mode.pending.json";
        std::ofstream(pending)<<status.dump(2)<<'\n';
        std::filesystem::rename(pending,capture_directory/"image-mode.json");
        std::ofstream(capture_directory/"image-mode-events.jsonl",std::ios::app)<<status.dump()<<'\n';
        fprintf(stderr,"SRW64_IMAGE_MODE %s vi=%llu\n",hd?"hd":"original",(unsigned long long)srw64_current_vi());
    }
    void audit_worldmap() {
        if (worldmap_hashes.empty() || worldmap_verified == worldmap_hashes.size()) return;
        nlohmann::json observed = nlohmann::json::array();
        {
            // Read the exact cache values consumed by RT64's GPU tile builder.
            // Do not call useTexture here: diagnostics must not alter eviction.
            std::unique_lock lock(app->textureCache->textureMapMutex);
            const auto& map = app->textureCache->textureMap;
            if (!map.replacementMapEnabled) return;
            for (size_t i=0; i<worldmap_hashes.size(); ++i) {
                auto found = map.hashMap.find(worldmap_hashes[i]);
                if (found == map.hashMap.end()) continue;
                const auto index = found->second;
                if (!map.textureReplacements[index]) continue;
                const auto original = map.cachedTextureDimensions[index];
                const auto replacement = map.cachedTextureReplacementDimensions[index];
                const auto scale = map.textureScales[index];
                if (original.x != 64 || original.y != 64 || replacement.x != worldmap_tile_size ||
                    replacement.y != worldmap_tile_size || scale.x != worldmap_tile_size/64 || scale.y != scale.x)
                    throw std::runtime_error("HD world map did not reach RT64 at its declared resolution");
                observed.push_back({{"hash",worldmap_spec.at("hashes").at(i)},
                    {"original_size",{original.x,original.y}}, {"replacement_size",{replacement.x,replacement.y}},
                    {"texture_coordinate_scale",{scale.x,scale.y}}});
            }
        }
        if (observed.size() <= worldmap_verified) return;
        worldmap_verified = observed.size();
        nlohmann::json report = {{"schema","srw64.worldmap-texture-runtime.v1"},
            {"evidence_scope","live RT64 texture cache used by the GPU tile builder"},
            {"native_vi",srw64_current_vi()}, {"expected_count",worldmap_hashes.size()},
            {"observed_count",worldmap_verified}, {"textures",observed}};
        std::ofstream(capture_directory/"worldmap-texture-runtime.json") << report.dump(2) << '\n';
        if (worldmap_verified == worldmap_hashes.size())
            fprintf(stderr,"SRW64_WORLDMAP_HD verified=%zu source=64 replacement=%u scale=%u vi=%llu\n",
                worldmap_verified,worldmap_tile_size,worldmap_tile_size/64,(unsigned long long)srw64_current_vi());
    }
    std::unique_ptr<RT64::Application> app;
    nlohmann::json worldmap_spec;
    std::vector<uint64_t> worldmap_hashes;
    size_t worldmap_verified=0;
    unsigned worldmap_tile_size=0;
    std::vector<uint8_t> display_copy;
};
}

std::unique_ptr<ultramodern::renderer::RendererContext> srw64_create_renderer(
    uint8_t* rdram, ultramodern::renderer::WindowHandle handle) {
    return std::make_unique<SRW64Renderer>(rdram, handle);
}

ultramodern::renderer::WindowHandle srw64_create_window(void*) {
#ifdef __ANDROID__
    // The back key and gesture are B (the on-screen controller, frontend.cpp), not the
    // system's "leave the activity", which would end the game without its save.
    SDL_SetHint("SDL_ANDROID_TRAP_BACK_BUTTON", "1");
#endif
#ifdef _WIN32
    // A scaled screen (4K at 150 % or 200 %) gets the picture and the pages at its own pixels,
    // not drawn at the scaled size and stretched by Windows: per-monitor DPI awareness, the
    // window in points as on a Mac (SDL_GetWindowSizeInPixels for the drawable, frontend.cpp).
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | (srw64_audio_enabled() ? SDL_INIT_AUDIO : 0)) != 0) {
        fprintf(stderr, "SRW64_SDL_INIT_FAILED %s\n", SDL_GetError());
        std::abort();
    }
    // UI and dialogue need display pixels regardless of the game's internal render scale.
#ifdef __APPLE__
    constexpr Uint32 surface = SDL_WINDOW_METAL;
#else
    // RT64 creates D3D12 swapchains from the HWND; the Vulkan flag is harmless there.
    constexpr Uint32 surface = SDL_WINDOW_VULKAN;
#endif
    // A handheld fills its screen, Game Mode or Desktop Mode; a desktop opens a window (View
    // menu, F11 or the settings page for full screen, frontend.cpp).
    const bool deck = srw64::on_steam_deck();
    if (deck) srw64::input::pad_family = 1;  // Deck icons before the controller has reported
    // Android: the activity hides the system bars itself (SRW64Activity).
    const bool fills_screen = deck;
    // A Steam Deck's 1280 x 800 is the reference (game_frame.hpp); the Deck fills its screen.
    window = SDL_CreateWindow("Marchwind64", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1280, 800, surface | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                              (fills_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) |
                              (std::getenv("SRW64_BACKGROUND") && std::string(std::getenv("SRW64_BACKGROUND")) == "1" ? SDL_WINDOW_HIDDEN : 0));
    if (!window) {
        fprintf(stderr, "SRW64_WINDOW_FAILED %s\n", SDL_GetError());
        std::abort();
    }
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) std::abort();
#ifdef SRW64_NATIVE_DIALOGUE
    srw64::ui::window_init(window,capture_directory);
    srw64::settings::window_init(window,capture_directory);
#endif
#if defined(__APPLE__)
    view = SDL_Metal_CreateView(window);
    if (!view) std::abort();
    static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(view))->setFramebufferOnly(false);
    return {info.info.cocoa.window, SDL_Metal_GetLayer(view)};
#elif defined(_WIN32)
    return {info.info.win.window, GetCurrentThreadId()};
#else
    return window;
#endif
}

void srw64_after_gpu(plume::RenderCommandList* list, std::function<void(bool completed)> callback) {
#ifdef __APPLE__
    if (metal_backend) {
        static_cast<plume::MetalCommandList*>(list)->mtl->addCompletedHandler([callback = std::move(callback)](MTL::CommandBuffer* command) {
            callback(command->status() == MTL::CommandBufferStatusCompleted);
        });
        return;
    }
#endif
    (void)list;
    after_present.push_back(std::move(callback));
}

void srw64_update_window(void*) {
    srw64::qa::update_window(window, capture_directory, srw64_current_vi());
#ifdef SRW64_NATIVE_DIALOGUE
    srw64::settings::update();
    srw64::settings::control(window,capture_directory);
    srw64::ui::update();
    srw64::settings_window::control(capture_directory);
    srw64::debug::service_main();
    // A native page owns the keyboard: no F6/F8 and no Esc-to-quit meanwhile.
    const bool editing_name=srw64::battle_page::owns_input() || srw64::intermission_page::owns_input() || srw64::upgrade_page::owns_input() || srw64::parts_page::owns_input() || srw64::ability_page::owns_input() || srw64::swap_page::owns_input() || srw64::save_page::owns_input() || srw64::title_page::owns_input() || srw64::names::owns_input() || srw64::link_page::owns_input();
#else
    const bool editing_name=false;
#endif
    // The application's name; a settings file that could not be saved says so after it.
    static bool title_error{};
    static std::string title_label;
#ifdef SRW64_NATIVE_DIALOGUE
    const bool failed=srw64::settings::failed();
    const auto error_label=failed?srw64::localization::catalog().ui("settings_error"):std::string();
#else
    const bool failed=false;
    const std::string error_label;
#endif
    if(failed!=title_error || error_label!=title_label) {
        title_error=failed;title_label=error_label;
        SDL_SetWindowTitle(window,(std::string("Marchwind64")+(failed?" | "+error_label:"")).c_str());
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // One controller at a time: the first one connected, replaced when it leaves.
        if(event.type==SDL_CONTROLLERDEVICEADDED) {
            if(!pad && (pad=SDL_GameControllerOpen(event.cdevice.which))) {
                // Hint icons follow the controller. Steam Input shows games a virtual controller,
                // which SDL reports as an Xbox one unless Steam says it is a PlayStation or
                // Nintendo pad; on the Deck, anything else is the Deck's own controls. The
                // bindings are by position, so every family plays the same defaults.
                const auto type=SDL_GameControllerGetType(pad);
                const char* name=SDL_GameControllerName(pad);
                srw64::input::pad_family=
                    type==SDL_CONTROLLER_TYPE_PS3 || type==SDL_CONTROLLER_TYPE_PS4 || type==SDL_CONTROLLER_TYPE_PS5?2:
                    type==SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO?3:
                    srw64::on_steam_deck() || (name && std::string(name).find("Steam Deck")!=std::string::npos)?1:0;
                fprintf(stderr,"SRW64_PAD name=\"%s\" type=%d family=%d\n",name?name:"",int(type),int(srw64::input::pad_family.load()));
            }
            continue;
        }
        if(event.type==SDL_CONTROLLERDEVICEREMOVED) {
            if(pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==event.cdevice.which){SDL_GameControllerClose(pad);pad=nullptr;}
            continue;
        }
#ifdef SRW64_NATIVE_DIALOGUE
        if(event.type!=SDL_QUIT && srw64::ui::event(event))continue;
        if(srw64::settings::owns_input() && event.type!=SDL_QUIT && !(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE))continue;
#endif
        if(!editing_name && event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F6 && !event.key.repeat &&
           SDL_GetKeyboardFocus()==window && !(event.key.keysym.mod & (KMOD_GUI|KMOD_ALT|KMOD_CTRL)))
            srw64::presentation::image_mode.toggle();
        if(!editing_name && event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F8 && !event.key.repeat &&
           SDL_GetKeyboardFocus()==window && !(event.key.keysym.mod & (KMOD_GUI|KMOD_ALT|KMOD_CTRL)))
            srw64::mini_stage::hotkey();
#ifdef SRW64_NATIVE_DIALOGUE
        // F5 reads the dialogue text files again (docs/guide/dialogue-text.md).
        if(!editing_name && event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_F5 && !event.key.repeat &&
           SDL_GetKeyboardFocus()==window && !(event.key.keysym.mod & (KMOD_GUI|KMOD_ALT|KMOD_CTRL)))
            srw64::dialogue::request_reload();
#endif
        if (event.type == SDL_QUIT || (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE && event.window.windowID==SDL_GetWindowID(window)) || (!editing_name && event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && SDL_GetKeyboardFocus() == window)) {
            fprintf(stderr, "SRW64_WINDOW_QUIT event=%u vi=%llu\n", event.type, (unsigned long long)srw64_current_vi());
            ultramodern::quit();
        }
    }
    // Presses from the debug interface take the same paths as the SDL events
    // above. F7 goes through the same SDL composition and repeat gates.
    for (const auto key : srw64::debug::keyboard().take_presses()) {
#ifdef SRW64_NATIVE_DIALOGUE
        {
            SDL_Event e{};e.type=SDL_KEYDOWN;
            e.key.keysym.sym=SDL_GetKeyFromName(std::string(srw64::debug::key_names[key]).c_str());
            // With the physical key too: the bindings (input_bindings.hpp) go by scancode.
            e.key.keysym.scancode=virtual_scancode(key);
            const bool consumed=srw64::ui::event(e);e.type=SDL_KEYUP;srw64::ui::event(e);
            if(consumed)continue;
        }
        if (srw64::settings::owns_input()) continue;
#endif
        if (editing_name) continue;
        if (key == srw64::debug::F6) srw64::presentation::image_mode.toggle();
        else if (key == srw64::debug::F8) srw64::mini_stage::hotkey();
#ifdef SRW64_NATIVE_DIALOGUE
        else if (key == srw64::debug::F5) srw64::dialogue::request_reload();
#endif
        else if (key == srw64::debug::Escape) {
            fprintf(stderr, "SRW64_WINDOW_QUIT event=debug vi=%llu\n", (unsigned long long)srw64_current_vi());
            ultramodern::quit();
        }
    }
    const uint32_t virtual_keys = srw64::debug::keyboard().held();
    uint32_t state = 0;
#ifdef SRW64_NATIVE_DIALOGUE
    int key_count=0;const auto* modal_keys=SDL_GetKeyboardState(&key_count);
    bool keys_released=true;
    for(int i=0;i<key_count;++i)if(modal_keys[i]){keys_released=false;break;}
    if(virtual_keys)keys_released=false;
    srw64::settings::release_input_when(keys_released);
#endif
    // The bindings, copied again only when the Controls page changes them.
    static srw64::input::Bindings bindings = srw64::input::live_bindings().get();
    static uint64_t bindings_revision = srw64::input::live_bindings().revision();
    if (const auto revision = srw64::input::live_bindings().revision(); revision != bindings_revision) {
        bindings = srw64::input::live_bindings().get();
        bindings_revision = revision;
    }
    if (!editing_name) {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        // Physical positions stay stable across keyboard layouts. Do not pass
        // macOS application shortcuts through as game input. Physical keys need
        // window focus; keys held through the debug interface do not, since it
        // drives the game while another application is in front.
        const bool physical = SDL_GetKeyboardFocus() == window && !(SDL_GetModState() & (KMOD_GUI | KMOD_ALT | KMOD_CTRL));
        // The keys are the player's bindings (input_bindings.hpp). Keys the debug interface
        // holds keep the classic layout whatever is bound, as the native pages take them.
        if (physical)
            state |= srw64::input::key_mask(bindings, [&](int key) { return key >= 0 && key < SDL_NUM_SCANCODES && keys[key]; });
        static const auto classic = srw64::input::classic_keys();
        state |= srw64::input::key_mask(classic, [&](int key) {
            for (unsigned k = 0; k < srw64::debug::KeyCount; ++k)
                if ((virtual_keys & srw64::debug::bit(srw64::debug::Key(k))) && virtual_scancode(srw64::debug::Key(k)) == key) return true;
            return false;
        });
    }
    // Controller: the same N64 mask as the keyboard table above. Native pages
    // that own the pad read it through srw64_pad_state(); the game never sees
    // it then, because each page's input() filter swallows the buttons.
    uint32_t buttons = 0;
    // The player's controller bindings (input_bindings.hpp). The defaults keep the host's
    // own buttons (docs/design/steam-deck-controls.md): View opens the settings window
    // (Steam Deck: no keyboard needed), the triggers serve dialogue and the map's enemy
    // cycling, and Z is Y alone, so L2 + Menu cannot make the original's quit-the-stage Z + START.
    if (pad)
        buttons = srw64::input::pad_mask(bindings,
            [&](uint8_t b) { return SDL_GameControllerGetButton(pad, SDL_GameControllerButton(b)) != 0; },
            [&](uint8_t a) { return int(SDL_GameControllerGetAxis(pad, SDL_GameControllerAxis(a))); });
    buttons |= srw64::debug::pad().load(std::memory_order_relaxed);
#ifdef SRW64_NATIVE_DIALOGUE
    // The on-screen controller on a phone (touch_pad.hpp), as a controller.
    buttons |= srw64::ui::touch_buttons();
#endif
    // Controller play hides the pointer; the mouse or touch screen brings it back.
    static int cursor=-1;
    const int wanted=srw64::input::pad_hints ? SDL_DISABLE : SDL_ENABLE;
    if(cursor!=wanted)SDL_ShowCursor(cursor=wanted);
    pad_state.store(buttons, std::memory_order_relaxed);
    keyboard_state.store(state | buttons, std::memory_order_relaxed);
}
uint32_t srw64_pad_state() { return pad_state.load(std::memory_order_relaxed); }
std::string srw64_pad_name() {
    const char* name = pad ? SDL_GameControllerName(pad) : nullptr;
    if (!name) return "";
    // The Deck's own controls reach a game through Steam Input's virtual controller.
    if (srw64::input::pad_family == 1 && std::string(name).starts_with("Steam Virtual")) return "Steam Deck";
    return name;
}
uint32_t srw64_keyboard_state() { return keyboard_state.load(std::memory_order_relaxed); }

nlohmann::json srw64_window_status() {
    if (!window) return nullptr;
    int width = 0, height = 0, pixel_width = 0, pixel_height = 0;
    SDL_GetWindowSize(window, &width, &height);
#ifdef __APPLE__
    SDL_Metal_GetDrawableSize(window, &pixel_width, &pixel_height);
#else
    SDL_Vulkan_GetDrawableSize(window, &pixel_width, &pixel_height);
#endif
    return {{"focused", SDL_GetKeyboardFocus() == window}, {"width", width}, {"height", height},
            {"fullscreen", (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0},
            {"pixel_width", pixel_width}, {"pixel_height", pixel_height}, {"title", SDL_GetWindowTitle(window)}};
}

// The graphics API and the GPU as plume describes it, for a bug report (bug_report.hpp).
nlohmann::json srw64_graphics_info() {
    using API = RT64::UserConfiguration::GraphicsAPI;
    const char* api = graphics_api < 0 ? "none" : graphics_api == int(API::D3D12) ? "D3D12" :
                      graphics_api == int(API::Vulkan) ? "Vulkan" : graphics_api == int(API::Metal) ? "Metal" : "other";
    nlohmann::json info = {{"api", api}};
    if (capture_device) {
        const auto& d = capture_device->getDescription();
        using Vendor = plume::RenderDeviceVendor;
        info["device"] = d.name;
        info["vendor"] = d.vendor == Vendor::AMD ? "AMD" : d.vendor == Vendor::NVIDIA ? "NVIDIA" : d.vendor == Vendor::INTEL ? "Intel" :
                         d.vendor == Vendor::APPLE ? "Apple" : "other";
        info["driver_version"] = d.driverVersion;
        info["video_memory_mb"] = d.dedicatedVideoMemory >> 20;
    }
    return info;
}

nlohmann::json srw64_window_control(const nlohmann::json& params) {
    if (!window) throw std::runtime_error("the game window is not open yet");
    if (params.contains("width") || params.contains("height")) {
        int width = 0, height = 0;
        SDL_GetWindowSize(window, &width, &height);
        width = params.value("width", width);
        height = params.value("height", height);
        // Same range as the window QA control (window-control.txt).
        if (width < 640 || width > 2560 || height < 480 || height > 1600)
            throw std::invalid_argument("window size must be 640..2560 x 480..1600");
        SDL_SetWindowSize(window, width, height);
    }
    // Bring the game to the front: the paths that need real window focus
    // (physical keys, the name page's key check, first-click activation).
    if (params.value("front", false)) SDL_RaiseWindow(window);
#ifdef SRW64_NATIVE_DIALOGUE
    if (params.value("close", false)) srw64::debug_ui::close_game_window();
#endif
    return srw64_window_status();
}

void srw64_keyboard_input(uint16_t* buttons, float* x, float* y) {
    uint32_t state = keyboard_state.load(std::memory_order_relaxed) | *buttons;
#ifdef SRW64_NATIVE_DIALOGUE
    state=srw64::settings::filter_input(state);
#endif
    // START held while the original boots opens its Controller Pak manager, whose
    // osPfs calls the runtime lacks (the host aborts). A START that is down in the
    // first two seconds is ignored until it has been released.
    static bool boot_start = true;
    if (boot_start && srw64_current_vi() >= 120 && !(state & 0x1000)) boot_start = false;
    if (boot_start) state &= ~0x1000U;
    *buttons = static_cast<uint16_t>(state);
    *x = float(bool(state & (1U << 19))) - float(bool(state & (1U << 18)));
    *y = float(bool(state & (1U << 16))) - float(bool(state & (1U << 17)));
}

void srw64_destroy_window() {
#ifdef SRW64_NATIVE_DIALOGUE
    srw64::settings::shutdown();
    srw64::settings_window::shutdown();
    srw64::ui::shutdown();
#endif
    keyboard_state = 0;
    if (pad) { SDL_GameControllerClose(pad); pad = nullptr; }
    srw64_close_audio();
#ifdef __APPLE__
    SDL_Metal_DestroyView(view);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void srw64_set_capture_directory(const std::filesystem::path& path) { capture_directory = path; }
void srw64_set_capture_clock(const char* name) { capture_clock = name; }
