#pragma once
#include <atomic>
#include <cstdint>

namespace srw64::presentation {
// SDL publishes intent. Only the renderer applies it at a drained workload
// boundary; no UI callback touches the cache, RDRAM, or in-flight resources.
// The applied mode also selects original versus native 5600 model commands.
class ImageMode {
    std::atomic_bool requested_hd{false};
    std::atomic_bool available{false};
    std::atomic_int applied{-1};
public:
    void configure(bool hd) {requested_hd=hd;available=true;}
    void original_only() {requested_hd=false;applied=0;available=false;}
    bool enabled() const {return available.load();}
    bool requested() const {return requested_hd.load();}
    int current() const {return applied.load();}
    void request(bool hd) {if(enabled())requested_hd=hd;}
    void toggle() {
        if(!enabled())return;
        bool old=requested_hd.load();
        while(!requested_hd.compare_exchange_weak(old,!old)){}
    }
    void acknowledge(bool hd) {applied=hd?1:0;}
};
inline ImageMode image_mode;
}
