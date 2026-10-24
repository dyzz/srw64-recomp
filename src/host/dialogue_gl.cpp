// The native dialogue on the GL renderer (docs/design/psv-port.md, V1): the same CPU
// raster as dialogue_plume.cpp (IncrementalRaster), its patches gathered in a window-sized
// copy in memory, the rows they touched uploaded once a frame into the canvas texture, and the
// texture drawn over the picture, premultiplied, inside the rectangle the frame draws.
// OpenGL 2.1 / ES 2.0 only, like src/gl.
#include "dialogue_raster.hpp"
#include "game_frame.hpp"
#include "../gl/gl_api.hpp"
#include "../gl/program_cache.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

namespace srw64::dialogue {
namespace {
IncrementalRaster raster;
GLuint program = 0;
GLint canvas_sampler = -1;
uint32_t canvas_width = 0, canvas_height = 0;
std::vector<uint8_t> canvas_pixels;   // the canvas in GL's order (RGBA), rows top down
// The canvas is drawn every frame, and the Switch's Mesa waits for the GPU to finish with a
// texture before writing it: a frame's patches went into one of three textures taken in turn,
// the one drawn with longest ago, as the atlas pages do (gl_renderer.cpp). Each keeps the rows
// written since it was last brought up to date.
#ifdef __SWITCH__
constexpr size_t kCanvasCopies = 3;
#else
constexpr size_t kCanvasCopies = 1;
#endif
struct Canvas { GLuint id = 0; int pending_top = 0, pending_bottom = 0; uint64_t current_at = 0; };
Canvas canvases[kCanvasCopies];
size_t current = 0;
uint64_t uploads = 0;

void widen(Canvas& c, int top, int bottom) {
    if (c.pending_bottom <= c.pending_top) { c.pending_top = top; c.pending_bottom = bottom; }
    else { c.pending_top = std::min(c.pending_top, top); c.pending_bottom = std::max(c.pending_bottom, bottom); }
}

#ifdef SRW64_GLES
constexpr const char* kPreamble = "#version 100\nprecision mediump float;\n";
#else
constexpr const char* kPreamble = "#version 120\n";
#endif


void make_program() {
    std::string error;
    program = n64gl::link_program(std::string(kPreamble) +
        "attribute vec2 a_position;\nvarying vec2 v_uv;\n"
        "void main() {\n  gl_Position = vec4(a_position.x * 2.0 - 1.0, 1.0 - a_position.y * 2.0, 0.0, 1.0);\n  v_uv = a_position;\n}\n", std::string(kPreamble) +
        "uniform sampler2D u_texture;\nvarying vec2 v_uv;\n"
        "void main() {\n  gl_FragColor = texture2D(u_texture, v_uv);\n}\n",
        {{0, "a_position"}}, error);
    if (!program) throw std::runtime_error("Dialogue GL " + error);
    canvas_sampler = glGetUniformLocation(program, "u_texture");
}
}  // namespace

void gl_draw(uint64_t workload, uint32_t width, uint32_t height) {
    auto frame = presented_frame(workload);
    if (!frame || std::none_of(frame->boxes.begin(), frame->boxes.end(), [](const auto& b) { return b.visible; })) return;
    localization::Scope locale(frame->catalog);
    if (!program) make_program();
    if (!canvases[0].id || canvas_width != width || canvas_height != height) {
        for (auto& c : canvases) {
            if (!c.id) glGenTextures(1, &c.id);
            glBindTexture(GL_TEXTURE_2D, c.id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            c.pending_top = c.pending_bottom = 0;
        }
        canvas_width = width; canvas_height = height;
        canvas_pixels.assign(size_t(width) * height * 4, 0);
        raster.reset();
    }
    std::ostringstream context;
    context << localization::catalog().locale << ',' << localization::catalog().font << ',' << localization::catalog().revision;
    const auto rastered = std::chrono::steady_clock::now();
    auto update = raster.update(*frame, width, height, srw64::frame::width(float(width), float(height)), context.str());
    n64gl::gl_stats().raster_ms += n64gl::gl_ms_since(rastered);
    const auto patched = std::chrono::steady_clock::now();
    int top = int(height), bottom = 0;
    for (const auto& patch : update.patches) {
        // BGRA as the raster keeps it; GL ES has no BGRA upload of its own.
        const auto& source = patch.pixels.pixels;
        const int left = std::max(0, int(patch.rect.left)), first = std::max(0, int(patch.rect.top));
        const int right = std::min(int(width), int(patch.rect.left) + int(patch.pixels.width));
        const int last = std::min(int(height), int(patch.rect.top) + int(patch.pixels.height));
        // Whole texels as words, red and blue swapped: the compiler makes it vector code, where
        // the byte at a time was 16 ms for a window-sized patch on the Switch.
        for (int y = first; y < last; ++y) {
            const uint8_t* in = source.data() + (size_t(y - patch.rect.top) * patch.pixels.width + size_t(left - patch.rect.left)) * 4;
            uint8_t* out = canvas_pixels.data() + (size_t(y) * width + left) * 4;
            for (int x = 0; x < right - left; ++x) {
                uint32_t v;
                std::memcpy(&v, in + x * 4, 4);
                v = (v & 0xFF00FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16);
                std::memcpy(out + x * 4, &v, 4);
            }
        }
        if (first < last) { top = std::min(top, first); bottom = std::max(bottom, last); }
        ++n64gl::gl_stats().patches;
        n64gl::gl_stats().patch_bytes += source.size();
    }
    if (top < bottom) {
        for (auto& c : canvases) widen(c, top, bottom);
        size_t next = current;
        for (size_t i = 0; i < kCanvasCopies; ++i)
            if (i != current && (next == current || canvases[i].current_at < canvases[next].current_at)) next = i;
        Canvas& c = canvases[next];
        glBindTexture(GL_TEXTURE_2D, c.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        // Whole rows: one contiguous run of the copy.
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, c.pending_top, GLsizei(width), c.pending_bottom - c.pending_top, GL_RGBA, GL_UNSIGNED_BYTE,
                        canvas_pixels.data() + size_t(c.pending_top) * width * 4);
        c.pending_top = c.pending_bottom = 0;
        c.current_at = ++uploads;
        current = next;
    }
    n64gl::gl_stats().patch_ms += n64gl::gl_ms_since(patched);
    if (update.drawn.empty()) return;
    // Premultiplied: the raster has applied alpha already.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glScissor(update.drawn.left, GLint(height) - update.drawn.bottom, GLsizei(update.drawn.width()), GLsizei(update.drawn.height()));
    glViewport(0, 0, GLsizei(width), GLsizei(height));
    glUseProgram(program);
    glUniform1i(canvas_sampler, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, canvases[current].id);
    static const float corners[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, corners);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

void gl_shutdown() {
    for (auto& c : canvases) {
        if (c.id) glDeleteTextures(1, &c.id);
        c = Canvas{};
    }
    current = 0;
    canvas_pixels.clear();
    if (program) glDeleteProgram(program);
    program = 0;
    canvas_width = canvas_height = 0;
    raster.reset();
}
}  // namespace srw64::dialogue
