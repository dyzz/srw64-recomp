#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace srw64::names {
enum Stage : unsigned { Player=0, Partner=1, Review=2, Selection=3 };
// One of the four protagonists on the selection page, in the game's route order:
// 0 ブラッド (super, male), 1 マナミ (super, female), 2 アークライト (real, male),
// 3 セレイン (real, female). Route < 2 is super robot, even routes are male.
struct Choice {
    std::array<std::array<std::u16string,2>,2> names;   // protagonist/partner x given/family defaults
    std::array<std::array<std::string,2>,2> portraits;  // protagonist/partner x original/HD
};
struct Request {
    uint64_t serial{},revision{};
    unsigned person{};
    unsigned route{};
    bool visible{},active{},pending{};
    std::array<std::u16string,3> values;
    std::array<std::array<std::u16string,3>,2> names;
    std::array<std::array<std::string,2>,2> portraits; // person, original/HD
    std::array<Choice,4> choices;                      // Selection only; `route` is the highlighted one
    std::string error;
};
void configure(const std::filesystem::path&);
void initialize_rom(const uint8_t*,size_t);
void overlay_loaded(uint32_t rom,uint32_t ram,uint32_t size);
bool descriptor(uint16_t table,uint16_t id,uint32_t& offset,uint32_t& size);
bool read(uint32_t rom,uint8_t* ram,uint32_t destination,uint32_t size);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
Request request();
std::string validate(const std::u16string&,unsigned field);
// Font glyph codes (a 0xFFFF- or blank-terminated name buffer) as text; empty when unknown.
std::u16string decode_glyphs(const std::vector<uint16_t>& codes);
void submit(uint64_t serial,const std::array<std::u16string,3>& values,bool cancel=false);
// Review: confirm starts the story; false returns to the protagonist editor.
void review(uint64_t serial,bool confirm);
// Selection: highlight a route (the game plays its cursor sound) or confirm it,
// which continues to the protagonist's name as the original はい does.
void select(uint64_t serial,unsigned route);
void choose(uint64_t serial,unsigned route);
void queue_cover(uint64_t workload,bool visible);
bool frame_cover(uint64_t workload);
void cover_presented(uint64_t workload,bool visible);
bool cover_in_flight();
// Platform backend: main/window thread only, never reads guest memory.
void window_init(void* cocoa_window,const std::filesystem::path&);
void window_update();
void window_shutdown();
}
