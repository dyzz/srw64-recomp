#pragma once
#include "dialogue_model.hpp"
#include "text/portable_text.hpp"
#include <utility>

namespace srw64::dialogue {
// Reader ranges and rendered glyphs share the same immutable layout.
inline Layout reader_layout(const text::TextLayout& shaped,double height=35) {
    Layout result;result.text=shaped.text();result.clusters=shaped.clusters();
    result.shaped=std::make_shared<const text::TextLayout>(shaped);
    for(const auto& p:shaped.pages(height)) {
        Page page;page.start=p.start;page.end=p.end;
        for(size_t i=0;i<p.line_count;++i) {
            const auto& line=shaped.lines().at(p.first_line+i);
            page.lines.push_back({line.start,line.end,line.width});
        }
        result.pages.push_back(std::move(page));
    }
    return result;
}
} // namespace srw64::dialogue
