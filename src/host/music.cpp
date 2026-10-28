#include "music.hpp"
#include "guest_memory.hpp"
#include "presentation_settings.hpp"
#include "wide_map.hpp"
#include "funcs.h"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <list>
#include <mutex>
#include <thread>

// The Ogg decoder RT64 carries (public domain), compiled here once.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb/stb_vorbis.c"
#undef L
#undef C
#undef R

// audio.cpp: the mixer every queued buffer of the game's own audio passes through.
extern void (*srw64_audio_mixer)(int16_t* stereo,size_t frames,uint32_t rate);

namespace srw64::music {
namespace {
using guest::read;

constexpr uint32_t current_song=0x800FFA6C;   // D_800FFA6C, u16 (8007E810)
constexpr uint32_t settings_byte=0x8015DDA8;  // bit 0: mono (title オプション)
constexpr uint32_t game_mode=0x8015DA02;      // 0x1A: カラオケ, which keeps the original
constexpr unsigned karaoke_mode=0x1A;

json read_json(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)return json();
    auto data=json::parse(in,nullptr,false);
    return data.is_discarded()?json():data;
}

// The table of replacements, read again when the packages change.
std::mutex table_mutex;
unsigned table_revision{};
std::map<int,Track> table;
BattleSongs battle_table;
std::map<int,Line> voice_table;
void refresh_locked() {
    if(const unsigned now=mods::revision();now!=table_revision) {
        const auto packages=mods::installed(mods::game_version());
        table=collect(packages,read_json);
        battle_table=collect_battle(packages,read_json);
        voice_table=collect_voices(packages,read_json);
        table_revision=now;
    }
}
std::optional<Track> lookup(int song) {
    std::lock_guard lock(table_mutex);
    refresh_locked();
    const auto found=table.find(song);
    if(found==table.end())return std::nullopt;
    return found->second;
}

// Decoding: a whole track at a time on its own thread, the last two kept, so switching
// between a stage's music and a battle theme does not decode again. Voice lines keep their
// own last few, as a pilot says the same lines again and again.
struct Decoded {fs::path file;std::shared_ptr<const Pcm> pcm;std::optional<std::pair<uint64_t,uint64_t>> tag_loop;};
struct Cache {std::list<Decoded> list;size_t keep;};
std::mutex decode_mutex;
Cache songs_decoded{{},2},lines_decoded{{},16};
std::optional<Decoded> decode(const fs::path& file,Cache& cache=songs_decoded) {
    {
        std::lock_guard lock(decode_mutex);
        auto& decoded=cache.list;
        for(auto it=decoded.begin();it!=decoded.end();++it)if(it->file==file){decoded.splice(decoded.begin(),decoded,it);return decoded.front();}
    }
    std::ifstream in(file,std::ios::binary);
    if(!in)return std::nullopt;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    int error=0;
    stb_vorbis* v=stb_vorbis_open_memory(bytes.data(),int(bytes.size()),&error,nullptr);
    if(!v){std::fprintf(stderr,"SRW64_MUSIC_DECODE_FAILED %s error=%d\n",file.string().c_str(),error);return std::nullopt;}
    const auto info=stb_vorbis_get_info(v);
    // LOOPSTART / LOOPLENGTH in the file's comments, as many game music packs carry them.
    std::optional<std::pair<uint64_t,uint64_t>> tag_loop;
    {
        const auto comments=stb_vorbis_get_comment(v);
        uint64_t start=0,length=0;bool has_start=false,has_length=false;
        for(int i=0;i<comments.comment_list_length;++i) {
            std::string c=comments.comment_list[i];
            const auto eq=c.find('=');
            if(eq==std::string::npos)continue;
            std::string key=c.substr(0,eq);
            std::transform(key.begin(),key.end(),key.begin(),[](unsigned char ch){return char(std::toupper(ch));});
            try {
                if(key=="LOOPSTART"){start=std::stoull(c.substr(eq+1));has_start=true;}
                if(key=="LOOPLENGTH"){length=std::stoull(c.substr(eq+1));has_length=true;}
            } catch(...) {}
        }
        if(has_start && has_length && length>0)tag_loop=std::pair{start,start+length};
    }
    auto pcm=std::make_shared<Pcm>();
    pcm->rate=info.sample_rate;
    std::vector<short> chunk(4096*2);
    for(;;) {
        const int got=stb_vorbis_get_samples_short_interleaved(v,2,chunk.data(),int(chunk.size()));
        if(got<=0)break;
        pcm->samples.insert(pcm->samples.end(),chunk.begin(),chunk.begin()+got*2);
    }
    stb_vorbis_close(v);
    Decoded d{file,std::move(pcm),tag_loop};
    std::lock_guard lock(decode_mutex);
    cache.list.push_front(d);
    while(cache.list.size()>cache.keep)cache.list.pop_back();
    return d;
}

// The engine: one voice at most. The game thread starts, fades and stops it; the audio
// thread mixes it.
std::mutex engine_mutex;
std::optional<Voice> voice;
int voice_song=-1;
uint64_t generation{};          // a start or stop since a decode began
bool mono{};
unsigned last_rate{44100};    // the output rate the mixer last saw
std::atomic_bool in_original{};  // our own calls into the original routines

void stop_locked(){voice.reset();voice_song=-1;++generation;}
// The battle voice: one line at a time, beside the music.
std::optional<Voice> speech;
int speech_line=-1;
uint64_t speech_generation{};
void stop_speech_locked(){speech.reset();speech_line=-1;++speech_generation;}

void start(int song,const Track& track) {
    uint64_t mine;
    {
        std::lock_guard lock(engine_mutex);
        Voice v;
        v.once=track.once.value_or(song==jingle_song);
        v.gain=gain_of(track.gain_db)*settings::mod_music_volume()/100.0;
        voice=v;voice_song=song;mine=++generation;
    }
    std::thread([track,mine]{
        const auto d=decode(track.file);
        std::lock_guard lock(engine_mutex);
        if(generation!=mine || !voice)return;   // replaced or stopped meanwhile
        if(!d){voice->finished=true;return;}
        voice->pcm=d->pcm;
        const auto loop=track.loop?track.loop:d->tag_loop;
        if(loop){voice->loop_start=loop->first;voice->loop_end=loop->second;}
    }).detach();
    std::fprintf(stderr,"SRW64_MUSIC_REPLACE song=%d file=%s package=%s\n",song,track.file.string().c_str(),track.package.c_str());
}

void mixer(int16_t* stereo,size_t frames,uint32_t rate) {
    std::lock_guard lock(engine_mutex);
    if(rate)last_rate=rate;
    // A line ends with its battle: the animation steps every frame (wide_map::mark_battle),
    // so 200 ms without one is its end or the player's abort.
    if(speech && (!wide_map::battle_shown() || !mix(*speech,stereo,frames,rate,mono)))stop_speech_locked();
    if(!voice)return;
    if(!mix(*voice,stereo,frames,rate,mono) && voice->fading())stop_locked();
}
}

void configure(){srw64_audio_mixer=mixer;}

bool play(uint8_t* ram,recomp_context* ctx) {
    const int song=int16_t(ctx->r4);
    {
        std::lock_guard lock(engine_mutex);
        mono=read(ram,settings_byte,1)&1;
    }
    // The original does nothing for the song already playing; neither does the host.
    if(song>=0 && song==int(int16_t(read(ram,current_song,2))))return false;
    std::optional<Track> track;
    if(song>0 && settings::mod_music() && read(ram,game_mode,1)!=karaoke_mode)track=lookup(song);
    if(!track) {
        std::lock_guard lock(engine_mutex);
        stop_locked();
        return false;
    }
    // Stop the original's music, keep its idea of the current song, play the file.
    in_original=true;
    auto c=*ctx;c.r4=-1;
    srw64_original_play_song(ram,&c);
    in_original=false;
    guest::write16(ram,current_song,uint16_t(song));
    start(song,*track);
    return true;
}

void fade(uint8_t* ram,recomp_context* ctx) {
    (void)ram;
    const int frames=int16_t(ctx->r4);
    std::lock_guard lock(engine_mutex);
    if(!voice)return;
    // The original counts its fade in frames of the game's 60 Hz clock.
    const uint64_t out=uint64_t(std::max(frames,1))*last_rate/60;
    voice->fade_total=voice->fade_left=out;
}

bool playing(uint8_t* ram,recomp_context* ctx) {
    (void)ram;
    if(in_original)return false;
    std::lock_guard lock(engine_mutex);
    if(!voice)return false;
    ctx->r2=voice->finished?0:1;
    return true;
}

bool battle(uint8_t* ram,recomp_context* ctx) {
    if(ctx->r5&0xFF || !settings::mod_music())return false;   // a super mode's fixed song stays the original's
    std::optional<int> song;
    {
        std::lock_guard lock(table_mutex);
        refresh_locked();
        song=battle_song(battle_table,uint32_t(ctx->r4));
    }
    if(!song)return false;
    // Through the play hook, so a replacement for the chosen song plays too.
    auto c=*ctx;c.r4=*song;
    resident_func_8007E810(ram,&c);
    return true;
}

void battle_quote(unsigned record) {
    if(!settings::mod_voice())return;
    std::optional<Line> line;
    {
        std::lock_guard lock(table_mutex);
        refresh_locked();
        if(const auto found=voice_table.find(int(record));found!=voice_table.end())line=found->second;
    }
    uint64_t mine;
    {
        std::lock_guard lock(engine_mutex);
        stop_speech_locked();
        if(!line)return;
        Voice v;
        v.once=true;
        v.gain=gain_of(line->gain_db)*settings::mod_voice_volume()/100.0;
        v.delay=uint64_t(line->delay_ms)*last_rate/1000;
        speech=v;speech_line=int(record);mine=speech_generation;
    }
    std::thread([file=line->file,mine]{
        const auto d=decode(file,lines_decoded);
        std::lock_guard lock(engine_mutex);
        if(speech_generation!=mine || !speech)return;   // the next line came meanwhile
        if(!d){speech->finished=true;return;}
        speech->pcm=d->pcm;
    }).detach();
    std::fprintf(stderr,"SRW64_VOICE line=%u file=%s package=%s\n",record,line->file.string().c_str(),line->package.c_str());
}

Shown shown(int song,const std::string& locale) {
    if(!settings::mod_music())return {};
    const auto track=lookup(song);
    if(!track)return {};
    return {true,campaign_library::text(track->title,locale)};
}

json status() {
    std::lock_guard lock(engine_mutex);
    json out=json::object();
    if(!voice)out={{"replaced",false}};
    else out={{"replaced",true},{"song",voice_song},{"decoded",bool(voice->pcm)},{"finished",voice->finished},
              {"position",voice->position},{"loop",{voice->loop_start,voice->loop_end}},{"once",voice->once}};
    if(speech)out["voice"]={{"line",speech_line},{"decoded",bool(speech->pcm)},{"position",speech->position},{"waiting",speech->delay}};
    return out;
}
}
