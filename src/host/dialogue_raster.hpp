#pragma once
#include "native_dialogue.hpp"
#include "presentation/raster_image.hpp"

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
}
