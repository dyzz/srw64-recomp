// The shared SDL/RmlUi frontend's renderer on OpenGL 2.1 / ES 2.0, for the PS Vita's GL
// path (docs/design/psv-port.md, V1): what RecompFrontend's Plume adapter
// (frontend-adapter/ui_renderer.cpp) does, with the same blending and the same named images.
// RmlUi may make or release textures on the window thread; every GL call waits for the
// graphics thread's next start() .. end(), where the context is current.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Rml { class RenderInterface; }

namespace recompui {
class RmlRenderInterface_GL {
public:
    RmlRenderInterface_GL();
    ~RmlRenderInterface_GL();
    Rml::RenderInterface* get_rml_interface();
    // Graphics thread, GL context current. begin() .. finish() gathers what RmlUi's Render()
    // asks for, for a target of this size, and finish() gives its hash: 0 for nothing, else
    // the same value for the same picture. draw() paints the last gathered into the bound
    // framebuffer; a frame whose hash is the last one drawn need not be drawn again.
    void begin(int width, int height);
    uint64_t finish();
    void draw();
    // Graphics thread: the GL objects go (the context is about to).
    void release_gl();
    void queue_image_from_bytes_file(const std::string& src, const std::vector<char>& bytes);
    void queue_image_from_bytes_rgba32(const std::string& src, const std::vector<char>& bytes, uint32_t width, uint32_t height);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace recompui
