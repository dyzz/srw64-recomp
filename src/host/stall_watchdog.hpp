#pragma once
// A watchdog for a render thread that stops (2026-10-08: a Redmi K60 drew two frames and
// then hung, black, while the game ran on). When no display list has finished for five
// seconds while the VI clock still runs, every thread's stack goes into the log
// (console.log), down to the driver's functions, as module offsets a symbolizer reads.
// Android on arm64 only (frame-pointer walk from a signal); elsewhere these do nothing.
// The display-list count also clears the start marker of GraphicsGuard.java.
// Also when the lists go on but no screen has been presented for five seconds (2026-10-26:
// a Seeker's present thread held in the driver after Android took the surface away).
#include <cstdint>

namespace srw64::stall {
void start();               // once, after the runtime's clock runs
void frame_done();          // the render thread, after each display list
void present_done();        // the present thread, after each screen
void stop();                // at exit, the Switch: its thread joined (no-op elsewhere)
// Work that keeps the game thread for longer than the watchdog waits and is no stall: the
// dialogue text loaded again (a text update, a change of language) took over ten seconds on
// a Switch, and the watchdog ended the game for it (2026-11-22). While one of these lives
// the watchdog does not count.
struct Busy {
    Busy();
    ~Busy();
    Busy(const Busy&) = delete;
    Busy& operator=(const Busy&) = delete;
};
}
