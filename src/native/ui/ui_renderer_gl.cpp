#include "ui_renderer_gl.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
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

    // A piece of geometry in buffers of its own, made once (RmlUi keeps it until the element
    // changes). Drawn through the compatibility layer's immediate path instead, every piece
    // was uploaded again every frame: two glBufferData each, which the Switch's Mesa makes
    // dear (a page up cost 7-15 ms a frame). The buffers go on the graphics thread (released).
    struct Compiled {
        uint64_t serial = 0;
        GLuint vertices = 0, indices = 0;
        GLsizei count = 0;
        Rml::TextureHandle texture = 0;
        std::vector<GLuint>* released = nullptr;
        ~Compiled() { if (vertices && released) { released->push_back(vertices); released->push_back(indices); } }
    };
    std::vector<GLuint> released_buffers;   // before what holds a Compiled: they are destroyed into it
    std::unordered_map<Rml::CompiledGeometryHandle, std::shared_ptr<Compiled>> compiled;
    Rml::CompiledGeometryHandle next_geometry = 1;
    uint64_t next_serial = 1;
    // What a render asked for, kept until it is drawn: begin() .. finish() gathers it and
    // gives its hash, draw() paints it. The same hash is the same picture: a piece of geometry
    // or a texture that changes gets a new serial or handle.
    struct Draw {
        std::shared_ptr<Compiled> geometry;
        Rml::Vector2f translation;
        int transform = -1;   // into transforms, -1 for none
        bool scissor = false;
        int scissor_rect[4] = {};
    };
    std::vector<Draw> draws;
    std::vector<Rml::Matrix4f> transforms;
    int transform_now = -1;

    bool recording = false;
    int width = 0, height = 0;
    bool scissor = false;
    int scissor_rect[4] = {};
    GLuint program = 0;
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

    // Only while a render is gathered (the graphics thread, its context current); the
    // compatibility layer asks at a piece's first draw, once for each texture it is drawn with.
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Vertex* v, int vertex_count, int* index, int index_count, Rml::TextureHandle texture) override {
        if (!recording || vertex_count <= 0 || index_count <= 0) return 0;
        auto made = std::make_shared<Compiled>();
        made->serial = next_serial++;
        made->count = GLsizei(index_count);
        made->texture = texture;
        made->released = &released_buffers;
        short_indices.resize(size_t(index_count));
        for (int i = 0; i < index_count; ++i) short_indices[size_t(i)] = uint16_t(index[i]);
        glGenBuffers(1, &made->vertices);
        glGenBuffers(1, &made->indices);
        glBindBuffer(GL_ARRAY_BUFFER, made->vertices);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sizeof(Rml::Vertex) * size_t(vertex_count)), v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, made->indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(sizeof(uint16_t) * short_indices.size()), short_indices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        const Rml::CompiledGeometryHandle handle = next_geometry++;
        compiled[handle] = std::move(made);
        return handle;
    }
    void RenderCompiledGeometry(Rml::CompiledGeometryHandle handle, const Rml::Vector2f& translation) override {
        if (!recording) return;
        const auto found = compiled.find(handle);
        if (found == compiled.end()) return;
        Draw d;
        d.geometry = found->second;
        d.translation = translation;
        d.transform = transform_now;
        d.scissor = scissor;
        std::copy(scissor_rect, scissor_rect + 4, d.scissor_rect);
        draws.push_back(std::move(d));
    }
    void ReleaseCompiledGeometry(Rml::CompiledGeometryHandle handle) override { compiled.erase(handle); }
    // Every piece is compiled while a render is gathered, so nothing comes this way.
    void RenderGeometry(Rml::Vertex*, int, int*, int, Rml::TextureHandle, const Rml::Vector2f&) override {}
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
    void SetTransform(const Rml::Matrix4f* matrix) override {
        if (!matrix) { transform_now = -1; return; }
        transform_now = int(transforms.size());
        transforms.push_back(*matrix);
    }
    void delete_released() {
        if (!released.empty()) { glDeleteTextures(GLsizei(released.size()), released.data()); released.clear(); }
        if (!released_buffers.empty()) { glDeleteBuffers(GLsizei(released_buffers.size()), released_buffers.data()); released_buffers.clear(); }
    }
};

RmlRenderInterface_GL::RmlRenderInterface_GL() : impl(std::make_unique<Impl>()) {}
RmlRenderInterface_GL::~RmlRenderInterface_GL() = default;
Rml::RenderInterface* RmlRenderInterface_GL::get_rml_interface() { return impl->GetAdaptedInterface(); }

void RmlRenderInterface_GL::begin(int width, int height) {
    Impl& s = *impl;
    s.draws.clear();
    s.transforms.clear();
    s.transform_now = -1;
    s.scissor = false;
    s.delete_released();
    if (!s.program) s.make_program();
    s.width = width; s.height = height;
    s.recording = true;
}

uint64_t RmlRenderInterface_GL::finish() {
    Impl& s = *impl;
    s.recording = false;
    if (s.draws.empty()) return 0;
    uint64_t hash = 0xCBF29CE484222325ull;
    const auto mix = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    };
    const int size[2] = {s.width, s.height};
    mix(size, sizeof size);
    for (const auto& d : s.draws) {
        mix(&d.geometry->serial, sizeof d.geometry->serial);
        mix(&d.geometry->texture, sizeof d.geometry->texture);
        mix(&d.translation.x, sizeof(float));
        mix(&d.translation.y, sizeof(float));
        const uint8_t flags = uint8_t(d.scissor) | uint8_t(d.transform >= 0) << 1;
        mix(&flags, 1);
        if (d.scissor) mix(d.scissor_rect, sizeof d.scissor_rect);
        if (d.transform >= 0) mix(s.transforms[size_t(d.transform)].data(), sizeof(float) * 16);
    }
    return hash ? hash : 1;
}

void RmlRenderInterface_GL::draw() {
    Impl& s = *impl;
    if (s.draws.empty()) return;
    const Rml::Matrix4f projection = Rml::Matrix4f::ProjectOrtho(0.0f, float(s.width), float(s.height), 0.0f, -10000, 10000);
    glViewport(0, 0, s.width, s.height);
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
    // Each state only as it changes from the draw before.
    int transform = -2;
    int rect[4] = {-1, -1, -1, -1};
    for (const auto& d : s.draws) {
        const Impl::Compiled& g = *d.geometry;
        if (!g.vertices) continue;
        const GLuint id = s.realize(g.texture);   // may bind another texture to make it
        if (d.transform != transform) {
            transform = d.transform;
            const Rml::Matrix4f mvp = transform >= 0 ? projection * s.transforms[size_t(transform)] : projection;
            glUniformMatrix4fv(s.mvp_uniform, 1, GL_FALSE, mvp.data());
        }
        glUniform2f(s.translation_uniform, d.translation.x, d.translation.y);
        glBindTexture(GL_TEXTURE_2D, id);
        const int wanted[4] = {d.scissor ? d.scissor_rect[0] : 0, d.scissor ? s.height - d.scissor_rect[1] - d.scissor_rect[3] : 0,
                               d.scissor ? d.scissor_rect[2] : s.width, d.scissor ? d.scissor_rect[3] : s.height};
        if (!std::equal(wanted, wanted + 4, rect)) {
            std::copy(wanted, wanted + 4, rect);
            glScissor(rect[0], rect[1], rect[2], rect[3]);
        }
        glBindBuffer(GL_ARRAY_BUFFER, g.vertices);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.indices);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<const void*>(offsetof(Rml::Vertex, position)));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex), reinterpret_cast<const void*>(offsetof(Rml::Vertex, colour)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<const void*>(offsetof(Rml::Vertex, tex_coord)));
        glDrawElements(GL_TRIANGLES, g.count, GL_UNSIGNED_SHORT, nullptr);
    }
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
    // The buffers of what RmlUi still holds too: nothing is drawn after this.
    s.draws.clear();
    for (auto& [handle, g] : s.compiled)
        if (g->vertices) { glDeleteBuffers(1, &g->vertices); glDeleteBuffers(1, &g->indices); g->vertices = g->indices = 0; }
    s.delete_released();
    if (s.program) { glDeleteProgram(s.program); s.program = 0; }
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
