// The Switch's platform layer (docs/design/switch-port.md, S0): what the runtime and the
// host ask of the system that libnx answers differently from the desktop.
#include <switch.h>

#include <string>
#include <unistd.h>

#include "ultramodern/ultramodern.hpp"

// Horizon has no call to name a thread, and ultramodern's game threads take turns (one
// runs at a time), so their priorities stay libnx's default for now.
namespace ultramodern {
void set_native_thread_name(const std::string&) {}
void set_native_thread_priority(ThreadPriority) {}
}

// Whether stdout and stderr go to nxlink (console_log.cpp keeps them there).
static int nxlink_socket = -1;
bool srw64_switch_nxlink() { return nxlink_socket >= 0; }

extern "C" {

// Before main and after it (libnx's weak hooks): the sockets, and stdout and stderr to
// nxlink on the Mac that pushed the NRO (tools/release/switch/README.txt).
void userAppInit() {
    if (R_SUCCEEDED(socketInitializeDefault())) nxlink_socket = nxlinkStdio();
}

void userAppExit() {
    if (nxlink_socket >= 0) close(nxlink_socket);
    socketExit();
}

}
