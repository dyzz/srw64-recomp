#include "base_fixes.hpp"
#include <cassert>
int main() {
    using namespace srw64::base_fixes;
    // BUG05: only the player's own 100 pilot records keep a Wing pilot's kills;
    // the same field on sides 1 and 2 is the dummy count.
    assert(player_pilot(pilot_table));
    assert(player_pilot(pilot_table + 99 * 0x4C));
    assert(!player_pilot(pilot_table + pilot_side_stride));
    assert(!player_pilot(pilot_table + 2 * pilot_side_stride));
    assert(!player_pilot(pilot_table - 1));
    assert(!player_pilot(0));
    return 0;
}
