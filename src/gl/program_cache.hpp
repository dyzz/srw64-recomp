// Every GL program of the host and the shared UI, linked here: the game's combiner programs
// (gl_renderer.cpp), the picture's blit, the host's layers (native_gl.cpp), the dialogue's
// (dialogue_gl.cpp) and RmlUi's (ui_renderer_gl.cpp). A compiled shader is kept for the next
// program with the same source. On the PS Vita each linked program is also kept as vitaGL's
// binary (glGetProgramBinary) in a folder, named by a hash of its sources, and loaded from
// there the next time: vitaGL compiles with the system's shader compiler, a second and more
// a program, and every first frame that needed one waited for it.
#pragma once
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
// A linked program, or 0 with `error` set.
GLuint link_program(const std::string& vertex, const std::string& fragment,
                    std::initializer_list<AttributeBinding> attributes, std::string& error);
}
