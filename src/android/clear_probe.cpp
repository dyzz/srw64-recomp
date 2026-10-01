// Android A0 probe (docs/design/android-port.md): SDL through sdl2-compat, an
// ANativeWindow, a Plume Vulkan swap chain and a cycling clear colour. It reports
// what the device gives (swap chain formats, refresh rate, capabilities) to logcat
// and rebuilds the swap chain when the app returns from the background.
#include <SDL.h>
#include <SDL_syswm.h>
#include <android/log.h>
#include <android/native_window.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <unistd.h>
#include <vector>

#include "plume_render_interface.h"

namespace plume {
extern std::unique_ptr<RenderInterface> CreateVulkanInterface();
}

namespace {
constexpr const char* kTag = "SRW64";

// Android drops stderr; Plume and RT64 report through it.
void forward_stderr() {
    int pipes[2];
    if (pipe(pipes) != 0) return;
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(pipes[1], STDERR_FILENO);
    std::thread([fd = pipes[0]] {
        char buffer[1024];
        std::string line;
        for (ssize_t n; (n = read(fd, buffer, sizeof(buffer))) > 0;) {
            line.append(buffer, size_t(n));
            for (size_t end; (end = line.find('\n')) != std::string::npos; line.erase(0, end + 1))
                __android_log_print(ANDROID_LOG_WARN, kTag, "%s", line.substr(0, end).c_str());
        }
    }).detach();
}

ANativeWindow* native_window(SDL_Window* window) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_ANDROID) return nullptr;
    return info.info.android.window;
}

struct Presenter {
    plume::RenderDevice* device{};
    plume::RenderCommandQueue* queue{};
    std::unique_ptr<plume::RenderSwapChain> swap_chain;
    std::vector<std::unique_ptr<plume::RenderFramebuffer>> framebuffers;
    std::vector<std::unique_ptr<plume::RenderCommandSemaphore>> release;
    std::unique_ptr<plume::RenderCommandList> list;
    std::unique_ptr<plume::RenderCommandSemaphore> acquire;
    std::unique_ptr<plume::RenderCommandFence> fence;
    plume::RenderFormat format{};

    bool create(ANativeWindow* window) {
        swap_chain.reset();
        framebuffers.clear();
        if (!window) return false;
        // RT64 asks for B8G8R8A8 (rt64_application.cpp), which the Seeker's surface lacks:
        // Plume then logs "No compatible surface formats" and its resize() crashes the
        // Mali driver in vkCreateSwapchainKHR, so the probe only asks for R8G8B8A8.
        format = plume::RenderFormat::R8G8B8A8_UNORM;
        swap_chain = queue->createSwapChain(plume::RenderSwapChainDesc(window, format, 3));
        const bool ok = swap_chain && swap_chain->resize() && !swap_chain->isEmpty();
        SDL_Log("SRW64_SWAPCHAIN format=R8G8B8A8 ok=%d", ok);
        if (!ok) { swap_chain.reset(); return false; }
        for (uint32_t i = 0; i < swap_chain->getTextureCount(); ++i) {
            const plume::RenderTexture* color = swap_chain->getTexture(i);
            plume::RenderFramebufferDesc desc;
            desc.colorAttachments = &color;
            desc.colorAttachmentsCount = 1;
            framebuffers.push_back(device->createFramebuffer(desc));
        }
        while (release.size() < swap_chain->getTextureCount()) release.push_back(device->createCommandSemaphore());
        SDL_Log("SRW64_SURFACE %ux%u window=%dx%d refresh=%u", swap_chain->getWidth(), swap_chain->getHeight(),
                ANativeWindow_getWidth(window), ANativeWindow_getHeight(window), swap_chain->getRefreshRate());
        return true;
    }

    bool frame(float t) {
        if (!swap_chain || swap_chain->isEmpty()) return false;
        uint32_t index = 0;
        if (!swap_chain->acquireTexture(acquire.get(), &index)) return false;
        plume::RenderTexture* texture = swap_chain->getTexture(index);
        list->begin();
        list->barriers(plume::RenderBarrierStage::GRAPHICS, plume::RenderTextureBarrier(texture, plume::RenderTextureLayout::COLOR_WRITE));
        list->setFramebuffer(framebuffers[index].get());
        const uint32_t w = swap_chain->getWidth(), h = swap_chain->getHeight();
        list->setViewports(plume::RenderViewport(0, 0, float(w), float(h)));
        list->setScissors(plume::RenderRect(0, 0, w, h));
        list->clearColor(0, plume::RenderColor(0.5f + 0.5f * std::sin(t), 0.2f, 0.5f + 0.5f * std::cos(t), 1));
        // A pure red bar along the top: it turns blue if the format's channels are swapped.
        const plume::RenderRect bar(0, 0, int32_t(w), int32_t(h / 12));
        list->clearColor(0, plume::RenderColor(1, 0, 0, 1), &bar, 1);
        list->barriers(plume::RenderBarrierStage::NONE, plume::RenderTextureBarrier(texture, plume::RenderTextureLayout::PRESENT));
        list->end();
        const plume::RenderCommandList* lists = list.get();
        plume::RenderCommandSemaphore* wait = acquire.get();
        plume::RenderCommandSemaphore* signal = release[index].get();
        queue->executeCommandLists(&lists, 1, &wait, 1, &signal, 1, fence.get());
        const bool presented = swap_chain->present(index, &signal, 1);
        queue->waitForCommandFence(fence.get());
        return presented;
    }
};
}  // namespace

int main(int, char**) {
    forward_stderr();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_Log("SRW64_FAIL SDL_Init %s", SDL_GetError());
        return 1;
    }
    SDL_version linked;
    SDL_GetVersion(&linked);
    SDL_Log("SRW64_SDL linked=%d.%d.%d video=%s", linked.major, linked.minor, linked.patch, SDL_GetCurrentVideoDriver());
    SDL_Window* window = SDL_CreateWindow("SRW64", 0, 0, 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_VULKAN);
    if (!window) {
        SDL_Log("SRW64_FAIL window %s", SDL_GetError());
        return 1;
    }
    auto rhi = plume::CreateVulkanInterface();
    if (!rhi) {
        SDL_Log("SRW64_FAIL vulkan interface");
        return 1;
    }
    auto device = rhi->createDevice();
    if (!device) {
        SDL_Log("SRW64_FAIL vulkan device");
        return 1;
    }
    const auto& caps = device->getCapabilities();
    const auto& description = device->getDescription();
    SDL_Log("SRW64_DEVICE name=%s descriptorIndexing=%d preferHDR=%d presentWait=%d sampleLocations=%d",
            description.name.c_str(), caps.descriptorIndexing, caps.preferHDR, caps.presentWait, caps.sampleLocations);
    auto queue = device->createCommandQueue(plume::RenderCommandListType::DIRECT);
    Presenter presenter;
    presenter.device = device.get();
    presenter.queue = queue.get();
    presenter.list = queue->createCommandList();
    presenter.acquire = device->createCommandSemaphore();
    presenter.fence = device->createCommandFence();
    if (!presenter.create(native_window(window))) SDL_Log("SRW64_FAIL swap chain");

    // SDL3 never queues the app lifecycle events: it hands them to event watchers on
    // the stack of the pump that saw them (SDL_events.c, SDL_SendAppEvent), so
    // SDL_PollEvent alone misses them. The watch runs on this thread, inside PollEvent.
    struct Lifecycle { Presenter* presenter; SDL_Window* window; bool background, running; } life{&presenter, window, false, true};
    SDL_AddEventWatch([](void* data, SDL_Event* event) -> int {
        auto& life = *static_cast<Lifecycle*>(data);
        switch (event->type) {
        case SDL_APP_WILLENTERBACKGROUND:
            SDL_Log("SRW64_LIFECYCLE will-background");
            life.background = true;
            life.presenter->swap_chain.reset();  // its surface dies with the ANativeWindow
            life.presenter->framebuffers.clear();
            break;
        case SDL_APP_DIDENTERBACKGROUND: SDL_Log("SRW64_LIFECYCLE did-background"); break;
        case SDL_APP_WILLENTERFOREGROUND: SDL_Log("SRW64_LIFECYCLE will-foreground"); break;
        case SDL_APP_DIDENTERFOREGROUND:
            SDL_Log("SRW64_LIFECYCLE did-foreground");
            life.background = false;
            if (!life.presenter->create(native_window(life.window))) SDL_Log("SRW64_FAIL swap chain after foreground");
            break;
        case SDL_APP_LOWMEMORY: SDL_Log("SRW64_LIFECYCLE low-memory"); break;
        case SDL_APP_TERMINATING: SDL_Log("SRW64_LIFECYCLE terminating"); life.running = false; break;
        default: break;
        }
        return 1;
    }, &life);
    uint64_t frames = 0, failed = 0;
    const uint64_t start = SDL_GetTicks64();
    uint64_t report = start;
    while (life.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_WINDOWEVENT)
                SDL_Log("SRW64_EVENT window %d (%d %d)", event.window.event, event.window.data1, event.window.data2);
            else if (event.type != SDL_FINGERMOTION && event.type != SDL_MOUSEMOTION && event.type != SDL_SENSORUPDATE)
                SDL_Log("SRW64_EVENT 0x%x", event.type);
            switch (event.type) {
            case SDL_QUIT: life.running = false; break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && presenter.swap_chain) {
                    SDL_Log("SRW64_RESIZE %dx%d", event.window.data1, event.window.data2);
                    presenter.create(native_window(window));
                }
                break;
            case SDL_FINGERDOWN: SDL_Log("SRW64_TOUCH %.3f %.3f", event.tfinger.x, event.tfinger.y); break;
            case SDL_KEYDOWN: SDL_Log("SRW64_KEY %s", SDL_GetKeyName(event.key.keysym.sym)); break;
            default: break;
            }
        }
        if (life.background) { SDL_Delay(50); continue; }
        const uint64_t now = SDL_GetTicks64();
        // A lost surface fails every present; back off so logcat keeps SDL's own lines.
        if (presenter.frame(float(now - start) / 1000.f)) ++frames; else { ++failed; SDL_Delay(failed > 10 ? 500 : 16); }
        if (now - report >= 5000) {
            SDL_Log("SRW64_FRAMES presented=%llu failed=%llu fps=%.1f", (unsigned long long)frames, (unsigned long long)failed,
                    frames * 1000.0 / double(now - report));
            frames = failed = 0;
            report = now;
        }
    }
    SDL_Log("SRW64_EXIT");
    presenter.swap_chain.reset();
    presenter.framebuffers.clear();
    SDL_Quit();
    return 0;
}
