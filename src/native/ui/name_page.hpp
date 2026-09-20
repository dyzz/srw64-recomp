#pragma once
#include "native_name_entry.hpp"
#include "text_input.hpp"
#include <RmlUi/Core.h>
#include <functional>
#include <map>

namespace srw64::ui {
std::string utf8(const std::u16string& value);
std::u16string utf16(const std::string& value);
struct NameActions {
    std::function<void(uint64_t,unsigned)> select, choose;
    std::function<void(uint64_t,const std::array<std::u16string,3>&,bool)> submit;
    std::function<void(uint64_t,bool)> review;
    std::function<std::string(const std::u16string&,unsigned)> validate;
};
// Consumes the same immutable Request and semantic actions as the AppKit page.
// No RDRAM, original engine functions, filesystem settings, or game advancement.
class NamePage final : public Rml::EventListener {
    Rml::Context& context;
    TextInput& text_input;
    NameActions actions;
    Rml::ElementDocument* document{};
    names::Request request;
    std::map<std::string,std::string> labels;
    std::string locale;
    std::string local_error;
    bool waiting{}, hd{}, art_changed{};
    void build();
    std::string label(const std::string& key) const;
    void advance();
public:
    NamePage(Rml::Context& context, TextInput& input, NameActions actions);
    ~NamePage();
    void set_hd(bool value) { if(hd!=value){hd=value;art_changed=true;} }
    void sync(const names::Request& next, const std::map<std::string,std::string>& next_labels, const std::string& next_locale);
    void ProcessEvent(Rml::Event& event) override;
    bool event(SDL_Event& event);
    void action(const std::string& id);
    Rml::ElementDocument* view() const { return document; }
    std::array<std::u16string,3> values() const;
    bool pending() const { return waiting; }
};
}
