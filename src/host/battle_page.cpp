#include "battle_page.hpp"
#include "battle_effects.hpp"
#include "native_dialogue.hpp"
#include "localization/catalog.hpp"
#include "presentation_settings.hpp"
#include "native_map.hpp"
#include "game_hooks.hpp"
#include "funcs.h"
#include <atomic>
#include <mutex>

uint32_t srw64_keyboard_state();  // graphics.cpp: keyboard and controller, host bits included

namespace srw64::battle_page {
namespace {
using namespace guest;
using json=nlohmann::json;
std::mutex mutex;
json current={{"visible",false}};
uint64_t serial{};
std::string pending;
std::atomic_bool owning{},window_owning{};
uint16_t held{};
std::ofstream log;
std::vector<uint8_t> scratch;
unsigned generation_rules{};
bool spirit_running{};
bool spirit_menu{};
// Original HUD in use (the setting): the step is called every frame it shows.
// The frontend keeps a small animation-toggle label up while this is recent.
std::atomic<uint64_t> original_vi{};
std::atomic_bool original_animation{};
std::atomic<unsigned> original_mode{};
// Every frame the original confirmation is on screen, whichever page replaces it.
std::atomic<uint64_t> screen_vi{};
bool keep_original_frame(uint16_t layout) {
    if((layout!=1196 && layout!=1197) || settings::battle_ui()!=settings::BattleUi::Original)return false;
    screen_vi=srw64_current_vi();   // the frame may be drawn before the first step
    return true;
}
struct SavedByte {uint32_t address;uint8_t value;};
std::vector<SavedByte> spirit_return_state;
void save_range(const uint8_t* ram,uint32_t address,unsigned length) {
    for(unsigned i=0;i<length;++i)spirit_return_state.push_back({address+i,uint8_t(read(ram,address+i,1))});
}
int invoke(uint8_t* ram,recomp_context* ctx,uint32_t function,uint32_t a0=0,uint32_t a1=0,uint32_t a2=0) {
    auto call=*ctx;call.r29=rule_probe::pointer(uint32_t(ctx->r29)-0x200);
    call.r4=rule_probe::pointer(a0);call.r5=rule_probe::pointer(a1);call.r6=rule_probe::pointer(a2);
    LOOKUP_FUNC(function)(ram,&call);return int32_t(call.r2);
}
// The original cast animation/effect owns HP, morale, spirit flags and SP.
// Only its normal map-return is redirected to the suspended encounter.
bool return_from_spirit(uint8_t* ram,recomp_context* ctx) {
    if(!spirit_running)return false;
    for(const auto& b:spirit_return_state)write8(ram,b.address,b.value);
    spirit_return_state.clear();spirit_running=false;
    invoke(ram,ctx,0x8009DB8C);invoke(ram,ctx,0x801C2FD8);
    // The original settles the whole exchange, rolls included, before this page: as the
    // target is chosen when attacking (801D3140), as the enemy picks its target when
    // defending (801C90F4), again only when the response or counter weapon changes. Settle
    // it again so the cast spirit counts, as those steps do; the same as backing out and
    // choosing the target again, or changing the response and back.
    unsigned mode;{std::lock_guard lock(mutex);mode=current.value("mode",0u);}
    const auto a=combat_preview::participant(ram,0),d=combat_preview::participant(ram,1);
    if(mode==1) {
        invoke(ram,ctx,0x801F7B54);write8(ram,0x8022796C,0);
        invoke(ram,ctx,0x801F7D6C,a.handle,d.handle,0);
    }
    // The response (8018B754) and counter weapon (8018B74C) stay as chosen (801D5294, 801D5404).
    else invoke(ram,ctx,0x801F7D6C,a.handle,d.handle,1);
    invoke(ram,ctx,0x801F3578);
    std::lock_guard lock(mutex);current["visible"]=false;current["spirit_running"]=false;
    spirit_menu=true;return true;
}

json art={{"units",json::object()},{"portraits",json::object()}};
json active_spirits(const uint8_t* ram,uint32_t flags) {
    json result=json::array();
    for(unsigned id=0;id<30;++id)if(flags&(1u<<id))
        result.push_back({{"id",id},{"name",dialogue::ui_text(ram,969+id)}});
    return result;
}

json spirit_options(uint8_t* copy,recomp_context* ctx,const rule_probe::Combatant& c) {
    json result=json::array();
    const unsigned crew_count=std::min(7u,read(copy,c.unit+0x34,1));
    for(unsigned crew=0;crew<crew_count;++crew) {
        const uint32_t pilot=read(copy,c.unit+0x38+crew*4,4);
        if(!valid(pilot,0x4C))continue;
        for(unsigned slot=0;slot<std::min(6u,read(copy,pilot+0xA,1));++slot) {
            const unsigned id=read(copy,pilot+0xB+slot,1);
            if(id>=30)continue;
            const unsigned cost=read(copy,0x80217F70+id,1),sp=read(copy,pilot+0x16,2);
            // Targeted/map-only commands keep their normal tactical-map entry.
            const bool battle_usable=id!=0 && id!=29 && read(copy,0x80217D20+id,1)==15;
            const bool usable=battle_usable && invoke(copy,ctx,0x801F18A0,c.handle,id)!=0;
            result.push_back({{"crew",crew},{"slot",slot},{"id",id},{"name",dialogue::ui_text(copy,969+id)},
                {"pilot_name",dialogue::ui_text(copy,4382+read(copy,pilot+2,2))},{"sp",sp},{"max_sp",read(copy,pilot+0x18,2)},
                {"cost",cost},{"enabled",battle_usable && usable && sp>=cost},
                {"reason",!battle_usable?"map":sp<cost?"sp":!usable?"unavailable":"ready"}});
        }
    }
    return result;
}
// The original screen's own words (layouts 0x45/0x46), for the HD original page.
json screen_words(const uint8_t* ram) {
    json words;
    for(const auto& [key,id]:std::initializer_list<std::pair<const char*,uint16_t>>{{"level",898},{"morale",908},{"hit",1014},
        {"start",1015},{"weapon",1016},{"evade",927},{"defend",1017},{"defending",890},{"no_counter",1109}})
        words[key]=dialogue::ui_text(ram,id);
    return words;
}
json spirit_grid(const uint8_t* ram,const rule_probe::Combatant& c) {
    json result=json::array();const auto flags=read(ram,c.pilot+0x1C,4);
    for(unsigned id=0;id<30;++id) {
        bool learned=false;
        for(unsigned crew=0;crew<std::min(7u,read(ram,c.unit+0x34,1));++crew) {
            const auto pilot=read(ram,c.unit+0x38+4*crew,4);if(!valid(pilot,0x4C))continue;
            for(unsigned n=0;n<std::min(6u,read(ram,pilot+0xA,1));++n)learned|=read(ram,pilot+0xB+n,1)==id;
        }
        if(learned || (flags&(1u<<id)))result.push_back({{"id",id},{"name",dialogue::ui_text(ram,969+id)},{"active",bool(flags&(1u<<id))}});
    }
    return result;
}

void record(const char* kind) {
    if(log){auto row=current;row["kind"]=kind;row["vi"]=srw64_current_vi();log<<row.dump()<<'\n';log.flush();}
}
json combatant(uint8_t* copy,recomp_context* ctx,const rule_probe::Combatant& a,const rule_probe::Combatant& d,unsigned slot) {
    const auto estimate=combat_preview::estimate(copy,ctx,a,d);
    const auto flags=read(copy,a.pilot+0x1C,4);
    return {{"unit_name",dialogue::ui_text(copy,527+read(copy,a.unit+2,2))},{"pilot_name",dialogue::ui_text(copy,4382+read(copy,a.pilot+2,2))},{"weapon_name",a.weapon?dialogue::ui_text(copy,1370+read(copy,a.weapon+2,2)):std::string{}},{"side",a.side},{"handle",a.handle},{"unit",read(copy,a.unit+2,2)},{"pilot",read(copy,a.pilot+2,2)},
        {"weapon",a.weapon?int(read(copy,a.weapon+2,2)):-1},{"level",read(copy,a.pilot+5,1)},
        {"sp",read(copy,a.pilot+0x16,2)},{"max_sp",read(copy,a.pilot+0x18,2)},{"spirit_grid",spirit_grid(copy,a)},{"hp",read(copy,a.unit+4,2)},{"max_hp",read(copy,a.unit+6,2)},{"en",read(copy,a.unit+8,2)},{"max_en",read(copy,a.unit+10,2)},
        {"unit_art",art["units"].value(std::to_string(read(copy,a.unit+2,2)),json::object())},
        {"size",[&]{const unsigned f=read(copy,a.unit+0x0C,1);return f&1?0:f&2?1:f&4?2:f&8?3:4;}()},  // SS..LL, as ability_page
        // What the original screen tells (801E7A28): an enemy's HP/EN and level only once known.
        {"known",[&]{const uint32_t lead=read(copy,a.unit+0x38,4);return a.side==0 || (valid(lead,0x4C) && (read(copy,lead,1)&0x40)!=0);}()},
        {"level_known",a.side==0 || (read(copy,a.pilot,1)&0x40)!=0},
        {"portrait",art["portraits"].value(std::to_string(read(copy,a.pilot+2,2)),json::object())},
        {"active_spirits",active_spirits(copy,flags)},{"pilot_effects",combat_preview::pilot_effects(copy,a)},
        {"morale",read(copy,a.pilot+0x20,2)},{"spirits",flags},{"abilities",read(copy,a.unit+0x28,4)},
        {"hit",std::clamp(int(int16_t(read(copy,rules::participants+slot*rules::participant_size+0x12,2))),0,100)},
        {"estimated_hit",estimate.hit},{"damage",estimate.damage},{"critical",estimate.critical},
        {"hit_modifier",estimate.hit_modifier},{"critical_modifier",estimate.critical_modifier},
        {"ammo",a.weapon?int(int8_t(read(copy,a.weapon+0xB,1))):-1},{"en_cost",read(copy,a.weapon+0xD,1)},
        {"morale_required",read(copy,a.weapon+0xE,1)}};
}
bool step(uint8_t* ram,recomp_context* ctx,unsigned mode) {
    screen_vi=srw64_current_vi();
    const auto a=combat_preview::participant(ram,0),d=combat_preview::participant(ram,1);
    // Forced/scripted battles and AI-vs-AI exchanges retain their original flow.
    if((a.side!=0 && d.side!=0) || read(ram,0x802279E8,1)!=0 || !valid(a.unit,0x54) || !valid(d.unit,0x54))return false;
    const auto language=localization::snapshot();localization::Scope language_scope(language);
    std::unique_lock lock(mutex);
    const auto style=settings::battle_ui();
    if(!current.value("visible",false) && style==settings::BattleUi::Original) {
        // Original confirmation, plus one addition: C-down, or the animation button
        // (input_bindings.hpp, Y on the Deck), toggles the battle animation (8015DDA8 & 4
        // set = off). Its A/B/menu handling is untouched.
        static bool animation_before=false;
        const bool animation_now=srw64_keyboard_state()&input::pad_animation,animation_pressed=animation_now && !animation_before;
        animation_before=animation_now;
        if((read(ram,0x80178A08,2)&0x0004) || animation_pressed)write8(ram,0x8015DDA8,read(ram,0x8015DDA8,1)^4);
        original_animation=(read(ram,0x8015DDA8,1)&4)==0;original_mode=mode;original_vi=srw64_current_vi();
        return false;
    }
    if(!current.value("visible",false) || current.value("mode",0u)!=mode) {
        scratch.assign(ram,ram+0x800000);auto call=*ctx;
        generation_rules=rules::active_fixes();
        auto left=combatant(scratch.data(),&call,a,d,0),right=combatant(scratch.data(),&call,d,a,1);
        left.update(combat_preview::damage_preview(scratch.data(),&call,a,d,0));
        right.update(combat_preview::damage_preview(scratch.data(),&call,d,a,1));
        left["defense"]=combat_preview::defense_preview(scratch.data(),a,d,right);
        right["defense"]=combat_preview::defense_preview(scratch.data(),d,a,left);
        left["defensive_effects"]=combat_preview::defensive_effects(scratch.data(),a,left["defense"],right);
        right["defensive_effects"]=combat_preview::defensive_effects(scratch.data(),d,right["defense"],left);
        left["target_shield"]=right["defense"]["shield"];
        right["target_shield"]=left["defense"]["shield"];
        current={{"visible",true},{"locale",language->locale},{"serial",++serial},{"mode",mode},{"style",settings::battle_ui_name(style)},{"attacker",left},{"defender",right},
            {"response",int8_t(read(ram,0x8018B754,1))},{"animation",(read(ram,0x8015DDA8,1)&4)==0},
            {"can_cancel",mode==1 && read(ram,0x8010F5E8,1)==1},{"turn",read(ram,0x8010F5EA,2)},
            {"can_counter",mode==2},{"rules",generation_rules},{"spirit_menu",spirit_menu},
            {"spirit_options",spirit_options(scratch.data(),&call,a.side==0?a:d)},{"words",screen_words(scratch.data())}};
        // The native cards replace the original HUD sprites/text. This original
        // cleanup removes slots 0x27..0x34, preserving battlefield unit sprites.
        auto cleanup=*ctx;resident_func_8009DB8C(ram,&cleanup);
        owning=true;pending.clear();record("open");
    }
    else if(owning && current.value("visible",false)) {
        // The original screen rebuilds its HUD sprites on some frames (seen as the
        // original cards showing through the native page); clear them every frame.
        auto cleanup=*ctx;resident_func_8009DB8C(ram,&cleanup);
    }
    // A language change only relabels the existing combat snapshot. Opening
    // settings must not mix newly selected rules into an already prepared battle.
    if(current.value("locale",std::string{})!=language->locale) {
        for(const auto& pair:{std::pair{"attacker",a},std::pair{"defender",d}}) {
            auto& card=current[pair.first];const auto& c=pair.second;
            card["spirit_grid"]=spirit_grid(ram,c);
            card["active_spirits"]=active_spirits(ram,card.at("spirits").get<uint32_t>());
            card["unit_name"]=dialogue::ui_text(ram,527+read(ram,c.unit+2,2));
            card["pilot_name"]=dialogue::ui_text(ram,4382+read(ram,c.pilot+2,2));
            card["weapon_name"]=c.weapon?dialogue::ui_text(ram,1370+read(ram,c.weapon+2,2)):std::string{};
        }
        scratch.assign(ram,ram+0x800000);auto call=*ctx;
        current["spirit_options"]=spirit_options(scratch.data(),&call,a.side==0?a:d);
        current["words"]=screen_words(ram);
        current["locale"]=language->locale;
    }
    if(pending.empty())return true;
    auto action=std::exchange(pending,{});
    // Evade/defend return to countering through the original weapon list, which
    // keeps its range, EN and ammo validation.
    if(action=="counter") {
        if(mode!=2 || current.value("response",0)==0)return true;
        action="weapon";
    }
    if(action=="spirits" || action=="spirit-back" || (action=="back" && spirit_menu)) {
        spirit_menu=action=="spirits";current["spirit_menu"]=spirit_menu;return true;
    }
    if(action.starts_with("cast:")) {
        if(!spirit_menu)return true;
        const auto player=a.side==0?a:d;
        scratch.assign(ram,ram+0x800000);auto call=*ctx;
        for(const auto& option:spirit_options(scratch.data(),&call,player)) {
            const auto key="cast:"+std::to_string(option.at("crew").get<unsigned>())+":"+std::to_string(option.at("slot").get<unsigned>());
            if(action!=key || !option.at("enabled").get<bool>())continue;
            const unsigned id=option.at("id"),crew=option.at("crew");
            spirit_return_state.clear();
            // UI state, cursor/selection, cast-menu context and participant cache;
            // never roll back pilot/unit state, phase counters, inventory or RNG.
            save_range(ram,0x80172EB0,4);save_range(ram,0x80172ECC,0x1C);
            save_range(ram,0x80227A81,0x27);save_range(ram,0x80227BCC,1);
            save_range(ram,0x80227210,4);save_range(ram,0x80102308,8);
            save_range(ram,rules::participants,2*rules::participant_size);
            write16(ram,0x80172EE2,player.handle);
            write8(ram,0x80227A83,crew);write8(ram,0x80227B60,id);write8(ram,0x80227BCC,0);
            for(unsigned n=0;n<std::min(7u,read(ram,player.unit+0x34,1));++n)
                write32(ram,0x80227A88+4*n,read(ram,player.unit+0x38+4*n,4));
            spirit_running=true;spirit_menu=false;current["spirit_running"]=true;
            current["action"]=action;record("spirit-cast");current["visible"]=false;owning=false;
            lock.unlock();invoke(ram,ctx,0x801D6A68,id,0);return true;
        }
        return true;
    }
    if(spirit_menu)return true;
    if(action=="animation") {
        write8(ram,0x8015DDA8,read(ram,0x8015DDA8,1)^4);
        current["animation"]=(read(ram,0x8015DDA8,1)&4)==0;record("animation");return true;
    }
    if(action=="back" && !current.value("can_cancel",false))return true;
    if((action=="evade" || action=="defend") && mode!=2)return true;
    if(action=="weapon" && mode==1 && !current.value("can_cancel",false))return true;
    if(action!="confirm" && action!="back" && action!="weapon" && action!="evade" && action!="defend")return true;
    current["action"]=action;record("action");current["visible"]=false;owning=false;
    lock.unlock();
    // Feed the original menu's A/B edge and selected entry exactly once. Its
    // validation, weapon chooser, battle events and state transitions stay intact.
    const auto old=read(ram,0x80178A08,2);
    write16(ram,0x80178A08,(action=="back" || (action=="weapon" && mode==1))?0x4000:0x8000);
    if(mode==2) {
        write8(ram,0x80227A81,action=="weapon"?1:action=="evade"?2:action=="defend"?3:0);
        srw64_original_battle_response_step(ram,ctx);
    } else {
        srw64_original_battle_confirm_step(ram,ctx);
        // B returns only to target selection. Open the original attack weapon
        // menu as well; it retains moved-unit eligibility and range validation.
        if(action=="weapon")invoke(ram,ctx,0x801D3010);
    }
    write16(ram,0x80178A08,old);
    return true;
}
}
void configure(const std::filesystem::path& directory) {
    hdmap::set_original_frames(keep_original_frame);
    if(const char* v=std::getenv("SRW64_NATIVE_BATTLE_UI");v && std::string_view(v)=="0")return;
    if(!std::getenv("SRW64_DIALOGUE_DATA"))return; // Legacy unprofiled runs keep the original UI.
    std::ifstream source(std::getenv("SRW64_DIALOGUE_DATA"));
    const auto data=json::parse(source);
    if(data.contains("battle_assets"))art=data.at("battle_assets");
    log.open(directory/"battle-page-events.jsonl");srw64_game_hooks.battle_step=step;srw64_game_hooks.battle_spirit_return=return_from_spirit;
}
json state() {
    std::lock_guard lock(mutex);
    auto result=current;
    const auto vi=srw64_current_vi(),seen=original_vi.load();
    result["original"]=seen && vi>=seen && vi-seen<=3;
    if(result["original"].get<bool>()){result["animation"]=original_animation.load();result["mode"]=original_mode.load();}
    result["original_images"]=original_screen();
    return result;
}
bool original_screen() {
    if(settings::battle_ui()!=settings::BattleUi::Original)return false;
    const auto vi=srw64_current_vi(),seen=screen_vi.load();
    return seen && vi>=seen && vi-seen<=3;
}
void answer(uint64_t id,const std::string& action){std::lock_guard lock(mutex);if(current.value("visible",false) && current.value("serial",uint64_t{})==id && pending.empty())pending=action;}
bool owns_input(){return owning || window_owning;}
void window_claim_input(bool value){window_owning=value;}
uint16_t input(uint16_t buttons){held&=buttons;if(owns_input())held|=buttons;return buttons&~held;}
}
