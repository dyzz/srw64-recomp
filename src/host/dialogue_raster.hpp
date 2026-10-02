#pragma once
#include "native_dialogue.hpp"
#include "presentation/raster_image.hpp"
#include <string>
#include <utility>
#include <vector>

namespace srw64::dialogue {
struct RasterizedFrame {
    presentation::Bgra8Surface image;
    nlohmann::json report;
};
// CPU-only backend boundary. Owns its output; no GPU state, filesystem writes,
// live guest reads or presentation callbacks. Uses the frame's pinned catalog.
// The compositor owns cache/upload/presentation and diagnostic file publication.
// picture_width: the game picture's width in original pixels (game_frame.hpp), the
// original 320 x 240 centred in it.
RasterizedFrame rasterize_frame(const Frame&, uint32_t width, uint32_t height, double picture_width = 320);

// Pixel rectangle, right and bottom exclusive.
struct PixelRect {
    int left{}, top{}, right{}, bottom{};
    bool empty() const { return right <= left || bottom <= top; }
    uint32_t width() const { return empty() ? 0 : uint32_t(right - left); }
    uint32_t height() const { return empty() ? 0 : uint32_t(bottom - top); }
    PixelRect operator&(const PixelRect&) const;
    PixelRect operator|(const PixelRect&) const;
    bool operator==(const PixelRect&) const = default;
};

// The same pictures as rasterize_frame, kept up to date by patches. A frame differs
// from the one before mostly in a line being revealed or a progress bar growing, so
// only the rectangles whose drawing changed are painted again, not the whole window.
class IncrementalRaster {
public:
    struct Patch {
        PixelRect rect;
        presentation::Bgra8Surface pixels;  // rect's size: the frame's pixels there
    };
    struct Update {
        std::vector<Patch> patches;  // applied in order, they turn the last picture into this one
        PixelRect drawn;             // all that the frame draws; transparent outside it
    };
    // context names what the drawing's keys do not: the catalog's font and revision.
    // A change of it or of the size repaints everything.
    Update update(const Frame&, uint32_t width, uint32_t height, double picture_width, const std::string& context);
    void reset() { valid_ = false; drawn_.clear(); }
private:
    bool valid_{};
    std::string context_;
    uint32_t width_{}, height_{};
    std::vector<std::pair<std::string, PixelRect>> drawn_;  // the last picture's drawing, sorted
};
}
