#include "program_cache.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace srw64::n64gl {
namespace {
std::mutex mutex;
std::filesystem::path folder;
std::set<std::string> kept;
std::map<std::pair<GLenum, std::string>, GLuint> shaders;
ProgramStats stats;
double since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

GLuint compile(GLenum type, const std::string& source, std::string& error) {
    if (const auto found = shaders.find({type, source}); found != shaders.end()) return found->second;
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
    return shaders[{type, source}] = shader;
}

#if defined(__vita__) || defined(__SWITCH__)
// A program's file: FNV-1a over its sources, its bindings and the binary's layout.
std::string kept_name(const std::string& vertex, const std::string& fragment,
                      std::initializer_list<AttributeBinding> attributes) {
    uint64_t hash = 0xcbf29ce484222325ull;
    const auto add = [&](const std::string& part) {
        for (const char c : part + '\0') hash = (hash ^ uint8_t(c)) * 0x100000001b3ull;
    };
    add("vitagl-program-binary-2");
    add(vertex);
    add(fragment);
    for (const auto& attribute : attributes) add(std::to_string(attribute.index) + attribute.name);
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.bin", static_cast<unsigned long long>(hash));
    return name;
}
#endif
}

void keep_programs(std::filesystem::path path, std::set<std::string> listed) {
    std::lock_guard lock(mutex);
    folder = std::move(path);
    kept = std::move(listed);
}

ProgramStats program_stats() {
    std::lock_guard lock(mutex);
    return stats;
}

GLuint link_program(const std::string& vertex, const std::string& fragment,
                    std::initializer_list<AttributeBinding> attributes, std::string& error) {
    std::lock_guard lock(mutex);
    GLint ok = 0;
    const auto start = std::chrono::steady_clock::now();
#if defined(__vita__) || defined(__SWITCH__)
    const std::string name = folder.empty() ? std::string() : kept_name(vertex, fragment, attributes);
    if (!name.empty() && kept.count(name)) {
        std::ifstream file(folder / name, std::ios::binary);
        const std::vector<char> binary((std::istreambuf_iterator<char>(file)), {});
        const GLuint program = glCreateProgram();
        glProgramBinary(program, 0, binary.data(), GLsizei(binary.size()));
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok) { ++stats.loaded; stats.load_ms += since(start); return program; }
        glDeleteProgram(program);
    }
#endif
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex, error);
    const GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, fragment, error) : 0;
    if (!vs || !fs) return 0;
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    for (const auto& attribute : attributes) glBindAttribLocation(program, attribute.index, attribute.name);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(program, sizeof log, nullptr, log);
        error = std::string("link: ") + log;
        glDeleteProgram(program);
        return 0;
    }
#if defined(__vita__) || defined(__SWITCH__)
    if (!name.empty()) {
        GLint length = 0;
        glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
        std::vector<char> binary(size_t(std::max(length, 0)));
        GLsizei written = 0;
        GLenum format = 0;
        if (length > 0) glGetProgramBinary(program, length, &written, &format, binary.data());
        if (written > 0) {
            std::ofstream(folder / name, std::ios::binary).write(binary.data(), written);
            kept.insert(name);
        }
    }
#endif
    ++stats.linked;
    stats.link_ms += since(start);
    return program;
}
}
