#include "ui_renderer_gl.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>

#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "../../gl/gl_api.hpp"
#include "../../gl/program_cache.hpp"
#include "stb/stb_image.h"   // the implementation is in RT64's texture cache on the desktop

namespace recompui {
namespace {
#ifdef SRW64_GLES
constexpr const char* kPreamble = "#version 100\nprecision highp float;\n";
#else
constexpr const char* kPreamble = "#version 120\n";
#endif

}  // namespace

struct RmlRenderInterface_GL::Impl final : Rml::RenderInterfaceCompatibility {
    // A texture's pixels until the graphics thread makes it (RGBA, as RmlUi gives them).
    struct Texture { GLuint id = 0; int width = 0, height = 0; std::vector<Rml::byte> pending; };
    std::unordered_map<Rml::TextureHandle, Texture> textures;
    std::vector<GLuint> released;   // to delete on the graphics thread
    Rml::TextureHandle next = 2;    // 0 white, 1 transparent, as the Plume adapter keeps them
    std::mutex queue_mutex;
    struct Named { std::vector<char> bytes; uint32_t width = 0, height = 0; bool file = true; };
    std::unordered_map<std::string, Named> named;

    bool drawing = false;
    int width = 0, height = 0;
    bool scissor = false;
    int scissor_rect[4] = {};
    Rml::Matrix4f projection = Rml::Matrix4f::Identity(), transform = Rml::Matrix4f::Identity();
    GLuint program = 0, vertices = 0, indices = 0;
    GLint mvp_uniform = -1, translation_uniform = -1, texture_uniform = -1;
    std::vector<uint16_t> short_indices;

    Impl() {
        textures[0] = {0, 1, 1, {255, 255, 255, 255}};
        textures[1] = {0, 1, 1, {0, 0, 0, 0}};
    }

    void make_program() {
        std::string error;
        program = srw64::n64gl::link_program(std::string(kPreamble) +
            "uniform mat4 u_mvp;\nuniform vec2 u_translation;\n"
            "attribute vec2 a_position;\nattribute vec4 a_colour;\nattribute vec2 a_uv;\n"
            "varying vec4 v_colour;\nvarying vec2 v_uv;\n"
            "void main() {\n  gl_Position = u_mvp * vec4(a_position + u_translation, 0.0, 1.0);\n  v_colour = a_colour;\n  v_uv = a_uv;\n}\n", std::string(kPreamble) +
            "uniform sampler2D u_texture;\nvarying vec4 v_colour;\nvarying vec2 v_uv;\n"
            "void main() {\n  gl_FragColor = texture2D(u_texture, v_uv) * v_colour;\n}\n",
            {{0, "a_position"}, {1, "a_colour"}, {2, "a_uv"}}, error);
        if (!program) throw std::runtime_error("UI GL " + error);
        mvp_uniform = glGetUniformLocation(program, "u_mvp");
        translation_uniform = glGetUniformLocation(program, "u_translation");
        texture_uniform = glGetUniformLocation(program, "u_texture");
        glGenBuffers(1, &vertices);
        glGenBuffers(1, &indices);
    }

    GLuint realize(Rml::TextureHandle handle) {
        auto found = textures.find(handle);
        if (found == textures.end()) found = textures.find(1);
        Texture& t = found->second;
        if (!t.id) {
            glGenTextures(1, &t.id);
            glBindTexture(GL_TEXTURE_2D, t.id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.pending.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            t.pending.clear();
            t.pending.shrink_to_fit();
        }
        return t.id;
    }

    void RenderGeometry(Rml::Vertex* v, int vertex_count, int* index, int index_count, Rml::TextureHandle texture, const Rml::Vector2f& translation) override {
        if (!drawing || vertex_count <= 0 || index_count <= 0) return;
        short_indices.resize(size_t(index_count));
        for (int i = 0; i < index_count; ++i) short_indices[size_t(i)] = uint16_t(index[i]);
        const GLuint id = realize(texture);
        glBindBuffer(GL_ARRAY_BUFFER, vertices);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sizeof(Rml::Vertex) * size_t(vertex_count)), v, GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(sizeof(uint16_t) * short_indices.size()), short_indices.data(), GL_STREAM_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<const void*>(offsetof(Rml::Vertex, position)));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex), reinterpret_cast<const void*>(offsetof(Rml::Vertex, colour)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<const void*>(offsetof(Rml::Vertex, tex_coord)));
        const Rml::Matrix4f mvp = projection * transform;
        glUniformMatrix4fv(mvp_uniform, 1, GL_FALSE, mvp.data());
        glUniform2f(translation_uniform, translation.x, translation.y);
        glBindTexture(GL_TEXTURE_2D, id);
        if (scissor) glScissor(scissor_rect[0], height - scissor_rect[1] - scissor_rect[3], scissor_rect[2], scissor_rect[3]);
        else glScissor(0, 0, width, height);
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_SHORT, nullptr);
    }
    void EnableScissorRegion(bool enable) override { scissor = enable; }
    void SetScissorRegion(int x, int y, int w, int h) override { scissor_rect[0] = x; scissor_rect[1] = y; scissor_rect[2] = w; scissor_rect[3] = h; }
    bool LoadTexture(Rml::TextureHandle& handle, Rml::Vector2i& size, const Rml::String& source) override {
        Named image;
        {
            std::lock_guard lock(queue_mutex);
            const auto found = named.find(source);
            if (found == named.end()) { handle = 1; size = {1, 1}; return true; }   // as the Plume adapter: transparent
            image = found->second;
        }
        Texture t;
        if (image.file) {
            int w = 0, h = 0, channels = 0;
            stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(image.bytes.data()), int(image.bytes.size()), &w, &h, &channels, 4);
            if (!pixels) return false;
            t.width = w; t.height = h;
            t.pending.assign(pixels, pixels + size_t(w) * h * 4);
            stbi_image_free(pixels);
        } else {
            t.width = int(image.width); t.height = int(image.height);
            t.pending.assign(image.bytes.begin(), image.bytes.end());
        }
        handle = next++;
        size = {t.width, t.height};
        textures[handle] = std::move(t);
        return true;
    }
    bool GenerateTexture(Rml::TextureHandle& handle, const Rml::byte* source, const Rml::Vector2i& size) override {
        if (size.x <= 0 || size.y <= 0) { handle = 0; return true; }
        handle = next++;
        Texture& t = textures[handle];
        t.width = size.x; t.height = size.y;
        t.pending.assign(source, source + size_t(size.x) * size.y * 4);
        return true;
    }
    void ReleaseTexture(Rml::TextureHandle handle) override {
        if (handle <= 1) return;
        const auto found = textures.find(handle);
        if (found == textures.end()) return;
        if (found->second.id) released.push_back(found->second.id);
        textures.erase(found);
    }
    void SetTransform(const Rml::Matrix4f* matrix) override { transform = matrix ? *matrix : Rml::Matrix4f::Identity(); }
};

RmlRenderInterface_GL::RmlRenderInterface_GL() : impl(std::make_unique<Impl>()) {}
RmlRenderInterface_GL::~RmlRenderInterface_GL() = default;
Rml::RenderInterface* RmlRenderInterface_GL::get_rml_interface() { return impl->GetAdaptedInterface(); }

void RmlRenderInterface_GL::start(int width, int height) {
    Impl& s = *impl;
    if (!s.released.empty()) { glDeleteTextures(GLsizei(s.released.size()), s.released.data()); s.released.clear(); }
    if (!s.program) s.make_program();
    s.width = width; s.height = height;
    s.projection = Rml::Matrix4f::ProjectOrtho(0.0f, float(width), float(height), 0.0f, -10000, 10000);
    s.drawing = true;
    glViewport(0, 0, width, height);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    // Not premultiplied, as the Plume adapter blends RmlUi's pictures.
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s.program);
    glUniform1i(s.texture_uniform, 0);
    glActiveTexture(GL_TEXTURE0);
    for (GLuint a = 0; a < 3; ++a) glEnableVertexAttribArray(a);
}

void RmlRenderInterface_GL::end() {
    Impl& s = *impl;
    s.drawing = false;
    for (GLuint a = 0; a < 3; ++a) glDisableVertexAttribArray(a);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
}

void RmlRenderInterface_GL::release_gl() {
    Impl& s = *impl;
    for (auto& [handle, t] : s.textures) if (t.id) { glDeleteTextures(1, &t.id); t.id = 0; }
    if (!s.released.empty()) glDeleteTextures(GLsizei(s.released.size()), s.released.data());
    s.released.clear();
    if (s.program) { glDeleteProgram(s.program); glDeleteBuffers(1, &s.vertices); glDeleteBuffers(1, &s.indices); s.program = 0; }
}

void RmlRenderInterface_GL::queue_image_from_bytes_file(const std::string& src, const std::vector<char>& bytes) {
    std::lock_guard lock(impl->queue_mutex);
    impl->named[src] = {bytes, 0, 0, true};
}

void RmlRenderInterface_GL::queue_image_from_bytes_rgba32(const std::string& src, const std::vector<char>& bytes, uint32_t width, uint32_t height) {
    std::lock_guard lock(impl->queue_mutex);
    impl->named[src] = {bytes, width, height, false};
}
}  // namespace recompui
