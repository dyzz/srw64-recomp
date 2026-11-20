#pragma once
// Which renderer draws the game: RT64, or the GL renderer (src/gl). The GL-only host (the
// Switch, the Vita) has only the latter; a host built with both (SRW64_GL_RENDERER beside RT64:
// Windows, where the GL renderer over ANGLE is the fallback for PCs RT64 cannot use, and the Mac
// for development) chooses once, before the window opens (graphics.cpp), and the HD layers
// prepare their pictures for that renderer (docs/design/windows-gl-fallback.md).
namespace srw64::renderer_mode {
#if defined(SRW64_GL_ONLY)
constexpr bool gl() { return true; }
#elif defined(SRW64_GL_RENDERER)
bool gl();   // graphics.cpp
#else
constexpr bool gl() { return false; }
#endif
}  // namespace srw64::renderer_mode
