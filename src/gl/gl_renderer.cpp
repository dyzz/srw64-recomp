#include "gl_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace srw64::n64gl {
namespace {
#ifdef SRW64_GLES
// Texture coordinates address 1024-texel atlas pages: half floats are not enough.
constexpr const char* kPreamble = "#version 100\nprecision highp float;\n";
#else
constexpr const char* kPreamble = "#version 120\n";
#endif

enum Attribute : GLuint { attribute_position = 0, attribute_uv = 1, attribute_shade = 2, attribute_prim = 3, attribute_bounds = 4 };
constexpr int kPageSize = 1024, kPages = 6, kAtlasLargest = 128;

// The combiner's inputs (one cycle; COMBINED and the LOD inputs are zero there, the game
// uses none of them, nor NOISE, K4, K5 or TEXEL1, which reads TEXEL0's tile here).
const char* color_a(uint8_t i) {
    static const char* const t[] = {"vec3(0.0)", "texel.rgb", "texel.rgb", "v_prim.rgb", "v_shade.rgb", "u_env.rgb", "vec3(1.0)", "vec3(0.5)"};
    return i < 8 ? t[i] : "vec3(0.0)";
}
const char* color_b(uint8_t i) {
    static const char* const t[] = {"vec3(0.0)", "texel.rgb", "texel.rgb", "v_prim.rgb", "v_shade.rgb", "u_env.rgb", "vec3(0.0)", "vec3(0.0)"};
    return i < 8 ? t[i] : "vec3(0.0)";
}
const char* color_c(uint8_t i) {
    static const char* const t[] = {"vec3(0.0)", "texel.rgb", "texel.rgb", "v_prim.rgb", "v_shade.rgb", "u_env.rgb", "vec3(0.0)",
                                    "vec3(texel.a)", "vec3(texel.a)", "vec3(v_prim.a)", "vec3(v_shade.a)", "vec3(u_env.a)"};
    return i < 12 ? t[i] : "vec3(0.0)";
}
const char* color_d(uint8_t i) {
    static const char* const t[] = {"vec3(0.0)", "texel.rgb", "texel.rgb", "v_prim.rgb", "v_shade.rgb", "u_env.rgb", "vec3(1.0)", "vec3(0.0)"};
    return t[i & 7];
}
const char* alpha_abd(uint8_t i) {
    static const char* const t[] = {"0.0", "texel.a", "texel.a", "v_prim.a", "v_shade.a", "u_env.a", "1.0", "0.0"};
    return t[i & 7];
}
const char* alpha_c(uint8_t i) {
    static const char* const t[] = {"0.0", "texel.a", "texel.a", "v_prim.a", "v_shade.a", "u_env.a", "0.0", "0.0"};
    return t[i & 7];
}

GLuint compile(GLenum type, const std::string& source, std::string& error) {
    const GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        error = std::string("shader: ") + log + "\n" + source;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
}  // namespace

std::string vertex_source() {
    return std::string(kPreamble) +
        "attribute vec4 a_position;\nattribute vec2 a_uv;\nattribute vec4 a_shade;\nattribute vec4 a_prim;\nattribute vec4 a_bounds;\n"
        "varying vec2 v_uv;\nvarying vec4 v_shade;\nvarying vec4 v_prim;\nvarying vec4 v_bounds;\n"
        "void main() {\n  gl_Position = a_position;\n  v_uv = a_uv;\n  v_shade = a_shade;\n  v_prim = a_prim;\n  v_bounds = a_bounds;\n}\n";
}

std::string fragment_source(uint32_t key) {
    const Combiner c = combiner_of(key);
    std::string s = kPreamble;
    s += "uniform sampler2D u_texture;\nuniform vec4 u_env;\nuniform float u_alpha_ref;\n"
         "varying vec2 v_uv;\nvarying vec4 v_shade;\nvarying vec4 v_prim;\nvarying vec4 v_bounds;\n"
         "void main() {\n";
    s += (key & shader_textured) ? "  vec4 texel = texture2D(u_texture, clamp(v_uv, v_bounds.xy, v_bounds.zw));\n" : "  vec4 texel = vec4(0.0);\n";
    s += std::string("  vec3 color = clamp((") + color_a(c.a) + " - " + color_b(c.b) + ") * " + color_c(c.c) + " + " + color_d(c.d) + ", 0.0, 1.0);\n";
    s += std::string("  float alpha = clamp((") + alpha_abd(c.aa) + " - " + alpha_abd(c.ab) + ") * " + alpha_c(c.ac) + " + " + alpha_abd(c.ad) + ", 0.0, 1.0);\n";
    if (key & shader_threshold) s += "  if (alpha < u_alpha_ref) discard;\n";
    if (key & shader_cutout) s += "  if (alpha < 0.5) discard;\n";
    s += "  gl_FragColor = vec4(color, alpha);\n}\n";
    return s;
}

Renderer::Renderer(int width, int height) : target_width(width), target_height(height) {
    glGenBuffers(1, &buffer);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
#ifdef SRW64_GLES
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
#else
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
#endif
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    auto make = [](GLuint& texture, int w, int h, const void* pixels) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    const uint32_t opaque_black = 0xFF000000u;
    make(black, 1, 1, &opaque_black);
    make(copy, width, height, nullptr);
}

Renderer::~Renderer() {
    for (auto& [key, p] : programs) glDeleteProgram(p.id);
    for (auto& [key, t] : textures) {
        if (t.page < 0) glDeleteTextures(1, &t.id);
        if (t.own) glDeleteTextures(1, &t.own);
    }
    for (auto& page : pages) glDeleteTextures(1, &page.id);
    for (auto& [key, t] : targets) { glDeleteFramebuffers(1, &t.framebuffer); glDeleteTextures(1, &t.texture); }
    glDeleteTextures(1, &copy);
    glDeleteTextures(1, &black);
    glDeleteRenderbuffers(1, &depth);
    glDeleteBuffers(1, &buffer);
}

Renderer::Target& Renderer::target(uint32_t image) {
    Target& t = targets[image];
    if (!t.framebuffer) {
        glGenTextures(1, &t.texture);
        glBindTexture(GL_TEXTURE_2D, t.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, target_width, target_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &t.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, t.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    t.used = frame_number;
    return t;
}

void Renderer::clear_targets() {
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    for (auto& [image, t] : targets) {
        glBindFramebuffer(GL_FRAMEBUFFER, t.framebuffer);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint Renderer::framebuffer(uint32_t image) const {
    const auto found = targets.find(image);
    return found == targets.end() ? 0 : found->second.framebuffer;
}

GLuint Renderer::picture(uint32_t image) const {
    const auto found = targets.find(image);
    return found == targets.end() ? 0 : found->second.texture;
}

const Renderer::Program* Renderer::program(uint32_t key) {
    if (auto found = programs.find(key); found != programs.end()) return found->second.id ? &found->second : nullptr;
    Program p;
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex_source(), last_error);
    const GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, fragment_source(key), last_error) : 0;
    if (vs && fs) {
        p.id = glCreateProgram();
        glAttachShader(p.id, vs);
        glAttachShader(p.id, fs);
        glBindAttribLocation(p.id, attribute_position, "a_position");
        glBindAttribLocation(p.id, attribute_uv, "a_uv");
        glBindAttribLocation(p.id, attribute_shade, "a_shade");
        glBindAttribLocation(p.id, attribute_prim, "a_prim");
        glBindAttribLocation(p.id, attribute_bounds, "a_bounds");
        glLinkProgram(p.id);
        GLint ok = 0;
        glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            glGetProgramInfoLog(p.id, sizeof log, nullptr, log);
            last_error = std::string("link: ") + log;
            glDeleteProgram(p.id);
            p.id = 0;
        } else {
            p.texture = glGetUniformLocation(p.id, "u_texture");
            p.env = glGetUniformLocation(p.id, "u_env");
            p.alpha_ref = glGetUniformLocation(p.id, "u_alpha_ref");
            glUseProgram(p.id);
            if (p.texture >= 0) glUniform1i(p.texture, 0);
        }
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    auto& stored = programs[key] = p;
    return stored.id ? &stored : nullptr;
}

bool Renderer::place(Texture& texture, uint16_t width, uint16_t height, bool linear) {
    auto fit = [&](Page& page) {
        if (page.cursor + width > kPageSize) {
            page.shelf_y = uint16_t(page.shelf_y + page.shelf_height);
            page.shelf_height = 0;
            page.cursor = 0;
        }
        if (page.shelf_y + height > kPageSize) return false;
        texture.x = page.cursor;
        texture.y = page.shelf_y;
        page.cursor = uint16_t(page.cursor + width);
        page.shelf_height = std::max(page.shelf_height, height);
        return true;
    };
    int chosen = -1;
    for (size_t i = 0; i < pages.size() && chosen < 0; ++i)
        if (pages[i].linear == linear && fit(pages[i])) chosen = int(i);
    if (chosen < 0 && pages.size() < kPages) {
        Page page;
        page.linear = linear;
        glGenTextures(1, &page.id);
        glBindTexture(GL_TEXTURE_2D, page.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kPageSize, kPageSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        pages.push_back(std::move(page));
        if (fit(pages.back())) chosen = int(pages.size() - 1);
    }
    if (chosen < 0) {
        // All pages full: start again on the one used longest ago, if this frame does not use it.
        int oldest = -1;
        for (size_t i = 0; i < pages.size(); ++i)
            if (pages[i].used != frame_number && (oldest < 0 || pages[i].used < pages[oldest].used)) oldest = int(i);
        if (oldest < 0) return false;
        Page& page = pages[oldest];
        for (uint64_t key : page.keys) {
            if (auto found = textures.find(key); found != textures.end()) {
                if (found->second.own) glDeleteTextures(1, &found->second.own);
                textures.erase(found);
            }
        }
        page.keys.clear();
        page.shelf_y = page.shelf_height = page.cursor = 0;
        page.linear = linear;
        if (fit(page)) chosen = oldest;
    }
    if (chosen < 0) return false;
    Page& page = pages[chosen];
    const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    glBindTexture(GL_TEXTURE_2D, page.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    page.used = frame_number;
    texture.page = int16_t(chosen);
    texture.id = page.id;
    return true;
}

void Renderer::upload(const Frame::Texture& t) {
    Texture texture;
    texture.width = t.width;
    texture.height = t.height;
    texture.used = frame_number;
    texture.wraps = t.wrap_s || t.wrap_t;
    texture.wrap_s = t.wrap_s;
    texture.wrap_t = t.wrap_t;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const bool placed = t.width <= kAtlasLargest && t.height <= kAtlasLargest && place(texture, t.width, t.height, t.linear);
    if (placed) {
        glBindTexture(GL_TEXTURE_2D, texture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, texture.x, texture.y, t.width, t.height, GL_RGBA, GL_UNSIGNED_BYTE, t.rgba.data());
        pages[texture.page].keys.push_back(t.key);
        if (!texture.wraps) { textures[t.key] = texture; return; }
    }
    GLuint& own = placed ? texture.own : texture.id;
    glGenTextures(1, &own);
    glBindTexture(GL_TEXTURE_2D, own);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.rgba.data());
    const GLint filter = t.linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    auto wrap = [](uint8_t mode) { return GLint(mode == 1 ? GL_REPEAT : mode == 2 ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE); };
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap(t.wrap_s));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap(t.wrap_t));
    textures[t.key] = texture;
}

void Renderer::draw(const Frame& frame) {
    ++frame_number;
    const int width = target_width, height = target_height;
    calls = 0;
    // What this frame draws with stays: its pages are not started over by its own uploads.
    for (const auto& command : frame.commands) {
        if (command.kind != Command::draw || !command.state.texture || is_picture(command.state.texture)) continue;
        if (auto found = textures.find(command.state.texture); found != textures.end()) {
            found->second.used = frame_number;
            if (found->second.page >= 0) pages[found->second.page].used = frame_number;
        }
    }
    for (const auto& t : frame.textures) upload(t);
    if (frame.commands.empty()) return;

    // The vertices in the bound texture's coordinates, and the batches: draws one after the
    // other that differ only in where their texture lies in the same page draw as one.
    struct Batch { const Command* command; GLuint texture; uint32_t first, count; };
    std::vector<Batch> batches;
    batches.reserve(frame.commands.size());
    staging.resize(frame.vertices.size());
    for (const auto& command : frame.commands) {
        if (command.kind == Command::native) continue;   // the host's (Renderer::draw's caller)
        if (command.kind != Command::draw) { batches.push_back({&command, 0, 0, 0}); continue; }
        const DrawState& s = command.state;
        GLuint bound = 0;
        float scale_u = 1, scale_v = 1, offset_u = 0, offset_v = 0;
        float bounds[4] = {-1e6f, -1e6f, 1e6f, 1e6f};
        float remap[2][2] = {{1, 0}, {1, 0}};   // per axis: scale and offset into one period
        if (s.shader & shader_textured) {
            if (is_picture(s.texture)) {
                const uint32_t image = picture_address(s.texture);
                const auto found = targets.find(image);
                bound = image == frame.color_image ? copy : found != targets.end() ? found->second.texture : black;
            } else if (auto found = textures.find(s.texture); found != textures.end()) {
                const Texture& t = found->second;
                bound = t.id;
                // A wrapping texture in a page: a draw within one period of it (a tile drawn
                // from its second, mirrored copy, say) takes the period back to [0, 1]; one
                // that crosses a period's edge repeats, from the texture of its own.
                bool leaves = false;
                if (t.wraps && t.page >= 0) {
                    float low[2] = {1e9f, 1e9f}, high[2] = {-1e9f, -1e9f};
                    for (uint32_t i = command.first; i < command.first + command.count; ++i) {
                        low[0] = std::min(low[0], frame.vertices[i].u); high[0] = std::max(high[0], frame.vertices[i].u);
                        low[1] = std::min(low[1], frame.vertices[i].v); high[1] = std::max(high[1], frame.vertices[i].v);
                    }
                    const uint8_t modes[2] = {t.wrap_s, t.wrap_t};
                    for (int axis = 0; axis < 2; ++axis) {
                        const float period = std::floor(low[axis] + 1e-3f);
                        if (high[axis] > period + 1.001f) { leaves = true; break; }
                        if (period == 0) continue;
                        if (!modes[axis]) { leaves = true; break; }   // clamped and outside: the edge texels stretch
                        const bool flip = modes[axis] == 2 && (int64_t(period) & 1);
                        remap[axis][0] = flip ? -1.f : 1.f;
                        remap[axis][1] = flip ? period + 1 : -period;
                    }
                }
                if (leaves) bound = t.own;
                else if (t.page >= 0) {
                    scale_u = float(t.width) / kPageSize; scale_v = float(t.height) / kPageSize;
                    offset_u = float(t.x) / kPageSize; offset_v = float(t.y) / kPageSize;
                    bounds[0] = (t.x + 0.5f) / kPageSize; bounds[1] = (t.y + 0.5f) / kPageSize;
                    bounds[2] = (t.x + t.width - 0.5f) / kPageSize; bounds[3] = (t.y + t.height - 0.5f) / kPageSize;
                } else if (!t.wraps) {
                    bounds[0] = 0.5f / t.width; bounds[1] = 0.5f / t.height;
                    bounds[2] = 1 - 0.5f / t.width; bounds[3] = 1 - 0.5f / t.height;
                }
            } else bound = black;
        }
        for (uint32_t i = command.first; i < command.first + command.count; ++i) {
            const Vertex& in = frame.vertices[i];
            GpuVertex& out = staging[i];
            out.x = in.x; out.y = in.y; out.z = in.z; out.w = in.w;
            out.u = offset_u + (in.u * remap[0][0] + remap[0][1]) * scale_u;
            out.v = offset_v + (in.v * remap[1][0] + remap[1][1]) * scale_v;
            std::copy(bounds, bounds + 4, out.bounds);
            std::copy(in.shade, in.shade + 4, out.shade);
            std::copy(in.prim, in.prim + 4, out.prim);
        }
        if (!batches.empty() && batches.back().command->kind == Command::draw && batches.back().texture == bound &&
            batches.back().first + batches.back().count == command.first) {
            const DrawState& last = batches.back().command->state;
            if (last.shader == s.shader && last.blend == s.blend && last.depth == s.depth && last.env == s.env &&
                last.alpha_ref == s.alpha_ref && std::equal(last.scissor, last.scissor + 4, s.scissor)) {
                batches.back().count += command.count;
                continue;
            }
        }
        batches.push_back({&command, bound, command.first, command.count});
    }

    const Target& into = target(frame.color_image);
    glBindFramebuffer(GL_FRAMEBUFFER, into.framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(staging.size() * sizeof(GpuVertex)), staging.data(), GL_STREAM_DRAW);
    for (GLuint a : {attribute_position, attribute_uv, attribute_shade, attribute_prim, attribute_bounds}) glEnableVertexAttribArray(a);
    glVertexAttribPointer(attribute_position, 4, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<const void*>(offsetof(GpuVertex, x)));
    glVertexAttribPointer(attribute_uv, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<const void*>(offsetof(GpuVertex, u)));
    glVertexAttribPointer(attribute_bounds, 4, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<const void*>(offsetof(GpuVertex, bounds)));
    glVertexAttribPointer(attribute_shade, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GpuVertex), reinterpret_cast<const void*>(offsetof(GpuVertex, shade)));
    glVertexAttribPointer(attribute_prim, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GpuVertex), reinterpret_cast<const void*>(offsetof(GpuVertex, prim)));
    glViewport(0, 0, width, height);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glActiveTexture(GL_TEXTURE0);

    const Program* current = nullptr;
    uint32_t current_key = ~0u;
    GLuint bound_texture = ~0u;
    int blend = -1, depth_mode = -1;
    int16_t scissor[4] = {-1, -1, -1, -1};
    auto set_scissor = [&](const int16_t* s) {
        if (std::equal(s, s + 4, scissor)) return;
        std::copy(s, s + 4, scissor);
        const int left = std::clamp<int>(s[0], 0, kPictureWidth), right = std::clamp<int>(s[2], 0, kPictureWidth);
        const int top = std::clamp<int>(s[1], 0, kPictureHeight), bottom = std::clamp<int>(s[3], 0, kPictureHeight);
        const int x0 = left * width / kPictureWidth, x1 = right * width / kPictureWidth;
        const int y0 = (kPictureHeight - bottom) * height / kPictureHeight, y1 = (kPictureHeight - top) * height / kPictureHeight;
        glScissor(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
    };
    for (const auto& batch : batches) {
        const Command& command = *batch.command;
        if (command.kind == Command::clear_depth) {
            const int16_t whole[4] = {0, 0, kPictureWidth, kPictureHeight};
            set_scissor(whole);
            glDepthMask(GL_TRUE);
            depth_mode = -1;
            glClear(GL_DEPTH_BUFFER_BIT);
            continue;
        }
        if (command.kind == Command::capture) {   // the picture so far, for draws that read it
            glBindTexture(GL_TEXTURE_2D, copy);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
            bound_texture = copy;
            continue;
        }
        const DrawState& s = command.state;
        if (s.shader != current_key) {
            current_key = s.shader;
            current = program(s.shader);
            if (current) glUseProgram(current->id);
        }
        if (!current) continue;
        if (current->env >= 0)
            glUniform4f(current->env, (s.env >> 24) / 255.f, ((s.env >> 16) & 0xFF) / 255.f, ((s.env >> 8) & 0xFF) / 255.f, (s.env & 0xFF) / 255.f);
        if (current->alpha_ref >= 0) glUniform1f(current->alpha_ref, s.alpha_ref / 255.f);
        if ((s.shader & shader_textured) && batch.texture != bound_texture) {
            bound_texture = batch.texture;
            glBindTexture(GL_TEXTURE_2D, batch.texture);
        }
        if (s.blend != blend) {
            blend = s.blend;
            if (blend == blend_alpha) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
            else glDisable(GL_BLEND);
        }
        if (s.depth != depth_mode) {
            depth_mode = s.depth;
            if (depth_mode & 1) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); }
            else if (depth_mode & 2) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); }
            else glDisable(GL_DEPTH_TEST);
            glDepthMask((depth_mode & 2) ? GL_TRUE : GL_FALSE);
        }
        set_scissor(s.scissor);
        glDrawArrays(GL_TRIANGLES, GLint(batch.first), GLsizei(batch.count));
        ++calls;
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    for (GLuint a : {attribute_position, attribute_uv, attribute_shade, attribute_prim, attribute_bounds}) glDisableVertexAttribArray(a);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::string Renderer::describe(uint64_t key) const {
    const auto found = textures.find(key);
    if (found == textures.end()) return "-";
    const Texture& t = found->second;
    char text[96];
    if (t.page >= 0) std::snprintf(text, sizeof text, "page %d %u,%u %ux%u%s", t.page, t.x, t.y, t.width, t.height, t.wraps ? " wraps" : "");
    else std::snprintf(text, sizeof text, "own %ux%u%s", t.width, t.height, t.wraps ? " wraps" : "");
    return text;
}

void Renderer::collect(uint32_t frames) {
    for (auto it = targets.begin(); it != targets.end();) {
        if (frame_number - it->second.used > frames) {
            glDeleteFramebuffers(1, &it->second.framebuffer);
            glDeleteTextures(1, &it->second.texture);
            it = targets.erase(it);
        } else ++it;
    }
    // Atlas pages are started over when full (place()); only textures of their own go here.
    for (auto it = textures.begin(); it != textures.end();) {
        if (it->second.page < 0 && frame_number - it->second.used > frames) {
            glDeleteTextures(1, &it->second.id);
            it = textures.erase(it);
        } else ++it;
    }
}

}  // namespace srw64::n64gl
