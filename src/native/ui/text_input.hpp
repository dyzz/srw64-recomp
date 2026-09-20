#pragma once
#include <RmlUi/Core.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <RmlUi/Core/TextInputContext.h>
#include <SDL.h>
#include <string>

namespace srw64::ui {
// RmlUi and SDL are both owned by the window thread in the standalone probe.
// Preedit is temporary: blur/cancel restores the selection it replaced.
class TextInput final : public Rml::TextInputHandler {
    Rml::Context* ui{};
    Rml::TextInputContext* active{};
    std::string original;
    int selection_start{}, selection_end{}, composition_end{};
    bool composing{}, swallow_return{};
    bool deferred{}, wants_text{};
    float scale=1;
    SDL_Rect rectangle{};
    void start_text(bool active);
    void cancel();
public:
    void defer_sdl(bool value) { deferred=value; }
    void set_scale(float value) { scale=value; }
    void flush_sdl();
    void bind(Rml::Context& context) { ui=&context; }
    void OnActivate(Rml::TextInputContext* context) override;
    void OnDeactivate(Rml::TextInputContext* context) override;
    void OnDestroy(Rml::TextInputContext* context) override;
    bool event(const SDL_Event& event);
    void update_rectangle();
    bool has_composition() const { return composing; }
    bool accepts_submit() const { return !composing && !swallow_return; }
    void clear() { cancel(); }
};
}
