#pragma once
#include "game_adapter/default_names.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace srw64::names {
// The original's steps. Players pick a protagonist and confirm; Player and Partner,
// the original name editors, are no longer shown (docs/native/default-names.md).
enum Stage : unsigned { Player=0, Partner=1, Review=2, Selection=3 };
// One of the four protagonists on the selection page, in the game's route order:
// 0 ブラッド (super, male), 1 マナミ (super, female), 2 アークライト (real, male),
// 3 セレイン (real, female). Route < 2 is super robot, even routes are male.
// A page image: the ROM original and, when the HD set carries one, the whole HD image.
struct Art {
    std::string original,hd;
    unsigned width{96},height{96};
};
struct Choice {
    std::array<std::array<std::u16string,2>,2> names;   // protagonist/partner x given/family defaults
    std::array<std::array<std::string,2>,2> portraits;  // protagonist/partner x original/HD
    std::array<Art,2> units;                            // protagonist/partner starting units' poses
    std::array<std::string,2> unit_names;               // UTF-8, in the reading language
};
// Names are the guest's own text, Japanese; the page shows defaults in the reading language.
struct Request {
    uint64_t serial{},revision{};
    unsigned person{};
    unsigned route{};
    bool visible{},active{},pending{};
    std::array<std::array<std::u16string,3>,2> names;  // Review: protagonist/partner x given/family/nickname
    std::array<Choice,4> choices;                      // `route` is the highlighted (Review: chosen) one
};
// Default names by reading language (docs/native/default-names.md). Filled while the
// host starts, before the game runs; read-only afterwards on every thread.
DefaultNames& default_names();
void configure(const std::filesystem::path&);
void initialize_rom(const uint8_t*,size_t);
void overlay_loaded(uint32_t rom,uint32_t ram,uint32_t size);
bool owns_input();
void window_claim_input(bool);
uint16_t input(uint16_t buttons);
Request request();
// Font glyph codes (a 0xFFFF- or blank-terminated name buffer) as text; empty when unknown.
std::u16string decode_glyphs(const std::vector<uint16_t>& codes);
// Review: confirm starts the story; false returns to the selection.
void review(uint64_t serial,bool confirm);
// Selection: highlight a route (the game plays its cursor sound) or confirm it, which
// commits both people's default names and continues to the review.
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
