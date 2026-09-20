#include "text_input.hpp"
#include <algorithm>

namespace srw64::ui {
void TextInput::start_text(bool value) {
    wants_text=value;
    if(!deferred){if(value)SDL_StartTextInput();else SDL_StopTextInput();}
}
void TextInput::flush_sdl() {
    if(wants_text){if(!SDL_IsTextInputActive())SDL_StartTextInput();SDL_SetTextInputRect(&rectangle);}
    else if(SDL_IsTextInputActive())SDL_StopTextInput();
}

void TextInput::OnActivate(Rml::TextInputContext* context) {
    if (active != context) cancel();
    active = context;
    start_text(true);
    update_rectangle();
}
void TextInput::OnDeactivate(Rml::TextInputContext* context) {
    if (active != context) return;
    cancel();
    active = nullptr;
    start_text(false);
}
void TextInput::OnDestroy(Rml::TextInputContext* context) {
    // Do not call through a dying TextInputContext.
    if (active == context) { active = nullptr; composing = false; start_text(false); }
}
void TextInput::cancel() {
    if (active && composing) {
        active->SetText(original, selection_start, composition_end);
        active->SetCompositionRange(0, 0);
        active->SetSelectionRange(selection_start, selection_end);
    }
    composing = false;
}
void TextInput::update_rectangle() {
    Rml::Rectanglef box;
    if (active && active->GetBoundingBox(box)) {
        // RmlUi 6 exposes field bounds, not a caret rectangle. Position the OS
        // candidates below the active field. Coordinates are SDL window points.
        rectangle={int(box.Left()/scale), int(box.Top()/scale), std::max(1, int(box.Width()/scale)), std::max(1, int(box.Height()/scale))};
        if(!deferred)SDL_SetTextInputRect(&rectangle);
    }
}
bool TextInput::event(const SDL_Event& event) {
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        cancel(); start_text(false); return false;
    }
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && active) {
        start_text(true); update_rectangle(); return false;
    }
    if (event.type == SDL_KEYUP && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER)) {
        bool consumed = swallow_return; swallow_return = false; return consumed;
    }
    if (event.type == SDL_KEYDOWN && composing) {
        if (event.key.keysym.sym == SDLK_ESCAPE) cancel();
        if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) swallow_return = true;
        // The OS IME owns editing keys until SDL supplies committed text.
        return true;
    }
    if (event.type == SDL_TEXTEDITING || event.type == SDL_TEXTEDITING_EXT) {
        if (!active) return true;
        const char* text = event.type == SDL_TEXTEDITING ? event.edit.text : event.editExt.text;
        const int cursor = event.type == SDL_TEXTEDITING ? event.edit.start : event.editExt.start;
        const int selected = event.type == SDL_TEXTEDITING ? event.edit.length : event.editExt.length;
        if (!text || !*text) { cancel(); return true; }
        if (!composing) {
            active->GetSelectionRange(selection_start, selection_end);
            if (selection_start > selection_end) std::swap(selection_start, selection_end);
            const auto value = ui->GetFocusElement()->GetAttribute<Rml::String>("value", "");
            auto a = Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(value, selection_start);
            auto b = Rml::StringUtilities::ConvertCharacterOffsetToByteOffset(value, selection_end);
            original = value.substr(a, b-a);
            composition_end = selection_end;
            composing = true;
        }
        const std::string preedit(text);
        active->SetText(preedit, selection_start, composition_end);
        const int count = int(Rml::StringUtilities::LengthUTF8(preedit));
        composition_end = selection_start + count;
        active->SetCompositionRange(selection_start, composition_end);
        int begin = selection_start + std::clamp(cursor, 0, count);
        active->SetSelectionRange(begin, std::min(composition_end, begin + std::max(0, selected)));
        return true;
    }
    if (event.type == SDL_TEXTINPUT) {
        if (!active) return true;
        if (composing) {
            active->CommitComposition(std::string(event.text.text));
            composing = false;
        } else ui->ProcessTextInput(event.text.text);
        return true;
    }
    return false;
}
}
