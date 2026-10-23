// Switch toolchain check (docs/design/switch-port.md, S0). Prints over nxlink: the applet
// type (only an application gets the full memory), the console mode, and what Mesa gives
// for a GLES 2.0 context and for a GL 2.1 compatibility context, the two the port can use.
// Then clears the screen through a colour cycle until + is pressed or 20 seconds pass.
#include <switch.h>
#include <SDL2/SDL.h>
#include <cstdio>

namespace {

using GetString = const unsigned char* (*)(unsigned);
using ClearColor = void (*)(float, float, float, float);
using Clear = void (*)(unsigned);
constexpr unsigned GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02,
    GL_SHADING_LANGUAGE_VERSION = 0x8B8C, GL_COLOR_BUFFER_BIT = 0x4000;
constexpr int button_plus = 10;

const char* applet_name(AppletType type) {
    switch (type) {
    case AppletType_Application: return "application";
    case AppletType_SystemApplication: return "system application";
    case AppletType_LibraryApplet: return "library applet (album: little memory)";
    case AppletType_OverlayApplet: return "overlay applet";
    default: return "other";
    }
}

// One context of the given kind; returns it current, or null after printing why not.
SDL_GLContext try_context(SDL_Window* window, int profile, int major, int minor, const char* label) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    const Uint64 start = SDL_GetPerformanceCounter();
    SDL_GLContext context = SDL_GL_CreateContext(window);
    const double ms = 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
    if (!context) {
        std::printf("%s: no context (%s)\n", label, SDL_GetError());
        return nullptr;
    }
    auto get_string = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
    auto text = [&](unsigned name) {
        const unsigned char* value = get_string ? get_string(name) : nullptr;
        return value ? reinterpret_cast<const char*>(value) : "?";
    };
    std::printf("%s: %.1f ms\n  vendor   %s\n  renderer %s\n  version  %s\n  glsl     %s\n", label, ms,
                text(GL_VENDOR), text(GL_RENDERER), text(GL_VERSION), text(GL_SHADING_LANGUAGE_VERSION));
    return context;
}

}  // namespace

int main(int, char**) {
    socketInitializeDefault();
    const int nxlink = nxlinkStdio();
    std::printf("srw64-smoke (nxlink %s)\n", nxlink >= 0 ? "connected" : "absent");

    std::printf("applet type: %s\n", applet_name(appletGetAppletType()));
    std::printf("mode: %s\n", appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld");
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    std::printf("memory: %llu MiB total, %llu MiB used\n", static_cast<unsigned long long>(total >> 20),
                static_cast<unsigned long long>(used >> 20));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        std::printf("SDL_Init: %s\n", SDL_GetError());
        socketExit();
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("srw64-smoke", 0, 0, 1280, 720, SDL_WINDOW_OPENGL);
    if (!window) std::printf("SDL_CreateWindow: %s\n", SDL_GetError());
    SDL_GLContext context = nullptr;
    if (window) {
        if (SDL_GLContext compat = try_context(window, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY, 2, 1, "GL 2.1 compat"))
            SDL_GL_DeleteContext(compat);
        context = try_context(window, SDL_GL_CONTEXT_PROFILE_ES, 2, 0, "GLES 2.0");
    }
    SDL_JoystickOpen(0);

    if (context) {
        auto clear_color = reinterpret_cast<ClearColor>(SDL_GL_GetProcAddress("glClearColor"));
        auto clear = reinterpret_cast<Clear>(SDL_GL_GetProcAddress("glClear"));
        const Uint32 start = SDL_GetTicks();
        int frames = 0;
        bool running = true;
        while (running && SDL_GetTicks() - start < 20000) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                if (event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == button_plus) running = false;
            }
            const float t = (SDL_GetTicks() - start) / 1000.0f;
            const float phase = t - static_cast<int>(t);
            clear_color(phase, 0.25f, 1.0f - phase, 1.0f);
            clear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(window);
            ++frames;
        }
        const float seconds = (SDL_GetTicks() - start) / 1000.0f;
        std::printf("presented %d frames in %.1f s (%.1f/s)\n", frames, seconds, frames / seconds);
        SDL_GL_DeleteContext(context);
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("done\n");
    socketExit();
    return 0;
}
