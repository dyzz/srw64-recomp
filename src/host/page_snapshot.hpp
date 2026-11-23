#pragma once
// What the window thread reads of a native intermission page (intermission_page, upgrade_page,
// parts_page, ability_page, swap_page, save_page): the page's state as of the last time its
// lock was free, shared rather than copied, with a revision that moves only when it changed.
// Each UI frame used to copy the state twice and print it whole to compare it with the last
// (frontend.cpp): 8-21 ms a frame on a Switch while nothing changed, the hidden pages too
// (SRW64_SYNC_SLOW, 2026-11-23).
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include "json/json.hpp"

namespace srw64 {
class PageSnapshot {
public:
    using Shared = std::shared_ptr<const nlohmann::json>;
    // With the page's lock held: `current` kept when it differs from what was kept. A page
    // hidden and still hidden is not compared (nothing reads more than "visible" then).
    void take(const nlohmann::json& current) {
        std::lock_guard lock(mutex_);
        const bool visible = current.value("visible", false), was = shared_->value("visible", false);
        if (!visible && !was) return;
        if (visible == was && *shared_ == current) return;
        shared_ = std::make_shared<const nlohmann::json>(current);
        ++revision_;
    }
    std::pair<Shared, uint64_t> get() const {
        std::lock_guard lock(mutex_);
        return {shared_, revision_};
    }

private:
    mutable std::mutex mutex_;
    Shared shared_ = std::make_shared<const nlohmann::json>(nlohmann::json{{"visible", false}});
    uint64_t revision_ = 0;
};
}
