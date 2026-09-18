#pragma once
#include <atomic>
#include <cstdint>

namespace srw64 {
class ModalInputRelease {
    std::atomic_bool waiting{};
public:
    void hold(){waiting=true;}
    bool pending() const{return waiting.load();}
    uint32_t filter(uint32_t input,bool modal_open,bool physical_keys_held) {
        if(modal_open){waiting=true;return 0;}
        if(waiting) {
            if(input==0 && !physical_keys_held)waiting=false;
            return 0;
        }
        return input;
    }
};
}
