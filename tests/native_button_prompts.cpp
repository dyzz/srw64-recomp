// Button prompt tokens (src/native/text/button_prompts.hpp): make recomp-button-prompts-test.
#include "text/button_prompts.hpp"
#include <cassert>
#include <cstdio>
using namespace srw64::text;
static std::string u8(char32_t c){return {char(0xE0|(c>>12)),char(0x80|((c>>6)&0x3F)),char(0x80|(c&0x3F))};}
int main(){
    assert(expand_prompts("{A} 确定 · {B} 返回",PadFamily::Xbox)==u8(0xE800)+" 确定 · "+u8(0xE801)+" 返回");
    assert(expand_prompts("{A}",PadFamily::PlayStation)==u8(0xE804));
    assert(expand_prompts("{A}",PadFamily::Nintendo)==u8(0xE808));
    assert(expand_prompts("{L1}",PadFamily::Deck)==u8(0xE814));
    assert(expand_prompts("{L1}",PadFamily::Xbox)==u8(0xE810));
    assert(expand_prompts("第{n}话 {Esc}",PadFamily::Xbox)=="第{n}话 "+u8(0xE840));
    assert(expand_prompts("{unknown} {",PadFamily::Xbox)=="{unknown} {");
    assert(expand_prompts("plain",PadFamily::Xbox)=="plain");
    assert(expand_prompts("E+{Enter}跳过",PadFamily::Deck)=="E+"+u8(0xE841)+"跳过");
    std::puts("ok");
}
