// Game speed: which scenes it hurries and how often the logic steps at each speed
// (src/host/game_speed.hpp, docs/native/game-speed.md).
#include "../src/host/game_speed.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

uint64_t vi_now = 100;
uint64_t srw64_current_vi() { return vi_now; }

namespace {
using namespace srw64;
int checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) throw std::runtime_error(what);
}

struct Ram {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x800000);
    uint8_t* data() { return bytes.data(); }
    // The tactical map in the player's phase with no event running, the idle cursor up.
    void tactical(unsigned state, unsigned phase = 1) {
        guest::write8(data(), game_speed::mode, 3);
        guest::write16(data(), game_speed::engine + 0x97C, 0x80);
        guest::write8(data(), game_speed::phase, uint8_t(phase));
        guest::write8(data(), game_speed::map_state, uint8_t(state));
    }
};

void scenes() {
    Ram ram;
    for (const unsigned mode : {2u, 12u, 13u}) {
        guest::write8(ram.data(), game_speed::mode, uint8_t(mode));
        check(game_speed::hurried(ram.data(), false), "the battle animation or the world map runs at 1x");
    }
    for (const unsigned mode : {1u, 4u, 5u, 6u, 7u, 0x12u, 0x1Au, 0x1Cu, 0x20u, 0x21u}) {
        guest::write8(ram.data(), game_speed::mode, uint8_t(mode));
        check(!game_speed::hurried(ram.data(), false), "a menu screen, the intermission, カラオケ or the title demo is hurried");
    }
    for (const unsigned state : {5u, 6u, 8u, 0xCu, 0xDu, 0x16u, 0x17u, 0x19u, 0x1Bu, 0x22u, 0x30u, 0x3Au, 0x3Cu}) {
        ram.tactical(state);
        check(!game_speed::hurried(ram.data(), false), "a player's menu or the cursor is hurried");
        ram.tactical(state, 2);
        check(game_speed::hurried(ram.data(), false), "the enemy phase runs at 1x");
    }
    ram.tactical(0x39);
    check(game_speed::hurried(ram.data(), false), "the phase change runs at 1x");
    // An event script: hurried, but not its choices or the sortie list.
    ram.tactical(5);
    guest::write16(ram.data(), game_speed::engine + 0x97C, 0x2000);
    guest::write32(ram.data(), game_speed::vm + 0x30, script_skip::handler::move);
    check(game_speed::hurried(ram.data(), false), "a script runs at 1x");
    guest::write32(ram.data(), game_speed::vm + 0x30, script_skip::handler::sortie);
    check(!game_speed::hurried(ram.data(), false), "the sortie list is hurried");
    guest::write32(ram.data(), game_speed::vm + 0x30, script_skip::handler::choice);
    check(!game_speed::hurried(ram.data(), false), "a choice is hurried");
    // The pre-battle screens, the enemy's attack too.
    ram.tactical(0x40, 2);
    check(!game_speed::hurried(ram.data(), true), "the pre-battle screen is hurried");
}

// At 120 Hz (the default, sched_rate.hpp): the original speed is every 4th retrace.
void interval() {
    Ram ram;
    game_speed::level = 2;
    guest::write8(ram.data(), game_speed::mode, 2);
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 0, "written before the game set it");
    guest::write32(ram.data(), game_speed::interval, 2);
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 2 && game_speed::running, "not hurried in battle");
    game_speed::menu_step();
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 4, "hurried right after a pre-battle step");
    vi_now += 4;
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 2, "the pre-battle screen's hold outlasts it");
    game_speed::level = 4;
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 1, "4x is not every retrace");
    game_speed::level = 1;
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 4 && !game_speed::running, "off still hurries");
}

// Logic steps in 120 retraces (one second) at each speed, the gate as 8008163C runs it:
// the scheduler raises the count after the frame boundary, a step clears it.
unsigned steps_per_second(unsigned speed) {
    Ram ram;
    game_speed::level = speed;
    guest::write8(ram.data(), game_speed::mode, 2);
    guest::write32(ram.data(), game_speed::interval, 4);
    unsigned steps = 0, gaps[4] = {};
    uint64_t last = 0;
    for (uint64_t tick = 1; tick <= 120 + 12; ++tick) {
        game_speed::frame(ram.data());
        const uint32_t count = guest::read(ram.data(), sched_rate::logic_count, 4) + 1;
        guest::write32(ram.data(), sched_rate::logic_count, count);
        if (count < guest::read(ram.data(), game_speed::interval, 4)) continue;
        guest::write32(ram.data(), sched_rate::logic_count, 0);
        if (tick <= 12) { last = tick; continue; }   // settle into the speed first
        ++steps;
        if (last && tick - last <= 4) ++gaps[tick - last - 1];
        last = tick;
    }
    if (speed == 3) check(gaps[0] == 60 && gaps[1] == 30, "3x is not steps 1, 1 and 2 retraces apart");
    return steps;
}

void speeds() {
    check(steps_per_second(1) == 30, "1x is not 30 steps a second");
    check(steps_per_second(2) == 60, "2x is not 60 steps a second");
    check(steps_per_second(3) == 90, "3x is not 90 steps a second");
    check(steps_per_second(4) == 120, "4x is not 120 steps a second");
}
// Pictures left undrawn (draw_skip.hpp): two seconds at 4x on a device that steps only every
// other retrace mark it slow, and then one display list in three is drawn. GAP: retraces
// between the steps the device manages.
void run_hurried(unsigned gap, unsigned seconds) {
    Ram ram;
    game_speed::level = 4;
    guest::write8(ram.data(), game_speed::mode, 2);
    guest::write32(ram.data(), game_speed::interval, 4);
    for (uint64_t tick = 1; tick <= 120 * seconds; ++tick) {
        game_speed::frame(ram.data());
        const uint32_t count = guest::read(ram.data(), sched_rate::logic_count, 4) + 1;
        guest::write32(ram.data(), sched_rate::logic_count, count);
        if (count >= guest::read(ram.data(), game_speed::interval, 4) && tick % gap == 0)
            guest::write32(ram.data(), sched_rate::logic_count, 0);
    }
}

void skipping() {
    Ram lists;
    const uint32_t plain = 0x80100000, sampling = 0x80100100;
    auto command = [&](uint32_t at, uint32_t w0, uint32_t w1) { guest::write32(lists.data(), at, w0); guest::write32(lists.data(), at + 4, w1); };
    command(plain, 0xFF10013F, 0x80300000);         // G_SETCIMG: 16-bit, 320 wide
    command(plain + 8, 0xFD100000, 0x80200000);     // G_SETTIMG: a texture
    command(plain + 16, 0xDF000000, 0);             // G_ENDDL
    command(sampling, 0xFF10013F, 0x80325800);
    command(sampling + 8, 0xDE010000, 0x80100200);  // G_DL, no push: a branch
    command(0x80100200, 0xFD100000, 0x80300100);    // a texture inside the first picture
    command(0x80100208, 0xDF000000, 0);
    const uint32_t physical = plain & 0x1FFFFFFF, sampled = sampling & 0x1FFFFFFF;

    run_hurried(1, 5);
    check(!draw_skip::slow && draw_skip::every == 1, "a device that keeps up at 4x is taken to be slow");
    check(!draw_skip::skip(lists.data(), physical), "a list goes undrawn on a device that keeps up");
    run_hurried(2, 1);
    check(!draw_skip::slow, "one slow second marks the device slow");
    run_hurried(2, 3);
    check(draw_skip::slow && draw_skip::every == 3, "4x on a slow device draws other than one list in three");
    unsigned drawn = 0;
    for (int i = 0; i < 9; ++i) {
        if (draw_skip::skip(lists.data(), physical)) check(draw_skip::swap_now.exchange(false), "an undrawn list's swap waits for a tick");
        else ++drawn;
    }
    check(drawn == 3, "not one list in three drawn");
    check(game_speed::picture_rate() == 40, "RT64 is not told 40 pictures a second");
    check(!draw_skip::skip(lists.data(), sampled) && draw_skip::holds == 1, "a list sampling a picture goes undrawn");
    for (int i = 0; i < 60; ++i) check(!draw_skip::skip(lists.data(), physical), "a list goes undrawn right after a sampled picture");
    check(draw_skip::skip(lists.data(), physical) || draw_skip::skip(lists.data(), physical), "lists drawn long after a sampled picture");
    game_speed::level = 2;
    Ram ram;
    guest::write8(ram.data(), game_speed::mode, 2);
    guest::write32(ram.data(), game_speed::interval, 2);
    game_speed::frame(ram.data());
    check(draw_skip::every == 1 && !draw_skip::skip(lists.data(), physical), "2x leaves lists undrawn");
}
}  // namespace

int main() {
    try {
        scenes();
        interval();
        speeds();
        skipping();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
    std::printf("game speed: %d checks\n", checks);
}
