// The native dialogue on the GL renderer (docs/design/psv-port.md, V1): the same CPU
// raster as dialogue_plume.cpp (IncrementalRaster), its patches uploaded into a
// window-sized texture with glTexSubImage2D and the texture drawn over the picture,
// premultiplied, inside the rectangle the frame draws. OpenGL 2.1 / ES 2.0 only, like src/gl.
#include "dialogue_raster.hpp"
#include "game_frame.hpp"
#include "../gl/gl_api.hpp"
#include "../gl/program_cache.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>

namespace srw64::dialogue {
namespace {
IncrementalRaster raster;
GLuint canvas = 0, program = 0;
GLint canvas_sampler = -1;
uint32_t canvas_width = 0, canvas_height = 0;
std::vector<uint8_t> rgba;   // a patch's pixels in GL's order

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
    if (!canvas || canvas_width != width || canvas_height != height) {
        if (!canvas) glGenTextures(1, &canvas);
        glBindTexture(GL_TEXTURE_2D, canvas);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        canvas_width = width; canvas_height = height;
        raster.reset();
    }
    std::ostringstream context;
    context << localization::catalog().locale << ',' << localization::catalog().font << ',' << localization::catalog().revision;
    const auto rastered = std::chrono::steady_clock::now();
    auto update = raster.update(*frame, width, height, srw64::frame::width(float(width), float(height)), context.str());
    n64gl::gl_stats().raster_ms += n64gl::gl_ms_since(rastered);
    const auto patched = std::chrono::steady_clock::now();
    glBindTexture(GL_TEXTURE_2D, canvas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (const auto& patch : update.patches) {
        // BGRA as the raster keeps it; GL ES has no BGRA upload of its own.
        const auto& source = patch.pixels.pixels;
        rgba.resize(source.size());
        for (size_t i = 0; i < source.size(); i += 4) {
            rgba[i] = source[i + 2]; rgba[i + 1] = source[i + 1]; rgba[i + 2] = source[i]; rgba[i + 3] = source[i + 3];
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, patch.rect.left, patch.rect.top, GLsizei(patch.pixels.width), GLsizei(patch.pixels.height),
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        ++n64gl::gl_stats().patches;
        n64gl::gl_stats().patch_bytes += rgba.size();
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
    if (canvas) glDeleteTextures(1, &canvas);
    if (program) glDeleteProgram(program);
    canvas = program = 0;
    canvas_width = canvas_height = 0;
    raster.reset();
}
}  // namespace srw64::dialogue
