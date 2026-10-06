#pragma once
#include <filesystem>
#include <functional>
#include <string>

// How the debug interface (debug_server.cpp, docs/guide/debug-interface.md) is reached.
// Desktop (macOS, Linux, Windows): loopback TCP on a port the system picks.
// <run>/debug.json (owner-only) names the port and a random token, and the first line
// of every connection must be {"jsonrpc":"2.0","id":0,"method":"auth","params":{"token":T}};
// anything else closes it. The token stands in for the owner-only Unix socket this
// replaced: any local process can open a loopback port, only the player can read the file.
// Android: the abstract socket @srw64-debug, reached only through an adb forward
// (tools/release/android/attach.py), without a token.
namespace srw64::debug::transport {
using Handler=std::function<std::string(const std::string& line)>;
struct Endpoint {std::string address;std::filesystem::path file;};
// Listens, writes <run>/debug.json (removed again by stop() and at exit) and answers each
// request line with `handle` on background threads. A new port and token each time.
// Throws when it cannot listen. Not concurrently with stop().
Endpoint listen(const std::filesystem::path& run,Handler handle);
// Stops listening, ends every connection and removes debug.json (the About page's
// switch). Returns once the listening thread has let go of its socket.
void stop();
bool listening();
int process_id();
// Equal strings, in time that depends only on their lengths.
bool same_token(const std::string& a,const std::string& b);
}
