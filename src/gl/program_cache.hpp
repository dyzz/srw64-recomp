// Every GL program of the host and the shared UI, linked here: the game's combiner programs
// (gl_renderer.cpp), the picture's blit, the host's layers (native_gl.cpp), the dialogue's
// (dialogue_gl.cpp) and RmlUi's (ui_renderer_gl.cpp). A compiled shader is kept for the next
// program with the same source. On the PS Vita each linked program is also kept as vitaGL's
// binary (glGetProgramBinary) in a folder, named by a hash of its sources, and loaded from
// there the next time: vitaGL compiles with the system's shader compiler, a second and more
// a program, and every first frame that needed one waited for it.
#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <string>

#include "gl_api.hpp"

namespace srw64::n64gl {
struct AttributeBinding { GLuint index; const char* name; };
// The folder the programs are kept in, and its listing (so no file that is not there is
// looked up). The PS Vita only; elsewhere nothing is kept.
void keep_programs(std::filesystem::path folder, std::set<std::string> kept);
// Programs linked so far and the milliseconds spent compiling and linking them; those
// loaded from a kept binary, and their milliseconds.
struct ProgramStats { unsigned linked = 0, loaded = 0; double link_ms = 0, load_ms = 0; };
ProgramStats program_stats();
// Where the graphics thread's GL time goes besides drawing (the Switch's performance lines,
// graphics.cpp): the game's textures uploaded, copies of the picture so far, the dialogue's
// raster and its patches uploaded, the host's layers' pictures uploaded. Graphics thread only.
struct GlStats {
    unsigned uploads = 0, copies = 0, patches = 0, quads = 0;
    uint64_t upload_bytes = 0, patch_bytes = 0, quad_bytes = 0;
    double upload_ms = 0, copy_ms = 0, raster_ms = 0, patch_ms = 0, quad_ms = 0;
};
GlStats& gl_stats();
inline double gl_ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
// A linked program, or 0 with `error` set.
GLuint link_program(const std::string& vertex, const std::string& fragment,
                    std::initializer_list<AttributeBinding> attributes, std::string& error);
}
