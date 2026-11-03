#pragma once
#include "json/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#if defined(__SWITCH__) || defined(__vita__)
#define SRW64_NO_PIPES 1   // newlib: no popen/pclose, and no ffmpeg to run anyway
#elif !defined(_WIN32)
#include <sys/wait.h>
#endif
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Shared state of the debug interface (docs/guide/debug-interface.md) that the window
// and render threads read without depending on the socket server, so the frame
// probes can link graphics.cpp without it. Everything here is inert until the
// server (debug_server.cpp, SRW64_DEBUG=1) writes to it.
uint64_t srw64_current_vi();   // host.cpp
namespace srw64::debug {

// Keys the interface can hold. graphics.cpp binds each to the SDL scancode of
// the physical key with the same name; the name page counts them by macOS key
// code while it waits for its closing keys to be released.
enum Key : unsigned {Z,X,Space,Return,Up,Down,Left,Right,Q,E,I,K,J,L,W,A,S,D,Escape,F6,F7,F8,F5,F12,KeyCount};
inline constexpr std::array<std::string_view,KeyCount> key_names={
    "z","x","space","return","up","down","left","right","q","e","i","k","j","l",
    "w","a","s","d","escape","f6","f7","f8","f5","f12"};
inline constexpr std::array<unsigned,KeyCount> mac_key_codes={
    6,7,49,36,126,125,123,124,12,14,34,40,38,37,13,0,1,2,53,97,98,100,96,111};
inline constexpr uint32_t bit(Key key){return 1u<<key;}

// "e+return" -> the mask of both keys. Case-insensitive; "enter" and "esc" are
// accepted aliases. Unknown names are an error rather than a silent no-op.
inline uint32_t parse_keys(std::string_view text) {
    uint32_t mask=0;
    size_t start=0;
    while(start<=text.size()) {
        const size_t plus=text.find('+',start);
        std::string name(text.substr(start,plus==std::string_view::npos?std::string_view::npos:plus-start));
        for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
        if(name=="enter")name="return";
        if(name=="esc")name="escape";
        unsigned index=0;
        while(index<KeyCount && key_names[index]!=name)++index;
        if(index==KeyCount)throw std::invalid_argument("unknown key '"+name+"'");
        mask|=bit(Key(index));
        if(plus==std::string_view::npos)break;
        start=plus+1;
    }
    return mask;
}
inline nlohmann::json key_list(uint32_t mask) {
    auto names=nlohmann::json::array();
    for(unsigned k=0;k<KeyCount;++k)if(mask&bit(Key(k)))names.push_back(key_names[k]);
    return names;
}

// Keys held through the interface. The window thread reads held() every frame
// and takes the press edges for the keys handled as events (F5/F6/F7/F8/F12/Esc).
class VirtualKeyboard {
public:
    void down(uint32_t mask) {
        std::lock_guard lock(mutex);
        const uint32_t fresh=mask&~held_.load();
        for(unsigned k=0;k<KeyCount;++k)if(fresh&bit(Key(k)))presses.push_back(Key(k));
        held_|=mask;
    }
    void up(uint32_t mask){held_&=~mask;}
    void release_all(){held_=0;}
    uint32_t held() const {return held_.load();}
    bool holds(Key key) const {return held_.load()&bit(key);}
    bool holds_mac_key(unsigned code) const {
        for(unsigned k=0;k<KeyCount;++k)if(mac_key_codes[k]==code && holds(Key(k)))return true;
        return false;
    }
    std::vector<Key> take_presses() {
        std::lock_guard lock(mutex);
        std::vector<Key> taken(presses.begin(),presses.end());
        presses.clear();
        return taken;
    }
private:
    std::atomic<uint32_t> held_{};
    std::mutex mutex;
    std::deque<Key> presses;
};
inline VirtualKeyboard& keyboard(){static VirtualKeyboard value;return value;}
// Controller buttons held through the interface, in srw64_pad_state()'s mask (N64 bits,
// stick bits 16-19, View / L2 / R2 from input_mode.hpp). The window thread ORs them into
// the controller's every frame, so pages, hints and host features see a real pad.
inline std::atomic<uint32_t>& pad(){static std::atomic<uint32_t> held{};return held;}

// On-demand screenshot of the next present. The requester waits for the render
// thread's GPU readback to finish and gets the file's metadata back.
class Screenshots {
public:
    struct Pending {uint64_t id;std::filesystem::path path;};
    // Debug thread. nullopt when no present completed within the timeout.
    std::optional<nlohmann::json> capture(const std::filesystem::path& path,std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);
        const uint64_t id=++requested;
        pending=Pending{id,path};
        if(!done.wait_for(lock,timeout,[&]{return completed>=id;})) {
            if(pending && pending->id==id)pending.reset();
            return std::nullopt;
        }
        return result;
    }
    // Whether a capture is waiting (the PS Vita's and the Switch's screen keeper draw then).
    bool wanted() { std::lock_guard lock(mutex); return pending.has_value(); }
    // Render thread, once per present: the capture wanted now, if any.
    std::optional<Pending> take() {
        std::lock_guard lock(mutex);
        auto value=pending;pending.reset();
        return value;
    }
    // GPU completion handler.
    void finish(uint64_t id,nlohmann::json value) {
        {
            std::lock_guard lock(mutex);
            if(id<=completed)return;
            completed=id;result=std::move(value);
        }
        done.notify_all();
    }
private:
    std::mutex mutex;
    std::condition_variable done;
    std::optional<Pending> pending;
    uint64_t requested{},completed{};
    nlohmann::json result;
};
inline Screenshots& screenshots(){static Screenshots value;return value;}

// A recording of every present, for a video of a stretch of play (debug methods record.start
// and record.stop). Each present is copied off the GPU completion handler and queued; a worker
// scales it to `width` (2 x 2 averaged samples) and puts it on a steady `fps` timeline by the
// VI it was presented at (not the wall clock: a frame two VIs after the last is the next tick
// whatever the host's own stalls did), holding the last picture over ticks without a present.
// With an ffmpeg (record.start's `ffmpeg`, SRW64_FFMPEG, or one on the PATH) the frames go
// straight into it and record.stop muxes the game's sound in: one MP4 on the host, nothing
// raw to pull over a connection. Without one the raw RGB frames, their times ("seconds vi"
// a line) and the sound (stereo s16le, as sent to the output device) are written as before
// for session.py to encode. In lockstep (the default while encoding) the VI thread waits
// whenever the queue is full, so a slow encoder slows the game rather than dropping frames.
class Recording {
public:
    struct Options {
        uint32_t width=960, fps=30;
        std::string ffmpeg;       // "" : SRW64_FFMPEG, else "ffmpeg"
        bool encode=true, lockstep=true;
        unsigned queue=12;        // frames waiting for the worker before the VI thread waits
        // Gated: nothing is taken until open_gate() (game thread, at the first frame wanted),
        // and nothing after close_gate(); trim_black drops black frames at both ends of the
        // picture (the loading and the fade of a battle animation), so a clip is the
        // animation alone (docs/design/video-capture.md).
        bool gated=false, trim_black=false;
    };
    // Debug thread. False when one is already running or the files cannot be opened.
    bool start(const std::filesystem::path& directory,Options options) {
        std::lock_guard lock(mutex);
        if(on)return false;
        std::filesystem::create_directories(directory);
        dir=directory;opts=options;
        if(opts.ffmpeg.empty()){const char* env=std::getenv("SRW64_FFMPEG");opts.ffmpeg=env && *env?env:"ffmpeg";}
        opts.fps=std::clamp<uint32_t>(opts.fps,1,120);opts.queue=std::clamp<unsigned>(opts.queue,2,64);
        // No encoder at hand: the raw files as before, rather than a recording lost at stop.
        encoder_missing=opts.encode && !encoder_runs(opts.ffmpeg);
        if(encoder_missing){opts.encode=false;opts.lockstep=false;}
        frames_path=directory/"frames.rgb";times_path=directory/"frames.times";
        audio_path=directory/"audio.s16";video_path=directory/"video.mp4";final_path=directory/"recording.mp4";
        audio_file.open(audio_path,std::ios::binary|std::ios::trunc);
        if(!audio_file)return false;
        if(!opts.encode) {
            frames.open(frames_path,std::ios::binary|std::ios::trunc);times.open(times_path,std::ios::trunc);
            if(!frames || !times)return false;
        }
        out_width=std::max<uint32_t>(64,opts.width&~1u);out_height=0;
        count=written=held=same_tick=dropped=resized=0;first_vi=0;last_tick=-1;
        audio_frames=0;audio_rate=0;audio_start=-1;audio_start_vi=0;
        encoder=nullptr;encode_error.clear();
        began=std::chrono::steady_clock::now();
        gate=!opts.gated;gate_opened_vi=gate_closed_vi=0;trimmed_lead=trimmed_tail=0;
        queue.clear();stopping=false;on=true;
        worker=std::thread([this]{run();});
        return true;
    }
    // Debug thread: stops it, finishes the file and says what was written.
    nlohmann::json stop() {
        {
            std::lock_guard lock(mutex);
            if(!on)return {{"error","not recording"}};
            on=false;stopping=true;
        }
        ready.notify_all();
        if(worker.joinable())worker.join();
        std::lock_guard lock(mutex);
        audio_file.close();
        if(!opts.encode){frames.close();times.close();}
        nlohmann::json result={{"presents",count},{"frames",opts.encode?written:count},{"held",held},{"same_tick",same_tick},
                {"dropped",dropped},{"resized",resized},{"width",out_width},{"height",out_height},{"fps",opts.fps},{"first_vi",first_vi},
                {"gated",opts.gated},{"gate_opened_vi",gate_opened_vi.load()},{"gate_closed_vi",gate_closed_vi.load()},{"trimmed_lead",trimmed_lead},{"trimmed_tail",trimmed_tail},
                {"seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()},
                {"audio_path",audio_path.string()},{"audio_frames",audio_frames},{"audio_rate",audio_rate},
                {"audio_start",audio_start},{"audio_start_vi",audio_start_vi},{"encoded",false},
                {"encoder",opts.encode?opts.ffmpeg:encoder_missing?"missing: "+opts.ffmpeg:"off"}};
        if(!opts.encode) {
            result["frames_path"]=frames_path.string();result["times_path"]=times_path.string();
            return result;
        }
        if(encoder){const int code=pclose_(encoder);encoder=nullptr;if(code!=0 && encode_error.empty())encode_error="ffmpeg exited with "+std::to_string(code);}
        if(encode_error.empty() && written)encode_error=mux();
        if(!encode_error.empty()){result["error"]=encode_error;return result;}
        if(!written){result["error"]="no frames were presented while recording";return result;}
        result["encoded"]=true;result["mp4"]=final_path.string();
        std::error_code ignored;std::filesystem::remove(audio_path,ignored);std::filesystem::remove(video_path,ignored);
        return result;
    }
    // Audio thread: one block of interleaved stereo samples as queued to the device. A change
    // of rate mid-recording ends the track there (the game sets it once at start-up).
    void audio(const int16_t* samples,size_t count,uint32_t rate) {
        if(!gate.load())return;
        std::lock_guard lock(mutex);
        if(!on || !count)return;
        if(audio_rate && audio_rate!=rate)return;
        if(!audio_rate) {
            audio_rate=rate;
            audio_start=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
            audio_start_vi=srw64_current_vi();
        }
        audio_file.write(reinterpret_cast<const char*>(samples),std::streamsize(count*sizeof(int16_t)));
        audio_frames+=count/2;
    }
    // Render thread, once per present: whether to read this one back.
    bool active() const {return on.load() && gate.load();}
    // Game thread, for a gated recording: the frames from here on are taken / none after this.
    void open_gate(){if(on.load() && !gate.exchange(true))gate_opened_vi=srw64_current_vi();}
    void close_gate(){if(on.load() && gate.exchange(false))gate_closed_vi=srw64_current_vi();}
    static bool encoder_available(const std::string& ffmpeg){return encoder_runs(ffmpeg);}
    nlohmann::json gate_state() const {return {{"on",on.load()},{"gate",gate.load()},{"opened_vi",gate_opened_vi.load()},{"closed_vi",gate_closed_vi.load()}};}
    // VI thread, each retrace: in lockstep, a full queue holds the game's clock.
    bool behind() {
        if(!on.load() || !opts.lockstep)return false;
        std::lock_guard lock(mutex);
        return queue.size()>=opts.queue;
    }
    // GPU completion handler: one present, RGBA w x h, copied and queued for the worker.
    void add(const uint8_t* rgba,uint32_t w,uint32_t h) {
        if(!on.load() || !gate.load() || !w || !h)return;
        Frame frame;
        frame.vi=srw64_current_vi();frame.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        frame.w=w;frame.h=h;frame.rgba.assign(rgba,rgba+size_t(w)*h*4);
        {
            std::lock_guard lock(mutex);
            if(!on)return;
            ++count;
            if(queue.size()>=opts.queue*2){++dropped;return;}   // the worker is far behind even in lockstep
            queue.push_back(std::move(frame));
        }
        ready.notify_one();
    }
private:
    struct Frame {std::vector<uint8_t> rgba;uint32_t w{},h{};uint64_t vi{};double seconds{};};
    static FILE* popen_(const std::string& command) {
#if SRW64_NO_PIPES
        (void)command;return nullptr;   // the worker reports "cannot start ffmpeg"
#elif defined(_WIN32)
        return _popen(("\""+command+"\"").c_str(),"wb");   // cmd /c keeps the inner quotes when the whole line is quoted
#else
        return popen(command.c_str(),"w");
#endif
    }
    static int pclose_(FILE* pipe) {
#if SRW64_NO_PIPES
        (void)pipe;return -1;
#elif defined(_WIN32)
        return _pclose(pipe);
#else
        const int status=pclose(pipe);
        return status==-1?-1:WIFEXITED(status)?WEXITSTATUS(status):status;
#endif
    }
    // Whether `ffmpeg -version` runs (a path or a name on the PATH).
    static bool encoder_runs(const std::string& ffmpeg) {
#if SRW64_NO_PIPES
        (void)ffmpeg;return false;
#elif defined(_WIN32)
        return std::system(("\""+quoted(ffmpeg)+" -version >NUL 2>&1\"").c_str())==0;
#else
        return std::system((quoted(ffmpeg)+" -version >/dev/null 2>&1").c_str())==0;
#endif
    }
    static std::string quoted(const std::string& text) {
#ifdef _WIN32
        return "\""+text+"\"";
#else
        std::string out="'";for(char c:text){if(c=='\'')out+="'\\''";else out+=c;}return out+"'";
#endif
    }
    // Worker: scales, places on the timeline, writes to the encoder or the raw files.
    void run() {
        std::vector<uint8_t> scaled;
        last.clear();black_tail.clear();
        for(;;) {
            Frame frame;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock,[&]{return !queue.empty() || stopping;});
                if(queue.empty()){trimmed_tail=black_tail.size();black_tail.clear();return;}
                frame=std::move(queue.front());queue.pop_front();
            }
            const uint32_t ow=std::min(out_width,frame.w&~1u);
            const uint32_t oh=std::max<uint32_t>(2,uint32_t(uint64_t(frame.h)*ow/frame.w)&~1u);
            // Sizes, counters and the encoder are the worker's alone until stop() has joined it.
            if(out_height && (ow!=out_width || oh!=out_height)){++resized;continue;}   // the window changed size: the rest is dropped
            scaled.resize(size_t(ow)*oh*3);
            for(uint32_t y=0;y<oh;++y) {
                const uint32_t sy=uint32_t(uint64_t(y)*frame.h/oh),sy2=std::min(frame.h-1,sy+1);
                uint8_t* row=scaled.data()+size_t(y)*ow*3;
                for(uint32_t x=0;x<ow;++x) {
                    const uint32_t sx=uint32_t(uint64_t(x)*frame.w/ow),sx2=std::min(frame.w-1,sx+1);
                    const uint8_t* a=frame.rgba.data()+(uint64_t(sy)*frame.w+sx)*4;const uint8_t* b=frame.rgba.data()+(uint64_t(sy)*frame.w+sx2)*4;
                    const uint8_t* c=frame.rgba.data()+(uint64_t(sy2)*frame.w+sx)*4;const uint8_t* d=frame.rgba.data()+(uint64_t(sy2)*frame.w+sx2)*4;
                    for(unsigned k=0;k<3;++k)row[x*3+k]=uint8_t((unsigned(a[k])+b[k]+c[k]+d[k]+2)/4);
                }
            }
            if(opts.trim_black) {
                // Black at the head is not a frame at all; black at the tail waits for a
                // picture to follow and is dropped at stop() when none does.
                if(is_black(scaled)) {
                    if(!out_height){++trimmed_lead;continue;}
                    if(opts.encode){black_tail.push_back({{},ow,oh,frame.vi,frame.seconds});black_tail.back().rgba.swap(scaled);scaled.clear();continue;}
                } else if(!black_tail.empty()) {
                    for(auto& b:black_tail){std::vector<uint8_t> rgb;rgb.swap(b.rgba);emit(rgb,b.vi);}
                    black_tail.clear();
                }
            }
            if(!out_height){out_width=ow;out_height=oh;first_vi=frame.vi;}
            if(!opts.encode) {
                frames.write(reinterpret_cast<const char*>(scaled.data()),std::streamsize(scaled.size()));
                times<<frame.seconds<<' '<<frame.vi<<'\n';
                continue;
            }
            if(!encode_error.empty())continue;
            if(!encoder) {
                const std::string command=quoted(opts.ffmpeg)+" -loglevel error -y -f rawvideo -pix_fmt rgb24 -s "+std::to_string(ow)+"x"+std::to_string(oh)+
                    " -r "+std::to_string(opts.fps)+" -i - -an -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p "+quoted(video_path.string());
                encoder=popen_(command);
                if(!encoder){encode_error="cannot start "+opts.ffmpeg;continue;}
            }
            emit(scaled,frame.vi);
        }
    }
    // The timeline: ticks of 1/fps from the first kept present's VI (60 Hz units).
    void emit(const std::vector<uint8_t>& rgb,uint64_t vi) {
        const int64_t tick=int64_t((vi-first_vi)*uint64_t(opts.fps)/60);
        if(tick==last_tick){++same_tick;return;}
        for(int64_t t=last_tick+1;t<tick && !last.empty();++t){write_frame(last);++held;}
        write_frame(rgb);last=rgb;last_tick=tick;++written;
    }
    // Whether an RGB picture is black for the trim: of every 7th pixel, fewer than 2% have a
    // channel above 16. A fade's last frames keep a few bright pixels (a corner button of the
    // host's own UI, the HUD's last remains), and the title menu's text over the demo's first
    // frame is about 1%: all of it is no picture.
    static bool is_black(const std::vector<uint8_t>& rgb) {
        size_t bright=0,total=0;
        for(size_t i=0;i+2<rgb.size();i+=21){++total;if(rgb[i]>16 || rgb[i+1]>16 || rgb[i+2]>16)++bright;}
        return total==0 || bright*50<total;
    }
    void write_frame(const std::vector<uint8_t>& rgb) {
        if(!encoder)return;
        if(std::fwrite(rgb.data(),1,rgb.size(),encoder)!=rgb.size())encode_error="ffmpeg stopped taking frames";
    }
    // After the video: the sound, offset to the first frame's VI, as an AAC track.
    std::string mux() {
        if(!audio_frames || !audio_rate) {
            std::error_code error;std::filesystem::rename(video_path,final_path,error);
            return error?"cannot name the video: "+error.message():"";
        }
        const double offset=(double(audio_start_vi)-double(first_vi))/60.0;
        char number[32];std::snprintf(number,sizeof number,"%.4f",std::fabs(offset));
        const std::string lead=offset>=0?std::string(" -itsoffset ")+number:std::string(" -ss ")+number;
        const std::string command=quoted(opts.ffmpeg)+" -loglevel error -y -i "+quoted(video_path.string())+lead+" -f s16le -ar "+std::to_string(audio_rate)+
            " -ac 2 -i "+quoted(audio_path.string())+" -map 0:v -map 1:a -c:v copy -c:a aac -b:a 192k -shortest "+quoted(final_path.string());
#if SRW64_NO_PIPES
        (void)command;const int code=-1;
#elif defined(_WIN32)
        const int code=std::system(("\""+command+"\"").c_str());
#else
        const int code=std::system(command.c_str());
#endif
        return code==0?"":"ffmpeg could not mux the sound (exit "+std::to_string(code)+")";
    }
    std::mutex mutex;
    std::condition_variable ready;
    std::thread worker;
    std::deque<Frame> queue;
    std::atomic_bool on{false},gate{true};
    std::atomic<uint64_t> gate_opened_vi{0},gate_closed_vi{0};
    uint64_t trimmed_lead=0,trimmed_tail=0;
    std::vector<uint8_t> last;
    std::vector<Frame> black_tail;
    bool stopping{};
    Options opts;
    std::filesystem::path dir,frames_path,times_path,audio_path,video_path,final_path;
    std::ofstream frames,times,audio_file;
    FILE* encoder{};
    bool encoder_missing{};
    std::string encode_error;
    uint32_t out_width=960,out_height=0,audio_rate=0;
    uint64_t count=0,written=0,held=0,same_tick=0,dropped=0,resized=0,audio_frames=0,first_vi=0,audio_start_vi=0;
    int64_t last_tick=-1;
    double audio_start=-1;
    std::chrono::steady_clock::time_point began;
};
inline Recording& recording(){static Recording value;return value;}

// JSON-RPC 2.0, one request or response per line.
enum ErrorCode : int {ParseError=-32700,InvalidRequest=-32600,MethodNotFound=-32601,InvalidParams=-32602,ServerError=-32000};
struct RpcError : std::runtime_error {
    int code;
    RpcError(int value,const std::string& message):std::runtime_error(message),code(value){}
};
struct Call {nlohmann::json id;std::string method;nlohmann::json params;};
inline Call parse_call(std::string_view line) {
    const auto request=nlohmann::json::parse(line,nullptr,false);
    if(request.is_discarded())throw RpcError(ParseError,"request is not JSON");
    if(!request.is_object() || request.value("jsonrpc","")!="2.0" || !request.contains("method") || !request["method"].is_string())
        throw RpcError(InvalidRequest,"expected a JSON-RPC 2.0 request object");
    Call call{request.contains("id")?request["id"]:nlohmann::json(nullptr),request["method"].get<std::string>(),
              request.value("params",nlohmann::json::object())};
    if(!call.params.is_object())throw RpcError(InvalidParams,"params must be an object");
    return call;
}
inline std::string reply(const nlohmann::json& id,const nlohmann::json& result) {
    return nlohmann::json({{"jsonrpc","2.0"},{"id",id},{"result",result}}).dump()+"\n";
}
inline std::string failure(const nlohmann::json& id,int code,const std::string& message) {
    return nlohmann::json({{"jsonrpc","2.0"},{"id",id},{"error",{{"code",code},{"message",message}}}}).dump()+"\n";
}
}
