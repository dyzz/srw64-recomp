// Rewind: the kept states and when the game may be left (src/host/rewind.hpp, docs/design/rewind.md).
#include "../src/host/rewind.hpp"
#include "../src/host/input_bindings.hpp"
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

rewind::Entry state(uint64_t id, uint8_t mark, uint8_t header = 0) {
    rewind::Entry e;
    e.id = id;
    e.bytes = std::vector<uint8_t>(rewind::suspend_size, 0);
    e.bytes[0] = header;          // the checksum word does not count
    e.bytes[0x100] = mark;
    return e;
}

void ring() {
    rewind::Ring r(3);
    check(r.empty() && !r.target(false) && !r.target(true), "an empty ring has nothing to go back to");
    check(r.push(state(1, 1)).empty(), "the first state is kept");
    check(!r.target(true), "standing at the only state there is nothing before it");
    check(r.target(false) == 0u, "after an action the only state is the one to go back to");
    check(r.push(state(2, 1, 0x80)).empty() && r.entries().size() == 1, "a state equal to the newest but for its +0 word is not kept twice");
    check(r.matches_newest(state(9, 1, 0x7F).bytes), "the +0 word is the checksum, not the state");
    r.push(state(2, 2));
    r.push(state(3, 3));
    const auto gone = r.push(state(4, 4));
    check(gone == std::vector<uint64_t>{1} && r.entries().size() == 3 && r.entries().front().id == 2, "the oldest state falls off at the capacity");
    check(r.target(true) == 1u && r.entries()[1].id == 3, "standing at the newest goes back to the one before it");
    check(r.target(false) == 2u, "after an action the newest is the one to go back to");
    const auto future = r.keep_through(1);
    check(future == std::vector<uint64_t>{4} && r.entries().back().id == 3, "going back drops the states after it");
    check(r.matches_newest(state(7, 3).bytes), "the restored state is the newest now: its first idle is not kept again");
    check(r.target(true) == 0u && r.entries()[0].id == 2, "a second press goes further back");
    r.push(state(5, 5));
    check(r.entries().size() == 3 && r.entries().back().id == 5, "play goes on from the restored state");
    const auto all = r.clear();
    check(all.size() == 3 && r.empty(), "leaving the map forgets every state");
}

struct Ram {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x800000);
    uint8_t* data() { return bytes.data(); }
    // The tactical map with no event running.
    void map(unsigned ui, unsigned phase = 1) {
        guest::write8(data(), rewind::mode, 3);
        guest::write16(data(), rewind::engine + 4, 0xC0);
        guest::write16(data(), rewind::engine + 0x97C, 0x80);
        guest::write16(data(), rewind::engine + 0x9AA, 0);
        guest::write16(data(), rewind::defeat, 0);
        guest::write8(data(), rewind::progress, uint8_t(phase));
        guest::write8(data(), rewind::map_state, uint8_t(ui));
    }
    void battle(int state, int fade = -1, unsigned abort = 0) {
        guest::write8(data(), rewind::mode, 2);
        guest::write8(data(), rewind::battle_state, uint8_t(state));
        guest::write8(data(), rewind::battle_fade, uint8_t(fade));
        guest::write8(data(), rewind::battle_abort, uint8_t(abort));
    }
};

void map_points() {
    Ram ram;
    ram.map(5);
    check(rewind::quiet(ram.data()) && rewind::leavable(ram.data(), false), "the player's idle map can be left");
    check(!rewind::leavable(ram.data(), true), "not while a pre-battle screen steps");
    for (const unsigned menu : {6u, 8u, 0xCu, 0x16u, 0x17u, 0x3Au}) {
        ram.map(menu);
        check(rewind::leavable(ram.data(), false), "the player's menus can be left: " + std::to_string(menu));
    }
    ram.map(5, 2);
    check(rewind::leavable(ram.data(), false), "the enemy phase stands idle between its units");
    ram.map(0x18, 2);
    check(!rewind::leavable(ram.data(), false), "not while an enemy moves or fights on the map");
    ram.map(0x18, 1);
    check(!rewind::leavable(ram.data(), false), "not while the player's attack settles");
    ram.map(5);
    guest::write16(ram.data(), rewind::engine + 0x97C, 0x2000);
    check(!rewind::quiet(ram.data()) && !rewind::leavable(ram.data(), false), "not while an event script runs");
    ram.map(5);
    guest::write16(ram.data(), rewind::defeat, 1);
    check(!rewind::leavable(ram.data(), false), "not in the defeat sequence");
    ram.map(5);
    guest::write8(ram.data(), rewind::mode, 7);
    check(!rewind::quiet(ram.data()), "the title is not the map");
}

void battle_points() {
    Ram ram;
    ram.battle(1);
    check(!rewind::battle_abortable(ram.data()), "not before the actors are built (state 0 and 1)");
    for (int s = 2; s <= 20; ++s) {
        ram.battle(s);
        check(rewind::battle_abortable(ram.data()), "the animation can be left in state " + std::to_string(s));
    }
    ram.battle(21);
    check(!rewind::battle_abortable(ram.data()), "not once it winds down");
    ram.battle(9, 1);
    check(!rewind::battle_abortable(ram.data()), "not after X ended it early");
    ram.battle(9, -1, 1);
    check(!rewind::battle_abortable(ram.data()), "not twice");
    ram.battle(9);
    guest::write8(ram.data(), rewind::mode, 0x1C);
    check(!rewind::battle_abortable(ram.data()), "not the title's demo or the battle viewer");
}

void binding() {
    const auto b = input::default_bindings();
    const auto i = size_t(input::Action::Rewind);
    check(b.keys[i] == std::vector<int>{input::scancode::R}, "R rewinds");
    check(b.pads[i] == std::vector<input::PadInput>{input::button(input::pad_button::Paddle2)}, "the Deck's L4 rewinds");
    check(input::info(input::Action::Rewind).mask == input::pad_rewind && input::info(input::Action::Rewind).id == "rewind", "a host bit the game never sees");
    for (size_t a = 0; a < input::action_count; ++a)
        if (a != i) {
            for (const int key : b.keys[a]) check(key != input::scancode::R, "R is free");
            for (const auto& pad : b.pads[a]) check(!(pad == input::button(input::pad_button::Paddle2)), "L4 is free");
        }
}
}

int main() {
    try {
        ring();
        map_points();
        battle_points();
        binding();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "native-rewind FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("native-rewind: %d checks passed\n", checks);
    return 0;
}
