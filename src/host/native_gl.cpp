// The host's tagged draws on the GL renderer (native_quads.hpp): called by src/gl's renderer
// in the frame where the tag was, into the frame's render target. Premultiplied pictures,
// tinted and faded as HdSprite draws them; a model-space quad goes through the triangles'
// own transform; the game's depth test is kept (its second, depth-writing pass is not yet).
#include "native_quads.hpp"
#include "native_gl.hpp"
#include "hd_texture.hpp"

#include <algorithm>
#include <cmath>
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
GLint color_uniform = -1, texture_uniform = -1, fill_uniform = -1;
// A tactical map's (HdMapPS): the base, its palette indices (nearest) and the live palette's
// shift per index, a 256 x 1 picture kept per palette (never rewritten while drawn).
GLuint map_program = 0;
GLint map_color = -1;
std::unordered_map<const void*, Texture> index_textures;
std::unordered_map<uint64_t, Texture> shift_textures;
uint64_t frame_count = 0;
std::vector<native_quads::Quad> quads;


void make_program() {
    std::string error;
    program = n64gl::link_program(std::string(kPreamble) +
        "attribute vec4 a_position;\nattribute vec2 a_uv;\nvarying vec2 v_uv;\n"
        "void main() {\n  gl_Position = a_position;\n  v_uv = a_uv;\n}\n", std::string(kPreamble) +
        "uniform sampler2D u_texture;\nuniform vec4 u_color;\nuniform vec4 u_fill;\nvarying vec2 v_uv;\n"
        "void main() {\n  vec4 c = texture2D(u_texture, v_uv);\n"
        "  gl_FragColor = vec4(mix(c.rgb * u_color.rgb, u_fill.rgb * c.a, u_fill.a), c.a) * u_color.a;\n}\n",
        {{0, "a_position"}, {1, "a_uv"}}, error);
    if (!program) throw std::runtime_error("Native GL " + error);
    color_uniform = glGetUniformLocation(program, "u_color");
    texture_uniform = glGetUniformLocation(program, "u_texture");
    fill_uniform = glGetUniformLocation(program, "u_fill");
    map_program = n64gl::link_program(std::string(kPreamble) +
        "attribute vec4 a_position;\nattribute vec2 a_uv;\nvarying vec2 v_uv;\n"
        "void main() {\n  gl_Position = a_position;\n  v_uv = a_uv;\n}\n", std::string(kPreamble) +
        "uniform sampler2D u_texture;\nuniform sampler2D u_index;\nuniform sampler2D u_shift;\nuniform vec4 u_color;\nvarying vec2 v_uv;\n"
        "void main() {\n  vec4 b = texture2D(u_texture, v_uv);\n"
        "  float i = texture2D(u_index, v_uv).r;\n"
        "  vec3 d = (texture2D(u_shift, vec2((i * 255.0 + 0.5) / 256.0, 0.5)).rgb * 255.0 - 128.0) * (2.0 / 255.0);\n"
        "  vec3 straight = b.a > 0.0 ? b.rgb / b.a : vec3(0.0);\n"
        "  gl_FragColor = vec4(clamp(straight + d, 0.0, 1.0) * b.a * u_color.rgb, b.a) * u_color.a;\n}\n",
        {{0, "a_position"}, {1, "a_uv"}}, error);
    if (!map_program) throw std::runtime_error("Native GL map " + error);
    map_color = glGetUniformLocation(map_program, "u_color");
    glUseProgram(map_program);
    glUniform1i(glGetUniformLocation(map_program, "u_texture"), 0);
    glUniform1i(glGetUniformLocation(map_program, "u_index"), 1);
    glUniform1i(glGetUniformLocation(map_program, "u_shift"), 2);
}

// One byte per texel, nearest: the palette indices that pick each texel's shift.
GLuint index_texture_for(const native_quads::Quad& q) {
    Texture& t = index_textures[q.index.get()];
    if (!t.id) {
        t.pixels = q.index;
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, GLsizei(q.width), GLsizei(q.height), 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, q.index->data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    t.used = frame_count;
    glBindTexture(GL_TEXTURE_2D, t.id);
    return t.id;
}

GLuint shift_texture_for(const native_quads::Quad& q) {
    uint64_t key = 1469598103934665603ull;
    for (const uint8_t b : *q.shift) key = (key ^ b) * 1099511628211ull;
    Texture& t = shift_textures[key];
    if (!t.id) {
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, q.shift->data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    t.used = frame_count;
    glBindTexture(GL_TEXTURE_2D, t.id);
    return t.id;
}

GLuint texture_for(const native_quads::Quad& q) {
    Texture& t = textures[q.pixels.get()];
    if (!t.id) {
        t.pixels = q.pixels;
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        const auto start = std::chrono::steady_clock::now();
        uint64_t bytes = 0;
        if (q.format) {   // every level as it was prepared (hd_texture.hpp)
            const uint8_t* at = q.pixels->data();
            for (uint32_t level = 0, w = q.width, h = q.height; level < q.levels; ++level, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
                const size_t size = hd_texture::level_bytes(q.format, w, h);
                glCompressedTexImage2D(GL_TEXTURE_2D, GLint(level), q.format, GLsizei(w), GLsizei(h), 0, GLsizei(size), at);
                at += size; bytes += size;
            }
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(q.width), GLsizei(q.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, q.pixels->data());
            bytes = uint64_t(q.width) * q.height * 4;
        }
        auto& stats = n64gl::gl_stats();
        ++stats.quads;
        stats.quad_bytes += bytes;
        const double ms = n64gl::gl_ms_since(start);
        stats.quad_ms += ms;
        if (ms > 10)
            std::fprintf(stderr, "SRW64_GL_SLOW_UPLOAD %s format=%04X %ux%u levels=%u %.1f ms\n", q.label.c_str(), q.format, q.width,
                         q.height, q.levels, ms);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, q.levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
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
    // The picture's width in original pixels (the target is 240 of them tall): the pieces are
    // in the original's coordinates, placed in it as the tagged draw says (Command::place).
    const float picture = n64gl::kPictureHeight * float(width) / float(height);
    const int picture_px = int(std::ceil(picture - 1e-3f));
    for (const auto& q : quads) {
        if (!q.pixels || !q.width || !q.height) continue;
        if (!q.format && q.pixels->size() < size_t(q.width) * q.height * 4) continue;
        // The scissor in the target's pixels (GL's origin at the bottom).
        int16_t s[4] = {0, 0, int16_t(picture_px), n64gl::kPictureHeight};
        if (!q.whole) std::copy(command.state.scissor, command.state.scissor + 4, s);
        const int x0 = int(std::clamp<int>(s[0], 0, picture_px) * width / picture);
        const int x1 = int(std::clamp<int>(s[2], 0, picture_px) * width / picture + 0.5f);
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
                const float px = q.left >= 0 ? x + q.left : x * command.place[0] + command.place[1], py = y + command.place[2];
                p[0] = px / (picture / 2) - 1; p[1] = 1 - py / (n64gl::kPictureHeight / 2.f); p[2] = 0; p[3] = 1;
            }
            uv[corner * 2] = q.uv[0] + (q.uv[2] - q.uv[0]) * cx;
            uv[corner * 2 + 1] = q.uv[1] + (q.uv[3] - q.uv[1]) * cy;
        }
        if (q.index && q.shift && q.shift->size() == 1024 && q.index->size() >= size_t(q.width) * q.height) {
            glUseProgram(map_program);
            glActiveTexture(GL_TEXTURE1);
            index_texture_for(q);
            glActiveTexture(GL_TEXTURE2);
            shift_texture_for(q);
            glActiveTexture(GL_TEXTURE0);
            texture_for(q);
            glUniform4f(map_color, q.color[0], q.color[1], q.color[2], q.color[3]);
        } else {
            glUseProgram(program);
            texture_for(q);
            glUniform4f(color_uniform, q.color[0], q.color[1], q.color[2], q.color[3]);
            glUniform4f(fill_uniform, q.fill[0], q.fill[1], q.fill[2], q.fill[3]);
        }
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
    for (auto* held : {&textures, &index_textures})
        for (auto it = held->begin(); it != held->end();) {
            if (frame_count - it->second.used > 600) { glDeleteTextures(1, &it->second.id); it = held->erase(it); }
            else ++it;
        }
    for (auto it = shift_textures.begin(); it != shift_textures.end();) {
        if (frame_count - it->second.used > 600) { glDeleteTextures(1, &it->second.id); it = shift_textures.erase(it); }
        else ++it;
    }
}

void shutdown() {
    for (auto& [key, t] : textures) glDeleteTextures(1, &t.id);
    for (auto& [key, t] : index_textures) glDeleteTextures(1, &t.id);
    for (auto& [key, t] : shift_textures) glDeleteTextures(1, &t.id);
    textures.clear(); index_textures.clear(); shift_textures.clear();
    if (program) glDeleteProgram(program);
    if (map_program) glDeleteProgram(map_program);
    program = map_program = 0;
}
}  // namespace srw64::native_gl
