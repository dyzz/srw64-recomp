#pragma once
#include "dialogue_model.hpp"
#include "localization/catalog.hpp"
#include "text/portable_text.hpp"
#include "json/json.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace plume { struct RenderInterface; struct RenderDevice; struct RenderCommandList; struct RenderFramebuffer; }
namespace srw64::dialogue {
// Game-thread expansion of a catalog label, including the current player names.
std::string ui_text(const uint8_t* ram,uint16_t id);
// A text page image's words (@intro:<resource> entries: opening and ending pages) in
// catalog form (<BR> lines, <STOP> blank lines, <END>); Japanese is the entries' original
// lines. Empty when no dialogue text file has the page. Any thread.
std::string page_text(const std::string& locale,unsigned resource);
// Original UI text (docs/native/native-ui-text.md), game thread. A label of the text
// engine (a 0x34-byte slot of the pool at 8015CB00) in the reading language: the
// record's translation while the label still shows that record's Japanese text, else
// the glyphs as drawn, lines split by '\n'. Empty when the reader is not configured.
std::string label_text(const uint8_t* ram,uint32_t label);
// Glyph codes as text, up to 0xFFFF; empty for an unterminated buffer.
std::string glyph_text(const uint8_t* ram,uint32_t address,size_t limit);
// One ROM font glyph as text.
std::string glyph_string(uint16_t code);
// A body text slot (0x218 bytes at 800FBAB0: choice and objective windows when the
// reader shows no box there): the page it draws, in the reading language while it is
// that record's Japanese page, else as drawn. Lines split by '\n'.
std::string body_page(const uint8_t* ram,uint32_t body);
// Whether the reader draws this label slot or this screen position itself: the
// speaker names and everything inside a dialogue box it shows.
bool reader_owns(unsigned label_slot,double x,double y);
bool reader_configured();
std::u16string utf16(const std::string&);
std::string utf8(const std::u16string&);
// Plain lines 1.22 x size apart: names, history and the typesetting check.
Layout typeset(const std::u16string&, double font_size, double width=177, double height=35);
// The text area under the name row (docs/design/dialogue-typesetting.md §4-7).
inline constexpr double body_width=177, body_height=43;
// The body size for the reader's font size setting: English takes 0.85 of it.
double body_size(unsigned setting);
// A story record or battle quote in the text area: as many lines as fit at the
// minimum spacing, spread over the height, page ends ranked. stops are the
// original's page breaks, which end a sentence; forced offsets start a page.
Layout typeset_body(const std::u16string&, double size, std::vector<size_t> stops={}, std::vector<size_t> forced={}, double width=body_width);
// The page rules typeset_body uses for a language.
text::PageStyle body_style(const std::string& locale, std::vector<size_t> stops={}, std::vector<size_t> forced={});

struct Box {
    bool visible{}, active{};
    unsigned slot{}, palette{};
    uint16_t text_id{};
    unsigned segment{}, page{};
    uint64_t event{};
    double x{}, y{};
    // Where it is drawn, if not at the original's x, y (which the glyphs are matched by),
    // and the text area's width: a battle quote's box moves and widens (battle_hud.hpp).
    double shift_x{}, shift_y{}, width=body_width;
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
    bool pad_hints{};                  // controls name the controller's buttons
    uint8_t pad_family{};              // whose button icons (text::PadFamily)
    // The reading controls and history hints with the player's bindings (text/button_prompts.hpp).
    std::string controls_text, history_controls_text;
    double bar_scale=1;                // the bottom bar's size: the interface size (settings::ui_scale)
    // Battle quotes: the translation replaces the drawn text, the original keeps
    // its own pacing, so there are no reading controls to show.
    bool display_only{};
    // The screen transition's black spans, [left, right) in original pixels for each of
    // the 240 lines (task 80099508 fills them over everything); empty when none runs.
    std::vector<std::pair<float,float>> cover;
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
// Read the dialogue text files again at the next game step (F5, app menu).
void request_reload();
// The player's dialogue text folder (beside the shipped files, which it overrides) and the
// last reading of them: {"locales": {locale: {entries, files, problems}}, "problems": n}.
std::filesystem::path text_overrides_dir();
nlohmann::json text_summary();
uint16_t input(uint16_t);
void overlay_loaded(uint32_t rom);
std::shared_ptr<const Frame> take_frame(uint32_t start, uint32_t size, std::vector<uint8_t>& display, const uint8_t* ram);
void queue_frame(uint64_t workload, std::shared_ptr<const Frame>);
std::shared_ptr<const Frame> presented_frame(uint64_t workload);
// Reader and dialogue boxes as dialogue-state.json holds them, from any thread;
// null when the native dialogue is not configured.
nlohmann::json state();
// The present hook's dialogue compositor, on any Plume backend (dialogue_plume.cpp).
void gpu_init(plume::RenderInterface*, plume::RenderDevice*, const std::filesystem::path&);
void gpu_draw(plume::RenderCommandList*, plume::RenderFramebuffer*, uint64_t workload);
void gpu_shutdown();
}
