#pragma once
// Music replacement (docs/design/audio-mods.md): an Ogg file in an enabled MOD package's
// audio/music/<song>.ogg takes the place of the original's song everywhere 8007E810 plays
// it. The original is stopped and the host mixes the decoded file into the game's own
// audio stream (audio.cpp), so it follows the game's audio clock. This header holds the
// parts tested without a game (tests/native_music.cpp): which file wins for a song or a
// battle quote's voice and how a decoded track is played into a buffer.
#include "mod_library.hpp"
#include "recomp.h"
#include "json/json.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace srw64::music {
namespace fs=std::filesystem;
using json=nlohmann::json;

// The enemy reinforcement jingle (0x26): the map waits for it to end (801E076C) before
// the stage music comes back, so a replacement plays once unless music.json says loop.
inline constexpr int jingle_song=0x26;

struct Track {
    fs::path file;
    std::string package;
    std::optional<std::pair<uint64_t,uint64_t>> loop;   // samples at the file's rate, [start, end)
    std::optional<bool> once;                           // music.json "loop": false
    double gain_db{};
    json title;                                         // a string or {locale: string}
};

// A package's audio/music.json (optional): {"schema": "srw64.music.v1", "tracks": {"7":
// {"loop": [start, end], "loop": false, "gain": -2.0, "title": {...}}}}. Fields of the
// wrong type are ignored rather than refusing the package's files.
inline std::map<int,Track> settings(const json& data) {
    std::map<int,Track> out;
    if(!data.is_object() || data.value("schema","")!="srw64.music.v1" || !data.contains("tracks") || !data["tracks"].is_object())return out;
    for(const auto& [key,v]:data["tracks"].items()) {
        int song=0;
        try{song=std::stoi(key);}catch(...){continue;}
        if(!v.is_object())continue;
        Track t;
        if(v.contains("loop")) {
            const auto& loop=v["loop"];
            if(loop.is_boolean())t.once=!loop.get<bool>();
            else if(loop.is_array() && loop.size()==2 && loop[0].is_number_unsigned() && loop[1].is_number_unsigned() && loop[0].get<uint64_t>()<loop[1].get<uint64_t>()) {
                // A section to loop loops, the reinforcement jingle's too.
                t.loop=std::pair{loop[0].get<uint64_t>(),loop[1].get<uint64_t>()};
                t.once=false;
            }
        }
        if(v.contains("gain") && v["gain"].is_number())t.gain_db=v["gain"].get<double>();
        if(v.contains("title") && (v["title"].is_string() || v["title"].is_object()))t.title=v["title"];
        out[song]=t;
    }
    return out;
}

// The replacement for each song from the active packages, bottom first, so the package
// higher in the list wins. Files are audio/music/<decimal song>.ogg.
inline std::map<int,Track> collect(const std::vector<mods::Package>& packages,
                                   const std::function<json(const fs::path&)>& read_json) {
    std::map<int,Track> out;
    for(const auto& p:packages) {
        if(!p.active)continue;
        const auto dir=p.folder/"audio"/"music";
        std::error_code error;
        if(!fs::is_directory(dir,error))continue;
        const auto extra=settings(read_json(p.folder/"audio"/"music.json"));
        for(const auto& item:fs::directory_iterator(dir,error)) {
            if(!item.is_regular_file(error))continue;
            auto ext=item.path().extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(ext!=".ogg")continue;
            const auto stem=item.path().stem().string();
            if(stem.empty() || stem.size()>3 || !std::all_of(stem.begin(),stem.end(),[](unsigned char c){return std::isdigit(c);}))continue;
            const int song=std::stoi(stem);
            if(song<1 || song>255)continue;
            Track t=extra.contains(song)?extra.at(song):Track{};
            t.file=item.path();t.package=p.id();
            out[song]=t;
        }
    }
    return out;
}

// Who fights to which song (music.json "battle"): the original takes a player's song from
// its main pilot (ROM table 0x7D6A0) and a fight with no player unit from the attacker's
// unit (0x7DF30); a package can put another song in either.
inline constexpr uint32_t pilot_songs=0x7D6A0,unit_songs=0x7DF30;
inline constexpr unsigned pilot_count=361,unit_count=363;
struct BattleSongs {std::map<int,int> pilot,unit;};

inline BattleSongs battle_settings(const json& data) {
    BattleSongs out;
    if(!data.is_object() || data.value("schema","")!="srw64.music.v1" || !data.contains("battle") || !data["battle"].is_object())return out;
    const auto read=[&](const char* key,unsigned count,std::map<int,int>& into) {
        if(!data["battle"].contains(key) || !data["battle"][key].is_object())return;
        for(const auto& [k,v]:data["battle"][key].items()) {
            int index=-1;
            try{index=std::stoi(k);}catch(...){continue;}
            if(index<0 || unsigned(index)>=count || !v.is_number_integer())continue;
            const int song=v.get<int>();
            if(song>=1 && song<=255)into[index]=song;
        }
    };
    read("pilot",pilot_count,out.pilot);
    read("unit",unit_count,out.unit);
    return out;
}

// The active packages' battle tables, bottom first, so the package higher in the list wins.
inline BattleSongs collect_battle(const std::vector<mods::Package>& packages,
                                  const std::function<json(const fs::path&)>& read_json) {
    BattleSongs out;
    for(const auto& p:packages) {
        if(!p.active)continue;
        const auto extra=battle_settings(read_json(p.folder/"audio"/"music.json"));
        for(const auto& [k,v]:extra.pilot)out.pilot[k]=v;
        for(const auto& [k,v]:extra.unit)out.unit[k]=v;
    }
    return out;
}

// 801E07C4 reads a song from one of the two tables at a ROM address: the package's song
// for that pilot or unit, if it has one.
inline std::optional<int> battle_song(const BattleSongs& songs,uint32_t rom) {
    if(rom>=pilot_songs && rom<pilot_songs+2*pilot_count && !((rom-pilot_songs)&1)) {
        const auto found=songs.pilot.find(int((rom-pilot_songs)/2));
        if(found!=songs.pilot.end())return found->second;
    } else if(rom>=unit_songs && rom<unit_songs+2*unit_count && !((rom-unit_songs)&1)) {
        const auto found=songs.unit.find(int((rom-unit_songs)/2));
        if(found!=songs.unit.end())return found->second;
    }
    return std::nullopt;
}

// Battle voices (docs/design/audio-mods.md §4.3): audio/voice/battle/<record>.ogg speaks the
// battle quote with that text record as its box opens, once, over the game's own sound.
// Records 5799-5812 are the flagship's last words, 5813-14226 the general quotes, 14227-17346
// the conditional ones (docs/guide/audio-mods/battle-voices.csv).
inline constexpr int first_quote=5799,last_quote=17346;
struct Line {
    fs::path file;
    std::string package;
    double gain_db{};
    unsigned delay_ms{};    // after the box opens
};
// A package's audio/voice.json (optional): {"schema": "srw64.voice.v1", "gain": -3.0,
// "delay": 120}, for every line in it. Fields out of range or of the wrong type are ignored.
struct VoiceSettings {double gain_db{};unsigned delay_ms{};};
inline VoiceSettings voice_settings(const json& data) {
    VoiceSettings out;
    if(!data.is_object() || data.value("schema","")!="srw64.voice.v1")return out;
    if(data.contains("gain") && data["gain"].is_number() && data["gain"].get<double>()>=-30 && data["gain"].get<double>()<=12)out.gain_db=data["gain"].get<double>();
    if(data.contains("delay") && data["delay"].is_number_unsigned() && data["delay"].get<unsigned>()<=2000)out.delay_ms=data["delay"].get<unsigned>();
    return out;
}
// The line for each quote record from the active packages, bottom first, so the package
// higher in the list wins.
inline std::map<int,Line> collect_voices(const std::vector<mods::Package>& packages,
                                         const std::function<json(const fs::path&)>& read_json) {
    std::map<int,Line> out;
    for(const auto& p:packages) {
        if(!p.active)continue;
        const auto dir=p.folder/"audio"/"voice"/"battle";
        std::error_code error;
        if(!fs::is_directory(dir,error))continue;
        const auto extra=voice_settings(read_json(p.folder/"audio"/"voice.json"));
        for(const auto& item:fs::directory_iterator(dir,error)) {
            if(!item.is_regular_file(error))continue;
            auto ext=item.path().extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(ext!=".ogg")continue;
            const auto stem=item.path().stem().string();
            if(stem.empty() || stem.size()>5 || stem[0]=='0' || !std::all_of(stem.begin(),stem.end(),[](unsigned char c){return std::isdigit(c);}))continue;
            const int record=std::stoi(stem);
            if(record<first_quote || record>last_quote)continue;
            out[record]={item.path(),p.id(),extra.gain_db,extra.delay_ms};
        }
    }
    return out;
}

// A decoded track: interleaved stereo at its own rate.
struct Pcm {
    std::vector<int16_t> samples;
    unsigned rate{44100};
    uint64_t frames() const {return samples.size()/2;}
};

// One track playing into the output: position, loop, gain and fade. The mixer reads it on
// the audio thread under the engine's lock.
struct Voice {
    std::shared_ptr<const Pcm> pcm;
    double position{};
    uint64_t loop_start{},loop_end{};   // 0, 0: the whole track
    bool once{},finished{};
    double gain{1.0};
    uint64_t fade_total{},fade_left{};  // output frames
    uint64_t delay{};                   // output frames of silence before it starts
    bool fading() const {return fade_total!=0;}
};

// Adds `frames` stereo frames of the voice at `rate` to `out`, clamped; returns false once
// the voice has nothing more to play (ended or faded out).
inline bool mix(Voice& v,int16_t* out,size_t frames,unsigned rate,bool mono) {
    if(v.finished)return false;
    // The delay runs from the start, decoded or not.
    const size_t waited=size_t(std::min<uint64_t>(v.delay,frames));
    v.delay-=waited;
    if(!v.pcm || v.pcm->frames()<2)return true;   // still decoding: silence, keep the place
    const auto& s=v.pcm->samples;
    const uint64_t total=v.pcm->frames();
    const uint64_t end=v.loop_end && v.loop_end<=total?v.loop_end:total;
    const uint64_t start=v.loop_end?std::min(v.loop_start,end-1):0;
    const double step=double(v.pcm->rate)/double(rate ? rate : 44100);
    for(size_t i=waited;i<frames;++i) {
        if(v.position>=double(end)) {
            if(v.once){v.finished=true;return false;}
            v.position-=double(end-start);
            if(v.position<double(start))v.position=double(start);
        }
        const uint64_t a=uint64_t(v.position);
        const uint64_t b=a+1<end?a+1:(v.once?a:start);
        const double t=v.position-double(a);
        double l=s[2*a]+(s[2*b]-s[2*a])*t,r=s[2*a+1]+(s[2*b+1]-s[2*a+1])*t;
        double g=v.gain;
        if(v.fade_total) {
            if(v.fade_left==0){v.finished=true;return false;}
            g*=double(v.fade_left)/double(v.fade_total);
            --v.fade_left;
        }
        l*=g;r*=g;
        if(mono)l=r=(l+r)/2;
        const auto add=[](int16_t& o,double x){const double y=o+x;o=int16_t(y>32767?32767:y<-32768?-32768:y);};
        add(out[2*i],l);add(out[2*i+1],r);
        v.position+=step;
    }
    return true;
}

inline double gain_of(double db){return std::pow(10.0,db/20.0);}

// music.cpp: the game-thread hooks and the engine.
void configure();
// 8007E810 (a0 the song): true when the host plays a replacement and the original must
// not run; the original has then been stopped and D_800FFA6C holds the song.
bool play(uint8_t* ram,recomp_context* ctx);
// 8007E87C: the host fades its track too (the original still runs).
void fade(uint8_t* ram,recomp_context* ctx);
// 80077AD0: true when the host answered whether its track is still playing (v0).
bool playing(uint8_t* ram,recomp_context* ctx);
// 801E07C4 (a0 the table entry's ROM address, a1 the super mode): true when a package
// chose this fight's song and the host has played it in place of the original's.
bool battle(uint8_t* ram,recomp_context* ctx);
// The dialogue layer saw a battle quote's box open (native_dialogue.cpp): its line starts,
// and the one before stops. Game thread.
void battle_quote(unsigned record);
// The sound test's view of a song: replaced, and the package's title in the locale.
struct Shown {bool replaced{};std::string title;};
Shown shown(int song,const std::string& locale);
// The state for the debug status.
json status();
}
