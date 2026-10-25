#pragma once
#include <cstdint>
#include <filesystem>
namespace plume { struct RenderDevice; }
namespace srw64::n64gl { struct Command; struct MeshTag; }
namespace srw64::marker {
// The 5600 marker and the model packs: $SRW64_NATIVE_MARKER / $SRW64_NATIVE_MODELS, else the
// folders given (the Switch's HD art, hd/native-marker and hd/native-models).
void configure(const std::filesystem::path& output, const std::filesystem::path& marker_pack = {},
               const std::filesystem::path& models_pack = {});
bool replacement_enabled();
// Render-hook init, after gpu::init (native_gpu.hpp).
void gpu_init();
void shutdown();
#ifdef SRW64_GL_ONLY
// The GL renderer's (src/gl): the interpreter's model hook, and the draw of a model in place.
n64gl::MeshTag classify_gl(const uint8_t* rdram, uint32_t address, uint32_t segment4);
// True when the command is a model's (drawn, or a part of one that draws nothing).
bool draw_gl(const n64gl::Command& command, int width, int height);
#endif
}
