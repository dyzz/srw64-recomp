// 2x speed: which scenes it hurries (src/host/game_speed.hpp, docs/native/game-speed.md).
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
    for (const unsigned mode : {2u, 0x1Au, 0x1Cu, 12u, 13u}) {
        guest::write8(ram.data(), game_speed::mode, uint8_t(mode));
        check(game_speed::hurried(ram.data(), false), "the battle animation or the world map runs at 1x");
    }
    for (const unsigned mode : {1u, 4u, 5u, 6u, 7u, 0x12u, 0x20u, 0x21u}) {
        guest::write8(ram.data(), game_speed::mode, uint8_t(mode));
        check(!game_speed::hurried(ram.data(), false), "a menu screen or the intermission is hurried");
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

void interval() {
    Ram ram;
    game_speed::on = true;
    guest::write8(ram.data(), game_speed::mode, 2);
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 0, "written before the game set it");
    guest::write32(ram.data(), game_speed::interval, 2);
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 1 && game_speed::running, "not hurried in battle");
    game_speed::menu_step();
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 2, "hurried right after a pre-battle step");
    vi_now += 4;
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 1, "the pre-battle screen's hold outlasts it");
    game_speed::on = false;
    game_speed::frame(ram.data());
    check(guest::read(ram.data(), game_speed::interval, 4) == 2 && !game_speed::running, "off still hurries");
}
}  // namespace

int main() {
    try {
        scenes();
        interval();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
    std::printf("game speed: %d checks\n", checks);
}
