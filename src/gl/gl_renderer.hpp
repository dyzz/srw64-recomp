// Draws an interpreted N64 frame (display_list.hpp) with the subset of GL that OpenGL 2.1
// and OpenGL ES 2.0 share, so the same code runs on the desktop for comparison with RT64
// and on the Vita through vitaGL (docs/design/psv-port.md, V1). One program per combiner
// key, compiled the first time it is drawn (the set is closed: gbi-usage.md lists them),
// one texture per decoded TMEM content, one vertex buffer per frame.
//
// Each colour image the game draws into is a render target of its own (a texture and a
// framebuffer object at the picture's resolution, sharing one depth buffer), as RT64 keeps
// them: a frame that reads an earlier frame's image (after-images, the title's speed lines)
// samples that target, and the host shows the one the VI scans out.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "display_list.hpp"
#include "gl_api.hpp"

namespace srw64::n64gl {

class Renderer {
public:
    Renderer(int width, int height);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    // Whether a texture is already uploaded (the interpreter's `known`).
    bool known(uint64_t key) const { return textures.count(key) != 0; }
    // Draw `frame` into the render target of its colour image. Leaves the framebuffer
    // binding at 0 and the state it switched off.
    void draw(const Frame& frame);
    // The render target of a colour image (its framebuffer object and texture), 0 if the
    // game has not drawn there.
    GLuint framebuffer(uint32_t image) const;
    GLuint picture(uint32_t image) const;
    int width() const { return target_width; }
    int height() const { return target_height; }
    // Draw a colour image's picture into the rectangle (x, y, width, height) of the bound
    // framebuffer (window coordinates, origin bottom left), filtered: the host's present.
    // The image is the VI origin; the target that holds it is found from its address.
    // Returns false if the game has drawn no such picture yet.
    bool present(uint32_t origin, int x, int y, int width, int height);
    // The colour image a VI origin shows, 0 if none has been drawn.
    uint32_t image_at(uint32_t origin) const;
    // The frame that last drew into a colour image (0 if none): a picture with the same
    // number shows what it showed before.
    uint32_t drawn_at(uint32_t image) const {
        const auto found = targets.find(image);
        return found == targets.end() ? 0 : found->second.used;
    }
    // The host's tagged draws (Command::native), called in their place in the frame with the
    // frame's render target bound and its size; the hook may change any GL state.
    using NativeHook = std::function<void(const Command&, int width, int height)>;
    void native_hook(NativeHook hook) { native = std::move(hook); }
    // Every render target black again (a replay's jump between frames apart).
    void clear_targets();
    // Free the textures and targets no frame has used for `frames` frames.
    void collect(uint32_t frames = 300);
    // The last frame's GL draw calls; programs and textures held.
    uint32_t draw_calls() const { return calls; }
    size_t programs_held() const { return programs.size(); }
    // The combiner keys of the programs linked so far, and linking those of an earlier run
    // ahead of the frames that need them (a first compile is a frame's stall).
    std::vector<uint32_t> program_keys() const;
    void prewarm(const std::vector<uint32_t>& keys);
    size_t textures_held() const { return textures.size(); }
    // Where a texture is: "page N x,y wxh", "own wxh", or "-" (for traces).
    std::string describe(uint64_t key) const;
    // A shader that would not compile or link: its log, for the caller to report.
    const std::string& error() const { return last_error; }
private:
    struct Program { GLuint id = 0; GLint texture = -1, env = -1, alpha_ref = -1; };
    // A decoded texture: a texture of its own (wrapping ones, the large ones) or a place in
    // an atlas page, so the game's many small tiles draw in few calls.
    // A wrapping texture placed in a page has a texture of its own too, for the draws whose
    // coordinates leave it (the game sets wrap on many tiles it never repeats); with batched
    // uploads it is made from the page's pixels the first time such a draw comes.
    struct Texture { GLuint id = 0, own = 0; uint32_t used = 0; int16_t page = -1; uint16_t x = 0, y = 0, width = 0, height = 0; bool wraps = false; uint8_t wrap_s = 0, wrap_t = 0; };
    struct Page { GLuint id = 0; bool linear = false; uint16_t shelf_y = 0, shelf_height = 0, cursor = 0; uint32_t used = 0; std::vector<uint64_t> keys;
                  // Where batched uploads gather (kBatchPageUploads): the page's pixels, and the rows written since the last upload.
                  std::vector<uint8_t> shadow; int dirty_top = 0, dirty_bottom = 0;
                  int filter = -1;   // the filter its textures want: 0 nearest, 1 linear, -1 none yet
                  // With batched uploads the page is several GL textures taken in turn (`id` is the
                  // one draws bind now), so an upload never writes one the GPU may still read: each
                  // keeps the rows written since it was last brought up to date, and its filter.
                  struct Copy { GLuint id = 0; int filter = -1, pending_top = 0, pending_bottom = 0; uint32_t current_at = 0; };
                  std::vector<Copy> copies; size_t current = 0;
    };
    void flush_pages();
    GLuint own_texture(Texture& texture);
    struct Target { GLuint framebuffer = 0, texture = 0; uint32_t used = 0; };
    // The vertex as the GPU takes it: texture coordinates in the bound texture, and the
    // rectangle they clamp to (a tile's edge inside its atlas page).
    struct GpuVertex { float x, y, z, w, u, v, bounds[4]; uint8_t shade[4], prim[4]; };
    const Program* program(uint32_t key);
    Target& target(uint32_t image);
    void upload(const Frame::Texture& texture);
    bool place(Texture& texture, uint16_t width, uint16_t height, bool linear);
    std::vector<Page> pages;
    std::vector<GpuVertex> staging;
    std::unordered_map<uint32_t, Program> programs;
    std::unordered_map<uint64_t, Texture> textures;
    std::unordered_map<uint32_t, Target> targets;
    int target_width, target_height;
    GLuint buffer = 0, depth = 0, copy = 0, black = 0, blit = 0;
    GLint blit_texture = -1;
    uint32_t frame_number = 0, calls = 0;
    std::string last_error;
    NativeHook native;
};

// The fragment and vertex shader sources of a key (the Vita's warm-up compiles these).
std::string vertex_source();
std::string fragment_source(uint32_t key);

}  // namespace srw64::n64gl
