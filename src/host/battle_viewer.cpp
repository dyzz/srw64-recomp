#include "battle_viewer.hpp"
#include "game_hooks.hpp"
#include "guest_memory.hpp"
#include "native_dialogue.hpp"
#include "librecomp/game.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>

uint64_t srw64_current_vi();
namespace srw64::battle_viewer {
namespace {
using namespace guest;
using json=nlohmann::json;
// Participant records (docs/design/battle-viewer.md §2): side 0 attacks (8009C2DC writes
// D_80178C78 = 0, D_80178B50 = 1), 0x1074 bytes each.
constexpr uint32_t records=0x800F97E0,record_size=0x1074;
// Game mode (80080188 writes it; the previous one beside it). The title overlay runs in
// modes 7 (after the boot screens) and 0x1D / 0x1E (back from a demo); its major state
// D_801CC3A6 is 2/3 on the title, and D_801CC390 counts the idle frames to the attract
// demo (801CA21C starts it at 180); D_801CC3A4 (u16) counts them on 请按START and the ring
// to the opening story (801C5D94, 801C6514: past 0x385 they call 801C9BF8, major 12).
constexpr uint32_t mode=0x8015DA02,previous_mode=0x8015DD60,title_state=0x801CC3A6,title_idle=0x801CC390,title_story_idle=0x801CC3A4;
constexpr uint8_t demo_mode=0x1C;
bool title_mode(unsigned m){return m==7 || m==0x1D || m==0x1E;}
// Cleared before the demo starts (§5): D_80161310 = 1 would replay the next demo record
// and after five return to the boot screens; D_801614EA counts demos; D_801CC3A3 is the
// title's "start the game" flag its exit checks.
constexpr uint32_t demo_repeat=0x80161310,demo_count=0x801614EA,title_start=0x801CC3A3;
// The pilot's ability record number per character (u16), resident.
constexpr uint32_t abilities=0x800CA9C4;
// ROM: unit records (HP, EN, size bits at +4), the per-unit weapon animation overrides
// {weapon, unit, animation} that 800AB470 applies (0-terminated), and the title's
// サウンドセレクト {text, song} x 49 (title overlay D_801CB290).
constexpr uint32_t unit_records=0x71B80,unit_record_size=0x24,animation_overrides=0x561AC;
constexpr uint32_t sound_table=0x801CB290;
constexpr unsigned sound_songs=49;
// The song playing (8007E810 keeps it; -1 none).
constexpr uint32_t playing=0x800FFA6C;
// The defender's reactions that take no HP (801C3E7C): everything but a hit (-1), a
// broken barrier (0xD-0x10) and the shield (0x11).
bool takes_damage(int reaction){return reaction==-1 || (reaction>=0xD && reaction<=0x11);}

// Battle backgrounds, one daytime record per kind of the battle scenes catalog
// (assets/hd-ai/battle-backgrounds/catalog): the record is D_800C5940[a] x 101 + b,
// written as +0xB / +0xA; +0xC is 1 for the scenes the units float in.
struct Scene{const char* key;uint8_t a,b,c;};
constexpr Scene scenes_table[]={
    {"plains",0,0,0},{"wasteland",0,7,0},{"rocks",0,21,0},{"village",0,22,0},{"mountains",0,24,0},
    {"forest",0,25,0},{"sea",0,31,0},{"city",0,37,0},{"sky",0,39,1},{"ruins",0,41,0},{"base",0,51,0},
    {"bridge",0,54,0},{"harbor",0,58,0},{"snow",6,32,0},{"desert",10,7,0},{"cave",15,15,0},{"moon",17,21,0},
    {"space",17,100,1},{"fortress",20,15,0},{"other",20,83,0},{"otherworld",22,61,1}};

struct Write{unsigned side;uint32_t offset;std::vector<uint8_t> bytes;};
std::mutex mutex;
std::vector<Write> raw;          // a debug request: the bytes as given
json choice;                     // a page request: computed when the records are filled
bool pending=false,armed=false,running=false;
std::atomic_bool page_open{},returned{};
unsigned started=0,filled=0;
int song=-1;
// A song the page plays to listen (-1 none), what it asked for last, and the title's own
// song to put back when it stops (-2: nothing to put back).
std::atomic_int listen{-1};
int listening=-1,title_song=-2;
json last,songs_cache=json::array();
std::ofstream log;

void note(const json& event){if(log.is_open()){log<<event.dump()<<'\n';log.flush();}}

std::span<const uint8_t> rom(){return recomp::get_rom();}
unsigned rom16(uint32_t at){const auto r=rom();return at+1<r.size()?unsigned(r[at])<<8|r[at+1]:0;}
unsigned rom8(uint32_t at){const auto r=rom();return at<r.size()?r[at]:0;}

// The battle music (801E085C, docs/design/battle-viewer.md §6.7): a player unit plays its
// pilot's song (s16 per character, ROM 0x7D6A0), except the forms its super mode makes
// (ゴッドガンダムH, the S forms, V-MAX); an enemy its unit's (s16 per unit, 0x7DF30). The
// viewer plays the attacker's: its pilot's when it has one, else its unit's.
constexpr uint32_t pilot_songs=0x7D6A0,unit_songs=0x7DF30;
constexpr unsigned pilot_song_count=361,unit_song_count=363;
int attacker_song(unsigned unit,unsigned actor) {
    if(unit==2)return 0xA;                                                       // 明鏡止水
    if(unit==4 || unit==10 || unit==12 || unit==14 || unit==16)return 0xB;      // 燃え上がれ闘志
    if(unit==270 || unit==273 || unit==277 || unit==278)return 0xD;            // V-MAX
    const int pilot=actor<pilot_song_count?int16_t(rom16(pilot_songs+2*actor)):-1;
    return pilot>0?pilot:unit<unit_song_count?int16_t(rom16(unit_songs+2*unit)):-1;
}

int animation(unsigned unit,unsigned weapon) {
    for(uint32_t at=animation_overrides;;at+=6) {
        const unsigned w=rom16(at),u=rom16(at+2),a=rom16(at+4);
        if(!w && !u && !a)return int(weapon);
        if(w==weapon && u==unit)return int(a);
    }
}

void put(std::vector<Write>& out,unsigned side,uint32_t offset,unsigned size,int value) {
    Write w{side,offset,{}};
    for(unsigned n=size;n-->0;)w.bytes.push_back(uint8_t(unsigned(value)>>(8*n)));
    out.push_back(std::move(w));
}

// The participant records for a page choice (§5.3).
std::vector<Write> compute(const uint8_t* ram,const json& c) {
    std::vector<Write> out;
    const json* sides[2]={&c.at("attacker"),&c.at("defender")};
    unsigned hp[2]{};
    for(unsigned s=0;s<2;++s)hp[s]=rom16(unit_records+sides[s]->at("unit").get<unsigned>()*unit_record_size);
    const int reaction=c.value("reaction",-1);
    const bool counter=c.value("counter",false);
    const auto dealt=[&](unsigned target,unsigned damage,bool destroy){
        return destroy?std::max(damage,hp[target]):std::min(damage,hp[target]>1?hp[target]-1:0u);
    };
    for(unsigned s=0;s<2;++s) {
        const json& side=*sides[s];
        const unsigned unit=side.at("unit").get<unsigned>(),pilot=side.at("pilot").get<unsigned>();
        const uint32_t record=unit_records+unit*unit_record_size;
        const unsigned en=rom16(record+2),size_bits=rom8(record+4);
        const auto& scene=scenes_table[std::min<size_t>(c.at("scene").at(s).get<size_t>(),std::size(scenes_table)-1)];
        const bool attacking=s==0;
        const int weapon=side.contains("weapon") && side.at("weapon").is_number()?animation(unit,side.at("weapon").get<unsigned>()):0;
        put(out,s,0x2,2,int(read(ram,abilities+2*pilot,2)));
        put(out,s,0x4,2,int(unit));
        put(out,s,0x6,2,attacking || counter?weapon:0);
        // The defender's reaction to the attack; the attacker's to the counter.
        put(out,s,0x8,2,attacking?(counter?c.value("counter_reaction",-1):-1):reaction);
        put(out,s,0xA,1,scene.b);put(out,s,0xB,1,scene.a);put(out,s,0xC,1,scene.c);
        for(unsigned bit=0;bit<5;++bit)if(size_bits&(1u<<bit)){put(out,s,0xD,1,int(bit));break;}
        // The defender's order: 0 counters, 1 defends (HUD 防御), 2 evades (回避).
        put(out,s,0x18,1,attacking?0:counter?0:reaction==0x16?2:1);
        const unsigned damage=attacking?dealt(1,c.value("damage",0u),c.value("destroy",false)):
                                        dealt(0,c.value("counter_damage",0u),c.value("counter_destroy",false));
        put(out,s,0x1A,2,int(damage));
        put(out,s,0x20,2,int(hp[s]));put(out,s,0x22,2,int(hp[s]));put(out,s,0x24,2,int(en));put(out,s,0x26,2,int(en));
    }
    return out;
}

bool start(uint8_t* ram,int* music) {
    const unsigned current=read(ram,mode,1);
    // Back on the title after a viewer battle: the page opens again.
    if(running && current!=demo_mode && title_mode(current)) {
        std::lock_guard lock(mutex);
        running=false;returned=true;
        note({{"event","returned"},{"vi",srw64_current_vi()},{"mode",current}});
    }
    if(!title_mode(current))return false;
    const unsigned state=read(ram,title_state,1);
    if(state>=1 && state<=3) {
        // The song list once the title overlay is in, for the page.
        if(songs_cache.empty()) {
            json list=json::array();
            for(unsigned n=0;n<sound_songs;++n)
                list.push_back({{"song",read(ram,sound_table+4*n+2,2)},{"text",dialogue::ui_text(ram,uint16_t(read(ram,sound_table+4*n,2)))}});
            std::lock_guard lock(mutex);songs_cache=std::move(list);
        }
        // No attract demo while the page is open or a battle waits.
        bool waiting;{std::lock_guard lock(mutex);waiting=pending;}
        if(page_open || waiting || returned){write32(ram,title_idle,0);write16(ram,title_story_idle,0);}
        // Listening on the BGM list: the song, and the title's back when it stops or the
        // page closes.
        const int wanted=page_open?listen.load():-1;
        if(wanted!=listening) {
            if(wanted>=0) {
                if(title_song==-2)title_song=int16_t(read(ram,playing,2));
                *music=wanted;
            } else if(title_song!=-2) {
                *music=title_song;title_song=-2;
            }
            listening=wanted;
            note({{"event","listen"},{"vi",srw64_current_vi()},{"song",wanted}});
        }
    }
    std::lock_guard lock(mutex);
    if(!pending || (state!=2 && state!=3))return false;
    write8(ram,demo_repeat,0);write8(ram,demo_count,0);write8(ram,title_start,0);
    pending=false;armed=true;running=true;++started;
    // The battle's song replaces one being listened to.
    listen=-1;listening=-1;title_song=-2;
    *music=song>=0?song:no_music;
    note({{"event","start"},{"vi",srw64_current_vi()},{"mode",current},{"title_state",state},{"song",song}});
    return true;
}

void filled_records(uint8_t* ram) {
    std::lock_guard lock(mutex);
    if(!armed)return;
    armed=false;++filled;
    const auto writes=choice.is_object()?compute(ram,choice):raw;
    for(const auto& w:writes)
        for(size_t i=0;i<w.bytes.size();++i)write8(ram,records+w.side*record_size+w.offset+uint32_t(i),w.bytes[i]);
    last={{"event","filled"},{"vi",srw64_current_vi()},{"mode",read(ram,mode,1)},{"previous_mode",read(ram,previous_mode,1)},
          {"writes",writes.size()},{"choice",choice}};
    note(last);
}
}

void install(const std::filesystem::path& output) {
    if(!output.empty())log.open(output/"battle-viewer-events.jsonl");
    srw64_game_hooks.viewer_start=start;
    srw64_game_hooks.demo_battle_filled=filled_records;
}

json request(const json& params) {
    std::vector<Write> next;
    json picked;
    if(params.contains("choice")) {
        picked=params.at("choice");
        for(const char* side:{"attacker","defender"})
            if(!picked.contains(side) || !picked.at(side).contains("unit") || !picked.at(side).contains("pilot"))throw std::invalid_argument("choice needs both sides' unit and pilot");
        if(!picked.contains("scene") || picked.at("scene").size()!=2)throw std::invalid_argument("choice needs two scenes");
    } else {
        for(const auto& w:params.at("writes")) {
            Write item{w.at("side").get<unsigned>(),w.at("offset").get<uint32_t>(),{}};
            const std::string hex=w.at("hex").get<std::string>();
            if(item.side>1 || hex.size()%2 || item.offset+hex.size()/2>record_size)throw std::invalid_argument("bad viewer write");
            for(size_t i=0;i<hex.size();i+=2)item.bytes.push_back(uint8_t(std::stoul(hex.substr(i,2),nullptr,16)));
            next.push_back(std::move(item));
        }
    }
    std::lock_guard lock(mutex);
    raw=std::move(next);choice=std::move(picked);
    song=params.value("song",choice.is_object()?choice.value("song",-1):-1);
    if(song<0 && choice.is_object())
        song=attacker_song(choice.at("attacker").at("unit").get<unsigned>(),choice.at("attacker").at("pilot").get<unsigned>());
    pending=true;armed=false;returned=false;listen=-1;
    return {{"pending",true},{"writes",raw.size()},{"choice",choice.is_object()}};
}

json state() {
    std::lock_guard lock(mutex);
    return {{"pending",pending},{"armed",armed},{"running",running},{"returned",bool(returned)},{"page_open",bool(page_open)},
            {"started",started},{"filled",filled},{"last",last}};
}

json songs(){std::lock_guard lock(mutex);return songs_cache;}
json scenes() {
    json list=json::array();
    for(const auto& s:scenes_table)list.push_back(s.key);
    return list;
}
int default_song(unsigned unit,unsigned actor){return attacker_song(unit,actor);}
int pilot_song(unsigned actor){return actor<pilot_song_count?int16_t(rom16(pilot_songs+2*actor)):-1;}
void set_page_open(bool open){page_open=open;}
void listen_song(int number){listen=number;}
int listened(){return listen;}
bool take_returned(){return returned.exchange(false);}
bool busy(){std::lock_guard lock(mutex);return pending || running;}
}
