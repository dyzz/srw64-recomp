#pragma once
#include "native_name_entry.hpp"
#include <RmlUi/Core.h>
#include <SDL.h>
#include <functional>
#include <map>

namespace srw64::ui {
std::string utf8(const std::u16string& value);
std::u16string utf16(const std::string& value);
struct NameActions {
    std::function<void(uint64_t,unsigned)> select, choose;
    std::function<void(uint64_t,bool)> review;
};
// The protagonist selection and the review of both names (docs/native/native-name-entry.md).
// Consumes an immutable Request whose names are already in the reading language, and
// semantic actions. No RDRAM, original engine functions, settings, or game advancement.
class NamePage final : public Rml::EventListener {
    Rml::Context& context;
    NameActions actions;
    Rml::ElementDocument* document{};
    names::Request request;
    std::map<std::string,std::string> labels;
    std::string locale;
    bool waiting{}, hd{}, art_changed{};
    void build();
    std::string label(const std::string& key) const;
    void advance();
public:
    NamePage(Rml::Context& context, NameActions actions);
    ~NamePage();
    void set_hd(bool value) { if(hd!=value){hd=value;art_changed=true;} }
    void sync(const names::Request& next, const std::map<std::string,std::string>& next_labels, const std::string& next_locale);
    void ProcessEvent(Rml::Event& event) override;
    bool event(SDL_Event& event);
    void action(const std::string& id);
    Rml::ElementDocument* view() const { return document; }
    bool pending() const { return waiting; }
};
}
