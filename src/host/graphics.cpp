#define HLSL_CPU
#ifndef SRW64_GL_ONLY
#include "hle/rt64_application.h"
#endif
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
#ifndef SRW64_GL_ONLY
#include "native_gpu.hpp"
#endif
#include "native_portrait.hpp"
#include "native_background.hpp"
#include "native_sprite.hpp"
#include "presentation/image_mode.hpp"
#include "console_log.hpp"
#include "gpu_report.hpp"
#include "startup_failure.hpp"
#ifdef SRW64_NATIVE_DIALOGUE
#include "presentation_settings.hpp"
#include "settings_window.hpp"
#include "debug_server.hpp"
#include "debug_ui.hpp"
#include "ui/frontend.hpp"
#endif
#ifndef SRW64_GL_ONLY
#include "hle/rt64_workload_queue.h"
#include "hle/rt64_present_queue.h"
#include "../include/rt64_extended_gbi.h"
#endif
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
#include "notices.hpp"
#include "gbi_stats.hpp"
#include "ultramodern/ultramodern.hpp"
#endif
#include "librecomp/game.hpp"
#ifdef SRW64_GL_RENDERER
#include "../gl/display_list.hpp"
#include "../gl/gl_renderer.hpp"
#include "native_gl.hpp"
#endif
#ifndef SRW64_GL_ONLY
#include "rt64_render_hooks.h"
#ifdef __APPLE__
#include "plume_metal.h"
#endif
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
#include <map>
#include <atomic>
#include <cstring>
#include <ctime>
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
#ifndef SRW64_GL_ONLY
plume::RenderDevice* capture_device;
#endif
int graphics_api = -1;  // RT64's chosen API (RT64::UserConfiguration::GraphicsAPI), for srw64_graphics_info
std::filesystem::path capture_directory;
std::vector<std::string> gpu_names;  // gpu_report's adapters, for the failure message
// The game cannot draw (no window, or no device RT64 can use): tell the player in their
// language (startup_failure.hpp) and leave, without the crash dump an abort would add to
// what console.log already says. Tool and test runs, not interactive, keep the abort.
[[noreturn]] void graphics_failure(const std::string& detail) {
    const char* interactive = std::getenv("SRW64_INTERACTIVE");
    if (!interactive || std::string(interactive) != "1") std::abort();
    std::string locale = "zh-Hans";
    if (const char* path = std::getenv("SRW64_PRESENTATION_SETTINGS")) {
        std::ifstream file(std::filesystem::path(reinterpret_cast<const char8_t*>(path)));
        const auto saved = nlohmann::json::parse(file, nullptr, false);
        if (saved.is_object() && saved.contains("locale") && saved["locale"].is_string()) locale = saved["locale"].get<std::string>();
    }
    std::string log;
    if (!capture_directory.empty()) {
        const auto text = (capture_directory / "console.log").u8string();
        log.assign(text.begin(), text.end());
    }
    const auto message = srw64::startup_failure::message(locale, gpu_names, log, detail);
#ifdef _WIN32
    // Topmost, over the game's own window. Not owned by it: that window's thread (the main
    // thread) waits for this one (the renderer's), so it could not take the box's messages.
    std::wstring text(size_t(MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, text.data(), int(text.size()));
    MessageBoxW(nullptr, text.c_str(), L"Marchwind64", MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Marchwind64", message.c_str(), nullptr);
#endif
    fprintf(stderr, "SRW64_GRAPHICS_FAILURE_SHOWN\n");
    srw64::console_log::flush_now();
    std::_Exit(3);
}
uint64_t presented_frames{};
std::string capture_clock = "native_vi_at_draw";
// F12 or Print Screen: the next present goes to screenshots/ in the user folder.
std::atomic<bool> player_shot_requested{};
// The Enter of an Alt + Enter (full screen, frontend.cpp) is not START until it is let go,
// whichever key comes up first.
SDL_Scancode alt_enter_key = SDL_SCANCODE_UNKNOWN;
// GPU completion thread: writes the picture as a dated PNG and says where it went.
void save_player_screenshot(const uint8_t* rgba, uint32_t width, uint32_t height) {
#ifdef SRW64_NATIVE_DIALOGUE
    const auto folder = srw64::settings::data_folder() / "screenshots";
#else
    const auto folder = capture_directory / "screenshots";
#endif
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
    auto path = folder / ("Marchwind64-" + std::string(stamp) + ".png");
    for (int n = 2; std::filesystem::exists(path, error); ++n)
        path = folder / ("Marchwind64-" + std::string(stamp) + "-" + std::to_string(n) + ".png");
    // Opaque whatever the swap chain's alpha holds.
    std::vector<uint8_t> opaque(rgba, rgba + size_t(width) * height * 4);
    for (size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 255;
    const bool written = stbi_write_png(path.string().c_str(), width, height, 4, opaque.data(), width * 4);
    fprintf(stderr, "SRW64_SCREENSHOT %s %s\n", written ? "saved" : "failed", path.string().c_str());
#ifdef SRW64_NATIVE_DIALOGUE
    auto text = srw64::localization::catalog().ui(written ? "screenshot_saved" : "screenshot_failed");
    if (const auto at = text.find("{path}"); at != std::string::npos) text.replace(at, 6, path.string());
    srw64::notices::post("screenshot", text);
#endif
}
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
// RT64: the scene picture, its present hook and the renderer (none in the GL-only host).
#ifndef SRW64_GL_ONLY
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
    // For the frame-pacing test (docs/design/60fps.md §9): SRW64_PRESENT_HOOK=0 draws
    // nothing of ours in RT64's present, to see what our drawing costs the present thread.
    static const bool hook_off = std::getenv("SRW64_PRESENT_HOOK") && std::string(std::getenv("SRW64_PRESENT_HOOK")) == "0";
    if (hook_off) { ++presented_frames; srw64::frame_rate::present_sent(); return; }
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
    srw64::frame_rate::present_sent();
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
    // The player's screenshot key (F12, Print Screen) takes this one too, the window as shown.
    const bool player_shot = player_shot_requested.exchange(false);
    // The Vita renderer's reference picture of a dumped list (gbi_stats.hpp).
    std::string dump_shot;
#ifdef SRW64_NATIVE_DIALOGUE
    dump_shot = srw64::gbi_stats::take_shot(workload);
#endif
    if (!trace && !debug_shot && !recording && !player_shot && dump_shot.empty()) return;
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
    srw64_after_gpu(list, [buffer, width, height, row_pixels, bgra, path, frame, vi, clock_name, image_mode, workload, trace, dialogue_trace, debug_shot, recording, player_shot, dump_shot](bool completed) {
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
        if (player_shot) save_player_screenshot(rgba.data(), width, height);
        if (!dump_shot.empty()) stbi_write_png(dump_shot.c_str(), width, height, 4, rgba.data(), width * 4);
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
        srw64::backgrounds::initialize(capture_directory);
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
        // Frames between the game's 30 (RT64 interpolation, docs/design/60fps.md): the
        // player's setting (refresh_rate, the original 30 by default), or for tests
        // SRW64_REFRESH_RATE=display|original|<rate>.
        app->userConfig.refreshRate = refresh_rate_setting();
        app->userConfig.refreshRateTarget = 60;   // the "60" choice (Manual), capped at the display's rate by RT64
        if (const char* rate = std::getenv("SRW64_REFRESH_RATE")) {
            const std::string value = rate;
            char* end = nullptr;
            const long target = std::strtol(rate, &end, 10);
            if (value == "display") app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Display;
            else if (value == "original") app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
            else if (end != rate && !*end && target >= 10 && target <= 1000) {
                app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Manual;
                app->userConfig.refreshRateTarget = int(target);
            }
            refresh_rate_fixed = true;
        }
        fprintf(stderr, "SRW64_REFRESH_RATE mode=%d target=%d\n", int(app->userConfig.refreshRate), app->userConfig.refreshRateTarget);
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
#ifdef SRW64_NATIVE_DIALOGUE
        // The player's switch (settings msaa), read before the device starts.
        if (!srw64::settings::msaa_at_start()) app->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None;
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
            graphics_failure({});
        }
        else {
            if (const char* directory = std::getenv("SRW64_TEXTURE_DUMP")) {
                std::filesystem::create_directories(directory);
                app->state->dumpingTexturesDirectory = directory;
            }
            const char* art = std::getenv("SRW64_ART_PACK");
            const char* hd_available = std::getenv("SRW64_HD_AVAILABLE");
            if (hd_available && std::string(hd_available)=="0") {
                srw64::presentation::image_mode.configure(false);
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
                    // The compiled/released RT64 manifest keeps each texture's family.
                    // Only map unit icons are affected by their independent preference.
                    std::ifstream file(std::filesystem::path(directory) / "rt64.json");
                    nlohmann::json database;
                    file >> database;
                    for(const auto& row:database.at("textures"))if(row.value("kind","")=="icon")
                        map_icon_hashes.push_back(std::stoull(row.at("hashes").at("rt64").get<std::string>(),nullptr,16));
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
        apply_refresh_rate();
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
        // The display list's commands and modes, counted for the Vita study (gbi_stats.hpp;
        // only with the debug interface and SRW64_GBI_STATS). Before RT64 reads the list:
        // the game thread, told the RSP is done, may already be rewriting the sub-lists
        // it keeps at fixed addresses.
        srw64::gbi_stats::record(native_text ? display_copy.data() : app->core.RDRAM, app->core.RDRAM, start, task->t.data_size);
        srw64::gbi_stats::dump(native_text ? display_copy.data() : app->core.RDRAM, app->core.RDRAM, start, task->t.data_size, app->state->workloadId+1);
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
    void shutdown() override { srw64::gbi_stats::flush(); if (app) app->end(); }
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
    // The player's refresh-rate setting as RT64's; follows a change while running.
    bool refresh_rate_fixed = false;
    RT64::UserConfiguration::RefreshRate refresh_rate_setting() const {
        auto rate = srw64::settings::RefreshRate::Original;
#ifdef SRW64_NATIVE_DIALOGUE
        rate = srw64::settings::refresh_rate();
#endif
        // 60 is RT64's Manual target (refreshRateTarget, set once at start).
        return rate == srw64::settings::RefreshRate::Original ? RT64::UserConfiguration::RefreshRate::Original :
               rate == srw64::settings::RefreshRate::Sixty ? RT64::UserConfiguration::RefreshRate::Manual :
                                                              RT64::UserConfiguration::RefreshRate::Display;
    }
    void apply_refresh_rate() {
        if (refresh_rate_fixed) return;
        const auto wanted = refresh_rate_setting();
        if (app->userConfig.refreshRate == wanted) return;
        app->userConfig.refreshRate = wanted;
        app->updateUserConfig(false);
        fprintf(stderr, "SRW64_REFRESH_RATE mode=%d\n", int(wanted));
    }
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
    std::vector<uint64_t> map_icon_hashes;
    int applied_map_icons = -1;
    void apply_images() {
        auto& mode=srw64::presentation::image_mode;
        bool hd_icons=true;
#ifdef SRW64_NATIVE_DIALOGUE
        hd_icons=srw64::settings::hd_map_unit_icons();
#endif
        if(!mode.enabled() || (mode.current()==int(mode.requested()) && applied_map_icons==int(hd_icons)))return;
        const bool hd=mode.requested();
        // Matches RT64's configuration-change drain. A mutex alone would let
        // a workload mix original UV scales with replacement descriptors.
        app->workloadQueue->waitForWorkloadId(app->state->workloadId);
        app->presentQueue->waitForPresentId(app->state->presentId);
        app->workloadQueue->waitForIdle();app->presentQueue->waitForIdle();
        {
            std::unique_lock lock(app->textureCache->textureMapMutex);
            auto& map=app->textureCache->textureMap;
            map.replacementMapEnabled=hd;
            if(applied_map_icons!=int(hd_icons)) {
                for(const auto hash:map_icon_hashes) {
                    if(hd_icons)map.replacementBlockedHashes.erase(hash);
                    else map.replacementBlockedHashes.insert(hash);
                    // Refresh cached descriptors, including those already seen on this map.
                    if(const auto it=map.hashMap.find(hash);it!=map.hashMap.end())++map.versions[it->second];
                }
                ++map.globalVersion;
            }
        }
        applied_map_icons=hd_icons;
        worldmap_verified=0;
        mode.acknowledge(hd);
        nlohmann::json status={{"schema","srw64.image-mode.v1"},{"mode",hd?"hd":"original"},
            {"hd_available",true},
            {"map_unit_icons",hd && hd_icons?"hd":"original"},
            {"map_unit_icons_preference",hd_icons?"hd":"original"},
            {"map_unit_icon_hashes",map_icon_hashes.size()},
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
#endif  // SRW64_GL_ONLY
}

#ifdef SRW64_GL_RENDERER
namespace {
// The PS Vita's renderer on the desktop (docs/design/psv-port.md, V1): with
// SRW64_RENDERER=gl the game's display lists go through src/gl's interpreter and an OpenGL
// 2.1 context on the window instead of RT64. The host's own layers (dialogue text, pages,
// window frames, HD art) are not drawn yet; the debug interface's screenshots are.
SDL_GLContext gl_context = nullptr;
bool gl_mode() {
#ifdef SRW64_GL_ONLY
    return true;
#else
    static const bool value = std::getenv("SRW64_RENDERER") && std::string(std::getenv("SRW64_RENDERER")) == "gl";
    return value;
#endif
}
std::string gl_device;   // GL_RENDERER, for srw64_graphics_info

class GLRenderer final : public ultramodern::renderer::RendererContext {
public:
    explicit GLRenderer(uint8_t* rdram) : rdram(rdram) {
        // The context was made on the window thread (srw64_create_window); it is this
        // thread's from here on, the graphics thread's, which also presents.
#ifdef __vita__
        // vitaGL owns the screen (SDL's window only carries input): 960 x 544, the shader
        // compiler of libshacccg.suprx. Its pools have fixed sizes: given a threshold
        // instead, it takes every byte above it, and the game's threads and the system's
        // own allocations have nothing left (Vita3K fell over at the first frame).
        vglInitWithCustomSizes(0, 960, 544, 24 * 1024 * 1024, 96 * 1024 * 1024, 16 * 1024 * 1024, 0, SCE_GXM_MULTISAMPLE_NONE);
        vglWaitVblankStart(GL_FALSE);
        // VitaSDK's vitaGL draws its own splash screen from a thread until the first frame is
        // presented; the game takes a while to its first picture, and Vita3K falls over under
        // that thread. A black frame now ends it.
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);
#else
        if (SDL_GL_MakeCurrent(window, gl_context) != 0) graphics_failure(SDL_GetError());
        SDL_GL_SetSwapInterval(0);
#endif
#ifdef __vita__
        int scale = 2;   // 640 x 480 targets, shown 725 x 544
#else
        int scale = 3;
#endif
        if (const char* value = std::getenv("SRW64_GL_SCALE")) scale = std::clamp(std::atoi(value), 1, 8);
        renderer = std::make_unique<srw64::n64gl::Renderer>(srw64::n64gl::kPictureWidth * scale, srw64::n64gl::kPictureHeight * scale);
        interpreter = std::make_unique<srw64::n64gl::Interpreter>([this](uint64_t key) { return renderer->known(key); });
        // The host's tagged draws: the tag's second word is the draw's id (two letters and a number).
        interpreter->classify([](uint32_t w0, uint32_t w1) { return (w0 >> 16) == 0x0053 ? w1 : 0u; });
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;
        fprintf(stderr, "SRW64_GRAPHICS_API gl %s / %s scale=%d\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER), scale);
        gl_device = std::string((const char*)glGetString(GL_VERSION)) + " / " + (const char*)glGetString(GL_RENDERER);
#ifdef SRW64_NATIVE_DIALOGUE
        srw64::ui::render_init_gl();
#endif
        // The original pictures: the Vita carries no HD art (docs/design/psv-port.md). Not
        // original_only(), which the HD layers read as "no switch", that is HD where they can.
        srw64::presentation::image_mode.configure(false);
        // The host's tagged draws (window frames, story and title text) from the modules that
        // tag them, as SRW64Renderer configures them; HD layers beyond these stay off here.
        srw64::hdmap::configure(capture_directory);
        const char* art = std::getenv("SRW64_ART_PACK");
        srw64::sprites::configure(art ? std::filesystem::path(art) : std::filesystem::path(), capture_directory);
        renderer->native_hook([](const srw64::n64gl::Command& command, int width, int height) {
            srw64::native_gl::draw(command, width, height);
        });
        // The picture stays 4:3 on this path for now: the host's layers follow it.
        srw64::frame::wide = false;
    }
    bool valid() override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        srw64::frame_rate::list_sent();
        const uint32_t start = task->t.data_ptr & 0x3FFFFFF;
        bool native_text = false;
#ifdef SRW64_NATIVE_DIALOGUE
        ++workload;
        auto native_frame = srw64::dialogue::take_frame(start, task->t.data_size, display_copy, rdram);
        native_text = bool(native_frame);
        srw64::dialogue::queue_frame(workload, native_frame);
        srw64::names::queue_cover(workload, srw64::names::request().visible);
        srw64::gbi_stats::record(native_text ? display_copy.data() : rdram, rdram, start, task->t.data_size);
#endif
        frame.clear();
        interpreter->run(rdram, start, frame, native_text ? display_copy.data() : nullptr, native_text ? task->t.data_size : 0);
        renderer->draw(frame);
        renderer->collect();
        srw64::native_gl::end_frame();
        // The workload whose overlays go with this colour image when the VI shows it.
        image_workloads[frame.color_image] = workload;
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {
        srw64::frame::wide = false;
        int width = 0, height = 0;
#ifdef __vita__
        width = 960; height = 544;
#else
        SDL_GL_GetDrawableSize(window, &width, &height);
#endif
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        // The 4:3 picture as large as the window allows, centred.
        const int picture_width = std::min(width, height * 4 / 3), picture_height = picture_width * 3 / 4;
        const uint32_t origin = ultramodern::renderer::get_vi_regs()->VI_ORIGIN_REG & 0x3FFFFFF;
        renderer->present(origin, (width - picture_width) / 2, (height - picture_height) / 2, picture_width, picture_height, width, height);
#ifdef SRW64_NATIVE_DIALOGUE
        const auto shown = image_workloads.find(renderer->image_at(origin));
        const uint64_t shown_workload = shown != image_workloads.end() ? shown->second : 0;
        if (shown_workload) srw64::dialogue::gl_draw(shown_workload, uint32_t(width), uint32_t(height));
        // The name page's cover, then the shared UI's pages over everything (capture_frame's order).
        const bool name_cover = shown_workload && srw64::names::frame_cover(shown_workload);
        if (name_cover) {
            glClearColor(.028f, .045f, .07f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (srw64::ui::draw_gl(width, height, name_cover)) srw64::ui::presented();
        if (shown_workload) srw64::names::cover_presented(shown_workload, name_cover);
#endif
        const uint64_t frame_number = ++presented_frames;
        srw64::frame_rate::present_sent();
        // The window as shown, read back for the debug interface's screenshot and recording
        // and the player's F12, as capture_frame does on RT64.
        const auto shot = srw64::debug::screenshots().take();
        const bool recording = srw64::debug::recording().active();
        const bool player_shot = player_shot_requested.exchange(false);
        if (shot || recording || player_shot) {
            std::vector<uint8_t> pixels(size_t(width) * height * 4), rows(pixels.size());
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (int y = 0; y < height; ++y)
                std::memcpy(rows.data() + size_t(y) * width * 4, pixels.data() + size_t(height - 1 - y) * width * 4, size_t(width) * 4);
            for (size_t i = 3; i < rows.size(); i += 4) rows[i] = 255;
            if (recording) srw64::debug::recording().add(rows.data(), uint32_t(width), uint32_t(height));
            if (player_shot) save_player_screenshot(rows.data(), uint32_t(width), uint32_t(height));
            if (shot) {
                const bool written = stbi_write_png(shot->path.string().c_str(), width, height, 4, rows.data(), width * 4);
                srw64::debug::screenshots().finish(shot->id, written
                    ? nlohmann::json({{"path", shot->path.string()}, {"present", frame_number}, {"vi", srw64_current_vi()},
                                      {"width", width}, {"height", height}, {"renderer", "gl"}})
                    : nlohmann::json({{"error", "cannot write " + shot->path.string()}}));
            }
        }
#ifdef __vita__
        vglSwapBuffers(GL_FALSE);
#else
        SDL_GL_SwapWindow(window);
#endif
    }
    void shutdown() override {
        srw64::gbi_stats::flush();
#ifdef SRW64_NATIVE_DIALOGUE
        srw64::ui::render_shutdown_gl();
        srw64::dialogue::gl_shutdown();
#endif
        srw64::native_gl::shutdown();
        srw64::sprites::shutdown();
        srw64::hdmap::shutdown();
        renderer.reset();
        interpreter.reset();
    }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
private:
    uint8_t* rdram;
    std::unique_ptr<srw64::n64gl::Renderer> renderer;
    std::unique_ptr<srw64::n64gl::Interpreter> interpreter;
    srw64::n64gl::Frame frame;
    std::vector<uint8_t> display_copy;
    uint64_t workload = 0;
    std::map<uint32_t, uint64_t> image_workloads;
};
}  // namespace
#endif

std::unique_ptr<ultramodern::renderer::RendererContext> srw64_create_renderer(
    uint8_t* rdram, ultramodern::renderer::WindowHandle handle) {
#ifdef SRW64_GL_RENDERER
    if (gl_mode()) return std::make_unique<GLRenderer>(rdram);
#endif
#ifdef SRW64_GL_ONLY
    (void)handle;
    return nullptr;
#else
    return std::make_unique<SRW64Renderer>(rdram, handle);
#endif
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
    gpu_names = srw64::gpu_report::log();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | (srw64_audio_enabled() ? SDL_INIT_AUDIO : 0)) != 0) {
        fprintf(stderr, "SRW64_SDL_INIT_FAILED %s\n", SDL_GetError());
        std::abort();
    }
    // UI and dialogue need display pixels regardless of the game's internal render scale.
#ifdef __APPLE__
    constexpr Uint32 surface = SDL_WINDOW_METAL;
#elif defined(_WIN32)
    // Plume makes both the D3D12 and the Vulkan swapchain from the HWND. SDL_WINDOW_VULKAN
    // would load Vulkan first and fail the window on a PC without a working Vulkan driver
    // ("doesn't implement the VK_KHR_surface extension"), before RT64 tries D3D12.
    constexpr Uint32 surface = 0;
#else
    constexpr Uint32 surface = SDL_WINDOW_VULKAN;
#endif
    // A handheld fills its screen, Game Mode or Desktop Mode; a desktop opens a window (View
    // menu, F11 or the settings page for full screen, frontend.cpp).
    const bool deck = srw64::on_steam_deck();
    if (deck) srw64::input::pad_family = 1;  // Deck icons before the controller has reported
    // Android: the activity hides the system bars itself (SRW64Activity).
    const bool fills_screen = deck;
    Uint32 surface_flags = surface;
#ifdef SRW64_GL_RENDERER
    if (gl_mode()) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        surface_flags = SDL_WINDOW_OPENGL;
    }
#ifdef __vita__
    surface_flags = 0;   // vitaGL, not SDL, draws (GLRenderer)
#endif
#endif
    // A Steam Deck's 1280 x 800 is the reference (game_frame.hpp); the Deck fills its screen.
    // The Vita's window is its screen, 960 x 544, which the shared UI lays out for.
#ifdef __vita__
    constexpr int window_width = 960, window_height = 544;
#else
    constexpr int window_width = 1280, window_height = 800;
#endif
    window = SDL_CreateWindow("Marchwind64", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              window_width, window_height, surface_flags | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                              (fills_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) |
                              (std::getenv("SRW64_BACKGROUND") && std::string(std::getenv("SRW64_BACKGROUND")) == "1" ? SDL_WINDOW_HIDDEN : 0));
    if (!window) {
        const std::string error = SDL_GetError();
        fprintf(stderr, "SRW64_WINDOW_FAILED %s\n", error.c_str());
        graphics_failure(error);
    }
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
#ifndef __vita__   // the Vita's SDL has no window-manager details to give
    if (!SDL_GetWindowWMInfo(window, &info)) std::abort();
#endif
#ifdef SRW64_NATIVE_DIALOGUE
    srw64::ui::window_init(window,capture_directory);
    srw64::settings::window_init(window,capture_directory);
#endif
#ifdef SRW64_GL_RENDERER
    if (gl_mode()) {
        // Made here, on the window thread, and handed to the graphics thread (GLRenderer).
#ifndef __vita__
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context) graphics_failure(SDL_GetError());
        SDL_GL_MakeCurrent(window, nullptr);
#endif
#if defined(__APPLE__)
        return {info.info.cocoa.window, nullptr};
#elif defined(_WIN32)
        return {info.info.win.window, GetCurrentThreadId()};
#else
        return window;
#endif
    }
#endif
#if defined(__APPLE__) && !defined(SRW64_GL_ONLY)
    view = SDL_Metal_CreateView(window);
    if (!view) std::abort();
    static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(view))->setFramebufferOnly(false);
    return {info.info.cocoa.window, SDL_Metal_GetLayer(view)};
#elif defined(__APPLE__)
    return {info.info.cocoa.window, nullptr};
#elif defined(_WIN32)
    return {info.info.win.window, GetCurrentThreadId()};
#else
    return window;
#endif
}

void srw64_after_gpu(plume::RenderCommandList* list, std::function<void(bool completed)> callback) {
#if defined(__APPLE__) && !defined(SRW64_GL_ONLY)
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
        if(event.type==SDL_KEYDOWN && (event.key.keysym.mod & KMOD_ALT) &&
           (event.key.keysym.scancode==SDL_SCANCODE_RETURN || event.key.keysym.scancode==SDL_SCANCODE_KP_ENTER))
            alt_enter_key=event.key.keysym.scancode;
        // Screenshots on every screen, the settings window and our pages included.
        if(event.type==SDL_KEYDOWN && !event.key.repeat && (event.key.keysym.sym==SDLK_F12 || event.key.keysym.sym==SDLK_PRINTSCREEN) &&
           !(event.key.keysym.mod & (KMOD_GUI|KMOD_ALT|KMOD_CTRL|KMOD_SHIFT))) {
            player_shot_requested=true;
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
        if (key == srw64::debug::F12) { player_shot_requested = true; continue; }
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
        if (alt_enter_key != SDL_SCANCODE_UNKNOWN && !keys[alt_enter_key]) alt_enter_key = SDL_SCANCODE_UNKNOWN;
        // Physical positions stay stable across keyboard layouts. Do not pass
        // macOS application shortcuts through as game input. Physical keys need
        // window focus; keys held through the debug interface do not, since it
        // drives the game while another application is in front.
        const bool physical = SDL_GetKeyboardFocus() == window && !(SDL_GetModState() & (KMOD_GUI | KMOD_ALT | KMOD_CTRL));
        // The keys are the player's bindings (input_bindings.hpp). Keys the debug interface
        // holds keep the classic layout whatever is bound, as the native pages take them.
        if (physical)
            state |= srw64::input::key_mask(bindings, [&](int key) { return key >= 0 && key < SDL_NUM_SCANCODES && keys[key] && key != alt_enter_key; });
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
#ifdef SRW64_GL_RENDERER
    if (gl_mode()) SDL_GL_GetDrawableSize(window, &pixel_width, &pixel_height);
#endif
#ifndef SRW64_GL_ONLY
#ifdef SRW64_GL_RENDERER
    else
#endif
#ifdef __APPLE__
    SDL_Metal_GetDrawableSize(window, &pixel_width, &pixel_height);
#else
    SDL_Vulkan_GetDrawableSize(window, &pixel_width, &pixel_height);
#endif
#endif
    return {{"focused", SDL_GetKeyboardFocus() == window}, {"width", width}, {"height", height},
            {"fullscreen", (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0},
            {"pixel_width", pixel_width}, {"pixel_height", pixel_height}, {"title", SDL_GetWindowTitle(window)}};
}

// The graphics API and the GPU as plume describes it, for a bug report (bug_report.hpp).
nlohmann::json srw64_graphics_info() {
#ifdef SRW64_GL_RENDERER
    if (gl_mode()) return {{"api", "OpenGL"}, {"device", gl_device}};
#endif
#ifdef SRW64_GL_ONLY
    return {{"api", "none"}};
#else
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
#endif
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
#ifdef SRW64_GL_RENDERER
    if (gl_context) { SDL_GL_DeleteContext(gl_context); gl_context = nullptr; }
#endif
#ifdef __APPLE__
    if (view) SDL_Metal_DestroyView(view);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void srw64_set_capture_directory(const std::filesystem::path& path) { capture_directory = path; }
void srw64_set_capture_clock(const char* name) { capture_clock = name; }
