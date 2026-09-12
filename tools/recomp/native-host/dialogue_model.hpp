#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <map>

namespace srw64::dialogue {
struct Line { size_t start{}, end{}; double width{}; };
struct Page { size_t start{}, end{}; std::vector<Line> lines; };
struct Layout {
    std::u16string text;
    std::vector<size_t> clusters; // Exclusive UTF-16 ends, never split a grapheme.
    std::vector<Page> pages;
};
struct Entry {
    uint64_t event{};
    uint16_t text_id{};
    unsigned segment{};
    std::u16string speaker, text;
    bool warm_name{};
    bool complete{};
    std::map<std::string,std::u16string> localized;
};
struct PageTiming {
    size_t clusters{};
    double characters_per_second{};
    uint64_t reveal_vis{}, wait_vis{};
    uint64_t total_vis() const {return reveal_vis+wait_vis;}
};
struct AdvanceProgress {
    bool visible{}, waiting{}, paused{};
    unsigned permille{};
};
struct Reader {
    uint64_t event{}, tick{}, last_repeat{}, page_started{}, last_fast{}, last_speed{};
    unsigned speed{}, font_size=13;
    size_t page{}, visible{}, history_offset{}, history_scroll_limit{};
    uint16_t previous{};
    bool history_open{}, skipping{}, pending{}, active{}, auto_read{}, fast{};
    Layout layout;
    std::deque<Entry> history;
    static constexpr size_t history_limit=256;
    static constexpr unsigned max_speed=4;
    static constexpr uint16_t A=0x8000, B=0x4000, START=0x1000,
        UP=0x800, DOWN=0x400, L=0x20, R=0x10, BIGGER=8, SMALLER=4;

    void begin(uint64_t id, uint16_t text_id, unsigned segment,
               std::u16string speaker, Layout value, uint64_t now) {
        active=true;
        if (id==event) return;
        event=id; layout=std::move(value); page=visible=0; pending=false;
        page_started=tick=now; history_offset=0;
        // Assign the turn color once so scrolling and trimming old history
        // cannot recolor existing names. STOP fragments keep their speaker.
        const bool warm_name=!history.empty() &&
            (history.back().warm_name != (history.back().speaker!=speaker));
        history.push_back({id,text_id,segment,std::move(speaker),{},warm_name});
        if(history.size()>history_limit) history.pop_front();
    }
    void relayout(Layout value) {
        const size_t anchor = layout.pages.empty()?0:layout.pages[page].start;
        layout=std::move(value); page=0;
        while(page+1<layout.pages.size() && layout.pages[page].end<=anchor) ++page;
        visible=std::clamp(visible,layout.pages[page].start,layout.pages[page].end);
        page_started=tick; pending=false;
    }
    void switch_language(Layout value,const std::string& locale,uint64_t now) {
        for(auto& entry:history) {
            const auto found=entry.localized.find(locale);
            // Completed fragments can be redisplayed in full. A partial
            // fragment has no safe cross-language character-offset mapping.
            entry.text=entry.complete && found!=entry.localized.end()?found->second:std::u16string{};
        }
        layout=std::move(value);page=visible=history_offset=0;
        page_started=tick=now;pending=skipping=auto_read=fast=false;
    }
    void boundary(bool clear_history=false) {
        active=history_open=skipping=pending=auto_read=fast=false;
        event=0; page=visible=history_offset=0;
        if(clear_history)history.clear();
        // Keep previous held buttons: a held chord cannot become a fresh press
        // at the next script boundary.
    }
    void remember() {
        if(history.empty() || history.back().event!=event)return;
        const size_t end=std::min(visible,layout.text.size());
        if(end>history.back().text.size())history.back().text=layout.text.substr(0,end);
    }
    PageTiming page_timing() const {
        if(layout.pages.empty())return {};
        const auto& p=layout.pages.at(page);
        const auto first=std::upper_bound(layout.clusters.begin(),layout.clusters.end(),p.start);
        const size_t available=std::upper_bound(first,layout.clusters.end(),p.end)-first;
        const double cps=auto_read?12.0+speed*7.0:30.0;
        return {available,cps,uint64_t(available*60/cps),uint64_t((available*2+100)/(0.5+speed*0.45))};
    }
    AdvanceProgress advance_progress() const {
        if(!auto_read || fast || skipping || !event || layout.pages.empty())return {};
        const auto duration=page_timing().total_vis();
        const auto elapsed=tick>=page_started?tick-page_started:0;
        return {true,visible==layout.pages.at(page).end,history_open,
            pending?1000U:unsigned(std::min<uint64_t>(1000,duration?elapsed*1000/duration:0))};
    }
    // Returns a single guest confirmation only after the final host page.
    // No changes to guest pointers, event flags, or game time occur here.
    bool update(uint16_t buttons, uint64_t now) {
        const uint16_t pressed=buttons & ~previous;
        previous=buttons;
        const auto elapsed=now>=tick?std::min<uint64_t>(now-tick,6):0;
        tick=now;
        if(!active || layout.pages.empty())return false;
        if(pressed & L) {
            history_open=!history_open; history_offset=0; skipping=false;
            if(!history_open) {page_started+=elapsed;return false;}
        }
        if(history_open) {
            if(pressed & (A|B|START))history_open=false;
            const bool repeat=(pressed & (UP|DOWN)) || now-last_repeat>=10;
            if(repeat) {
                if(buttons&UP)history_offset=std::min(history_offset+1,history_scroll_limit);
                if(buttons&DOWN)history_offset=history_offset?history_offset-1:0;
                if(buttons&(UP|DOWN))last_repeat=now;
            }
            page_started+=elapsed;
            return false;
        }
        if(pressed & B) { auto_read=skipping=false; }
        if(pressed&BIGGER)font_size=std::min(font_size+1,18U);
        if(pressed&SMALLER)font_size=std::max(font_size-1,10U);
        const bool speed_repeat=(pressed & (UP|DOWN)) || now-last_speed>=18;
        if(speed_repeat && (buttons&(UP|DOWN)) && (buttons&(UP|DOWN))!=(UP|DOWN)) {
            if(buttons&UP)speed=std::min(speed+1,max_speed);
            else speed=speed?speed-1:0;
            auto_read=speed!=0;last_speed=now;
        }
        if((buttons&(R|START))==(R|START) && (pressed&(R|START))) {
            skipping=true; history_open=false;
        }
        fast=(buttons&(R|A))==(R|A);
        const auto& p=layout.pages[page];
        const auto timing=page_timing();
        const double cps=timing.characters_per_second;
        const auto first=std::upper_bound(layout.clusters.begin(),layout.clusters.end(),p.start);
        const size_t count=size_t((now-page_started)*cps/60.0);
        const auto available=timing.clusters;
        if(count)visible=std::max(visible,count>=available?p.end:*(first+count-1));
        visible=std::clamp(visible,p.start,p.end);
        if(fast || skipping)visible=p.end;
        remember();
        if(pending)return false;
        const bool advance = ((pressed&A) && !(buttons&R)) ||
            (fast && now-last_fast>=6) || skipping ||
            (auto_read && visible==p.end && now-page_started>=timing.total_vis());
        if(!advance)return false;
        last_fast=now;
        // A confirmed page remains readable in history even if its typewriter
        // animation was interrupted. Skip records the current script segment,
        // never future segments that the original script has not reached.
        visible=skipping?layout.text.size():p.end;remember();
        if(page+1<layout.pages.size() && !skipping) {
            ++page;visible=layout.pages[page].start;page_started=now;
            return false;
        }
        if(!history.empty() && history.back().event==event)history.back().complete=true;
        pending=true;return true;
    }
};
}
