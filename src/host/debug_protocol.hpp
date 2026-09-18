#pragma once
#include "json/json.hpp"
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
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
namespace srw64::debug {

// Keys the interface can hold. graphics.cpp binds each to the SDL scancode of
// the physical key with the same name; the name page counts them by macOS key
// code while it waits for its closing keys to be released.
enum Key : unsigned {Z,X,Space,Return,Up,Down,Left,Right,Q,E,I,K,J,L,W,A,S,D,Escape,F6,F7,F8,KeyCount};
inline constexpr std::array<std::string_view,KeyCount> key_names={
    "z","x","space","return","up","down","left","right","q","e","i","k","j","l",
    "w","a","s","d","escape","f6","f7","f8"};
inline constexpr std::array<unsigned,KeyCount> mac_key_codes={
    6,7,49,36,126,125,123,124,12,14,34,40,38,37,13,0,1,2,53,97,98,100};
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
// and takes the press edges for the keys handled as events (F6/F7/F8/Esc).
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
