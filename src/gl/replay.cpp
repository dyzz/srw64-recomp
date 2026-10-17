// Offline replay of display lists dumped by the RT64 host (gbi_stats.hpp, SRW64_GL_DUMP), for
// the Vita renderer's development on the desktop (docs/design/psv-port.md, V1): each
// <n>.rdram/<n>.json goes through the interpreter and the GL renderer into an offscreen
// picture, written as <n>.gl.png next to RT64's <n>.rt64.png; a line per frame says what it
// cost. The folders are replayed in order with one interpreter, as the host would run them.
//
//   srw64-gl-replay [--scale N] [--keep] <dump folder>...
//
// --keep draws every frame over the last instead of clearing it first (the dumps are spaced
// apart, so this only matters for consecutive ones).
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "display_list.hpp"
#include "gl_renderer.hpp"
#include "json/json.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace fs = std::filesystem;
using namespace srw64::n64gl;

int main(int argc, char** argv) {
    int scale = 3;
    bool keep = false;
    std::string trace;   // --trace <n>: print that dump's commands
    std::vector<fs::path> folders;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--scale" && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
        else if (a == "--keep") keep = true;
        else if (a == "--trace" && i + 1 < argc) trace = argv[++i];
        else folders.emplace_back(a);
    }
    if (folders.empty()) {
        std::fprintf(stderr, "usage: srw64-gl-replay [--scale N] [--keep] <dump folder>...\n");
        return 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow("srw64-gl-replay", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { std::fprintf(stderr, "context: %s\n", SDL_GetError()); return 1; }
    std::fprintf(stderr, "GL %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    const int width = kPictureWidth * scale, height = kPictureHeight * scale;
    int failures = 0;
    {
        Renderer renderer(width, height);
        int64_t previous = -2;
        Interpreter interpreter([&](uint64_t key) { return renderer.known(key); });
        // The host's tagged draws (window frames, story text) are its own: not drawn here.
        interpreter.classify([](uint32_t w0, uint32_t w1) { return (w0 >> 16) == 0x0053 ? w1 : 0u; });
        Frame frame;
        std::vector<uint8_t> rdram(8 * 1024 * 1024);
        std::vector<uint8_t> pixels(size_t(width) * height * 4), flipped(pixels.size());
        for (const auto& folder : folders) {
            std::vector<fs::path> dumps;
            for (const auto& entry : fs::directory_iterator(folder))
                if (entry.path().extension() == ".json" && entry.path().stem().string().find('.') == std::string::npos) dumps.push_back(entry.path());
            std::sort(dumps.begin(), dumps.end());
            for (const auto& meta_path : dumps) {
                std::ifstream meta_file(meta_path);
                const auto meta = nlohmann::json::parse(meta_file, nullptr, false);
                auto image = meta_path;
                image.replace_extension(".rdram");
                std::ifstream image_file(image, std::ios::binary);
                if (meta.is_discarded() || !image_file.read(reinterpret_cast<char*>(rdram.data()), std::streamsize(rdram.size()))) {
                    std::fprintf(stderr, "%s: unreadable\n", meta_path.string().c_str());
                    ++failures;
                    continue;
                }
                frame.clear();
                const auto t0 = std::chrono::steady_clock::now();
                interpreter.trace(meta_path.stem().string() == trace ? stdout : nullptr);
                interpreter.run(rdram.data(), meta.at("start").get<uint32_t>(), frame);
                const auto t1 = std::chrono::steady_clock::now();
                if (meta_path.stem().string() == trace) {   // the frame's new textures as PNGs, then upload them to describe
                    const fs::path into = meta_path.parent_path() / (trace + ".textures");
                    fs::create_directories(into);
                    for (const auto& t : frame.textures) {
                        char name[64];
                        std::snprintf(name, sizeof name, "%016llX.png", (unsigned long long)t.key);
                        stbi_write_png((into / name).string().c_str(), t.width, t.height, 4, t.rgba.data(), t.width * 4);
                    }
                    renderer.draw(frame);
                    frame.textures.clear();
                }
                if (meta_path.stem().string() == trace) {
                    for (const auto& c : frame.commands) {
                        if (c.kind == Command::clear_depth) { std::printf("  clear depth\n"); continue; }
                        const auto& v = frame.vertices[c.first];
                        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, u0 = 1e9f, u1 = -1e9f, t0 = 1e9f, t1 = -1e9f;
                        for (uint32_t i = c.first; i < c.first + c.count; ++i) {
                            const auto& p = frame.vertices[i];
                            const float px = (p.x / p.w + 1) * 160, py = (1 - p.y / p.w) * 120;
                            x0 = std::min(x0, px); x1 = std::max(x1, px); y0 = std::min(y0, py); y1 = std::max(y1, py);
                            u0 = std::min(u0, p.u); u1 = std::max(u1, p.u); t0 = std::min(t0, p.v); t1 = std::max(t1, p.v);
                        }
                        std::printf("  box=%.1f,%.1f-%.1f,%.1f uv=%.3f,%.3f-%.3f,%.3f", x0, y0, x1, y1, u0, t0, u1, t1);
                        std::printf("  [%s] shader=%08X tex=%016llX blend=%u depth=%u ref=%u scissor=%d,%d,%d,%d first=%u count=%u v0=(%.3f %.3f %.3f %.3f uv %.3f %.3f shade %02X%02X%02X%02X prim %02X%02X%02X%02X)\n",
                                    renderer.describe(c.state.texture).c_str(), c.state.shader, (unsigned long long)c.state.texture, c.state.blend, c.state.depth, c.state.alpha_ref,
                                    c.state.scissor[0], c.state.scissor[1], c.state.scissor[2], c.state.scissor[3], c.first, c.count,
                                    v.x, v.y, v.z, v.w, v.u, v.v, v.shade[0], v.shade[1], v.shade[2], v.shade[3], v.prim[0], v.prim[1], v.prim[2], v.prim[3]);
                    }
                }
                // Dumps apart from the last start from black; a run in a row keeps its pictures.
                const int64_t list = meta.value("list", int64_t(-1));
                if (!keep && list != previous + 1) renderer.clear_targets();
                previous = list;
                renderer.draw(frame);
                glFinish();
                const auto t2 = std::chrono::steady_clock::now();
                glBindFramebuffer(GL_FRAMEBUFFER, renderer.framebuffer(frame.color_image));
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                for (int y = 0; y < height; ++y)
                    std::memcpy(flipped.data() + size_t(y) * width * 4, pixels.data() + size_t(height - 1 - y) * width * 4, size_t(width) * 4);
                for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
                auto out = meta_path;
                out.replace_extension(".gl.png");
                stbi_write_png(out.string().c_str(), width, height, 4, flipped.data(), width * 4);
                if (!renderer.error().empty()) { std::fprintf(stderr, "%s\n", renderer.error().c_str()); ++failures; }
                std::printf("%s natives=%u tris=%u rects=%u fills=%u zclears=%u verts=%zu commands=%zu calls=%u new_textures=%zu held=%zu programs=%zu interpret=%.2fms draw=%.2fms\n",
                            meta_path.string().c_str(), frame.natives, frame.triangles, frame.rectangles, frame.fills, frame.depth_clears, frame.vertices.size(),
                            frame.commands.size(), renderer.draw_calls(), frame.textures.size(), renderer.textures_held(), renderer.programs_held(),
                            std::chrono::duration<double, std::milli>(t1 - t0).count(), std::chrono::duration<double, std::milli>(t2 - t1).count());
                renderer.collect();
            }
        }
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures ? 1 : 0;
}
