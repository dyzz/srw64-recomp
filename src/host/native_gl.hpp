// The host's tagged draws (native_quads.hpp) on the GL renderer: the graphics thread, GL
// context current, the frame's render target bound (src/gl's Renderer::native_hook).
#pragma once
#include "../gl/display_list.hpp"

namespace srw64::native_gl {
void draw(const n64gl::Command& command, int width, int height);
// Once a frame: pictures not drawn for a while are let go.
void end_frame();
void shutdown();
}
