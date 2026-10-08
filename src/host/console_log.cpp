#include "console_log.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#endif
#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace srw64::console_log {
namespace {
#ifdef _WIN32
int sys_write(int fd, const char* data, size_t size) { return _write(fd, data, unsigned(size)); }
int sys_read(int fd, char* data, size_t size) { return _read(fd, data, unsigned(size)); }
#else
int sys_write(int fd, const char* data, size_t size) { return int(::write(fd, data, size)); }
int sys_read(int fd, char* data, size_t size) { return int(::read(fd, data, size)); }
#endif

constexpr size_t kHeld = 1 << 20;
std::mutex mutex;
std::string held;              // before attach
std::atomic<int> file{-1};     // console.log
// stdout and stderr: the pipe's read end, the original descriptor, the pump's end of file.
struct Stream {
    int target = -1, reader = -1, original = -1, priority = 0;
    std::atomic<bool> done{false};
};
Stream streams[2];

void keep(const char* data, size_t size) {
    std::lock_guard lock(mutex);
    if (const int fd = file.load(); fd >= 0) { (void)sys_write(fd, data, size); return; }
    if (held.size() + size <= kHeld) held.append(data, size);
}

void forward(Stream& s, const char* data, size_t size) {
#ifdef __ANDROID__
    // logcat takes lines; a chunk is cut at its newlines (a line split across two reads
    // shows as two entries, rarely).
    std::string text(data, size);
    for (size_t at = 0; at < text.size();) {
        const size_t end = std::min(text.find('\n', at), text.size());
        if (end > at) __android_log_write(s.priority, "SRW64", text.substr(at, end - at).c_str());
        at = end + 1;
    }
#else
    (void)sys_write(s.original, data, size);
#endif
}

void pump(Stream* s) {
    char buffer[4096];
    for (int n; (n = sys_read(s->reader, buffer, sizeof(buffer))) > 0;) {
        keep(buffer, size_t(n));
        forward(*s, buffer, size_t(n));
    }
    s->done = true;
}

bool redirect(Stream& s, int target, int priority) {
    int pipes[2];
#ifdef _WIN32
    if (_pipe(pipes, 65536, _O_BINARY) != 0) return false;
    const int original = _dup(target);
    if (original < 0 || _dup2(pipes[1], target) != 0) return false;
    _close(pipes[1]);
#else
    if (::pipe(pipes) != 0) return false;
    const int original = ::dup(target);
    if (original < 0 || ::dup2(pipes[1], target) < 0) return false;
    ::close(pipes[1]);
    // Children the game starts (the mini-stage compiler, the recording encoder) inherit
    // stdout and stderr, not these.
    ::fcntl(pipes[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(original, F_SETFD, FD_CLOEXEC);
#endif
    s.target = target;
    s.reader = pipes[0];
    s.original = original;
    s.priority = priority;
    std::thread(pump, &s).detach();
    return true;
}

// Normal exit: put stdout and stderr back, which closes the pipes' write ends, and let
// the pumps read to the end (at most 0.5 s: a child still holding them).
void drain() {
    std::fflush(stdout);
    std::fflush(stderr);
    for (auto& s : streams) {
        if (s.reader < 0) continue;
#ifdef _WIN32
        _dup2(s.original, s.target);
#else
        ::dup2(s.original, s.target);
#endif
    }
    for (int i = 0; i < 100; ++i) {
        bool done = true;
        for (auto& s : streams) done = done && (s.reader < 0 || s.done);
        if (done) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

#ifndef _WIN32
struct sigaction previous[32];
void on_fatal(int signal) {
    flush_now();
    sigaction(signal, &previous[signal], nullptr);
    raise(signal);
}
#else
void on_abort(int signal) {
    flush_now();
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}
#endif
}

void flush_now() {
    // Give a pump that already read a chunk the moment to pass it on.
#ifdef _WIN32
    Sleep(30);
#else
    timespec pause{0, 30'000'000};
    nanosleep(&pause, nullptr);
#endif
    char buffer[4096];
    for (auto& s : streams) {
        if (s.reader < 0) continue;
        for (int round = 0; round < 64; ++round) {
#ifdef _WIN32
            DWORD waiting = 0;
            if (!PeekNamedPipe(reinterpret_cast<HANDLE>(_get_osfhandle(s.reader)), nullptr, 0, nullptr, &waiting, nullptr) || !waiting) break;
            const int n = sys_read(s.reader, buffer, std::min<size_t>(waiting, sizeof(buffer)));
#else
            const int flags = ::fcntl(s.reader, F_GETFL);
            ::fcntl(s.reader, F_SETFL, flags | O_NONBLOCK);
            const int n = sys_read(s.reader, buffer, sizeof(buffer));
            ::fcntl(s.reader, F_SETFL, flags);
#endif
            if (n <= 0) break;
            if (const int fd = file.load(); fd >= 0) (void)sys_write(fd, buffer, size_t(n));
#ifdef __ANDROID__
            buffer[std::min(n, int(sizeof(buffer)) - 1)] = 0;
            __android_log_write(s.priority, "SRW64", buffer);
#else
            (void)sys_write(s.original, buffer, size_t(n));
#endif
        }
    }
}

int original_stderr() { return streams[1].reader >= 0 ? streams[1].original : 2; }

void start() {
    static bool started = false;
    if (started) return;
    started = true;
#ifdef _WIN32
    // The UCRT has no line buffering (_IOLBF is full buffering) and takes a size of 0 as
    // an invalid parameter, which ends the process before main() goes on: unbuffered.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef __ANDROID__
    redirect(streams[0], 1, ANDROID_LOG_INFO);
    redirect(streams[1], 2, ANDROID_LOG_INFO);
#else
    redirect(streams[0], 1, 0);
    redirect(streams[1], 2, 0);
#endif
    std::atexit(drain);
    // abort() (the host's fail(), failed assertions) and crashes kill the process before
    // the pumps pass on the last lines, which are the ones that matter: read them out
    // first, then let the signal do what it did before (Android: debuggerd's tombstone).
#ifndef _WIN32
    for (const int signal : {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE}) {
        struct sigaction action{};
        action.sa_handler = on_fatal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_ONSTACK;
        sigaction(signal, &action, &previous[signal]);
    }
#else
    std::signal(SIGABRT, on_abort);
#endif
}

void attach(const std::filesystem::path& run) {
    const auto path = run / "console.log";
#ifdef _WIN32
    const int fd = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
#endif
    if (fd < 0) return;
    std::lock_guard lock(mutex);
    if (file.load() >= 0) return;
    if (!held.empty()) (void)sys_write(fd, held.data(), held.size());
    held.clear();
    held.shrink_to_fit();
    file = fd;
}

int crash_fd() { return file.load(); }
}
