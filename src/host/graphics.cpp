#define HLSL_CPU
#include "hle/rt64_application.h"
#include "graphics.hpp"
#include "mini_stage.hpp"
#include "debug_protocol.hpp"
#include "audio.hpp"
#include "diagnostics.hpp"
#include "window_test_control.hpp"
#include "native_marker.hpp"
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
#include "dialogue_layout.hpp"
#include "dialogue_style.hpp"
#ifdef SRW64_NATIVE_DIALOGUE
#include "native_name_entry.hpp"
#include "link_page.hpp"
#include "native_dialogue.hpp"
#endif
#ifdef SRW64_CORETEXT_PROBE
#include "coretext_probe.hpp"
#endif
#include "ultramodern/ultramodern.hpp"
#include "librecomp/game.hpp"
#include "rt64_render_hooks.h"
#include "plume_metal.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_metal.h>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>

namespace {
SDL_Window* window;
SDL_MetalView view;
std::array<uint8_t, 0x1000> dmem{}, imem{};
std::array<uint8_t, 0x40> header{};
uint32_t mi_interrupt{}, dpc_start{}, dpc_end{}, dpc_current{}, dpc_status{};
uint32_t dpc_clock{}, dpc_buffer_busy{}, dpc_pipe_busy{}, dpc_tmem{};
plume::RenderDevice* capture_device;
std::filesystem::path capture_directory;
uint64_t presented_frames{};
std::string capture_clock = "native_vi_at_draw";
// SDL is polled on the window thread; the game reads one coherent snapshot.
std::atomic<uint32_t> keyboard_state{};

void capture_frame(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer) {
    using namespace plume;
#ifdef SRW64_CORETEXT_PROBE
    srw64_coretext_draw(list, framebuffer);
#endif
#ifdef SRW64_NATIVE_DIALOGUE
    srw64::dialogue::metal_draw(list, framebuffer, RT64::GetRenderHookWorkloadId());
    // Clear workload-keyed guest naming frames before the shared UI renders.
    const auto name_workload=RT64::GetRenderHookWorkloadId();
    const bool name_cover=srw64::names::frame_cover(name_workload);
    auto* name_commands=static_cast<plume::MetalCommandList*>(list);
    if(name_cover) {
        name_commands->endActiveRenderEncoder();name_commands->endActiveBlitEncoder();
        auto* pass=MTL::RenderPassDescriptor::renderPassDescriptor();
        auto* color=pass->colorAttachments()->object(0);
        color->setTexture(static_cast<const plume::MetalFramebuffer*>(framebuffer)->colorAttachments[0].getTexture());
        color->setLoadAction(MTL::LoadActionClear);color->setStoreAction(MTL::StoreActionStore);
        color->setClearColor(MTL::ClearColor(.028,.045,.07,1));
        name_commands->mtl->renderCommandEncoder(pass)->endEncoding();
    }
    const bool ui_drawn=srw64::ui::draw(list,framebuffer,name_cover);
    name_commands->mtl->addCompletedHandler([name_workload,name_cover,ui_drawn](MTL::CommandBuffer* command) {
        if(ui_drawn)srw64::ui::presented();
        if(command->status()==MTL::CommandBufferStatusCompleted)srw64::names::cover_presented(name_workload,name_cover);
    });
#endif
    const uint64_t frame = ++presented_frames;
    const uint64_t vi = srw64_current_vi();
    const std::string clock_name = capture_clock;
    const int image_mode = srw64::presentation::image_mode.current();
    const uint64_t workload = RT64::GetRenderHookWorkloadId();
    // Opt-in temporal QA. Sample every completed frame, not only the one-in-60
    // screenshots used by ordinary probes, so a single bad present is visible.
    static const uint64_t trace_begin = std::getenv("SRW64_FRAME_TRACE_FROM") ? std::stoull(std::getenv("SRW64_FRAME_TRACE_FROM")) : UINT64_MAX;
    static const uint64_t trace_end = std::getenv("SRW64_FRAME_TRACE_TO") ? std::stoull(std::getenv("SRW64_FRAME_TRACE_TO")) : 0;
    const bool trace = vi >= trace_begin && vi <= trace_end;
    const bool screenshot = srw64_full_diagnostics() && (frame == 1 || frame % 60 == 0);
    // A debug-interface screenshot request takes this present, diagnostics or not.
    const auto debug_shot = srw64::debug::screenshots().take();
    if (!screenshot && !trace && !debug_shot) return;
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
    const auto* metal_framebuffer = static_cast<const plume::MetalFramebuffer*>(framebuffer);
    if (metal_framebuffer->colorAttachments.size() != 1) std::abort();
    const auto& attachment = metal_framebuffer->colorAttachments[0];
    const bool bgra = attachment.format == RenderFormat::B8G8R8A8_UNORM;
    if (!bgra && attachment.format != RenderFormat::R8G8B8A8_UNORM) {
        fprintf(stderr, "SRW64_CAPTURE_UNSUPPORTED_FORMAT %u\n", (unsigned)attachment.format);
        return;
    }
    const uint32_t width = framebuffer->getWidth(), height = framebuffer->getHeight();
    const uint32_t row_pixels = (width + 63) & ~63U;
    auto buffer = std::shared_ptr<RenderBuffer>(capture_device->createBuffer(RenderBufferDesc::ReadbackBuffer((uint64_t)row_pixels * 4 * height)));
    // This Plume revision implements only buffer-to-texture copies through
    // copyTextureRegion. Use Metal's texture-to-buffer blit on the same command
    // buffer, after ending the render encoder and before presentation.
    auto* command_list = static_cast<plume::MetalCommandList*>(list);
    command_list->checkActiveBlitEncoder();
    auto* blit = command_list->activeBlitEncoder;
    blit->copyFromTexture(attachment.getTexture(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(width, height, 1),
                         static_cast<plume::MetalBuffer*>(buffer.get())->mtl, 0, row_pixels * 4, (uint64_t)row_pixels * 4 * height);
    command_list->endActiveBlitEncoder();
    const bool interactive = std::getenv("SRW64_INTERACTIVE") && std::string(std::getenv("SRW64_INTERACTIVE")) == "1";
    const auto path = capture_directory / (interactive ? "present-latest.png" : "present-" + std::to_string(frame) + ".png");
    static_cast<plume::MetalCommandList*>(list)->mtl->addCompletedHandler([buffer, width, height, row_pixels, bgra, path, frame, vi, clock_name, image_mode, workload, trace, screenshot, dialogue_trace, debug_shot](MTL::CommandBuffer* command) {
        if (command->status() != MTL::CommandBufferStatusCompleted) {
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
        if (debug_shot) {
            const bool written = stbi_write_png(debug_shot->path.c_str(), width, height, 4, rgba.data(), width * 4);
            srw64::debug::screenshots().finish(debug_shot->id, written
                ? nlohmann::json({{"path", debug_shot->path.string()}, {"present", frame}, {"vi", vi},
                                  {"width", width}, {"height", height}, {"image_mode", image_mode}})
                : nlohmann::json({{"error", "cannot write " + debug_shot->path.string()}}));
        }
        if(!screenshot && !anomaly)return;
        if (!stbi_write_png(path.c_str(), width, height, 4, rgba.data(), width * 4)) std::abort();
        auto metadata_path = path;
        metadata_path.replace_extension("json");
        std::ofstream metadata(metadata_path);
        metadata << "{\"schema\":\"srw64.native-gpu-frame.v1\",\"present\":" << frame
                 << ",\"" << clock_name << "\":" << vi << ",\"width\":" << width << ",\"height\":" << height
                 << ",\"image_mode\":" << image_mode << ",\"GPU_completion\":\"completed\"}\n";
        fprintf(stderr, "SRW64_CAPTURE %s vi=%llu %ux%u\n", path.filename().c_str(), (unsigned long long)vi, width, height);
    });
}

class SRW64Renderer final : public ultramodern::renderer::RendererContext {
public:
    SRW64Renderer(uint8_t* rdram, ultramodern::renderer::WindowHandle handle) {
        srw64::marker::configure(capture_directory);
        RT64::SetRenderHooks([](plume::RenderInterface* rhi, plume::RenderDevice* device) {
            capture_device = device;
            srw64::marker::metal_init(device);
#ifdef SRW64_NATIVE_DIALOGUE
            srw64::dialogue::metal_init(device, capture_directory);
            srw64::ui::render_init(rhi,device);
#endif
#ifdef SRW64_CORETEXT_PROBE
            srw64_coretext_init(device, capture_directory);
#endif
        }, capture_frame, [] {
            srw64::marker::shutdown();
#ifdef SRW64_NATIVE_DIALOGUE
            srw64::ui::render_shutdown();
            srw64::dialogue::metal_shutdown();
#endif
#ifdef SRW64_CORETEXT_PROBE
            srw64_coretext_shutdown();
#endif
        });
        RT64::Application::Core core{};
        core.window.window = handle.window;
        core.window.view = handle.view;
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
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal;
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
        app->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Original;
        app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
        app->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None;
        app->userConfig.developerMode = false;
        const auto result = app->setup(0);
        chosen_api = ultramodern::renderer::GraphicsApi::Metal;
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
                }
                dialogue_padding = std::filesystem::exists(std::filesystem::path(directory) / "srw64-dialogue-padding-v1");
                dialogue_blue_names = std::filesystem::exists(std::filesystem::path(directory) / "srw64-dialogue-name-blue-v1");
                const auto map_spec = std::filesystem::path(directory) / "srw64-worldmap-hd.json";
                if (std::filesystem::exists(map_spec)) {
                    std::ifstream input(map_spec);
                    input >> worldmap_spec;
                    if (worldmap_spec.at("schema") != "srw64.worldmap-hd.v1" ||
                        worldmap_spec.at("resource_id") != 5604 || worldmap_spec.at("source_tile_size") != 64)
                        throw std::runtime_error("Unsupported HD world-map specification");
                    worldmap_tile_size = worldmap_spec.at("replacement_tile_size").get<unsigned>();
                    if (worldmap_tile_size != 256 && worldmap_tile_size != 512)
                        throw std::runtime_error("Unsupported HD world-map tile density");
                    for (const auto& hash : worldmap_spec.at("hashes")) {
                        const auto value = hash.get<std::string>();
                        if (value.size() != 16 || value.find_first_not_of("0123456789abcdef") != std::string::npos)
                            throw std::runtime_error("Invalid HD world-map hash");
                        worldmap_hashes.push_back(std::stoull(value,nullptr,16));
                    }
                }
            }
        }
    }
    bool valid() override { return app != nullptr; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        apply_images();
        app->state->rsp->reset();
        app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
        const uint32_t start=task->t.data_ptr & 0x3FFFFFF;
        bool native_text=false;
#ifdef SRW64_NATIVE_DIALOGUE
        auto native_frame=srw64::dialogue::take_frame(start,task->t.data_size,display_copy,app->core.RDRAM);
        native_text=bool(native_frame);
        srw64::dialogue::queue_frame(app->state->workloadId+1,native_frame);
        const bool name_cover=srw64::names::request().visible;
        srw64::names::queue_cover(app->state->workloadId+1,name_cover);
#endif
        const bool padded=native_text || (dialogue_padding && srw64_pad_dialogue(app->core.RDRAM,start,task->t.data_size,display_copy));
        if (!native_text && padded && dialogue_blue_names) {
            const auto names=srw64_blue_dialogue_names(app->core.RDRAM,start,task->t.data_size,display_copy);
            if (names && !reported_blue_names) {
                fprintf(stderr,"SRW64_DIALOGUE_NAMES_BLUE glyphs=%u rgb=69bfff vi=%llu\n",names,
                        (unsigned long long)srw64_current_vi());
                reported_blue_names=true;
            }
        }
        app->processDisplayLists(padded ? display_copy.data() : app->core.RDRAM, start, 0, true);
#ifdef SRW64_NATIVE_DIALOGUE
        srw64::dialogue::queue_frame(app->state->workloadId,native_frame);
        srw64::names::queue_cover(app->state->workloadId,name_cover);
#endif
        audit_worldmap();
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override { app->updateScreen(); }
    void shutdown() override { if (app) app->end(); }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
private:
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
    bool dialogue_padding=false;
    bool dialogue_blue_names=false, reported_blue_names=false;
    std::vector<uint8_t> display_copy;
};
}

std::unique_ptr<ultramodern::renderer::RendererContext> srw64_create_renderer(
    uint8_t* rdram, ultramodern::renderer::WindowHandle handle) {
    return std::make_unique<SRW64Renderer>(rdram, handle);
}

ultramodern::renderer::WindowHandle srw64_create_window(void*) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | (srw64_audio_enabled() ? SDL_INIT_AUDIO : 0)) != 0) {
        fprintf(stderr, "SRW64_SDL_INIT_FAILED %s\n", SDL_GetError());
        std::abort();
    }
    window = SDL_CreateWindow("SRW64 native graphics probe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              960, 720, SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE |
                              (std::getenv("SRW64_BACKGROUND") && std::string(std::getenv("SRW64_BACKGROUND")) == "1" ? SDL_WINDOW_HIDDEN : 0) |
                              (std::getenv("SRW64_NATIVE_RESOLUTION") && std::string(std::getenv("SRW64_NATIVE_RESOLUTION")) == "1" ? SDL_WINDOW_ALLOW_HIGHDPI : 0));
    if (!window) std::abort();
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) std::abort();
#ifdef SRW64_NATIVE_DIALOGUE
    srw64::ui::window_init(window,capture_directory);
    srw64::settings::window_init(window,capture_directory);
#endif
    view = SDL_Metal_CreateView(window);
    if (!view) std::abort();
    static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(view))->setFramebufferOnly(false);
    return {info.info.cocoa.window, SDL_Metal_GetLayer(view)};
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
    const bool editing_name=srw64::names::owns_input() || srw64::link_page::owns_input();
#else
    const bool editing_name=false;
#endif
    static int title_mode=-2;
    static std::string title_locale;
    static bool title_error{};
    const int applied=srw64::presentation::image_mode.current();
#ifdef SRW64_NATIVE_DIALOGUE
    const auto locale=srw64::localization::catalog().locale;
    const bool failed=srw64::settings::failed();
    const auto error_label=srw64::localization::catalog().ui("settings_error");
    const auto language=srw64::localization::display_name(locale);
    const auto languages=srw64::localization::language_choices();
#else
    const std::string locale="ja",error_label;
    const bool failed=false;
    const std::string language="日本語",languages="日本語";
#endif
    if(applied!=title_mode || locale!=title_locale || failed!=title_error) {
        title_mode=applied;
        title_locale=locale;title_error=failed;
        const std::string mode=!srw64::presentation::image_mode.enabled()?"Original | HD unavailable":applied?"HD":"Original";
        if(applied>=0)SDL_SetWindowTitle(window,(std::string("SRW64 | ")+language+" | "+mode+
            " | F6: Original / HD | F7: "+languages+(srw64::mini_stage::state().image?" | F8: mini stage "+srw64::mini_stage::state().image->name:"")+(failed?" | "+error_label:"")).c_str());
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
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
            const bool consumed=srw64::ui::event(e);e.type=SDL_KEYUP;srw64::ui::event(e);
            if(consumed)continue;
        }
        if (srw64::settings::owns_input()) continue;
#endif
        if (editing_name) continue;
        if (key == srw64::debug::F6) srw64::presentation::image_mode.toggle();
        else if (key == srw64::debug::F8) srw64::mini_stage::hotkey();
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
    if (!editing_name) {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        // Physical positions stay stable across keyboard layouts. Do not pass
        // macOS application shortcuts through as game input. Physical keys need
        // window focus; keys held through the debug interface do not, since it
        // drives the game while another application is in front.
        const bool physical = SDL_GetKeyboardFocus() == window && !(SDL_GetModState() & (KMOD_GUI | KMOD_ALT | KMOD_CTRL));
        using namespace srw64::debug;
        const auto bind = [&](SDL_Scancode key, Key debug_key, uint32_t mask) {
            if ((physical && keys[key]) || (virtual_keys & bit(debug_key))) state |= mask;
        };
        bind(SDL_SCANCODE_Z, Z, 0x8000); bind(SDL_SCANCODE_X, X, 0x4000);
        bind(SDL_SCANCODE_SPACE, Space, 0x2000); bind(SDL_SCANCODE_RETURN, Return, 0x1000);
        bind(SDL_SCANCODE_UP, Up, 0x0800); bind(SDL_SCANCODE_DOWN, Down, 0x0400);
        bind(SDL_SCANCODE_LEFT, Left, 0x0200); bind(SDL_SCANCODE_RIGHT, Right, 0x0100);
        bind(SDL_SCANCODE_Q, Q, 0x0020); bind(SDL_SCANCODE_E, E, 0x0010);
        bind(SDL_SCANCODE_I, I, 0x0008); bind(SDL_SCANCODE_K, K, 0x0004);
        bind(SDL_SCANCODE_J, J, 0x0002); bind(SDL_SCANCODE_L, L, 0x0001);
        bind(SDL_SCANCODE_W, W, 1U << 16); bind(SDL_SCANCODE_S, S, 1U << 17);
        bind(SDL_SCANCODE_A, A, 1U << 18); bind(SDL_SCANCODE_D, D, 1U << 19);
    }
    keyboard_state.store(state, std::memory_order_relaxed);
}

nlohmann::json srw64_window_status() {
    if (!window) return nullptr;
    int width = 0, height = 0, pixel_width = 0, pixel_height = 0;
    SDL_GetWindowSize(window, &width, &height);
    SDL_Metal_GetDrawableSize(window, &pixel_width, &pixel_height);
    return {{"focused", SDL_GetKeyboardFocus() == window}, {"width", width}, {"height", height},
            {"pixel_width", pixel_width}, {"pixel_height", pixel_height}, {"title", SDL_GetWindowTitle(window)}};
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
    srw64_close_audio();
    SDL_Metal_DestroyView(view);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void srw64_set_capture_directory(const std::filesystem::path& path) { capture_directory = path; }
void srw64_set_capture_clock(const char* name) { capture_clock = name; }
