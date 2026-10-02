#include "native_dialogue.hpp"
#include "battle_hud.hpp"
#include "native_name_entry.hpp"
#include "game_hooks.hpp"
#include "script_skip.hpp"
#include "state_probe.hpp"
#include "presentation_settings.hpp"
#include "notices.hpp"
#include "upgrade_refund.hpp"
#include "campaign.hpp"
#include "input_mode.hpp"
#include "text/button_prompts.hpp"
#include "game_adapter/dialogue_source.hpp"
#include "localization/dialogue_text.hpp"
#include "presentation/display_list_snapshots.hpp"
#include "json/json.hpp"
#include <algorithm>
#include <atomic>
#include <ctime>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>

uint64_t srw64_current_vi();
uint32_t srw64_pad_state();
uint32_t srw64_keyboard_state();
namespace srw64::dialogue {
namespace {
using json=nlohmann::json;
constexpr uint32_t body_base=0xFBAB0, name_base=0x15CB00, stride=0x218;
std::recursive_mutex mutex;
std::atomic<uint16_t> raw_buttons{};
std::atomic_bool auto_toggle{};              // an L2 press, for the next reading step
std::atomic_bool owns_input{};
uint16_t consumed_hold{};
bool enabled{}, observe{}, scene_supported{}, display_only{};
uint32_t overlay{}, executing_owner{}, reading_owner{}, skip_owner{};
uint64_t generation{}, event_serial{};
std::array<uint64_t,2> loads{}, events{};
std::array<std::pair<uint64_t,unsigned>,2> identities{};
Reader reader;
LocaleStatus language_status;
unsigned reader_font_size=13;
std::array<Box,2> current;
std::map<uint16_t,std::string> glyphs;
std::filesystem::path output;
std::ofstream log;
presentation::DisplayListSnapshots<Frame> drawings;
std::map<uint64_t,std::shared_ptr<const Frame>> frames;
// Dialogue text files (docs/guide/dialogue-text.md): the catalogs as the profile
// built them, the bundled and the player's roots, and a pending F5 reload.
std::map<std::string,localization::Snapshot> base_catalogs;
std::filesystem::path text_bundled, text_overrides;
// @intro:<resource> text pages by locale, in catalog form; Japanese from their '>' lines.
std::mutex page_mutex;
std::map<std::string,std::map<std::string,std::string>> page_texts;
std::atomic_bool reload_requested{};

uint8_t byte(const uint8_t* ram,uint32_t p) { return ram[p^3]; }
uint16_t half(const uint8_t* ram,uint32_t p) { uint16_t v;std::memcpy(&v,ram+(p^2),2);return v; }
uint32_t word(const uint8_t* ram,uint32_t p) { uint32_t v;std::memcpy(&v,ram+p,4);return v; }
void record(const char* kind,json extra=json::object()) {
    extra["schema"]="srw64.native-dialogue-event.v1";
    extra["kind"]=kind;extra["vi"]=srw64_current_vi();extra["overlay"]=overlay;
    log<<extra.dump()<<'\n';log.flush();
}
std::string glyph(uint16_t code) {
    auto it=glyphs.find(code);
    if(it!=glyphs.end())return it->second;
    // Keep an explicit visible diagnostic for an unresolved game-specific icon.
    char text[24];std::snprintf(text,sizeof(text),"〔%04X〕",code);return text;
}
std::string decode(const uint8_t* ram,uint32_t address,size_t limit) {
    if(address>=0x800000 || limit>(0x800000-address)/2)return {};
    std::string text;
    for(size_t i=0;i<limit;++i) {
        const uint16_t code=half(ram,address+2*i);
        if(code==0xFFFF)return text;
        if(code==0xFFFE)text+='\n';
        else if(code==0xFFFD)text+='\f';
        else text+=glyph(code);
    }
    return {}; // Unterminated guest buffers are not accepted as dialogue.
}
// The name fields the dynamic tokens <G:0124>..<G:012C> read, in token order: the
// protagonist's nickname, full, given and family name, the partner's the same, the unit.
struct NameSlot {uint32_t address;names::Field field;bool full;};
constexpr NameSlot name_slots[]={{0x10F638,names::Field::Nick,false},{0x10F650,names::Field::Name,true},
    {0x10F5F8,names::Field::Name,false},{0x10F618,names::Field::Surname,false},
    {0x10F644,names::Field::Nick,false},{0x10F674,names::Field::Name,true},
    {0x10F608,names::Field::Name,false},{0x10F628,names::Field::Surname,false},
    {0x10F698,names::Field::Unit,false}};
// A name field as the reading language shows it: a default in that language, any
// other name as stored (docs/native/default-names.md).
std::string shown_name(const std::string& stored,const NameSlot& slot,const std::string& locale) {
    const auto& table=names::default_names();
    return slot.full?table.display_full(stored,locale):table.display(slot.field,stored,locale);
}
std::string expand(const uint8_t* ram,const std::string& text,const std::string& locale) {
    std::string result;
    for(size_t i=0;i<text.size();) {
        if(text[i]!='<') {result+=text[i++];continue;}
        const auto end=text.find('>',i);
        if(end==std::string::npos)throw std::runtime_error("Invalid native text token");
        const auto token=text.substr(i+1,end-i-1);
        if(token=="BR")result+='\n';
        else if(token=="STOP")result+='\f';
        else if(token=="END")break;
        else if(token.rfind("G:",0)==0) {
            const auto code=std::stoul(token.substr(2),nullptr,16);
            if(code>=0x124 && code<=0x12C) {
                const auto& slot=name_slots[code-0x124];
                result+=shown_name(decode(ram,slot.address,16),slot,locale);
                // The guest encodes a name field as repeated copies of the
                // same token; the runtime expands that entire run once.
                const auto raw=text.substr(i,end-i+1);
                size_t next=end+1;
                while(text.compare(next,raw.size(),raw)==0)next+=raw.size();
                i=next;continue;
            }
            result+=glyph(uint16_t(code));
        }
        else throw std::runtime_error("Unsupported native text token");
        i=end+1;
    }
    return result;
}
// The original fetcher (8008CF14) swaps the protagonists' and partners' name
// records for the name fields; every other record is its text.
const NameSlot* entered_name(uint16_t id) {
    if(uint16_t(id-0x1137)<4)return &name_slots[0];
    if(uint16_t(id-0x113B)<4)return &name_slots[4];
    if(uint16_t(id-0x12A0)<4)return &name_slots[1];
    if(uint16_t(id-0x12A4)<4)return &name_slots[5];
    return nullptr;
}
std::string record_text(const uint8_t* ram,const localization::Catalog& catalog,uint16_t id) {
    if(const auto* slot=entered_name(id))
        if(auto name=decode(ram,slot->address,16);!name.empty())return shown_name(name,*slot,catalog.locale);
    const auto* value=catalog.resolve(localization::TextKey::base(0,id));
    return value?expand(ram,*value,catalog.locale):std::string();
}
// The speaker label holds its record id at +0 and the glyphs it shows at +12.
// Show the record in the reading language while the label still shows that
// record's Japanese text, and a name field as shown_name does; anything else
// stays as drawn.
std::string speaker_name(const uint8_t* ram,uint32_t label,const localization::Catalog& language) {
    const auto shown=decode(ram,label+12,20);
    const uint16_t id=half(ram,label);
    if(const auto* slot=entered_name(id))return shown_name(shown,*slot,language.locale);
    const auto japanese=localization::find("ja");
    if(!japanese || record_text(ram,*japanese,id)!=shown)return shown;
    return record_text(ram,language,id);
}
// A battle quote cannot be paged: the original advances it on its own clock.
// Shrink the text until it fits the box; the log names any that still overflow.
Layout fitted(const std::u16string& body,double size,double width) {
    for(double s=size;s>9;s-=1)if(auto layout=typeset_body(body,s,{},{},width);layout.pages.size()<=1)return layout;
    return typeset_body(body,9,{},{},width);
}
std::string segment(const std::string& text,unsigned number) {
    size_t start=0;
    for(unsigned i=0;i<number;++i) {
        auto p=text.find('\f',start);if(p==std::string::npos)return {};
        start=p+1;
    }
    return text.substr(start,text.find('\f',start)-start);
}
// A story record in one language. The game runs the Japanese ROM record, so
// its pages decide how many the host confirms: a translation with fewer
// original pages confirms the rest at its end, extra ones just flow on.
Record story_record(const uint8_t* ram,const localization::Catalog& catalog,uint16_t text_id,uint32_t p) {
    const auto key=game_adapter::standard_dialogue_key(text_id);
    const auto* message=catalog.resolve(key);
    auto record=joined_record(utf16(message?expand(ram,*message,catalog.locale):decode(ram,p+12,256)),catalog.locale);
    const auto japanese=localization::find("ja");
    if(const auto* original=japanese?japanese->resolve(key):nullptr) {
        const auto end=original->find("<END>");
        size_t stops=0;
        for(auto at=original->find("<STOP>");at<end;at=original->find("<STOP>",at+6))++stops;
        record.stops.resize(stops,record.text.size());
    }
    return record;
}
void cancel(const char* reason,bool clear=false) {
    if(reader.active || reader.skipping || reader.history_open)record("boundary",{{"reason",reason}});
    reader.boundary(clear);current={};owns_input=false;reading_owner=skip_owner=0;script_skip::end();
}
void refresh(uint8_t* ram,bool create_events) {
    if(!scene_supported) {current={};owns_input=false;return;}
    unsigned active_count=0;
    for(unsigned slot=0;slot<2;++slot) {
        const uint32_t p=body_base+slot*stride, np=name_base+slot*52;
        const auto status=byte(ram,p+2), name_status=byte(ram,np+2);
        Box box;box.slot=slot;
        const int x=int32_t(word(ram,p+4)),y=int32_t(word(ram,p+8));
        if(status<1 || status>3 || name_status!=1 || x<0 || x>135 || y<15 || y>190) {
            current[slot]={};continue;
        }
        // Normal script dialogue has a 189 x 61 panel and a speaker name;
        // standalone menu/title strings never qualify for this adapter.
        const auto object=0xFFA70+((0x20+slot)*0xC4);
        float panel_width{},panel_height{};
        auto w=word(ram,object+0x10),h=word(ram,object+0x14);
        std::memcpy(&panel_width,&w,4);std::memcpy(&panel_height,&h,4);
        if(panel_width!=189 || panel_height!=61) {current[slot]={};continue;}
        box.visible=true;box.active=status==1;box.text_id=half(ram,p);
        box.segment=byte(ram,p+0x215);box.palette=byte(ram,p+3);
        box.x=x+4;box.y=y+4;
        // A battle quote's box follows the portrait to the picture's edge, wider (battle_hud.hpp).
        if(display_only){const auto moved=battle_hud::quote();box.shift_x=moved.dx;box.shift_y=moved.dy;box.width+=moved.extra;}
        box.speaker=utf16(speaker_name(ram,np,localization::catalog()));
        if(box.speaker.empty()) {current[slot]={};continue;}
        // A battle quote follows the original page by page; a story record is read whole.
        Record source;
        if(display_only) {
            const auto* message=localization::catalog().resolve(game_adapter::standard_dialogue_key(box.text_id));
            source.text=utf16(segment(message?expand(ram,*message,localization::catalog().locale):decode(ram,p+12,256),box.segment));
        } else source=story_record(ram,localization::catalog(),box.text_id,p);
        const auto& body=source.text;
        if(body.empty()) {current[slot]={};continue;}
        const std::pair<uint64_t,unsigned> identity{loads[slot],display_only?box.segment:0};
        if(identity!=identities[slot]) {
            identities[slot]=identity;events[slot]=++event_serial;
        }
        box.event=events[slot];
        if(current[slot].event==box.event && current[slot].layout.text==body &&
           reader.font_size==reader_font_size && current[slot].width==box.width)box.layout=current[slot].layout;
        else {
            box.layout=display_only?fitted(body,body_size(reader.font_size),box.width):typeset_body(body,body_size(reader.font_size),source.stops);
            if(display_only && box.layout.pages.size()>1)record("battle_overflow",{{"text_id",box.text_id},{"segment",box.segment},{"pages",box.layout.pages.size()}});
        }
        box.page=0;box.revealed=body.size();
        if(display_only) {
            if(box.event!=current[slot].event)record("battle_quote",{{"event",box.event},{"slot",slot},{"text_id",box.text_id},
                {"segment",box.segment},{"speaker",utf8(box.speaker)},{"text",utf8(body)},{"guest_mode",byte(ram,p+0x20E)}});
        } else if(box.active) {
            ++active_count;
            if(create_events && reader.event!=box.event) {
                reader.begin(box.event,box.text_id,box.speaker,box.layout,srw64_current_vi(),source.stops);
                auto& entry=reader.history.back();
                for(const auto& [locale,catalog]:localization::registered()) {
                    auto localized=story_record(ram,*catalog,box.text_id,p);
                    entry.localized[locale]=std::move(localized.text);
                    entry.localized_stops[locale]=std::move(localized.stops);
                    entry.localized_speaker[locale]=utf16(speaker_name(ram,np,*catalog));
                }
                srw64::state_probe::capture(ram,"dialogue-fragment",(uint32_t(box.text_id)<<8)|box.segment);
                record("fragment",{{"event",box.event},{"slot",slot},{"text_id",box.text_id},
                    {"text_key",game_adapter::standard_dialogue_key(box.text_id).value},
                    {"segment",box.segment},{"stops",source.stops},{"speaker",utf8(box.speaker)},{"text",utf8(body)},
                    {"pages",box.layout.pages.size()},{"owner",reading_owner},
                    {"guest_mode",byte(ram,p+0x20E)},{"guest_wait",half(ram,p+0x210)}});
            }
            if(reader.event==box.event) {
                box.layout=reader.layout;box.page=reader.page;box.revealed=reader.visible;
                reader.guest=box.segment;
            }
        } else if(current[slot].event==box.event && reader.font_size==reader_font_size) {
            box.page=std::min<size_t>(current[slot].page,box.layout.pages.size()-1);box.revealed=body.size();
        } else box.page=box.layout.pages.size()-1;
        current[slot]=std::move(box);
    }
    reader_font_size=reader.font_size;
    reader.active=!display_only && active_count==1;
    owns_input=reader.active && !observe;
}
json state_snapshot() {
    json state={{"schema","srw64.native-dialogue-state.v1"},{"vi",srw64_current_vi()},
        {"locale",localization::catalog().locale},{"catalog",localization::catalog().revision},
        {"active",reader.active},{"event",reader.event},{"page",reader.page},
        {"pages",reader.layout.pages.size()},{"revealed_utf16",reader.visible},
        {"font_size",reader.font_size},{"speed",reader.speed},{"automatic",reader.auto_read},{"fast",reader.fast},
        {"history_open",reader.history_open},{"history_entries",reader.history.size()},
        {"history_offset",reader.history_offset},{"skipping",reader.skipping},
        {"guest_segment",reader.guest},{"stops",reader.stops},{"page_start",reader.page_start()},
        {"page_end",reader.layout.pages.empty()?0:reader.layout.pages[reader.page].end},{"pending",reader.pending},
        {"owner",reading_owner},{"boxes",json::array()},{"history",json::array()}};
    for(const auto& entry:reader.history)state["history"].push_back({{"event",entry.event},
        {"text_id",entry.text_id},{"complete",entry.complete},{"notice",entry.notice},
        {"text",utf8(entry.text)}});
    for(const auto& box:current)if(box.visible)state["boxes"].push_back({{"active",box.active},
        {"event",box.event},{"text_id",box.text_id},{"text_key",game_adapter::standard_dialogue_key(box.text_id).value},
                    {"segment",box.segment},{"speaker",utf8(box.speaker)},
        {"text",utf8(box.layout.text)},{"page",box.page},{"pages",box.layout.pages.size()}});
    return state;
}
// Upgrade refund (upgrade_refund.hpp): a history line in every language and a
// banner in the current one, the machine named in each language.
std::string grouped(uint32_t value) {
    const auto digits=std::to_string(value);
    std::string result;
    for(size_t i=0;i<digits.size();++i){if(i && (digits.size()-i)%3==0)result+=',';result+=digits[i];}
    return result;
}
std::string filled(std::string text,const std::string& token,const std::string& value) {
    for(size_t at=text.find(token);at!=std::string::npos;at=text.find(token,at+value.size()))text.replace(at,token.size(),value);
    return text;
}
void refund_notice(const uint8_t* ram,uint16_t unit,uint32_t amount) {
    std::string name;
    for(const uint16_t code:refund::unit_name(unit))name+=glyph(code);
    std::map<std::string,std::u16string> localized;
    for(const auto& [locale,catalog]:localization::registered()) {
        // The unit's name in each language; the ROM glyphs when a catalog lacks it.
        auto local=record_text(ram,*catalog,uint16_t(refund::unit_name_base+unit));
        localized[locale]=utf16(filled(filled(catalog->ui("refund_notice"),"{unit}",local.empty()?name:local),"{amount}",grouped(amount)));
    }
    const auto locale=localization::catalog().locale;
    const auto text=utf8(localized[locale]);
    {
        std::lock_guard lock(mutex);
        reader.note(++event_serial,std::move(localized),locale);
        record("refund_notice",{{"unit",unit},{"name",name},{"amount",amount},{"text",text}});
    }
    notices::post("refund",text);
}
// Layer every locale's text files over the profile's catalog and swap them in.
// The report lists each problem; broken entries fall back to the layer below.
// Returns the entries loaded per locale and the problems in all of them.
std::pair<std::map<std::string,size_t>,size_t> load_text() {
    std::map<std::string,localization::Snapshot> next;
    std::vector<localization::dialogue_text::Problem> problems;
    json summary=json::object();std::map<std::string,size_t> entries;
    std::map<std::string,std::map<std::string,std::string>> pages;
    for(const auto& [locale,base]:base_catalogs) {
        const std::vector<std::filesystem::path> roots{text_bundled.empty()?text_bundled:text_bundled/locale,
                                                       text_overrides.empty()?text_overrides:text_overrides/locale};
        auto result=localization::dialogue_text::load(roots,*base);
        for(auto problem:result.problems){problem.path=locale+"/"+problem.path;problems.push_back(std::move(problem));}
        summary[locale]={{"entries",result.targets.size()},{"intro",result.intro.size()},{"files",result.files},{"problems",result.problems.size()}};
        entries[locale]=result.targets.size();
        next[locale]=result.targets.empty()?base:base->with_translations(result.targets,"dialogue-text");
        for(auto& [key,value]:result.intro)pages[locale][key]=std::move(value);
        for(auto& [key,value]:result.intro_source)pages["ja"].try_emplace(key,std::move(value));
    }
    localization::replace(std::move(next));
    {std::lock_guard lock(page_mutex);page_texts=std::move(pages);}
    json listed=json::array();
    std::string report="SRW64 dialogue text report\n";
    for(const auto& [locale,row]:summary.items())
        report+=locale+": "+std::to_string(row.at("entries").get<size_t>())+" entries from "+std::to_string(row.at("files").get<unsigned>())+" files, "+
            std::to_string(row.at("problems").get<size_t>())+" problems\n";
    for(const auto& problem:problems) {
        listed.push_back({{"path",problem.path},{"line",problem.line},{"key",problem.key},{"message",problem.message}});
        report+=problem.path+":"+std::to_string(problem.line)+"  "+(problem.key.empty()?"-":problem.key)+"  "+problem.message+"\n";
    }
    if(!text_overrides.empty()) {
        std::error_code error;std::filesystem::create_directories(text_overrides,error);
        if(!error)std::ofstream(text_overrides/"dialogue-report.txt")<<report;
    }
    record("dialogue_text",{{"bundled",text_bundled.string()},{"overrides",text_overrides.string()},{"locales",summary},
        {"problems",listed.size()>20?json(std::vector<json>(listed.begin(),listed.begin()+20)):listed}});
    return {entries,problems.size()};
}
// The banner counts the reading language's entries; problems in any language.
void announce_text(const std::map<std::string,size_t>& entries,size_t problems) {
    const auto& catalog=localization::catalog();
    const auto found=entries.find(catalog.locale);
    notices::post("dialogue-text",filled(filled(catalog.ui("dialogue_text_status"),"{n}",std::to_string(found==entries.end()?0:found->second)),
        "{k}",std::to_string(problems)));
}
// F5: read the files again, rebuild every record the history holds, and resume
// the current record at the original's page, as a language switch does.
void reload_text(uint8_t* ram) {
    try {
        const auto [entries,problems]=load_text();
        for(auto& entry:reader.history)if(!entry.notice)
            for(const auto& [locale,catalog]:localization::registered())
                if(catalog->resolve(game_adapter::standard_dialogue_key(entry.text_id))) {
                    auto localized=story_record(ram,*catalog,entry.text_id,body_base);
                    entry.localized[locale]=std::move(localized.text);entry.localized_stops[locale]=std::move(localized.stops);
                }
        language_status.locale=localization::snapshot()->locale;language_status.error.clear();++language_status.request;
        announce_text(entries,problems);
    } catch(const std::exception& error) {
        record("dialogue_text_error",{{"error",error.what()}});
        notices::post("dialogue-text",error.what());
    }
}
void service_locale(uint8_t* ram) {
    std::lock_guard lock(mutex);
    if(reload_requested.exchange(false))reload_text(ram);
    if(language_status.request==language_status.completed)return;
    refresh(ram,true);
    if(language_status.request!=language_status.completed) {
        const auto previous_catalog=localization::snapshot();
        const auto previous_reader=reader;
        const auto previous_boxes=current;
        const auto previous_skip_owner=skip_owner;
        try {
            auto target=localization::find(language_status.locale);
            if(!target)throw std::runtime_error("Requested locale is unavailable");
            if(target!=localization::snapshot()) {
                Reader next=reader;
                localization::Scope scope(target);
                std::u16string body;std::vector<size_t> stops;
                for(const auto& entry:reader.history)if(entry.event==reader.event) {
                    const auto found=entry.localized.find(target->locale);
                    if(found!=entry.localized.end())body=found->second;
                    if(const auto at=entry.localized_stops.find(target->locale);at!=entry.localized_stops.end())stops=at->second;
                }
                if(reader.active && body.empty())throw std::runtime_error("Current record has no language snapshot");
                // The new layout starts a page where the original page the game shows begins.
                const size_t anchor=reader.guest && reader.guest<=stops.size()?stops[reader.guest-1]:0;
                next.switch_language(typeset_body(body,body_size(reader.font_size),stops,{anchor}),stops,target->locale,srw64_current_vi());
                next.previous=raw_buttons.load();
                // Preflight the inactive box too before publishing anything.
                for(const auto& box:current)if(box.visible) {
                    const auto* text=target->resolve(game_adapter::standard_dialogue_key(box.text_id));
                    if(!text)continue;
                    if(display_only)typeset_body(utf16(segment(expand(ram,*text,target->locale),box.segment)),body_size(reader.font_size));
                    else {
                        const auto source=story_record(ram,*target,box.text_id,body_base+box.slot*stride);
                        typeset_body(source.text,body_size(reader.font_size),source.stops);
                    }
                }
                state_probe::capture(ram,"locale-before",uint32_t(language_status.request));
                reader=std::move(next);current={};skip_owner=0;
                localization::activate(target);refresh(ram,false);
                state_probe::capture(ram,"locale-after",uint32_t(language_status.request));
                record("locale_changed",{{"locale",target->locale},{"request",language_status.request},
                    {"event",reader.event},{"history_entries",reader.history.size()}});
            }
        } catch(const std::exception& error) {
            localization::activate(previous_catalog);reader=previous_reader;current=previous_boxes;skip_owner=previous_skip_owner;
            language_status.error=error.what();
        }
        language_status.completed=language_status.request;
    }
}
// The reader takes this frame's keys. The log gets each press and release of
// fast-forward and each page the reader turns, with what was held.
void read_keys() {
    const bool was_fast=reader.fast,was_pending=reader.pending;
    const uint64_t was_event=reader.event;const size_t was_page=reader.page;
    reader.update(raw_buttons.load(),srw64_current_vi());
    const json state={{"event",reader.event},{"page",reader.page},{"pending",reader.pending},{"fast",reader.fast},
        {"automatic",reader.auto_read},{"skip",reader.skipping}};
    if(was_fast!=reader.fast)record("fast",state);
    if(reader.event && reader.event==was_event && (reader.page!=was_page || reader.pending!=was_pending))record("turn",state);
}
bool step(uint8_t* ram,recomp_context* ctx) {
    std::lock_guard lock(mutex);
    service_locale(ram);refresh(ram,true);
    if(observe)return false;
    if(settings::owns_input() && reader.active) {
        const auto now=srw64_current_vi();reader.page_started+=now-reader.tick;reader.tick=now;
        auto_toggle=false;return true;
    }
    if(!reader.active) {
        auto_toggle=false;read_keys();return false;
    }
    const auto old_font=reader.font_size,old_speed=reader.speed;
    const bool was_history=reader.history_open,was_skip=reader.skipping;
    if(auto_toggle.exchange(false)) {
        reader.toggle_auto(srw64_current_vi());
        record("auto",{{"automatic",reader.auto_read},{"level",reader.speed}});
    }
    read_keys();
    if(reader.skipping && !was_skip) {
        if(reading_owner) {skip_owner=reading_owner;script_skip::start(skip_owner);record("skip_start",{{"owner",skip_owner}});}
        else {reader.skipping=false;record("skip_unavailable");}
    }
    // B or the history ends the skip where it is (Reader::update).
    if(was_skip && !reader.skipping && script_skip::active()){script_skip::end();skip_owner=0;record("skip_cancelled");}
    if(old_font!=reader.font_size) {
        // The current page keeps its first character; only what follows moves.
        reader.relayout(typeset_body(reader.layout.text,body_size(reader.font_size),reader.stops,{reader.page_start()}));
        record("font",{{"size",reader.font_size},{"pages",reader.layout.pages.size()}});
    }
    if(old_speed!=reader.speed)record("speed",{{"level",reader.speed},{"automatic",reader.auto_read}});
    if(was_history!=reader.history_open)record("history",{{"open",reader.history_open},{"entries",reader.history.size()}});
    if(reader.history_open && !was_history) {
        // As the panel lays them out (dialogue_scene.cpp): a name line for each record
        // but a notice, its text, a gap.
        size_t lines=0;
        for(const auto& entry:reader.history)if(!entry.text.empty()) {
            const auto layout=typeset(entry.text,10,270,10000);
            lines+=entry.notice?1:2;
            for(const auto& page:layout.pages)lines+=page.lines.size();
        }
        reader.history_scroll_limit=lines>Reader::history_shown?lines-Reader::history_shown:0;
    }
    // The exact original routine owns STOP increments and completion status.
    // Only its local A trigger is substituted, then restored to avoid leakage:
    // once for each original page the reader reaches, once more for <END>.
    const auto confirm=reader.confirmation(srw64_current_vi());
    const bool advance=confirm!=Confirm::none;
    const auto saved=half(ram,0x15CAF0);
    const uint16_t mask=advance ? uint16_t(saved|Reader::A):uint16_t(saved&~Reader::A);
    std::memcpy(ram+(0x15CAF0^2),&mask,2);
    if(!reader.history_open) {
        // Timed text headers (modes 2/3) also wait for native reading pages.
        std::array<uint16_t,2> timers{};
        for(unsigned i=0;i<2;++i) {
            const auto p=body_base+i*stride;
            timers[i]=half(ram,p+0x210);
            if(current[i].active && byte(ram,p+0x20E)>=2) {
                uint16_t value=advance?0:std::max<uint16_t>(timers[i],2);std::memcpy(ram+((p+0x210)^2),&value,2);
            }
        }
        srw64_original_dialogue_step(ram,ctx);
        for(unsigned i=0;i<2;++i) {
            const auto p=body_base+i*stride;
            if(current[i].active && byte(ram,p+0x20E)>=2 && !advance)
                std::memcpy(ram+((p+0x210)^2),&timers[i],2);
        }
    }
    std::memcpy(ram+(0x15CAF0^2),&saved,2);
    if(confirm==Confirm::stop)record("guest_stop",{{"event",reader.event},{"segment",reader.guest},{"page",reader.page}});
    if(confirm==Confirm::end)record("guest_confirm",{{"event",reader.event},{"page",reader.page},{"skip",reader.skipping},
        {"stops",reader.stops.size()}});
    refresh(ram,true);
    return true;
}
void loaded(uint8_t*,unsigned slot) {
    std::lock_guard lock(mutex);
    if(slot<2) {
        if(reader.skipping && executing_owner && executing_owner!=skip_owner)cancel("script_owner_changed");
        loads[slot]=++generation;if(executing_owner)reading_owner=executing_owner;
    }
}
void drawn(uint8_t* ram,uint32_t begin,uint32_t end) {
    std::lock_guard lock(mutex);
    if(!scene_supported || end<begin || end>0x800000)return;
    refresh(ram,false);
    auto frame=std::make_shared<Frame>();
    frame->vi=srw64_current_vi();frame->boxes=current;frame->font_size=reader.font_size;
    frame->catalog=localization::snapshot();
    frame->reading_event=reader.event;frame->advance=reader.advance_progress();
    frame->speed=reader.speed;frame->auto_read=reader.auto_read;frame->fast=reader.fast;
    frame->history_open=reader.history_open;frame->skipping=reader.skipping;frame->display_only=display_only;
    frame->pad_hints=input::pad_hints.load();frame->pad_family=input::pad_family.load();
    frame->bar_scale=settings::ui_scale(settings::ui_size());
    {
        // The hints name what the player bound (input_bindings.hpp); copied again only when that changes.
        static input::Bindings bindings=input::live_bindings().get();
        static uint64_t revision=input::live_bindings().revision();
        if(const auto now=input::live_bindings().revision();now!=revision){bindings=input::live_bindings().get();revision=now;}
        const text::PromptContext context{frame->pad_hints,text::PadFamily(frame->pad_family),&bindings,settings::key_display_name};
        frame->controls_text=text::expand_prompts(localization::catalog().ui(frame->pad_hints?"controls_pad":"controls"),context);
        frame->history_controls_text=text::expand_prompts(localization::catalog().ui(frame->pad_hints?"history_controls_pad":"history_controls"),context);
    }
    frame->history=reader.history;frame->history_offset=reader.history_offset;
    drawings.publish(begin,end,frame);
}
}

nlohmann::json state() {
    std::lock_guard lock(mutex);
    if(!enabled)return nullptr;
    return state_snapshot();
}
void configure(const std::filesystem::path& directory) {
    const char* path=std::getenv("SRW64_DIALOGUE_DATA");if(!path)return;
    std::ifstream input(path);json data;input>>data;
    localization::initialize(data);
    base_catalogs=localization::registered();
    for(const auto& [locale,catalog]:localization::registered()) {
        localization::Scope scope(catalog);typeset(u"Aa 日本語 简体中文",13);
    }
    reader.font_size=data.at("config").at("font_size").get<unsigned>();
    if(reader.font_size<10 || reader.font_size>18)throw std::runtime_error("Native font size outside 10..18");
    for(const auto& [key,value]:data.at("glyphs").items())glyphs.emplace(std::stoul(key),value.get<std::string>());
    enabled=true;observe=data.at("config").value("mode","replace")=="observe";
    output=directory;log.open(directory/"dialogue-events.jsonl");
    {
        // The protagonists' and partners' defaults in every reading language.
        std::vector<std::string> locales;
        for(const auto& [locale,catalog]:base_catalogs)locales.push_back(locale);
        const auto unsplit=names::add_people(names::default_names(),locales,[](const std::string& locale,unsigned id) {
            const auto catalog=localization::find(locale);
            const auto* text=catalog?catalog->resolve(localization::TextKey::base(0,uint16_t(id))):nullptr;
            return text?*text:std::string();
        });
        for(const auto& locale:unsplit)record("default_names_unsplit",{{"locale",locale}});
    }
    if(const char* value=std::getenv("SRW64_DIALOGUE_TEXT"))text_bundled=value;
    if(const char* value=std::getenv("SRW64_DIALOGUE_OVERRIDES"))text_overrides=value;
    {
        const auto [entries,problems]=load_text();
        localization::activate(localization::find(localization::snapshot()->locale));
        if(problems)announce_text(entries,problems);
    }
    srw64_game_hooks.presentation_step=service_locale;
    srw64_game_hooks.dialogue_step=step;srw64_game_hooks.text_loaded=loaded;srw64_game_hooks.text_drawn=drawn;
    srw64_game_hooks.reset=[](uint8_t*) {std::lock_guard lock(mutex);cancel("dialogue_reset",true);};
    srw64_game_hooks.script_before=[](uint8_t* ram,uint32_t owner) {
        std::lock_guard lock(mutex);executing_owner=owner;
        if(reader.skipping && skip_owner==owner && half(ram,(owner&0x1FFFFFFF)+0x24)==0x80)cancel("script_ended");
        const bool pause=!observe && reader.history_open && reading_owner==owner;
        if(pause)executing_owner=0;
        return pause;
    };
    srw64_game_hooks.script_after=[](uint8_t* ram,uint32_t owner) {
        std::lock_guard lock(mutex);executing_owner=0;
        if(const char* stop=script_skip::take_stop())cancel(stop);
        if(reading_owner==owner && (!word(ram,(owner&0x1FFFFFFF)+0x1C) || half(ram,(owner&0x1FFFFFFF)+0x24)==0x80))
            cancel("script_ended");
    };
    srw64_game_hooks.choice=[](uint8_t*) {std::lock_guard lock(mutex);cancel("choice");};
    // A page the short skip read without showing goes to the history in every language:
    // the name is the record the original's name panel shows, 0x111E + the header's
    // speaker digits (8008F648), which also covers the protagonists' fields.
    script_skip::state().skipped=[](const uint8_t* ram,uint16_t text_id,uint16_t speaker) {
        std::lock_guard lock(mutex);
        Entry entry;entry.event=++event_serial;entry.text_id=text_id;
        for(const auto& [locale,catalog]:localization::registered()) {
            entry.localized[locale]=story_record(ram,*catalog,text_id,body_base).text;
            entry.localized_speaker[locale]=utf16(record_text(ram,*catalog,uint16_t(0x111E + speaker)));
        }
        const auto& locale=localization::catalog().locale;
        entry.text=entry.localized[locale];entry.speaker=entry.localized_speaker[locale];
        if(entry.text.empty())return;
        record("skipped",{{"text_id",text_id},{"speaker",speaker},{"name",utf8(entry.speaker)}});
        reader.skipped(std::move(entry));
    };
    srw64_game_hooks.refund=[](uint8_t* ram,uint16_t unit,uint32_t amount) {refund_notice(ram,unit,amount);};
    record("configured",{{"mode",observe?"observe":"replace"},{"font_size",reader.font_size},
        {"locale",localization::catalog().locale},{"catalog",localization::catalog().revision}});
}
uint64_t request_locale(const std::string& locale) {
    std::lock_guard lock(mutex);
    if(!localization::find(locale))throw std::runtime_error("Requested locale is unavailable");
    language_status.locale=locale;language_status.error.clear();return ++language_status.request;
}
LocaleStatus locale_status(){std::lock_guard lock(mutex);return language_status;}
void request_reload(){if(enabled)reload_requested=true;}
uint16_t input(uint16_t buttons) {
    // The controller's triggers are the reader's alone (docs/design/steam-deck-controls.md):
    // R2 held reads as R + A, fast-forward, the same chord as E + Z (Reader::update: a press
    // turns one page, held it runs, let go reading is manual); with Menu as R + START, the
    // segment skip, which R2 letting go does not stop. An L2 press toggles automatic
    // reading. The game never sees either trigger. Keys bound to them (input_bindings.hpp)
    // count the same, so this reads the keyboard state, which holds the controller's too.
    const uint32_t pad=srw64_keyboard_state();
    static bool l2_before=false;
    if((pad&srw64::input::pad_l2) && !l2_before)auto_toggle=true;
    l2_before=pad&srw64::input::pad_l2;
    // The stick counts as the D-pad, as the original folds it in (original-controls.md), so
    // the auto speed and the history scroll take it too.
    uint16_t stick=0;
    if(pad&(1u<<16))stick|=Reader::UP;
    if(pad&(1u<<17))stick|=Reader::DOWN;
    if(pad&(1u<<18))stick|=Reader::LEFT;
    if(pad&(1u<<19))stick|=Reader::RIGHT;
    raw_buttons=uint16_t(buttons|stick|((pad&srw64::input::pad_r2)?(Reader::R|Reader::A):0));
    consumed_hold &= buttons;
    if(owns_input)consumed_hold |= buttons & (Reader::A|Reader::B|Reader::START|Reader::UP|Reader::DOWN|Reader::L|Reader::R|Reader::BIGGER|Reader::SMALLER);
    return buttons & ~consumed_hold;
}
void overlay_loaded(uint32_t rom) {
    if(!enabled)return;
    std::lock_guard lock(mutex);
    // World map and tactical map script dialogue; battle quotes (0x121560) use the
    // same boxes but are only redrawn, never read or paced by the native reader.
    overlay=rom;scene_supported=rom==0xA7EC0 || rom==0xAB160 || rom==0x121560;display_only=rom==0x121560;
    cancel("overlay_changed");drawings.clear();identities={};loads={};events={};
}
std::shared_ptr<const Frame> take_frame(uint32_t start,uint32_t size,std::vector<uint8_t>& display,const uint8_t* ram) {
    if(!enabled)return {};
    std::lock_guard lock(mutex);
    if(start>0x800000 || size>0x800000-start)return {};
    std::shared_ptr<Frame> frame;
    if(auto drawing=drawings.take(start,size)) {
        frame=std::make_shared<Frame>(*drawing->frame);
        if(observe)return {};
        if(display.size()<start+size)display.resize(start+size);
        std::memcpy(display.data()+start,ram+start,size);
        bool font=false;std::array<unsigned,2> removed{};
        for(uint32_t p=drawing->begin;p+24<=drawing->end;p+=8) {
            const auto a=word(ram,p),b=word(ram,p+4);
            if((a>>24)==0xFD) {
                const auto texture=b&0x1FFFFFFF;
                font=texture>=8 && texture<0x800000 && game_adapter::dialogue_font_image(
                    a,word(ram,texture-8),word(ram,texture-4));
            }
            if((a>>24)!=0xE4 || !font)continue;
            const double x=((b>>12)&4095)/4.0,y=(b&4095)/4.0;
            for(auto& box:frame->boxes) {
                if(!box.visible || x<box.x-4 || x>box.x+177 || y<box.y-19 || y>box.y+32)continue;
                if((word(ram,p+8)>>24)!=0xE1 || (word(ram,p+16)>>24)!=0xF1)
                    throw std::runtime_error("Native dialogue glyph command shape differs");
                for(unsigned offset=0;offset<24;offset+=8) {
                    uint32_t nop=0xE0000000,zero=0;std::memcpy(display.data()+p+offset,&nop,4);
                    std::memcpy(display.data()+p+offset+4,&zero,4);
                }
                ++removed[box.slot];break;
            }
        }
        for(auto& box:frame->boxes)box.visible &= removed[box.slot]!=0;
    }
    return observe?nullptr:frame;
}
void queue_frame(uint64_t workload,std::shared_ptr<const Frame> frame) {
    if(!enabled)return;
    std::lock_guard lock(mutex);frames[workload]=std::move(frame);
    while(frames.size()>64)frames.erase(frames.begin());
}
std::shared_ptr<const Frame> presented_frame(uint64_t workload) {
    std::lock_guard lock(mutex);auto it=frames.find(workload);return it==frames.end()?nullptr:it->second;
}
}

namespace srw64::dialogue {
std::string label_text(const uint8_t* ram,uint32_t label) {
    if(!enabled)return {};
    // As speaker_name, but a label may run past its 20 glyphs into the next slot:
    // the original fetcher does not check the length.
    label&=0x1FFFFFFF;
    const auto shown=decode(ram,label+12,64);
    const uint16_t id=half(ram,label);
    if(const auto* slot=entered_name(id))return shown_name(shown,*slot,localization::catalog().locale);
    const auto japanese=localization::find("ja");
    if(!id || !japanese || record_text(ram,*japanese,id)!=shown)return shown;
    return record_text(ram,localization::catalog(),id);
}
std::string glyph_text(const uint8_t* ram,uint32_t address,size_t limit) {
    return enabled?decode(ram,address&0x1FFFFFFF,limit):std::string();
}
std::string glyph_string(uint16_t code){return enabled?glyph(code):std::string();}
std::string body_page(const uint8_t* ram,uint32_t body) {
    if(!enabled)return {};
    body&=0x1FFFFFFF;
    const unsigned page=byte(ram,body+0x214) && byte(ram,body+0x215)?byte(ram,body+0x215):0;
    const auto shown=segment(decode(ram,body+12,256),page);
    const uint16_t id=half(ram,body);
    const auto japanese=localization::find("ja");
    if(!id || !japanese || segment(record_text(ram,*japanese,id),page)!=shown)return shown;
    const auto translated=segment(record_text(ram,localization::catalog(),id),page);
    return translated.empty()?shown:translated;
}
bool reader_owns(unsigned label_slot,double x,double y) {
    std::lock_guard lock(mutex);
    for(const auto& box:current) {
        if(!box.visible)continue;
        if(label_slot==box.slot)return true;
        // take_frame's box area: speaker row above, text area below.
        if(x>=box.x-4 && x<=box.x+177 && y>=box.y-19 && y<=box.y+32)return true;
    }
    return false;
}
bool reader_configured(){return enabled;}
std::string page_text(const std::string& locale,unsigned resource) {
    std::lock_guard lock(page_mutex);
    const auto language=page_texts.find(locale);
    if(language==page_texts.end())return {};
    const auto found=language->second.find("intro:"+std::to_string(resource));
    return found==language->second.end()?std::string():found->second;
}
std::string ui_text(const uint8_t* ram,uint16_t id) {
    // Chapter titles are record 281 + scene everywhere (title card, intermission,
    // save list); a campaign's stage names the scene it borrows.
    if(id>=281 && id<281+256)
        if(auto title=campaign::title(id-281u,localization::snapshot()->locale))return *title;
    auto text=record_text(ram,localization::catalog(),id);
    return text.empty() && !localization::catalog().resolve(localization::TextKey::base(0,id))?std::to_string(id):text;
}
}

namespace srw64::campaign {
std::string notice(const std::string& key,uint32_t scene) {
    return dialogue::filled(localization::catalog().ui(key),"{n}",std::to_string(scene));
}
}
