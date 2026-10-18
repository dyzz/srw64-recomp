// The host's tagged draws on the GL renderer (native_quads.hpp): called by src/gl's renderer
// in the frame where the tag was, into the frame's render target. Premultiplied pictures,
// tinted and faded as HdSprite draws them; a model-space quad goes through the triangles'
// own transform; the game's depth test is kept (its second, depth-writing pass is not yet).
#include "native_quads.hpp"
#include "native_gl.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "../gl/gl_api.hpp"
#include "../gl/program_cache.hpp"

namespace srw64::native_gl {
namespace {
#ifdef SRW64_GLES
constexpr const char* kPreamble = "#version 100\nprecision highp float;\n";
#else
constexpr const char* kPreamble = "#version 120\n";
#endif
struct Texture { GLuint id = 0; std::shared_ptr<const std::vector<uint8_t>> pixels; uint64_t used = 0; bool wrap = false; };
std::unordered_map<const void*, Texture> textures;
GLuint program = 0;
GLint color_uniform = -1, texture_uniform = -1;
uint64_t frame_count = 0;
std::vector<native_quads::Quad> quads;


void make_program() {
    std::string error;
    program = n64gl::link_program(std::string(kPreamble) +
        "attribute vec4 a_position;\nattribute vec2 a_uv;\nvarying vec2 v_uv;\n"
        "void main() {\n  gl_Position = a_position;\n  v_uv = a_uv;\n}\n", std::string(kPreamble) +
        "uniform sampler2D u_texture;\nuniform vec4 u_color;\nvarying vec2 v_uv;\n"
        "void main() {\n  vec4 c = texture2D(u_texture, v_uv);\n  gl_FragColor = vec4(c.rgb * u_color.rgb, c.a) * u_color.a;\n}\n",
        {{0, "a_position"}, {1, "a_uv"}}, error);
    if (!program) throw std::runtime_error("Native GL " + error);
    color_uniform = glGetUniformLocation(program, "u_color");
    texture_uniform = glGetUniformLocation(program, "u_texture");
}

GLuint texture_for(const native_quads::Quad& q) {
    Texture& t = textures[q.pixels.get()];
    if (!t.id) {
        t.pixels = q.pixels;
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(q.width), GLsizei(q.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, q.pixels->data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, t.id);
    if (t.wrap != q.wrap || t.used == 0) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, q.wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        t.wrap = q.wrap;
    }
    t.used = frame_count;
    return t.id;
}
}  // namespace

void draw(const n64gl::Command& command, int width, int height) {
    quads.clear();
    if (!native_quads::find(command.native_id, quads) || quads.empty()) return;
    static const bool trace = std::getenv("SRW64_GL_NATIVE_TRACE") != nullptr;
    if (trace)
        for (const auto& q : quads)
            std::fprintf(stderr, "SRW64_GL_NATIVE %08X %s %.1f,%.1f-%.1f,%.1f px=%p %ux%u\n", command.native_id, q.label.c_str(),
                         q.rect[0], q.rect[1], q.rect[2], q.rect[3], (const void*)q.pixels.get(), q.width, q.height);
    if (!program) make_program();
    glUseProgram(program);
    glUniform1i(texture_uniform, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (command.model && (command.depth & 1)) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); }
    else glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    for (const auto& q : quads) {
        if (!q.pixels || q.pixels->size() < size_t(q.width) * q.height * 4 || !q.width || !q.height) continue;
        // The scissor in the target's pixels (GL's origin at the bottom).
        int16_t s[4] = {0, 0, n64gl::kPictureWidth, n64gl::kPictureHeight};
        if (!q.whole) std::copy(command.state.scissor, command.state.scissor + 4, s);
        const int x0 = std::clamp<int>(s[0], 0, n64gl::kPictureWidth) * width / n64gl::kPictureWidth;
        const int x1 = std::clamp<int>(s[2], 0, n64gl::kPictureWidth) * width / n64gl::kPictureWidth;
        const int y0 = (n64gl::kPictureHeight - std::clamp<int>(s[3], 0, n64gl::kPictureHeight)) * height / n64gl::kPictureHeight;
        const int y1 = (n64gl::kPictureHeight - std::clamp<int>(s[1], 0, n64gl::kPictureHeight)) * height / n64gl::kPictureHeight;
        glEnable(GL_SCISSOR_TEST);
        glScissor(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
        float position[16], uv[8];
        for (int corner = 0; corner < 4; ++corner) {
            const float cx = float(corner & 1), cy = float(corner >> 1);
            const float x = q.rect[0] + (q.rect[2] - q.rect[0]) * cx, y = q.rect[1] + (q.rect[3] - q.rect[1]) * cy;
            float* p = position + corner * 4;
            if (q.model) {
                const float* m = command.mvp;
                for (int j = 0; j < 4; ++j) p[j] = x * m[j] + y * m[4 + j] + q.z * m[8 + j] + m[12 + j];
            } else {
                p[0] = x / (n64gl::kPictureWidth / 2.f) - 1; p[1] = 1 - y / (n64gl::kPictureHeight / 2.f); p[2] = 0; p[3] = 1;
            }
            uv[corner * 2] = q.uv[0] + (q.uv[2] - q.uv[0]) * cx;
            uv[corner * 2 + 1] = q.uv[1] + (q.uv[3] - q.uv[1]) * cy;
        }
        texture_for(q);
        glUniform4f(color_uniform, q.color[0], q.color[1], q.color[2], q.color[3]);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, position);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void end_frame() {
    // A picture no frame has drawn for a while gives its texture (and its pixels) back.
    ++frame_count;
    for (auto it = textures.begin(); it != textures.end();) {
        if (frame_count - it->second.used > 600) { glDeleteTextures(1, &it->second.id); it = textures.erase(it); }
        else ++it;
    }
}

void shutdown() {
    for (auto& [key, t] : textures) glDeleteTextures(1, &t.id);
    textures.clear();
    if (program) glDeleteProgram(program);
    program = 0;
}
}  // namespace srw64::native_gl
