#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace srw64::names {
enum Stage : unsigned { Player=0, Partner=1, Review=2, Selection=3 };
struct Request {
    uint64_t serial{},revision{};
    unsigned person{};
    unsigned route{};
    bool visible{},active{},pending{};
    std::array<std::u16string,3> values;
    std::array<std::array<std::u16string,3>,2> names;
    std::array<std::array<std::string,2>,2> portraits; // person, original/HD
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
void submit(uint64_t serial,const std::array<std::u16string,3>& values,bool cancel=false);
// Review: confirm starts the story; false returns to the protagonist editor.
void review(uint64_t serial,bool confirm);
void queue_cover(uint64_t workload,bool visible);
bool frame_cover(uint64_t workload);
void cover_presented(uint64_t workload,bool visible);
bool cover_in_flight();
// Platform backend: main/window thread only, never reads guest memory.
void window_init(void* cocoa_window,const std::filesystem::path&);
void window_update();
void window_shutdown();
}
