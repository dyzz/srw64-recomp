#pragma once
// Combination attacks on the map's 機体データ weapon page (801E6410, the tactical overlay;
// docs/gameplay/combination-attacks.md §5): the original lists a combination attack only
// while 8020178C left it unlocked, that is with every partner on the field, in one group
// with it, with EN and morale to spare. Here the page lists them all (user, 2026-10-21):
// around the original's draw the lock bits of the unit's combination attacks are cleared
// and put back, a row the attack cannot use now is drawn again in the grey face, and
// under the page a line, grey while the attack cannot be used, names the partners in red
// and how close they must stand; the details' empty skill cell says 合体技. The unlock
// 石破ラブラブ天驚拳 also needs is not told (user, 2026-10-21). The attack list of the
// 攻撃 command (801F8F1C) keeps the original's rule: it lists what can hit a target.
//
// The page draws into the text engine's handle array 802271DD.. (the page number, then
// four handles a row, then the details), freed whole at each redraw; a cursor move
// redraws the whole page (801E79B8), so this runs each time. The host's own lines are
// ui_text labels (set_extra), dropped when a page resets (801E61BC), when the pages'
// dispatcher draws another page (801E7430), and whenever the game leaves the tactical modes.
#include "combo_attacks.hpp"
#include "enemy_pilot_page.hpp"
#include "guest_memory.hpp"
#include "recomp.h"
#include "funcs.h"
#include "ui_text.hpp"
#include "native_dialogue.hpp"
#include "localization/catalog.hpp"
#include "librecomp/game.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace srw64::map_combo {
using guest::read;
constexpr uint32_t row_handles=0x802271DE,selected_weapon=0x80227210,roster=0x8015E100,roster_side=0x258,roster_entry=0x14,
    positions=0x800FFA74,position_stride=0xC4,text_buffer=0x801614F0,game_mode=0x8015DA02;
// The page's own number formats: "%4d", "%d_%2d" (the range's ～), "$%2d" / "+%2d" / "-%2d" (hit).
constexpr uint32_t format_number=0x8021EBD0,format_range=0x8021EC3C,format_hit_zero=0x8021EC44,format_hit_plus=0x8021EC4C,format_hit_minus=0x8021EC54;
constexpr uint16_t text_weapon_menu=0xA8B,text_unit_names=0x20F,text_pilot_names=0x111E;
// Palette 2 is the dark grey of resource 4 (ui_text kPaletteHandles); the page's own
// "cannot use" palette 1 comes out red here.
constexpr unsigned rows_per_page=7,row_y=0x2F,row_pitch=16,palette_grey=2,tile=16;
constexpr int handle_bases[3]={0x42,0x60,0x7E};   // 801E514C: the side's first handle
// Where the host's lines go: under the panel like the intermission's hint bar, and in the
// details' skill cell (0x108, 0xBC), empty for a weapon that names no skill.
constexpr int hint_x=0x1C,hint_y=0xE0,hint_width=0x10C,kind_x=0x108,kind_y=0xBC,kind_width=0x22;

struct Status{bool ok=true;std::vector<ui_text::Segment> line;};
struct Page {
    bool active=false;
    uint32_t unit=0,array=0,unit_number=0;unsigned side=0,page=0,enemy=0;int handle=-1;
    std::vector<std::pair<uint32_t,uint8_t>> cleared;   // weapon instance, its flag byte before
    std::map<uint32_t,Status> status;                    // by weapon instance
};
inline std::atomic_bool shown{};

inline bool tactical(const uint8_t* ram){const unsigned m=read(ram,game_mode,1);return m==3 || m==0xB || m==0x11 || m==0x16 || m==0x22;}
inline float position(const uint8_t* ram,int handle,unsigned axis) {
    const uint32_t bits=read(ram,positions+uint32_t(handle)*position_stride+axis*4,4);
    float f;std::memcpy(&f,&bits,4);return f;
}
inline std::string ui(const char* key){return localization::catalog().ui(key);}
inline std::string with(const char* key,const char* token,const std::string& value) {
    std::string text=ui(key);
    if(const auto at=text.find(token);at!=std::string::npos)text.replace(at,std::strlen(token),value);
    return text;
}
inline std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for(const auto& n:names){if(!out.empty())out+=ui("ability_combo_sep");out+=n;}
    return out;
}
inline int lead_pilot_number(const uint8_t* ram,uint32_t unit) {
    const uint32_t pilot=read(ram,unit+0x38,4);
    return guest::valid(pilot,0x4C)?int(read(ram,pilot+2,2)):-1;
}
inline uint32_t roster_unit(const uint8_t* ram,unsigned side,int handle) {
    const int slot=handle-handle_bases[side];
    if(slot<0 || slot>=30)return 0;
    const uint32_t unit=read(ram,roster+side*roster_side+uint32_t(slot)*roster_entry+0xC,4);
    return guest::valid(unit,0x54)?unit:0;
}
// What 80200530 would find for this unit's combination attack right now: the partners
// (801E9514: the side's first on the field), one group, EN, morale, the skill condition,
// the pilot pairing. ok when the attack is usable where the unit stands.
inline Status evaluate(uint8_t* ram,recomp_context* ctx,const Page& page,uint32_t weapon) {
    Status s;
    const auto rom=recomp::get_rom();
    const auto attack=combo_attacks::find(rom,uint16_t(read(ram,weapon+2,2)));
    if(!attack)return s;
    const auto* row=combo_attacks::lead_row(*attack,int(page.unit_number),lead_pilot_number(ram,page.unit));
    std::vector<std::string> names;std::vector<std::pair<uint32_t,int>> members={{page.unit,page.handle}};std::vector<int> leads={lead_pilot_number(ram,page.unit)};
    if(!row) {
        // This form never leads the attack: the forms that do.
        std::vector<int> seen;
        for(const auto& r:attack->rows) {
            const int form=r.by_pilot?r.unit:r.id;
            if(form>=0 && std::find(seen.begin(),seen.end(),form)==seen.end()){seen.push_back(form);names.push_back(dialogue::ui_text(ram,uint16_t(text_unit_names+form)));}
        }
        s.ok=false;
    } else for(const int16_t id:row->partners) {
        uint32_t result=0;
        enemy_pilot_page::call(ram,ctx,LOOKUP_FUNC(0x801E9514),page.side,row->by_pilot,uint32_t(uint16_t(id)),0,0,&result);
        const int handle=int16_t(result);
        uint32_t unit=handle>=0?roster_unit(ram,page.side,handle):0;
        if(unit && row->by_pilot && int(read(ram,unit+2,2))!=row->unit)unit=0;
        names.push_back(dialogue::ui_text(ram,uint16_t((row->by_pilot?text_pilot_names:text_unit_names)+id)));
        if(unit){members.push_back({unit,handle});leads.push_back(lead_pilot_number(ram,unit));}
        else s.ok=false;   // not on the field
    }
    if(row && s.ok) {
        // One group: eight neighbours, by the truncated map positions (80200530).
        std::vector<int> x,y;
        for(const auto& [unit,handle]:members){x.push_back(int(position(ram,handle,0)));y.push_back(int(position(ram,handle,1)));}
        std::vector<bool> seen(members.size(),false);std::vector<size_t> open={0};seen[0]=true;
        while(!open.empty()) {
            const size_t a=open.back();open.pop_back();
            for(size_t b=0;b<members.size();++b)if(!seen[b] && std::abs(x[a]-x[b])<=int(tile) && std::abs(y[a]-y[b])<=int(tile)){seen[b]=true;open.push_back(b);}
        }
        if(std::find(seen.begin(),seen.end(),false)!=seen.end())s.ok=false;
    }
    if(row && s.ok)for(const auto& [unit,handle]:members) {
        const uint32_t pilot=read(ram,unit+0x38,4);
        if(read(ram,unit+8,2)<read(ram,weapon+0xD,1)){s.ok=false;break;}
        if(guest::valid(pilot,0x4C) && read(ram,pilot+0x20,2)<read(ram,weapon+0xE,1)){s.ok=false;break;}
        if(read(ram,weapon+0xF,1)>=2) {
            uint32_t result=0;
            enemy_pilot_page::call(ram,ctx,LOOKUP_FUNC(0x801E6214),page.side,pilot,weapon,unit,1,&result);
            if(result&0xFF){s.ok=false;break;}
        }
    }
    if(row && s.ok && !combo_attacks::pilots_met(*attack,leads))s.ok=false;
    // The line (user, 2026-10-21): grey while the attack cannot be used, the partners'
    // names red, and last how close they must stand; a form that never leads it names the
    // forms that do.
    const float white[3]={1,1,1},grey[3]={.62f,.69f,.76f},red[3]={1,.55f,.55f};
    const float* tone=s.ok?white:grey;
    const auto seg=[&](const std::string& text,const float* tint){if(text.empty())return;ui_text::Segment g;g.text=text;std::copy(tint,tint+3,g.tint);s.line.push_back(g);};
    const std::string sep=ui("ability_combo_sep");
    if(!row){seg(with("ability_combo_forms","{forms}",joined(names)),tone);return s;}
    const std::string pattern=ui("ability_combo_with");
    const auto at=pattern.find("{names}");
    seg(at==std::string::npos?pattern:pattern.substr(0,at),tone);
    for(size_t k=0;k<names.size();++k){if(k)seg(sep,tone);seg(names[k],red);}
    if(at!=std::string::npos)seg(pattern.substr(at+7),tone);
    seg(with("ability_combo_range","{n}","1"),tone);
    return s;
}
// Before 801E6410(enemy, page, row, weapons; unit, pilot, flag): the unit's combination
// attacks unlocked for the draw, each one's status kept.
inline Page prepare(uint8_t* ram,recomp_context* ctx) {
    Page page;
    page.enemy=uint32_t(ctx->r4)&0xFF;page.page=uint32_t(ctx->r5)&0xFF;page.array=uint32_t(ctx->r7);
    page.unit=read(ram,uint32_t(ctx->r29)+0x10,4);
    if(!guest::valid(page.unit,0x54) || !tactical(ram))return page;
    page.unit_number=read(ram,page.unit+2,2);
    for(unsigned side=0;side<3 && page.handle<0;++side)for(unsigned slot=0;slot<30;++slot) {
        const uint32_t entry=roster+side*roster_side+slot*roster_entry;
        if(read(ram,entry,1)==1 && read(ram,entry+0xC,4)==page.unit){page.side=side;page.handle=handle_bases[side]+int(slot);break;}
    }
    if(page.handle<0)return page;
    const unsigned count=read(ram,page.unit+0x2C,1);
    if(!guest::valid(page.array,count*0x24))return page;
    for(unsigned i=0;i<count;++i) {
        const uint32_t w=page.array+i*0x24;
        const unsigned flags=read(ram,w+0x22,1);
        if(!(flags&combo_attacks::flag_combo))continue;
        bool own=false;
        for(unsigned s=0;s<5;++s)own|=read(ram,w+0x18+s*2,2)==page.unit_number;
        if(!own)continue;
        page.status[w]=evaluate(ram,ctx,page,w);
        page.cleared.push_back({w,uint8_t(flags)});
        guest::write8(ram,w+0x22,uint8_t(flags&~combo_attacks::flag_locked));
    }
    page.active=true;
    return page;
}
inline void sprintf1(uint8_t* ram,recomp_context* ctx,uint32_t format,uint32_t a,uint32_t b=0) {
    enemy_pilot_page::call(ram,ctx,resident_func_800B4950,text_buffer,format,a,b);
}
inline void draw(uint8_t* ram,recomp_context* ctx,uint32_t at,unsigned x,unsigned y) {
    if(const int8_t handle=enemy_pilot_page::renew(ram,ctx,at);handle>=0)enemy_pilot_page::call(ram,ctx,resident_func_8008D3C4,uint32_t(int32_t(handle)),x,y,palette_grey);
}
// After the draw: the lock bits back; the rows of this page as the original listed them
// (array order, the form's weapons, kind 1 not for an enemy, locked ones left out) and
// those of an attack not usable now drawn again grey; the host's lines for the selected one.
inline void finish(uint8_t* ram,recomp_context* ctx,const Page& page) {
    for(const auto& [w,flags]:page.cleared)guest::write8(ram,w+0x22,flags);
    if(!page.active){if(shown.exchange(false)){ui_text::clear_extra("map-combo");ui_text::clear_extra("map-combo-kind");}return;}
    std::vector<uint32_t> rows;
    const unsigned count=read(ram,page.unit+0x2C,1);
    for(unsigned i=0;i<count;++i) {
        const uint32_t w=page.array+i*0x24;
        bool own=false;
        for(unsigned s=0;s<5;++s){const int slot=int16_t(read(ram,w+0x18+s*2,2));if(slot==-1)break;if(uint32_t(slot)==page.unit_number){own=true;break;}}
        if(!own)continue;
        if(read(ram,w+0xF,1)==1 && page.enemy)continue;
        const bool combo=page.status.count(w);
        if(!combo && (read(ram,w+0x22,1)&combo_attacks::flag_locked))continue;
        rows.push_back(w);
    }
    for(unsigned k=0;k<rows_per_page;++k) {
        const unsigned n=page.page*rows_per_page+k;
        if(n>=rows.size())break;
        const uint32_t w=rows[n];
        const auto found=page.status.find(w);
        if(found==page.status.end() || found->second.ok)continue;
        const uint32_t at=row_handles+k*4;const unsigned y=row_y+k*row_pitch;
        if(const int8_t handle=enemy_pilot_page::renew(ram,ctx,at);handle>=0)
            enemy_pilot_page::call(ram,ctx,resident_func_8008D0E8,uint32_t(int32_t(handle)),text_weapon_menu+read(ram,w+2,2),0x1C,y,palette_grey);
        sprintf1(ram,ctx,format_number,read(ram,w+6,2));draw(ram,ctx,at+1,0xB4,y);
        const unsigned lo=read(ram,w+8,1),hi=read(ram,w+9,1);
        if(lo==hi)sprintf1(ram,ctx,format_number,hi);else sprintf1(ram,ctx,format_range,lo,hi);
        draw(ram,ctx,at+2,0xE4,y);
        const int hit=int8_t(read(ram,w+0xA,1));
        sprintf1(ram,ctx,hit==0?format_hit_zero:hit>0?format_hit_plus:format_hit_minus,uint32_t(hit<0?-hit:hit));
        draw(ram,ctx,at+3,0x110,y);
    }
    const uint32_t selected=read(ram,selected_weapon,4);
    const auto found=page.status.find(selected);
    if(found==page.status.end() || found->second.line.empty()){if(shown.exchange(false)){ui_text::clear_extra("map-combo");ui_text::clear_extra("map-combo-kind");}return;}
    ui_text::set_extra_line("map-combo",hint_x,hint_y,hint_width,found->second.line);
    if(read(ram,selected+0xF,1)<2)ui_text::set_extra("map-combo-kind",kind_x,kind_y,"ability_combo",kind_width);
    else ui_text::clear_extra("map-combo-kind");
    shown=true;
}
// A page reset (801E61BC), the pilot page drawn, or the game off the map: the lines go.
inline void reset() {
    if(shown.exchange(false)){ui_text::clear_extra("map-combo");ui_text::clear_extra("map-combo-kind");}
}
inline void frame(const uint8_t* ram){if(shown.load() && !tactical(ram))reset();}
}
