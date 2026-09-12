// Exercise the actual adapted runtime timer with ASan/UBSan. The external
// message sink is isolated so a timer cannot require a running guest kernel.
#include "timer.cpp"
#include <atomic>
#include <cassert>
#include <iostream>
#include <vector>

std::atomic<unsigned> delivered{};
void ultramodern::set_native_thread_name(const std::string&) {}
void ultramodern::set_native_thread_priority(ultramodern::ThreadPriority) {}
void ultramodern::enqueue_external_message_src(int32_t, OSMesg, bool, ultramodern::EventMessageSource) { ++delivered; }

int main() {
    for(unsigned iteration=0;iteration<30;++iteration) {
        std::vector<uint8_t> ram(8192);
        ultramodern::init_timers(ram.data());
        if(iteration%3==1) // Interrupt a long timed wait.
            osSetTimer(ram.data(),int32_t(0x80001000),46'875ULL*60'000,0,0,0);
        if(iteration%3==2) // Stop an actively repeating timer.
            osSetTimer(ram.data(),int32_t(0x80001000),46'875,46'875,0,0);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        const auto start=std::chrono::steady_clock::now();
        srw64_shutdown_timers();
        srw64_shutdown_timers(); // Also safe if the worker was already joined.
        assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
        assert(!timer_context.thread.joinable());
        const auto count=delivered.load();
        ram.clear();ram.shrink_to_fit();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        assert(delivered==count);
    }
    assert(delivered>0);
    std::cout<<"native timer shutdown: empty, long-wait, repeating, repeated join and RDRAM lifetime passed\n";
}
