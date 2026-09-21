#pragma once
#include "dialogue_model.hpp"
#include "localization/catalog.hpp"
#include "json/json.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace plume { class RenderDevice; class RenderCommandList; class RenderFramebuffer; }
namespace srw64::dialogue {
// Game-thread expansion of a catalog label, including the current player names.
std::string ui_text(const uint8_t* ram,uint16_t id);
std::u16string utf16(const std::string&);
std::string utf8(const std::u16string&);
Layout typeset(const std::u16string&, unsigned font_size, double width=177, double height=35);
struct Box {
    bool visible{}, active{};
    unsigned slot{}, palette{};
    uint16_t text_id{};
    unsigned segment{}, page{};
    uint64_t event{};
    double x{}, y{};
    size_t revealed{};
    std::u16string speaker;
    Layout layout;
};
struct Frame {
    localization::Snapshot catalog;
    uint64_t vi{}, workload{};
    uint64_t reading_event{};
    AdvanceProgress advance;
    unsigned font_size=13, speed{};
    bool auto_read{}, history_open{}, fast{}, skipping{};
    size_t history_offset{};
    std::array<Box,2> boxes;
    std::deque<Entry> history;
    const Box* focused_box() const {
        const Box* active=nullptr;
        for(const auto& box:boxes)if(box.visible && box.active) {
            if(active)return nullptr; // No unique speaker in an unsupported state.
            active=&box;
        }
        if(active)return active;
        // Retain the last reader only during a visible handoff, never a closed
        // panel or a different workload's dialogue.
        if(reading_event)for(const auto& box:boxes)
            if(box.visible && box.event==reading_event)return &box;
        return nullptr;
    }
};
void configure(const std::filesystem::path&);
uint64_t request_locale(const std::string& locale);
struct LocaleStatus {uint64_t request{},completed{};std::string locale,error;};
LocaleStatus locale_status();
uint16_t input(uint16_t);
void overlay_loaded(uint32_t rom);
std::shared_ptr<const Frame> take_frame(uint32_t start, uint32_t size, std::vector<uint8_t>& display, const uint8_t* ram);
void queue_frame(uint64_t workload, std::shared_ptr<const Frame>);
std::shared_ptr<const Frame> presented_frame(uint64_t workload);
// Reader and dialogue boxes as dialogue-state.json holds them, from any thread;
// null when the native dialogue is not configured.
nlohmann::json state();
void metal_init(plume::RenderDevice*, const std::filesystem::path&);
void metal_draw(plume::RenderCommandList*, plume::RenderFramebuffer*, uint64_t workload);
void metal_shutdown();
}
