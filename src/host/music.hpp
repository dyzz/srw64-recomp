#pragma once
// Music replacement (docs/design/audio-mods.md): an Ogg file in an enabled MOD package's
// audio/music/<song>.ogg takes the place of the original's song everywhere 8007E810 plays
// it. The original is stopped and the host mixes the decoded file into the game's own
// audio stream (audio.cpp), so it follows the game's audio clock. This header holds the
// parts tested without a game (tests/native_music.cpp): which file wins for a song or a
// battle quote's voice and how a decoded track is played into a buffer.
#include "mod_library.hpp"
#include "table_window.hpp"
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

// A package's own songs (a new character's battle theme): audio/music/<name>.ogg with a name
// that is not a number, "@name" in music.json. They get song numbers of their own, from 128
// up in the packages' order (bottom first) and their names' order, which the original never
// plays: the play hook (music.cpp play) stops the original's music and plays the file, so the
// number only has to be one no ROM song has (the original's are 1-49).
inline constexpr int first_named_song=128,last_named_song=255;
inline bool song_name(const std::string& s) {
    if(s.empty() || s.size()>40 || std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isdigit(c);}))return false;
    return std::all_of(s.begin(),s.end(),[](unsigned char c){return std::islower(c) || std::isdigit(c) || c=='.' || c=='_' || c=='-';});
}
// "package@name" -> song number, for the active packages' named songs.
inline std::map<std::string,int> named_songs(const std::vector<mods::Package>& packages) {
    std::map<std::string,int> out;
    int next=first_named_song;
    for(const auto& p:packages) {
        if(!p.active)continue;
        std::error_code error;
        const auto dir=p.folder/"audio"/"music";
        if(!fs::is_directory(dir,error))continue;
        std::vector<std::string> names;
        for(const auto& item:fs::directory_iterator(dir,error)) {
            if(!item.is_regular_file(error) || item.path().extension()!=".ogg")continue;
            if(const auto stem=item.path().stem().string();song_name(stem))names.push_back(stem);
        }
        std::sort(names.begin(),names.end());
        for(const auto& name:names)if(next<=last_named_song)out[p.id()+"@"+name]=next++;
    }
    return out;
}
// A song in music.json: a number 1-255, or "@name" (this package's) / "package@name" as
// `named` resolves it.
using SongOf=std::function<std::optional<int>(const std::string&)>;
inline std::optional<int> song_of(const json& v,const SongOf& named) {
    if(v.is_number_integer()) {
        const int song=v.get<int>();
        return song>=1 && song<=255?std::optional<int>(song):std::nullopt;
    }
    if(v.is_string() && named)return named(v.get<std::string>());
    return std::nullopt;
}
// `named` with a package's own: "@name" is the package's, "other@name" another's.
inline SongOf named_in(const std::map<std::string,int>& names,const std::string& package) {
    return [&names,package](const std::string& key)->std::optional<int>{
        const auto at=key.find('@');
        if(at==std::string::npos)return std::nullopt;
        const auto found=names.find((at==0?package:key.substr(0,at))+"@"+key.substr(at+1));
        return found==names.end()?std::nullopt:std::optional<int>(found->second);
    };
}

// A package's audio/music.json (optional): {"schema": "srw64.music.v1", "tracks": {"7":
// {"loop": [start, end], "loop": false, "gain": -2.0, "title": {...}}, "@theme": {...}}}.
// Fields of the wrong type are ignored rather than refusing the package's files.
inline std::map<int,Track> settings(const json& data,const SongOf& named={}) {
    std::map<int,Track> out;
    if(!data.is_object() || data.value("schema","")!="srw64.music.v1" || !data.contains("tracks") || !data["tracks"].is_object())return out;
    for(const auto& [key,v]:data["tracks"].items()) {
        int song=0;
        if(key.find('@')!=std::string::npos) {
            const auto found=named?named(key):std::nullopt;
            if(!found)continue;
            song=*found;
        } else {
            try{song=std::stoi(key);}catch(...){continue;}
        }
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
    const auto names=named_songs(packages);
    for(const auto& p:packages) {
        if(!p.active)continue;
        const auto dir=p.folder/"audio"/"music";
        std::error_code error;
        if(!fs::is_directory(dir,error))continue;
        const auto extra=settings(read_json(p.folder/"audio"/"music.json"),named_in(names,p.id()));
        for(const auto& item:fs::directory_iterator(dir,error)) {
            if(!item.is_regular_file(error))continue;
            auto ext=item.path().extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(ext!=".ogg")continue;
            const auto stem=item.path().stem().string();
            int song=0;
            if(const auto named=names.find(p.id()+"@"+stem);named!=names.end() && item.path().extension()==".ogg")song=named->second;
            else if(stem.empty() || stem.size()>3 || !std::all_of(stem.begin(),stem.end(),[](unsigned char c){return std::isdigit(c);}))continue;
            else song=std::stoi(stem);
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

// A key may name a package's new unit or character ("@final", "package@final"): `unit_of` and
// `pilot_of` give its number (mod_ids.hpp), when given. A song is a number or a named song
// ("@theme", `named`).
using NumberOf=std::function<std::optional<unsigned>(const std::string&)>;
inline BattleSongs battle_settings(const json& data,const NumberOf& unit_of={},const NumberOf& pilot_of={},const SongOf& named={}) {
    BattleSongs out;
    if(!data.is_object() || data.value("schema","")!="srw64.music.v1" || !data.contains("battle") || !data["battle"].is_object())return out;
    const auto read=[&](const char* key,unsigned count,const NumberOf& new_of,std::map<int,int>& into) {
        if(!data["battle"].contains(key) || !data["battle"][key].is_object())return;
        for(const auto& [k,v]:data["battle"][key].items()) {
            const auto song=song_of(v,named);
            if(!song)continue;
            if(k.find('@')!=std::string::npos) {
                const auto found=new_of?new_of(k):std::nullopt;
                if(found)into[int(*found)]=*song;
                continue;
            }
            int index=-1;
            try{index=std::stoi(k);}catch(...){continue;}
            if(index<0 || unsigned(index)>=count)continue;
            into[index]=*song;
        }
    };
    read("pilot",pilot_count,pilot_of,out.pilot);
    read("unit",unit_count,unit_of,out.unit);
    return out;
}

// The active packages' battle tables, bottom first, so the package higher in the list wins.
// `unit_of`, `pilot_of` (key, package id): a new unit's or character's number, as mod_ids.
using PackageNumberOf=std::function<std::optional<unsigned>(const std::string&,const std::string&)>;
inline BattleSongs collect_battle(const std::vector<mods::Package>& packages,
                                  const std::function<json(const fs::path&)>& read_json,
                                  const PackageNumberOf& unit_of={},const PackageNumberOf& pilot_of={}) {
    BattleSongs out;
    const auto names=named_songs(packages);
    for(const auto& p:packages) {
        if(!p.active)continue;
        const auto extra=battle_settings(read_json(p.folder/"audio"/"music.json"),
            [&](const std::string& key)->std::optional<unsigned>{return unit_of?unit_of(key,p.id()):std::nullopt;},
            [&](const std::string& key)->std::optional<unsigned>{return pilot_of?pilot_of(key,p.id()):std::nullopt;},
            named_in(names,p.id()));
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
    } else if(const uint32_t moved=table_window::base(table_window::unit_songs);rom>=moved && rom<moved+table_window::block && !((rom-moved)&1)) {
        // The table is read at the window since new unit numbers (table_window.hpp).
        const auto found=songs.unit.find(int((rom-moved)/2));
        if(found!=songs.unit.end())return found->second;
    } else if(const uint32_t moved=table_window::base(table_window::pilot_songs);rom>=moved && rom<moved+table_window::block && !((rom-moved)&1)) {
        // A new character's entry (its row past the ROM's 361).
        const auto found=songs.pilot.find(int((rom-moved)/2));
        if(found!=songs.pilot.end())return found->second;
    }
    return std::nullopt;
}

// Battle voices (docs/design/audio-mods.md §4.3): audio/voice/battle/<record>.ogg speaks the
// battle quote with that text record as its box opens, once, over the game's own sound.
// Records 5799-5812 are the flagship's last words, 5813-14226 the general quotes, 14227-17346
// the conditional ones (docs/guide/audio-mods/battle-voices.csv).
inline constexpr int first_quote=5799,last_quote=17346;
struct Line {
    fs::path file;          // the Ogg file, or the package file it is in
    std::string entry;      // its name in there (mods::voice_archive); empty for a file of its own
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
// The quote record a file's name says, if it is one: <record>.ogg, no leading zero.
inline std::optional<int> quote_record(const fs::path& file) {
    auto ext=file.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(ext!=".ogg")return std::nullopt;
    const auto stem=file.stem().string();
    if(stem.empty() || stem.size()>5 || stem[0]=='0' || !std::all_of(stem.begin(),stem.end(),[](unsigned char c){return std::isdigit(c);}))return std::nullopt;
    const int record=std::stoi(stem);
    if(record<first_quote || record>last_quote)return std::nullopt;
    return record;
}
// The line for each quote record from the active packages, bottom first, so the package
// higher in the list wins, whether its lines are in its package file (`archive_voices`
// lists it) or in its folder. A package with both: the folder's. A package's own battle
// quotes (mod_quotes.hpp) name their files: `named` gives the record of the line a package's
// file name speaks.
using NamedVoice=std::function<std::optional<int>(const std::string& package,const std::string& file)>;
inline std::map<int,Line> collect_voices(const std::vector<mods::Package>& packages,
                                         const std::function<json(const fs::path&)>& read_json,
                                         const std::function<std::vector<std::string>(const fs::path&)>& archive_voices,
                                         const NamedVoice& named={}) {
    const auto record_of=[&](const std::string& package,const fs::path& name)->std::optional<int> {
        if(const auto record=quote_record(name))return record;
        return named?named(package,name.filename().string()):std::nullopt;
    };
    std::map<int,Line> out;
    for(const auto& p:packages) {
        if(!p.active)continue;
        const auto extra=voice_settings(read_json(p.folder/"audio"/"voice.json"));
        const auto archive=p.folder/mods::voice_archive;
        for(const auto& entry:archive_voices(archive)) {
            auto path=entry;   // the file's own spelling; some archivers write backslashes
            std::replace(path.begin(),path.end(),'\\','/');
            const fs::path name(path);
            const auto record=record_of(p.id(),name);
            if(record && name.parent_path().string().ends_with("audio/voice/battle"))out[*record]={archive,entry,p.id(),extra.gain_db,extra.delay_ms};
        }
        const auto dir=p.folder/"audio"/"voice"/"battle";
        std::error_code error;
        if(!fs::is_directory(dir,error))continue;
        for(const auto& item:fs::directory_iterator(dir,error)) {
            if(!item.is_regular_file(error))continue;
            if(const auto record=record_of(p.id(),item.path()))out[*record]={item.path(),{},p.id(),extra.gain_db,extra.delay_ms};
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
    if(!v.pcm || v.pcm->frames()<2)return true;   // still decoding: silence, keep the place
    // The delay counts from the decode, so a slow machine still hears it.
    const size_t waited=size_t(std::min<uint64_t>(v.delay,frames));
    v.delay-=waited;
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

// A line spoken `factor` times faster at the same pitch (WSOLA: overlap-add of 25 ms
// Hann-windowed grains, each taken from the input near its nominal place where it best
// continues the grain before, within +-8 ms). Speech stays intelligible up to about 2x;
// the battle voices follow the game speed with it (music.cpp voice_speed). The two
// channels share the alignment, found on their sum. Below 1.01 the input comes back as is.
inline std::shared_ptr<Pcm> stretch(const Pcm& in,double factor) {
    auto out=std::make_shared<Pcm>();
    out->rate=in.rate;
    const uint64_t n=in.frames();
    const size_t grain=std::max<size_t>(64,size_t(in.rate/40)&~size_t(1));   // 25 ms, even
    const size_t hop=grain/2,tol=std::max<size_t>(8,in.rate/125);             // 8 ms
    if(factor<1.01 || n<4*grain){out->samples=in.samples;return out;}
    const auto& s=in.samples;
    std::vector<float> window(grain);
    constexpr double pi=3.14159265358979323846;
    for(size_t i=0;i<grain;++i)window[i]=float(0.5-0.5*std::cos(2*pi*(i+0.5)/grain));
    const uint64_t frames_out=uint64_t(double(n)/factor);
    std::vector<float> acc(2*(frames_out+grain),0.f);
    const auto mono=[&](uint64_t i){return float(s[2*i])+float(s[2*i+1]);};
    // The first grain sits at the start; each next one continues the previous (previous
    // input position + hop) as closely as the search allows around its nominal place.
    uint64_t prev=0;
    for(uint64_t k=0;k*hop<frames_out;++k) {
        const uint64_t o=k*hop;
        uint64_t pos=0;
        if(k) {
            const double nominal=double(o)*factor;
            const uint64_t lo=uint64_t(std::max(0.0,nominal-double(tol))),hi=uint64_t(std::min(double(n-grain),nominal+double(tol)));
            const uint64_t natural=prev+hop;   // where the previous grain would have continued
            double best=-1e300;pos=std::min<uint64_t>(uint64_t(nominal),n-grain);
            for(uint64_t c=lo;c<=hi;c+=2) {
                double dot=0;
                for(size_t i=0;i<grain;i+=4)dot+=double(mono(natural+i<n?natural+i:n-1))*mono(c+i);
                if(dot>best){best=dot;pos=c;}
            }
        }
        prev=pos;
        for(size_t i=0;i<grain && pos+i<n;++i) {
            acc[2*(o+i)]+=window[i]*s[2*(pos+i)];
            acc[2*(o+i)+1]+=window[i]*s[2*(pos+i)+1];
        }
    }
    out->samples.resize(2*frames_out);
    for(size_t i=0;i<out->samples.size();++i)out->samples[i]=int16_t(std::clamp(acc[i],-32768.f,32767.f));
    return out;
}

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
// The song a package chose for the entry at this ROM address of the battle song tables (the
// original's pilot or unit table, or the table window's), while MOD music is on: what the
// battle viewer plays for an attacker, as a map battle would (battle_viewer.cpp). Any thread.
std::optional<int> package_battle_song(uint32_t rom);
// The dialogue layer saw a battle quote's box open (native_dialogue.cpp): its line starts,
// and the one before fades out in 20 ms. Game thread.
void battle_quote(unsigned record);
// The player ended the animation early (battle_animation_probe.hpp): the line fades with
// the picture, 300 ms. Game thread.
void battle_aborted();
// Whether this battle quote's line is still being spoken: decoding, in its delay, or playing
// (voice_wait.hpp holds its box open meanwhile). Game thread.
bool speaking(unsigned record);
// The battle line being spoken (decoding, its delay or playing), -1 when none.
int speaking_line();
// How much faster a line is spoken now (1, 1.75 at 2x, 2 at 3x and 4x, from the settings).
double voice_speed();
// The sound test's view of a song: replaced, and the package's title in the locale.
struct Shown {bool replaced{};std::string title;};
Shown shown(int song,const std::string& locale);
// The state for the debug status.
json status();
}
