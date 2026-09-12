#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>

namespace srw64::presentation {
// The guest may finish both display-list buffers before the renderer consumes
// the first. Consuming A must leave the already-published B snapshot intact.
// The owner supplies synchronization with guest publication/render submission.
template<class Frame> class DisplayListSnapshots {
public:
    struct Drawing {
        uint32_t begin, end;
        std::shared_ptr<const Frame> frame;
    };
    void publish(uint32_t begin,uint32_t end,std::shared_ptr<const Frame> frame) {
        pending.push_back({begin,end,std::move(frame)});
        while(pending.size()>8)pending.pop_front();
    }
    std::optional<Drawing> take(uint32_t start,uint32_t size) {
        for(auto it=pending.rbegin();it!=pending.rend();++it) {
            if(it->begin<start || uint64_t(it->end)>uint64_t(start)+size)continue;
            auto result=*it;
            // erase only this drawing, never other in-flight display lists.
            pending.erase(std::next(it).base());
            return result;
        }
        return {};
    }
    void clear() {pending.clear();}
private:
    std::deque<Drawing> pending;
};
}
