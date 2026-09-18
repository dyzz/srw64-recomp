#pragma once
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>

// Debug interface (docs/guide/debug-interface.md): with SRW64_DEBUG=1 the graphics
// host serves newline-delimited JSON-RPC 2.0 on <run>/debug.sock. The server
// thread answers requests; anything touching SDL or AppKit runs on the window
// thread through service_main().
namespace srw64::debug {
// Host start-up, before the runtime starts. Inert unless SRW64_DEBUG=1.
void start(const std::filesystem::path& output, nlohmann::json host);
// Window thread, every frame: runs queued window-thread work.
void service_main();
bool enabled();
}

// Provided by host.cpp: an immediate N64 button pulse and a quit that is
// recorded like the control file's.
void srw64_debug_buttons(uint16_t mask, uint64_t duration_vis);
void srw64_debug_quit();
