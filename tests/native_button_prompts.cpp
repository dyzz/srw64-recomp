// Button prompt tokens (src/native/text/button_prompts.hpp): make recomp-button-prompts-test.
#include "text/button_prompts.hpp"
#include <cassert>
#include <cstdio>
using namespace srw64::text;
namespace in = srw64::input;
static std::string u8(char32_t c){std::string s;append_utf8(s,c);return s;}
static std::string name(int scancode){return scancode==in::scancode::Z?"Z":scancode==in::scancode::X?"X":scancode==in::scancode::K?"K":scancode==in::scancode::Q?"Q":"?";}
int main(){
    const PromptContext xbox{true,PadFamily::Xbox,nullptr,name},deck{true,PadFamily::Deck,nullptr,name};
    const PromptContext sony{true,PadFamily::PlayStation,nullptr,name},nintendo{true,PadFamily::Nintendo,nullptr,name};
    const PromptContext keys{false,PadFamily::Xbox,nullptr,name};
    // Controller hints: the defaults' buttons in the controller's family.
    assert(expand_prompts("{A} 确定 · {B} 返回",xbox)==u8(0xE800)+" 确定 · "+u8(0xE801)+" 返回");
    assert(expand_prompts("{A}",sony)==u8(0xE804) && expand_prompts("{A}",nintendo)==u8(0xE808));
    assert(expand_prompts("{L}",deck)==u8(0xE814) && expand_prompts("{L}",xbox)==u8(0xE810));
    assert(expand_prompts("{Start}{Settings}",xbox)==u8(0xE821)+u8(0xE820));
    assert(expand_prompts("{AuxR}",deck)==u8(0xE817) && expand_prompts("{AuxR}",xbox)==u8(0xE813));
    assert(expand_prompts("{CUp}{CDown}",xbox)==u8(0xE83A)+u8(0xE83B));
    // Groups at their defaults are one icon.
    assert(expand_prompts("{DPad}{DUpDown}{CUpDown}{Stick}",xbox)==u8(0xE830)+u8(0xE835)+u8(0xE83C)+u8(0xE838));
    // Keyboard hints: fixed keys are icons, bound keys an icon or their name.
    assert(expand_prompts("{Enter} / {A} 确定",keys)==u8(0xE841)+" / Z 确定");
    assert(expand_prompts("{Esc} / {B}",keys)==u8(0xE840)+" / X");
    assert(expand_prompts("{Start}",keys)==u8(0xE841));             // Return
    assert(expand_prompts("{Z}",keys)==u8(0xE843));                 // Space
    assert(expand_prompts("{DPad} ／ {Stick}",keys)==u8(0xE849)+" ／ "+u8(0xE84A));
    assert(expand_prompts("{DUpDown}",keys)==u8(0xE846)+u8(0xE848));  // two icons, no slash
    assert(expand_prompts("{CUpDown}",keys)=="?/K");                 // I has no name in this test
    assert(expand_prompts("{AuxL}",keys)=="\xe2\x80\x94");           // unbound on the keyboard
    // Rebinding moves the hint with it.
    auto b=in::default_bindings();
    in::assign_key(b,in::Action::A,in::scancode::K);
    in::assign_pad(b,in::Action::A,in::button(in::pad_button::Y));
    const PromptContext keys2{false,PadFamily::Xbox,&b,name},pad2{true,PadFamily::Xbox,&b,name};
    assert(expand_prompts("{A}",keys2)=="K" && expand_prompts("{CDown}",keys2)=="Z");
    assert(expand_prompts("{A}",pad2)==u8(0xE803) && expand_prompts("{Z}",pad2)==u8(0xE800));
    in::assign_pad(b,in::Action::DUp,in::button(in::pad_button::X));
    assert(expand_prompts("{DUpDown}",pad2)==u8(0xE802)+u8(0xE832));  // no longer the group icon
    // Other braces stay.
    assert(expand_prompts("第{n}话 {Esc}",keys)=="第{n}话 "+u8(0xE840));
    assert(expand_prompts("{unknown} {",keys)=="{unknown} {");
    assert(expand_prompts("plain",keys)=="plain");
    std::puts("ok");
}
