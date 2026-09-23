#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace srw64::text { class TextLayout; }

namespace srw64::dialogue {
struct Line { size_t start{}, end{}; double width{}; };
struct Page { size_t start{}, end{}; std::vector<Line> lines; };
struct Layout {
    std::u16string text;
    std::vector<size_t> clusters; // Exclusive UTF-16 ends, never split a grapheme.
    std::vector<Page> pages;
    // Pins shaped glyphs and font bytes for the exact queued game frame.
    std::shared_ptr<const text::TextLayout> shaped;
};
// One history entry: a whole story record (every original page of it) or a host notice.
struct Entry {
    uint64_t event{};
    uint16_t text_id{};
    std::u16string speaker, text;
    bool warm_name{};
    bool complete{};
    std::map<std::string,std::u16string> localized, localized_speaker;
    bool notice{};   // a host line such as an upgrade refund, not a dialogue record
    std::map<std::string,std::vector<size_t>> localized_stops;  // each language's original page starts
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
// A story record as one text: the original's pages (split at '\f') joined,
// directly in Chinese and Japanese and with a space in English, and the
// offsets where the later pages start.
struct Record {std::u16string text;std::vector<size_t> stops;};
inline Record joined_record(const std::u16string& pages,const std::string& locale) {
    Record record;
    const auto blank=[](char16_t c){return c==u' ' || c==u'\n' || c==u'\u3000';};
    size_t start=0;
    for(bool first=true;;first=false) {
        const auto end=pages.find(u'\f',start);
        const auto part=pages.substr(start,end==std::u16string::npos?std::u16string::npos:end-start);
        if(!first) {
            if(locale=="en" && !record.text.empty() && !part.empty() && !blank(record.text.back()) && !blank(part.front()))
                record.text+=u' ';
            record.stops.push_back(record.text.size());
        }
        record.text+=part;
        if(end==std::u16string::npos)return record;
        start=end+1;
    }
}
// What the host confirms for the original this frame (docs/design/dialogue-typesetting.md §3).
enum class Confirm {none,stop,end};
struct Reader {
    uint64_t event{}, tick{}, last_repeat{}, page_started{}, last_fast{}, last_speed{};
    unsigned speed{}, font_size=13;
    // A story record is read as one text laid out in the host's own pages.
    // stops are the offsets where the original's later pages (after each
    // <STOP>) begin; guest is the original page the game shows now. The host
    // confirms an original page once its own page reaches the next one.
    std::vector<size_t> stops;
    unsigned guest{}, stop_sent=~0U;
    uint64_t stop_sent_at{};
    bool end_sent{};
    static constexpr uint64_t stop_pause_vis=18;   // the typewriter rests 0.3 s at an original page break
    static constexpr uint64_t stop_retry_vis=30;   // an A the original ignored is sent again
    size_t page{}, visible{}, history_offset{}, history_scroll_limit{};
    uint16_t previous{};
    bool history_open{}, skipping{}, pending{}, active{}, auto_read{}, fast{};
    Layout layout;
    std::deque<Entry> history;
    static constexpr size_t history_limit=256;
    static constexpr unsigned max_speed=4;
    static constexpr uint16_t A=0x8000, B=0x4000, START=0x1000,
        UP=0x800, DOWN=0x400, L=0x20, R=0x10, BIGGER=8, SMALLER=4;

    void begin(uint64_t id, uint16_t text_id, std::u16string speaker, Layout value,
               uint64_t now, std::vector<size_t> record_stops={}) {
        active=true;
        if (id==event) return;
        event=id; layout=std::move(value); page=visible=0; pending=false;
        stops=std::move(record_stops); guest=0; stop_sent=~0U; end_sent=false;
        page_started=tick=now; history_offset=0;
        // Assign the turn color once so scrolling and trimming old history
        // cannot recolor existing names. STOP fragments keep their speaker.
        const auto previous=std::find_if(history.rbegin(),history.rend(),[](const Entry& e){return !e.notice;});
        const bool warm_name=previous!=history.rend() &&
            (previous->warm_name != (previous->speaker!=speaker));
        history.push_back({id,text_id,std::move(speaker),{},warm_name});
        if(history.size()>history_limit) history.pop_front();
    }
    // A host notice in every language. It goes before the fragment being read, so
    // that fragment stays last for remember() and the completion mark.
    void note(uint64_t id,std::map<std::string,std::u16string> localized,const std::string& locale) {
        Entry entry{id,0,{},{},false,true,std::move(localized),{},true};
        const auto found=entry.localized.find(locale);
        if(found!=entry.localized.end())entry.text=found->second;
        auto at=history.end();
        if(event && !history.empty() && history.back().event==event)--at;
        history.insert(at,std::move(entry));
        if(history.size()>history_limit) history.pop_front();
    }
    // The page the current one starts at; a new font size keeps it a page start.
    size_t page_start() const {return layout.pages.empty()?0:layout.pages[page].start;}
    // The offset where the original page the game shows now begins.
    size_t guest_start() const {return guest && guest<=stops.size()?stops[guest-1]:0;}
    void relayout(Layout value) {
        const size_t anchor=page_start();
        layout=std::move(value); page=0;
        while(page+1<layout.pages.size() && layout.pages[page].end<=anchor) ++page;
        visible=std::clamp(visible,layout.pages[page].start,layout.pages[page].end);
        page_started=tick; pending=false;
    }
    // The original page (0 = the first) the current host page reaches into.
    unsigned required_guest() const {
        if(layout.pages.empty())return 0;
        const size_t end=layout.pages.at(page).end;
        return unsigned(std::lower_bound(stops.begin(),stops.end(),end)-stops.begin());
    }
    size_t page_stop_count(const Page& p) const {
        return size_t(std::lower_bound(stops.begin(),stops.end(),p.end)-std::upper_bound(stops.begin(),stops.end(),p.start));
    }
    // Clusters are revealed at cps; an original page break inside the page
    // holds the reveal for stop_pause_vis first.
    size_t revealed_at(const Page& p,uint64_t elapsed,double cps) const {
        const auto after=[&](size_t from,size_t count) {
            auto it=std::upper_bound(layout.clusters.begin(),layout.clusters.end(),from);
            if(!count || it==layout.clusters.end())return from;
            const size_t left=size_t(layout.clusters.end()-it);
            return *(it+std::ptrdiff_t(std::min(count,left)-1));
        };
        const auto between=[&](size_t a,size_t b) {
            return size_t(std::upper_bound(layout.clusters.begin(),layout.clusters.end(),b)-std::upper_bound(layout.clusters.begin(),layout.clusters.end(),a));
        };
        size_t at=p.start;double time=double(elapsed);
        for(auto it=std::upper_bound(stops.begin(),stops.end(),p.start);it!=stops.end() && *it<p.end;++it) {
            const double need=between(at,*it)*60.0/cps;
            if(time<need)return std::min(p.end,after(at,size_t(time*cps/60.0)));
            time-=need;at=*it;
            if(time<double(stop_pause_vis))return at;
            time-=double(stop_pause_vis);
        }
        return std::min(p.end,after(at,size_t(time*cps/60.0)));
    }
    // The record in another language. Reading resumes where the original page
    // the game shows begins; value must start a page there (the stops map one
    // to one between languages, so that point exists in both).
    void switch_language(Layout value,std::vector<size_t> record_stops,const std::string& locale,uint64_t now) {
        stops=std::move(record_stops);
        const size_t anchor=guest_start();
        for(auto& entry:history) {
            const auto found=entry.localized.find(locale);
            // Completed records are redisplayed in full; the one being read up
            // to the original page the game shows.
            entry.text=entry.complete && found!=entry.localized.end()?found->second:std::u16string{};
            if(!entry.complete && entry.event==event)entry.text=value.text.substr(0,anchor);
            if(const auto name=entry.localized_speaker.find(locale);name!=entry.localized_speaker.end())entry.speaker=name->second;
        }
        layout=std::move(value);page=visible=history_offset=0;
        while(page+1<layout.pages.size() && layout.pages[page].end<=anchor)++page;
        if(!layout.pages.empty())visible=layout.pages[page].start;
        page_started=tick=now;pending=skipping=auto_read=fast=false;end_sent=false;
    }
    // One A at a time: for the next original page once the host page reaches
    // it (again if the original ignored it), then <END> after the last page.
    // Confirmations only move forward, whatever the reader does.
    Confirm confirmation(uint64_t now) {
        if(!active || !event || layout.pages.empty() || history_open)return Confirm::none;
        const unsigned want=pending?unsigned(stops.size()):required_guest();
        if(guest<want) {
            if(guest==stop_sent && now-stop_sent_at<stop_retry_vis)return Confirm::none;
            stop_sent=guest;stop_sent_at=now;return Confirm::stop;
        }
        if(pending && !end_sent) {end_sent=true;return Confirm::end;}
        return Confirm::none;
    }
    void boundary(bool clear_history=false) {
        active=history_open=skipping=pending=auto_read=fast=end_sent=false;
        event=0; page=visible=history_offset=0; stops.clear(); guest=0; stop_sent=~0U;
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
        return {available,cps,uint64_t(available*60/cps)+page_stop_count(p)*stop_pause_vis,uint64_t((available*2+100)/(0.5+speed*0.45))};
    }
    AdvanceProgress advance_progress() const {
        if(!auto_read || fast || skipping || !event || layout.pages.empty())return {};
        const auto duration=page_timing().total_vis();
        const auto elapsed=tick>=page_started?tick-page_started:0;
        return {true,visible==layout.pages.at(page).end,history_open,
            pending?1000U:unsigned(std::min<uint64_t>(1000,duration?elapsed*1000/duration:0))};
    }
    // Returns true once, when the final host page is confirmed; confirmation()
    // then sends the original's remaining pages and <END>. No changes to guest
    // pointers, event flags, or game time occur here.
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
        visible=std::max(visible,revealed_at(p,now-page_started,cps));
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
        // animation was interrupted. Skip records the whole record: the
        // original shows every page of it before its <END>.
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
