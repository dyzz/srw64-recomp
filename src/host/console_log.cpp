#include "console_log.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
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
std::string held;       // before attach
int file = -1;          // console.log
int readers[2] = {-1, -1};  // the pipes' read ends, for drain()

void keep(const char* data, size_t size) {
    std::lock_guard lock(mutex);
    if (file >= 0) { (void)sys_write(file, data, size); return; }
    if (held.size() + size <= kHeld) held.append(data, size);
}

// One stream: whatever arrives on `from` goes to `to` (the original descriptor) and to the log.
void pump(int from, [[maybe_unused]] int to, [[maybe_unused]] int priority) {
    char buffer[4096];
#ifdef __ANDROID__
    std::string line;
#endif
    for (int n; (n = sys_read(from, buffer, sizeof(buffer))) > 0;) {
        keep(buffer, size_t(n));
#ifdef __ANDROID__
        line.append(buffer, size_t(n));
        for (size_t end; (end = line.find('\n')) != std::string::npos; line.erase(0, end + 1))
            __android_log_write(priority, "SRW64", line.substr(0, end).c_str());
#else
        (void)sys_write(to, buffer, size_t(n));
#endif
    }
}

bool redirect(int target, int priority) {
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
#endif
    readers[target == 1 ? 0 : 1] = pipes[0];
    std::thread(pump, pipes[0], original, priority).detach();
    return true;
}

// At exit, give the pumps a moment (at most 0.2 s) to pass on what was printed last.
void drain() {
    std::fflush(stdout);
    std::fflush(stderr);
    for (int i = 0; i < 50; ++i) {
        bool empty = true;
#ifndef _WIN32
        for (const int fd : readers) {
            int waiting = 0;
            if (fd >= 0 && ::ioctl(fd, FIONREAD, &waiting) == 0 && waiting > 0) empty = false;
        }
#else
        empty = i > 0;
#endif
        if (empty) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
}
}

void start() {
    static bool started = false;
    if (started) return;
    started = true;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef __ANDROID__
    redirect(1, ANDROID_LOG_INFO);
    redirect(2, ANDROID_LOG_INFO);
#else
    redirect(1, 0);
    redirect(2, 0);
#endif
    std::atexit(drain);
}

void attach(const std::filesystem::path& run) {
    std::error_code error;
    std::filesystem::create_directories(run, error);
    const auto path = run / "console.log";
#ifdef _WIN32
    const int fd = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
#endif
    if (fd < 0) return;
    std::lock_guard lock(mutex);
    if (file >= 0) return;
    file = fd;
    if (!held.empty()) (void)sys_write(file, held.data(), held.size());
    held.clear();
    held.shrink_to_fit();
}

int crash_fd() { return file; }
}
